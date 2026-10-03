#include "../shared/warn_once2d.hpp"
#include "../shared/cached_string_names2d.hpp"
#include "./directional_bullets2d.hpp"
#include "../factory/bullet_factory2d.hpp"
#include "../shared/multimesh_object_pool2d.hpp"

#include "godot_cpp/classes/curve.hpp"
#include "godot_cpp/classes/curve2d.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/print_string.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "directional_bullets2d.hpp"
#include "shared/bullet_curves_data2d.hpp"
#include "shared/bullet_movement_pattern_data2d.hpp"
#include "shared/collision_shape_helper2d.hpp"
#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

void DirectionalBullets2D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PREDELETE: {
			// The destructor also runs for editor-time instances, which have no runtime state.
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}

			if (!marked_for_internal_deletion && bullet_factory) {
				bullet_factory->handle_manual_user_deletion_of_multimesh_bullets(*this);
			}

			clear_homing_state_for_teardown();

			// Sprite effect teardown first: factory one-shot bakes keyed by
			// this volley die here (in-flight visuals stop), trail shard
			// children are freed with the node below. Safe during factory
			// teardown too (its vectors were already cleared, unregister
			// no-ops, and our own children are still alive).
			if (bullet_factory != nullptr) {
				bullet_factory->fx_unregister_volley(get_instance_id());
			}
			fx_clear_trail_layers();

			if (physics_server && area.is_valid()) {
				// Disable the area's shapes (ALL OF THEM no matter their bullets_enabled_status).
				// Bounds-checked: never let a desynced attachments vector take down PREDELETE.
				// When the factory itself is tearing down, only run the script callback and
				// drop the slot: re-pooling into (or queue_freeing from) a dying factory is
				// pointless, the engine destroys the whole subtree anyway.
				const bool factory_is_dying = bullet_factory == nullptr || bullet_factory->get_is_tearing_down();
				for (int i = 0; i < amount_bullets && i < area_shape_count; ++i) {
					physics_server->area_set_shape_disabled(area, i, true);

					if (i >= 0 && i < (int)attachments.size() && attachments[i] != nullptr) {
						if (factory_is_dying) {
							// Null the slot BEFORE the callback (same ordering as
							// bullet_disable_attachment): re-entrant API calls from the
							// script must see an empty slot.
							BulletAttachment2D *detaching = attachments[i];
							attachments[i] = nullptr;
							// Every ownership change bumps the slot epoch, including
							// this teardown path, so the "bumped on every change"
							// invariant holds without exceptions.
							bump_attachment_epoch(i);
							// Owner tracking cleared like bullet_disable_attachment:
							// attachments outlive this multimesh (siblings in the
							// container), and a stale owner id would make their
							// PREDELETE resolve a dead multimesh.
							detaching->owner_multimesh_id = 0;
							detaching->owner_bullet_index = -1;
							detaching->call_on_bullet_disable();
						} else {
							bullet_disable_attachment(i);
						}
					}
				}

				physics_server->area_set_area_monitor_callback(area, Variant());
				physics_server->area_set_monitor_callback(area, Variant());

				// Detach the shapes from the area BEFORE freeing the RID (freeing a
				// still-attached shape RID warns/leaks on the physics server).
				release_volley_shape();

				if (area.is_valid()) {
					physics_server->free_rid(area);
				}
				area = RID();
			}
		} break;
	}
}

int DirectionalBullets2D::get_amount_active_attachments() const {
	int amount_active_attachments = 0;

	// min(): the vector is sized to amount_bullets by spawn(), but this can be
	// called on a not-yet-spawned instance through debug helpers.
	const int count = Math::min((int)attachments.size(), amount_bullets);
	for (int i = 0; i < count; ++i) {
		if (attachments[i] != nullptr) {
			++amount_active_attachments;
		}
	}

	return amount_active_attachments;
}

Dictionary DirectionalBullets2D::debug_get_volley_info() const {
	Dictionary d;
	d["amount_bullets"] = amount_bullets;
	d["active_bullets"] = active_bullets_counter;
	d["generation"] = multimesh_generation;
	d["owner_spawner_id"] = (int64_t)owner_spawner_id;
	d["is_active"] = is_active;
	d["is_pooled"] = is_pooled_in_pool;
	const PoolKey k = get_pool_key();
	d["pool_amount"] = k.amount_bullets;
	d["pool_shape"] = (int)k.shape_type;
	d["auto_pool_multimesh"] = is_multimesh_auto_pooling_enabled;
	d["auto_pool_attachments"] = is_attachments_auto_pooling_enabled;
	d["self_modulate"] = get_self_modulate();
	return d;
}

Dictionary DirectionalBullets2D::debug_get_shape_state() const {
	Dictionary d;
	d["valid"] = physics_server != nullptr && area.is_valid() && volley_shape.is_valid() && area_shape_count == amount_bullets;
	d["type"] = (int)cached_effective_shape_type;
	d["circle_radius"] = cached_circle_radius;
	d["rect_size"] = cached_rect_size;
	d["capsule_radius"] = cached_capsule_radius;
	d["capsule_height"] = cached_capsule_height;
	// One shared server shape per volley (see volley_shape); shape_count is
	// the number of area shape slots (one per bullet).
	d["rid_count"] = volley_shape.is_valid() ? 1 : 0;
	d["shape_count"] = area_shape_count;
	return d;
}

Dictionary DirectionalBullets2D::debug_get_attachment_info(int bullet_index) const {
	Dictionary d;
	d["has_attachment"] = false;
	d["pooling_id"] = 0;
	d["owner_match"] = false;
	if (bullet_index < 0 || bullet_index >= amount_bullets || bullet_index >= (int)attachments.size() || bullet_index >= (int)attachment_pooling_ids.size()) {
		return d;
	}
	BulletAttachment2D *a = attachments[bullet_index];
	if (a == nullptr) {
		return d;
	}
	d["has_attachment"] = true;
	d["pooling_id"] = (int64_t)attachment_pooling_ids[bullet_index];
	// Owner ids must point back here; a stale owner after pool reuse fails.
	// Never dereferences a: ids are plain values, compared by value.
	d["owner_match"] = a->owner_multimesh_id == get_instance_id() && a->owner_bullet_index == bullet_index;
	return d;
}

void DirectionalBullets2D::reset_attachment_state_for_reuse() {
	// Force-disable any surviving slot first. Deferred attachment disables can be
	// dropped by a generation bump (e.g. lifetime expiry pooled this instance and a
	// spawn re-enabled it before the deferred flush ran), so a new owner must never
	// be able to observe, disable or re-pool a previous owner's attachment.
	for (int i = 0; i < (int)attachments.size(); ++i) {
		if (attachments[i] != nullptr) {
			bullet_disable_attachment(i);
		}
	}

	const int count = amount_bullets;
	attachment_pooling_ids.assign(count, 0);
	attachments.assign(count, nullptr);
	// New life, new assignment history: stale deferred disables (which carry
	// the old epoch) can never match the fresh slots.
	attachment_assignment_epochs.assign(count, 0);
	signal_protected_attachment_slot = -1;
	attachment_transforms.assign(count, Transform2D());
	attachment_offsets.assign(count, Vector2());
	attachment_local_transforms.assign(count, Transform2D());
	attachment_stick_relative_to_bullet.assign(count, 1);
	all_previous_attachment_transf.assign(count, Transform2D());
}

//// SPRITE EFFECT (FX LAYER) TRAILS ////

// Bakes one trail layer's frames and builds one shard node per frame
// (children of the volley: pooling hides them, freeing is automatic, and
// relative z tracks the volley). Shard textures never change, so per-tick
// work is a single instance write into the current frame's shard.
void DirectionalBullets2D::fx_rebuild_trail_layers(const TypedArray<BulletEffectLayerData2D> &layers) {
	fx_clear_trail_layers();
	if (amount_bullets <= 0) {
		return;
	}
	for (int li = 0; li < layers.size(); ++li) {
		Ref<BulletEffectLayerData2D> layer = layers[li];
		if (layer.is_null() || !layer->enabled) {
			continue;
		}
		if (layer->trigger != EFFECT_TRAIL_FOLLOW) {
			continue;
		}
		FXTrailBake bake;
		if (!layer->bake_effect_frames(layer->animation, bake.frames, bake.secs, bake.total)) {
			continue;
		}
		bake.frame_starts.clear();
		bake.frame_starts.reserve(bake.secs.size());
		{
			double acc = 0.0;
			for (double s : bake.secs) {
				acc += s;
				bake.frame_starts.push_back(acc);
			}
		}
		bake.layer = layer;
		bake.layer_index = li;
		for (size_t f = 0; f < bake.frames.size(); ++f) {
			MultiMeshInstance2D *shard = BulletFactory2D::fx_create_shard(this, bake.frames[f], BulletFactory2D::fx_quad_size_for_texture(bake.frames[f]), layer->material, layer->self_modulate, layer->z_index, layer->z_as_relative, layer->visibility_layer, layer->light_mask, amount_bullets, true);
			bake.shards.push_back(shard);
			bake.shard_multimeshes.push_back(shard != nullptr ? shard->get_multimesh() : Ref<MultiMesh>());
		}
		bake.shard_visible.assign(bake.shards.size(), 0);
		bake.shard_refcount.assign(bake.shards.size(), 0);
		bake.phase.assign(amount_bullets, 0.0);
		if (layer->random_start_frame && bake.total > 0.0) {
			for (int i = 0; i < amount_bullets; ++i) {
				bake.phase[i] = (double)UtilityFunctions::randf_range(0.0f, (float)bake.total);
			}
		}
		bake.bullet_on.assign(amount_bullets, 1);
		bake.bullet_shard.assign(amount_bullets, -1);
		bake.bullet_trail_transf.assign(amount_bullets, Transform2D());
		bake.bullet_trail_tint.assign(amount_bullets, Color(1, 1, 1, 1));
		fx_trail_bakes.push_back(bake);
	}
}

void DirectionalBullets2D::fx_clear_trail_layers() {
	for (size_t b = 0; b < fx_trail_bakes.size(); ++b) {
		for (size_t s = 0; s < fx_trail_bakes[b].shards.size(); ++s) {
			MultiMeshInstance2D *shard = fx_trail_bakes[b].shards[s];
			if (shard == nullptr) {
				continue;
			}
			// Zero first: a queued-free node still renders this frame.
			Ref<MultiMesh> mm = shard->get_multimesh();
			if (mm.is_valid()) {
				for (int i = 0; i < mm->get_instance_count(); ++i) {
					mm->set_instance_transform_2d(i, zero_transform);
				}
			}
			shard->queue_free();
		}
	}
	fx_trail_bakes.clear();
}

// Full reseed from a layer list: retains it for trigger routing, rebuilds
// trail shards, and re-registers factory one-shot bakes (erasing the
// previous life's). fire_spawn flashes ON_SPAWN layers at every bullet.
bool DirectionalBullets2D::fx_layers_match_seeded(const TypedArray<BulletEffectLayerData2D> &layers) const {
	if (layers.size() != fx_data_layers.size() || (int)fx_seeded_snapshots.size() != layers.size()) {
		return false;
	}
	for (int li = 0; li < layers.size(); ++li) {
		Ref<BulletEffectLayerData2D> incoming = layers[li];
		Ref<BulletEffectLayerData2D> seeded = fx_data_layers[li];
		if (incoming != seeded) {
			return false;
		}
		if (!(fx_snapshot_layer(incoming) == fx_seeded_snapshots[li])) {
			return false;
		}
	}
	// Trail shards must still exist exactly as baked (a play_effect_animation
	// swap rebuilt them with other frames: rebuild to restore the layer's).
	for (const FXTrailBake &bake : fx_trail_bakes) {
		if (bake.layer.is_null() || bake.shards.empty() || bake.animation_override) {
			return false;
		}
		for (MultiMeshInstance2D *shard : bake.shards) {
			if (shard == nullptr || shard->is_queued_for_deletion()) {
				return false;
			}
		}
		if ((int)bake.bullet_shard.size() != amount_bullets) {
			return false;
		}
	}
	return true;
}

void DirectionalBullets2D::fx_soft_reset_trail_layers() {
	for (FXTrailBake &bake : fx_trail_bakes) {
		for (int i = 0; i < (int)bake.bullet_shard.size(); ++i) {
			const int shard_index = bake.bullet_shard[i];
			if (shard_index >= 0 && shard_index < (int)bake.shards.size() && bake.shards[shard_index] != nullptr) {
				bake.shard_multimesh(shard_index)->set_instance_transform_2d(i, zero_transform);
			}
		}
		for (size_t s = 0; s < bake.shards.size(); ++s) {
			if (bake.shards[s] != nullptr && s < bake.shard_visible.size() && bake.shard_visible[s]) {
				bake.shards[s]->set_visible(false);
			}
		}
		bake.shard_visible.assign(bake.shards.size(), 0);
		bake.shard_refcount.assign(bake.shards.size(), 0);
		bake.bullet_shard.assign(amount_bullets, -1);
		bake.bullet_on.assign(amount_bullets, 1);
		bake.bullet_trail_transf.assign(amount_bullets, Transform2D());
		bake.bullet_trail_tint.assign(amount_bullets, Color(1, 1, 1, 1));
		bake.phase.assign(amount_bullets, 0.0);
		if (bake.layer->random_start_frame && bake.total > 0.0) {
			for (int i = 0; i < amount_bullets; ++i) {
				bake.phase[i] = (double)UtilityFunctions::randf_range(0.0f, (float)bake.total);
			}
		}
	}
}

void DirectionalBullets2D::fx_reseed_from_data(const TypedArray<BulletEffectLayerData2D> &layers, bool fire_spawn) {
	// Pooled-reuse fast path: same layer resources at the same bake
	// versions. Shard nodes and factory one-shot bakes are kept (the old
	// rebuild queue_freed and re-created them on every spawn, and erasing
	// the factory bakes cut the previous life's in-flight one-shots, e.g.
	// a death explosion, the moment the pooled volley was reused).
	if (fx_layers_match_seeded(layers)) {
		fx_soft_reset_trail_layers();
		if (fire_spawn) {
			fx_fire_spawn_layers();
		}
		return;
	}
	fx_data_layers = layers;
	// One-shot trigger mask: only layers registered as factory bakes below
	// (enabled, non-trail) can ever fire, so a trigger missing here makes
	// fx_fire_oneshot a single bit test instead of a per-layer Variant
	// unbox + Ref copy on every bullet disable / hit / spawn.
	fx_oneshot_trigger_mask = 0;
	for (int li = 0; li < layers.size(); ++li) {
		Ref<BulletEffectLayerData2D> layer = layers[li];
		if (layer.is_valid() && layer->enabled && layer->trigger != EFFECT_TRAIL_FOLLOW && layer->trigger >= 0 && layer->trigger < 32) {
			fx_oneshot_trigger_mask |= (1u << layer->trigger);
		}
	}
	fx_seeded_snapshots.resize(layers.size());
	for (int li = 0; li < layers.size(); ++li) {
		fx_seeded_snapshots[li] = fx_snapshot_layer(layers[li]);
	}
	fx_rebuild_trail_layers(layers);
	if (bullet_factory != nullptr) {
		bullet_factory->fx_unregister_volley(get_instance_id());
		for (int li = 0; li < layers.size(); ++li) {
			Ref<BulletEffectLayerData2D> layer = layers[li];
			if (layer.is_null() || !layer->enabled) {
				continue;
			}
			if (layer->trigger == EFFECT_TRAIL_FOLLOW) {
				continue;
			}
			bullet_factory->fx_register_volley_bake(get_instance_id(), li, layer, amount_bullets);
		}
	}
	if (fire_spawn) {
		fx_fire_spawn_layers();
	}
}

void DirectionalBullets2D::fx_fire_spawn_layers() {
	if (bullet_factory == nullptr || (int)all_cached_instance_transforms.size() != amount_bullets) {
		return;
	}
	if (!(fx_oneshot_trigger_mask & (1u << EFFECT_ON_SPAWN))) {
		return;
	}
	for (int i = 0; i < amount_bullets; ++i) {
		fx_fire_oneshot(EFFECT_ON_SPAWN, i, all_cached_instance_transforms[i]);
	}
}

void DirectionalBullets2D::fx_fire_oneshot(int trigger, int bullet_index, const Transform2D &at) {
	if (trigger < 0 || trigger >= 32 || !(fx_oneshot_trigger_mask & (1u << trigger))) {
		return;
	}
	if (bullet_factory == nullptr || bullet_index < 0 || bullet_index >= amount_bullets) {
		return;
	}
	if (!at.get_origin().is_finite() || !Math::is_finite(at.get_rotation())) {
		return;
	}
	for (int li = 0; li < fx_data_layers.size(); ++li) {
		Ref<BulletEffectLayerData2D> layer = fx_data_layers[li];
		if (layer.is_null() || !layer->enabled) {
			continue;
		}
		if (layer->trigger != trigger) {
			continue;
		}
		bullet_factory->fx_fire(get_instance_id(), li, at);
	}
}

bool DirectionalBullets2D::has_trail_effects() const {
	return !fx_trail_bakes.empty();
}

bool DirectionalBullets2D::fx_has_trail_layer(int layer_index) const {
	for (size_t b = 0; b < fx_trail_bakes.size(); ++b) {
		if (fx_trail_bakes[b].layer_index == layer_index) {
			return true;
		}
	}
	return false;
}

