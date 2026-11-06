// Sprite effect layers (EffectLayerData2D): trail shards that follow bullets
// and one-shot effects fired on spawn/hit/destroy/bounce/clear.

#include "bullet_volley/bullet_volley2d_internal.hpp"
#include "core/cosmetic_rng2d.hpp"

using namespace godot;

namespace BlastBullets2D {

BulletVolley2D::FXLayerSnapshot BulletVolley2D::fx_snapshot_layer(const Ref<EffectLayerData2D> &layer) {
	FXLayerSnapshot snap;
	if (layer.is_null() || !layer->enabled) {
		return snap;
	}
	snap.present = true;
	snap.bake_version = layer->get_bake_version();
	snap.trigger = layer->trigger;
	snap.material_id = layer->material.is_valid() ? (uint64_t)layer->material->get_instance_id() : 0;
	snap.self_modulate = layer->self_modulate;
	snap.z_index = layer->z_index;
	snap.z_as_relative = layer->z_as_relative;
	snap.visibility_layer = layer->visibility_layer;
	snap.light_mask = layer->light_mask;
	snap.max_instances = layer->max_instances;
	return snap;
}

//// SPRITE EFFECT (FX LAYER) TRAILS ////

// Bakes one trail layer's frames and builds one shard node per frame
// (children of the volley: pooling hides them, freeing is automatic, and
// relative z tracks the volley). Shard textures never change, so per-tick
// work is a single instance write into the current frame's shard.
void BulletVolley2D::fx_rebuild_trail_layers(const TypedArray<EffectLayerData2D> &layers) {
	// Dropping the previous trail bakes drops their layer Refs: the last one
	// destroys a script layer whose NOTIFICATION_PREDELETE is user code that
	// may free this volley (proven under ASan). Nothing below may read `this`
	// while the check fails.
	const uint64_t self_id = get_instance_id();
	fx_clear_trail_layers();
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return;
	}
	if (amount_bullets <= 0) {
		return;
	}
	for (int li = 0; li < layers.size(); ++li) {
		Ref<EffectLayerData2D> layer = layers[li];
		if (layer.is_null() || !layer->enabled) {
			continue;
		}
		if (layer->trigger != EFFECT_TRAIL_FOLLOW) {
			continue;
		}
		FXTrailBake bake;
		if (!layer->bake_effect_animation(layer->animation, frame_cache(), bake.anim)) {
			continue;
		}
		bake.layer = layer;
		bake.layer_index = li;
		const uint64_t entry_epoch = fx_trail_epoch;
		for (size_t f = 0; f < bake.anim.frames.size(); ++f) {
			const Ref<Texture2D> frame = bake.anim.frames[f];
			MultiMeshInstance2D *shard = nullptr;
			// add_child runs node_added handlers (user code): guard the free of
			// this volley and re-check after it.
			guarded_user_call2d(this, emit_guard, [&]() {
				shard = BulletFactory2D::fx_create_shard(this, frame, BulletFactory2D::fx_quad_size_for_texture(frame), layer->material, layer->self_modulate, layer->z_index, layer->z_as_relative, layer->visibility_layer, layer->light_mask, amount_bullets, true);
			});
			if (fx_trail_epoch != entry_epoch || !survived_user_code(self_id) || is_queued_for_deletion()) {
				// A handler replaced the layers or asked to free the volley (the
				// guard refused it): drop the shards this loop made, keep whatever
				// the nested call left in the member list.
				for (auto &created : bake.shards) {
					if (MultiMeshInstance2D *node = created.get()) {
						node->queue_free();
					}
				}
				if (shard != nullptr) {
					shard->queue_free();
				}
				return;
			}
			bake.shards.push_back(shard);
			bake.shard_multimeshes.push_back(shard != nullptr ? shard->get_multimesh() : Ref<MultiMesh>());
		}
		bake.shard_visible.assign(bake.shards.size(), 0);
		bake.shard_refcount.assign(bake.shards.size(), 0);
		bake.phase.assign(amount_bullets, 0.0);
		if (layer->random_start_frame && bake.anim.total > 0.0) {
			for (int i = 0; i < amount_bullets; ++i) {
				bake.phase[i] = (double)CosmeticRng2D::randf_range(0.0f, (float)bake.anim.total);
			}
		}
		bake.bullet_on.assign(amount_bullets, 1);
		bake.bullet_shard.assign(amount_bullets, -1);
		bake.bullet_trail_transf.assign(amount_bullets, Transform2D());
		bake.bullet_trail_tint.assign(amount_bullets, Color(1, 1, 1, 1));
		fx_trail_bakes.push_back(bake);
	}
}

