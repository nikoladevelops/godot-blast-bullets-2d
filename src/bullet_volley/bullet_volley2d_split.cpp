// Splitting on the volley side: arming (the spawner's shared config, or
// split_set_data), the capture of a dying bullet (split_capture, called by
// every death path BEFORE the disable: the last disable pools this volley),
// the per-volley split timer, and seeding a child volley. The spawns
// themselves are the factory's (flush_splits in bullet_factory2d_split.cpp).

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletVolley2D::split_arm(const std::shared_ptr<SplitConfig2D> &config, const Ref<BulletVolleyData2D> &spawn_data, int depth, uint64_t shot_salt) {
	split_config = config;
	split_shot_salt = config != nullptr ? shot_salt : 0;
	split_spawn_data = config != nullptr ? spawn_data : Ref<BulletVolleyData2D>();
	split_depth = config != nullptr ? Math::max(0, depth) : 0;
	// What this life already spent stays spent (the timer of a chain that
	// fired, max_splits_per_bullet's counts): begin_life starts a life over.
	split_timer_idle_chain = nullptr;
	split_arming_cache.reset();
	split_arming_cache_serial = 0;
}

int BulletVolley2D::split_capture(int bullet_index, int cause, const Transform2D &pose) {
	const SplitConfig2D *config = split_config.get();
	if (config == nullptr || !config->enabled || config->split.is_null() || bullet_factory == nullptr || !factory_alive() || pickup_data.is_valid()) {
		return 0;
	}
	if (bullet_index < 0 || bullet_index >= amount_bullets || bullet_index >= (int)all_cached_speed.size() || bullet_index >= (int)all_cached_direction.size()) {
		return 0;
	}
	if (bullet_factory->get_is_tearing_down()) {
		return 0;
	}
	const Ref<BulletSplitStage2D> stage = config->split->get_stage_for_depth(split_depth);
	if (stage.is_null()) {
		return 0;
	}
	bool wanted = false;
	switch (cause) {
		case SPLIT_CAUSE_HIT:
			wanted = stage->split_on_hit;
			break;
		case SPLIT_CAUSE_LIFETIME_OVER:
			wanted = stage->split_on_lifetime_over;
			break;
		case SPLIT_CAUSE_TIMER:
			wanted = stage->split_after_sec > 0.0;
			break;
		case SPLIT_CAUSE_DESPAWN:
			wanted = stage->split_on_despawn;
			break;
		case SPLIT_CAUSE_CLEAR:
			wanted = stage->split_on_clear;
			break;
		case SPLIT_CAUSE_MANUAL:
			wanted = true;
			break;
		case SPLIT_CAUSE_BOUNCE:
			wanted = stage->split_on_bounce;
			break;
		case SPLIT_CAUSE_NON_LETHAL_HIT:
			wanted = stage->split_on_non_lethal_hit;
			break;
		case SPLIT_CAUSE_HOMING_REACHED:
			wanted = stage->split_on_homing_reached;
			break;
		case SPLIT_CAUSE_GRAZE:
			wanted = stage->split_on_graze;
			break;
		default:
			break;
	}
	if (!wanted) {
		return 0;
	}
	const Ref<BulletVolleyData2D> data = stage->child_data.is_valid() ? stage->child_data : split_spawn_data;
	if (data.is_null()) {
		return 0;
	}
	// A bullet that already split max_splits_per_bullet times (it survived
	// its earlier splits) splits no more; the limit only counts surviving
	// parents (a bullet that died splitting and was revived splits again).
	// Manual splits ignore the limit and the odds (the script decided).
	if (cause != SPLIT_CAUSE_MANUAL && stage->parent_survives && stage->max_splits_per_bullet > 0 && bullet_index < (int)split_counts.size() && (int64_t)split_counts[bullet_index] >= (int64_t)stage->max_splits_per_bullet) {
		return 0;
	}
	if (cause != SPLIT_CAUSE_MANUAL && stage->chance < 1.0) {
		if (stage->chance <= 0.0) {
			return 0;
		}
		const double roll = stage->seed != 0 ? split_seeded_unit(stage->seed, split_roll_salt(split_shot_salt, split_count_of(bullet_index)), split_depth, bullet_index, 0, SPLIT_ROLL_CHANCE) : UtilityFunctions::randf();
		if (roll >= (double)stage->chance) {
			return 0;
		}
	}
	if (!bullet_factory->split_has_room()) {
		return 0; // the queue is full: no split (counted and warned once)
	}
	SplitRequest2D request;
	request.config = split_config;
	request.stage = stage;
	request.data = data;
	request.stage_data = stage->child_data.is_valid();
	// A parent still growing in: full-size children. The drawn pose carries
	// the parent's texture rotation (cosmetic): stripped, so the children's
	// spawn applies a texture rotation once (like the tick's own strip).
	request.pose = full_grown2d(pose);
	if (cache_texture_rotation_radians != 0.0) {
		request.pose = request.pose.rotated_local(-cache_texture_rotation_radians);
	}
	request.direction = all_cached_direction[bullet_index];
	request.speed = all_cached_speed[bullet_index];
	request.max_speed = bullet_index < (int)all_cached_max_speed.size() ? all_cached_max_speed[bullet_index] : request.speed;
	request.acceleration = bullet_index < (int)all_cached_acceleration.size() ? all_cached_acceleration[bullet_index] : 0.0f;
	request.inherited_velocity = inherited_velocity_offset;
	if (bounce_frames_active && bullet_index < (int)all_bounce_frame.size()) {
		request.framed = true;
		request.frame_x = all_bounce_frame[bullet_index].x_axis;
		request.frame_y = all_bounce_frame[bullet_index].y_axis;
	}
	request.owner_id = owner_spawner_id;
	request.shot_salt = split_shot_salt;
	request.roll_salt = split_roll_salt(split_shot_salt, split_count_of(bullet_index));
	request.parent_key = get_pool_key();
	request.parent_index = bullet_index;
	request.child_depth = split_depth + 1;
	request.cause = cause;
	request.collision_count = bullet_index < (int)bullets_current_collision_count.size() ? bullets_current_collision_count[bullet_index] : 0;
	if (stage->keep_custom_data) {
		request.custom_data = bullet_get_custom_data(bullet_index);
	}
	if (stage->keep_homing) {
		// The target the bullet chases now: its own front, else the shared front.
		if (bullet_index < (int)all_homing_count.size() && bullet_index < (int)all_bullet_homing_targets.size() && all_homing_count[bullet_index] > 0 && !all_bullet_homing_targets[bullet_index].empty()) {
			request.homing = all_bullet_homing_targets[bullet_index].front();
		} else if (!shared_homing_deque.empty()) {
			request.homing = shared_homing_deque.front();
		}
		SplitRequest2D::HomingSteering &s = request.steering;
		s.smoothing = (use_per_bullet_homing_smoothing && bullet_index < (int)all_bullet_homing_smoothing.size()) ? all_bullet_homing_smoothing[bullet_index] : homing_smoothing;
		s.update_interval = homing_update_interval;
		s.distance_before_reached = homing_distance_before_reached;
		s.delay_sec = homing_delay_sec;
		s.duration_sec = homing_duration_sec;
		s.lose_range_px = homing_lose_range_px;
		s.reach_counts_bullet_size = homing_reach_counts_bullet_size;
		s.take_control = homing_take_control_of_texture_rotation;
		s.auto_pop_shared = shared_homing_deque_auto_pop_after_target_reached;
		s.auto_pop_bullet = bullet_homing_auto_pop_after_target_reached;
		request.has_steering = true;
	}
	// The parent's own per-bullet settings (its entries of per-bullet arrays
	// or per-bullet writes), for every child that copies the parent's data.
	if (!request.stage_data) {
		if (bullet_index < (int)all_bullet_curves_data.size() && all_bullet_curves_data[bullet_index].is_valid()) {
			request.curves = all_bullet_curves_data[bullet_index];
		}
		if (bullet_index < (int)all_bullet_wobble_data.size() && all_bullet_wobble_data[bullet_index].is_valid()) {
			request.wobble = all_bullet_wobble_data[bullet_index];
		}
		if (bullet_index < (int)has_per_bullet_gravity.size() && has_per_bullet_gravity[bullet_index] && bullet_index < (int)all_gravity.size()) {
			request.gravity = all_gravity[bullet_index];
			request.has_gravity = true;
		}
		if (bullet_index < (int)has_per_bullet_rotation_data.size() && has_per_bullet_rotation_data[bullet_index] && bullet_index < (int)all_rotation_speed.size() && bullet_index < (int)all_max_rotation_speed.size() && bullet_index < (int)all_rotation_acceleration.size()) {
			request.rotation_speed = all_rotation_speed[bullet_index];
			request.max_rotation_speed = all_max_rotation_speed[bullet_index];
			request.rotation_acceleration = all_rotation_acceleration[bullet_index];
			request.has_rotation = true;
		}
		if (colors_enabled) {
			request.color = get_bullet_color(bullet_index);
			request.has_color = true;
		}
	}
	if (stage->keep_graze || stage->keep_sound || stage->keep_flock) {
		// One snapshot per arming change of this life, shared by its requests.
		if (split_arming_cache == nullptr || split_arming_cache_serial != split_arming_serial) {
			std::shared_ptr<SplitArming2D> arming(new SplitArming2D());
			for (int z = 0; z < graze_zone_slots && z < MAX_GRAZE_ZONES; ++z) {
				arming->graze_zones[z] = graze_zones[z];
			}
			arming->graze_slots = graze_zone_slots;
			arming->graze_detector = graze_detector;
			arming->graze_target_group = graze_target_group;
			arming->sound_effects = sound_effects;
			arming->sound_detector = sound_detector;
			arming->sound_listener_group = sound_listener_group;
			arming->sound_volume_offset_db = sound_volume_offset_db;
			arming->flock = flock_config;
			split_arming_cache = arming;
			split_arming_cache_serial = split_arming_serial;
		}
		request.arming = split_arming_cache;
	}
	bullet_factory->split_enqueue(std::move(request));
	if (cause == SPLIT_CAUSE_MANUAL && split_manual_tally != nullptr) {
		++*split_manual_tally;
	}
	if ((int)split_counts.size() != amount_bullets) {
		split_counts.assign(amount_bullets, 0);
	}
	static_assert(sizeof(split_counts[0]) >= sizeof(int32_t), "a split count holds every max_splits_per_bullet");
	if (split_counts[bullet_index] < (uint32_t)INT32_MAX) {
		++split_counts[bullet_index];
	}
	// The split's own report, at the parent's pose (no user code: offers and
	// one-shot effects only).
	sound_fire(BulletSoundData2D::SOUND_ON_SPLIT, bullet_index, pose.get_origin());
	fx_fire_oneshot(EFFECT_ON_SPLIT, bullet_index, pose);
	int flags = SPLIT_RECORDED;
	if (!stage->fire_death_effects) {
		flags |= SPLIT_SILENCE_DEATH;
	}
	if (stage->parent_survives && split_cause_survivable(cause)) {
		flags |= SPLIT_PARENT_SURVIVES;
	}
	return flags;
}

