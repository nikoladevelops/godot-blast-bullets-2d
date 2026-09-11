#pragma once

#include "../shared/bullet_speed_data2d.hpp"
#include "../shared/homing_target_deque.hpp"
#include "godot_cpp/classes/curve2d.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/classes/path2d.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/core/defs.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/math_defs.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/callable.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/variant.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "multimesh_bullets2d.hpp"
#include "shared/bullet_curves_data2d.hpp"
#include "spawn-data/directional_bullets_data2d.hpp"
#include "spawn-data/multimesh_bullets_data2d.hpp"

#include <cstdint>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class DirectionalBullets2D : public MultiMeshBullets2D {
	GDCLASS(DirectionalBullets2D, MultiMeshBullets2D)

public:
	enum OrbitingDirection {
		DontMove = 0,
		OrbitLeft,
		OrbitRight
	};

	enum OrbitingTextureRotation {
		FaceTarget = 0,
		FaceOppositeTarget,
		FaceOrbitingDirection,
		FaceOppositeOrbitingDirection
	};

	struct OrbitingData {
		real_t angle = 0.0f;
		real_t radius = 0.0f;
		OrbitingDirection direction = OrbitRight;
		OrbitingTextureRotation texture_rotation = FaceTarget;
		bool is_locked_orbiting = false;

		OrbitingData() = default;

		OrbitingData(real_t new_radius, OrbitingDirection new_direction, OrbitingTextureRotation new_texture_rotation) :
				angle(0.0),
				radius(new_radius),
				direction(new_direction),
				texture_rotation(new_texture_rotation),
				is_locked_orbiting(false) {};
	};

protected:
	// Configuration flags
	bool adjust_direction_based_on_rotation = false;
	bool homing_take_control_of_texture_rotation = false;
	bool bullet_homing_auto_pop_after_target_reached = false;
	bool shared_homing_deque_auto_pop_after_target_reached = false;

	Vector2 cached_mouse_global_position{ 0, 0 };

	// ORBITING

	// For each bullet containing its orbiting data
	std::vector<OrbitingData> all_orbiting_data;

	// For each bullet whether the orbiting is enabled or not
	std::vector<uint8_t> all_orbiting_status;

	int active_orbiting_count = 0;
	//

	// HOMING

	double homing_update_interval = 0.0;
	double homing_update_timer = 0.0;
	real_t homing_smoothing = 0.0;

	// Minimum distance (in pixels) from the homing target at which the bullet is considered to have reached it. Once within this distance, the bullet_homing_target_reached signal is emitted
	real_t homing_distance_before_reached = 5.0;

	// This tracks each bullet's homing deque - allows each bullet to have its own separate homing targets (per-bullet homing)
	std::vector<HomingTargetDeque> all_bullet_homing_targets;

	// For each bullet's homing deque, store the amount targets
	std::vector<int> all_homing_count;

	// Per-bullet turn agility. Empty/unused unless any per-bullet smoothing was
	// set (use_per_bullet_homing_smoothing), in which case update_homing reads
	// the per-bullet value and the shared homing_smoothing is ignored.
	std::vector<real_t> all_bullet_homing_smoothing;
	bool use_per_bullet_homing_smoothing = false;

	// Tracks how many bullets are currently homing in TOTAL (per-bullet homing, NOT shared) - basically determines whether the per-bullet homing feature is even turned on
	int active_homing_count = 0;

	// Once-flag so the silent-homing footgun warns exactly once per multimesh lifetime
	// segment (reset on spawn/enable). See move_bullets homing branch.
	bool homing_inert_warning_issued = false;

	// This is a shared homing deque - allows the bullets to share the same target
	HomingTargetDeque shared_homing_deque;

	//

	// SHARED MOVEMENT PATTERN (from spawn data; per-bullet entries in
	// all_movement_pattern_data stay exclusively runtime-owned). The slot holds
	// only curve+flags - distance traveled is per bullet (see below), so one
	// shared pattern costs one curve instead of N copies.
	Ref<Curve2D> shared_movement_pattern_curve;
	bool shared_movement_pattern_face_movement_direction = false;
	bool shared_movement_pattern_repeat = true;
	// Per-bullet distance ledger for the shared pattern. Sized to
	// amount_bullets at spawn/enable; a finished non-repeating bullet is one
	// whose distance reached the curve length (no extra flag needed).
	std::vector<real_t> shared_movement_pattern_distances;