void BulletVolley2D::fx_clear_trail_layers() {
	// Every rebuild invalidates loops that hold a bake element across user
	// code (nested set_effect_layers / play_effect_animation).
	++fx_trail_epoch;
	// The bakes are swapped into a local first: dropping their layer Refs is
	// the destruction of a script layer, whose NOTIFICATION_PREDELETE is user
	// code that may free this volley (proven under ASan). Destroying them in
	// place would keep mutating the member vector (storage inside the volley)
	// after that free. The local dies here, after every read of `this`, and
	// the caller re-checks before touching the volley again.
	std::vector<FXTrailBake> dying;
	dying.swap(fx_trail_bakes);
	for (size_t b = 0; b < dying.size(); ++b) {
		for (size_t s = 0; s < dying[b].shards.size(); ++s) {
			MultiMeshInstance2D *shard = dying[b].shards[s].get();
			if (shard == nullptr) {
				continue; // never made, or freed by a script
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
}

// Full reseed from a layer list: retains it for trigger routing, rebuilds
// trail shards, and re-registers factory one-shot bakes (erasing the
// previous life's). fire_spawn flashes ON_SPAWN layers at every bullet.
bool BulletVolley2D::fx_layers_match_seeded(const TypedArray<EffectLayerData2D> &layers) const {
	if (layers.size() != fx_data_layers.size() || (int)fx_seeded_snapshots.size() != layers.size()) {
		return false;
	}
	for (int li = 0; li < layers.size(); ++li) {
		Ref<EffectLayerData2D> incoming = layers[li];
		Ref<EffectLayerData2D> seeded = fx_data_layers[li];
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
		for (const OwnedNode2D<MultiMeshInstance2D> &handle : bake.shards) {
			const MultiMeshInstance2D *shard = handle.get();
			if (shard == nullptr || shard->is_queued_for_deletion()) {
				return false; // freed by a script: rebuild
			}
		}
		if ((int)bake.bullet_shard.size() != amount_bullets) {
			return false;
		}
	}
	// The factory's one-shot bakes too: shards a script freed (or the whole
	// effects container) are rebuilt, never fired into unseen.
	if (fx_oneshot_trigger_mask != 0 && bullet_factory != nullptr && !bullet_factory->fx_volley_bakes_intact(get_instance_id())) {
		return false;
	}
	return true;
}

void BulletVolley2D::fx_soft_reset_trail_layers() {
	// set_visible(false) runs a shard's visibility_changed handlers (user
	// code): a nested set_effect_layers/play_effect_animation swaps
	// fx_trail_bakes (fx_clear_trail_layers), so the loop below must abandon
	// its bake on a moved epoch or a freed/queued volley, never write into
	// the destroyed vector (ASan: heap-use-after-free).
	const uint64_t entry_epoch = fx_trail_epoch;
	const uint64_t self_id = get_instance_id();
	for (FXTrailBake &bake : fx_trail_bakes) {
		for (int i = 0; i < (int)bake.bullet_shard.size(); ++i) {
			const int shard_index = bake.bullet_shard[i];
			if (shard_index >= 0 && shard_index < (int)bake.shards.size() && bake.shards[shard_index].ptr != nullptr) {
				bake.shard_multimesh(shard_index)->set_instance_transform_2d(i, zero_transform);
			}
		}
		for (size_t s = 0; s < bake.shards.size(); ++s) {
			MultiMeshInstance2D *node = bake.shards[s].get();
			if (node != nullptr && s < bake.shard_visible.size() && bake.shard_visible[s]) {
				guarded_user_call2d(this, emit_guard, [&]() { node->set_visible(false); });
				if (fx_trail_epoch != entry_epoch || !survived_user_code(self_id) || is_queued_for_deletion()) {
					return;
				}
			}
		}
		bake.shard_visible.assign(bake.shards.size(), 0);
		bake.shard_refcount.assign(bake.shards.size(), 0);
		bake.bullet_shard.assign(amount_bullets, -1);
		bake.bullet_on.assign(amount_bullets, 1);
		bake.bullet_trail_transf.assign(amount_bullets, Transform2D());
		bake.bullet_trail_tint.assign(amount_bullets, Color(1, 1, 1, 1));
		bake.phase.assign(amount_bullets, 0.0);
		if (bake.layer->random_start_frame && bake.anim.total > 0.0) {
			for (int i = 0; i < amount_bullets; ++i) {
				bake.phase[i] = (double)CosmeticRng2D::randf_range(0.0f, (float)bake.anim.total);
			}
		}
	}
}

void BulletVolley2D::fx_reseed_from_data(const TypedArray<EffectLayerData2D> &layers, bool fire_spawn) {
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
	// Replacing the layer list drops the old Refs: the last one destroys a
	// script layer whose NOTIFICATION_PREDELETE is user code that may free
	// this volley (proven under ASan). godot-cpp's Array assignment destroys
	// the old value BEFORE it constructs the new one, so the old array is held
	// in a local across the store (the store itself then runs no user code);
	// the local's own drop does, and nothing below may read `this` while the
	// check fails.
	const uint64_t self_id = get_instance_id();
	{
		const TypedArray<EffectLayerData2D> previous_layers = fx_data_layers;
		fx_data_layers = layers;
	}
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return;
	}
	// One-shot trigger mask: only layers registered as factory bakes below
	// (enabled, non-trail) can ever fire, so a trigger missing here makes
	// fx_fire_oneshot a single bit test instead of a per-layer Variant
	// unbox + Ref copy on every bullet disable / hit / spawn.
	fx_oneshot_trigger_mask = 0;
	for (int li = 0; li < layers.size(); ++li) {
		Ref<EffectLayerData2D> layer = layers[li];
		if (layer.is_valid() && layer->enabled && layer->trigger != EFFECT_TRAIL_FOLLOW && layer->trigger >= 0 && layer->trigger < 32) {
			fx_oneshot_trigger_mask |= (1u << layer->trigger);
		}
	}
	fx_seeded_snapshots.resize(layers.size());
	for (int li = 0; li < layers.size(); ++li) {
		fx_seeded_snapshots[li] = fx_snapshot_layer(layers[li]);
	}
	fx_rebuild_trail_layers(layers);
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return; // the trail rebuild dropped a script layer's last Ref
	}
	if (bullet_factory != nullptr && factory_alive()) {
		bullet_factory->fx_unregister_volley(get_instance_id());
		if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
			return; // the factory's old bake Refs dropped a script layer's last Ref
		}
		// Registering a bake builds its shard nodes: every add_child runs
		// SceneTree.node_added handlers (user code), and one may free this
		// volley. Under the volley's emit guard that free is refused and
		// finished with queue_free(), so this loop never resumes on a freed
		// volley and the bakes it registers die with it in PREDELETE.
		guarded_user_call2d(this, emit_guard, [&]() {
			for (int li = 0; li < layers.size(); ++li) {
				Ref<EffectLayerData2D> layer = layers[li];
				if (layer.is_null() || !layer->enabled) {
					continue;
				}
				if (layer->trigger == EFFECT_TRAIL_FOLLOW) {
					continue;
				}
				bullet_factory->fx_register_volley_bake(get_instance_id(), li, layer, amount_bullets);
			}
		});
	}
	if (fire_spawn) {
		fx_fire_spawn_layers();
	}
}