void DirectionalBullets2D::bullet_set_trail_enabled(int layer_index, int bullet_index, bool trail_on) {
	if (!validate_bullet_index(bullet_index, "bullet_set_trail_enabled")) {
		return;
	}
	if (!fx_has_trail_layer(layer_index)) {
		UtilityFunctions::push_error("bullet_set_trail_enabled: no baked trail layer " + String::num_int64(layer_index) + " on this volley.");
		return;
	}
	for (size_t b = 0; b < fx_trail_bakes.size(); ++b) {
		if (fx_trail_bakes[b].layer_index != layer_index) {
			continue;
		}
		if (bullet_index < 0 || bullet_index >= (int)fx_trail_bakes[b].bullet_on.size()) {
			return;
		}
		fx_trail_bakes[b].bullet_on[bullet_index] = trail_on ? 1 : 0;
		if (!trail_on) {
			hide_trail_instances(bullet_index);
		} else {
			write_trail_instances(bullet_index);
		}
		return;
	}
}

void DirectionalBullets2D::all_bullets_set_trail_enabled(int layer_index, bool trail_on, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_trail_enabled");
	// Single error for the whole range instead of one per bullet below.
	if (!fx_has_trail_layer(layer_index)) {
		UtilityFunctions::push_error("all_bullets_set_trail_enabled: no baked trail layer " + String::num_int64(layer_index) + " on this volley.");
		return;
	}
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		bullet_set_trail_enabled(layer_index, i, trail_on);
	}
}

// Switches one trail layer to another baked animation (same frames for the
// next reseed; one-shot layers switch through their layer resource +
// set_effect_layers). Shard nodes are rebuilt: counts follow the new frame
// list, per-bullet phase/toggles survive.
bool DirectionalBullets2D::play_effect_animation(int layer_index, const StringName &animation) {
	for (size_t b = 0; b < fx_trail_bakes.size(); ++b) {
		FXTrailBake &bake = fx_trail_bakes[b];
		if (bake.layer_index != layer_index || bake.layer.is_null()) {
			continue;
		}
		std::vector<Ref<Texture2D>> frames;
		std::vector<double> secs;
		double total = 0.0;
		if (!bake.layer->bake_effect_frames(animation, frames, secs, total)) {
			return false;
		}
		for (size_t s = 0; s < bake.shards.size(); ++s) {
			if (bake.shards[s] != nullptr) {
				// Zero first: a queued-free node still renders this frame.
				Ref<MultiMesh> mm = bake.shards[s]->get_multimesh();
				if (mm.is_valid()) {
					for (int i = 0; i < mm->get_instance_count(); ++i) {
						mm->set_instance_transform_2d(i, zero_transform);
					}
				}
				bake.shards[s]->queue_free();
			}
		}
		bake.shards.clear();
		bake.shard_multimeshes.clear();
		bake.shard_visible.clear();
		bake.shard_refcount.clear();
		bake.frames = frames;
		bake.secs = secs;
		bake.frame_starts.clear();
		bake.frame_starts.reserve(secs.size());
		{
			double acc = 0.0;
			for (double s : secs) {
				acc += s;
				bake.frame_starts.push_back(acc);
			}
		}
		bake.total = total;
		bake.animation_override = true;
		for (size_t f = 0; f < frames.size(); ++f) {
			MultiMeshInstance2D *shard = BulletFactory2D::fx_create_shard(this, frames[f], BulletFactory2D::fx_quad_size_for_texture(frames[f]), bake.layer->material, bake.layer->self_modulate, bake.layer->z_index, bake.layer->z_as_relative, bake.layer->visibility_layer, bake.layer->light_mask, amount_bullets, true);
			bake.shards.push_back(shard);
			bake.shard_multimeshes.push_back(shard != nullptr ? shard->get_multimesh() : Ref<MultiMesh>());
		}
		bake.shard_visible.assign(bake.shards.size(), 0);
		bake.shard_refcount.assign(bake.shards.size(), 0);
		bake.bullet_shard.assign(amount_bullets, -1);
		bake.bullet_trail_transf.assign(amount_bullets, Transform2D());
		bake.bullet_trail_tint.assign(amount_bullets, Color(1, 1, 1, 1));
		if ((int)bake.phase.size() != amount_bullets) {
			bake.phase.assign(amount_bullets, 0.0);
		}
		if ((int)bake.bullet_on.size() != amount_bullets) {
			bake.bullet_on.assign(amount_bullets, 1);
		}
		return true;
	}
	return false;
}

TypedArray<BulletEffectLayerData2D> DirectionalBullets2D::get_effect_layers() const {
	return fx_data_layers;
}

void DirectionalBullets2D::set_effect_layers(const TypedArray<BulletEffectLayerData2D> &new_layers) {
	// Live rebake without a spawn flash (edits must not detonate): trigger
	// routing, trail shards and factory bakes all follow the new list.
	fx_reseed_from_data(new_layers, false);
}

Dictionary DirectionalBullets2D::debug_get_effect_layers_info() const {
	Dictionary d;
	d["data_layer_count"] = fx_data_layers.size();
	d["trail_bake_count"] = (int)fx_trail_bakes.size();
	Array bakes;
	for (size_t b = 0; b < fx_trail_bakes.size(); ++b) {
		const FXTrailBake &bake = fx_trail_bakes[b];
		Dictionary e;
		e["layer_index"] = bake.layer_index;
		e["frames"] = (int)bake.frames.size();
		e["shards"] = (int)bake.shards.size();
		if (!bake.shards.empty() && bake.shards[0] != nullptr) {
			e["z_index"] = bake.shards[0]->get_z_index();
			e["z_as_relative"] = bake.shards[0]->is_z_relative();
			e["modulate"] = bake.shards[0]->get_modulate();
			e["shard_texture_valid"] = bake.shards[0]->get_texture().is_valid();
			e["visibility_layer"] = bake.shards[0]->get_visibility_layer();
			e["light_mask"] = bake.shards[0]->get_light_mask();
		}
		int shown = 0;
		for (size_t s = 0; s < bake.shard_visible.size(); ++s) {
			if (bake.shard_visible[s]) {
				++shown;
			}
		}
		e["shards_visible"] = shown;
		int tracked = 0;
		for (size_t i = 0; i < bake.bullet_shard.size(); ++i) {
			if (bake.bullet_shard[i] >= 0) {
				++tracked;
			}
		}
		e["bullets_tracked"] = tracked;
		Array live_trails;
		for (size_t i = 0; i < bake.bullet_shard.size() && i < bake.bullet_trail_tint.size(); ++i) {
			if (bake.bullet_shard[i] < 0) {
				continue;
			}
			Dictionary row;
			row["bullet"] = (int)i;
			row["shard"] = bake.bullet_shard[i];
			row["tint"] = bake.bullet_trail_tint[i];
			live_trails.push_back(row);
		}
		e["live_trails"] = live_trails;
		bakes.push_back(e);
	}
	d["trail_bakes"] = bakes;
	return d;
}

Transform2D DirectionalBullets2D::debug_get_trail_transform(int layer_index, int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "debug_get_trail_transform")) {
		return Transform2D();
	}
	for (size_t b = 0; b < fx_trail_bakes.size(); ++b) {
		const FXTrailBake &bake = fx_trail_bakes[b];
		if (bake.layer_index != layer_index) {
			continue;
		}
		if (bullet_index < 0 || bullet_index >= (int)bake.bullet_shard.size()) {
			return Transform2D();
		}
		const int frame = bake.bullet_shard[bullet_index];
		if (frame < 0 || frame >= (int)bake.shards.size() || bake.shards[frame] == nullptr) {
			return Transform2D();
		}
		if (bullet_index < 0 || bullet_index >= (int)bake.bullet_trail_transf.size()) {
			return Transform2D();
		}
		return bake.bullet_trail_transf[bullet_index];
	}
	return Transform2D();
}

// Used to spawn brand new bullets.
void DirectionalBullets2D::spawn(const DirectionalBulletsData2D &data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container, const Vector2 &new_inherited_velocity_offset, int new_sparse_set_id, bool spawn_in_pool, uint64_t spawner_id) {
	this->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF); // We have custom physics interpolation logic, so disable the Godot one that comes from Godot 4.5

	sparse_set_id = new_sparse_set_id;
	inherited_velocity_offset = new_inherited_velocity_offset;

	bullets_pool = pool;
	bullet_factory = factory;
	// Ownership is stamped FIRST, before the area gets a physics space, shapes
	// are enabled, or the node enters the tree below: a spawner volley is never
	// observable as factory-owned. Pooled pre-population passes 0, so reused
	// instances can never inherit a previous owner's spawner.
	owner_spawner_id = spawner_id;
	physics_server = PhysicsServer2D::get_singleton();

	warn_data_id = data.get_instance_id();
	amount_bullets = spawn_transform_count(data); // important, because some set_up methods use this
	cache_collision_shape_typed(data.collision_shape);

	++multimesh_generation;

	all_bullets_enabled_set.resize(amount_bullets);
	all_bullet_curves_data.assign(amount_bullets, Ref<BulletCurvesData2D>());
	all_movement_pattern_data.assign(amount_bullets, BulletMovementPatternData2D());
	bullet_collision_epochs.assign(amount_bullets, 0);
	batch_buffer.resize(amount_bullets * 8);

	set_up_life_time_timer(data.max_life_time, data.max_life_time);

	generate_multimesh();
	set_up_multimesh(amount_bullets, data.mesh, resolve_quad_size(data.sprite_frames, data.animation, data.texture_size));

	area = physics_server->area_create();
	generate_physics_shapes_for_area(amount_bullets);

	set_up_bullet_instances(data);

	// Set up bullet attachments so that for every bullet you will be able to have an attachment if needed.
	// Blanks all slots and arrays (fresh instances get zeroed vectors; the loop in
	// reset_attachment_state_for_reuse is a no-op here but keeps the invariant in one place).
	reset_attachment_state_for_reuse();

	set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures, data.tile_all_bullet_rotation_data);

	all_previous_instance_transf.resize(amount_bullets);
	all_previous_attachment_transf.resize(amount_bullets);

	update_all_previous_transforms_for_interpolation();

	finalize_set_up(
			data.shared_bullets_custom_data,
			data.material,
			data.z_index,
			data.light_mask,
			data.visibility_layer,
			data.self_modulate,
			data.instance_shader_parameters);

	// Single-error policy: rebuild_sprite_animation already reported the cause;
	// no wrapper error here. Failure leaves previous texture/cache untouched.
	// Appearance snapshot first: the rebuild below whitens frames when the
	// data asks for it, and the fade half starts transparent when fading in.
	snapshot_appearance_from_data(data);
	rebuild_sprite_animation(data.sprite_frames, data.animation);

	seed_motion_features_on_spawn(data);

	set_process(false);
	set_physics_process(false);
	if (spawn_in_pool) {
		set_visible(false);
		is_active = false;
		// Pooled instances hold zero enabled bullets: reset the counter that
		// set_up_bullet_instances set to amount_bullets so counter==0 matches
		// the empty enabled set (enable_bullet wake counts up from here).
		active_bullets_counter = 0;
		set_all_physics_shapes_enabled_for_area(false);
		bullets_container->add_child(this);
		bullets_pool->push(this, get_pool_key());
	} else {
		all_bullets_enabled_set.activate_all_data();
		is_active = true;
		bullets_container->add_child(this);
	}

	// Shared spawn-data attachments (both bullet types). Skipped for pooled
	// pre-population: the slots were just blanked above, and enable_multimesh()
	// applies the data when the instance is popped instead.
	if (!spawn_in_pool) {
		apply_shared_bullet_attachment_from_data(data);
		// Sprite effect layers reseed here too (trail shards rebuilt, factory
		// one-shot bakes registered, spawn flashes fired at every bullet).
		fx_reseed_from_data(data.effect_layers, true);
	}
}

void DirectionalBullets2D::reset_transient_volley_state(uint64_t new_owner_spawner_id, bool drop_stale_work, bool keep_attachment_slots) {
	// A new life / a dead life never replays the previous life's parked overlaps.
	paused_overlaps.clear();
	// Ownership is stamped first so every step below already belongs to the
	// new life (or to nobody, when dying).
	owner_spawner_id = new_owner_spawner_id;
	// Shared curves/patterns are cleared HERE, before the motion reset: the
	// reseed functions return early on empty arrays and skip null entries, so
	// a stale slot left here would leak the previous owner's per-bullet
	// curves and movement patterns into the next life.
	shared_bullet_curves_data.unref();
	for (auto &r : all_bullet_curves_data) {
		r.unref();
	}
	for (auto &p : all_movement_pattern_data) {
		p = BulletMovementPatternData2D();
	}
	reset_motion_feature_state(drop_stale_work);
	// Blank attachment state: a reused instance must never carry the previous
	// owner's attachment slots into the next life. A held expiry keeps them
	// for its deferred handler (released after the flush); a new life always
	// ends any hold first.
	if (drop_stale_work && lifetime_flush_pending) {
		lifetime_flush_pending = false;
	}
	if (!keep_attachment_slots) {
		reset_attachment_state_for_reuse();
	}
	// Pooling flags reset every life - if you turned pooling off to hold a volley manually, the next pooled reuse still pools normally unless you turn it off again.
	reset_pooling_flags_to_default();
	if (drop_stale_work) {
		// New life: stale deferred emits/disables (scheduled before a pool
		// reuse) carry the old generation and no-op at flush time, and the
		// previous owner's volley-wide connections must not fire again.
		// Disabled (but not pooled) volleys keep their connections here: a
		// same-owner enable_bullet() wake must not silence the volley.
		++multimesh_generation;
		disconnect_sprite_animation_connections();
	}
	// A fresh volley starts with zero hits and no timers, no matter how the last one died.
	all_collided_bullets.clear();
	// The dedup keys mirror all_collided_bullets, so they reset with it.
	clear_collision_dedup_keys();
	_do_detach_all_time_based_functions(multimesh_timers_generation);
	// Same for the volley clock - waking an old instance must not resume the previous owner's curve time.
	curves_elapsed_time = 0.0;
	// Animation cursor restarts; baked frames are kept so a same-owner wake
	// resumes them. New-life paths blank the frames before rebuilding.
	anim_frame_index = 0;
	anim_paused = false;
	anim_finished = false;
	if (!anim_frame_secs.empty()) {
		anim_frame_time_left = anim_frame_secs[0];
	}
}

void DirectionalBullets2D::deactivate_volley() {
	all_bullets_enabled_set.clear();
	active_bullets_counter = 0;
	is_active = false;
	set_visible(false);
	set_all_physics_shapes_enabled_for_area(false);
}

