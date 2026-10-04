// Factory-owned sprite effects: one-shot effect bakes (spawn/hit/destroy/bounce/
// clear flashes) fired by volleys or by spawn_layer_effect, aged every physics
// frame. Volley trails live on the volley (bullet_volley2d_effects.cpp).

#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

//// SPRITE EFFECT (ONE-SHOT) MANAGER ////

// Zero-scale hide for effect instances (Transform2D has no
// (rotation, position, scale) constructor in godot-cpp).
static const Transform2D FX_HIDDEN_TRANSF = Transform2D(0.0, Vector2(0, 0)).scaled(Vector2(0, 0));

static double fx_clamp01(double v) {
	return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

// Ring size for one-shot bakes: explicit caps win, 0 means auto (one slot
// per bullet plus margin, 32 floor). Clamped hard: a misconfigured layer
// can never preallocate unbounded memory.
static int fx_ring_size_for_layer(const Ref<BulletEffectLayerData2D> &layer, int amount_bullets) {
	if (layer.is_valid() && layer->max_instances > 0) {
		return (int)Math::clamp((double)layer->max_instances, 1.0, 4096.0);
	}
	return (int)Math::clamp((double)Math::max(32, 2 * amount_bullets), 1.0, 4096.0);
}

// Continuous spin: rewrites the slot transform every frame from the fixed
// base pose plus spin at the slot's age (no-op when spin is 0, so static
// layers keep their write-once behavior). last_angle mirrors the write
// for debug (same headless-readback caveat as colors).
static void fx_apply_slot_spin(BulletFactory2D::FXOneShotBake &bake, int slot_index, double age, Node2D *container) {
	if (bake.layer.is_null() || bake.layer->spin_degrees_per_sec == 0.0 || !Math::is_finite(bake.layer->spin_degrees_per_sec) || !Math::is_finite(age)) {
		return;
	}
	if (slot_index < 0 || slot_index >= (int)bake.slots.size()) {
		return;
	}
	BulletFactory2D::FXOneShotSlot &slot = bake.slots[slot_index];
	if (!slot.active || slot.last_shard < 0 || slot.last_shard >= (int)bake.shards.size() || bake.shards[slot.last_shard] == nullptr) {
		return;
	}
	if (!slot.fixed.is_finite()) {
		return;
	}
	Transform2D spun = slot.fixed.rotated_local(Math::fposmod(bake.layer->spin_degrees_per_sec * Math::PI / 180.0 * age, Math::TAU));
	if (!spun.get_origin().is_finite()) {
		return;
	}
	Transform2D local = spun;
	if (container != nullptr) {
		const Transform2D node_global = container->get_global_transform();
		if (BulletVolley2D::is_transform_invertible_safe(node_global)) {
			local = node_global.affine_inverse() * spun;
		}
	}
	if (!local.get_origin().is_finite()) {
		return;
	}
	bake.shard_multimesh(slot.last_shard)->set_instance_transform_2d(slot_index, local);
	slot.last_angle = spun.get_rotation();
}

// Per-instance ramp sampling plus fade envelope (no-op without either):
// tint-over-life for one-shots, loop-phase tint for trails. Shard
// self_modulate multiplies on top, exactly like CanvasItem modulate chains.
// Trails never reach here with fades (their layers ignore the knobs, so a
// trail tick stays the pure ramp path it always was).
static void fx_refresh_slot_color(BulletFactory2D::FXOneShotBake &bake, int slot_index, double age) {
	if (bake.layer.is_null()) {
		return;
	}
	const bool has_ramp = !bake.layer->color_ramp.is_null() && bake.total > 0.0;
	const bool has_fade = (bake.layer->fade_in_sec > 0.0 || bake.layer->fade_out_sec > 0.0) && Math::is_finite(age) && bake.total > 0.0;
	if (!has_ramp && !has_fade) {
		return;
	}
	if (slot_index < 0 || slot_index >= (int)bake.slots.size()) {
		return;
	}
	BulletFactory2D::FXOneShotSlot &slot = bake.slots[slot_index];
	if (!slot.active || slot.last_shard < 0 || slot.last_shard >= (int)bake.shards.size() || bake.shards[slot.last_shard] == nullptr) {
		return;
	}
	Color tint(1, 1, 1, 1);
	if (has_ramp) {
		tint = bake.layer->color_ramp->sample((float)fx_clamp01(age / bake.total));
	}
	if (has_fade) {
		double alpha = 1.0;
		if (bake.layer->fade_in_sec > 0.0 && age < bake.layer->fade_in_sec) {
			alpha = age / bake.layer->fade_in_sec;
		}
		const double remaining = bake.total - age;
		if (bake.layer->fade_out_sec > 0.0 && remaining < bake.layer->fade_out_sec) {
			const double out_alpha = remaining / bake.layer->fade_out_sec;
			if (out_alpha < alpha) {
				alpha = out_alpha;
			}
		}
		tint.a *= (float)Math::clamp(alpha, 0.0, 1.0);
	}
	bake.shard_multimesh(slot.last_shard)->set_instance_color(slot_index, tint);
	slot.tint = tint;
}

Vector2 BulletFactory2D::fx_quad_size_for_texture(const Ref<Texture2D> &tex) {
	if (tex.is_valid()) {
		if (const Ref<AtlasTexture> atlas = tex; atlas.is_valid()) {
			const Vector2 region = atlas->get_region().size;
			if (region.x > 0.0f && region.y > 0.0f) {
				return region;
			}
		}
		const Vector2 size = tex->get_size();
		if (size.x > 0.0f && size.y > 0.0f) {
			return size;
		}
	}
	return Vector2(32, 32);
}

MultiMeshInstance2D *BulletFactory2D::fx_create_shard(Node *parent, const Ref<Texture2D> &tex, const Vector2 &quad_size, const Ref<Material> &mat, const Color &col, int z, bool z_rel, int vis, int light, int instance_count, bool with_colors) {
	MultiMeshInstance2D *shard = memnew(MultiMeshInstance2D);
	Ref<MultiMesh> mm;
	mm.instantiate();
	mm->set_transform_format(MultiMesh::TRANSFORM_2D);
	if (with_colors) {
		mm->set_use_colors(true);
	}
	Ref<QuadMesh> quad;
	quad.instantiate();
	quad->set_size(quad_size);
	mm->set_mesh(quad);
	// Same never-cull box as the bullet volleys: effects live all over the level.
	mm->set_custom_aabb(AABB(Vector3(-100000, -100000, -1000), Vector3(200000, 200000, 2000)));
	mm->set_instance_count(Math::max(1, instance_count));
	for (int i = 0; i < mm->get_instance_count(); ++i) {
		mm->set_instance_transform_2d(i, FX_HIDDEN_TRANSF);
		if (with_colors) {
			mm->set_instance_color(i, Color(1, 1, 1, 1));
		}
	}
	shard->set_multimesh(mm);
	shard->set_texture(tex);
	if (mat.is_valid()) {
		shard->set_material(mat);
	}
	shard->set_modulate(col);
	shard->set_z_index(z);
	shard->set_z_as_relative(z_rel);
	shard->set_visibility_layer(vis);
	shard->set_light_mask(light);
	shard->set_visible(false);
	parent->add_child(shard);
	return shard;
}

int BulletFactory2D::fx_frame_for_age(const std::vector<double> &starts, double total, double age) {
	if (starts.empty() || !(total > 0.0) || !Math::is_finite(age)) {
		return 0;
	}
	const double t = age - Math::floor(age / total) * total;
	// First frame whose end boundary passes t (bit-identical to the old
	// linear accumulation); past the end (float error) holds the last frame.
	size_t lo = 0;
	size_t hi = starts.size();
	while (lo < hi) {
		const size_t mid = lo + (hi - lo) / 2;
		if (t < starts[mid]) {
			hi = mid;
		} else {
			lo = mid + 1;
		}
	}
	if (lo >= starts.size()) {
		return (int)starts.size() - 1;
	}
	return (int)lo;
}

Ref<Texture2D> BulletFactory2D::get_whitened_frame(const Ref<Texture2D> &source) {
	if (source.is_null()) {
		return Ref<Texture2D>();
	}
	const uint64_t key = (uint64_t)source->get_instance_id();
	auto it = whitened_frame_cache.find(key);
	if (it != whitened_frame_cache.end()) {
		return it->second;
	}
	if (whitened_frame_cache.size() >= 4096) {
		whitened_frame_cache.clear();
	}
	Ref<Texture2D> result;
	Ref<Image> white = BulletEffectLayerData2D::whiten_image_copy(BulletEffectLayerData2D::read_frame_image(source));
	if (white.is_valid()) {
		Ref<ImageTexture> white_tex;
		white_tex.instantiate();
		white_tex->set_image(white);
		result = white_tex;
	}
	whitened_frame_cache[key] = result;
	return result;
}

void BulletFactory2D::fx_ensure_effects_container() {
	if (sprite_effects_container != nullptr) {
		return;
	}
	sprite_effects_container = memnew(Node2D);
	sprite_effects_container->set_name("SpriteEffectsContainer");
	add_child(sprite_effects_container);
}

// Frees one bake's shard nodes (zeroed first so this frame never renders
// stale) and drops the record. Queue_free is deferred-safe by contract, so
// teardown and reseed paths share it.
void BulletFactory2D::fx_erase_bake(FXOneShotBake &bake) {
	for (size_t s = 0; s < bake.shards.size(); ++s) {
		MultiMeshInstance2D *shard = bake.shards[s];
		if (shard == nullptr) {
			continue;
		}
		Ref<MultiMesh> mm = shard->get_multimesh();
		if (mm.is_valid()) {
			const Transform2D hidden = FX_HIDDEN_TRANSF;
			for (int i = 0; i < mm->get_instance_count(); ++i) {
				mm->set_instance_transform_2d(i, hidden);
			}
		}
		shard->queue_free();
	}
	bake.shards.clear();
	bake.shard_multimeshes.clear();
	bake.slots.clear();
	bake.frames.clear();
	bake.secs.clear();
	bake.frame_starts.clear();
	bake.active_count = 0;
	bake.ring_cursor = 0;
}

bool BulletFactory2D::fx_register_volley_bake(uint64_t volley_id, int layer_index, const Ref<BulletEffectLayerData2D> &layer, int amount_bullets) {
	if (volley_id == 0 || layer.is_null() || !layer->enabled) {
		return false;
	}
	if (layer->trigger == EFFECT_TRAIL_FOLLOW) {
		return false;
	}
	fx_unregister_volley_bake(volley_id, layer_index);
	fx_ensure_effects_container();
	if (sprite_effects_container == nullptr) {
		UtilityFunctions::push_error("BulletFactory2D: sprite effects container missing, one-shot layer not registered.");
		return false;
	}
	std::vector<Ref<Texture2D>> frames;
	std::vector<double> secs;
	double total = 0.0;
	if (!layer->bake_effect_frames(layer->animation, frames, secs, total)) {
		return false;
	}
	FXOneShotBake bake;
	bake.volley_id = volley_id;
	bake.layer_index = layer_index;
	bake.layer = layer;
	bake.frames = frames;
	bake.secs = secs;
	bake.frame_starts = fx_prefix_sums(secs);
	bake.total = total;
	const int ring = fx_ring_size_for_layer(layer, amount_bullets);
	bake.slots.assign(ring, FXOneShotSlot());
	for (size_t f = 0; f < frames.size(); ++f) {
		MultiMeshInstance2D *shard = fx_create_shard(sprite_effects_container, frames[f], fx_quad_size_for_texture(frames[f]), layer->material, layer->self_modulate, layer->z_index, layer->z_as_relative, layer->visibility_layer, layer->light_mask, ring, true);
		bake.add_shard(shard);
	}
	fx_bakes.push_back(bake);
	return true;
}

void BulletFactory2D::fx_unregister_volley(uint64_t volley_id) {
	fx_unregister_volley_bake(volley_id, -1);
}

void BulletFactory2D::fx_unregister_volley_bake(uint64_t volley_id, int layer_index) {
	for (size_t i = 0; i < fx_bakes.size();) {
		if (fx_bakes[i].volley_id == volley_id && (layer_index < 0 || fx_bakes[i].layer_index == layer_index)) {
			fx_erase_bake(fx_bakes[i]);
			fx_bakes.erase(fx_bakes.begin() + i);
		} else {
			++i;
		}
	}
}

void BulletFactory2D::fx_unregister_all_bakes() {
	for (size_t i = 0; i < fx_bakes.size(); ++i) {
		fx_erase_bake(fx_bakes[i]);
	}
	fx_bakes.clear();
	for (size_t i = 0; i < fx_manual_bakes.size(); ++i) {
		fx_erase_bake(fx_manual_bakes[i]);
	}
	fx_manual_bakes.clear();
	fx_clock = 0.0;
}

// Shared fire path for volley triggers and the manual hatch: chance gate,
// ring allocate (recycling the oldest slot), pose compose with layer
// offset plus randomization, first-frame write. Returns the slot index.
int BulletFactory2D::fx_fire_into_bake(FXOneShotBake &bake, const Transform2D &at) {
	if (bake.layer.is_null() || bake.frames.empty() || bake.shards.empty() || bake.slots.empty() || !(bake.total > 0.0)) {
		return -1;
	}
	if (!at.get_origin().is_finite() || !Math::is_finite(at.get_rotation())) {
		return -1;
	}
	BulletEffectLayerData2D *layer = bake.layer.ptr();
	if (layer->trigger_chance < 1.0) {
		if (!Math::is_finite((double)layer->trigger_chance) || UtilityFunctions::randf() > layer->trigger_chance) {
			return -1;
		}
	}
	FXOneShotSlot &slot = bake.slots[bake.ring_cursor];
	const int slot_index = bake.ring_cursor;
	bake.ring_cursor = (bake.ring_cursor + 1) % (int)bake.slots.size();
	if (slot.active && slot.last_shard >= 0 && slot.last_shard < (int)bake.shards.size() && bake.shards[slot.last_shard] != nullptr) {
		bake.shard_multimesh(slot.last_shard)->set_instance_transform_2d(slot_index, FX_HIDDEN_TRANSF);
	} else if (!slot.active) {
		++bake.active_count;
	}
	Transform2D t(at.get_rotation(), at.get_origin() + layer->offset.rotated(at.get_rotation()));
	if (Math::is_finite(layer->rotation_degrees) && layer->rotation_degrees != 0.0) {
		t = t.rotated_local(layer->rotation_degrees * Math::PI / 180.0);
	}
	if (layer->randomize_rotation) {
		t = t.rotated_local(UtilityFunctions::randf_range(0.0f, (float)Math::TAU));
	}
	Vector2 s = layer->scale;
	if (!s.is_finite()) {
		s = Vector2(1, 1);
	}
	if (layer->random_scale_max > layer->random_scale_min && Math::is_finite(layer->random_scale_min) && Math::is_finite(layer->random_scale_max)) {
		const float mul = UtilityFunctions::randf_range((float)layer->random_scale_min, (float)layer->random_scale_max);
		if (Math::is_finite((double)mul) && mul > 0.0f) {
			s *= mul;
		}
	}
	if (s.is_finite()) {
		t = t.scaled_local(s);
	}
	slot.fixed = t;
	slot.birth = fx_clock;
	slot.duration = bake.total;
	slot.start_age = (layer->random_start_frame && bake.total > 0.0) ? (double)UtilityFunctions::randf_range(0.0f, (float)bake.total) : 0.0;
	slot.active = true;
	const int frame = fx_frame_for_age(bake.frame_starts, bake.total, slot.start_age);
	slot.last_shard = frame;
	// Spin starts at the slot's own age (usually 0): the written pose is
	// the base spun forward, and last_angle mirrors it for debug.
	Transform2D spun = t;
	if (layer->spin_degrees_per_sec != 0.0 && Math::is_finite(layer->spin_degrees_per_sec) && Math::is_finite(slot.start_age)) {
		spun = spun.rotated_local(Math::fposmod(layer->spin_degrees_per_sec * Math::PI / 180.0 * slot.start_age, Math::TAU));
	}
	slot.last_angle = spun.is_finite() ? spun.get_rotation() : t.get_rotation();
	if (frame >= 0 && frame < (int)bake.shards.size() && bake.shards[frame] != nullptr) {
		Transform2D local = spun;
		if (sprite_effects_container != nullptr) {
			const Transform2D node_global = sprite_effects_container->get_global_transform();
			if (BulletVolley2D::is_transform_invertible_safe(node_global)) {
				// MUST convert the SPUN pose, matching the aging path
				// (fx_apply_slot_spin). Converting the unspun base here wrote a
				// pose that disagreed with slot.last_angle and with every
				// later frame, so a spinning one-shot visibly snapped by
				// spin * start_age on its second frame.
				local = node_global.affine_inverse() * spun;
			}
		}
		if (local.get_origin().is_finite()) {
			bake.shard_multimesh(frame)->set_instance_transform_2d(slot_index, local);
			bake.shards[frame]->set_visible(true);
			fx_refresh_slot_color(bake, slot_index, slot.start_age);
		}
	}
	return slot_index;
}

void BulletFactory2D::fx_fire(uint64_t volley_id, int layer_index, const Transform2D &at) {
	if (volley_id == 0) {
		return;
	}
	for (size_t i = 0; i < fx_bakes.size(); ++i) {
		if (fx_bakes[i].volley_id == volley_id && fx_bakes[i].layer_index == layer_index) {
			fx_fire_into_bake(fx_bakes[i], at);
			return;
		}
	}
}

void BulletFactory2D::age_fx_effects(double delta) {
	if (!Math::is_finite(delta) || delta < 0.0) {
		return;
	}
	fx_clock += delta;
	// Index loops with per-access size (same pattern as the timer loops):
	// user code never runs here, but manual-hatch registration can append
	// bakes from any GDScript context, so never hold a reference across it.
	for (size_t b = 0; b < fx_bakes.size(); ++b) {
		// Shared worker with the manual bakes below: one maintenance point
		// for slot aging + occupancy. Index loop (not a reference held
		// across calls): aging never grows the vector itself.
		age_fx_bake(fx_bakes[b]);
	}
	for (size_t m = 0; m < fx_manual_bakes.size(); ++m) {
		age_fx_bake(fx_manual_bakes[m]);
	}
}

void BulletFactory2D::age_fx_bake(FXOneShotBake &bake) {
	if (bake.active_count <= 0 || bake.slots.empty() || bake.shards.empty()) {
		return;
	}
	for (size_t s = 0; s < bake.slots.size(); ++s) {
		FXOneShotSlot &slot = bake.slots[s];
		if (!slot.active) {
			continue;
		}
		const double age = slot.start_age + (fx_clock - slot.birth);
		if (!Math::is_finite(age) || age >= slot.duration) {
			if (slot.last_shard >= 0 && slot.last_shard < (int)bake.shards.size() && bake.shards[slot.last_shard] != nullptr) {
				bake.shard_multimesh(slot.last_shard)->set_instance_transform_2d((int)s, FX_HIDDEN_TRANSF);
			}
			slot.active = false;
			slot.last_shard = -1;
			--bake.active_count;
			if (bake.active_count < 0) {
				bake.active_count = 0;
			}
			continue;
		}
		if (age < 0.0) {
			continue;
		}
		fx_refresh_slot_color(bake, (int)s, age);
		fx_apply_slot_spin(bake, (int)s, age, sprite_effects_container);
		const int frame = fx_frame_for_age(bake.frame_starts, bake.total, age);
		if (frame != slot.last_shard && frame >= 0 && frame < (int)bake.shards.size()) {
			if (slot.last_shard >= 0 && slot.last_shard < (int)bake.shards.size() && bake.shards[slot.last_shard] != nullptr) {
				bake.shard_multimesh(slot.last_shard)->set_instance_transform_2d((int)s, FX_HIDDEN_TRANSF);
			}
			slot.last_shard = frame;
			if (bake.shards[frame] != nullptr) {
				Transform2D local = slot.fixed;
				if (sprite_effects_container != nullptr) {
					const Transform2D node_global = sprite_effects_container->get_global_transform();
					if (BulletVolley2D::is_transform_invertible_safe(node_global)) {
						local = node_global.affine_inverse() * slot.fixed;
					}
				}
				if (local.get_origin().is_finite()) {
					bake.shard_multimesh(frame)->set_instance_transform_2d((int)s, local);
					bake.shards[frame]->set_visible(true);
				}
			}
		}
	}
	fx_occupancy_scratch.assign(bake.shards.size(), 0);
	std::vector<int> &occupancy = fx_occupancy_scratch;
	for (size_t s = 0; s < bake.slots.size(); ++s) {
		if (bake.slots[s].active && bake.slots[s].last_shard >= 0 && bake.slots[s].last_shard < (int)occupancy.size()) {
			++occupancy[bake.slots[s].last_shard];
		}
	}
	for (size_t s = 0; s < bake.shards.size(); ++s) {
		if (bake.shards[s] != nullptr) {
			bake.shards[s]->set_visible(occupancy[s] > 0);
		}
	}
}

int BulletFactory2D::get_active_effect_count() const {
	int total = 0;
	for (size_t i = 0; i < fx_bakes.size(); ++i) {
		if (fx_bakes[i].active_count > 0) {
			total += fx_bakes[i].active_count;
		}
	}
	for (size_t i = 0; i < fx_manual_bakes.size(); ++i) {
		if (fx_manual_bakes[i].active_count > 0) {
			total += fx_manual_bakes[i].active_count;
		}
	}
	return total;
}

int BulletFactory2D::spawn_layer_effect(const Ref<BulletEffectLayerData2D> &layer, const Transform2D &at) {
	if (layer.is_null() || !layer->enabled) {
		UtilityFunctions::push_error("BulletFactory2D.spawn_layer_effect: layer is null or disabled.");
		return -1;
	}
	for (size_t i = 0; i < fx_manual_bakes.size(); ++i) {
		// Same resource, edited content: the stored generation trails the
		// layer's, so drop the stale bake and rebuild below.
		if (fx_manual_bakes[i].layer == layer && fx_manual_bakes[i].layer_version != layer->get_bake_version()) {
			fx_erase_bake(fx_manual_bakes[i]);
			fx_manual_bakes.erase(fx_manual_bakes.begin() + i);
			break;
		}
		if (fx_manual_bakes[i].layer == layer) {
			return fx_fire_into_bake(fx_manual_bakes[i], at);
		}
	}
	// New distinct layer: cap the manual set, oldest bake drops (with its
	// live visuals) so mixed hatches coexist without unbounded growth.
	if (fx_manual_bakes.size() >= 8) {
		fx_erase_bake(fx_manual_bakes[0]);
		fx_manual_bakes.erase(fx_manual_bakes.begin());
	}
	fx_ensure_effects_container();
	if (sprite_effects_container == nullptr) {
		UtilityFunctions::push_error("BulletFactory2D: sprite effects container missing, manual effect not spawned.");
		return -1;
	}
	std::vector<Ref<Texture2D>> frames;
	std::vector<double> secs;
	double total = 0.0;
	if (!layer->bake_effect_frames(layer->animation, frames, secs, total)) {
		return -1;
	}
	FXOneShotBake bake;
	bake.volley_id = 0;
	bake.layer_index = -1;
	bake.layer = layer;
	bake.layer_version = layer->get_bake_version();
	bake.frames = frames;
	bake.secs = secs;
	bake.frame_starts = fx_prefix_sums(secs);
	bake.total = total;
	const int ring = fx_ring_size_for_layer(layer, 32);
	bake.slots.assign(ring, FXOneShotSlot());
	for (size_t f = 0; f < frames.size(); ++f) {
		bake.add_shard(fx_create_shard(sprite_effects_container, frames[f], fx_quad_size_for_texture(frames[f]), layer->material, layer->self_modulate, layer->z_index, layer->z_as_relative, layer->visibility_layer, layer->light_mask, ring, true));
	}
	fx_manual_bakes.push_back(bake);
	return fx_fire_into_bake(fx_manual_bakes[fx_manual_bakes.size() - 1], at);
}

void BulletFactory2D::clear_sprite_effects() {
	// Volley bakes keep their configs (triggers keep working); only live
	// visuals stop. The manual bake is dropped entirely.
	for (size_t i = 0; i < fx_bakes.size(); ++i) {
		FXOneShotBake &bake = fx_bakes[i];
		for (size_t s = 0; s < bake.slots.size(); ++s) {
			bake.slots[s].active = false;
			bake.slots[s].last_shard = -1;
		}
		for (size_t s = 0; s < bake.shards.size(); ++s) {
			if (bake.shards[s] != nullptr) {
				Ref<MultiMesh> mm = bake.shards[s]->get_multimesh();
				if (mm.is_valid()) {
					const Transform2D hidden = FX_HIDDEN_TRANSF;
					for (int k = 0; k < mm->get_instance_count(); ++k) {
						mm->set_instance_transform_2d(k, hidden);
					}
				}
				bake.shards[s]->set_visible(false);
			}
		}
		bake.active_count = 0;
		bake.ring_cursor = 0;
	}
	for (size_t i = 0; i < fx_manual_bakes.size(); ++i) {
		fx_erase_bake(fx_manual_bakes[i]);
	}
	fx_manual_bakes.clear();
}

void BulletFactory2D::debug_set_effect_log_enabled(bool enabled) {
	debug_effect_log_enabled = enabled;
	if (!enabled) {
		debug_effect_log.clear();
	}
}

void BulletFactory2D::debug_log_effect(int trigger, uint64_t volley_id, int bullet_index, const Vector2 &position) {
	if (!debug_effect_log_enabled) {
		return;
	}
	if (debug_effect_log.size() >= 4096) {
		debug_effect_log.remove_at(0);
	}
	Dictionary e;
	e["trigger"] = trigger;
	e["volley"] = (int64_t)volley_id;
	e["bullet"] = bullet_index;
	e["position"] = position;
	e["frame"] = (int64_t)Engine::get_singleton()->get_physics_frames();
	debug_effect_log.push_back(e);
}

Dictionary BulletFactory2D::debug_get_effect_state() const {
	Dictionary d;
	d["clock"] = fx_clock;
	d["bake_count"] = (int)fx_bakes.size();
	d["active_total"] = get_active_effect_count();
	int manual_active = 0;
	int manual_frames = 0;
	for (size_t i = 0; i < fx_manual_bakes.size(); ++i) {
		manual_active += fx_manual_bakes[i].active_count;
		manual_frames += (int)fx_manual_bakes[i].frames.size();
	}
	d["manual_active"] = manual_active;
	d["manual_frames"] = manual_frames;
	d["manual_bakes"] = (int)fx_manual_bakes.size();
	d["container_valid"] = sprite_effects_container != nullptr;
	Array bakes;
	for (size_t i = 0; i < fx_bakes.size(); ++i) {
		const FXOneShotBake &bake = fx_bakes[i];
		Dictionary e;
		e["volley"] = (int64_t)bake.volley_id;
		e["layer"] = bake.layer_index;
		e["active"] = bake.active_count;
		e["slots"] = (int)bake.slots.size();
		if (!bake.shards.empty() && bake.shards[0] != nullptr) {
			e["z_index"] = bake.shards[0]->get_z_index();
			e["visibility_layer"] = bake.shards[0]->get_visibility_layer();
			e["light_mask"] = bake.shards[0]->get_light_mask();
		}
		int busy = 0;
		std::vector<uint8_t> seen(bake.shards.size(), 0);
		for (size_t s = 0; s < bake.slots.size(); ++s) {
			if (bake.slots[s].active && bake.slots[s].last_shard >= 0 && bake.slots[s].last_shard < (int)seen.size() && !seen[bake.slots[s].last_shard]) {
				seen[bake.slots[s].last_shard] = 1;
				++busy;
			}
		}
		e["shards_busy"] = busy;
		Array live;
		for (size_t s = 0; s < bake.slots.size(); ++s) {
			if (!bake.slots[s].active) {
				continue;
			}
			Dictionary row;
			row["slot"] = (int)s;
			row["shard"] = bake.slots[s].last_shard;
			row["tint"] = bake.slots[s].tint;
			row["rotation"] = bake.slots[s].last_angle;
			live.push_back(row);
		}
		e["live_slots"] = live;
		bakes.push_back(e);
	}
	d["bakes"] = bakes;
	return d;
}

} // namespace BlastBullets2D
