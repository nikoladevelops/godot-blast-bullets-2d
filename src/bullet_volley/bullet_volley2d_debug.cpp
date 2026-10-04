// debug_* introspection used by the test suites and by curious users. Never on a
// hot path.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

bool BulletVolley2D::debug_dedup_probe(int bullet_index, int64_t target_instance_id) {
	return collision_already_queued(bullet_index, target_instance_id);
}

void BulletVolley2D::debug_dedup_mark(int bullet_index, int64_t target_instance_id) {
	mark_collision_queued(bullet_index, target_instance_id);
}

Dictionary BulletVolley2D::debug_dedup_stats() const {
	Dictionary d;
	d["used"] = (int64_t)collision_dedup_slot_used;
	d["capacity"] = (int64_t)collision_dedup_slots.size();
	d["dedup_by_object"] = collision_dedup_by_object;
	return d;
}

Dictionary BulletVolley2D::debug_dedup_find_collision(int probe_count) const {
	Dictionary d;
	d["collided"] = false;
	d["probes"] = 0;
	if (probe_count <= 1) {
		return d;
	}
	std::vector<uint64_t> keys;
	keys.reserve((size_t)probe_count);
	for (int i = 0; i < probe_count; ++i) {
		keys.push_back(collision_dedup_key(i, (int64_t)1000003 + (int64_t)i * (int64_t)7919));
	}
	std::sort(keys.begin(), keys.end());
	for (size_t i = 1; i < keys.size(); ++i) {
		if (keys[i] == keys[i - 1]) {
			d["collided"] = true;
			d["probes"] = (int64_t)(i + 1);
			return d;
		}
	}
	d["probes"] = (int64_t)probe_count;
	return d;
}

void BulletVolley2D::debug_run_interpolation_pass() { interpolate_bullet_visuals(); }

Dictionary BulletVolley2D::debug_get_orbiting_info(int bullet_index) const {
	Dictionary d;
	d["valid"] = false;
	d["enabled"] = false;
	d["locked"] = false;
	d["center"] = Vector2(0, 0);
	d["angle"] = 0.0;
	d["radius"] = 0.0;
	d["direction"] = 0;
	d["texture_rotation"] = 0;
	d["follow_mode"] = 0;
	d["deadzone"] = 0.0;
	d["lock_policy"] = 0;
	d["rigid_follow"] = false;
	d["deque_src"] = "none";
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return d;
	}
	d["valid"] = true;
	if (bullet_index >= 0 && bullet_index < (int)all_orbiting_status.size()) {
		d["enabled"] = all_orbiting_status[bullet_index] != 0;
	}
	if (bullet_index >= 0 && bullet_index < (int)all_orbiting_data.size()) {
		const OrbitingData &o = all_orbiting_data[bullet_index];
		d["locked"] = o.is_locked_orbiting;
		d["center"] = o.locked_center;
		d["angle"] = o.angle;
		d["radius"] = o.radius;
		d["direction"] = (int)o.direction;
		d["texture_rotation"] = (int)o.texture_rotation;
		d["follow_mode"] = (int)o.follow_mode;
		d["deadzone"] = o.follow_deadzone;
		d["lock_policy"] = (int)o.lock_policy;
		d["rigid_follow"] = o.rigid_follow;
		if (o.is_locked_orbiting) {
			d["deque_src"] = "locked";
			return d;
		}
	}
	const HomingTargetDeque *live_deque = nullptr;
	bool is_per = false;
	if (bullet_index >= 0 && bullet_index < (int)all_bullet_homing_targets.size() && bullet_index < (int)all_homing_count.size()) {
		is_per = all_homing_count[bullet_index] > 0 && !all_bullet_homing_targets[bullet_index].empty();
	}
	if (orbit_live_deque_for_bullet(bullet_index, live_deque) && live_deque != nullptr) {
		d["deque_src"] = is_per ? "per" : "shared";
		d["center"] = live_deque->get_cached_front_target_global_position();
	}
	return d;
}

Vector2 BulletVolley2D::debug_get_previous_origin(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "debug_get_previous_origin")) {
		return Vector2(0, 0);
	}
	if (bullet_index < 0 || bullet_index >= (int)all_previous_instance_transf.size()) {
		return Vector2(0, 0);
	}
	return all_previous_instance_transf[bullet_index].get_origin();
}