bool BulletVolley2D::split_on_event(int bullet_index, int cause, const Transform2D &pose) {
	const int flags = split_capture(bullet_index, cause, pose);
	if (!(flags & SPLIT_RECORDED) || (flags & SPLIT_PARENT_SURVIVES)) {
		return false;
	}
	// The children replace it: no death effect of its own (it did not die),
	// On Split already fired. Attachment callbacks run inside.
	disable_bullet_core(bullet_index, true, false, "split");
	return true;
}

int BulletVolley2D::split_live_bullets(const std::vector<int> &indexes, int cause, int death_trigger) {
	// The stage decides whether the parents stay (no disable, no user code)
	// or leave through the batch disable (attachment callbacks run inside).
	const Ref<BulletSplitStage2D> stage = split_config != nullptr && split_config->split.is_valid() ? split_config->split->get_stage_for_depth(split_depth) : Ref<BulletSplitStage2D>();
	if (stage.is_valid() && stage->parent_survives && split_cause_survivable(cause)) {
		int recorded = 0;
		for (const int i : indexes) {
			if (i >= 0 && i < (int)all_cached_instance_transforms.size() && all_bullets_enabled_set.contains(i) && (split_capture(i, cause, all_cached_instance_transforms[i]) & SPLIT_RECORDED)) {
				++recorded;
			}
		}
		return recorded;
	}
	// Only the bullets that split leave (a lost roll, a reached
	// max_splits_per_bullet or a missing child data keeps the bullet).
	// A manual split leaves like a clear (On Clear): its own name in the log.
	disable_bullets_bulk(indexes, death_trigger, nullptr, cause, true, cause == SPLIT_CAUSE_MANUAL ? "split_manual" : "split");
	return -1; // the caller counts through the factory (the batch may free things)
}