// Activates the multimesh
bool DirectionalBullets2D::enable_multimesh(const DirectionalBulletsData2D &data, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id) {
	warn_data_id = data.get_instance_id();
	// The pool sorts volleys by bullet count, so a wrong-size array here would read past the end - bail before touching anything.
	if (spawn_transform_count(data) != amount_bullets) {
		spawn_transforms_ptr = nullptr;
		UtilityFunctions::push_error("enable_multimesh: transforms size (" + String::num_int64(spawn_transform_count(data)) + ") must match amount_bullets (" + String::num_int64(amount_bullets) + ").");
		return false;
	}

	// Fast path: same shape and count means we just rewrite transforms instead of rebuilding physics.
	// shapes and reseeds SoA vectors (no RID alloc/free), so it is safe from
	// ordinary physics callbacks such as _physics_process shooting. Only a
	// shape-TYPE change performs structural RID work (area_clear_shapes /
	// free_rid / re-bucket) and must wait for a safe point. Iterating sweeps
	// (factory busy) always reject: vectors below are being walked.
	const PhysicsServer2D::ShapeType incoming_effective = CollisionShapeHelper2D::get_effective_type(data.collision_shape, false);
	const bool shape_type_changes = (incoming_effective != cached_effective_shape_type);
	// Only THIS volley's own tick is off limits (its drain/lifetime pass is
	// still walking its vectors). Other pooled volleys reuse fine mid-sweep:
	// the factory iterates a snapshot, so a re-activated volley simply joins
	// the next tick.
	if (is_being_ticked) {
		UtilityFunctions::push_error("enable_multimesh cannot run on a volley while its own tick is running (e.g. from its collision handler or attachment callback). Use call_deferred() to enable it after the sweep.");
		return false;
	}
	if (shape_type_changes && bullet_factory != nullptr && bullet_factory->is_structural_mutation_unsafe()) {
		UtilityFunctions::push_error("enable_multimesh with a different collision shape type cannot run inside a physics frame (server flush locks apply to the shape RIDs it must recreate). Use call_deferred() to enable outside the physics step.");
		return false;
	}

	// Re-enabling a live volley would wipe its attachments, timers, curves and
	// patterns mid-flight. Pool pops only hand out disabled instances, so a
	// live instance here is always a direct (mis)call: reject, don't reseed.
	if (is_active) {
		UtilityFunctions::push_error("enable_multimesh: instance is already active. Disable it first or spawn a new volley instead.");
		return false;
	}

	// Reseeding a dying instance would configure a volley that never lives a
	// tick (pool pops already filter these; this covers direct GDScript
	// calls). queue_free() is terminal.
	if (is_queued_for_deletion()) {
		UtilityFunctions::push_error("enable_multimesh: multimesh is queued for deletion.");
		return false;
	}

	// Validate everything before touching state; reset + reseed only runs on
	// first mutation, so a reject below leaves zero state behind and no
	// valid input, so a rejected enable never needs a rollback.
	// Never-spawned instances hold no multimesh handle (generate_multimesh
	// runs in spawn() only): reseeding one would null-deref below. Pool
	// pops always carry theirs, so this rejects misuse only.
	if (multi.is_null() || !multi.is_valid()) {
		UtilityFunctions::push_error("enable_multimesh: multimesh was never spawned through BulletFactory2D (no bullet storage). Spawn it first.");
		return false;
	}

	// The factory spawn_* paths validate finiteness, but a direct
	// enable_multimesh() call bypasses them: a NaN/Inf offset here would
	// poison every bullet's velocity for the whole volley. Checked before any
	// mutation (previously after the owner/curve clears, leaking on reject).
	if (!new_inherited_velocity_offset.is_finite()) {
		UtilityFunctions::push_error("enable_multimesh: inherited velocity offset must be finite, keeping the old value.");
		return false;
	}

	// One reset owns the whole clean-disabled state (owner stamp,
	// curves/patterns/motion ballistics, attachment slots, collided hits,
	// timers, clocks, animation cursor, generation bump, connection scrub).
	// Spawner ownership is stamped here (before any re-activation below), so
	// a spawner reuse is never observable as factory-owned. Whoever enables
	// next stamps anew; 0 keeps the factory-owned default.
	reset_transient_volley_state(spawner_id, true);

	// A new life never inherits the previous owner's baked animation
	// (reset kept the frames for same-owner wakes; a reseed always rebuilds,
	// so blank them here). rebuild only overwrites on success: a failed
	// rebuild leaves blank state, never the old animation playing.
	anim_source.unref();
	anim_frames.clear();
	anim_frame_secs.clear();
	set_texture(Ref<Texture2D>());
	// No snapshot/rollback needed. Wrong-type input was rejected above
	// before any mutation, and every reseed below fully overwrites the blank
	// state reset_transient_volley_state() left behind.
	inherited_velocity_offset = new_inherited_velocity_offset;
	const PhysicsServer2D::ShapeType old_effective_shape_type = cached_effective_shape_type;
	cache_collision_shape_typed(data.collision_shape);

	// Reused RIDs keep their original type. If the new spawn switches shape type,
	// the old RIDs would receive mismatched data below, so recreate them exactly
	// like set_collision_shape_runtime() does.
	if (cached_effective_shape_type != old_effective_shape_type && physics_server != nullptr && area.is_valid()) {
		release_volley_shape();
		generate_physics_shapes_for_area(amount_bullets);
	}

	// Blank attachment state before anything else: a reused instance must never
	// carry the previous owner's attachment slots into this enable.
	reset_attachment_state_for_reuse();

	++multimesh_generation;

	// A fresh volley starts with zero hits and no timers, no matter how the last one died.
	all_collided_bullets.clear();
	// The dedup keys mirror all_collided_bullets, so they reset with it.
	clear_collision_dedup_keys();
	_do_detach_all_time_based_functions(multimesh_timers_generation);

	// Same for the volley clock - waking an old instance must not resume the previous owner's curve time.
	curves_elapsed_time = 0.0;

	set_up_life_time_timer(data.max_life_time, data.max_life_time);

	set_up_multimesh(amount_bullets, data.mesh, resolve_quad_size(data.sprite_frames, data.animation, data.texture_size));

	set_up_bullet_instances(data);
	set_all_physics_shapes_enabled_for_area(true);

	// Shared spawn-data attachments (both bullet types). Slot vectors were
	// reset above and caches sized by set_up_bullet_instances, so this is safe
	// for pooled reuse with new data.
	apply_shared_bullet_attachment_from_data(data);

	set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures, data.tile_all_bullet_rotation_data);

	move_to_front(); // Pooled instances render behind newer ones without this; moving to front emulates fresh spawn order.

	update_all_previous_transforms_for_interpolation();

	finalize_set_up(
			data.shared_bullets_custom_data,
			data.material,
			data.z_index,
			data.light_mask,
			data.visibility_layer,
			data.self_modulate,
			data.instance_shader_parameters);

	// Single-error policy: rebuild already reported; blank animation kept on failure.
	// (Frames were blanked in the prologue; connections scrubbed by the reset.)
	// Appearance snapshot first (same ordering as spawn above).
	snapshot_appearance_from_data(data);
	rebuild_sprite_animation(data.sprite_frames, data.animation);

	// Motion reseed. A refusal leaves a clean disabled instance for the pool
	// instead of half-seeded state: reset + deactivate fully blank what the
	// reseed above wrote, so the next correct enable starts from neutral.
	if (!reseed_motion_features_on_enable(data)) {
		reset_transient_volley_state(spawner_id, true);
		deactivate_volley();
		return false;
	}

	// Sprite effect layers reseed from the new data (previous life's bakes
	// die here, trail shards rebuild, spawn flashes fire at every bullet).
	fx_reseed_from_data(data.effect_layers, true);

	set_visible(true);

	// Mark all bullets as enabled in the sparse set (amount_bullets never changes)
	all_bullets_enabled_set.activate_all_data();
	is_active = true;
	return true;
}