Dictionary BulletVolley2D::debug_get_bounce_info(int bullet_index) const {
	Dictionary d;
	d["valid"] = false;
	d["bounce_count"] = 0;
	d["bounce_mask"] = bounce_mask;
	d["bounce_strength"] = bounce_strength;
	d["bounce_hit_consumed"] = bounce_hit_consumed;
	d["bounce_max_count"] = bounce_max_count;
	d["bounce_mode"] = bounce_mode;
	d["bounce_tilemap_layers"] = bounce_tilemap_layers;
	d["bounce_enabled"] = bounce_mask != 0;
	d["visual_pending"] = false;
	d["cooldown"] = 0.0;
	d["last_normal"] = Vector2(0, 0);
	d["last_target_velocity"] = Vector2(0, 0);
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return d;
	}
	d["valid"] = true;
	if (bullet_index >= 0 && bullet_index < (int)all_bounce_count.size()) {
		d["bounce_count"] = all_bounce_count[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)all_bounce_last_normal.size()) {
		d["last_normal"] = all_bounce_last_normal[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)all_bounce_last_target_velocity.size()) {
		d["last_target_velocity"] = all_bounce_last_target_velocity[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)bounce_visual_pending.size()) {
		d["visual_pending"] = bounce_visual_pending[bullet_index] != 0;
	}
	if (bullet_index >= 0 && bullet_index < (int)all_bounce_cooldown.size()) {
		d["cooldown"] = all_bounce_cooldown[bullet_index];
	}
	// Seconds left before this bullet may bounce off the SAME target
	// again (0 when the window is over, disarmed, or never bounced).
	double debounce_left = 0.0;
	if (bounce_debounce_sec > 0.0 && Math::is_finite((double)bounce_debounce_sec)
			&& bullet_index >= 0 && bullet_index < (int)all_bounce_last_target.size()
			&& bullet_index < (int)all_bounce_last_time.size()
			&& all_bounce_last_target[bullet_index] != 0 && Math::is_finite(curves_elapsed_time)) {
		const double anchor = all_bounce_last_time[bullet_index];
		if (Math::is_finite(anchor)) {
			debounce_left = Math::max(0.0, (double)bounce_debounce_sec - (curves_elapsed_time - anchor));
		}
	}
	d["debounce"] = debounce_left;
	return d;
}

Dictionary BulletVolley2D::debug_get_curves_info(int bullet_index) const {
	Dictionary d;
	d["valid"] = false;
	d["x_src"] = "none";
	d["y_src"] = "none";
	d["rot_src"] = "none";
	d["speed_src"] = "none";
	d["gravity_src"] = "none";
	d["has_per_bullet_resource"] = false;
	d["has_shared_fallback"] = shared_bullet_curves_data.is_valid();
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return d;
	}
	d["valid"] = true;
	const BulletCurvesData2D *per = (bullet_index >= 0 && bullet_index < (int)all_bullet_curves_data.size() && all_bullet_curves_data[bullet_index].is_valid()) ? all_bullet_curves_data[bullet_index].ptr() : nullptr;
	const BulletCurvesData2D *shared = shared_bullet_curves_data.is_valid() ? shared_bullet_curves_data.ptr() : nullptr;
	d["has_per_bullet_resource"] = per != nullptr;
	auto pick = [](const BulletCurvesData2D *p, const BulletCurvesData2D *s, bool p_has, bool s_has) -> String {
		if (p_has) {
			return "per";
		}
		if (s_has) {
			return "shared";
		}
		(void)p;
		(void)s;
		return "none";
	};
	d["x_src"] = pick(per, shared, per != nullptr && per->x_direction_curve.is_valid(), shared != nullptr && shared->x_direction_curve.is_valid());
	d["y_src"] = pick(per, shared, per != nullptr && per->y_direction_curve.is_valid(), shared != nullptr && shared->y_direction_curve.is_valid());
	d["rot_src"] = pick(per, shared, per != nullptr && per->rotation_speed_curve.is_valid(), shared != nullptr && shared->rotation_speed_curve.is_valid());
	d["speed_src"] = pick(per, shared, per != nullptr && per->movement_speed_curve.is_valid(), shared != nullptr && shared->movement_speed_curve.is_valid());
	d["gravity_src"] = pick(per, shared, per != nullptr && per->gravity_strength_curve.is_valid(), shared != nullptr && shared->gravity_strength_curve.is_valid());
	return d;
}