void BulletVolley2D::split_check_timer() {
	if (split_config == nullptr || !split_config->enabled || split_config->split.is_null() || active_bullets_counter <= 0) {
		return;
	}
	// Most stages have no timer: remember that until the chain or any
	// split rules change (the lookup reads a script Array every tick).
	const BulletSplitData2D *chain = split_config->split.ptr();
	if (split_timer_fired_chain != 0 && split_timer_fired_chain == (uint64_t)chain->get_instance_id()) {
		return; // this chain's timer already fired this life
	}
	if (split_timer_idle_chain == chain && split_timer_idle_epoch == BulletSplitStage2D::rules_epoch) {
		return;
	}
	const Ref<BulletSplitStage2D> stage = chain->get_stage_for_depth(split_depth);
	if (stage.is_null() || stage->split_after_sec <= 0.0) {
		split_timer_idle_chain = chain;
		split_timer_idle_epoch = BulletSplitStage2D::rules_epoch;
		return;
	}
	if (curves_elapsed_time < (double)stage->split_after_sec) {
		return;
	}
	split_timer_fired_chain = (uint64_t)chain->get_instance_id();
	// Local copy: the batch mutates the live set and runs attachment callbacks.
	const std::vector<int> live = all_bullets_enabled_set.get_active_indexes();
	split_live_bullets(live, SPLIT_CAUSE_TIMER, -1);
}