void DirectionalBullets2D::set_up_bullet_instances(const DirectionalBulletsData2D &data) {
	active_bullets_counter = amount_bullets;

	bullet_max_collision_count = data.bullet_max_collision_count;

	if (data.bullets_current_collision_count.size() == 0) {
		bullets_current_collision_count.clear();
		bullets_current_collision_count.resize(amount_bullets, 0);
	} else {
		// Always succeeds (fills + warns); uncovered bullets start at 0.
		set_bullets_current_collision_count(data.bullets_current_collision_count, data.tile_bullets_current_collision_count);
	}

	// Per-bullet custom data (strict indexing; strictly separate from
	// shared_bullets_custom_data - bullets without an entry read null, never
	// the shared value). Empty = all null, size == N = entry i for bullet i,
	// short = tail bullets null, long = extras ignored. With the tile
	// checkbox, short arrays wrap (i % size).
	all_bullets_custom_data.assign(amount_bullets, Ref<Resource>());
	if (data.all_bullets_custom_data.size() > 0) {
		const int custom_size = data.all_bullets_custom_data.size();
		const bool tile_custom = data.tile_all_bullets_custom_data;
		if (custom_size != amount_bullets) {
			WarnOnce2D::warn(warn_data_id, 2u, custom_size, amount_bullets, "DirectionalBullets2D: all_bullets_custom_data size (" + String::num_int64(custom_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets read null" + String(tile_custom ? " (tiling on: wrapping short array)." : " (check tile_all_bullets_custom_data to wrap, or provide one entry per bullet)."));
		}
		for (int i = 0; i < amount_bullets; ++i) {
			const int src = tile_custom ? (i % custom_size) : i;
			all_bullets_custom_data[i] = (src >= 0 && src < custom_size) ? Ref<Resource>(data.all_bullets_custom_data[src]) : Ref<Resource>();
		}
	}

	is_life_time_over_signal_enabled = data.is_life_time_over_signal_enabled;

	is_life_time_infinite = data.is_life_time_infinite;

	set_up_area(data.collision_layer, data.collision_mask, data.monitorable, bullet_factory != nullptr ? bullet_factory->physics_space : RID());

	stop_rotation_when_max_reached = data.stop_rotation_when_max_reached;

	cache_collision_shape_offset = data.collision_shape_offset;

	if (all_cached_instance_transforms.size() != 0) {
		// Waking a pooled volley: drop last life's frames. The buffers keep their size since the bullet count never changes on reuse.
		all_cached_instance_transforms.clear();
		all_cached_instance_origin.clear();
		all_cached_shape_transforms.clear();
		all_cached_shape_origin.clear();
	} else {
		// First spawn: reserve everything up front for the fixed bullet count.
		all_cached_instance_transforms.reserve(amount_bullets);
		all_cached_instance_origin.reserve(amount_bullets);
		all_cached_shape_transforms.reserve(amount_bullets);
		all_cached_shape_origin.reserve(amount_bullets);
	}

	cache_texture_rotation_radians = data.texture_rotation_radians;
	cache_texture_transforms.resize(amount_bullets);

	// One node inverse for the whole setup loop (generate_texture_transform
	// converts every bullet to multimesh-local space).
	NodeInverseScope inverse_scope(this);
	// Shape data once per volley (pooled reuse with the same shape skips it).
	apply_volley_shape_data();

	// Transform source: the factory's native span when present (no Variant),
	// else the data resource's Array. Consumed here (cleared below) so a
	// later re-entrant spawn can never read a stale pointer.
	const Transform2D *span = spawn_transforms_ptr;
	const int span_count = spawn_transforms_count;
	spawn_transforms_ptr = nullptr;
	spawn_transforms_count = 0;
	if (span != nullptr && span_count != amount_bullets) {
		span = nullptr; // defensive: never index past the span
	}
	// One upload for every instance (see generate_texture_transform).
	if ((int)batch_buffer.size() != amount_bullets * 8) {
		batch_buffer.resize(amount_bullets * 8);
	}
	const bool batch_ok = multi.is_valid() && multi->get_instance_count() == amount_bullets;
	float *batch_w = batch_ok ? batch_buffer.ptrw() : nullptr;
	for (int i = 0; i < amount_bullets; ++i) {
		const Transform2D curr_data_transf = span != nullptr ? span[i] : (Transform2D)data.transforms[i];

		// Generates a collision shape transform for a particular bullet and attaches it to the area
		Transform2D shape_transf = generate_collision_shape_transform_for_area(curr_data_transf, data.collision_shape_offset, i);

		// Generates texture transform with correct rotation and sets it to the correct bullet on the multimesh
		const Transform2D &texture_transf = generate_texture_transform(curr_data_transf, data.is_texture_rotation_permanent, cache_texture_rotation_radians, i);

		cache_texture_transforms[i] = texture_transf;
		if (batch_ok) {
			const Transform2D local = to_local_for_multimesh(texture_transf);
			float *o = batch_w + i * 8;
			o[0] = local.columns[0][0];
			o[1] = local.columns[1][0];
			o[2] = 0;
			o[3] = local.columns[2][0];
			o[4] = local.columns[0][1];
			o[5] = local.columns[1][1];
			o[6] = 0;
			o[7] = local.columns[2][1];
		}

		// Cache bullet transforms and origin vectors
		all_cached_instance_transforms.emplace_back(texture_transf);
		all_cached_instance_origin.emplace_back(texture_transf.get_origin());

		all_cached_shape_transforms.emplace_back(shape_transf);
		all_cached_shape_origin.emplace_back(shape_transf.get_origin());
	}
	if (batch_ok) {
		multi->set_buffer(batch_buffer);
	} else if (multi.is_valid()) {
		// Instance count not set up yet (never expected): per-instance
		// fallback so the bullets are never drawn at stale poses.
		const int n = MIN(amount_bullets, (int)multi->get_instance_count());
		for (int i = 0; i < n; ++i) {
			multi->set_instance_transform_2d(i, to_local_for_multimesh(cache_texture_transforms[i]));
		}
	}
}

void DirectionalBullets2D::generate_multimesh() {
	Ref<MultiMesh> new_multi;
	new_multi.instantiate();
	new_multi->set_transform_format(MultiMesh::TRANSFORM_2D);

	multi = new_multi;
	set_multimesh(multi);
}

void DirectionalBullets2D::set_up_multimesh(int new_instance_count, const Ref<Mesh> &new_mesh, Vector2 new_texture_size) {
	if (new_mesh.is_valid()) {
		if (multi->get_mesh() != new_mesh) {
			multi->set_mesh(new_mesh);
		}
	} else {
		// Pooled reuse keeps the volley's own quad: a fresh QuadMesh per
		// enable was a resource + RS mesh allocation on every spawn.
		if (owned_quad_mesh.is_null()) {
			owned_quad_mesh.instantiate();
		}
		if (owned_quad_mesh->get_size() != new_texture_size) {
			owned_quad_mesh->set_size(new_texture_size);
		}
		if (multi->get_mesh() != owned_quad_mesh) {
			multi->set_mesh(owned_quad_mesh);
		}
	}
	// Always track the resolved size, even with a custom mesh, so pooled reuse
	// with different data cannot inherit a stale quad size.
	texture_size = new_texture_size;

	// One huge bounding box so the camera can never cull the whole volley by mistake (bullets live all over the level). Hidden bullets cost nothing - they're written as zero-scale.
	multi->set_custom_aabb(AABB(Vector3(-100000, -100000, -1000), Vector3(200000, 200000, 2000)));

	// MultiMesh::set_instance_count reallocates the RS buffer even for an
	// unchanged count, and the count never changes on pool reuse (it is
	// the pool key). Every instance is rewritten by set_up_bullet_instances.
	if (multi->get_instance_count() != new_instance_count) {
		multi->set_instance_count(new_instance_count);
	}
	if ((int)batch_buffer.size() != new_instance_count * 8) {
		batch_buffer.resize(new_instance_count * 8);
	}
}

void DirectionalBullets2D::set_up_life_time_timer(double new_max_life_time, double new_current_life_time) {
	max_life_time = new_max_life_time;
	current_life_time = new_current_life_time;
}

double DirectionalBullets2D::get_fade_in_sec() const {
	return fade_in_sec;
}
void DirectionalBullets2D::set_fade_in_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("DirectionalBullets2D: fade_in_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	fade_in_sec = value;
}
double DirectionalBullets2D::get_fade_out_sec() const {
	return fade_out_sec;
}
void DirectionalBullets2D::set_fade_out_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("DirectionalBullets2D: fade_out_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	fade_out_sec = value;
}
Ref<Gradient> DirectionalBullets2D::get_modulate_ramp() const {
	return fade_modulate_ramp;
}
void DirectionalBullets2D::set_modulate_ramp(const Ref<Gradient> &value) {
	fade_modulate_ramp = value;
}
Color DirectionalBullets2D::get_fade_base_modulate() const {
	return fade_base_modulate;
}
void DirectionalBullets2D::set_fade_base_modulate(const Color &value) {
	fade_base_modulate = value;
	// No fade/ramp configured: the tick early-returns and would never apply
	// the new base, so write it now (this setter is the documented path).
	if (fade_in_sec <= 0.0 && fade_out_sec <= 0.0 && fade_modulate_ramp.is_null()) {
		set_self_modulate(value);
		fade_applied = value;
		return;
	}
	// Re-arm the change detector so the next tick writes even if the value
	// happens to equal the stale applied snapshot.
	fade_applied = Color(-1, -1, -1, -1);
}
bool DirectionalBullets2D::get_override_frame_color() const {
	return anim_override_frame_color;
}
void DirectionalBullets2D::set_override_frame_color(bool value) {
	if (anim_override_frame_color == value) {
		return;
	}
	anim_override_frame_color = value;
	// Live toggle rebuilds the cached frames from the stored source (same
	// path as play_sprite_animation_name). No source yet means nothing to
	// rebuild: the next spawn/enable applies it through the snapshot.
	if (!anim_source.is_null()) {
		rebuild_sprite_animation(anim_source, anim_name);
	}
}

void DirectionalBullets2D::snapshot_appearance_from_data(const DirectionalBulletsData2D &data) {
	fade_base_modulate = data.self_modulate;
	fade_in_sec = data.fade_in_sec;
	fade_out_sec = data.fade_out_sec;
	fade_modulate_ramp = data.modulate_ramp;
	anim_override_frame_color = data.override_frame_color;
	if (fade_in_sec > 0.0) {
		// No full-alpha flash before the first tick: start transparent now,
		// the tick below ramps up from here.
		Color start = fade_base_modulate;
		start.a = 0.0;
		set_self_modulate(start);
		fade_applied = start;
	} else {
		fade_applied = fade_base_modulate;
	}
}

void DirectionalBullets2D::tick_volley_fade() {
	if (fade_in_sec <= 0.0 && fade_out_sec <= 0.0 && fade_modulate_ramp.is_null()) {
		return;
	}
	if (!Math::is_finite(curves_elapsed_time)) {
		return;
	}
	const double age = curves_elapsed_time;
	double alpha = 1.0;
	if (fade_in_sec > 0.0 && age < fade_in_sec) {
		alpha = age / fade_in_sec;
	}
	Color target = fade_base_modulate;
	// Fade-out and the ramp need a lifetime fraction: infinite volleys
	// never expire, so only fade-in applies there (documented).
	if (!is_life_time_infinite && max_life_time > 0.0 && Math::is_finite(current_life_time)) {
		if (fade_out_sec > 0.0 && current_life_time < fade_out_sec) {
			const double out_alpha = current_life_time / fade_out_sec;
			if (out_alpha < alpha) {
				alpha = out_alpha;
			}
		}
		if (fade_modulate_ramp.is_valid()) {
			const double pos = Math::clamp(age / max_life_time, 0.0, 1.0);
			target = fade_base_modulate * fade_modulate_ramp->sample((float)pos);
		}
	}
	if (alpha < 0.0) {
		alpha = 0.0;
	} else if (alpha > 1.0) {
		alpha = 1.0;
	}
	target.a *= (float)alpha;
	if (target == fade_applied) {
		return;
	}
	set_self_modulate(target);
	fade_applied = target;
}

static bool resolve_sprite_animation_impl(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_requested, StringName &out_anim, bool silent) {
	if (p_sprite_frames.is_null()) {
		if (!silent) {
			UtilityFunctions::push_error("DirectionalBullets2D: sprite_frames is null. Assign a SpriteFrames resource.");
		}
		return false;
	}
	const PackedStringArray names = p_sprite_frames->get_animation_names();
	if (names.is_empty()) {
		if (!silent) {
			UtilityFunctions::push_error("DirectionalBullets2D: sprite_frames has no animations.");
		}
		return false;
	}
	const String requested_str = String(p_requested);
	const bool is_auto = requested_str.is_empty() || p_requested == StringName("default");
	// Picks the first animation that actually has frames. An empty "default" (fresh
	// SpriteFrames resources always contain one) must not shadow a populated animation.
	auto first_with_frames = [&]() -> StringName {
		for (int i = 0; i < names.size(); ++i) {
			if (p_sprite_frames->get_frame_count(names[i]) > 0) {
				return names[i];
			}
		}
		return StringName();
	};
	if (is_auto) {
		// Unselected animation: play "default" silently when usable, else first animation
		// with frames, silently.
		if (p_sprite_frames->has_animation(StringName("default")) && p_sprite_frames->get_frame_count(StringName("default")) > 0) {
			out_anim = StringName("default");
			return true;
		}
		const StringName fallback = first_with_frames();
		if (String(fallback).is_empty()) {
			if (!silent) {
				UtilityFunctions::push_error("DirectionalBullets2D: sprite_frames has no animation with frames.");
			}
			return false;
		}
		out_anim = fallback;
		return true;
	}
	if (p_sprite_frames->has_animation(p_requested) && p_sprite_frames->get_frame_count(p_requested) > 0) {
		out_anim = p_requested;
		return true;
	}
	const StringName fallback = first_with_frames();
	if (String(fallback).is_empty()) {
		if (!silent) {
			UtilityFunctions::push_error("DirectionalBullets2D: sprite_frames has no animation with frames.");
		}
		return false;
	}
	if (!silent) {
		UtilityFunctions::push_error("DirectionalBullets2D: missing animation '" + requested_str + "', falling back to '" + String(fallback) + "'.");
	}
	out_anim = fallback;
	return true;
}

bool DirectionalBullets2D::resolve_sprite_animation(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_requested, StringName &out_anim) {
	return resolve_sprite_animation_impl(p_sprite_frames, p_requested, out_anim, false);
}

bool DirectionalBullets2D::resolve_sprite_animation_quiet(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_requested, StringName &out_anim) {
	return resolve_sprite_animation_impl(p_sprite_frames, p_requested, out_anim, true);
}

bool DirectionalBullets2D::rebuild_sprite_animation(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_animation) {
	StringName anim;
	if (!resolve_sprite_animation(p_sprite_frames, p_animation, anim)) {
		return false; // error already reported, previous animation untouched
	}
	const int count = p_sprite_frames->get_frame_count(anim);
	if (count <= 0) {
		UtilityFunctions::push_error("DirectionalBullets2D: animation '" + String(anim) + "' has no frames.");
		return false;
	}
	double fps = p_sprite_frames->get_animation_speed(anim);
	if (!Math::is_finite(fps) || fps <= 0.0) {
		UtilityFunctions::push_error("DirectionalBullets2D: animation '" + String(anim) + "' has invalid speed, using 1 fps.");
		fps = 1.0;
	}
	std::vector<Ref<Texture2D>> frames;
	std::vector<double> secs;
	frames.reserve(count);
	secs.reserve(count);
	bool whiten_warned = false;
	for (int i = 0; i < count; ++i) {
		Ref<Texture2D> tex = p_sprite_frames->get_frame_texture(anim, i);
		if (tex.is_null()) {
			UtilityFunctions::push_error("DirectionalBullets2D: animation '" + String(anim) + "' frame " + String::num_int64(i) + " has null texture.");
			return false; // previous cache untouched (swap only on success below)
		}
		const float dur = p_sprite_frames->get_frame_duration(anim, i);
		if (!Math::is_finite((double)dur)) {
			UtilityFunctions::push_error("DirectionalBullets2D: animation '" + String(anim) + "' frame " + String::num_int64(i) + " has non-finite duration, using 0.");
		}
		if (anim_override_frame_color) {
			// Exact-color bullets: whitened copy (alpha preserved) so the
			// volley tint reads exactly. Same fallback contract as the
			// effect-layer override: unreadable frames keep the original
			// art with one warning per rebuild, never a blank.
			// Factory-cached: the whiten reads pixels back from the GPU, so
			// rebuilding it on every spawn/pool reuse was a per-frame stall.
			Ref<Texture2D> white_tex;
			if (bullet_factory != nullptr) {
				white_tex = bullet_factory->get_whitened_frame(tex);
			} else {
				Ref<Image> white = BulletEffectLayerData2D::whiten_image_copy(BulletEffectLayerData2D::read_frame_image(tex));
				if (white.is_valid()) {
					Ref<ImageTexture> fresh;
					fresh.instantiate();
					fresh->set_image(white);
					white_tex = fresh;
				}
			}
			if (white_tex.is_valid()) {
				frames.push_back(white_tex);
			} else {
				if (!whiten_warned) {
					whiten_warned = true;
					UtilityFunctions::push_warning("DirectionalBullets2D: override_frame_color could not read a frame of '" + String(anim) + "', keeping the original art for unreadable frames.");
				}
				frames.push_back(tex);
			}
		} else {
			frames.push_back(tex);
		}
		secs.push_back((!Math::is_finite((double)dur) || dur <= 0.0f) ? 0.0 : (double)dur / fps);
	}
	anim_source = p_sprite_frames;
	anim_name = anim;
	anim_frames.swap(frames);
	anim_frame_secs.swap(secs);
	anim_loop = p_sprite_frames->get_animation_loop(anim);
	anim_paused = false;
	anim_finished = false;
	anim_frame_index = 0;
	anim_frame_time_left = anim_frame_secs[0];
	set_texture(anim_frames[0]);
	return true;
}

bool DirectionalBullets2D::play_sprite_animation(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_animation) {
	if (p_sprite_frames.is_null()) {
		UtilityFunctions::push_error("DirectionalBullets2D play_sprite_animation: sprite_frames is null.");
		return false;
	}
	// Empty follows the same auto-resolve rules as spawn ("default" if present,
	// else the first animation with frames) instead of erroring.
	return rebuild_sprite_animation(p_sprite_frames, p_animation);
}

bool DirectionalBullets2D::play_sprite_animation_name(const StringName &p_animation) {
	if (anim_source.is_null()) {
		UtilityFunctions::push_error("DirectionalBullets2D play_sprite_animation_name: no SpriteFrames cached yet, call play_sprite_animation first.");
		return false;
	}
	// Empty follows the same auto-resolve rules as spawn.
	return rebuild_sprite_animation(anim_source, p_animation);
}

bool DirectionalBullets2D::restart_sprite_animation() {
	if (anim_frames.empty()) {
		UtilityFunctions::push_error("DirectionalBullets2D restart_sprite_animation: no baked animation to restart.");
		return false;
	}
	anim_frame_index = 0;
	anim_frame_time_left = anim_frame_secs[0];
	anim_paused = false;
	anim_finished = false;
	set_texture(anim_frames[0]);
	return true;
}

Vector2 DirectionalBullets2D::resolve_quad_size(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_animation, Vector2 override_size) {
	if (override_size.is_finite() && override_size.x > 0.0f && override_size.y > 0.0f) {
		return override_size;
	}
	if (override_size != Vector2(0, 0) && (!override_size.is_finite() || override_size.x <= 0.0f || override_size.y <= 0.0f)) {
		WarnOnce2D::warn(0, 3u, (int64_t)(override_size.x * 1024.0f), (int64_t)(override_size.y * 1024.0f), "DirectionalBullets2D: texture_size override is non-finite or non-positive, deriving size from the first frame.");
	}
	// Silent fallback: rebuild_sprite_animation owns all error reporting (spawn calls
	// both, so resolving loudly here would print every failure twice).
	StringName anim;
	if (!resolve_sprite_animation_quiet(p_sprite_frames, p_animation, anim)) {
		return Vector2(32, 32);
	}	if (p_sprite_frames->get_frame_count(anim) > 0) {
		if (const Ref<Texture2D> tex = p_sprite_frames->get_frame_texture(anim, 0); tex.is_valid()) {
			if (const Ref<AtlasTexture> atlas = tex; atlas.is_valid()) {
				const Vector2 region = atlas->get_region().size;
				if (region.is_finite() && region.x > 0.0f && region.y > 0.0f) {
					return region;
				}
			}
			const Vector2 size = tex->get_size();
			if (size.is_finite() && size.x > 0.0f && size.y > 0.0f) {
				return size;
			}
		}
	}
	return Vector2(32, 32);
}

// Always called last (texture comes from rebuild_sprite_animation, called by spawn/enable)
void DirectionalBullets2D::finalize_set_up(
		const Ref<Resource> &new_shared_bullets_custom_data,
		const Ref<Material> &new_material,
		int new_z_index,
		int new_light_mask,
		int new_visibility_layer,
		const Color &new_self_modulate,
		const Dictionary &new_instance_shader_parameters) {
	// Bullets custom data. Always assigned (null clears) so pool reuse never leaks
	// the previous owner's data into a new spawn.
	shared_bullets_custom_data = new_shared_bullets_custom_data;

	if (new_material.is_valid()) {
		godot::Ref<ShaderMaterial> shader_material = new_material;
		// If a shader material was passed and the user has provided instance shader parameters
		if (shader_material.is_valid() && new_instance_shader_parameters.is_empty() == false) {
			// Keys removed since the last life must be reset on the CanvasItem:
			// clearing only the C++ dict leaves stale GPU overrides behind.
			for (const String &old_key : applied_instance_shader_keys) {
				if (!old_key.is_empty() && !new_instance_shader_parameters.has(old_key)) {
					set_instance_shader_parameter(old_key, Variant());
				}
			}
			applied_instance_shader_keys.clear();
			instance_shader_parameters = new_instance_shader_parameters;

			const Array &keys = new_instance_shader_parameters.keys();
			for (int i = 0; i < keys.size(); ++i) {
				const String key = keys[i];
				if (key.is_empty()) {
					continue;
				}
				const Variant &value = new_instance_shader_parameters[key];

				set_instance_shader_parameter(key, value);
				applied_instance_shader_keys.push_back(key);
			}
		} else {
			// Shader without params (or non-shader material handled below): drop the
			// previous owner's dict AND its live CanvasItem overrides so pool
			// reuse can't leak stale entries into the next volley.
			clear_applied_instance_shader_overrides();
			instance_shader_parameters.clear();
		}

		set_material(new_material);
	} else {
		clear_applied_instance_shader_overrides();
		instance_shader_parameters.clear();
		set_material(nullptr);
	}

	// Z Index
	set_z_index(new_z_index);

	// Light mask
	set_light_mask(new_light_mask);

	// Visibility layer
	set_visibility_layer(new_visibility_layer);

	// Whole-volley tint. Always assigned (white clears) so pool reuse never
	// leaks the previous owner's color into a new spawn.
	set_self_modulate(new_self_modulate);
}

// OTHER

void DirectionalBullets2D::set_rotation_data(const TypedArray<BulletRotationData2D> &rotation_data, bool new_rotate_only_textures, bool tile_short_arrays) {
	int amount_rotation_data = rotation_data.size();

	// Strict rule: entry i rotates bullet i only. Slots past the end keep
	// zeros (shared rotation fills those gaps afterwards).
	// With the tile checkbox, short arrays wrap (i % size).
	if (amount_rotation_data == 0) {
		is_rotation_data_active = false;
		all_rotation_speed.clear();
		all_max_rotation_speed.clear();
		all_rotation_acceleration.clear();
		// Rotation is off entirely, so no slot is seeded: a later
		// set_shared_bullet_rotation_data full-seeds every slot instead.
		reset_per_bullet_rotation_presence();
		// The flag must follow the new data even when rotation is disabled:
		// otherwise a dead owner's texture/shape-follow mode leaks into the
		// next life (e.g. set_shared_bullet_rotation_data reuses this flag).
		rotate_only_textures = new_rotate_only_textures;
		return;
	}

	is_rotation_data_active = true;

	if (amount_rotation_data != amount_bullets) {
		WarnOnce2D::warn(warn_data_id, 4u, amount_rotation_data, amount_bullets, "DirectionalBullets2D: all_bullet_rotation_data size (" + String::num_int64(amount_rotation_data) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets get zero spin unless shared rotation fills them" + String(tile_short_arrays ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_rotation_data to wrap, or provide one entry per bullet)."));
	}

	// Validate every element we are about to read. Null/wrong-type entries
	// seed zeros for that bullet only — never for its siblings. Non-finite
	// values fail open the same way.
	for (int i = 0; i < amount_rotation_data; ++i) {
		BulletRotationData2D *entry = Object::cast_to<BulletRotationData2D>(rotation_data[i]);
		if (entry == nullptr) {
			UtilityFunctions::push_error("Invalid rotation data at index " + String::num_int64(i) + ": expected BulletRotationData2D. Using zeros for that bullet.");
		} else if (!Math::is_finite(entry->rotation_speed) || !Math::is_finite(entry->max_rotation_speed) || !Math::is_finite(entry->rotation_acceleration)) {
			UtilityFunctions::push_error("Non-finite rotation data at index " + String::num_int64(i) + ": rotation values must be finite. Using zeros for that bullet.");
		}
	}

	rotate_only_textures = new_rotate_only_textures;

	// Clear existing data (avoids freeing the actual memory, instead only the .amount_bullets is changed which allows me to push brand new elements as if the vector is empty/ overwrite existing but not accessible ones)
	all_rotation_speed.clear();
	all_max_rotation_speed.clear();
	all_rotation_acceleration.clear();
	// Fresh seed: every slot below is re-derived from its own entry, so the
	// presence bit resets to all-zero and the shared fallback may fill the gaps
	// again.
	reset_per_bullet_rotation_presence();

	if (amount_bullets > (int)all_rotation_speed.capacity()) {
		all_rotation_speed.reserve(amount_bullets);
		all_max_rotation_speed.reserve(amount_bullets);
		all_rotation_acceleration.reserve(amount_bullets);
	}
	// Strict: slot i reads entry i. Uncovered slots seed zeros so the
	// shared-rotation fallback can fill those gaps afterwards. With the tile
	// checkbox, short arrays wrap (i % size).
	for (int i = 0; i < amount_bullets; ++i) {
		const int src = tile_short_arrays ? (i % amount_rotation_data) : i;
		BulletRotationData2D *entry = (src >= 0 && src < amount_rotation_data) ? Object::cast_to<BulletRotationData2D>(rotation_data[src]) : nullptr;
		if (entry == nullptr || !Math::is_finite(entry->rotation_speed) || !Math::is_finite(entry->max_rotation_speed) || !Math::is_finite(entry->rotation_acceleration)) {
			all_rotation_speed.emplace_back(0.0);
			all_max_rotation_speed.emplace_back(0.0);
			all_rotation_acceleration.emplace_back(0.0);
			// Out-of-range slot (no entry covers it) or an invalid entry: a
			// genuine gap, so the shared fallback may fill it.
			mark_per_bullet_rotation_presence(i, false);
			continue;
		}
		all_rotation_speed.emplace_back(entry->rotation_speed);
		all_max_rotation_speed.emplace_back(entry->max_rotation_speed);
		all_rotation_acceleration.emplace_back(entry->rotation_acceleration);
		// The user authored an entry for this slot (an all-zero "no spin"
		// included): shared rotation must not touch it.
		mark_per_bullet_rotation_presence(i, true);
	}
}

Ref<BulletRotationData2D> DirectionalBullets2D::get_bullet_rotation_data(int bullet_index) const {
	Ref<BulletRotationData2D> rotation_data = memnew(BulletRotationData2D);

	if (!validate_bullet_index(bullet_index, "get_bullet_rotation_data")) {
		return rotation_data;
	}

	if (bullet_index < 0 || bullet_index >= (int)all_rotation_speed.size() || bullet_index >= (int)all_max_rotation_speed.size() || bullet_index >= (int)all_rotation_acceleration.size()) {
		return rotation_data;
	}
	rotation_data->rotation_speed = all_rotation_speed[bullet_index];
	rotation_data->max_rotation_speed = all_max_rotation_speed[bullet_index];
	rotation_data->rotation_acceleration = all_rotation_acceleration[bullet_index];

	return rotation_data;
}

void DirectionalBullets2D::set_bullet_rotation_data(int bullet_index, const Ref<BulletRotationData2D> &new_bullet_rotation_data) {
	if (!validate_bullet_index(bullet_index, "set_bullet_rotation_data")) {
		return;
	}

	if (new_bullet_rotation_data.is_null()) {
		UtilityFunctions::push_error("set_bullet_rotation_data: new_bullet_rotation_data is null.");
		return;
	}

	if (!Math::is_finite(new_bullet_rotation_data->rotation_speed) || !Math::is_finite(new_bullet_rotation_data->max_rotation_speed) || !Math::is_finite(new_bullet_rotation_data->rotation_acceleration)) {
		UtilityFunctions::push_error("set_bullet_rotation_data: rotation values must be finite.");
		return;
	}


	// A rotation-less volley (empty seed path) has empty vectors: size them
	// here so a live write wakes rotation instead of silently no-op'ing.
	// amount_bullets is fixed for the volley's life, so resize is exact.
	if (all_rotation_speed.empty() || all_max_rotation_speed.empty() || all_rotation_acceleration.empty()) {
		all_rotation_speed.assign(amount_bullets, 0.0);
		all_max_rotation_speed.assign(amount_bullets, 0.0);
		all_rotation_acceleration.assign(amount_bullets, 0.0);
	}

	if (bullet_index < 0 || bullet_index >= (int)all_rotation_speed.size() || bullet_index >= (int)all_max_rotation_speed.size() || bullet_index >= (int)all_rotation_acceleration.size()) {
		return;
	}

	// A direct per-bullet write is as authoritative as a seeded entry: shared
	// rotation must never overwrite it, including the all-zero "no spin" case.
	mark_per_bullet_rotation_presence(bullet_index, true);

	all_rotation_speed[bullet_index] = new_bullet_rotation_data->rotation_speed;
	all_max_rotation_speed[bullet_index] = new_bullet_rotation_data->max_rotation_speed;
	all_rotation_acceleration[bullet_index] = new_bullet_rotation_data->rotation_acceleration;
	// A live write on a rotation-less volley must wake the tick branch:
	// set_rotation_data only flips this on spawn/enable seeds. Exact-size
	// writes are per-bullet; anything else fans out like the seed path.
	is_rotation_data_active = true;
}

TypedArray<BulletRotationData2D> DirectionalBullets2D::all_bullets_get_rotation_data(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_rotation_data");

	TypedArray<BulletRotationData2D> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_rotation_data(i));
	}

	return arr;
}