Dictionary BulletVolley2D::debug_get_pattern_info(int bullet_index) const {
	Dictionary d;
	d["valid"] = false;
	d["src"] = "none";
	d["face"] = false;
	d["repeat"] = true;
	d["distance"] = 0.0;
	d["length"] = 0.0;
	d["finished"] = false;
	d["has_shared_fallback"] = shared_movement_pattern_curve.is_valid();
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return d;
	}
	d["valid"] = true;
	if (bullet_index >= 0 && bullet_index < (int)all_movement_pattern_data.size() && all_movement_pattern_data[bullet_index].path_curve.is_valid()) {
		const auto &p = all_movement_pattern_data[bullet_index];
		d["src"] = "per";
		d["face"] = p.face_movement_direction;
		d["repeat"] = p.repeat_pattern;
		d["distance"] = p.distance_traveled;
		const real_t len = p.path_curve->get_baked_length();
		d["length"] = Math::is_finite(len) ? len : 0.0;
		d["finished"] = false;
		return d;
	}
	if (shared_movement_pattern_curve.is_valid() && bullet_index >= 0 && bullet_index < (int)shared_movement_pattern_distances.size()) {
		const real_t len = shared_movement_pattern_curve->get_baked_length();
		const real_t safe_len = Math::is_finite(len) ? len : 0.0;
		const real_t dist = shared_movement_pattern_distances[bullet_index];
		d["src"] = "shared";
		d["face"] = shared_movement_pattern_face_movement_direction;
		d["repeat"] = shared_movement_pattern_repeat;
		d["distance"] = dist;
		d["length"] = safe_len;
		d["finished"] = !shared_movement_pattern_repeat && safe_len >= 0.001 && dist >= safe_len;
		return d;
	}
	return d;
}

Dictionary BulletVolley2D::debug_get_volley_info() const {
	Dictionary d;
	d["amount_bullets"] = amount_bullets;
	d["active_bullets"] = active_bullets_counter;
	d["generation"] = volley_generation;
	d["owner_spawner_id"] = (int64_t)owner_spawner_id;
	d["is_active"] = is_active;
	d["is_pooled"] = is_pooled_in_pool;
	const PoolKey k = get_pool_key();
	d["pool_amount"] = k.amount_bullets;
	d["pool_shape"] = (int)k.shape_type;
	d["auto_pool_volley"] = is_auto_pooling_enabled;
	d["auto_pool_attachments"] = is_attachments_auto_pooling_enabled;
	d["self_modulate"] = get_self_modulate();
	return d;
}