void BulletVolley2D::split_seed_children(const SplitRequest2D &request, const Vector2 *directions, const real_t *speed_multipliers, int count) {
	const BulletSplitStage2D *stage = request.stage.ptr();
	if (stage == nullptr) {
		return;
	}
	// A bounced parent hands its mirror frame on (before the velocities below
	// compose the mirrored drift).
	if (request.framed) {
		ensure_bounce_frames();
		BounceFrame2D frame;
		frame.x_axis = request.frame_x;
		frame.y_axis = request.frame_y;
		for (int i = 0; i < Math::min(count, (int)all_bounce_frame.size()); ++i) {
			all_bounce_frame[i] = frame;
		}
		bounce_frames_active = true;
	}
	const int limit = Math::min(count, Math::min(amount_bullets, Math::min((int)all_cached_speed.size(), (int)all_cached_direction.size())));
	const bool has_max = (int)all_cached_max_speed.size() >= limit;
	const bool has_acceleration = (int)all_cached_acceleration.size() >= limit;
	for (int i = 0; i < limit; ++i) {
		const real_t m = speed_multipliers[i];
		real_t speed = all_cached_speed[i];
		real_t max_speed = has_max ? all_cached_max_speed[i] : speed;
		real_t acceleration = has_acceleration ? all_cached_acceleration[i] : 0.0f;
		switch (stage->speed_mode) {
			case BulletSplitStage2D::SPEED_MULTIPLY_PARENT:
				speed = request.speed * m;
				max_speed = request.max_speed * m;
				acceleration = request.acceleration * m;
				break;
			case BulletSplitStage2D::SPEED_FIXED:
				speed = stage->fixed_speed * m;
				// A top speed of 0 means no cap: keep it unlimited.
				if (max_speed > 0.0f) {
					max_speed = Math::max(max_speed, speed);
				}
				break;
			case BulletSplitStage2D::SPEED_FROM_SPAWN_DATA:
			default:
				speed *= m;
				max_speed *= m;
				acceleration *= m;
				break;
		}
		// An overflowing product keeps the spawn data's motion.
		if (Math::is_finite(speed) && Math::is_finite(max_speed) && Math::is_finite(acceleration)) {
			all_cached_speed[i] = speed;
			if (has_max) {
				all_cached_max_speed[i] = max_speed;
			}
			if (has_acceleration) {
				all_cached_acceleration[i] = acceleration;
			}
		}
		all_cached_direction[i] = directions[i];
		refresh_cached_velocity(i);
	}
	// The parent's per-bullet settings, on every child.
	if (request.curves.is_valid() || request.wobble.is_valid() || request.has_gravity || request.has_rotation) {
		Ref<BulletRotationData2D> rotation;
		if (request.has_rotation) {
			rotation.instantiate();
			rotation->rotation_speed = request.rotation_speed;
			rotation->max_rotation_speed = request.max_rotation_speed;
			rotation->rotation_acceleration = request.rotation_acceleration;
		}
		for (int i = 0; i < limit; ++i) {
			if (request.curves.is_valid()) {
				bullet_set_curves_data(i, request.curves);
			}
			if (request.wobble.is_valid()) {
				bullet_set_wobble_data(i, request.wobble);
			}
			if (request.has_gravity) {
				bullet_set_gravity(i, request.gravity);
			}
			if (rotation.is_valid()) {
				set_bullet_rotation_data(i, rotation);
			}
		}
	}
	if (stage->keep_collision_count && request.collision_count > 0) {
		const int n = Math::min(limit, (int)bullets_current_collision_count.size());
		for (int i = 0; i < n; ++i) {
			bullets_current_collision_count[i] = request.collision_count;
		}
	}
	if (stage->keep_custom_data && request.custom_data.is_valid()) {
		for (int i = 0; i < limit; ++i) {
			bullet_set_custom_data(i, request.custom_data);
		}
	}
	const SplitArming2D *arming = request.arming.get();
	if (arming != nullptr) {
		if (stage->keep_graze && arming->graze_slots > 0) {
			graze_arm(arming->graze_zones, arming->graze_slots, arming->graze_detector, arming->graze_target_group);
		}
		if (stage->keep_sound && !arming->sound_effects.empty()) {
			sound_release();
			sound_effects = arming->sound_effects;
			sound_detector = arming->sound_detector;
			sound_listener_group = arming->sound_listener_group;
			sound_volume_offset_db = arming->sound_volume_offset_db;
			sound_mask_epoch = BulletSoundData2D::mask_epoch - 1; // recompute the mask below
			sound_refresh_mask();
			++split_arming_serial;
		}
		if (stage->keep_flock && arming->flock != nullptr) {
			flock_set_config(arming->flock);
		}
	}
}