void DirectionalBullets2D::all_bullets_set_rotation_data(const Ref<BulletRotationData2D> &new_bullet_rotation_data, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_rotation_data");

	if (new_bullet_rotation_data.is_null()) {
		UtilityFunctions::push_error("all_bullets_set_rotation_data: new_bullet_rotation_data is null.");
		return;
	}

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_rotation_data(i, new_bullet_rotation_data);
	}
}

void DirectionalBullets2D::clear_bullet_rotation_data() {
	is_rotation_data_active = false;
	all_rotation_speed.clear();
	all_max_rotation_speed.clear();
	all_rotation_acceleration.clear();
	// Presence must go with the values: without this, Directional keeps stale
	// per-bullet bits and a later shared fallback skips slots that are now
	// genuine gaps (rotation cleared means "no seed", not "authored zero").
	reset_per_bullet_rotation_presence();
}

Transform2D DirectionalBullets2D::generate_texture_transform(Transform2D transf, bool is_texture_rotation_permanent, real_t texture_rotation_radians, int bullet_index) {
	if (is_texture_rotation_permanent) {
		// Same texture rotation no matter the rotation of the bullet's transform
		transf.set_rotation(texture_rotation_radians);
	} else {
		// The rotation of the texture will be influenced by the rotation of the bullet transform
		transf.set_rotation(transf.get_rotation() + texture_rotation_radians);
	}

	// No per-instance server write here: set_up_bullet_instances writes every
	// instance into batch_buffer and uploads them with ONE set_buffer call
	// (N set_instance_transform_2d calls were N boundary crossings + N
	// server-side cache updates per spawn).
	(void)bullet_index;
	return transf;
}

void DirectionalBullets2D::set_up_area(const int collision_layer, const int collision_mask, bool new_monitorable, const RID &physics_space) {
	monitorable = new_monitorable;
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_up_area: physics server or area is not ready, bullets will not collide.");
		return;
	}
	RID space_to_use = physics_space;
	if (!space_to_use.is_valid() && bullet_factory != nullptr) {
		space_to_use = bullet_factory->physics_space;
	}
	if (!space_to_use.is_valid()) {
		UtilityFunctions::push_error("set_up_area: no valid physics space, bullets will not collide. Set BulletFactory2D physics_space first.");
		return;
	}
	physics_server->area_set_space(area, space_to_use);
	physics_server->area_set_monitorable(area, monitorable);
	physics_server->area_set_area_monitor_callback(area, callable_mp(this, &DirectionalBullets2D::area_entered_func));
	physics_server->area_set_monitor_callback(area, callable_mp(this, &DirectionalBullets2D::body_entered_func));
	physics_server->area_set_collision_layer(area, collision_layer);
	physics_server->area_set_collision_mask(area, collision_mask);
}

Transform2D DirectionalBullets2D::generate_collision_shape_transform_for_area(Transform2D transf, const Vector2 &collision_shape_offset, int bullet_index) {
	// The rotation of each transform
	real_t curr_bullet_rotation = transf.get_rotation();

	// Rotate collision_shape_offset based on the direction of the bullets (single cos/sin) - early out if zero (common case)
	Vector2 rotated_offset = Vector2(0, 0);
	if (collision_shape_offset != Vector2(0, 0)) {
		rotated_offset = collision_shape_offset.rotated(curr_bullet_rotation);
	}

	transf.set_origin(transf.get_origin() + rotated_offset);

	physics_server->area_set_shape_transform(area, bullet_index, transf);
	return transf;
}

void DirectionalBullets2D::apply_volley_shape_data() {
	if (physics_server == nullptr || !volley_shape.is_valid() || shape_data_matches_applied()) {
		return;
	}
	switch (cached_effective_shape_type) {
		case PhysicsServer2D::SHAPE_CIRCLE:
			physics_server->shape_set_data(volley_shape, cached_circle_radius);
			break;
		case PhysicsServer2D::SHAPE_CAPSULE:
			physics_server->shape_set_data(volley_shape, Vector2(cached_capsule_radius, cached_capsule_height));
			break;
		case PhysicsServer2D::SHAPE_RECTANGLE:
		default:
			physics_server->shape_set_data(volley_shape, cached_rect_size / 2);
			break;
	}
	mark_shape_data_applied();
}

void DirectionalBullets2D::release_volley_shape() {
	if (physics_server == nullptr) {
		return;
	}
	if (area.is_valid()) {
		physics_server->area_clear_shapes(area);
	}
	area_shape_count = 0;
	if (volley_shape.is_valid()) {
		physics_server->free_rid(volley_shape);
	}
	volley_shape = RID();
	shape_data_applied = false;
}

void DirectionalBullets2D::generate_physics_shapes_for_area(int amount) {
	// Fresh RID carries no data yet. The data is pushed BEFORE the shape gets
	// any owner, so the push costs nothing area-wide; each add below only
	// queues a deferred shape update (O(1)).
	shape_data_applied = false;
	if (!volley_shape.is_valid()) {
		// Type already resolved + error printed once in cache_collision_shape_typed().
		volley_shape = CollisionShapeHelper2D::create_server_shape(physics_server, cached_effective_shape_type);
	}
	apply_volley_shape_data();
	for (int i = 0; i < amount; ++i) {
		physics_server->area_add_shape(area, volley_shape);
	}
	area_shape_count = amount;
}

void DirectionalBullets2D::set_all_physics_shapes_enabled_for_area(bool enable) {
	for (int i = 0; i < amount_bullets; ++i) {
		physics_server->area_set_shape_disabled(area, i, !enable);
	}
}

Ref<BulletSpeedData2D> DirectionalBullets2D::get_bullet_speed_data(int bullet_index) const {
	Ref<BulletSpeedData2D> speed_data = memnew(BulletSpeedData2D);

	if (!validate_bullet_index(bullet_index, "get_bullet_speed_data")) {
		return speed_data;
	}

	if (bullet_index < 0 || bullet_index >= (int)all_cached_speed.size() || bullet_index >= (int)all_cached_max_speed.size() || bullet_index >= (int)all_cached_acceleration.size()) {
		return speed_data;
	}
	speed_data->speed = all_cached_speed[bullet_index];
	speed_data->max_speed = all_cached_max_speed[bullet_index];
	speed_data->acceleration = all_cached_acceleration[bullet_index];

	return speed_data;
}

void DirectionalBullets2D::set_bullet_speed_data(int bullet_index, const Ref<BulletSpeedData2D> &new_bullet_speed_data) {
	if (!validate_bullet_index(bullet_index, "set_bullet_speed_data")) {
		return;
	}

	if (new_bullet_speed_data.is_null()) {
		UtilityFunctions::push_error("set_bullet_speed_data: new_bullet_speed_data is null.");
		return;
	}

	if (shared_bullet_curves_data.is_valid() && shared_bullet_curves_data->movement_speed_curve.is_valid()) {
		UtilityFunctions::push_warning("You are trying to set bullet speed data directly while having a movement speed curve assigned to the shared curves data. The curve will override any direct speed data changes. Set the curve to null first if you want to set speed data directly.");
		return;
	}

	BulletCurvesData2D *curves_data = find_bullet_curves_data_ptr(bullet_index);

	if (curves_data != nullptr && curves_data->movement_speed_curve.is_valid()) {
		UtilityFunctions::push_warning("You are trying to set bullet speed data directly while having a movement speed curve assigned as an individual bullet curves data. The curve will override any direct speed data changes. Set the curve to null first if you want to set speed data directly.");
		return;
	}


	if (!Math::is_finite(new_bullet_speed_data->speed) || !Math::is_finite(new_bullet_speed_data->max_speed) || !Math::is_finite(new_bullet_speed_data->acceleration)) {
		UtilityFunctions::push_error("set_bullet_speed_data: speed values must be finite.");
		return;
	}

	if (bullet_index < 0 || bullet_index >= (int)all_cached_speed.size() || bullet_index >= (int)all_cached_max_speed.size() || bullet_index >= (int)all_cached_acceleration.size() || bullet_index >= (int)all_cached_velocity.size() || bullet_index >= (int)all_cached_direction.size()) {
		return;
	}

	all_cached_speed[bullet_index] = new_bullet_speed_data->speed;
	all_cached_max_speed[bullet_index] = new_bullet_speed_data->max_speed;
	all_cached_acceleration[bullet_index] = new_bullet_speed_data->acceleration;
	all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * new_bullet_speed_data->speed + inherited_velocity_offset;
	// A direct per-bullet write is as authoritative as a seeded entry: the
	// shared fallback must never overwrite it, including an all-zero "freeze".
	mark_per_bullet_speed_presence(bullet_index, true);
}

TypedArray<BulletSpeedData2D> DirectionalBullets2D::all_bullets_get_speed_data(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_speed_data");

	TypedArray<BulletSpeedData2D> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_speed_data(i));
	}

	return arr;
}

void DirectionalBullets2D::all_bullets_set_speed_data(const Ref<BulletSpeedData2D> &new_bullet_speed_data, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_speed_data");

	if (new_bullet_speed_data.is_null()) {
		UtilityFunctions::push_error("all_bullets_set_speed_data: new_bullet_speed_data is null.");
		return;
	}

	if (shared_bullet_curves_data.is_valid() && shared_bullet_curves_data->movement_speed_curve.is_valid()) {
		UtilityFunctions::push_warning("You are trying to set bullet speed data directly while having a movement speed curve assigned. The curve will override any direct speed data changes. Set the curve to null first if you want to set speed data directly.");
		return;
	}

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_speed_data(i, new_bullet_speed_data);
	}
}

Vector2 DirectionalBullets2D::get_bullet_direction(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_direction")) {
		return Vector2();
	}

	if (bullet_index < 0 || bullet_index >= (int)all_cached_direction.size()) {
		return Vector2();
	}
	return all_cached_direction[bullet_index];
}

void DirectionalBullets2D::set_bullet_direction(int bullet_index, const Vector2 &new_direction) {
	if (!validate_bullet_index(bullet_index, "set_bullet_direction")) {
		return;
	}

	if (!new_direction.is_finite()) {
		UtilityFunctions::push_error("set_bullet_direction: new_direction must be finite, keeping the old direction.");
		return;
	}

	if (new_direction.length_squared() < 0.000001) {
		UtilityFunctions::push_error("set_bullet_direction: new_direction is zero, keeping the old direction.");
		return;
	}

	if (shared_bullet_curves_data.is_valid() && (shared_bullet_curves_data->x_direction_curve.is_valid() || shared_bullet_curves_data->y_direction_curve.is_valid())) {
		UtilityFunctions::push_warning("You are trying to set bullet direction directly while having a direction curve assigned to the shared curves data. The curve will override any direct speed data changes. Set the curve to null first if you want to set speed data directly.");
		return;
	}

	BulletCurvesData2D *curves_data = find_bullet_curves_data_ptr(bullet_index);

	if (curves_data != nullptr && (curves_data->x_direction_curve.is_valid() || curves_data->y_direction_curve.is_valid())) {
		UtilityFunctions::push_warning("You are trying to set bullet direction directly while having a direction curve assigned to the individual curves data. The curve will override any direct speed data changes. Set the curve to null first if you want to set speed data directly.");
		return;
	}

	if (bullet_index < 0 || bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_velocity.size() || bullet_index >= (int)all_cached_speed.size()) {
		return;
	}
	all_cached_direction[bullet_index] = new_direction.normalized();
	all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * all_cached_speed[bullet_index] + inherited_velocity_offset;
}

TypedArray<Vector2> DirectionalBullets2D::all_bullets_get_direction(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_direction");

	TypedArray<Vector2> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_direction(i));
	}

	return arr;
}

void DirectionalBullets2D::all_bullets_set_direction(const Vector2 &new_direction, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_direction");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_direction(i, new_direction);
	}
}

real_t DirectionalBullets2D::get_bullet_texture_rotation_radians(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_texture_rotation_radians")) {
		return 0.0;
	}
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return 0.0;
	}

	// Same space the setter writes: the volley-wide texture offset is baked
	// into the instance basis, so strip it here or set(get()) would add the
	// offset again on every round-trip.
	const real_t instance_rotation = all_cached_instance_transforms[bullet_index].get_rotation();
	if (cache_texture_rotation_radians == 0.0) {
		return instance_rotation;
	}
	return Math::wrapf(instance_rotation - cache_texture_rotation_radians, (real_t)-Math::PI, (real_t)Math::PI);
}

void DirectionalBullets2D::set_bullet_texture_rotation_radians(int bullet_index, real_t new_rotation_radians) {
	if (!validate_bullet_index(bullet_index, "set_bullet_texture_rotation_radians")) {
		return;
	}
	if (!Math::is_finite(new_rotation_radians)) {
		UtilityFunctions::push_error("set_bullet_texture_rotation_radians: rotation must be finite, keeping the old rotation.");
		return;
	}
	if (bullet_index >= (int)all_cached_instance_transforms.size()) {
		return;
	}

	auto &curr_transf = all_cached_instance_transforms[bullet_index];
	// Absolute visual rotation: the volley-wide texture offset is part of
	// the instance basis (spawn path bakes it in), so writing absolute
	// would double-count it in adjust_direction_based_on_rotation (which
	// strips exactly one offset). Compose like the towards_position setter.
	curr_transf.set_rotation(new_rotation_radians + cache_texture_rotation_radians);
	sync_shape_transform_from_instance(bullet_index, curr_transf);

	// Instantly apply the updated transform so paused factories don't render stale visuals.
	if (all_bullets_enabled_set.contains(bullet_index)) {
		multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(curr_transf));
	}

	// Stick-relative attachments follow the rotation like set_bullet_transform
	// does: without this they sit at the old angle until the next tick (and
	// forever while paused).
	carry_attachment_with_transform(bullet_index, curr_transf, Vector2(0, 0));

	update_bullet_previous_transform_for_interpolation(bullet_index);
}

TypedArray<real_t> DirectionalBullets2D::all_bullets_get_texture_rotation_radians(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_texture_rotation_radians");

	TypedArray<real_t> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_texture_rotation_radians(i));
	}

	return arr;
}

void DirectionalBullets2D::all_bullets_set_texture_rotation_radians(real_t new_rotation_radians, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_texture_rotation_radians");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_texture_rotation_radians(i, new_rotation_radians);
	}
}

real_t DirectionalBullets2D::get_bullet_texture_rotation_degrees(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_texture_rotation_degrees")) {
		return 0.0;
	}
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return 0.0;
	}

	return Math::rad_to_deg(get_bullet_texture_rotation_radians(bullet_index));
}

void DirectionalBullets2D::set_bullet_texture_rotation_degrees(int bullet_index, real_t new_rotation_degrees) {
	if (!validate_bullet_index(bullet_index, "set_bullet_texture_rotation_degrees")) {
		return;
	}
	if (!Math::is_finite(new_rotation_degrees)) {
		UtilityFunctions::push_error("set_bullet_texture_rotation_degrees: rotation must be finite, keeping the old rotation.");
		return;
	}
	if (bullet_index >= (int)all_cached_instance_transforms.size()) {
		return;
	}

	auto &curr_transf = all_cached_instance_transforms[bullet_index];
	curr_transf.set_rotation(Math::deg_to_rad(new_rotation_degrees) + cache_texture_rotation_radians);
	sync_shape_transform_from_instance(bullet_index, curr_transf);

	// Instantly apply the updated transform so paused factories don't render stale visuals.
	if (all_bullets_enabled_set.contains(bullet_index)) {
		multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(curr_transf));
	}

	carry_attachment_with_transform(bullet_index, curr_transf, Vector2(0, 0));

	update_bullet_previous_transform_for_interpolation(bullet_index);
}

TypedArray<real_t> DirectionalBullets2D::all_bullets_get_texture_rotation_degrees(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_texture_rotation_degrees");

	TypedArray<real_t> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_texture_rotation_degrees(i));
	}

	return arr;
}

void DirectionalBullets2D::all_bullets_set_texture_rotation_degrees(real_t new_rotation_degrees, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_texture_rotation_degrees");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_texture_rotation_degrees(i, new_rotation_degrees);
	}
}

Transform2D DirectionalBullets2D::get_bullet_transform(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_transform")) {
		return Transform2D();
	}
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return Transform2D();
	}

	// Caches are global; return the cache directly so get()/set() round-trip in
	// the same space. Use get_bullet_global_transform() for an explicit world read.
	return all_cached_instance_transforms[bullet_index];
}

Transform2D DirectionalBullets2D::get_bullet_global_transform(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_global_transform")) {
		return Transform2D();
	}
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return Transform2D();
	}

	return all_cached_instance_transforms[bullet_index];
}