Dictionary BulletVolley2D::debug_get_shape_state() const {
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

Dictionary BulletVolley2D::debug_get_attachment_info(int bullet_index) const {
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
	d["owner_match"] = a->owner_volley_id == get_instance_id() && a->owner_bullet_index == bullet_index;
	return d;
}

Dictionary BulletVolley2D::debug_get_effect_layers_info() const {
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

Transform2D BulletVolley2D::debug_get_trail_transform(int layer_index, int bullet_index) const {
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

Dictionary BulletVolley2D::debug_get_gravity_info(int bullet_index) const {
	Dictionary d;
	d["vector"] = Vector2(0, 0);
	d["fall_speed"] = 0.0;
	d["window_active"] = false;
	d["curve_scale"] = 1.0;
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return d;
	}
	if (bullet_index >= 0 && bullet_index < (int)all_gravity.size()) {
		d["vector"] = all_gravity[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)all_gravity_velocity.size()) {
		d["fall_speed"] = all_gravity_velocity[bullet_index].length();
	}
	d["window_active"] = gravity_window_open();
	const BulletCurvesData2D *grav_shared = shared_bullet_curves_data.is_valid() ? shared_bullet_curves_data.ptr() : nullptr;
	const BulletCurvesData2D *grav_per = (bullet_index >= 0 && bullet_index < (int)all_bullet_curves_data.size() && all_bullet_curves_data[bullet_index].is_valid()) ? all_bullet_curves_data[bullet_index].ptr() : nullptr;
	d["curve_scale"] = gravity_strength_scale_for_bullet(grav_shared, grav_per);
	return d;
}

Dictionary BulletVolley2D::debug_get_wobble_info(int bullet_index) const {
	Dictionary d;
	d["active"] = false;
	d["amplitude"] = 0.0;
	d["frequency_hz"] = 0.0;
	d["mode"] = 0;
	d["waveform"] = 0;
	d["phase"] = 0.0;
	d["damping_per_sec"] = 0.0;
	d["delay_sec"] = 0.0;
	d["duration_sec"] = 0.0;
	d["face_movement_direction"] = false;
	d["face_rotation_speed"] = 0.0;
	d["has_per_bullet_resource"] = false;
	d["has_shared_fallback"] = shared_bullet_wobble_data.is_valid();
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return d;
	}
	if (bullet_index >= 0 && bullet_index < (int)all_bullet_wobble.size()) {
		const WobbleSeed &w = all_bullet_wobble[bullet_index];
		d["active"] = w.active;
		d["amplitude"] = w.active ? w.amplitude : 0.0;
		d["frequency_hz"] = w.active ? w.frequency_hz : 0.0;
		d["mode"] = w.active ? w.mode : 0;
		d["waveform"] = w.active ? w.waveform : 0;
		d["phase"] = w.active ? w.phase : 0.0;
		d["damping_per_sec"] = w.active ? w.damping_per_sec : 0.0;
		d["delay_sec"] = w.active ? w.delay_sec : 0.0;
		d["duration_sec"] = w.active ? w.duration_sec : 0.0;
		d["face_movement_direction"] = w.active && w.face_movement_direction;
		d["face_rotation_speed"] = w.active ? w.face_rotation_speed : 0.0;
	}
	if (bullet_index >= 0 && bullet_index < (int)all_bullet_wobble_data.size()) {
		d["has_per_bullet_resource"] = all_bullet_wobble_data[bullet_index].is_valid();
	}
	return d;
}

Dictionary BulletVolley2D::debug_get_bullet_info(int bullet_index) const {
	Dictionary d;
	d["index"] = bullet_index;
	d["valid"] = false;
	d["active"] = false;
	d["direction"] = Vector2(0, 0);
	d["velocity"] = Vector2(0, 0);
	d["speed"] = 0.0;
	d["gravity"] = Vector2(0, 0);
	d["fall_speed"] = 0.0;
	d["wobble_active"] = false;
	d["wobble_amplitude"] = 0.0;
	d["has_homing_targets"] = false;
	d["homing_targets_amount"] = 0;
	d["homing_smoothing"] = 0.0;
	d["orbiting_enabled"] = false;
	d["orbiting_locked"] = false;
	d["pattern"] = false;
	d["shared_pattern_active"] = shared_movement_pattern_curve.is_valid();
	d["rotation_speed"] = 0.0;
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return d;
	}
	d["valid"] = true;
	d["active"] = all_bullets_enabled_set.contains(bullet_index);
	if (bullet_index >= 0 && bullet_index < (int)all_cached_direction.size()) {
		d["direction"] = all_cached_direction[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)all_cached_velocity.size()) {
		d["velocity"] = all_cached_velocity[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)all_cached_speed.size()) {
		d["speed"] = all_cached_speed[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)all_gravity.size()) {
		d["gravity"] = all_gravity[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)all_gravity_velocity.size()) {
		d["fall_speed"] = all_gravity_velocity[bullet_index].length();
	}
	if (bullet_index >= 0 && bullet_index < (int)all_bullet_wobble.size()) {
		const WobbleSeed &w = all_bullet_wobble[bullet_index];
		d["wobble_active"] = w.active;
		d["wobble_amplitude"] = w.active ? w.amplitude : 0.0;
	}
	if (bullet_index >= 0 && bullet_index < (int)all_bullet_homing_targets.size() && bullet_index < (int)all_homing_count.size()) {
		d["has_homing_targets"] = all_homing_count[bullet_index] > 0 && !all_bullet_homing_targets[bullet_index].empty();
		d["homing_targets_amount"] = all_homing_count[bullet_index];
	}
	d["homing_smoothing"] = bullet_get_homing_smoothing(bullet_index);
	if (bullet_index >= 0 && bullet_index < (int)all_orbiting_status.size()) {
		d["orbiting_enabled"] = all_orbiting_status[bullet_index] != 0;
	}
	if (bullet_index >= 0 && bullet_index < (int)all_orbiting_data.size()) {
		d["orbiting_locked"] = all_orbiting_data[bullet_index].is_locked_orbiting;
	}
	d["pattern"] = check_exists_bullet_movement_pattern_data(bullet_index);
	if (bullet_index >= 0 && bullet_index < (int)all_rotation_speed.size()) {
		d["rotation_speed"] = all_rotation_speed[bullet_index];
	}
	return d;
}

Dictionary BulletVolley2D::debug_get_clocks() const {
	// Every clock a volley advances, in one read-only snapshot (tests/support).
	Dictionary d;
	d["curves_elapsed_time"] = curves_elapsed_time;
	d["life_time_left"] = get_life_time_left();
	d["max_life_time"] = max_life_time;
	d["is_life_time_infinite"] = is_life_time_infinite;
	d["homing_update_timer"] = homing_update_timer;
	d["homing_update_interval"] = homing_update_interval;
	d["anim_frame_index"] = anim_frame_index;
	d["anim_frame_time_left"] = anim_frame_time_left;
	d["anim_finished"] = anim_finished;
	d["fade_alpha"] = fade_applied.a;
	Array timers;
	for (const CustomTimer &t : custom_timers) {
		Dictionary e;
		e["id"] = (int64_t)t._id;
		e["time_left"] = t._current_time;
		e["period"] = t._initial_time;
		e["repeating"] = t._repeating;
		timers.push_back(e);
	}
	d["timers"] = timers;
	PackedFloat32Array cooldowns;
	for (real_t c : all_bounce_cooldown) {
		cooldowns.push_back((float)c);
	}
	d["bounce_cooldowns"] = cooldowns;
	return d;
}

} // namespace BlastBullets2D