bool BulletVolley2D::split_set_data(const Ref<BulletSplitData2D> &split, const Ref<BulletVolleyData2D> &child_spawn_data) {
	if (reject_pooled_handle("split_set_data")) {
		return false;
	}
	if (split.is_null()) {
		UtilityFunctions::push_error("split_set_data: split must be a BulletSplitData2D (split_clear() stops splitting), nothing changed.");
		return false;
	}
	if (pickup_data.is_valid()) {
		UtilityFunctions::push_error("split_set_data: pickups never split, nothing changed.");
		return false;
	}
	const Ref<BulletVolleyData2D> data = child_spawn_data.is_valid() ? child_spawn_data : split_spawn_data;
	if (data.is_null()) {
		UtilityFunctions::push_error("split_set_data: this volley has no spawn data to copy (pass child_spawn_data), nothing changed.");
		return false;
	}
	std::shared_ptr<SplitConfig2D> config(new SplitConfig2D());
	config->split = split;
	// A script call starts a fresh chain: generation 0 (children re-armed
	// this way split by the chain's first stage).
	split_arm(config, data, 0, (uint64_t)volley_generation);
	return true;
}

void BulletVolley2D::split_clear() {
	if (reject_pooled_handle("split_clear")) {
		return;
	}
	split_arm(nullptr, Ref<BulletVolleyData2D>(), 0);
}

Ref<BulletSplitData2D> BulletVolley2D::split_get_data() const {
	if (split_config == nullptr || !split_config->enabled) {
		return Ref<BulletSplitData2D>();
	}
	return split_config->split;
}

int BulletVolley2D::split_get_depth() const {
	return split_depth;
}

bool BulletVolley2D::split_bullet(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "split_bullet") || reject_pooled_handle("split_bullet")) {
		return false;
	}
	if (!all_bullets_enabled_set.contains(bullet_index)) {
		return false;
	}
	return all_bullets_split(bullet_index, bullet_index) > 0;
}

int BulletVolley2D::all_bullets_split(int bullet_index_start, int bullet_index_end_inclusive) {
	if (reject_pooled_handle("all_bullets_split")) {
		return 0;
	}
	std::vector<int> picked;
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_split", [&](int i) {
		if (all_bullets_enabled_set.contains(i)) {
			picked.push_back(i);
		}
	});
	// A bullet that cannot split stays (never cleared for nothing).
	if (picked.empty() || !split_possible_now()) {
		return 0;
	}
	const std::shared_ptr<bool> life = factory_life;
	// Counts this call's own splits: attachment callbacks inside the batch
	// may split other bullets (which are theirs to count).
	int tally = 0;
	int *const outer_tally = split_manual_tally;
	split_manual_tally = &tally;
	const uint64_t self_id = get_instance_id();
	const int kept = split_live_bullets(picked, SPLIT_CAUSE_MANUAL, EFFECT_ON_CLEAR);
	if (ObjectDB::get_instance(ObjectID(self_id)) == this) {
		split_manual_tally = outer_tally;
	}
	if (kept >= 0) {
		return kept; // the parents stayed: no user code ran
	}
	if (life != nullptr && !*life) {
		return 0; // a callback freed the factory
	}
	return tally;
}

bool BulletVolley2D::split_possible_now() const {
	if (split_config == nullptr || !split_config->enabled || split_config->split.is_null() || bullet_factory == nullptr || !factory_alive() || pickup_data.is_valid()) {
		return false;
	}
	const Ref<BulletSplitStage2D> stage = split_config->split->get_stage_for_depth(split_depth);
	return stage.is_valid() && (stage->child_data.is_valid() || split_spawn_data.is_valid());
}

} // namespace BlastBullets2D