Vector2 DirectionalBullets2D::get_bullet_velocity(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_velocity")) {
		return Vector2();
	}

	if (bullet_index < 0 || bullet_index >= (int)all_cached_velocity.size()) {
		return Vector2();
	}
	return all_cached_velocity[bullet_index];
}
void DirectionalBullets2D::set_bullet_transform(int bullet_index, const Transform2D &new_transform, bool set_direction_based_on_transform) {
	if (!validate_bullet_index(bullet_index, "set_bullet_transform")) {
		return;
	}
	if (!new_transform.get_origin().is_finite() || !Math::is_finite(new_transform.get_rotation()) || !new_transform.get_scale().is_finite()) {
		UtilityFunctions::push_error("set_bullet_transform: new_transform must be finite, keeping the old transform.");
		return;
	}
	// A degenerate (near-zero or singular) scale collapses columns[0] to zero, which silently
	// zeroes the movement direction wherever it is derived from the transform
	// (adjust_direction_based_on_rotation tick path -> velocity falls back to
	// the inherited offset only). A (0, 1) scale passes a length check but has
	// determinant 0, so centralise on the invertibility check. Reject instead
	// of storing a poisoned basis.
	if (new_transform.get_scale().length_squared() < 0.00000001 || !is_transform_invertible_safe(new_transform)) {
		UtilityFunctions::push_error("set_bullet_transform: scale must be non-zero and non-singular, keeping the old transform.");
		return;
	}
	if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
		return;
	}
	auto &curr_bullet_transf = all_cached_instance_transforms[bullet_index];
	auto &curr_bullet_origin = all_cached_instance_origin[bullet_index];

	const Vector2 origin_delta = new_transform.get_origin() - curr_bullet_origin;

	curr_bullet_transf = new_transform;
	curr_bullet_origin = new_transform.get_origin();

	sync_shape_transform_from_instance(bullet_index, curr_bullet_transf);

	// Instantly apply the updated transforms
	if (all_bullets_enabled_set.contains(bullet_index)) {
		multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(curr_bullet_transf));
	}

	// Carry the attachment along so it doesn't stay behind at the old position.
	// Stick-relative attachments recompute from the new transform (same as the next
	// tick would); non-stick ones translate by the jump delta (they never heal otherwise).
	if (bullet_factory != nullptr && bullet_index >= 0 && bullet_index < (int)attachments.size() && attachments[bullet_index] != nullptr) {
		if (attachment_stick_relative_to_bullet[bullet_index]) {
			attachment_transforms[bullet_index] = calculate_attachment_global_transf(bullet_index, curr_bullet_transf);
		} else {
			attachment_transforms[bullet_index] = attachment_transforms[bullet_index].translated(origin_delta);
		}
		if (!bullet_factory->use_physics_interpolation) {
			attachments[bullet_index]->set_global_transform(attachment_transforms[bullet_index]);
		}
	}

	// Update direction if requested. A direction curve owns the direction, so
	// skip just this part (the transform itself is still applied above).
	if (set_direction_based_on_transform) {
		bool direction_owned_by_curve = shared_bullet_curves_data.is_valid() && (shared_bullet_curves_data->x_direction_curve.is_valid() || shared_bullet_curves_data->y_direction_curve.is_valid());
		BulletCurvesData2D *transform_curves_data = direction_owned_by_curve ? nullptr : find_bullet_curves_data_ptr(bullet_index);
		if (transform_curves_data != nullptr && (transform_curves_data->x_direction_curve.is_valid() || transform_curves_data->y_direction_curve.is_valid())) {
			direction_owned_by_curve = true;
		}
		if (direction_owned_by_curve) {
			UtilityFunctions::push_warning("set_bullet_transform was asked to derive the direction while a direction curve is assigned. The curve owns the direction, so it was left alone. Set the curve to null first if you want the transform to steer.");
		} else {
			// Strip the volley-wide texture offset like the tick's adjust
			// path does: the instance basis carries it, the logical
			// direction must not.
			Vector2 new_direction = Vector2(1, 0).rotated(curr_bullet_transf.get_rotation() - cache_texture_rotation_radians);
			if (bullet_index >= 0 && bullet_index < (int)all_cached_direction.size() && bullet_index < (int)all_cached_velocity.size() && bullet_index < (int)all_cached_speed.size()) {
				all_cached_direction[bullet_index] = new_direction.normalized();
				all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * all_cached_speed[bullet_index] + inherited_velocity_offset;
			}
		}
	}

	update_bullet_previous_transform_for_interpolation(bullet_index);
}

TypedArray<Transform2D> DirectionalBullets2D::all_bullets_get_transforms(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_transforms");

	TypedArray<Transform2D> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_transform(i));
	}

	return arr;
}

void DirectionalBullets2D::all_bullets_set_transforms(const Transform2D &new_transform, bool set_direction_based_on_transform, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_transforms");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_transform(i, new_transform, set_direction_based_on_transform);
	}
}

void DirectionalBullets2D::set_bullet_direction_towards_position(int bullet_index, const Vector2 &target_position) {
	if (!validate_bullet_index(bullet_index, "set_bullet_direction_towards_position")) {
		return;
	}
	if (!target_position.is_finite()) {
		UtilityFunctions::push_error("set_bullet_direction_towards_position: target_position must be finite.");
		return;
	}

	if (shared_bullet_curves_data.is_valid() && (shared_bullet_curves_data->x_direction_curve.is_valid() || shared_bullet_curves_data->y_direction_curve.is_valid())) {
		UtilityFunctions::push_warning("You are trying to set bullet direction directly while having a direction curve assigned to the shared curves data. The curve will override any direct direction changes. Set the curve to null first if you want to set direction directly.");
		return;
	}

	BulletCurvesData2D *towards_curves_data = find_bullet_curves_data_ptr(bullet_index);

	if (towards_curves_data != nullptr && (towards_curves_data->x_direction_curve.is_valid() || towards_curves_data->y_direction_curve.is_valid())) {
		UtilityFunctions::push_warning("You are trying to set bullet direction directly while having a direction curve assigned to the individual curves data. The curve will override any direct direction changes. Set the curve to null first if you want to set direction directly.");
		return;
	}

	if (bullet_index < 0 || bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_velocity.size() || bullet_index >= (int)all_cached_speed.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
		return;
	}
	const Vector2 to_target = target_position - all_cached_instance_origin[bullet_index];
	if (to_target.length_squared() < 0.000001) {
		UtilityFunctions::push_error("set_bullet_direction_towards_position: bullet is already at the target position, keeping the old direction.");
		return;
	}
	all_cached_direction[bullet_index] = to_target.normalized();
	all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * all_cached_speed[bullet_index] + inherited_velocity_offset;
}

void DirectionalBullets2D::all_bullets_set_direction_towards_position(const Vector2 &target_position, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_direction_towards_position");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_direction_towards_position(i, target_position);
	}
}

void DirectionalBullets2D::set_bullet_direction_towards_node2d(int bullet_index, const Node2D *target_node) {
	if (!validate_bullet_index(bullet_index, "set_bullet_direction_towards_node2d")) {
		return;
	}

	if (target_node == nullptr) {
		UtilityFunctions::push_error("The target_node provided to set_bullet_direction_towards_node2d is null. Cannot set direction towards a null node.");
		return;
	}

	const Vector2 target_position = target_node->get_global_position();
	if (!target_position.is_finite()) {
		UtilityFunctions::push_error("set_bullet_direction_towards_node2d: target position must be finite.");
		return;
	}
	if (shared_bullet_curves_data.is_valid() && (shared_bullet_curves_data->x_direction_curve.is_valid() || shared_bullet_curves_data->y_direction_curve.is_valid())) {
		UtilityFunctions::push_warning("You are trying to set bullet direction directly while having a direction curve assigned to the shared curves data. The curve will override any direct direction changes. Set the curve to null first if you want to set direction directly.");
		return;
	}

	BulletCurvesData2D *towards_node_curves_data = find_bullet_curves_data_ptr(bullet_index);

	if (towards_node_curves_data != nullptr && (towards_node_curves_data->x_direction_curve.is_valid() || towards_node_curves_data->y_direction_curve.is_valid())) {
		UtilityFunctions::push_warning("You are trying to set bullet direction directly while having a direction curve assigned to the individual curves data. The curve will override any direct direction changes. Set the curve to null first if you want to set direction directly.");
		return;
	}
	if (bullet_index < 0 || bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_velocity.size() || bullet_index >= (int)all_cached_speed.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
		return;
	}
	const Vector2 to_node = target_position - all_cached_instance_origin[bullet_index];
	if (to_node.length_squared() < 0.000001) {
		UtilityFunctions::push_error("set_bullet_direction_towards_node2d: bullet is already at the target position, keeping the old direction.");
		return;
	}
	all_cached_direction[bullet_index] = to_node.normalized();
	all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * all_cached_speed[bullet_index] + inherited_velocity_offset;
}

void DirectionalBullets2D::all_bullets_set_direction_towards_node2d(const Node2D *target_node, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_direction_towards_node2d");

	if (target_node == nullptr) {
		UtilityFunctions::push_error("The target_node provided to all_bullets_set_direction_towards_node2d is null. Cannot set direction towards a null node.");
		return;
	}

	const Vector2 target_position = target_node->get_global_position();

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_direction_towards_position(i, target_position);
	}
}

void DirectionalBullets2D::set_bullet_texture_rotation_towards_position(int bullet_index, const Vector2 &target_position) {
	if (!validate_bullet_index(bullet_index, "set_bullet_texture_rotation_towards_position"))
		return;
	if (!target_position.is_finite()) {
		UtilityFunctions::push_error("set_bullet_texture_rotation_towards_position: target_position must be finite.");
		return;
	}
	if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
		return;
	}

	Transform2D &transf = all_cached_instance_transforms[bullet_index];

	Vector2 pos = all_cached_instance_origin[bullet_index];
	const Vector2 to_target = target_position - pos;
	if (to_target.length_squared() < 0.000001) {
		UtilityFunctions::push_error("set_bullet_texture_rotation_towards_position: bullet is already at the target position, keeping the old rotation.");
		return;
	}
	Vector2 dir = to_target.normalized();
	real_t angle = Math::atan2(dir.y, dir.x);

	// Compose with the volley-wide texture offset like the spawn path does
	// (generate_texture_transform adds cache_texture_rotation_radians):
	// without it the visual faces the target while the physics shape
	// (synced with -cache stripped) sits off by exactly the offset, and
	// adjust_direction_based_on_rotation re-derives a wrong direction.
	Vector2 scale = transf.get_scale();
	transf.set_rotation_and_scale(angle + cache_texture_rotation_radians, scale);
	transf.set_origin(pos);
	sync_shape_transform_from_instance(bullet_index, transf);

	// Only write the multimesh slot for ENABLED bullets: a disabled slot holds the
	// zero transform that hides it, and writing a real transform here would
	// resurrect the visual for a frame (or permanently on a paused factory).
	if (all_bullets_enabled_set.contains(bullet_index)) {
		multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(transf));
	}

	carry_attachment_with_transform(bullet_index, transf, Vector2(0, 0));

	update_bullet_previous_transform_for_interpolation(bullet_index);
}

void DirectionalBullets2D::all_bullets_set_texture_rotation_towards_position(const Vector2 &target_position, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_texture_rotation_towards_position");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_texture_rotation_towards_position(i, target_position);
	}
}

void DirectionalBullets2D::set_bullet_texture_rotation_towards_node2d(int bullet_index, const Node2D *target_node) {
	if (!validate_bullet_index(bullet_index, "set_bullet_texture_rotation_towards_node2d"))
		return;

	if (target_node == nullptr) {
		UtilityFunctions::push_error("The target_node provided to set_bullet_texture_rotation_towards_node2d is null. Cannot set texture rotation towards a null node.");
		return;
	}

	const Vector2 target_position = target_node->get_global_position();
	set_bullet_texture_rotation_towards_position(bullet_index, target_position);
}

void DirectionalBullets2D::all_bullets_set_texture_rotation_towards_node2d(const Node2D *target_node, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_texture_rotation_towards_node2d");

	if (target_node == nullptr) {
		UtilityFunctions::push_error("The target_node provided to all_bullets_set_texture_rotation_towards_node2d is null. Cannot set texture rotation towards a null node.");
		return;
	}

	const Vector2 target_position = target_node->get_global_position();

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_texture_rotation_towards_position(i, target_position);
	}
}

real_t DirectionalBullets2D::get_curves_elapsed_time() const {
	return curves_elapsed_time;
}
void DirectionalBullets2D::set_curves_elapsed_time(real_t new_time) {
	// NaN/Inf here would poison every curve sample (speed/rotation/direction) with no
	// recovery, so reject non-finite time like the other movement setters do.
	if (!Math::is_finite(new_time) || new_time < 0.0) {
		UtilityFunctions::push_error("set_curves_elapsed_time: new_time must be a finite value >= 0.");
		return;
	}
	curves_elapsed_time = new_time;
}

Ref<Curve2D> DirectionalBullets2D::get_bullet_movement_pattern_curve(int bullet_index) const {
	if (check_exists_bullet_movement_pattern_data(bullet_index)) {
		return find_bullet_movement_pattern_data(bullet_index).path_curve;
	}

	return nullptr;
}

void DirectionalBullets2D::set_bullet_movement_pattern_from_path(int bullet_index, Path2D *path_holding_pattern, bool face_movement_direction, bool repeat_pattern) {
	if (path_holding_pattern == nullptr) {
		remove_bullet_movement_pattern(bullet_index);
		return;
	}

	const Ref<Curve2D> &curve = path_holding_pattern->get_curve();

	set_bullet_movement_pattern_from_curve(bullet_index, curve, face_movement_direction, repeat_pattern);
}

void DirectionalBullets2D::all_bullets_set_movement_pattern_from_path(Path2D *path_holding_pattern, bool face_movement_direction, bool repeat_pattern, int start_index, int end_index_inclusive) {
	ensure_indexes_match_amount_bullets_range(start_index, end_index_inclusive, "all_bullets_set_movement_pattern_from_path");

	if (path_holding_pattern == nullptr) {
		all_bullets_remove_movement_pattern(start_index, end_index_inclusive);
		return;
	}

	const Ref<Curve2D> &curve = path_holding_pattern->get_curve();

	if (curve.is_null()) {
		all_bullets_remove_movement_pattern(start_index, end_index_inclusive);
		return;
	}

	for (int i = start_index; i <= end_index_inclusive; ++i) {
		set_bullet_movement_pattern_from_curve(i, curve, face_movement_direction, repeat_pattern);
	}
}

void DirectionalBullets2D::set_bullet_movement_pattern_from_curve(int bullet_index, const Ref<Curve2D> &curve_pattern, bool face_movement_direction, bool repeat_pattern) {
	if (!validate_bullet_index(bullet_index, "set_bullet_movement_pattern_from_curve")) {
		return;
	}
	if (curve_pattern.is_null()) {
		remove_bullet_movement_pattern(bullet_index);
		return;
	}
	all_movement_pattern_data[bullet_index] = BulletMovementPatternData2D{ curve_pattern, face_movement_direction, repeat_pattern };
}

void DirectionalBullets2D::all_bullets_set_movement_pattern_from_curve(const Ref<Curve2D> &curve_pattern, bool face_movement_direction, bool repeat_pattern, int start_index, int end_index_inclusive) {
	ensure_indexes_match_amount_bullets_range(start_index, end_index_inclusive, "all_bullets_set_movement_pattern_from_curve");

	if (curve_pattern.is_null()) {
		all_bullets_remove_movement_pattern(start_index, end_index_inclusive);
		return;
	}

	for (int i = start_index; i <= end_index_inclusive; ++i) {
		set_bullet_movement_pattern_from_curve(i, curve_pattern, face_movement_direction, repeat_pattern);
	}
}

void DirectionalBullets2D::remove_bullet_movement_pattern(int bullet_index) {
	if (check_exists_bullet_movement_pattern_data(bullet_index)) {
		all_movement_pattern_data[bullet_index] = BulletMovementPatternData2D();
	}
}

void DirectionalBullets2D::all_bullets_remove_movement_pattern(int start_index, int end_index_inclusive) {
	ensure_indexes_match_amount_bullets_range(start_index, end_index_inclusive, "all_bullets_remove_movement_pattern");

	for (int i = start_index; i <= end_index_inclusive; ++i) {
		remove_bullet_movement_pattern(i);
	}
}

int DirectionalBullets2D::get_collision_layer() const {
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("get_collision_layer: multimesh was never spawned through BulletFactory2D.");
		return 0;
	}
	return physics_server->area_get_collision_layer(area);
}

void DirectionalBullets2D::set_collision_layer(int new_collision_layer) {
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_collision_layer: multimesh was never spawned through BulletFactory2D.");
		return;
	}
	physics_server->area_set_collision_layer(area, new_collision_layer);
}

void DirectionalBullets2D::set_collision_layer_from_array(const TypedArray<int> &numbers) {
	int bitmask = DirectionalBulletsData2D::calculate_bitmask(numbers);
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_collision_layer_from_array: multimesh was never spawned through BulletFactory2D.");
		return;
	}
	physics_server->area_set_collision_layer(area, bitmask);
}

int DirectionalBullets2D::get_collision_mask() const {
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("get_collision_mask: multimesh was never spawned through BulletFactory2D.");
		return 0;
	}
	return physics_server->area_get_collision_mask(area);
}

void DirectionalBullets2D::set_collision_mask(int new_collision_mask) {
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_collision_mask: multimesh was never spawned through BulletFactory2D.");
		return;
	}
	physics_server->area_set_collision_mask(area, new_collision_mask);
}

void DirectionalBullets2D::set_collision_mask_from_array(const TypedArray<int> &numbers) {
	int bitmask = DirectionalBulletsData2D::calculate_bitmask(numbers);
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_collision_mask_from_array: multimesh was never spawned through BulletFactory2D.");
		return;
	}
	physics_server->area_set_collision_mask(area, bitmask);
}

bool DirectionalBullets2D::get_monitorable() const {
	return monitorable;
}

void DirectionalBullets2D::set_monitorable(bool value) {
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_monitorable: multimesh was never spawned through BulletFactory2D.");
		return;
	}
	monitorable = value;
	physics_server->area_set_monitorable(area, monitorable);
}