public:
	// Advances one bullet along a movement-pattern curve for this tick.
	// distance_traveled is updated in place. Returns false when the pattern is
	// finished (degenerate curve or completed non-repeating run); the caller
	// then clears per-bullet entries or parks the shared ledger at the end.
	// Single implementation shared by the per-bullet and shared patterns.
	_ALWAYS_INLINE_ bool advance_movement_pattern(const Ref<Curve2D> &curve, bool face_movement_direction, bool repeat_pattern, real_t &distance_traveled, Vector2 &velocity_delta, Vector2 &curr_bullet_direction, Transform2D &curr_bullet_transf) {
		const real_t len = curve->get_baked_length();
		if (len < 0.001) {
			return false;
		}
		const real_t prev_dist = distance_traveled;
		const real_t advance_dist = velocity_delta.length();
		distance_traveled += advance_dist;
		// Non-repeating patterns stop exactly at the end: clamp into the
		// final tile instead of wrapping once past it and snapping back.
		const real_t clamped_dist = (!repeat_pattern && distance_traveled >= len) ? len : distance_traveled;
		const real_t s1 = Math::fmod(prev_dist, len);
		const real_t s2 = Math::fmod(clamped_dist, len);
		const int64_t l1 = (int64_t)(prev_dist / len);
		const int64_t l2 = (int64_t)(clamped_dist / len);
		const Vector2 start = curve->sample_baked(0.0);
		const Vector2 end = curve->sample_baked(len * 0.9999);
		const Vector2 disp = end - start;
		const Vector2 p1 = l1 * disp + (curve->sample_baked(s1) - start);
		const Vector2 p2 = l2 * disp + (curve->sample_baked(s2) - start);
		Vector2 local_delta = p2 - p1;
		const real_t original_speed = velocity_delta.length();
		if (original_speed > 0.0001 && local_delta.length_squared() > 0.00000001) {
			Vector2 pattern_direction = local_delta.rotated(curr_bullet_direction.angle()).normalized();
			velocity_delta = pattern_direction * original_speed;
		}
		if (face_movement_direction && velocity_delta.length_squared() > 0.0001) {
			const Vector2 tangent = velocity_delta.normalized();
			// Preserve scale like set_bullet_texture_rotation_towards_position does.
			const Vector2 pattern_scale = curr_bullet_transf.get_scale();
			curr_bullet_transf.set_rotation_and_scale(tangent.angle(), pattern_scale);
		}
		if (!repeat_pattern && distance_traveled >= len) {
			if (face_movement_direction) {
				const Vector2 logical_dir = curr_bullet_direction.normalized();
				const Vector2 pattern_scale = curr_bullet_transf.get_scale();
				curr_bullet_transf.set_rotation_and_scale(logical_dir.angle(), pattern_scale);
			}
			return false;
		}
		return true;
	}

	// Updates all bullets' positions, rotations, and homing
	inline void move_bullets(double delta) {
		if (amount_bullets <= 0 || physics_server == nullptr || !area.is_valid()) {
			return;
		}
		if (!Math::is_finite(delta) || delta < 0.0) {
			return;
		}
		const bool is_using_physics_interpolation = bullet_factory != nullptr && bullet_factory->use_physics_interpolation;
		if (is_using_physics_interpolation) {
			update_all_previous_transforms_for_interpolation();
		}

		bool homing_interval_reached = false;

		bool shared_homing_deque_enabled = !shared_homing_deque.empty();
		const bool is_per_bullet_homing_enabled = (active_homing_count > 0);

		// If homing is enabled (either shared or per-bullet) update the timer and cache mouse position if needed
		if (shared_homing_deque_enabled || is_per_bullet_homing_enabled) {
			// Update homing timer / how often to update the homing target position
			homing_interval_reached = update_homing_timer(delta);

			// In case we have the mouse as a homing target, make sure to cache its global position
			if (homing_interval_reached && HomingTargetDeque::mouse_homing_targets_amount > 0) {
				cached_mouse_global_position = get_global_mouse_position();
			}
		}

		// Since shared homing deque is used for all bullets, do this once
		if (shared_homing_deque_enabled) {
			// Delete any invalid (freed) targets
			auto targets_amount = shared_homing_deque.get_homing_targets_amount();
			int trimmed = shared_homing_deque.bullet_homing_trim_front_invalid_targets(cached_mouse_global_position, targets_amount);
			if (trimmed > 0) {
				// Front changed, so every bullet gets a clean slate for the new target.
				// Without this a trimmed address could be reused and look already-fired.
				reset_shared_homing_reached_state();
			}
			shared_homing_deque_enabled = (targets_amount - trimmed) > 0;

			// If timer timed out, refresh the cached global position of the front target
			if (shared_homing_deque_enabled && homing_interval_reached) {
				shared_homing_deque.refresh_cached_front_target_global_position(cached_mouse_global_position);
			}
		}

		const bool is_orbiting_feature_enabled = (active_orbiting_count > 0);

		Vector2 homing_bullet_pos;
		Vector2 homing_target_pos;

		const bool shared_curves_data_enabled = shared_bullet_curves_data.is_valid();
		const BulletCurvesData2D *const shared_curves_ptr = shared_bullet_curves_data.ptr();

		const bool shared_curves_x_direction_curve_valid = shared_curves_data_enabled && shared_curves_ptr->x_direction_curve.is_valid();
		const bool shared_curves_y_direction_curve_valid = shared_curves_data_enabled && shared_curves_ptr->y_direction_curve.is_valid();

		const bool shared_curves_rotation_curve_valid = shared_curves_data_enabled && shared_curves_ptr->rotation_speed_curve.is_valid();
		const bool shared_curves_acceleration_curve_valid = shared_curves_data_enabled && shared_curves_ptr->movement_speed_curve.is_valid();

		// Hoist shared curve samples outside loop (same for all bullets in this multimesh)
		real_t shared_x_offset = 0, shared_y_offset = 0;
		real_t shared_x_strength = 0, shared_y_strength = 0;
		DirectionCurveMode shared_x_mode = DirectionCurveMode::Additive, shared_y_mode = DirectionCurveMode::Additive;
		real_t shared_movement_speed_val = 0, shared_rotation_speed_val = 0;
		if (shared_curves_data_enabled) {
			if (shared_curves_x_direction_curve_valid) {
				shared_x_offset = get_bullet_curves_x_direction_offset(shared_curves_ptr);
				shared_x_strength = shared_curves_ptr->x_direction_curve_strength;
				shared_x_mode = shared_curves_ptr->x_direction_curve_mode;
			}
			if (shared_curves_y_direction_curve_valid) {
				shared_y_offset = get_bullet_curves_y_direction_offset(shared_curves_ptr);
				shared_y_strength = shared_curves_ptr->y_direction_curve_strength;
				shared_y_mode = shared_curves_ptr->y_direction_curve_mode;
			}
			if (shared_curves_acceleration_curve_valid) {
				shared_movement_speed_val = get_bullet_curves_movement_speed(shared_curves_ptr);
			}
			if (shared_curves_rotation_curve_valid) {
				shared_rotation_speed_val = get_bullet_curves_rotation_speed(shared_curves_ptr);
			}
		}

		bool is_per_bullet_curves_valid = false;
		const BulletCurvesData2D *per_bullet_curves_data = nullptr;

		// Shared movement pattern curve sampled once (same for all bullets).
		const bool shared_pattern_curve_valid = shared_movement_pattern_curve.is_valid();
		real_t shared_pattern_len = 0.0;
		if (shared_pattern_curve_valid) {
			shared_pattern_len = shared_movement_pattern_curve->get_baked_length();
		}

		// Usability guard: homing targets without steering is a silent no-op (direction only
		// changes via rotate_to_target, movement patterns, rotation data, or orbiting).
		// Warn once.
		if (!homing_inert_warning_issued && (shared_homing_deque_enabled || is_per_bullet_homing_enabled) && !homing_take_control_of_texture_rotation && !is_rotation_data_active && active_orbiting_count == 0) {
			bool any_pattern = false;
			for (int pi : all_bullets_enabled_set.get_active_indexes()) {
				if (check_exists_bullet_movement_pattern_data(pi)) {
					any_pattern = true;
					break;
				}
			}
			// The spawn-data shared pattern counts too (per-bullet entries are
			// only half the story now).
			any_pattern = any_pattern || shared_movement_pattern_curve.is_valid();
			if (!any_pattern) {
				UtilityFunctions::push_warning("DirectionalBullets2D has homing targets but homing_take_control_of_texture_rotation is false (and no movement pattern/rotation data), so homing will not steer bullets. Set it to true.");
				homing_inert_warning_issued = true;
			}
		}

		// Loop only through ACTIVE bullets (skip the disabled ones)
		const auto &active_bullet_indexes = all_bullets_enabled_set.get_active_indexes();

		// BulletCurvesData2D is a mutable shared Resource: a user can gain a rotation
		// curve AFTER the multimesh was spawned/enabled, when populate_* had no reason
		// to size all_rotation_speed. Enforce the invariant once per tick so the
		// rotation branches below can never index out of bounds.
		if ((int)all_rotation_speed.size() != amount_bullets) {
			all_rotation_speed.resize(amount_bullets, 0.0);
		}
		// Same invariant for the shared-deque reached states (H5 fix storage).
		if ((int)all_shared_homing_reached.size() != amount_bullets) {
			all_shared_homing_reached.resize(amount_bullets);
		}

		for (int i : active_bullet_indexes) {
			if (i < 0 || i >= amount_bullets) {
				continue;
			}
			if (i >= (int)all_cached_instance_transforms.size() || i >= (int)all_cached_direction.size() || i >= (int)all_cached_velocity.size()) {
				continue;
			}
			bool direction_got_updated = false;
			HomingTargetDeque *target_deque_used_for_orbiting = nullptr;

			// 1. STANDARD HOMING PHASE
			if (shared_homing_deque_enabled) { // Handle homing towards shared deque (takes precedence over per-bullet homing)
				update_homing(shared_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
				try_to_emit_bullet_homing_target_reached_signal(shared_homing_deque, shared_homing_deque_enabled, i, homing_bullet_pos, homing_target_pos, delta);
				direction_got_updated = true;
				target_deque_used_for_orbiting = &shared_homing_deque;
			} else if (is_per_bullet_homing_enabled) { // Handle per-bullet homing
				// Whether the deque has any targets
				if (i < 0 || i >= (int)all_homing_count.size() || i >= (int)all_bullet_homing_targets.size()) {
					// Vectors out of sync, skip homing for this bullet this frame.
				} else {
				auto &curr_homing_count = all_homing_count[i];

				if (curr_homing_count > 0) {
					auto &curr_homing_deque = all_bullet_homing_targets[i];

					// Trim the invalid ones
					int trimmed_count = curr_homing_deque.bullet_homing_trim_front_invalid_targets(cached_mouse_global_position, curr_homing_count);

					// Very important to keep track of the amount of targets after trimming
					curr_homing_count -= trimmed_count; // count for the deque
					active_homing_count -= trimmed_count; // global count across all bullets that determines whether the per-bullet homing feature is even active
					if (curr_homing_count < 0) {
						curr_homing_count = 0;
					}
					if (active_homing_count < 0) {
						active_homing_count = 0;
					}

					if (curr_homing_count > 0) {
						// If per bullet homing is indeed active, then refresh the cache if interval has been reached
						if (homing_interval_reached) {
							curr_homing_deque.refresh_cached_front_target_global_position(cached_mouse_global_position);
						}

						update_homing(curr_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
						try_to_emit_bullet_homing_target_reached_signal(curr_homing_deque, shared_homing_deque_enabled, i, homing_bullet_pos, homing_target_pos, delta);
						direction_got_updated = true;
						target_deque_used_for_orbiting = &curr_homing_deque;
					}
				}
				}
			}

			auto &curr_bullet_transf = all_cached_instance_transforms[i];
			auto &curr_bullet_direction = all_cached_direction[i];

			// 2. DIRECTION CURVES - shared sampled once before loop
		// Per-bullet curves resolve regardless of shared data so individual channels
		// can fill gaps left by a partially specified shared resource (shared wins ties).
		is_per_bullet_curves_valid = (i >= 0 && i < (int)all_bullet_curves_data.size() && all_bullet_curves_data[i].is_valid());
		per_bullet_curves_data = is_per_bullet_curves_valid ? all_bullet_curves_data[i].ptr() : nullptr; // O(1) vector index, borrows - valid until vector reassigned
		const bool per_bullet_x_curve_valid = is_per_bullet_curves_valid && per_bullet_curves_data->x_direction_curve.is_valid();
		const bool per_bullet_y_curve_valid = is_per_bullet_curves_valid && per_bullet_curves_data->y_direction_curve.is_valid();

		const BulletCurvesData2D *direction_curves_for_texture = nullptr;
		if (shared_curves_x_direction_curve_valid) {
			if (shared_x_mode == DirectionCurveMode::Additive) {
				curr_bullet_direction.x += shared_x_offset * shared_x_strength;
			} else {
				curr_bullet_direction.x = shared_x_offset * shared_x_strength;
			}
			curr_bullet_direction = curr_bullet_direction.normalized();
			direction_curves_for_texture = shared_curves_ptr;
		} else if (per_bullet_x_curve_valid) {
			apply_x_direction_curve(curr_bullet_direction, per_bullet_curves_data);
			direction_curves_for_texture = per_bullet_curves_data;
		}

		if (shared_curves_y_direction_curve_valid) {
			if (shared_y_mode == DirectionCurveMode::Additive) {
				curr_bullet_direction.y += shared_y_offset * shared_y_strength;
			} else {
				curr_bullet_direction.y = shared_y_offset * shared_y_strength;
			}
			curr_bullet_direction = curr_bullet_direction.normalized();
			direction_curves_for_texture = shared_curves_ptr;
		} else if (per_bullet_y_curve_valid) {
			apply_y_direction_curve(curr_bullet_direction, per_bullet_curves_data);
			if (direction_curves_for_texture == nullptr) {
				direction_curves_for_texture = per_bullet_curves_data;
			}
		}

		if (direction_curves_for_texture != nullptr) {
			apply_direction_curve_texture_rotation_if_needed(curr_bullet_direction, curr_bullet_transf, delta, direction_curves_for_texture);
			direction_got_updated = true;
		}

			// 3. ROTATION - shared sampled once before loop
			if (shared_curves_rotation_curve_valid) {
				all_rotation_speed[i] = shared_rotation_speed_val;
				update_rotation_using_curve(i, delta);
			} else if (is_per_bullet_curves_valid && per_bullet_curves_data->rotation_speed_curve.is_valid()) {
				bullet_accelerate_rotation_speed_using_curve(i, delta, per_bullet_curves_data);
				update_rotation_using_curve(i, delta);
			} else if (is_rotation_data_active) {
				bullet_accelerate_rotation_speed(i, delta);
				update_rotation(i, delta);
			}

			// 4. ADJUST DIRECTION BASED ON THE NEW ROTATION (OPTIONALLY)
			if (adjust_direction_based_on_rotation) {
				// columns[0] includes the texture rotation used for rendering;
				// strip it to recover the logical movement direction.
				curr_bullet_direction = all_cached_instance_transforms[i].columns[0].rotated(-cache_texture_rotation_radians).normalized();
				direction_got_updated = true;
			}

			// 5. VELOCITY CALCULATION (ONLY IF DIRECTION GOT UPDATED) - use temp to avoid mutating cached velocity
			if (direction_got_updated) {
				all_cached_velocity[i] = curr_bullet_direction * all_cached_speed[i] + inherited_velocity_offset;
			}
			Vector2 velocity_delta = all_cached_velocity[i] * (real_t)delta;

		// 6. MOVEMENT PATTERNS (RELYING ON CURVES AND PATH2D)
		// Per-bullet entries are exclusively runtime-owned; the spawn-data
		// shared pattern lives in the shared slot with a per-bullet distance
		// ledger. Shared wins while active (consistent with shared curves and
		// the shared homing deque); per-bullet runs only when no shared
		// pattern is set - or once a non-repeating shared run finished.
		// Patterns (either source) steer direction, keeping the speed
		// magnitude from BulletSpeedData2D.
		bool use_shared_pattern = false;
		if (shared_pattern_curve_valid && shared_pattern_len >= 0.001 && i >= 0 && i < (int)shared_movement_pattern_distances.size()) {
			use_shared_pattern = shared_movement_pattern_repeat || shared_movement_pattern_distances[i] < shared_pattern_len;
		}
		const bool use_per_bullet_pattern = !use_shared_pattern && check_exists_bullet_movement_pattern_data(i);
		if (use_shared_pattern || use_per_bullet_pattern) {
			if (use_shared_pattern) {
				if (!advance_movement_pattern(shared_movement_pattern_curve, shared_movement_pattern_face_movement_direction, shared_movement_pattern_repeat, shared_movement_pattern_distances[i], velocity_delta, curr_bullet_direction, curr_bullet_transf)) {
					// Park the ledger at the end (degenerate curve or completed
					// run) so finished bullets skip without an extra flag.
					shared_movement_pattern_distances[i] = shared_pattern_len;
				}
			} else {
				auto &pattern = all_movement_pattern_data[i];
				const Ref<Curve2D> &curve = pattern.path_curve;
				if (curve.is_null()) {
					all_movement_pattern_data[i] = BulletMovementPatternData2D();
				} else if (!advance_movement_pattern(curve, pattern.face_movement_direction, pattern.repeat_pattern, pattern.distance_traveled, velocity_delta, curr_bullet_direction, curr_bullet_transf)) {
					all_movement_pattern_data[i] = BulletMovementPatternData2D();
				}
			}
		}

			auto &curr_bullet_origin = all_cached_instance_origin[i];

			// 7. ORBITING LOGIC (RELYING ON HOMING TARGETS)
			// A deque that ran dry unlocks the orbit: keeping a stale angle would snap
			// the bullet when the next target arrives.
			const bool orbit_vectors_ready = i >= 0 && i < (int)all_orbiting_data.size() && i < (int)all_orbiting_status.size();
			if (target_deque_used_for_orbiting == nullptr || target_deque_used_for_orbiting->empty()) {
				if (orbit_vectors_ready) {
					all_orbiting_data[i].is_locked_orbiting = false;
				}
			}
			if (is_orbiting_feature_enabled && orbit_vectors_ready && all_orbiting_status[i] && target_deque_used_for_orbiting != nullptr && !target_deque_used_for_orbiting->empty()) {
				OrbitingData *const orbiting_data = &all_orbiting_data[i];
				if (orbiting_data != nullptr) {
					const Vector2 to_target = curr_bullet_origin - homing_target_pos;
					const real_t current_dist = to_target.length();
					const bool already_locked = orbiting_data->is_locked_orbiting;

					// Track if we are ACTUALLY doing orbit movement this frame
					bool is_physically_orbiting_this_frame = false;

					// Movement Logic (Locked or Boundary Arrival)
					// DontMove freezes the bullet: no ring snap, no target tracking.
					if (already_locked && orbiting_data->direction != DontMove) {
						real_t dir_multiplier = (orbiting_data->direction == OrbitRight) ? 1.0 : (orbiting_data->direction == OrbitLeft ? -1.0 : 0.0);

						if (dir_multiplier != 0.0) {
							// Guard against tiny radius (division by zero -> inf/NaN)
							real_t safe_radius = orbiting_data->radius;
							if (safe_radius < 0.01) {
								safe_radius = 0.01;
							}
							real_t angular_speed = (all_cached_speed[i] / safe_radius) * dir_multiplier;
							orbiting_data->angle += angular_speed * delta;
						}

						Vector2 target_pos = homing_target_pos + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
						Vector2 snap_delta = target_pos - curr_bullet_origin;
						real_t max_step = all_cached_speed[i] * (real_t)delta;
						if (max_step > 0.0 && snap_delta.length_squared() > max_step * max_step) {
							snap_delta = snap_delta.normalized() * max_step;
						}
						velocity_delta = snap_delta;

						is_physically_orbiting_this_frame = true;
					}
					// Check exact frame arrival: use epsilon so low-speed / high-FPS bullets still lock (speed*delta can be <0.2px)
					// DontMove is excluded: freezing means no ring snap, no push-out, no target tracking.
					else if (orbiting_data->direction != DontMove && Math::abs(current_dist - orbiting_data->radius) < Math::max((real_t)(all_cached_speed[i] * delta), (real_t)2.0)) {
						// REACHED RADIUS - LOCK NOW
						orbiting_data->angle = to_target.angle();
						orbiting_data->is_locked_orbiting = true;

						Vector2 target_pos = homing_target_pos + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
						Vector2 snap_delta = target_pos - curr_bullet_origin;
						real_t max_step = all_cached_speed[i] * (real_t)delta;
						if (max_step > 0.0 && snap_delta.length_squared() > max_step * max_step) {
							snap_delta = snap_delta.normalized() * max_step;
						}
						velocity_delta = snap_delta;

						// We consider this frame as orbiting because we just snapped to the ring
						is_physically_orbiting_this_frame = true;
					} else if (orbiting_data->direction != DontMove && current_dist < orbiting_data->radius) {
						// SPAWNED INSIDE - PUSH OUT
						// This is technically NOT orbiting yet, it's just moving to the border
						// (skipped for DontMove: frozen bullets never move toward the ring)
						Vector2 outward_dir = (current_dist > 0.1f) ? (to_target / current_dist) : Vector2(1, 0);
						real_t next_dist = current_dist + (all_cached_speed[i] * delta);
						Vector2 target_pos = homing_target_pos + (outward_dir * next_dist);
						velocity_delta = target_pos - curr_bullet_origin;
					}

					// TEXTURE ROTATION WHEN ORBITING
					// Only rotate if the bullet is PHYSICALLY orbiting (Locked or just snapped).
					// DontMove means frozen: leave the texture alone.
				if (is_physically_orbiting_this_frame && orbiting_data->direction != DontMove) {
					Vector2 look_dir = Vector2();
					Vector2 radial_vec = (curr_bullet_origin - homing_target_pos).normalized();

					switch (orbiting_data->texture_rotation) {
						case FaceTarget:
							look_dir = -radial_vec;
							break;
						case FaceOppositeTarget:
							look_dir = radial_vec;
							break;
						case FaceOrbitingDirection:
							look_dir = (orbiting_data->direction == OrbitRight) ? Vector2(-radial_vec.y, radial_vec.x) : Vector2(radial_vec.y, -radial_vec.x);
							break;
						case FaceOppositeOrbitingDirection:
							look_dir = (orbiting_data->direction == OrbitRight) ? Vector2(radial_vec.y, -radial_vec.x) : Vector2(-radial_vec.y, radial_vec.x);
							break;
						default:
							break;
					}

						if (look_dir != Vector2()) {
							// Orbiting owns its texture rotation: it must not require the
							// homing_take_control_of_texture_rotation flag (an undocumented
							// cross-feature dependency that left Face* modes silently dead).
							rotate_to_target_preserve_interpolation(i, look_dir, false);
						}
					}
				}
			}

			// 8. TRANSFORM UPDATES
			curr_bullet_origin += velocity_delta;
			curr_bullet_transf.set_origin(curr_bullet_origin);

			auto &curr_shape_transf = all_cached_shape_transforms[i];
			auto &curr_shape_origin = all_cached_shape_origin[i];
			// Instance carries the texture rotation for rendering; physics must
			// use the logical (un-textured) rotation. When rotate_only_textures
			// is true, keep the shape at its previous logical orientation (it
			// does not follow bullet rotation), otherwise follow the bullet and
			// strip the texture-only offset.
			if (!rotate_only_textures) {
				curr_shape_transf = curr_bullet_transf;
				if (cache_texture_rotation_radians != 0.0) {
					curr_shape_transf = curr_shape_transf.rotated_local(-cache_texture_rotation_radians);
				}
			}
			Vector2 rotated_offset = Vector2(0, 0);
			if (cache_collision_shape_offset != Vector2(0, 0)) {
				rotated_offset = cache_collision_shape_offset.rotated(curr_shape_transf.get_rotation());
			}
			curr_shape_origin = curr_bullet_origin + rotated_offset;
			curr_shape_transf.set_origin(curr_shape_origin);

			physics_server->area_set_shape_transform(area, i, curr_shape_transf);
			move_bullet_attachment(velocity_delta, i);

			// 9. MOVEMENT SPEED ACCELERATION - shared sampled once before loop
			if (shared_curves_acceleration_curve_valid) {
				all_cached_speed[i] = shared_movement_speed_val;
				all_cached_velocity[i] = all_cached_direction[i] * shared_movement_speed_val + inherited_velocity_offset;
			} else if (is_per_bullet_curves_valid && per_bullet_curves_data->movement_speed_curve.is_valid()) {
				bullet_accelerate_speed_using_curve(i, delta, per_bullet_curves_data);
			} else {
				bullet_accelerate_speed(i, delta);
			}
		}
		if (!is_using_physics_interpolation) {
			batch_flush_instance_transforms();
		}

		// Handle collisions safely after all physics processing logic is done.
		// Swap into a local first: handle_bullet_collision can funnel into
		// disable_multimesh() (last bullet out), which clears the member vector.
		// Iterating the member directly would invalidate iterators mid-loop and
		// silently drop the remaining collisions of this frame.
		if (!all_collided_bullets.empty()) {
			std::vector<BulletCollisionData2D> pending_collisions;
			pending_collisions.swap(all_collided_bullets);
			for (auto &data : pending_collisions) {
				handle_bullet_collision(data.collision_type, data.bullet_index, data.collided_instance_id);
			}
		}
	}

	///////////////// ORBITING DATA METHODS

	_ALWAYS_INLINE_ void bullet_enable_orbiting(int bullet_index, real_t orbiting_radius, OrbitingDirection orbiting_direction, OrbitingTextureRotation orbiting_texture_rotation) {
		if (!validate_bullet_index(bullet_index, "bullet_enable_orbiting")) {
			return;
		}

		if (orbiting_radius < 0.01) {
			UtilityFunctions::push_error("Orbiting radius must be >= 0.01, got " + String::num(orbiting_radius) + ". Clamping to 0.01 to avoid division by zero.");
			orbiting_radius = 0.01;
		}

		if (orbiting_direction < DontMove || orbiting_direction > OrbitRight) {
			UtilityFunctions::push_error("Invalid orbiting direction " + String::num_int64(orbiting_direction) + ". Use DontMove, OrbitLeft or OrbitRight.");
			return;
		}

		if (orbiting_texture_rotation < FaceTarget || orbiting_texture_rotation > FaceOppositeOrbitingDirection) {
			UtilityFunctions::push_error("Invalid orbiting texture rotation " + String::num_int64(orbiting_texture_rotation) + ". Use FaceTarget, FaceOppositeTarget, FaceOrbitingDirection or FaceOppositeOrbitingDirection.");
			return;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 1) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " already has orbiting enabled.");
			return;
		}

		all_orbiting_data[bullet_index] = OrbitingData(orbiting_radius, orbiting_direction, orbiting_texture_rotation);
		active_orbiting_count++; // Important because it tracks whether orbiting is even used at all
		orbiting_status = 1;
	}

	_ALWAYS_INLINE_ void bullet_disable_orbiting(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_disable_orbiting")) {
			return;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " already has orbiting disabled.");
			return;
		}

		all_orbiting_data[bullet_index].is_locked_orbiting = false;
		active_orbiting_count--;
		orbiting_status = 0;
	}

	_ALWAYS_INLINE_ void bullet_set_orbiting_radius(int bullet_index, real_t new_radius) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_radius")) {
			return;
		}

		if (new_radius < 0.01) {
			UtilityFunctions::push_error("Orbiting radius must be >= 0.01, got " + String::num(new_radius) + ". Clamping to 0.01.");
			new_radius = 0.01;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting radius.");
			return;
		}

		auto &orbiting_data = all_orbiting_data[bullet_index];
		orbiting_data.is_locked_orbiting = false; // Reset lock when changing radius

		orbiting_data.radius = new_radius;
	}

	_ALWAYS_INLINE_ real_t bullet_get_orbiting_radius(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_radius")) {
			return 0.0;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting radius.");
			return 0.0;
		}

		auto &orbiting_data = all_orbiting_data[bullet_index];
		return orbiting_data.radius;
	}

	_ALWAYS_INLINE_ bool bullet_is_orbiting_enabled(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_is_orbiting_enabled")) {
			return false;
		}

		return all_orbiting_status[bullet_index] == 1;
	}

	_ALWAYS_INLINE_ void bullet_set_orbiting_texture_rotation(int bullet_index, OrbitingTextureRotation new_texture_rotation) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_texture_rotation")) {
			return;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting texture rotation.");
			return;
		}

		auto &orbiting_data = all_orbiting_data[bullet_index];
		if (new_texture_rotation < FaceTarget || new_texture_rotation > FaceOppositeOrbitingDirection) {
			UtilityFunctions::push_error("Invalid orbiting texture rotation " + String::num_int64(new_texture_rotation) + ". Use FaceTarget, FaceOppositeTarget, FaceOrbitingDirection or FaceOppositeOrbitingDirection.");
			return;
		}
		orbiting_data.texture_rotation = new_texture_rotation;
	}

	_ALWAYS_INLINE_ OrbitingTextureRotation bullet_get_orbiting_texture_rotation(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_texture_rotation")) {
			return FaceTarget;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting texture rotation.");
			return FaceTarget;
		}

		auto &orbiting_data = all_orbiting_data[bullet_index];
		return orbiting_data.texture_rotation;
	}

	_ALWAYS_INLINE_ void bullet_set_orbiting_direction(int bullet_index, OrbitingDirection new_direction) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_direction")) {
			return;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting direction.");
			return;
		}

		auto &orbiting_data = all_orbiting_data[bullet_index];
		if (new_direction < DontMove || new_direction > OrbitRight) {
			UtilityFunctions::push_error("Invalid orbiting direction " + String::num_int64(new_direction) + ". Use DontMove, OrbitLeft or OrbitRight.");
			return;
		}
		// Same as changing the radius: the old lock belongs to the old sweep, so
		// drop it instead of reversing around a stale angle mid-orbit.
		orbiting_data.is_locked_orbiting = false;
		orbiting_data.direction = new_direction;
	}

	_ALWAYS_INLINE_ OrbitingDirection bullet_get_orbiting_direction(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_direction")) {
			return DontMove;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting direction.");
			return DontMove;
		}

		auto &orbiting_data = all_orbiting_data[bullet_index];
		return orbiting_data.direction;
	}

	/////////////////

	///////////////// ORBITING DATA HELPERS

	_ALWAYS_INLINE_ void all_bullets_enable_orbiting(real_t orbiting_radius, OrbitingDirection orbiting_direction, OrbitingTextureRotation orbiting_texture_rotation, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_enable_orbiting");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_enable_orbiting(i, orbiting_radius, orbiting_direction, orbiting_texture_rotation);
		}
	}

	// Concentric-ring enable: bullet (start + k) orbits at radius_start + radius_step * k.
	// Invalid enums are rejected per bullet by bullet_enable_orbiting (already-enabled
	// bullets keep their radius with a warning instead of erroring the whole range).
	_ALWAYS_INLINE_ void all_bullets_enable_orbiting_linear(real_t radius_start, real_t radius_step, OrbitingDirection orbiting_direction = OrbitRight, OrbitingTextureRotation orbiting_texture_rotation = FaceTarget, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_enable_orbiting_linear");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_enable_orbiting(i, radius_start + radius_step * (real_t)(i - bullet_index_start), orbiting_direction, orbiting_texture_rotation);
		}
	}

	_ALWAYS_INLINE_ PackedFloat32Array all_bullets_get_orbiting_radius(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_orbiting_radius");

		PackedFloat32Array arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_get_orbiting_radius(i));
		}

		return arr;
	}

	_ALWAYS_INLINE_ TypedArray<bool> all_bullets_is_orbiting_enabled(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_is_orbiting_enabled");

		TypedArray<bool> arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_is_orbiting_enabled(i));
		}

		return arr;
	}

	_ALWAYS_INLINE_ void all_bullets_disable_orbiting(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_disable_orbiting");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_disable_orbiting(i);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_radius(real_t new_radius, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_radius");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_radius(i, new_radius);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_direction(OrbitingDirection new_direction, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_direction");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_direction(i, new_direction);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_texture_rotation(OrbitingTextureRotation new_rotation, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_texture_rotation");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_texture_rotation(i, new_rotation);
		}
	}

	////////////////

	///////////// PER BULLET HOMING DEQUE POP METHODS

	_ALWAYS_INLINE_ Variant bullet_homing_pop_front_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_front_target") || !bullet_check_has_homing_targets(bullet_index)) {
			return nullptr;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		if (all_homing_count[bullet_index] > 0) {
			--all_homing_count[bullet_index];
		}
		if (active_homing_count > 0) {
			--active_homing_count;
		}

		return queue.pop_front_target(cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ Variant bullet_homing_pop_back_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_back_target") || !bullet_check_has_homing_targets(bullet_index)) {
			return nullptr;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		if (all_homing_count[bullet_index] > 0) {
			--all_homing_count[bullet_index];
		}
		if (active_homing_count > 0) {
			--active_homing_count;
		}

		return queue.pop_back_target(cached_mouse_global_position);
	}
	/////////////////////

	//////////////// PER BULLET HOMING DEQUE PUSH METHODS

	_ALWAYS_INLINE_ bool bullet_homing_push_front_mouse_position_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_mouse_position_target")) {
			return false;
		}

		// Always refresh on push: keying freshness off the GLOBAL mouse-target counter
		// made a fresh target inherit this node's stale cache whenever any OTHER
		// multimesh held mouse targets.
		cached_mouse_global_position = get_global_mouse_position();

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_front_mouse_position_target(cached_mouse_global_position);

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_node2d_target")) {
			return false;
		}

		if (new_homing_target == nullptr) {
			UtilityFunctions::push_error("bullet_homing_push_front_node2d_target: target is null, nothing pushed.");
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_front_node2d_target(new_homing_target);

		++all_homing_count[bullet_index];
		++active_homing_count;
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_global_position_target")) {
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		// Only count the target if the deque actually stored it (a non-finite position
		// is rejected inside the deque; counting it would desync the counters and leave
		// this bullet phantom-homing an empty deque forever).
		if (!queue.push_front_global_position_target(global_position)) {
			return false;
		}

		++all_homing_count[bullet_index];
		++active_homing_count;
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_mouse_position_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_mouse_position_target")) {
			return false;
		}

		// Always refresh on push (see push_front variant for the rationale).
		cached_mouse_global_position = get_global_mouse_position();

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_back_mouse_position_target(cached_mouse_global_position);

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_node2d_target")) {
			return false;
		}

		if (new_homing_target == nullptr) {
			UtilityFunctions::push_error("bullet_homing_push_back_node2d_target: target is null, nothing pushed.");
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_back_node2d_target(new_homing_target);

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_global_position_target")) {
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		// Only count the target if the deque actually stored it (see push_front variant).
		if (!queue.push_back_global_position_target(global_position)) {
			return false;
		}

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}

	// Single-bullet Variant push (Node2D or Vector2), mirroring the
	// all_bullets_*_homing_target type branch. Returns false with an error on
	// invalid index or target type, pushing nothing.
	_ALWAYS_INLINE_ bool bullet_homing_push_back_homing_target(int bullet_index, const Variant &node2d_or_global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_homing_target")) {
			return false;
		}
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			return bullet_homing_push_back_node2d_target(bullet_index, node);
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			return bullet_homing_push_back_global_position_target(bullet_index, node2d_or_global_position);
		}
		UtilityFunctions::push_error("Invalid homing target type in bullet_homing_push_back_homing_target. Use a Node2D or Vector2.");
		return false;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_homing_target(int bullet_index, const Variant &node2d_or_global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_homing_target")) {
			return false;
		}
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			return bullet_homing_push_front_node2d_target(bullet_index, node);
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			return bullet_homing_push_front_global_position_target(bullet_index, node2d_or_global_position);
		}
		UtilityFunctions::push_error("Invalid homing target type in bullet_homing_push_front_homing_target. Use a Node2D or Vector2.");
		return false;
	}
	/////////////////////////////

	///  PER BULLET HOMING DEQUE HELPERS

	_ALWAYS_INLINE_ void bullet_clear_homing_targets(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_clear_homing_targets")) {
			return;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		auto &count = all_homing_count[bullet_index];
		active_homing_count -= count;
		if (active_homing_count < 0) {
			active_homing_count = 0;
		}
		count = 0;

		queue.clear_homing_targets(cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ Array all_bullets_pop_front_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_pop_front_target");
		Array popped_targets;

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			popped_targets.push_back(bullet_homing_pop_front_target(i)); // could push nullptr but that's expected
		}

		return popped_targets;
	}

	_ALWAYS_INLINE_ Array all_bullets_pop_back_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_pop_back_target");
		Array popped_targets;

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			popped_targets.push_back(bullet_homing_pop_back_target(i));
		}

		return popped_targets;
	}

	_ALWAYS_INLINE_ void all_bullets_push_back_mouse_position_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_mouse_position_target");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_homing_push_back_mouse_position_target(i);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_mouse_position_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_mouse_position_target");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_homing_push_front_mouse_position_target(i);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_back_homing_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_homing_target");
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				bullet_homing_push_back_node2d_target(i, node);
			}
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			Vector2 global_pos = node2d_or_global_position;
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				bullet_homing_push_back_global_position_target(i, global_pos);
			}
		} else {
			UtilityFunctions::push_error("Invalid homing target type in all_bullets_push_back_homing_target");
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_homing_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_homing_target");
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				bullet_homing_push_front_node2d_target(i, node);
			}
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			Vector2 global_pos = node2d_or_global_position;
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				bullet_homing_push_front_global_position_target(i, global_pos);
			}
		} else {
			UtilityFunctions::push_error("Invalid homing target type in all_bullets_push_front_homing_target");
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_back_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_homing_targets_array");
		for (const Variant &target : node2ds_or_global_positions_array) {
			all_bullets_push_back_homing_target(target, bullet_index_start, bullet_index_end_inclusive);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_homing_targets_array");
		for (const Variant &target : node2ds_or_global_positions_array) {
			all_bullets_push_front_homing_target(target, bullet_index_start, bullet_index_end_inclusive);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_new_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_new_target");

		// Validate BEFORE clearing: an invalid target must not wipe the user's targets.
		const bool is_node2d = Object::cast_to<Node2D>(node2d_or_global_position) != nullptr;
		if (!is_node2d && node2d_or_global_position.get_type() != Variant::VECTOR2) {
			UtilityFunctions::push_error("Invalid homing target type in all_bullets_replace_homing_targets_with_new_target. Use a Node2D or Vector2. Nothing was changed.");
			return;
		}

		all_bullets_clear_homing_targets(bullet_index_start, bullet_index_end_inclusive);
		all_bullets_push_back_homing_target(node2d_or_global_position, bullet_index_start, bullet_index_end_inclusive);
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_new_target_array");

		// Validate every entry BEFORE clearing so a bad entry can't wipe the user's
		// targets (all-or-nothing replace).
		for (int k = 0; k < node2ds_or_global_positions_array.size(); ++k) {
			const Variant &target = node2ds_or_global_positions_array[k];
			if (Object::cast_to<Node2D>(target) == nullptr && target.get_type() != Variant::VECTOR2) {
				UtilityFunctions::push_error("Invalid homing target type in all_bullets_replace_homing_targets_with_new_target_array at array index " + String::num_int64(k) + ". Use Node2D or Vector2 entries. Nothing was changed.");
				return;
			}
		}

		all_bullets_clear_homing_targets(bullet_index_start, bullet_index_end_inclusive);
		all_bullets_push_back_homing_targets_array(node2ds_or_global_positions_array, bullet_index_start, bullet_index_end_inclusive);
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_mouse(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_mouse");

		// Cache once for the whole loop
		cached_mouse_global_position = get_global_mouse_position();

		all_bullets_clear_homing_targets(bullet_index_start, bullet_index_end_inclusive);
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			all_bullet_homing_targets[i].push_back_mouse_position_target(cached_mouse_global_position);
			++all_homing_count[i];
			++active_homing_count;
		}
	}

	_ALWAYS_INLINE_ void all_bullets_assign_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_assign_homing_targets_array");

		const int range_size = bullet_index_end_inclusive - bullet_index_start + 1;
		if (node2ds_or_global_positions_array.size() != range_size) {
			UtilityFunctions::push_error("all_bullets_assign_homing_targets_array: targets array size must match the bullet range size. Nothing pushed.");
			return;
		}

		// Validate every element first so a bad entry can't leave a half-assigned range behind.
		for (int k = 0; k < range_size; ++k) {
			const Variant &target = node2ds_or_global_positions_array[k];
			if (Object::cast_to<Node2D>(target) == nullptr && target.get_type() != Variant::VECTOR2) {
				UtilityFunctions::push_error("Invalid homing target type in all_bullets_assign_homing_targets_array at array index " + String::num_int64(k) + ". Use Node2D or Vector2 entries. Nothing pushed.");
				return;
			}
			if (target.get_type() == Variant::VECTOR2 && !Vector2(target).is_finite()) {
				UtilityFunctions::push_error("Non-finite homing target in all_bullets_assign_homing_targets_array at array index " + String::num_int64(k) + ". Nothing pushed.");
				return;
			}
		}

		// Element i of the array goes to bullet (start + i), pushed to the back.
		for (int k = 0; k < range_size; ++k) {
			bullet_homing_push_back_homing_target(bullet_index_start + k, node2ds_or_global_positions_array[k]);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_clear_homing_targets(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_clear_homing_targets");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_clear_homing_targets(i);
		}
	}

	_ALWAYS_INLINE_ int bullet_homing_check_targets_amount(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_homing_check_targets_amount")) {
			return 0;
		}

		return all_bullet_homing_targets[bullet_index].get_homing_targets_amount();
	}

	_ALWAYS_INLINE_ bool bullet_check_has_homing_targets(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_check_has_homing_targets")) {
			return false;
		}

		return all_bullet_homing_targets[bullet_index].has_homing_targets();
	}

	_ALWAYS_INLINE_ HomingType bullet_homing_check_current_target_type(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_homing_check_current_target_type")) {
			return HomingType::NotHoming;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		return queue.get_current_target_type();
	}

	_ALWAYS_INLINE_ Variant bullet_get_current_homing_target(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_current_homing_target") || !bullet_check_has_homing_targets(bullet_index)) {
			return nullptr;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		return queue.get_current_homing_target();
	}

	//////////////////////////////

	// SHARED BULLET HOMING DEQUE POP METHODS

	_ALWAYS_INLINE_ Variant shared_homing_deque_pop_front_target() {
		Variant popped = shared_homing_deque.pop_front_target(cached_mouse_global_position);
		// The front changed: re-arm every bullet so the next target can fire its own
		// reached signal (per-bullet semantics for the shared deque).
		reset_shared_homing_reached_state();
		return popped;
	}

	_ALWAYS_INLINE_ Variant shared_homing_deque_pop_back_target() {
		Variant popped = shared_homing_deque.pop_back_target(cached_mouse_global_position);
		if (shared_homing_deque.empty()) {
			// The sole (= front) element is gone: drop the dangling front
			// pointers so the next push starts every bullet fresh.
			reset_shared_homing_reached_state();
		}
		return popped;
	}

	// SHARED BULLET HOMING DEQUE PUSH METHODS
	// Push-front always swaps the front target, so every bullet is re-armed for
	// it. Push-back only re-arms when the deque was empty (that push creates
	// the front); otherwise the front is unchanged and fired flags must stay.

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_mouse_position_target() {
		// Always refresh on push (see bullet_homing_push_front_mouse_position_target).
		cached_mouse_global_position = get_global_mouse_position();

		shared_homing_deque.push_front_mouse_position_target(cached_mouse_global_position);
		reset_shared_homing_reached_state();
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_node2d_target(Node2D *new_homing_target) {
		const int before = shared_homing_deque.get_homing_targets_amount();
		shared_homing_deque.push_front_node2d_target(new_homing_target);
		if (shared_homing_deque.get_homing_targets_amount() != before) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_global_position_target(const Vector2 &global_position) {
		if (shared_homing_deque.push_front_global_position_target(global_position)) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_mouse_position_target() {
		// Always refresh on push (see bullet_homing_push_front_mouse_position_target).
		cached_mouse_global_position = get_global_mouse_position();

		const bool was_empty = shared_homing_deque.empty();
		shared_homing_deque.push_back_mouse_position_target(cached_mouse_global_position);
		if (was_empty) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_node2d_target(Node2D *new_homing_target) {
		const bool was_empty = shared_homing_deque.empty();
		shared_homing_deque.push_back_node2d_target(new_homing_target);
		if (was_empty && !shared_homing_deque.empty()) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_global_position_target(const Vector2 &global_position) {
		const bool was_empty = shared_homing_deque.empty();
		if (shared_homing_deque.push_back_global_position_target(global_position) && was_empty) {
			reset_shared_homing_reached_state();
		}
	}

	////////////////////////////////////

	/// SHARED BULLET HOMING DEQUE HELPER METHODS

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_homing_targets_array(const Array &node2ds_or_global_positions_array) {
		for (const Variant &target : node2ds_or_global_positions_array) {
			Node2D *node2d_target = Object::cast_to<Node2D>(target);

			if (node2d_target) {
				shared_homing_deque_push_back_node2d_target(node2d_target);
			} else if (target.get_type() == Variant::VECTOR2) {
				shared_homing_deque_push_back_global_position_target(target);
			} else {
				UtilityFunctions::push_error("Invalid homing target type in shared_homing_deque_push_back_homing_targets_array. Use Node2D or Vector2 entries.");
			}
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_homing_targets_array(const Array &node2ds_or_global_positions_array) {
		for (const Variant &target : node2ds_or_global_positions_array) {
			Node2D *node2d_target = Object::cast_to<Node2D>(target);

			if (node2d_target) {
				shared_homing_deque_push_front_node2d_target(node2d_target);
			} else if (target.get_type() == Variant::VECTOR2) {
				shared_homing_deque_push_front_global_position_target(target);
			} else {
				UtilityFunctions::push_error("Invalid homing target type in shared_homing_deque_push_front_homing_targets_array. Use Node2D or Vector2 entries.");
			}
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_clear_homing_targets() {
		shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
		reset_shared_homing_reached_state();
	}

	_ALWAYS_INLINE_ void shared_homing_deque_replace_homing_targets_with_new_target(const Variant &node2d_or_global_position) {
		Node2D *node2d_target = Object::cast_to<Node2D>(node2d_or_global_position);

		if (node2d_target) {
			shared_homing_deque_clear_homing_targets();
			shared_homing_deque_push_back_node2d_target(node2d_target);
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			shared_homing_deque_clear_homing_targets();
			shared_homing_deque_push_back_global_position_target(node2d_or_global_position);
		} else {
			UtilityFunctions::push_error("Invalid type passed to shared_homing_deque_replace_homing_targets_with_new_target");
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array) {
		shared_homing_deque_clear_homing_targets();

		for (auto &target : node2ds_or_global_positions_array) {
			Node2D *node2d_target = Object::cast_to<Node2D>(target);

			if (node2d_target) {
				shared_homing_deque_push_back_node2d_target(node2d_target);
			} else if (target.get_type() == Variant::VECTOR2) {
				shared_homing_deque_push_back_global_position_target(target);
			} else {
				UtilityFunctions::push_error("Invalid type passed to shared_homing_deque_replace_homing_targets_with_new_target_array");
			}
		}
	}

	_ALWAYS_INLINE_ int shared_homing_deque_check_homing_targets_amount() const {
		return shared_homing_deque.get_homing_targets_amount();
	}

	_ALWAYS_INLINE_ bool shared_homing_deque_check_has_homing_targets() const {
		return shared_homing_deque.has_homing_targets();
	}

	_ALWAYS_INLINE_ HomingType shared_homing_deque_check_current_target_type() const {
		return shared_homing_deque.get_current_target_type();
	}

	_ALWAYS_INLINE_ Variant shared_homing_deque_get_current_homing_target() const {
		return shared_homing_deque.get_current_homing_target();
	}

	/////////////////////////

	// Teleports a bullet to a new global position
	_ALWAYS_INLINE_ void teleport_bullet(int bullet_index, const Vector2 &new_global_pos) {
		if (!validate_bullet_index(bullet_index, "teleport_bullet")) {
			return;
		}

		// Non-finite input would permanently poison the cached transform, the multimesh
		// instance and the physics shape with no recovery API - reject like bullet_set_velocity does.
		if (!new_global_pos.is_finite()) {
			UtilityFunctions::push_error("teleport_bullet: new_global_pos must be finite (NaN/Inf is rejected).");
			return;
		}

		if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
			return;
		}

		auto &curr_bullet_transf = all_cached_instance_transforms[bullet_index];
		auto &curr_bullet_origin = all_cached_instance_origin[bullet_index];

		const Vector2 origin_delta = new_global_pos - curr_bullet_origin;
		curr_bullet_origin = new_global_pos;
		curr_bullet_transf.set_origin(curr_bullet_origin);

		sync_shape_transform_from_instance(bullet_index, curr_bullet_transf);

		// Instantly apply the updated transforms
		if (all_bullets_enabled_set.contains(bullet_index)) {
			multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(curr_bullet_transf));
		}

		if (bullet_index < (int)attachments.size() && bullet_index < (int)attachment_transforms.size() && bullet_index < (int)attachment_stick_relative_to_bullet.size() && attachments[bullet_index]) {
			BulletAttachment2D *attachment_instance = attachments[bullet_index];

			// Same carry policy as set_bullet_transform: stick-relative attachments
			// recompute from the new transform, non-stick ones shift by the jump.
			Transform2D att_global_transf;
			if (attachment_stick_relative_to_bullet[bullet_index]) {
				att_global_transf = calculate_attachment_global_transf(bullet_index, curr_bullet_transf);
			} else {
				att_global_transf = attachment_transforms[bullet_index].translated(origin_delta);
			}

			// Update the cache
			attachment_transforms[bullet_index] = att_global_transf;

			// Move the actual Node
			attachment_instance->set_global_transform(att_global_transf);

			// Reset Godot's internal engine interpolation
			attachment_instance->reset_physics_interpolation();
		}

		// Reset physics interpolation data
		update_bullet_previous_transform_for_interpolation(bullet_index);
	}

	// Shifts a bullet's position by a certain amount
	_ALWAYS_INLINE_ void teleport_shift_bullet(int bullet_index, const Vector2 &shift_amount) {
		if (!validate_bullet_index(bullet_index, "teleport_shift_bullet")) {
			return;
		}

		if (!shift_amount.is_finite()) {
			UtilityFunctions::push_error("teleport_shift_bullet: shift_amount must be finite (NaN/Inf is rejected).");
			return;
		}

		if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
			return;
		}

		auto &curr_bullet_transf = all_cached_instance_transforms[bullet_index];
		auto &curr_bullet_origin = all_cached_instance_origin[bullet_index];

		curr_bullet_origin += shift_amount;
		curr_bullet_transf.set_origin(curr_bullet_origin);

		sync_shape_transform_from_instance(bullet_index, curr_bullet_transf);

		// Instantly apply the updated transforms
		if (all_bullets_enabled_set.contains(bullet_index)) {
			multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(curr_bullet_transf));
		}

		if (bullet_index < (int)attachments.size() && bullet_index < (int)attachment_transforms.size() && bullet_index < (int)attachment_stick_relative_to_bullet.size() && attachments[bullet_index]) {
			BulletAttachment2D *attachment_instance = attachments[bullet_index];

			// Same carry policy as set_bullet_transform: stick-relative attachments
			// recompute from the new transform, non-stick ones shift by the jump.
			Transform2D att_global_transf;
			if (attachment_stick_relative_to_bullet[bullet_index]) {
				att_global_transf = calculate_attachment_global_transf(bullet_index, curr_bullet_transf);
			} else {
				att_global_transf = attachment_transforms[bullet_index].translated(shift_amount);
			}

			// Update the cache
			attachment_transforms[bullet_index] = att_global_transf;

			// Move the actual Node
			attachment_instance->set_global_transform(att_global_transf);

			// Reset Godot's internal engine interpolation
			attachment_instance->reset_physics_interpolation();
		}

		// Reset physics interpolation data
		update_bullet_previous_transform_for_interpolation(bullet_index);
	}

	_ALWAYS_INLINE_ void teleport_shift_all_bullets(const Vector2 &shift_amount, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "teleport_shift_all_bullets");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			teleport_shift_bullet(i, shift_amount);
		}
	}

	// Sets a bullet's velocity directly (wind, knockback, split inheritance).
	// Decomposes into direction + speed so the per-tick integrator
	// (velocity = direction * speed + inherited offset) keeps producing exactly
	// this velocity. max_speed is raised when below the new speed so the next
	// tick doesn't snap it back down. Non-finite input is rejected.
	_ALWAYS_INLINE_ void bullet_set_velocity(int bullet_index, const Vector2 &new_velocity) {
		if (!validate_bullet_index(bullet_index, "bullet_set_velocity")) {
			return;
		}

		if (!new_velocity.is_finite()) {
			UtilityFunctions::push_error("bullet_set_velocity: new_velocity must be finite.");
			return;
		}

		if (shared_bullet_curves_data.is_valid() && shared_bullet_curves_data->movement_speed_curve.is_valid()) {
			UtilityFunctions::push_warning("You are trying to set bullet velocity directly while having a movement speed curve assigned to the shared curves data. The curve will override any direct velocity changes. Set the curve to null first if you want to set velocity directly.");
			return;
		}

		BulletCurvesData2D *curves_data = find_bullet_curves_data_ptr(bullet_index);

		if (curves_data != nullptr && curves_data->movement_speed_curve.is_valid()) {
			UtilityFunctions::push_warning("You are trying to set bullet velocity directly while having a movement speed curve assigned as an individual bullet curves data. The curve will override any direct velocity changes. Set the curve to null first if you want to set velocity directly.");
			return;
		}

		const Vector2 without_offset = new_velocity - inherited_velocity_offset;
		const real_t new_speed = without_offset.length();

		// Block bullets keep a single shared entry; map any index to 0 like the
		// getters do instead of writing out of bounds.
		const int eff = (all_cached_direction.size() == 1) ? 0 : bullet_index;
		if (eff < 0 || eff >= (int)all_cached_direction.size() || eff >= (int)all_cached_speed.size() || eff >= (int)all_cached_max_speed.size() || eff >= (int)all_cached_velocity.size()) {
			return;
		}
		if (new_speed > 0.0001) {
			all_cached_direction[eff] = without_offset / new_speed;
		}

		all_cached_speed[eff] = new_speed;
		if (all_cached_max_speed[eff] < new_speed) {
			all_cached_max_speed[eff] = new_speed;
		}
		all_cached_velocity[eff] = all_cached_direction[eff] * new_speed + inherited_velocity_offset;
	}

	_ALWAYS_INLINE_ void all_bullets_set_velocity(const Vector2 &new_velocity, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_velocity");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_velocity(i, new_velocity);
		}
	}

	_ALWAYS_INLINE_ TypedArray<Vector2> all_bullets_get_velocity(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_velocity");

		TypedArray<Vector2> arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			int eff = ((int)all_cached_velocity.size() == 1) ? 0 : i;
			if (eff < 0 || eff >= (int)all_cached_velocity.size()) {
				arr.push_back(Vector2());
				continue;
			}
			arr.push_back(all_cached_velocity[eff]);
		}

		return arr;
	}

	// Property getters and setters
	real_t get_homing_smoothing() const { return homing_smoothing; }
	void set_homing_smoothing(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("homing_smoothing must be a finite value >= 0 (0 snaps instantly).");
			return;
		}
		homing_smoothing = value;
	}
	real_t get_homing_update_interval() const { return homing_update_interval; }
	void set_homing_update_interval(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("homing_update_interval must be a finite value >= 0 (0 refreshes every tick).");
			return;
		}
		homing_update_interval = value;
	}
	bool get_homing_take_control_of_texture_rotation() const { return homing_take_control_of_texture_rotation; }
	void set_homing_take_control_of_texture_rotation(bool value) { homing_take_control_of_texture_rotation = value; }
	bool get_bullet_homing_auto_pop_after_target_reached() const { return bullet_homing_auto_pop_after_target_reached; }
	void set_bullet_homing_auto_pop_after_target_reached(bool value) { bullet_homing_auto_pop_after_target_reached = value; }
	real_t get_homing_distance_before_reached() const { return homing_distance_before_reached; }
	void set_homing_distance_before_reached(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("homing_distance_before_reached must be a finite value >= 0.");
			return;
		}
		homing_distance_before_reached = value;
	}
	bool get_shared_homing_deque_auto_pop_after_target_reached() const { return shared_homing_deque_auto_pop_after_target_reached; }
	void set_shared_homing_deque_auto_pop_after_target_reached(bool value) { shared_homing_deque_auto_pop_after_target_reached = value; }

	// Per-bullet turn agility. Setting any value enables per-bullet mode, after
	// which update_homing ignores the shared homing_smoothing. Same validation
	// as the shared setter. Reading returns the effective value per bullet.
	real_t bullet_get_homing_smoothing(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_homing_smoothing")) {
			return 0.0;
		}
		if (use_per_bullet_homing_smoothing && bullet_index >= 0 && bullet_index < (int)all_bullet_homing_smoothing.size()) {
			return all_bullet_homing_smoothing[bullet_index];
		}
		return homing_smoothing;
	}
	void bullet_set_homing_smoothing(int bullet_index, real_t value) {
		if (!validate_bullet_index(bullet_index, "bullet_set_homing_smoothing")) {
			return;
		}
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("bullet_set_homing_smoothing: value must be finite and >= 0 (0 snaps instantly).");
			return;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_bullet_homing_smoothing.size()) {
			UtilityFunctions::push_error("bullet_set_homing_smoothing: per-bullet smoothing storage is not set up for this multimesh.");
			return;
		}
		// First per-bullet write seeds every bullet with the shared value so
		// untouched bullets keep steering exactly as before.
		if (!use_per_bullet_homing_smoothing) {
			all_bullet_homing_smoothing.assign(all_bullet_homing_smoothing.size(), homing_smoothing);
		}
		all_bullet_homing_smoothing[bullet_index] = value;
		use_per_bullet_homing_smoothing = true;
	}
	void all_bullets_set_homing_smoothing(real_t value, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_homing_smoothing");
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("all_bullets_set_homing_smoothing: value must be finite and >= 0 (0 snaps instantly).");
			return;
		}
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (i < 0 || i >= (int)all_bullet_homing_smoothing.size()) {
				UtilityFunctions::push_error("all_bullets_set_homing_smoothing: per-bullet smoothing storage is not set up for this multimesh.");
				return;
			}
		}
		// First per-bullet write seeds every bullet with the shared value so
		// untouched bullets keep steering exactly as before.
		if (!use_per_bullet_homing_smoothing) {
			all_bullet_homing_smoothing.assign(all_bullet_homing_smoothing.size(), homing_smoothing);
		}
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			all_bullet_homing_smoothing[i] = value;
		}
		use_per_bullet_homing_smoothing = true;
	}

	// Virtual methods
	void set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data);
	virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_enable_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_disable_logic() override final;

	// Resolves the spawn data's shared movement pattern Path2D and applies its
	// Curve2D to every bullet through the existing helpers. Empty path = off.
	void apply_shared_movement_pattern_from_data(const DirectionalBulletsData2D &directional_data);

	// SHARED MOVEMENT PATTERN RUNTIME API (spawn-data equivalent, editable live
	// on the instance; per-bullet helpers stay in the base class). While a
	// shared pattern is set it takes precedence over per-bullet patterns;
	// clearing it (null curve) hands control back to them.
	Ref<Curve2D> get_shared_movement_pattern_curve() const { return shared_movement_pattern_curve; }
	void set_shared_movement_pattern_curve(const Ref<Curve2D> &new_curve) {
		shared_movement_pattern_curve = new_curve;
		// A fresh pattern starts every bullet at distance 0. A null curve
		// removes the feature: flags go back to defaults, matching the
		// spawn-data null handling.
		if (new_curve.is_null()) {
			shared_movement_pattern_face_movement_direction = false;
			shared_movement_pattern_repeat = true;
		}
		shared_movement_pattern_distances.assign(amount_bullets, 0.0);
	}

	bool get_shared_movement_pattern_face_movement_direction() const { return shared_movement_pattern_face_movement_direction; }
	void set_shared_movement_pattern_face_movement_direction(bool value) { shared_movement_pattern_face_movement_direction = value; }

	bool get_shared_movement_pattern_repeat() const { return shared_movement_pattern_repeat; }
	void set_shared_movement_pattern_repeat(bool value) { shared_movement_pattern_repeat = value; }

	bool has_shared_movement_pattern() const { return shared_movement_pattern_curve.is_valid(); }
	void remove_shared_movement_pattern() { set_shared_movement_pattern_curve(Ref<Curve2D>()); }

	// Teardown hook: drop every homing target (per-bullet + shared) through the same
	// clear helpers the enable path uses, so the global mouse-target counter can't leak
	// when a multimesh dies holding mouse targets.
	virtual void clear_homing_state_for_teardown() override {
		for (auto &queue : all_bullet_homing_targets) {
			queue.clear_homing_targets(cached_mouse_global_position);
		}
		all_homing_count.assign(all_homing_count.size(), 0);
		active_homing_count = 0;
		shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
		reset_shared_homing_reached_state();
		// A pooled instance must not carry runtime homing/orbit setup into the
		// next owner. Mirrors custom_additional_enable_logic so an enable_bullet()
		// wake (which skips that path) starts from the same blank state.
		all_bullet_homing_smoothing.assign(all_bullet_homing_smoothing.size(), 0.0);
		use_per_bullet_homing_smoothing = false;
		for (auto &o : all_orbiting_data) {
			o.is_locked_orbiting = false;
		}
		all_orbiting_status.assign(all_orbiting_status.size(), 0);
		active_orbiting_count = 0;
		homing_update_interval = 0.0;
		homing_update_timer = 0.0;
		homing_smoothing = 0.0;
		homing_take_control_of_texture_rotation = false;
		homing_distance_before_reached = 5.0;
		bullet_homing_auto_pop_after_target_reached = false;
		shared_homing_deque_auto_pop_after_target_reached = false;
		adjust_direction_based_on_rotation = false;
		homing_inert_warning_issued = false;
		cached_mouse_global_position = Vector2(0, 0);
	}

	// Single-bullet hook called from disable_bullet(): the tick only trims
	// active bullets, so a partially disabled multimesh would otherwise leak
	// this bullet's targets (and the global mouse counter) until full teardown.
	virtual void on_bullet_disabled(int bullet_index) override {
		if (bullet_index < 0 || bullet_index >= (int)all_bullet_homing_targets.size()) {
			return;
		}
		auto &queue = all_bullet_homing_targets[bullet_index];
		queue.clear_homing_targets(cached_mouse_global_position);
		if (bullet_index >= 0 && bullet_index < (int)all_homing_count.size()) {
			active_homing_count -= all_homing_count[bullet_index];
			if (active_homing_count < 0) {
				active_homing_count = 0;
			}
			all_homing_count[bullet_index] = 0;
		}
		// Drop this bullet's shared-deque reached state so a re-enabled bullet can
		// emit again for the current front target.
		if (bullet_index >= 0 && bullet_index < (int)all_shared_homing_reached.size()) {
			all_shared_homing_reached[bullet_index] = SharedHomingReachedState();
		}
		if (bullet_index >= 0 && bullet_index < (int)all_orbiting_status.size() && all_orbiting_status[bullet_index]) {
			all_orbiting_status[bullet_index] = 0;
			--active_orbiting_count;
			if (active_orbiting_count < 0) {
				active_orbiting_count = 0;
			}
		}
		if (bullet_index >= 0 && bullet_index < (int)all_orbiting_data.size()) {
			all_orbiting_data[bullet_index].is_locked_orbiting = false;
		}
	}