void BulletVolley2D::fx_fire_spawn_layers() {
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

void BulletVolley2D::fx_fire_oneshot(int trigger, int bullet_index, const Transform2D &at) {
	if (trigger < 0 || trigger >= 32 || !(fx_oneshot_trigger_mask & (1u << trigger))) {
		return;
	}
	if (bullet_factory == nullptr || !factory_alive() || bullet_index < 0 || bullet_index >= amount_bullets) {
		return;
	}
	if (!at.get_origin().is_finite() || !Math::is_finite(at.get_rotation())) {
		return;
	}
	bool fired = false;
	for (int li = 0; li < fx_data_layers.size(); ++li) {
		Ref<EffectLayerData2D> layer = fx_data_layers[li];
		if (layer.is_null() || !layer->enabled) {
			continue;
		}
		if (layer->trigger != trigger) {
			continue;
		}
		const uint64_t self_id = get_instance_id();
		bullet_factory->fx_fire(get_instance_id(), li, at);
		fired = true;
		if (!survived_user_code(self_id)) {
			return; // fx_fire adds shard nodes (user code): the factory or volley may be gone
		}
	}
	if (fired && factory_alive() && bullet_factory->debug_effect_log_enabled) {
		bullet_factory->debug_log_effect(trigger, get_instance_id(), get_life_id(), bullet_index, at.get_origin());
	}
}

bool BulletVolley2D::has_trail_effects() const {
	return !fx_trail_bakes.empty();
}

bool BulletVolley2D::fx_has_trail_layer(int layer_index) const {
	for (size_t b = 0; b < fx_trail_bakes.size(); ++b) {
		if (fx_trail_bakes[b].layer_index == layer_index) {
			return true;
		}
	}
	return false;
}

void BulletVolley2D::bullet_set_trail_enabled(int layer_index, int bullet_index, bool trail_on) {
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

void BulletVolley2D::all_bullets_set_trail_enabled(int layer_index, bool trail_on, int bullet_index_start, int bullet_index_end_inclusive) {
	if (!ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_trail_enabled")) {
		return;
	}
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
bool BulletVolley2D::play_effect_animation(int layer_index, const StringName &animation) {
	const uint64_t self_id = get_instance_id();
	for (size_t b = 0; b < fx_trail_bakes.size(); ++b) {
		if (fx_trail_bakes[b].layer_index != layer_index || fx_trail_bakes[b].layer.is_null()) {
			continue;
		}
		SpriteAnim2D baked;
		if (!fx_trail_bakes[b].layer->bake_effect_animation(animation, frame_cache(), baked)) {
			return false;
		}
		// This rebuild invalidates every outer loop over the bakes.
		++fx_trail_epoch;
		const uint64_t epoch = fx_trail_epoch;
		{
			// Old shards out (queue_free: deferred, no user code), then the new
			// animation and bookkeeping in place.
			FXTrailBake &bake = fx_trail_bakes[b];
			for (auto &created : bake.shards) {
				if (MultiMeshInstance2D *node = created.get()) {
					// Zero first: a queued-free node still renders this frame.
					Ref<MultiMesh> mm = node->get_multimesh();
					if (mm.is_valid()) {
						for (int i = 0; i < mm->get_instance_count(); ++i) {
							mm->set_instance_transform_2d(i, zero_transform);
						}
					}
					node->queue_free();
				}
			}
			bake.shards.clear();
			bake.shard_multimeshes.clear();
			bake.shard_visible.clear();
			bake.shard_refcount.clear();
			bake.anim = std::move(baked);
			bake.animation_override = true;
			bake.bullet_shard.assign(amount_bullets, -1);
			bake.bullet_trail_transf.assign(amount_bullets, Transform2D());
			bake.bullet_trail_tint.assign(amount_bullets, Color(1, 1, 1, 1));
			if ((int)bake.phase.size() != amount_bullets) {
				bake.phase.assign(amount_bullets, 0.0);
			}
			if ((int)bake.bullet_on.size() != amount_bullets) {
				bake.bullet_on.assign(amount_bullets, 1);
			}
		}
		// Create the shards without holding a reference into the bake list:
		// each add_child runs node_added handlers (user code) that may replace
		// the layers or ask to free the volley (refused by the guard above).
		const int frame_count = (int)fx_trail_bakes[b].anim.frames.size();
		for (int f = 0; f < frame_count; ++f) {
			const Ref<Texture2D> frame = fx_trail_bakes[b].anim.frames[f];
			const Ref<EffectLayerData2D> layer = fx_trail_bakes[b].layer;
			MultiMeshInstance2D *shard = nullptr;
			guarded_user_call2d(this, emit_guard, [&]() {
				shard = BulletFactory2D::fx_create_shard(this, frame, BulletFactory2D::fx_quad_size_for_texture(frame), layer->material, layer->self_modulate, layer->z_index, layer->z_as_relative, layer->visibility_layer, layer->light_mask, amount_bullets, true);
			});
			if (fx_trail_epoch != epoch || !survived_user_code(self_id) || is_queued_for_deletion()) {
				if (shard != nullptr) {
					shard->queue_free();
				}
				return false; // a handler replaced the layers or freed the volley
			}
			FXTrailBake &live = fx_trail_bakes[b];
			live.shards.push_back(shard);
			live.shard_multimeshes.push_back(shard != nullptr ? shard->get_multimesh() : Ref<MultiMesh>());
		}
		FXTrailBake &live = fx_trail_bakes[b];
		live.shard_visible.assign(live.shards.size(), 0);
		live.shard_refcount.assign(live.shards.size(), 0);
		return true;
	}
	return false;
}

TypedArray<EffectLayerData2D> BulletVolley2D::get_effect_layers() const {
	return fx_data_layers;
}

void BulletVolley2D::set_effect_layers(const TypedArray<EffectLayerData2D> &new_layers) {
	if (reject_pooled_handle("set_effect_layers")) {
		return; // a pooled handle keeps no layers (nor their baked frames)
	}
	// Live rebake without a spawn flash (edits must not detonate): trigger
	// routing, trail shards and factory bakes all follow the new list.
	fx_reseed_from_data(new_layers, false);
}

} // namespace BlastBullets2D