void DirectionalBullets2D::set_collision_shape_runtime(const Ref<Shape2D> &new_shape) {
	if (!physics_server || !area.is_valid()) {
		UtilityFunctions::push_error("set_collision_shape_runtime: physics not ready, cannot change shape at runtime.");
		return;
	}
	// Frees/recreates server RIDs and re-buckets the pool: unsafe while the
	// factory iterates bullet state or inside any physics frame (server flush
	// locks apply). Same contract as the factory structural methods.
	if (bullet_factory != nullptr && bullet_factory->is_structural_mutation_unsafe()) {
		UtilityFunctions::push_error("set_collision_shape_runtime cannot run while bullets are being processed or inside a physics frame (e.g. inside directional_area_entered/directional_body_entered handlers). Use call_deferred() to run this after the physics step.");
		return;
	}
	PhysicsServer2D::ShapeType old_effective = cached_effective_shape_type;
	const PoolKey old_key{ amount_bullets, old_effective };
	cache_collision_shape_typed(new_shape);
	// Typed cache already printed error once + fallback if needed.
	if (cached_effective_shape_type != old_effective) {
		// RID type mismatch: free the old shape and recreate the correct type
		// (a circle RID must never receive rectangle/capsule data).
		release_volley_shape();
		generate_physics_shapes_for_area(amount_bullets);
	}
	// Refresh data + transforms for all bullets so physics + debugger pick up new size immediately.
	// generate sets area transform + shape data from typed cache; then sync cached vectors (no second area_set) + interp cache to avoid lerp pop.
	if (area_shape_count != amount_bullets || !volley_shape.is_valid()) {
		UtilityFunctions::push_error("set_collision_shape_runtime: area shape count mismatch, cannot refresh.");
		return;
	}
	// Same-type resize: one data push for the whole volley.
	apply_volley_shape_data();
	for (int i = 0; i < amount_bullets; ++i) {
		// Push the new size/type data to the server shape. The returned transform
		// is intentionally discarded: the cached shape transform below is derived
		// through the sync helper so rotate_only_textures and the texture-rotation
		// strip stay consistent with the tick and teleport paths.
		(void)generate_collision_shape_transform_for_area(all_cached_instance_transforms[i], cache_collision_shape_offset, i);
		sync_shape_transform_from_instance(i, all_cached_instance_transforms[i]);
		// Fresh RIDs from a type change come enabled; restore per-bullet disabled state
		// so individually disabled bullets don't become collidable again.
		if (!all_bullets_enabled_set.contains(i)) {
			physics_server->area_set_shape_disabled(area, i, true);
		}
		update_bullet_previous_transform_for_interpolation(i);
	}
	if (amount_bullets > 0) {
		mark_shape_data_applied();
	}
	// Pooled instances live inside a bucket keyed by get_pool_key(). A runtime type change
	// while disabled would otherwise leave this instance in the stale bucket. Re-bucket it,
	// unless the user opted out of auto pooling (then it must never enter the pool).
	// An unpooled instance with pooling off keeps its new key cached but stays
	// out of every bucket on purpose: it is only reusable through a direct
	// enable_multimesh() (which reads the live key) or free_disabled_bullets().
	if (!is_active && is_multimesh_auto_pooling_enabled && bullets_pool != nullptr) {
		const PoolKey new_key = get_pool_key();
		if (!(new_key == old_key)) {
			bullets_pool->try_remove_instance(this, old_key);
			bullets_pool->push(this, new_key);
		}
	}
}

int DirectionalBullets2D::get_bullet_collision_count(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_collision_count")) {
		return 0;
	}
	if (bullet_index < 0 || bullet_index >= (int)bullets_current_collision_count.size()) {
		return 0;
	}
	return bullets_current_collision_count[bullet_index];
}

void DirectionalBullets2D::set_bullet_collision_count(int bullet_index, int value) {
	if (!validate_bullet_index(bullet_index, "set_bullet_collision_count")) {
		return;
	}
	if (bullet_index < 0 || bullet_index >= (int)bullets_current_collision_count.size()) {
		UtilityFunctions::push_error("set_bullet_collision_count: collision data not initialized for this multimesh.");
		return;
	}
	// Same clamp as enable_bullet()'s wake top-up: values at/above max leave
	// exactly one hit remaining (max - 1). Storing max itself would pin the
	// bullet at the kill threshold so the next handle_bullet_collision() hit
	// disables it immediately - inconsistent with a wake with the same amount.
	if (value < 0) {
		bullets_current_collision_count[bullet_index] = 0;
	} else if (bullet_max_collision_count > 0 && value >= bullet_max_collision_count) {
		bullets_current_collision_count[bullet_index] = bullet_max_collision_count - 1;
	} else {
		bullets_current_collision_count[bullet_index] = value;
	}
}


// Cold paths live here; per-tick hot paths stay inline in the header.

void DirectionalBullets2D::_do_deferred_bullet_disable_attachments(int expected_generation, const PackedInt64Array &requests) {
	if (expected_generation != multimesh_generation) {
		return;
	}
	const int64_t *req = requests.ptr();
	const int64_t n = requests.size();
	for (int64_t k = 0; k + 2 < n; k += 3) {
		const int bullet_index = (int)req[k];
		if (!slot_still_holds_attachment_id(bullet_index, (uint64_t)req[k + 1], (uint64_t)req[k + 2])) {
			continue;
		}
		bullet_disable_attachment(bullet_index);
		// bullet_disable_attachment runs user code (on_bullet_disable): a
		// handler may recycle this instance (pool pop -> new generation).
		if (expected_generation != multimesh_generation) {
			return;
		}
	}
}

void DirectionalBullets2D::_do_emit_life_time_over(int expected_generation, uint64_t emitter_instance_id, const StringName &signal_name, const TypedArray<int> &bullet_indexes) {
	if (expected_generation != multimesh_generation) {
		return;
	}
	if (bullet_indexes.is_empty()) {
		return;
	}
	Object *emitter = ObjectDB::get_instance(ObjectID(emitter_instance_id));
	if (emitter == nullptr) {
		return;
	}
	// Re-ownership guard: the volley may have been handed to a different
	// owner without a generation bump (adopt_live_volley re-stamps only).
	// Never deliver the old life's expiry to the new owner, and never
	// deliver a spawner life's expiry to the factory. A cleared tag
	// (owner == 0 after the expiry pooled the instance) still belongs to
	// the captured emitter, so only a *different live owner* drops here.
	if (owner_spawner_id != 0 && owner_spawner_id != emitter_instance_id) {
		return;
	}
	emitter->emit_signal(signal_name, this, bullet_indexes);
}

void DirectionalBullets2D::_do_emit_sprite_animation_finished(int expected_generation) {
	if (expected_generation != multimesh_generation) {
		return;
	}
	if (!anim_finished) {
		return;
	}
	emit_signal(CachedStringNames2D::get().sprite_animation_finished, this);
}


// Cold paths live here; per-tick hot paths stay inline in the header.

void DirectionalBullets2D::reduce_lifetime(double delta) {
		if (!Math::is_finite(delta) || delta < 0.0) {
			return;
		}
		curves_elapsed_time += delta;
		tick_volley_fade();

		// If the lifetime is infinite there is no lifetime timer
		if (is_life_time_infinite) {
			return;
		}

		// Life time timer logic
		current_life_time -= delta;

		// The bullets still have life time left, so don't do anything yet
		if (current_life_time > 0) {
			return;
		}

		std::vector<int> active_copy = all_bullets_enabled_set.get_active_indexes();
		if (bullet_factory != nullptr) {
			bullet_factory->stats_expired_bullets_total += (uint64_t)active_copy.size();
		}

		// If the life_time_over signal is not enabled, we can just disable all bullets right away and skip the additional logic
		if (!is_life_time_over_signal_enabled) {
			for (int i : active_copy) {
				if (!all_bullets_enabled_set.contains(i)) {
					continue;
				}
				// Lifetime expiry visuals fire here (not on collision kills):
				// capture the pose first, the disable below never moves it.
				Transform2D fx_expire_transf;
				bool fx_have_pose = i >= 0 && i < (int)all_cached_instance_transforms.size();
				if (fx_have_pose) {
					fx_expire_transf = all_cached_instance_transforms[i];
				}
				disable_bullet(i, true);
				if (fx_have_pose) {
					fx_fire_oneshot(EFFECT_ON_LIFETIME_OVER, i, fx_expire_transf);
				}
			}

			return;
		}

		// If the life_time_over signal is enabled - collect indexes, disable bullets immediately (consistent with collision path),
		// but keep attachment disable and signal deferred so handler can still access attachment.
		// Full-volley expiry additionally detaches the survivors from the disable
		// sweep: the last disable_bullet() funnels into disable_multimesh(),
		// whose sweep would otherwise pool every attachment BEFORE the deferred
		// signal fires (handler would see nullptr). Detaching first keeps the
		// slots alive for the signal; the deferred disables re-pool them after.
		TypedArray<int> bullet_indexes;

		// Hold the volley out of the pool (and keep its attachment slots)
		// until the deferred signal flushes: the last disable below funnels
		// into disable_multimesh(), which would otherwise pool the volley at
		// once (a same-frame spawn then pops it and the generation bump drops
		// the signal) and release every attachment before the handler runs.
		lifetime_flush_pending = true;

		// Snapshot the signal owner BEFORE the disable loop below: a full-volley
		// expiry funnels into disable_multimesh(), which clears owner_spawner_id
		// and pools the instance. Resolving after would schedule the factory's
		// life_time_over signal for a spawner-owned volley.
		Object *lifetime_emitter = resolve_signal_emitter();

		// Caches already hold global-space transforms (spawn data is global and all
		// movement/homing math is global), so no get_global_transform() compose is
		// needed anywhere transforms are read (handlers use get_bullet_global_transform()).
		for (int i : active_copy) {
			if (!all_bullets_enabled_set.contains(i)) {
				continue;
			}
			bullet_indexes.push_back(i);
			Transform2D fx_expire_transf;
			const bool fx_have_pose = i >= 0 && i < (int)all_cached_instance_transforms.size();
			if (fx_have_pose) {
				fx_expire_transf = all_cached_instance_transforms[i];
			}
			disable_bullet(i, false); // immediate shape disable, keep attachment for signal
			if (fx_have_pose) {
				fx_fire_oneshot(EFFECT_ON_LIFETIME_OVER, i, fx_expire_transf);
			}
		}
		// Never drained (a callback woke a bullet, or nothing was live): no
		// hold applies, the volley lives on normally.
		if (is_active) {
			lifetime_flush_pending = false;
		}

	if (bullet_indexes.size() > 0) {
		// Emit deferred so user code runs outside physics step. Guarded by
		// spawn generation (not just emitter validity): expiry queues the
		// emit, then a same-frame pool reuse hands this instance to a new
		// owner before the flush. The bare call_deferred("emit_signal")
		// would then deliver the OLD life's indexes to the NEW life.
		// Uses the pre-disable snapshot above, never a post-disable resolve.
		Object *emitter = lifetime_emitter;
		if (emitter != nullptr) {
			const uint64_t emitter_id = emitter->get_instance_id();
			if (emitter == bullet_factory) {
				const StringName &signal_name = CachedStringNames2D::get().directional_life_time_over;
				call_deferred(CachedStringNames2D::get().m_do_emit_life_time_over, multimesh_generation, emitter_id, signal_name, bullet_indexes);
			} else {
				call_deferred(CachedStringNames2D::get().m_do_emit_life_time_over, multimesh_generation, emitter_id, CachedStringNames2D::get().life_time_over, bullet_indexes);
			}
		}

		// Disable attachments after signal (deferred keeps order). The per-slot
		// assignment epoch travels with the request so a same-life ABA reuse
		// (pool returns the same node to the same slot) cannot match stale.
		// Only slots that actually hold an attachment are queued (one batched
		// call carrying ids + epochs, never pointers - see
		// _do_deferred_bullet_disable_attachments).
		PackedInt64Array attachment_requests;
		for (int i = 0; i < bullet_indexes.size(); ++i) {
			const int idx = bullet_indexes[i];
			BulletAttachment2D *queued_attachment = (idx >= 0 && idx < (int)attachments.size()) ? attachments[idx] : nullptr;
			if (queued_attachment == nullptr) {
				continue;
			}
			attachment_requests.push_back(idx);
			attachment_requests.push_back((int64_t)queued_attachment->get_instance_id());
			attachment_requests.push_back((int64_t)attachment_epoch_for(idx));
		}
		if (!attachment_requests.is_empty()) {
			call_deferred(CachedStringNames2D::get().m_do_deferred_bullet_disable_attachments, multimesh_generation, attachment_requests);
		}
	}
	// Queued last so it flushes after the signal and the slot releases.
	if (lifetime_flush_pending) {
		call_deferred(CachedStringNames2D::get().m_do_finish_lifetime_hold, multimesh_generation);
	}
	}

void DirectionalBullets2D::release_lifetime_hold_attachments() {
	lifetime_flush_pending = false;
	for (int i = 0; i < (int)attachments.size(); ++i) {
		if (attachments[i] != nullptr) {
			bullet_disable_attachment(i);
		}
	}
}

void DirectionalBullets2D::_do_finish_lifetime_hold(int expected_generation) {
	// A new life (wake/enable) already ended the hold and owns the volley.
	if (expected_generation != multimesh_generation || !lifetime_flush_pending) {
		return;
	}
	if (is_active || is_queued_for_deletion()) {
		lifetime_flush_pending = false;
		return;
	}
	// Same busy window as disable_multimesh(): the release below runs user
	// callbacks (on_bullet_disable), which must not reset/free the factory
	// under us.
	const bool saved_factory_busy = bullet_factory != nullptr ? bullet_factory->get_is_factory_busy() : false;
	if (bullet_factory != nullptr) {
		bullet_factory->_set_internal_operation_busy(true);
	}
	const uint64_t self_id = get_instance_id();
	release_lifetime_hold_attachments();
	if (bullet_factory != nullptr) {
		bullet_factory->_set_internal_operation_busy(saved_factory_busy);
	}
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return;
	}
	// The handler may have woken a bullet (new life) or opted out of pooling.
	if (is_active || active_bullets_counter > 0 || !is_multimesh_auto_pooling_enabled) {
		return;
	}
	// Slots are released: blank them like a normal disable would have.
	reset_attachment_state_for_reuse();
	if (bullets_pool != nullptr) {
		bullets_pool->push(this, get_pool_key());
	}
}