protected:
	// Updates homing behavior for a bullet
	_ALWAYS_INLINE_ void update_homing(HomingTargetDeque &homing_deque, int bullet_index, double delta, Vector2 &bullet_pos, Vector2 &target_pos) {
		if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_origin.size() || bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
			return;
		}
		// Get the front target's cached position
		target_pos = homing_deque.get_cached_front_target_global_position();
		if (!target_pos.is_finite()) {
			return;
		}

		bullet_pos = all_cached_instance_origin[bullet_index];
		Vector2 diff = target_pos - bullet_pos;

		real_t dist_sq = diff.length_squared();
		if (dist_sq <= 0.0) {
			return;
		}

		real_t max_turn = homing_smoothing * delta;
		if (use_per_bullet_homing_smoothing && bullet_index >= 0 && bullet_index < (int)all_bullet_homing_smoothing.size()) {
			max_turn = all_bullet_homing_smoothing[bullet_index] * delta;
		}
		if (max_turn < 0.0) {
			max_turn = 0.0;
		}

		Vector2 &current_direction = all_cached_direction[bullet_index];

		auto &curr_transf = all_cached_instance_transforms[bullet_index];
		// If rotation is controlled via movement pattern (per-bullet or shared)
		// or rotation data, just set direction directly toward target
		if (check_exists_bullet_movement_pattern_data(bullet_index) || shared_movement_pattern_curve.is_valid() || is_rotation_data_active) {
			current_direction = diff.normalized();
		} else { // Otherwise use smoothing to rotate toward target
			// Rotate toward target with smoothing
			rotate_to_target(bullet_index, diff, max_turn);

			// Logical direction strips the texture rotation used for rendering
			// (rotate_to_target aims the visual forward; movement must not
			// inherit that offset or bullets head off-target every tick).
			current_direction = curr_transf[0].rotated(-cache_texture_rotation_radians).normalized();
		}
	}

	// Rotates bullet to face target with smoothing (boundary-agnostic version).
	// require_homing_flag: the homing feature only rotates the texture when the user
	// opted in via homing_take_control_of_texture_rotation; orbiting's Face* modes
	// own their texture rotation unconditionally and pass false.
	_ALWAYS_INLINE_ void rotate_to_target(int bullet_index, const Vector2 &diff, real_t max_turn, bool require_homing_flag = true) {
		if ((require_homing_flag && !homing_take_control_of_texture_rotation) || diff.length_squared() <= 0.0) {
			return;
		}
		if (!diff.is_finite() || !Math::is_finite(max_turn)) {
			return;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
			return;
		}

		// Normalize diff once for facing direction
		real_t dist_to_target = diff.length();
		Vector2 face_dir = diff / dist_to_target;

		// Adjust for texture offset: target_forward is the transform's [0] dir that makes visual face target
		Vector2 target_forward = face_dir.rotated(-cache_texture_rotation_radians);

		// Current forward from transform
		Vector2 current_forward = all_cached_instance_transforms[bullet_index][0].normalized();

		// Direct delta_rot via cross/dot (one atan2, no get_rotation())
		real_t dot = current_forward.dot(target_forward);
		real_t cross = current_forward.x * target_forward.y - current_forward.y * target_forward.x;
		real_t delta_rot = Math::atan2(cross, dot);
		normalize_angle(delta_rot);

		bool use_smoothing = max_turn > 0.0; // Hoist for clamp

		// Apply smoothing clamp
		if (use_smoothing) {
			delta_rot = Math::clamp(delta_rot, -max_turn, max_turn);
		}

		// Rotate locally
		rotate_transform_locally(all_cached_instance_transforms[bullet_index], delta_rot);

		// Snap only (no smoothing): collapse the rotation lerp so the visual
		// doesn't trail. Orbiting calls here every frame with max_turn 0 and
		// needs continuous interpolation, so it snapshots/restores around the
		// call (see orbit block) instead of resetting here.
		if (!use_smoothing) {
			update_bullet_previous_transform_for_interpolation(bullet_index);
		}
	}

	// Orbit-safe wrapper: rotate without collapsing the interpolation cache,
	// so orbiters keep smooth rotation instead of jittering every frame.
	_ALWAYS_INLINE_ void rotate_to_target_preserve_interpolation(int bullet_index, const Vector2 &diff, bool require_homing_flag = true) {
		if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_previous_instance_transf.size()) {
			return;
		}
		const Transform2D prev = all_cached_instance_transforms[bullet_index];
		rotate_to_target(bullet_index, diff, 0.0, require_homing_flag);
		all_previous_instance_transf[bullet_index] = prev;
	}

	// Updates bullet rotation based on rotation speed
	_ALWAYS_INLINE_ void update_rotation(int bullet_index, double delta) {
		real_t cache_rotation_speed = all_rotation_speed[bullet_index];
		real_t rot_delta = cache_rotation_speed * (real_t)delta;

		// Apply rotation if active or speed > 0
		if (cache_rotation_speed != 0.0f) {
			bool max_reached = cache_rotation_speed >= all_max_rotation_speed[bullet_index];

			if (!(max_reached && stop_rotation_when_max_reached)) {
				rotate_transform_locally(all_cached_instance_transforms[bullet_index], rot_delta);
			}
		}
	}

	// Updates bullet rotation based on rotation speed using a curve
	_ALWAYS_INLINE_ void update_rotation_using_curve(int bullet_index, double delta) {
		real_t cache_rotation_speed = all_rotation_speed[bullet_index];
		real_t rot_delta = cache_rotation_speed * (real_t)delta;

		rotate_transform_locally(all_cached_instance_transforms[bullet_index], rot_delta);
	}

	// Per-bullet reached tracking for the SHARED homing deque. HomingTarget's own
	// has_bullet_reached_target flag would let only the FIRST bullet in range emit
	// bullet_homing_target_reached, but the signal carries bullet_index and auto-pop
	// is a per-bullet feature. Each bullet tracks which front target it already fired
	// for; popping the front re-arms everyone (reset_shared_homing_reached_state).
	// Pointer is only COMPARED (deque references stay valid while the element is
	// stored), never dereferenced.
	struct SharedHomingReachedState {
		const void *front_target = nullptr;
		bool fired = false;
	};
	std::vector<SharedHomingReachedState> all_shared_homing_reached;

	_ALWAYS_INLINE_ void reset_shared_homing_reached_state() {
		for (SharedHomingReachedState &state : all_shared_homing_reached) {
			state.front_target = nullptr;
			state.fired = false;
		}
	}

	// Coalesced auto-pop: set while a deferred shared-deque pop is in flight so N
	// bullets reaching in one tick queue exactly one pop instead of draining the deque.
	bool shared_auto_pop_queued = false;

	_ALWAYS_INLINE_ void _do_shared_auto_pop_front_target() {
		shared_auto_pop_queued = false;
		shared_homing_deque_pop_front_target();
	}

	_ALWAYS_INLINE_ void try_to_emit_bullet_homing_target_reached_signal(HomingTargetDeque &homing_deque, bool is_using_shared_homing_deque, int bullet_index, const Vector2 &bullet_pos, const Vector2 &target_pos, double delta) {
		if (homing_deque.empty()) {
			return;
		}
		// Reached check against the predicted post-move position: at high speed a bullet
		// can tunnel past the threshold within one tick and never fire on pre-move pos.
		Vector2 check_pos = bullet_pos;
		if (delta > 0.0 && bullet_index >= 0 && bullet_index < (int)all_cached_velocity.size()) {
			check_pos += all_cached_velocity[bullet_index] * (real_t)delta;
		}
		Vector2 post_to_target = target_pos - check_pos;
		real_t post_dist_sq = post_to_target.length_squared();
		real_t threshold_sq = homing_distance_before_reached * homing_distance_before_reached;
		if (post_dist_sq <= threshold_sq) { // Fully squared for perf

			HomingTarget &target = homing_deque.front();

			// Decide whether THIS bullet may fire:
			// - shared deque: per-bullet state keyed on the current front target
			// - per-bullet deque: the target's own flag (each bullet owns its targets)
			bool fire_for_this_bullet = false;
			if (is_using_shared_homing_deque) {
				if (bullet_index >= 0 && bullet_index < (int)all_shared_homing_reached.size()) {
					SharedHomingReachedState &state = all_shared_homing_reached[bullet_index];
					fire_for_this_bullet = (state.front_target != (const void *)&target) || !state.fired;
					state.front_target = &target;
					state.fired = true;
				}
			} else {
				fire_for_this_bullet = !target.has_bullet_reached_target;
				target.has_bullet_reached_target = true;
			}

			// Ensure that the signal is emitted only ONCE per bullet per target
			if (fire_for_this_bullet) {
				switch (target.type) {
					case GlobalPositionTarget:
						call_deferred("emit_signal", "bullet_homing_target_reached", this, bullet_index, nullptr, target_pos);
						break;
					case Node2DTarget: {
						auto &target_data = target.node2d_target_data;

						// In case the target instance is freed - will still emit the signal, but with a nullptr as the target
						if (!homing_deque.is_homing_target_valid(target_data.target, target_data.cached_valid_instance_id)) {
							call_deferred("emit_signal", "bullet_homing_target_reached", this, bullet_index, nullptr, target_pos);
							break;
						}

						call_deferred("emit_signal", "bullet_homing_target_reached", this, bullet_index, target_data.target, target_pos);
						break;
					}
					case NotHoming:
						break;
					case MousePositionTarget:
						call_deferred("emit_signal", "bullet_homing_target_reached", this, bullet_index, nullptr, target_pos);
						break;
				}

				// Pop the front target automatically if that's what the user wants.
				// Shared deque: N bullets reaching in the same tick must queue exactly
				// ONE deferred pop, otherwise the storm would drain every target the
				// user pushed. Per-bullet deques pop their own deque per bullet - no
				// storm there.
				if (is_using_shared_homing_deque) {
					if (shared_homing_deque_auto_pop_after_target_reached && !shared_auto_pop_queued) {
						shared_auto_pop_queued = true;
						call_deferred("_do_shared_auto_pop_front_target");
					}
				} else {
					if (bullet_homing_auto_pop_after_target_reached) {
						call_deferred("bullet_homing_pop_front_target", bullet_index);
					}
				}
			}
		}
	}

	// Normalizes an angle to [-PI, PI]
	_ALWAYS_INLINE_ void normalize_angle(real_t &angle) const {
		angle = Math::wrapf(angle, -static_cast<real_t>(Math::PI), static_cast<real_t>(Math::PI));
	}

	// Updates the homing timer and checks if interval is reached
	_ALWAYS_INLINE_ bool update_homing_timer(double delta) {
		homing_update_timer -= delta;
		if (homing_update_timer <= 0.0) {
			homing_update_timer = homing_update_interval;
			return true;
		}
		return false;
	}

	static void _bind_methods();
};
} // namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::HomingType);
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::OrbitingDirection);
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::OrbitingTextureRotation);
