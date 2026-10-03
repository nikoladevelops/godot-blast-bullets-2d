// Sprite effect layers (BulletEffectLayerData2D): trail shards that follow bullets
// and one-shot effects fired on spawn/hit/destroy/bounce/clear.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

BulletVolley2D::FXLayerSnapshot BulletVolley2D::fx_snapshot_layer(const Ref<BulletEffectLayerData2D> &layer) {
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
void BulletVolley2D::fx_rebuild_trail_layers(const TypedArray<BulletEffectLayerData2D> &layers) {
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

void BulletVolley2D::fx_clear_trail_layers() {
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
bool BulletVolley2D::fx_layers_match_seeded(const TypedArray<BulletEffectLayerData2D> &layers) const {
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

void BulletVolley2D::fx_soft_reset_trail_layers() {
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

void BulletVolley2D::fx_reseed_from_data(const TypedArray<BulletEffectLayerData2D> &layers, bool fire_spawn) {
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
bool BulletVolley2D::play_effect_animation(int layer_index, const StringName &animation) {
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

TypedArray<BulletEffectLayerData2D> BulletVolley2D::get_effect_layers() const {
	return fx_data_layers;
}

void BulletVolley2D::set_effect_layers(const TypedArray<BulletEffectLayerData2D> &new_layers) {
	// Live rebake without a spawn flash (edits must not detonate): trigger
	// routing, trail shards and factory bakes all follow the new list.
	fx_reseed_from_data(new_layers, false);
}

} // namespace BlastBullets2D