void DirectionalBullets2D::enable_bullet(int bullet_index, int collision_amount, bool should_enable_attachment) {
		// Wake semantics (contract, keep in sync with the doc XML):
		// "resume, not respawn" for ballistics (speed/direction/velocity/
		// position), appearance, custom data and per-bullet movement state -
		// there is no spawn data to reseed from. Two deliberate exceptions:
		// (1) an expiry-pooled wake tops up the whole volley's lifetime AND
		// rewinds the curve clock together (curves sample
		// curves_elapsed_time/max_life_time, so one without the other would
		// pin curves at their end sample); (2) per-bullet homing queues and
		// orbit state are NOT resumed - disable_bullet() clears them, so
		// re-push targets and re-enable orbit after the wake.
		// Cross-owner reuse must go through spawn_*()/enable_multimesh(),
		// which reseed everything from fresh data.
		if (!validate_bullet_index(bullet_index, "enable_bullet")) {
			return;
		}
		// Waking a volley that is queued for deletion would reactivate,
		// re-pool-unlink, and re-register an instance that dies at the end
		// of the frame. Refuse: queue_free() is terminal.
		if (is_queued_for_deletion()) {
			UtilityFunctions::push_error("enable_bullet: multimesh is queued for deletion.");
			return;
		}
		if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)bullets_current_collision_count.size()) {
			return;
		}
		if (multi.is_null() || !multi.is_valid()) {
			return;
		}
		if (physics_server == nullptr || !area.is_valid()) {
			return;
		}

		const bool curr_bullet_status = all_bullets_enabled_set.contains(bullet_index);

		// If the bullet is already enabled, just return
		if (curr_bullet_status) {
			return;
		}

		// A wake from inside the disable sweep (on_bullet_disable handler) would
		// resurrect the volley while _disable_multimesh_internal() is tearing it
		// down; the sweep aborts on wake now, but the factory also drops the
		// re-registration (reactivate fails under the busy flag), leaving a live
		// volley the factory never ticks. Reject here so the wake is explicit
		// (call_deferred) instead of silently frozen.
		if (bullet_factory != nullptr && bullet_factory->get_is_factory_busy()) {
			UtilityFunctions::push_error("enable_bullet: cannot wake a bullet while the factory is busy (e.g. inside an on_bullet_disable handler during the disable sweep). Use call_deferred to wake after the sweep.");
			return;
		}

		// The attachment callback below runs user code that may re-enter
		// enable_bullet()/disable_bullet() on this volley. Claim the slot (set
		// + counter) BEFORE it runs, and reject nested enable/disable calls
		// while the latch is held, so the counter can never drift vs the
		// sparse set (activate_data dedups, the counter does not).
		if (_bullet_enable_depth > 0) {
			UtilityFunctions::push_error("enable_bullet: re-entrant call from inside on_bullet_enable is not allowed. Use call_deferred to change bullet state from that callback.");
			return;
		}
		ReentrancyGuard bullet_enable_guard(_bullet_enable_depth);

		// Full-volley wake coming: the generations must bump BEFORE the
		// attachment callback below (user on_bullet_enable code) runs, so work
		// it queues is stamped with the new life instead of being invalidated
		// right after. Deferred work from the dead life is correctly dropped.
		// Timers are dropped outright here (not just bumped): disable only
		// detaches on the full-kill path, so a disable→attach→wake sequence
		// would otherwise hand the new life timers armed while pooled.
		const bool will_reactivate_volley = !is_active;
		// Waking a held expiry before its flush: the dying life's queued
		// releases are about to be invalidated by the generation bump, so
		// release the held slots now (the new life starts blank).
		if (will_reactivate_volley && lifetime_flush_pending) {
			release_lifetime_hold_attachments();
			reset_attachment_state_for_reuse();
		}
		if (will_reactivate_volley) {
			++multimesh_generation;
			++multimesh_timers_generation;
			multimesh_custom_timers.clear();
		}

		all_bullets_enabled_set.activate_data(bullet_index);
		++active_bullets_counter;
		if (active_bullets_counter > amount_bullets) {
			active_bullets_counter = amount_bullets;
		}

		// A wake is a new life for this slot: stale queued hits from before
		// the disable must not fire now (same-overlap double count).
		bump_collision_epoch_for_bullet(bullet_index);

		multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(all_cached_instance_transforms[bullet_index])); // Start rendering the instance
		write_trail_instances(bullet_index); // Wake resumes the trail with the bullet

		physics_server->area_set_shape_disabled(area, bullet_index, false);

		// Woken bullets must not lerp from a stale pre-disable position.
		// Every other teleport-sentenced path syncs prev==curr; do the same.
		update_bullet_previous_transform_for_interpolation(bullet_index);

		auto &current_bullet_collision_amount = bullets_current_collision_count[bullet_index];

		// collision_amount is how many hits the bullet has already taken: 0 means fresh
		// (full hits remaining). Clamp into range so re-enabling can't grant extra hits
		// or kill the bullet one hit early: values at/above max leave exactly one
		// hit remaining (max - 1), same as set_bullet_collision_count().
		if (collision_amount < 0) {
			current_bullet_collision_amount = 0;
		} else if (bullet_max_collision_count > 0 && collision_amount >= bullet_max_collision_count) {
			current_bullet_collision_amount = bullet_max_collision_count - 1;
		} else {
			current_bullet_collision_amount = collision_amount;
		}

		if (should_enable_attachment) {
			bullet_enable_attachment(bullet_index);
		}

		if (!is_active) {
			// Waking a fully pooled multimesh outside the factory pop path: drop it from
			// the pool first, otherwise the next pop() would hand out this live instance
			// to a second owner while the first still drives it. The factory also resumes
			// processing it so woken bullets actually move.
			// Generations were already bumped above (before user callbacks), so
			// deferred work from the dead life is gone and anything queued from
			// here on belongs to this new life.
			// Only a foreign life (actually pooled) carries the previous
			// Only a foreign life (actually pooled) carries the previous
			// owner's volley-wide signal connections. A same-owner revive of
			// a drained-but-unpooled volley must keep them: disconnecting here
			// would silence the whole volley because of one bullet's wake.
			// try_remove_instance returns false when pooling is off or the
			// instance was never pooled - both mean same-owner revive.
			const bool was_pooled = (bullets_pool != nullptr) && bullets_pool->try_remove_instance(this, get_pool_key());
			if (bullet_factory != nullptr) {
				bullet_factory->reactivate_multimesh_instance(*this);
			}
		// A pooled instance carries the previous owner's signal connections; they
		// must not fire for this wake (same cleanup the pool-pop enable does).
		// Scoped to foreign lives only (see was_pooled above): same-owner
		// revives skip the disconnects so sibling notifications survive.
		if (was_pooled) {
			disconnect_sprite_animation_connections();
			// Same for the previous owner's homing forward: without this, the old
			// spawner would keep retargeting a volley someone else woke manually.
			for (const Dictionary &connection : get_signal_connection_list("bullet_homing_target_reached")) {
				const Callable callable = connection["callable"];
				disconnect("bullet_homing_target_reached", callable);
			}
			// Fail-safe neutral subset: the queue_free-vs-pool decision and the
			// rotation drive must not follow a dead owner into the new life.
			// Pooling flags reset to default (a foreign wake must never inherit
			// "don't pool" and strand itself, nor "pool" against the new owner's
			// wishes — set them explicitly after the wake if needed). Rotation
			// speeds are cleared (stale spin would steer the new life with no
			// data behind it). Ballistics, appearance, custom data, collision
			// counts/max, lifetime and shape state resume by design (warned
			// below): reseed via spawn_*/enable_multimesh for a clean slate.
			reset_pooling_flags_to_default();
			set_rotation_data(TypedArray<BulletRotationData2D>(), rotate_only_textures);
		}
		// Ownership restarts from scratch: whoever woke this re-stamps if it
		// is a spawner (see BulletSpawner2D::adopt_live_volley). Keeping the
		// old id would let a foreign spawner steer manual wakes.
		owner_spawner_id = 0;
		// A foreign pooled wake is a new owner with partially stale state:
		// ballistics, appearance, custom data, collision counts/max, lifetime
		// and shape state still hold the previous owner's values (only the
		// woken slot's collision count was reseeded above). Pooling flags,
		// rotation, signals and ownership were neutralized above. Same-owner
		// revives resume everything by design; foreign wakes must reseed
		// through spawn_*/enable_multimesh or adopt_live_volley + manual
		// re-push, so warn once per wake instead of driving silently stale.
		if (was_pooled) {
			UtilityFunctions::push_warning("enable_bullet: woke a pooled volley from the pool outside spawn_*/enable_multimesh. Ballistics, appearance, custom data, collision counts, lifetime and shape state still hold the previous owner's values: reseed them (or adopt_live_volley + re-push homing/orbit) before relying on this volley.");
		}
			// An expiry-pooled wake would otherwise die again on the next tick with an
			// exhausted timer. Only top it up when expired; manual-disable wakes keep
			// their remaining lifetime untouched. Both clocks restart together:
			// curves sample curves_elapsed_time/max_life_time, so topping up one
			// without the other would pin curves at their end sample (1.0).
			if (!is_life_time_infinite && current_life_time <= 0.0) {
				current_life_time = max_life_time;
				curves_elapsed_time = 0.0;
			}
			is_active = true;
			set_visible(true);
			// Keep the interpolator consistent for the whole volley: disabled
			// bullets are not rendered, but their prev cache is stale. Sync all
			// so a later enable_bullet() never lerps from a pre-disable pose.
			// (The woken bullet itself was already synced above.)
			update_all_previous_transforms_for_interpolation();
		}
	}

void DirectionalBullets2D::disable_bullet(int bullet_index, bool should_disable_attachment) {
		if (!validate_bullet_index(bullet_index, "disable_bullet")) {
			return;
		}
		// disable_bullet() is a teardown-adjacent path (debug helpers call it on
		// unspawned instances; PREDELETE frees the area). enable_bullet() guards
		// these; mirror the guards so a dead multimesh can't null-deref below.
		if (multi.is_null() || !multi.is_valid()) {
			return;
		}
		if (physics_server == nullptr || !area.is_valid()) {
			return;
		}

		// Same re-entrancy latch as enable_bullet(): the attachment callback
		// below runs user code that may call enable_bullet()/disable_bullet().
		// A nested enable-then-disable pair inside one outer disable would
		// otherwise decrement the counter twice for one claimed slot, and a
		// nested disable inside the sweep below would pool the instance twice.
		if (_bullet_enable_depth > 0) {
			UtilityFunctions::push_error("disable_bullet: re-entrant call from inside on_bullet_enable/on_bullet_disable is not allowed. Use call_deferred to change bullet state from that callback.");
			return;
		}
		ReentrancyGuard bullet_disable_guard(_bullet_enable_depth);

		const bool curr_bullet_status = all_bullets_enabled_set.contains(bullet_index);

		// If the bullet is already disabled, just return
		if (!curr_bullet_status) {
			return;
		}

		all_bullets_enabled_set.disable_data(bullet_index);

		--active_bullets_counter;
		if (active_bullets_counter < 0) {
			active_bullets_counter = 0;
		}

		// Stale queued hits for this slot must not fire after a re-enable:
		// bump the drain epoch so records queued before this disable
		// mismatch at emit time.
		bump_collision_epoch_for_bullet(bullet_index);

		// Drop per-bullet homing/orbit state now: the tick
		// only trims active bullets, so without this a disabled bullet's invalid
		// targets leak counters until the whole multimesh dies. Pattern and curve
		// state is deliberately kept: a wake resumes the bullet's own movement
		// (documented wake contract), while homing queues and orbit locks are
		// re-pushed/re-armed after the wake.
		on_bullet_disabled(bullet_index);

		multi->set_instance_transform_2d(bullet_index, zero_transform); // Stops rendering the instance
		hide_trail_instances(bullet_index); // Trail dies with its bullet, same frame

		physics_server->area_set_shape_disabled(area, bullet_index, true);

		if (should_disable_attachment) {
			bullet_disable_attachment(bullet_index);
		}

		if (active_bullets_counter <= 0) {
			disable_multimesh();
		}
	}

bool DirectionalBullets2D::clear_bullet(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "clear_bullet")) {
			return false;
		}
		// Already-dead slots stay silent: without this a double clear would
		// fire a second visual for a bullet that is already gone.
		if (!all_bullets_enabled_set.contains(bullet_index)) {
			return false;
		}
		// Pose captured before the disable below (disable never moves the
		// bullet, but the last-bullet disable funnels into disable_multimesh
		// which pools the instance; the DESTROY path fires the same way).
		Transform2D fx_clear_transf;
		const bool fx_have_pose = bullet_index >= 0 && bullet_index < (int)all_cached_instance_transforms.size();
		if (fx_have_pose) {
			fx_clear_transf = all_cached_instance_transforms[bullet_index];
		}
		disable_bullet(bullet_index, true);
		if (fx_have_pose) {
			fx_fire_oneshot(EFFECT_ON_CLEAR, bullet_index, fx_clear_transf);
		}
		return true;
	}

int DirectionalBullets2D::clear_all_bullets() {
		// Snapshot first: each clear mutates the live set below.
		std::vector<int> live = all_bullets_enabled_set.get_active_indexes();
		int cleared = 0;
		for (int i : live) {
			if (clear_bullet(i)) {
				++cleared;
			}
		}
		return cleared;
	}

void DirectionalBullets2D::handle_bullet_collision(CollisionType collision_type, int bullet_index, int64_t entered_instance_id, uint64_t queued_bullet_epoch, Vector2 queued_target_velocity, bool queued_velocity_valid, Vector2 queued_target_position, bool queue_position_valid) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return;
		}
		if (bullet_index >= (int)bullets_current_collision_count.size() || bullet_index >= (int)attachments.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
			return;
		}
		// Stale record check: a handler earlier in this same drain may have
		// disabled then re-enabled this slot (epoch bump on both). The queued
		// stamp no longer matches, so this record describes a dead overlap,
		// not a new hit - skip it instead of double-counting.
		if (queued_bullet_epoch != collision_epoch_for_bullet(bullet_index)) {
			return;
		}
		const bool curr_bullet_status = all_bullets_enabled_set.contains(bullet_index);

		// If the bullet is already disabled, just return
		if (!curr_bullet_status) {
			return;
		}
		if (bullet_factory != nullptr) {
			++bullet_factory->stats_collision_records_total;
		}

		// Bounce precedence: a bounce-eligible hit ricochets here and never
		// reaches the counter below (unless the volley asked to consume the
		// hit too, decision 2). The hook owns its signals + self-liveness.
		const int bounce_decision = try_handle_bounce(collision_type, bullet_index, entered_instance_id, queued_target_velocity, queued_velocity_valid, queued_target_position, queue_position_valid);
		if (bounce_decision == 1) {
			return;
		}

		int &current_bullet_collision_amount = bullets_current_collision_count[bullet_index];

		// Always keep track of how many collisions this bullet had (yes even if the user set bullet_max_collision_count to 0, I just want consistent behavior)
		++current_bullet_collision_amount;

		const bool bullet_reached_max_collisions = bullet_max_collision_count > 0 && current_bullet_collision_amount >= bullet_max_collision_count;

		// Effect pose captured before any disable below (disable never moves
		// the bullet, but the handler at the signal below may).
		const Transform2D fx_hit_transf = all_cached_instance_transforms[bullet_index];

		// Snapshot the signal owner BEFORE any disable below: the killing blow
		// funnels into disable_multimesh(), which clears owner_spawner_id and
		// pools the instance. Resolving after would route a spawner volley's
		// hit to the factory (or drop it) instead of the owning spawner.
		Object *emitter = resolve_signal_emitter();

		// Only disable the bullet if the max collision count is greater than 0, otherwise the bullet should never be disabled due to collisions
		// Killing blow: guard the attachment slot across the disable below.
		// disable_bullet() on the last live bullet funnels into
		// disable_multimesh(), whose sweep would pool every attachment BEFORE
		// the collision signal fires (handler would see nullptr). The lifetime
		// path reclaims attachments for exactly this reason; do the same here.
		// Note the guard covers the disable (where the sweep runs), not the
		// signal emit that follows it.
		if (bullet_reached_max_collisions) {
			// Save/restore, not plain set/clear: disable_bullet runs user code
			// (on_bullet_disabled), which could re-enter this handler for a
			// different slot. The inner frame would clear the outer guard and
			// leave the outer sweep unprotected.
			const int saved_protected_slot = signal_protected_attachment_slot;
			if (bullet_index >= 0 && bullet_index < (int)attachments.size() && attachments[bullet_index] != nullptr) {
				signal_protected_attachment_slot = bullet_index;
			}
			disable_bullet(bullet_index, false); // Don't disable the attachment yet, first emit the signal for collision so user has access to the attachment and CAN detach it himself inside GDScript
			signal_protected_attachment_slot = saved_protected_slot;
			// Destroy explosion only (never the hit spark too): the killing
			// blow gets one visual. Lifetime/manual disables never reach
			// here, so timeouts don't detonate.
			fx_fire_oneshot(EFFECT_ON_DESTROY, bullet_index, fx_hit_transf);
		} else {
			// Hit sparks for counted hits (free-bounce records never arrive:
			// the bounce branch above returns before the counter; consumed
			// bounces already fired the bounce spark, so they stay silent
			// here instead of doubling the visual).
			if (bounce_decision != 2) {
				fx_fire_oneshot(EFFECT_ON_HIT, bullet_index, fx_hit_transf);
			}
		}

		// Capture the slot AND its assignment epoch before the signal: the
		// handler runs user code that may detach, replace, or - through a
		// re-entrant spawn that pops this instance from the pool - hand the
		// whole multimesh to a new owner. The post-signal disable below must
		// only fire when the slot still holds the SAME assignment.
		BulletAttachment2D *attachment_at_signal_time = (bullet_index >= 0 && bullet_index < (int)attachments.size()) ? attachments[bullet_index] : nullptr;
		const uint64_t attachment_epoch_at_signal_time = attachment_epoch_for(bullet_index);
		// The id is captured NOW: the handler below may free() the attachment,
		// after which the pointer must never be dereferenced again.
		const uint64_t attachment_id_at_signal_time = attachment_at_signal_time != nullptr ? attachment_at_signal_time->get_instance_id() : 0;

		Object *hit_target = ObjectDB::get_instance(entered_instance_id);

		// Typed per-kind signals emit synchronously (Godot-style): the instance
		// is alive and the slot state valid by construction here, so handlers
		// run with live data, need no casts, and need no call_deferred for
		// game logic. HANDLER CONTRACT: to destroy this volley from inside
		// the handler use queue_free() (or call_deferred factory free/reset
		// calls) — never immediate Object.free()/memdelete. The post-emit
		// code below touches this instance, so an immediately-freed volley
		// would be use-after-free. Slim payload - custom data and transforms
		// are one
		// instance call away (bullet_get_custom_data(),
		// get_bullet_global_transform()).
		// Possessed by the tagged spawner when there is one, else the factory.
		// A null emitter (teardown, spawner gone, or both gone) only skips the
		// notification - cleanup below still runs.
		// Self-liveness token, captured before user code runs: a handler that
		// immediately frees this volley (against the contract below) leaves
		// every member access after the emit as use-after-free - including the
		// is_queued_for_deletion() check itself. ObjectDB validates the id
		// without touching the object, and comparing the result against this
		// performs no dereference, so a freed volley bails safely instead of
		// crashing (misuse is still prohibited: state after the emit is lost).
		const uint64_t self_id = get_instance_id();
		if (emitter != nullptr) {
			if (emitter == bullet_factory) {
				if (collision_type == CollisionType::AREA) {
					emitter->emit_signal(CachedStringNames2D::get().directional_area_entered, hit_target, this, bullet_index);
				} else if (collision_type == CollisionType::BODY) {
					emitter->emit_signal(CachedStringNames2D::get().directional_body_entered, hit_target, this, bullet_index);
				}
			} else {
				if (collision_type == CollisionType::AREA) {
					emitter->emit_signal(CachedStringNames2D::get().area_entered, hit_target, this, bullet_index);
				} else if (collision_type == CollisionType::BODY) {
					emitter->emit_signal(CachedStringNames2D::get().body_entered, hit_target, this, bullet_index);
				}
			}
		}

		// Disable the bullet attachment if the bullet reached its max collision count and the attachment is still enabled
		if (bullet_reached_max_collisions) {
			// The signal above runs user code that may have detached this slot
			// already (bullet_set_attachment_to_null / bullet_free_attachment /
			// bullet_set_attachment), or re-assigned it. Only disable the slot if
			// it still holds what we captured - anything else belongs to whoever
			// changed it (possibly a new pool owner), and disable_multimesh()'s
			// sweep catches any survivor that would otherwise leak.
			// The handler may also have freed THIS multimesh (queue_free during
			// the sync emit): attachments[]/bullets_current_collision_count[] are
			// member vectors, so bail before touching them.
			// The handler may also have freed the captured attachment itself:
			// compare by instance id (never a raw dangling pointer), and only
			// after confirming this multimesh is still alive.
			// Liveness FIRST (see self_id token above): no member touch - not
			// even is_queued_for_deletion() - when the handler freed us.
		if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
			return;
		}
		if (is_queued_for_deletion()) {
			return;
		}
		if (slot_still_holds_attachment(bullet_index, attachment_at_signal_time, attachment_id_at_signal_time, attachment_epoch_at_signal_time)) {
			bullet_disable_attachment(bullet_index);
		}
		}
	}
} //namespace BlastBullets2D
