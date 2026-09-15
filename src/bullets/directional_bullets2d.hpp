#pragma once

#include "../shared/bullet_speed_data2d.hpp"
#include "../shared/bullet_wobble_data2d.hpp"
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
	// Clears both homing deques through the pop loop so the global
	// mouse-target counter stays exact: force_delete() memdeletes without
	// running disable logic, and a leaked counter would query the mouse every
	// homing tick forever. Pop paths never touch the tree, so this is safe.
	~DirectionalBullets2D() override;

	enum OrbitingDirection {
		DontMove = 0,
		OrbitLeft,
		OrbitRight,
		OrbitRandom
	};

	enum OrbitingTextureRotation {
		FaceTarget = 0,
		FaceOppositeTarget,
		FaceOrbitingDirection,
		FaceOppositeOrbitingDirection
	};

	enum OrbitingFollowMode {
		FollowTarget = 0,
		FollowDeadzone,
		Anchored
	};

	enum OrbitingLockPolicy {
		RelockAlways = 0,
		StayLocked,
		RelockOnTargetChange
	};

	struct OrbitingData {
		real_t angle = 0.0f;
		real_t radius = 0.0f;
		OrbitingDirection direction = OrbitRight;
		OrbitingTextureRotation texture_rotation = FaceTarget;
		bool is_locked_orbiting = false;
		OrbitingFollowMode follow_mode = FollowTarget;
		real_t follow_deadzone = 0.0f;
		OrbitingLockPolicy lock_policy = RelockAlways;
		bool rigid_follow = true;
		Vector2 locked_center{ 0, 0 };
		HomingType locked_target_type = NotHoming;
		uint64_t locked_target_identity = 0;

		OrbitingData() = default;

		OrbitingData(real_t new_radius, OrbitingDirection new_direction, OrbitingTextureRotation new_texture_rotation) :
				angle(0.0),
				radius(new_radius),
				direction(new_direction),
				texture_rotation(new_texture_rotation),
				is_locked_orbiting(false) {};

		OrbitingData(real_t new_radius, OrbitingDirection new_direction, OrbitingTextureRotation new_texture_rotation, OrbitingFollowMode new_follow_mode, real_t new_follow_deadzone, OrbitingLockPolicy new_lock_policy, bool new_rigid_follow = true) :
				angle(0.0),
				radius(new_radius),
				direction(new_direction),
				texture_rotation(new_texture_rotation),
				is_locked_orbiting(false),
				follow_mode(new_follow_mode),
				follow_deadzone(new_follow_deadzone),
				lock_policy(new_lock_policy),
				rigid_follow(new_rigid_follow) {};
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

	// Ownership stamp for deferred homing work (reached-emits, auto-pops).
	// The volley-wide generation is bumped on every spawn/enable/disable: a
	// deferred call scheduled by a previous life carries a stale generation
	// and no-ops instead of eating the new life's targets or emitting ghost
	// signals. The per-bullet epoch below covers the single-bullet path the
	// volley generation can't: reach -> disable_bullet(i) -> enable_bullet(i)
	// -> push-new-target(i) before the flush. Without it the stale deferred
	// pop/emit (same volley generation) would eat the fresh front target and
	// fire a ghost reached signal for the dead life's target.
	uint64_t homing_operation_generation = 0;
	std::vector<uint64_t> bullet_homing_epochs;

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

	// SHARED SPEED / ROTATION (from spawn data or the runtime API below;
	// mirrors the data so getters stay truthful across pool reuse). When set
	// they take precedence over the per-bullet arrays; null hands control back
	// (already-seeded values persist - re-spawn or re-set to change them).
	Ref<BulletSpeedData2D> shared_bullet_speed_data;
	Ref<BulletRotationData2D> shared_bullet_rotation_data;

	// WOBBLE (sine/cos flight modulation, seeded from spawn data; editable
	// live below). Per-bullet entries resolve with the shared fallback rule
	// (empty = off, size == amount = entry i, else first drives all); null
	// entries and disabled entries are skipped per bullet. Shared wobble
	// takes precedence when set and enabled. Phase seeds fan out per bullet
	// (phase + step * i) so one resource makes snakes/petals, not sync waves.
	struct WobbleSeed {
		bool active = false;
		int mode = 0;
		real_t amplitude = 0.0;
		real_t frequency_hz = 0.0;
		real_t phase = 0.0;
		bool distance_phased = false;
		real_t damping_per_sec = 0.0;
		real_t delay_sec = 0.0;
		real_t duration_sec = 0.0;
	};
	std::vector<WobbleSeed> all_bullet_wobble;
	bool is_wobble_feature_enabled = false;
	// Per-bullet distance ledger for distance-phased wobble (same idea as
	// the shared pattern ledger; time-phased wobble uses curves_elapsed_time).
	std::vector<real_t> wobble_distance_traveled;

	// GRAVITY / DRAG (seeded from spawn data; editable live below).
	// Gravity is a constant acceleration in px/s^2; drag is a linear
	// coefficient (speed -= speed * drag * delta). Both default to off.
	Vector2 gravity = Vector2(0, 0);
	real_t linear_drag = 0.0;

	// HOMING GATING (seeded from spawn data; editable live below).
	// delay = straight-flight seconds before steering starts; duration =
	// seconds of steering before it stops (0 = infinite); lose_range =
	// steering pauses beyond this distance from the target (0 = unlimited).
	real_t homing_delay_sec = 0.0;
	real_t homing_duration_sec = 0.0;
	real_t homing_lose_range_px = 0.0;

public:
	// Advances one bullet along a movement-pattern curve for this tick.
	// distance_traveled is updated in place. Returns false when the pattern is
	// finished (degenerate curve or completed non-repeating run); the caller
	// then clears per-bullet entries or parks the shared ledger at the end.
	// Single implementation shared by the per-bullet and shared patterns.
	// known_len lets the shared-path caller pass its hoisted baked length
	// instead of re-querying it per bullet per tick (< 0 = query inside).
	// The inherited offset is carried in world space AFTER the pattern
	// steering: folding it into advance_dist would only stretch the pattern
	// step along the pattern direction instead of adding true wind.
	_ALWAYS_INLINE_ bool advance_movement_pattern(const Ref<Curve2D> &curve, bool face_movement_direction, bool repeat_pattern, real_t &distance_traveled, Vector2 &velocity_delta, Vector2 &curr_bullet_direction, Transform2D &curr_bullet_transf, real_t known_len = -1.0) {
		const real_t len = (known_len >= 0.001) ? known_len : curve->get_baked_length();
		// NaN baked length (corrupt Curve2D points) fails the < comparison, so
		// check finiteness explicitly: without it fmod/sample_baked propagate
		// NaN into velocity/origin and brick the volley permanently.
		if (!Math::is_finite(len) || len < 0.001) {
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
		// Reuse advance_dist (== |velocity_delta|, unchanged since entry) instead
		// of a second length() sqrt per bullet per tick.
		if (advance_dist > 0.0001 && local_delta.length_squared() > 0.00000001) {
			Vector2 pattern_direction = local_delta.rotated(curr_bullet_direction.angle()).normalized();
			velocity_delta = pattern_direction * advance_dist;
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
		// The mouse query is hoisted here (once per tick, not once per push):
		// get_global_mouse_position() walks to the viewport each call, and a
		// volley with per-bullet mouse pushes would otherwise pay it N times.
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
				// Front changed: route the orbit lock per policy (a new front
				// target never inherits a stale center) and give every bullet
				// a clean reached slate for the new target.
				orbit_route_shared_front_change();
			}
			shared_homing_deque_enabled = (targets_amount - trimmed) > 0;

			// If timer timed out, refresh the cached global position of the front target
			if (shared_homing_deque_enabled && homing_interval_reached) {
				shared_homing_deque.refresh_cached_front_target_global_position(cached_mouse_global_position);
			}
		}

		// While the shared deque holds targets, per-bullet caches go stale
		// (their branch never runs). Refresh them on the interval too, so a
		// shared drain hands over live positions instead of push-time ones.
		if (shared_homing_deque_enabled && homing_interval_reached && active_homing_count > 0) {
			for (size_t qi = 0; qi < all_bullet_homing_targets.size() && qi < all_homing_count.size(); ++qi) {
				if (all_homing_count[qi] > 0 && !all_bullet_homing_targets[qi].empty()) {
					all_bullet_homing_targets[qi].refresh_cached_front_target_global_position(cached_mouse_global_position);
				}
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
			// NaN baked length (corrupt Curve2D points) would flow into the
			// per-bullet advance as known_len; normalize to 0 here so the
			// use_shared_pattern gate below stays shut (advance also guards).
			if (!Math::is_finite(shared_pattern_len)) {
				shared_pattern_len = 0.0;
			}
		}

		// Locked-orbit snapshot for the pattern gate below: the orbit lock
		// state read per bullet must match what phase 7 sees, or the pattern
		// gate and the orbit displacement disagree for one frame.
		std::vector<uint8_t> orbit_locked_snapshot;
		if (is_orbiting_feature_enabled) {
			orbit_locked_snapshot.assign(amount_bullets, 0);
			for (int li : all_bullets_enabled_set.get_active_indexes()) {
				if (li >= 0 && li < amount_bullets && li < (int)all_orbiting_status.size() && li < (int)all_orbiting_data.size()) {
					if (all_orbiting_status[li] && all_orbiting_data[li].is_locked_orbiting) {
						orbit_locked_snapshot[li] = 1;
					}
				}
			}
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
			// Defense-in-depth: speed SoA is invariant-sized by set_up_movement_data,
			// but every consumer below indexes it - never trust the invariant in a hot loop.
			if (i >= (int)all_cached_speed.size() || i >= (int)all_cached_max_speed.size() || i >= (int)all_cached_acceleration.size()) {
				continue;
			}
			bool direction_got_updated = false;
			HomingTargetDeque *target_deque_used_for_orbiting = nullptr;

			// 1. STANDARD HOMING PHASE
			// Reached-signal timing runs AFTER steering below (see phase 7b):
			// orbit, pattern and curve steering rewrite the velocity, so the
			// reached test must predict with the final velocity_delta, not
			// the pre-steer cached velocity.
			if (shared_homing_deque_enabled) { // Handle homing towards shared deque (takes precedence over per-bullet homing)
				update_homing(shared_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
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
					// Upper resync like the pop paths: a counter above the live
					// deque size would phantom-home a stale cache.
					const int live_after_trim = curr_homing_deque.get_homing_targets_amount();
					if (curr_homing_count > live_after_trim) {
						active_homing_count -= (curr_homing_count - live_after_trim);
						if (active_homing_count < 0) {
							active_homing_count = 0;
						}
						curr_homing_count = live_after_trim;
					}

					// A trim that exposes a live deque whose front changed is a
					// front change like a pop: route the lock policy so a new
					// front target never inherits a stale center.
					if (trimmed_count > 0) {
						orbit_route_front_change_for_bullet(i, curr_homing_deque);
					}

					if (curr_homing_count > 0 && !curr_homing_deque.empty()) {
						// If per bullet homing is indeed active, then refresh the cache if interval has been reached
						if (homing_interval_reached) {
							curr_homing_deque.refresh_cached_front_target_global_position(cached_mouse_global_position);
						}

						update_homing(curr_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
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
			const Vector2 before_x = curr_bullet_direction;
			if (shared_x_mode == DirectionCurveMode::Additive) {
				curr_bullet_direction.x += shared_x_offset * shared_x_strength;
			} else {
				curr_bullet_direction.x = shared_x_offset * shared_x_strength;
			}
			// Same degenerate guard as the per-bullet appliers: an Override
			// result near zero keeps the incoming direction instead of
			// stalling the bullet (and snapping its texture to angle 0).
			if (curr_bullet_direction.length_squared() < 0.00000001) {
				curr_bullet_direction = before_x;
			} else {
				curr_bullet_direction = curr_bullet_direction.normalized();
			}
			direction_curves_for_texture = shared_curves_ptr;
		} else if (per_bullet_x_curve_valid) {
			apply_x_direction_curve(curr_bullet_direction, per_bullet_curves_data);
			direction_curves_for_texture = per_bullet_curves_data;
		}

		if (shared_curves_y_direction_curve_valid) {
			const Vector2 before_y = curr_bullet_direction;
			if (shared_y_mode == DirectionCurveMode::Additive) {
				curr_bullet_direction.y += shared_y_offset * shared_y_strength;
			} else {
				curr_bullet_direction.y = shared_y_offset * shared_y_strength;
			}
			if (curr_bullet_direction.length_squared() < 0.00000001) {
				curr_bullet_direction = before_y;
			} else {
				curr_bullet_direction = curr_bullet_direction.normalized();
			}
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

		// 2b. WOBBLE (sine/cos flight modulation). Lateral displaces the
		// heading perpendicular to flight (snakes, weaves, curtains);
		// angular oscillates the heading itself (corkscrews, petals).
		// Runs on the analytic offset DELTA between frames (not the absolute
		// offset) so pausing/teleporting never jumps the bullet. Disabled
		// entries cost one bool check per bullet; fully disabled volleys
		// skip the loop via the hoisted flag below.
		if (is_wobble_feature_enabled && i >= 0 && i < (int)all_bullet_wobble.size() && all_bullet_wobble[i].active) {
			const WobbleSeed &w = all_bullet_wobble[i];
			const real_t t = (real_t)curves_elapsed_time;
			bool in_window = t >= w.delay_sec && (w.duration_sec <= 0.0 || t < w.delay_sec + w.duration_sec);
			if (in_window && w.frequency_hz >= 0.0 && w.amplitude >= 0.0) {
				const real_t phase_base = w.distance_phased && i >= 0 && i < (int)wobble_distance_traveled.size()
						? wobble_distance_traveled[i] * 0.02
						: t;
				const real_t damp = (w.damping_per_sec > 0.0 && t > w.delay_sec) ? (real_t)Math::exp(-(double)(w.damping_per_sec * (t - w.delay_sec))) : 1.0;
				const real_t now_off = w.amplitude * damp * Math::sin(Math::TAU * w.frequency_hz * phase_base + w.phase);
				const real_t prev_t = t - (real_t)delta;
				const bool prev_in = prev_t >= w.delay_sec && (w.duration_sec <= 0.0 || prev_t < w.delay_sec + w.duration_sec);
				real_t prev_off = 0.0;
				if (prev_in && delta > 0.0) {
					const real_t prev_base = w.distance_phased && i >= 0 && i < (int)wobble_distance_traveled.size()
							? (wobble_distance_traveled[i] - (curr_bullet_direction * all_cached_speed[i]).length() * (real_t)delta) * 0.02
							: prev_t;
					const real_t prev_damp = (w.damping_per_sec > 0.0 && prev_t > w.delay_sec) ? (real_t)Math::exp(-(double)(w.damping_per_sec * (prev_t - w.delay_sec))) : 1.0;
					prev_off = w.amplitude * prev_damp * Math::sin(Math::TAU * w.frequency_hz * prev_base + w.phase);
				}
				const real_t frame_delta = now_off - prev_off;
				if (Math::is_finite(frame_delta) && Math::abs(frame_delta) > 0.00001) {
					if (w.mode == 1) {
						curr_bullet_direction = curr_bullet_direction.rotated(Math::deg_to_rad(frame_delta));
						if (curr_bullet_direction.length_squared() < 0.00000001) {
							curr_bullet_direction = Vector2(1, 0);
						} else {
							curr_bullet_direction = curr_bullet_direction.normalized();
						}
					} else {
						if (curr_bullet_direction.length_squared() > 0.00000001) {
							const Vector2 perp = Vector2(-curr_bullet_direction.y, curr_bullet_direction.x).normalized();
							curr_bullet_direction = (curr_bullet_direction + perp * (frame_delta * 0.01)).normalized();
						}
					}
					direction_got_updated = true;
				}
			}
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
			// Visual-only spin must not leak into ballistics: with
			// rotate_only_textures the instance rotation is rendering-only (the
			// shape path freezes too), so re-deriving direction from it would
			// steer bullets with texture spin. Skipped the same way.
			if (adjust_direction_based_on_rotation && !rotate_only_textures) {
				// columns[0] includes the texture rotation used for rendering;
				// strip it to recover the logical movement direction.
				// Degenerate basis (zero-scale) keeps the last good direction
				// instead of normalizing a ~zero vector into a stall.
				const Vector2 stripped_basis = all_cached_instance_transforms[i].columns[0].rotated(-cache_texture_rotation_radians);
				if (stripped_basis.length_squared() > 0.00000001) {
					curr_bullet_direction = stripped_basis.normalized();
				}
				direction_got_updated = true;
			}

			// 5. VELOCITY CALCULATION (ONLY IF DIRECTION GOT UPDATED) - use temp to avoid mutating cached velocity
			if (direction_got_updated) {
				all_cached_velocity[i] = curr_bullet_direction * all_cached_speed[i] + inherited_velocity_offset;
			}
			// Wind-free base: the inherited offset rides along AFTER pattern
			// steering below. advance_movement_pattern measures advance_dist =
			// |velocity_delta|, so leaving the offset in would stretch the
			// pattern step along the pattern direction instead of adding
			// world-space drift. Re-added after the pattern runs.
			Vector2 velocity_delta = (curr_bullet_direction * all_cached_speed[i]) * (real_t)delta;

		// 6. MOVEMENT PATTERNS (RELYING ON CURVES AND PATH2D)
		// Per-bullet entries are exclusively runtime-owned; the spawn-data
		// shared pattern lives in the shared slot with a per-bullet distance
		// ledger. Shared wins while active (consistent with shared curves and
		// the shared homing deque); per-bullet runs only when no shared
		// pattern is set - or once a non-repeating shared run finished.
		// A locked orbit owns the displacement: the pattern advance (and its
		// distance ledger) is skipped so a non-repeating pattern cannot finish
		// invisibly while the ring drives the bullet. Unlocked bullets run
		// patterns normally, including the fly-to-ring approach. The gate reads
		// the pre-tick snapshot above so it agrees with phase 7 even when the
		// deque empties mid-tick.
		const bool orbit_locked_this_bullet = is_orbiting_feature_enabled && i >= 0 && i < amount_bullets && i < (int)orbit_locked_snapshot.size() && orbit_locked_snapshot[i] && target_deque_used_for_orbiting != nullptr && !target_deque_used_for_orbiting->empty();
		bool use_shared_pattern = false;
		if (!orbit_locked_this_bullet) {
			if (shared_pattern_curve_valid && shared_pattern_len >= 0.001 && i >= 0 && i < (int)shared_movement_pattern_distances.size()) {
				use_shared_pattern = shared_movement_pattern_repeat || shared_movement_pattern_distances[i] < shared_pattern_len;
			}
		}
		const bool use_per_bullet_pattern = !orbit_locked_this_bullet && !use_shared_pattern && check_exists_bullet_movement_pattern_data(i);
		if (use_shared_pattern || use_per_bullet_pattern) {
			if (use_shared_pattern) {
				if (!advance_movement_pattern(shared_movement_pattern_curve, shared_movement_pattern_face_movement_direction, shared_movement_pattern_repeat, shared_movement_pattern_distances[i], velocity_delta, curr_bullet_direction, curr_bullet_transf, shared_pattern_len)) {
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
		// World-space wind: added after pattern steering so patterns shape
		// the ballistic step and the offset drifts the result (matches the
		// documented velocity composition direction * speed + offset).
		velocity_delta += inherited_velocity_offset * (real_t)delta;
		// Gravity: constant world-space acceleration folded into the step
		// (0.5 * g * dt^2 positional term; the velocity term lands in the
		// speed/drag phase below through all_cached_velocity). Drag trims
		// speed there. Distance-phased wobble measures true travel below.
		if (gravity.length_squared() > 0.0) {
			velocity_delta += gravity * (real_t)(0.5 * delta * delta);
		}
		if (is_wobble_feature_enabled && i >= 0 && i < (int)wobble_distance_traveled.size()) {
			wobble_distance_traveled[i] += velocity_delta.length();
		}

			auto &curr_bullet_origin = all_cached_instance_origin[i];

			// 7. ORBITING LOGIC (RELYING ON HOMING TARGETS)
			// A deque that ran dry unlocks the orbit under RelockAlways and
			// RelockOnTargetChange; StayLocked rides out the gap, keeping
			// angle/center/identity so the ring re-pins silently when a target
			// returns. Helpers below keep this policy-aware everywhere.
			const bool orbit_vectors_ready = i >= 0 && i < (int)all_orbiting_data.size() && i < (int)all_orbiting_status.size();
			if (is_orbiting_feature_enabled && (target_deque_used_for_orbiting == nullptr || target_deque_used_for_orbiting->empty())) {
				if (orbit_vectors_ready && all_orbiting_data[i].lock_policy != StayLocked) {
					all_orbiting_data[i].is_locked_orbiting = false;
				}
			}
			if (is_orbiting_feature_enabled && orbit_vectors_ready && all_orbiting_status[i] && target_deque_used_for_orbiting != nullptr && !target_deque_used_for_orbiting->empty()) {
				OrbitingData *const orbiting_data = &all_orbiting_data[i];
				if (orbiting_data != nullptr) {
					// RelockOnTargetChange watches identity, not distance: a new
					// front target (retarget, round-robin, reached-pop) unlocks
					// so the bullet re-acquires the ring around the new target;
					// the same target moving keeps the lock. StayLocked never
					// unlocks here; RelockAlways unlocked via the no-op write
					// and replace paths, not per-frame distance.
					if (orbiting_data->is_locked_orbiting && orbiting_data->lock_policy == RelockOnTargetChange && orbit_should_unlock_for_front_change(*orbiting_data, *target_deque_used_for_orbiting)) {
						orbiting_data->is_locked_orbiting = false;
					}
					// Ring center for this frame: FollowTarget tracks the target,
					// FollowDeadzone pins until the target walks out of the
					// deadzone, Anchored freezes at the lock point. Unlocked
					// bullets always fly toward the live target so they can
					// still reach the ring.
					const Vector2 orbit_center = orbiting_data->is_locked_orbiting ? orbit_effective_center(*orbiting_data, homing_target_pos) : homing_target_pos;
					const Vector2 to_center = curr_bullet_origin - orbit_center;
					const real_t current_dist = to_center.length();
					const bool already_locked = orbiting_data->is_locked_orbiting;

					// Track if we are ACTUALLY doing orbit movement this frame
					bool is_physically_orbiting_this_frame = false;

					// Movement Logic (Locked or Boundary Arrival)
					// DontMove = escort: holds a fixed ring slot (angle set at
					// lock time, never advanced) and translates with the target.
					// Zero-delta ticks and zero-speed bullets hold still: with
					// no time passing (or no speed to advance the sweep) the
					// ring slot is already correct, so any snap would be a
					// teleport, not motion.
					const real_t orbit_speed = all_cached_speed[i];
					const bool orbit_can_move = delta > 0.0 && orbit_speed > 0.0;
					if (already_locked && orbiting_data->direction == DontMove) {
						if (orbiting_data->rigid_follow || orbit_can_move) {
							Vector2 target_pos = orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
							Vector2 snap_delta = target_pos - curr_bullet_origin;
							if (orbiting_data->rigid_follow) {
								// Rigid follow: the formation holds regardless
								// of bullet speed, so fast targets can't drag
								// escorts behind.
								velocity_delta = snap_delta;
							} else if (orbit_can_move) {
								real_t max_step = orbit_speed * (real_t)delta;
								if (snap_delta.length_squared() > max_step * max_step) {
									snap_delta = snap_delta.normalized() * max_step;
								}
								velocity_delta = snap_delta;
							} else {
								velocity_delta = Vector2(0, 0);
							}
						} else {
							velocity_delta = Vector2(0, 0);
						}

						is_physically_orbiting_this_frame = true;
					} else if (already_locked) {
						real_t dir_multiplier = (orbiting_data->direction == OrbitRight) ? 1.0 : (orbiting_data->direction == OrbitLeft ? -1.0 : 0.0);

						if (dir_multiplier != 0.0) {
							// Guard against tiny radius (division by zero -> inf/NaN)
							real_t safe_radius = orbiting_data->radius;
							if (safe_radius < 0.01) {
								safe_radius = 0.01;
							}
							// Rigid follow moves the whole ring slot with the
							// target first (same as DontMove escorts), then
							// advances the angle: circling never lags behind a
							// moving target, no matter how slow the bullet is.
							// With rigid follow off, the angle advances but the
							// ring center lags: cheap drift look, bullets fall
							// behind fast targets (clamped to speed * delta).
							// Zero-delta ticks hold both angle and position: with
							// no time passing any snap would be a teleport.
							if (orbiting_data->rigid_follow) {
								if (delta > 0.0) {
									orbiting_data->angle += (orbit_speed / safe_radius) * dir_multiplier * (real_t)delta;
								}
								velocity_delta = (orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle)) - curr_bullet_origin;
							} else if (orbit_can_move) {
								real_t angular_speed = (orbit_speed / safe_radius) * dir_multiplier;
								orbiting_data->angle += angular_speed * delta;
								Vector2 target_pos = orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
								Vector2 snap_delta = target_pos - curr_bullet_origin;
								real_t max_step = orbit_speed * (real_t)delta;
								if (snap_delta.length_squared() > max_step * max_step) {
									snap_delta = snap_delta.normalized() * max_step;
								}
								velocity_delta = snap_delta;
							} else {
								velocity_delta = Vector2(0, 0);
							}
						} else {
							velocity_delta = (orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle)) - curr_bullet_origin;
						}

						is_physically_orbiting_this_frame = true;
					}
					// Check exact frame arrival: use epsilon so low-speed / high-FPS bullets still lock (speed*delta can be <0.2px)
					// DontMove locks here too: the angle stored is the escort's
					// fixed slot, held (never advanced) by the branch above.
					// Zero-delta ticks never lock: with no motion the distance
					// test is meaningless and the snap below would teleport.
					else if (delta > 0.0 && Math::abs(current_dist - orbiting_data->radius) < Math::max((real_t)(orbit_speed * delta), (real_t)2.0)) {
						// REACHED RADIUS - LOCK NOW
						orbiting_data->angle = to_center.angle();
						orbit_stamp_lock(*orbiting_data, orbit_center, *target_deque_used_for_orbiting);

						Vector2 target_pos = orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
						Vector2 snap_delta = target_pos - curr_bullet_origin;
						real_t max_step = orbit_speed * (real_t)delta;
						if (snap_delta.length_squared() > max_step * max_step) {
							snap_delta = snap_delta.normalized() * max_step;
						}
						velocity_delta = snap_delta;

						// We consider this frame as orbiting because we just snapped to the ring
						is_physically_orbiting_this_frame = true;
					} else if (current_dist < orbiting_data->radius) {
						// SPAWNED INSIDE - PUSH OUT
						// This is technically NOT orbiting yet, it's just moving to the border.
						// Zero-delta ticks hold still: advancing next_dist by
						// speed * 0 keeps the bullet exactly where it is.
						Vector2 outward_dir = (current_dist > 0.1f) ? (to_center / current_dist) : Vector2(1, 0);
						real_t next_dist = current_dist + (orbit_speed * (real_t)delta);
						Vector2 target_pos = orbit_center + (outward_dir * next_dist);
						velocity_delta = target_pos - curr_bullet_origin;
					}

					// TEXTURE ROTATION WHEN ORBITING
					// Only rotate if the bullet is PHYSICALLY orbiting (Locked or just snapped).
					// DontMove escorts still face the target (FaceTarget /
					// FaceOppositeTarget); tangential modes are skipped - an
					// escort has no direction of travel.
				if (is_physically_orbiting_this_frame && (orbiting_data->direction != DontMove || (orbiting_data->texture_rotation != FaceOrbitingDirection && orbiting_data->texture_rotation != FaceOppositeOrbitingDirection))) {
					Vector2 look_dir = Vector2();
					Vector2 radial_vec = (curr_bullet_origin - orbit_center).normalized();

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

			// 7b. REACHED SIGNAL (after all steering): tests the post-move
			// position directly. The transform was already advanced above, so
			// predicting again with velocity_delta would test two ticks
			// ahead. Queued behind a shared deque the per-bullet branch never
			// ran: homing_target_pos below is only valid when this bullet
			// actually homed this tick.
			if (target_deque_used_for_orbiting != nullptr && !target_deque_used_for_orbiting->empty() && homing_target_pos.is_finite()) {
				try_to_emit_bullet_homing_target_reached_signal(*target_deque_used_for_orbiting, shared_homing_deque_enabled, i, curr_bullet_origin, homing_target_pos, Vector2(0, 0));
			}

			// 9. MOVEMENT SPEED ACCELERATION - shared sampled once before loop.
			// NOTE: unlike BlockBullets2D (which accelerates ALL entries so a
			// re-enabled bullet rejoins at the volley's current speed),
			// directional freezes disabled bullets at their disable-time speed:
			// per-bullet ballistics are individually owned here, so a wake
			// resumes where that bullet left off (see enable_bullet).
			// Gravity steers velocity directly (no uphill slowdown model here:
			// speed magnitude stays ballistic, the step already curved above).
			// Linear drag trims speed after curves/accel so TD shells decay.
			if (shared_curves_acceleration_curve_valid) {
				all_cached_speed[i] = shared_movement_speed_val;
				all_cached_velocity[i] = all_cached_direction[i] * shared_movement_speed_val + inherited_velocity_offset + gravity * (real_t)delta;
			} else if (is_per_bullet_curves_valid && per_bullet_curves_data->movement_speed_curve.is_valid()) {
				bullet_accelerate_speed_using_curve(i, delta, per_bullet_curves_data);
			} else {
				bullet_accelerate_speed(i, delta);
			}
			if (linear_drag > 0.0 && Math::is_finite(linear_drag)) {
				const real_t keep = Math::max((real_t)0.0, (real_t)1.0 - linear_drag * (real_t)delta);
				all_cached_speed[i] *= keep;
				all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset + gravity * (real_t)delta;
			} else if (gravity.length_squared() > 0.0) {
				all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset + gravity * (real_t)delta;
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
			collision_scratch.clear();
			collision_scratch.swap(all_collided_bullets);
			for (auto &data : collision_scratch) {
				handle_bullet_collision(data.collision_type, data.bullet_index, data.collided_instance_id, data.queue_bullet_epoch);
				// handle_bullet_collision emits synchronously into user code, and a
				// handler may queue_free THIS multimesh mid-drain. The vectors above
				// are members - stop before the next iteration touches freed memory
				// (direct free() paths already reject via the factory guards).
				if (is_queued_for_deletion()) {
					collision_scratch.clear();
					break;
				}
			}
		}
	}

	// Identity used by the RelockOnTargetChange policy: the front target the
	// bullet locked onto last. Node2D = instance id, Vector2 = bit hash of
	// the snapshot namespaced away from the mouse sentinel, mouse = 1 (it
	// has no stable address).
	_ALWAYS_INLINE_ uint64_t orbit_target_identity(const HomingTargetDeque &deque) const {
		if (deque.empty()) {
			return 0;
		}
		const HomingTarget &front = deque.front();
		switch (front.type) {
			case Node2DTarget:
				if (deque.is_homing_target_valid(front.node2d_target_data.target, front.node2d_target_data.cached_valid_instance_id)) {
					return front.node2d_target_data.cached_valid_instance_id;
				}
				return 0;
			case GlobalPositionTarget: {
				const Vector2 p = front.global_position_target;
				if (!p.is_finite()) {
					return 0;
				}
				// Upper bits set: a hashed position can never equal the mouse
				// sentinel (3) or an empty deque (0), and sign information
				// survives (abs() collapsed +p/-p onto one identity before).
				int32_t bx = (int32_t)Math::round(p.x * 16.0);
				int32_t by = (int32_t)Math::round(p.y * 16.0);
				uint64_t h = ((uint64_t)(uint32_t)bx * 0x9E3779B1ULL) ^ ((uint64_t)(uint32_t)by * 0x85EBCA77ULL) ^ ((uint64_t)GlobalPositionTarget * 0xC2B2AE35ULL);
				return h | 0x4000000000000000ULL;
			}
			case MousePositionTarget:
				return (uint64_t)MousePositionTarget;
			default:
				return 0;
		}
	}

	// Locked bullet's live deque (shared wins, same precedence as the tick):
	// the center getter needs the unlocked fallback without duplicating it.
	_ALWAYS_INLINE_ bool orbit_live_deque_for_bullet(int bullet_index, const HomingTargetDeque *&r_deque) const {
		if (!shared_homing_deque.empty()) {
			r_deque = &shared_homing_deque;
			return true;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_bullet_homing_targets.size() || bullet_index >= (int)all_homing_count.size()) {
			return false;
		}
		if (all_homing_count[bullet_index] <= 0 || all_bullet_homing_targets[bullet_index].empty()) {
			return false;
		}
		r_deque = &all_bullet_homing_targets[bullet_index];
		return true;
	}

	_ALWAYS_INLINE_ HomingType orbit_target_type(const HomingTargetDeque &deque) const {
		if (deque.empty()) {
			return NotHoming;
		}
		return deque.front().type;
	}

	// Locked-ring center for this tick. FollowTarget tracks the target.
	// FollowDeadzone pins locked_center until the target walks farther than
	// follow_deadzone from it, then re-pins. Anchored ignores the target
	// entirely: the ring freezes where it locked.
	_ALWAYS_INLINE_ Vector2 orbit_effective_center(OrbitingData &orbiting_data, const Vector2 &live_target_pos) {
		switch (orbiting_data.follow_mode) {
			case Anchored:
				if (!orbiting_data.locked_center.is_finite()) {
					return live_target_pos;
				}
				return orbiting_data.locked_center;
			case FollowDeadzone: {
				const real_t deadzone = (orbiting_data.follow_deadzone > 0.0f) ? orbiting_data.follow_deadzone : 0.0f;
				if (!orbiting_data.locked_center.is_finite() || !live_target_pos.is_finite()) {
					return live_target_pos;
				}
				if ((live_target_pos - orbiting_data.locked_center).length() > deadzone) {
					orbiting_data.locked_center = live_target_pos;
				}
				return orbiting_data.locked_center;
			}
			case FollowTarget:
			default:
				return live_target_pos;
		}
	}

	// Stamp the lock bookkeeping shared by every lock site.
	_ALWAYS_INLINE_ void orbit_stamp_lock(OrbitingData &orbiting_data, const Vector2 &center, const HomingTargetDeque &deque) {
		orbiting_data.is_locked_orbiting = true;
		orbiting_data.locked_center = center;
		orbiting_data.locked_target_type = orbit_target_type(deque);
		orbiting_data.locked_target_identity = orbit_target_identity(deque);
	}

	// Policy gate for every event that would drop the lock.
	// Explicit user action (disable, clear, freed target) always unlocks.
	// Otherwise: RelockAlways unlocks, StayLocked keeps everything
	// (angle + center + identity) so the ring re-pins silently when the
	// target returns, and RelockOnTargetChange unlocks only when the front
	// target is a different identity than the one the bullet locked onto.
	_ALWAYS_INLINE_ bool orbit_should_unlock_for_front_change(OrbitingData &orbiting_data, const HomingTargetDeque &deque) {
		switch (orbiting_data.lock_policy) {
			case StayLocked:
				return false;
			case RelockOnTargetChange: {
				const uint64_t current = orbit_target_identity(deque);
				if (current == 0 || current != orbiting_data.locked_target_identity) {
					return true;
				}
				return false;
			}
			case RelockAlways:
			default:
				return true;
		}
	}

	///////////////// ORBITING DATA METHODS

	// Disabled bullets own no homing/orbit state (disable_bullet clears it
	// and the tick never moves them): pushing targets or arming orbit there
	// inflates the homing/orbiting counters for slots that never drain.
	// Retarget passes skip disabled slots; direct script calls get a loud
	// error here instead of a silent counter leak.
	_ALWAYS_INLINE_ bool orbit_reject_disabled_bullet(int bullet_index, const char *function_name) const {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return false;
		}
		if (!all_bullets_enabled_set.contains(bullet_index)) {
			UtilityFunctions::push_error(String(function_name) + ": bullet index " + String::num_int64(bullet_index) + " is disabled. Wake it with enable_bullet() first, then push targets or enable orbiting.");
			return true;
		}
		return false;
	}

	_ALWAYS_INLINE_ void bullet_enable_orbiting(int bullet_index, real_t orbiting_radius, OrbitingDirection orbiting_direction, OrbitingTextureRotation orbiting_texture_rotation, OrbitingFollowMode orbiting_follow_mode = FollowTarget, real_t orbiting_follow_deadzone = 0.0f, OrbitingLockPolicy orbiting_lock_policy = RelockAlways, bool orbiting_rigid_follow = true) {
		if (!validate_bullet_index(bullet_index, "bullet_enable_orbiting")) {
			return;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_enable_orbiting")) {
			return;
		}

		// NaN comparisons are always false, so a bare `< 0.01` check would
		// store NaN and brick the volley (tick math propagates it into the
		// origin forever). Reject non-finite outright like the setters do.
		if (!Math::is_finite(orbiting_radius) || orbiting_radius < 0.01) {
			if (!Math::is_finite(orbiting_radius)) {
				UtilityFunctions::push_error("Orbiting radius must be finite and >= 0.01, got " + String::num(orbiting_radius) + ". Orbiting stays disabled.");
				return;
			}
			UtilityFunctions::push_error("Orbiting radius must be >= 0.01, got " + String::num(orbiting_radius) + ". Clamping to 0.01 to avoid division by zero.");
			orbiting_radius = 0.01;
		}

		// OrbitRandom resolves per bullet at enable time: each bullet rolls
		// OrbitLeft or OrbitRight (never DontMove) so one call fans a mixed
		// ring. Stored as the rolled value, so getters and re-applies see a
		// concrete direction and the roll never changes mid-flight.
		if (orbiting_direction == OrbitRandom) {
			orbiting_direction = (UtilityFunctions::randi() % 2 == 0) ? OrbitLeft : OrbitRight;
		}

		if (orbiting_direction < DontMove || orbiting_direction > OrbitRight) {
			UtilityFunctions::push_error("Invalid orbiting direction " + String::num_int64(orbiting_direction) + ". Use DontMove, OrbitLeft, OrbitRight or OrbitRandom.");
			return;
		}

		if (orbiting_texture_rotation < FaceTarget || orbiting_texture_rotation > FaceOppositeOrbitingDirection) {
			UtilityFunctions::push_error("Invalid orbiting texture rotation " + String::num_int64(orbiting_texture_rotation) + ". Use FaceTarget, FaceOppositeTarget, FaceOrbitingDirection or FaceOppositeOrbitingDirection.");
			return;
		}

		if (orbiting_follow_mode < FollowTarget || orbiting_follow_mode > Anchored) {
			UtilityFunctions::push_error("Invalid orbiting follow mode " + String::num_int64(orbiting_follow_mode) + ". Use FollowTarget, FollowDeadzone or Anchored.");
			return;
		}

		if (!Math::is_finite(orbiting_follow_deadzone) || orbiting_follow_deadzone < 0.0) {
			UtilityFunctions::push_error("Orbiting follow deadzone must be finite and >= 0, got " + String::num(orbiting_follow_deadzone) + ".");
			return;
		}

		if (orbiting_lock_policy < RelockAlways || orbiting_lock_policy > RelockOnTargetChange) {
			UtilityFunctions::push_error("Invalid orbiting lock policy " + String::num_int64(orbiting_lock_policy) + ". Use RelockAlways, StayLocked or RelockOnTargetChange.");
			return;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 1) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " already has orbiting enabled.");
			return;
		}

		all_orbiting_data[bullet_index] = OrbitingData(orbiting_radius, orbiting_direction, orbiting_texture_rotation, orbiting_follow_mode, orbiting_follow_deadzone, orbiting_lock_policy, orbiting_rigid_follow);
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

		if (!Math::is_finite(new_radius)) {
			UtilityFunctions::push_error("Orbiting radius must be finite, got " + String::num(new_radius) + ". Radius unchanged (a NaN radius would brick the volley).");
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
		// No-op writes keep the lock: the spawner re-applies identical
		// tuning every retarget pass, and unlocking there caused the
		// 1-frame fly-to-rim flicker on moving targets.
		if (Math::is_equal_approx(orbiting_data.radius, new_radius)) {
			orbiting_data.radius = new_radius;
			return;
		}

		orbiting_data.radius = new_radius;
		// StayLocked survives retarget tuning: keep the angle, re-seat the
		// slot on the new radius next tick instead of flying back out to
		// re-acquire it. Every other policy re-locks from scratch.
		if (orbiting_data.lock_policy != StayLocked) {
			orbiting_data.is_locked_orbiting = false;
		}
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
		// OrbitRandom rolls a concrete direction per call (never DontMove):
		// setting it re-rolls instead of storing the sentinel, so the stored
		// direction is always a real sweep.
		if (new_direction == OrbitRandom) {
			new_direction = (UtilityFunctions::randi() % 2 == 0) ? OrbitLeft : OrbitRight;
		}
		if (new_direction < DontMove || new_direction > OrbitRight) {
			UtilityFunctions::push_error("Invalid orbiting direction " + String::num_int64(new_direction) + ". Use DontMove, OrbitLeft, OrbitRight or OrbitRandom.");
			return;
		}
		if (orbiting_data.direction == new_direction) {
			return;
		}
		orbiting_data.direction = new_direction;
		// Same StayLocked rule as the radius setter: a retarget carrying a
		// changed sweep keeps the slot instead of dropping it.
		if (orbiting_data.lock_policy != StayLocked) {
			orbiting_data.is_locked_orbiting = false;
		}
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

	// Re-lock every locked bullet onto a fresh deque without the fly-to-rim
	// flicker: same front identity = keep angle + center, new identity =
	// stamp the new target (angle preserved, center re-pinned) under
	// StayLocked/RelockOnTargetChange, full relock under RelockAlways.
	// Explicit clears still go through bullet_clear_homing_targets (unlock).
	_ALWAYS_INLINE_ void orbit_keep_lock_across_replace(HomingTargetDeque &deque) {
		for (size_t k = 0; k < all_orbiting_data.size(); ++k) {
			if (k >= all_orbiting_status.size() || all_orbiting_status[k] == 0) {
				continue;
			}
			OrbitingData &o = all_orbiting_data[k];
			if (!o.is_locked_orbiting) {
				continue;
			}
			if (deque.empty()) {
				if (o.lock_policy == StayLocked) {
					continue;
				}
				o.is_locked_orbiting = false;
				continue;
			}
			if (o.lock_policy == RelockAlways) {
				o.is_locked_orbiting = false;
				continue;
			}
			if (!orbit_should_unlock_for_front_change(o, deque)) {
				o.locked_center = deque.get_cached_front_target_global_position();
				o.locked_target_type = orbit_target_type(deque);
				o.locked_target_identity = orbit_target_identity(deque);
			} else {
				o.is_locked_orbiting = false;
			}
		}
	}

	// Policy-aware unlock for a deque that ran dry: StayLocked rides out the
	// gap (keeps angle/center/identity so the ring re-pins silently when a
	// target returns), every other policy unlocks.
	_ALWAYS_INLINE_ void orbit_unlock_on_empty_deque() {
		for (size_t k = 0; k < all_orbiting_data.size(); ++k) {
			if (k >= all_orbiting_status.size() || all_orbiting_status[k] == 0) {
				continue;
			}
			OrbitingData &o = all_orbiting_data[k];
			if (!o.is_locked_orbiting || o.lock_policy == StayLocked) {
				continue;
			}
			o.is_locked_orbiting = false;
		}
	}

	// Same, scoped to one per-bullet deque's owner.
	_ALWAYS_INLINE_ void orbit_unlock_on_empty_deque_for_bullet(int bullet_index) {
		if (bullet_index < 0 || bullet_index >= (int)all_orbiting_data.size() || bullet_index >= (int)all_orbiting_status.size()) {
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			return;
		}
		OrbitingData &o = all_orbiting_data[bullet_index];
		if (!o.is_locked_orbiting || o.lock_policy == StayLocked) {
			return;
		}
		o.is_locked_orbiting = false;
	}

	_ALWAYS_INLINE_ void all_bullets_enable_orbiting(real_t orbiting_radius, OrbitingDirection orbiting_direction, OrbitingTextureRotation orbiting_texture_rotation, int bullet_index_start = 0, int bullet_index_end_inclusive = -1, OrbitingFollowMode orbiting_follow_mode = FollowTarget, real_t orbiting_follow_deadzone = 0.0f, OrbitingLockPolicy orbiting_lock_policy = RelockAlways, bool orbiting_rigid_follow = true) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_enable_orbiting");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_enable_orbiting(i, orbiting_radius, orbiting_direction, orbiting_texture_rotation, orbiting_follow_mode, orbiting_follow_deadzone, orbiting_lock_policy, orbiting_rigid_follow);
		}
	}

	// Concentric-ring enable: bullet (start + k) orbits at radius_start + radius_step * k.
	// Invalid enums are rejected per bullet by bullet_enable_orbiting (already-enabled
	// bullets keep their radius with a warning instead of erroring the whole range).
	_ALWAYS_INLINE_ void all_bullets_enable_orbiting_linear(real_t radius_start, real_t radius_step, OrbitingDirection orbiting_direction = OrbitRight, OrbitingTextureRotation orbiting_texture_rotation = FaceTarget, int bullet_index_start = 0, int bullet_index_end_inclusive = -1, OrbitingFollowMode orbiting_follow_mode = FollowTarget, real_t orbiting_follow_deadzone = 0.0f, OrbitingLockPolicy orbiting_lock_policy = RelockAlways, bool orbiting_rigid_follow = true) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_enable_orbiting_linear");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_enable_orbiting(i, radius_start + radius_step * (real_t)(i - bullet_index_start), orbiting_direction, orbiting_texture_rotation, orbiting_follow_mode, orbiting_follow_deadzone, orbiting_lock_policy, orbiting_rigid_follow);
		}
	}

	_ALWAYS_INLINE_ bool bullet_is_orbiting_locked(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_is_orbiting_locked")) {
			return false;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			return false;
		}
		return all_orbiting_data[bullet_index].is_locked_orbiting;
	}

	_ALWAYS_INLINE_ Vector2 bullet_get_orbiting_center(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_center")) {
			return Vector2(0, 0);
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting center.");
			return Vector2(0, 0);
		}
		auto &orbiting_data = all_orbiting_data[bullet_index];
		if (orbiting_data.is_locked_orbiting) {
			return orbiting_data.locked_center;
		}
		const HomingTargetDeque *live_deque = nullptr;
		if (orbit_live_deque_for_bullet(bullet_index, live_deque) && live_deque != nullptr) {
			return live_deque->get_cached_front_target_global_position();
		}
		return Vector2(0, 0);
	}

	// Locked angle in radians: the ring slot the bullet holds (or is flying to).
	_ALWAYS_INLINE_ real_t bullet_get_orbiting_angle(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_angle")) {
			return 0.0;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting angle.");
			return 0.0;
		}
		return all_orbiting_data[bullet_index].angle;
	}

	// Re-pin the locked ring center without unlocking: Anchored rings follow a
	// scripted point, Deadzone rings skip ahead, StayLocked rings jump to a
	// teleported target. Rejected (no unlock) when orbiting is off or the
	// bullet never locked.
	_ALWAYS_INLINE_ void bullet_set_orbiting_center(int bullet_index, const Vector2 &new_center) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_center")) {
			return;
		}
		if (!new_center.is_finite()) {
			UtilityFunctions::push_error("bullet_set_orbiting_center: new_center must be finite (NaN/Inf is rejected).");
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting center.");
			return;
		}
		auto &orbiting_data = all_orbiting_data[bullet_index];
		if (!orbiting_data.is_locked_orbiting) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " is not locked onto its ring yet. Cannot set orbiting center.");
			return;
		}
		orbiting_data.locked_center = new_center;
	}

	_ALWAYS_INLINE_ void bullet_set_orbiting_follow_mode(int bullet_index, OrbitingFollowMode new_follow_mode) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_follow_mode")) {
			return;
		}
		if (new_follow_mode < FollowTarget || new_follow_mode > Anchored) {
			UtilityFunctions::push_error("Invalid orbiting follow mode " + String::num_int64(new_follow_mode) + ". Use FollowTarget, FollowDeadzone or Anchored.");
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting follow mode.");
			return;
		}
		all_orbiting_data[bullet_index].follow_mode = new_follow_mode;
	}

	_ALWAYS_INLINE_ OrbitingFollowMode bullet_get_orbiting_follow_mode(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_follow_mode")) {
			return FollowTarget;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting follow mode.");
			return FollowTarget;
		}
		return all_orbiting_data[bullet_index].follow_mode;
	}

	_ALWAYS_INLINE_ void bullet_set_orbiting_follow_deadzone(int bullet_index, real_t new_deadzone) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_follow_deadzone")) {
			return;
		}
		if (!Math::is_finite(new_deadzone) || new_deadzone < 0.0) {
			UtilityFunctions::push_error("Orbiting follow deadzone must be finite and >= 0, got " + String::num(new_deadzone) + ".");
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting follow deadzone.");
			return;
		}
		all_orbiting_data[bullet_index].follow_deadzone = new_deadzone;
	}

	_ALWAYS_INLINE_ real_t bullet_get_orbiting_follow_deadzone(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_follow_deadzone")) {
			return 0.0;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting follow deadzone.");
			return 0.0;
		}
		return all_orbiting_data[bullet_index].follow_deadzone;
	}

	_ALWAYS_INLINE_ void bullet_set_orbiting_lock_policy(int bullet_index, OrbitingLockPolicy new_lock_policy) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_lock_policy")) {
			return;
		}
		if (new_lock_policy < RelockAlways || new_lock_policy > RelockOnTargetChange) {
			UtilityFunctions::push_error("Invalid orbiting lock policy " + String::num_int64(new_lock_policy) + ". Use RelockAlways, StayLocked or RelockOnTargetChange.");
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting lock policy.");
			return;
		}
		all_orbiting_data[bullet_index].lock_policy = new_lock_policy;
	}

	_ALWAYS_INLINE_ OrbitingLockPolicy bullet_get_orbiting_lock_policy(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_lock_policy")) {
			return RelockAlways;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting lock policy.");
			return RelockAlways;
		}
		return all_orbiting_data[bullet_index].lock_policy;
	}

	// When on, locked OrbitLeft/OrbitRight bullets translate 1:1 with the
	// target (same rigid snap DontMove escorts use) and keep circling: the
	// ring never lags, stretches, or re-locks when the target moves. When
	// off, locked bullets chase the ring clamped to speed * delta, so slow
	// bullets trail behind fast targets. Never drops the lock.
	_ALWAYS_INLINE_ void bullet_set_orbiting_rigid_follow(int bullet_index, bool new_rigid_follow) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_rigid_follow")) {
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting rigid follow.");
			return;
		}
		all_orbiting_data[bullet_index].rigid_follow = new_rigid_follow;
	}

	_ALWAYS_INLINE_ bool bullet_get_orbiting_rigid_follow(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_rigid_follow")) {
			return true;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting rigid follow.");
			return true;
		}
		return all_orbiting_data[bullet_index].rigid_follow;
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_rigid_follow(bool new_rigid_follow, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_rigid_follow");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_rigid_follow(i, new_rigid_follow);
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

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_follow_mode(OrbitingFollowMode new_follow_mode, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_follow_mode");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_follow_mode(i, new_follow_mode);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_follow_deadzone(real_t new_deadzone, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_follow_deadzone");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_follow_deadzone(i, new_deadzone);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_lock_policy(OrbitingLockPolicy new_lock_policy, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_lock_policy");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_lock_policy(i, new_lock_policy);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_center(const Vector2 &new_center, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_center");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_center(i, new_center);
		}
	}

	_ALWAYS_INLINE_ TypedArray<bool> all_bullets_is_orbiting_locked(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_is_orbiting_locked");

		TypedArray<bool> arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_is_orbiting_locked(i));
		}

		return arr;
	}

	////////////////

	///////////// PER BULLET HOMING DEQUE POP METHODS

	// NOTE: manual pop/clear + push in the same frame as a reach races the
	// deferred auto-pop/emit queued for the old front (same bullet epoch):
	// the flush still pops the fresh front and fires a ghost reached signal.
	// Keep manual edits and auto-pop apart in one frame, or re-push after
	// the flush.
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

		Variant popped = queue.pop_front_target(cached_mouse_global_position);
		// Resync hardening: the tick trim path and direct deque edits can
		// leave the counter above the live deque size (phantom-homing an
		// empty deque). Clamp down so counters always reflect reality.
		const int live = queue.get_homing_targets_amount();
		if (all_homing_count[bullet_index] > live) {
			active_homing_count -= (all_homing_count[bullet_index] - live);
			if (active_homing_count < 0) {
				active_homing_count = 0;
			}
			all_homing_count[bullet_index] = live;
		}
		orbit_route_front_change_for_bullet(bullet_index, queue);
		return popped;
	}

	// NOTE: manual pop/clear + push in the same frame as a reach races the
	// deferred auto-pop/emit queued for the old front (same bullet epoch):
	// the flush still pops the fresh front and fires a ghost reached signal.
	// Keep manual edits and auto-pop apart in one frame, or re-push after
	// the flush.
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

		Variant popped = queue.pop_back_target(cached_mouse_global_position);
		// Same resync as the front-pop above.
		const int live = queue.get_homing_targets_amount();
		if (all_homing_count[bullet_index] > live) {
			active_homing_count -= (all_homing_count[bullet_index] - live);
			if (active_homing_count < 0) {
				active_homing_count = 0;
			}
			all_homing_count[bullet_index] = live;
		}
		// Back-pop leaves the front target untouched: only an emptied deque
		// unlocks here, the lock itself is never disturbed by a tail edit.
		if (bullet_index >= 0 && bullet_index < (int)all_orbiting_data.size() && bullet_index < (int)all_orbiting_status.size() && all_orbiting_status[bullet_index]) {
			orbit_keep_lock_across_replace_for_bullet_front_only(bullet_index, queue, false);
		}
		return popped;
	}
	/////////////////////

	//////////////// PER BULLET HOMING DEQUE PUSH METHODS

	_ALWAYS_INLINE_ bool bullet_homing_push_front_mouse_position_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_mouse_position_target")) {
			return false;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_homing_push_front_mouse_position_target")) {
			return false;
		}

		// Always refresh on push: keying freshness off the GLOBAL mouse-target counter
		// made a fresh target inherit this node's stale cache whenever any OTHER
		// multimesh held mouse targets.
		cached_mouse_global_position = get_global_mouse_position();

		auto &queue = all_bullet_homing_targets[bullet_index];

		// Only count the target if the deque actually stored it (rejected
		// pushes - full deque - must not desync the counters into
		// phantom-homing an empty deque forever).
		if (!queue.push_front_mouse_position_target(cached_mouse_global_position)) {
			return false;
		}

		++all_homing_count[bullet_index];
		++active_homing_count;

		// A re-exposed front target re-arms like the shared deque does: the
		// per-bullet reached flag below is per-target, so without this a
		// popped-then-repushed target never fires again.
		queue.reset_front_reached_flag();

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_node2d_target")) {
			return false;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_homing_push_front_node2d_target")) {
			return false;
		}

		if (new_homing_target == nullptr) {
			UtilityFunctions::push_error("bullet_homing_push_front_node2d_target: target is null, nothing pushed.");
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		if (!queue.push_front_node2d_target(new_homing_target)) {
			return false;
		}

		++all_homing_count[bullet_index];
		++active_homing_count;
		queue.reset_front_reached_flag();
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_global_position_target")) {
			return false;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_homing_push_front_global_position_target")) {
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
		queue.reset_front_reached_flag();
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_mouse_position_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_mouse_position_target")) {
			return false;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_homing_push_back_mouse_position_target")) {
			return false;
		}

		// Always refresh on push (see push_front variant for the rationale).
		cached_mouse_global_position = get_global_mouse_position();

		auto &queue = all_bullet_homing_targets[bullet_index];

		// Only count the target if the deque actually stored it (see front variant).
		if (!queue.push_back_mouse_position_target(cached_mouse_global_position)) {
			return false;
		}

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_node2d_target")) {
			return false;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_homing_push_back_node2d_target")) {
			return false;
		}

		if (new_homing_target == nullptr) {
			UtilityFunctions::push_error("bullet_homing_push_back_node2d_target: target is null, nothing pushed.");
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		if (!queue.push_back_node2d_target(new_homing_target)) {
			return false;
		}

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_global_position_target")) {
			return false;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_homing_push_back_global_position_target")) {
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

	// NOTE: manual pop/clear + push in the same frame as a reach races the
	// deferred auto-pop/emit queued for the old front (same bullet epoch):
	// the flush still pops the fresh front and fires a ghost reached signal.
	// Keep manual edits and auto-pop apart in one frame, or re-push after
	// the flush.
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
		orbit_unlock_on_empty_deque_for_bullet(bullet_index);
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

	// Bulk push paths skip disabled slots silently: the per-bullet push
	// rejects them loudly, and a retarget pass over a partially-disabled
	// volley must not spam one error per bullet per interval. Declared
	// before every bulk path that uses it (including the mouse pushes).
	_ALWAYS_INLINE_ bool orbit_skip_disabled_in_bulk(int bullet_index) const {
		return bullet_index < 0 || bullet_index >= amount_bullets || !all_bullets_enabled_set.contains(bullet_index);
	}

	_ALWAYS_INLINE_ void all_bullets_push_back_mouse_position_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_mouse_position_target");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (orbit_skip_disabled_in_bulk(i)) {
				continue;
			}
			bullet_homing_push_back_mouse_position_target(i);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_mouse_position_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_mouse_position_target");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (orbit_skip_disabled_in_bulk(i)) {
				continue;
			}
			bullet_homing_push_front_mouse_position_target(i);
		}
	}

	// Bulk push paths skip disabled slots silently (see the helper above the
	// mouse pushes): the per-bullet push rejects them loudly, and a retarget
	// pass over a partially-disabled volley must not spam one error per
	// bullet per interval.
	_ALWAYS_INLINE_ void all_bullets_push_back_homing_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_homing_target");
		if (node2d_or_global_position.get_type() == Variant::VECTOR2 && !Vector2(node2d_or_global_position).is_finite()) {
			UtilityFunctions::push_error("Non-finite homing target in all_bullets_push_back_homing_target. Nothing was pushed.");
			return;
		}
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (orbit_skip_disabled_in_bulk(i)) {
					continue;
				}
				bullet_homing_push_back_node2d_target(i, node);
			}
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			Vector2 global_pos = node2d_or_global_position;
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (orbit_skip_disabled_in_bulk(i)) {
					continue;
				}
				bullet_homing_push_back_global_position_target(i, global_pos);
			}
		} else {
			UtilityFunctions::push_error("Invalid homing target type in all_bullets_push_back_homing_target");
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_homing_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_homing_target");
		if (node2d_or_global_position.get_type() == Variant::VECTOR2 && !Vector2(node2d_or_global_position).is_finite()) {
			UtilityFunctions::push_error("Non-finite homing target in all_bullets_push_front_homing_target. Nothing was pushed.");
			return;
		}
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (orbit_skip_disabled_in_bulk(i)) {
					continue;
				}
				bullet_homing_push_front_node2d_target(i, node);
			}
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			Vector2 global_pos = node2d_or_global_position;
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (orbit_skip_disabled_in_bulk(i)) {
					continue;
				}
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

	// Per-bullet variant of the shared replace above: instead of clear (which
	// unlocks) + push, clear the raw deque inline and re-pin surviving locks
	// onto the fresh front. Returns false when the slot is disabled or the
	// target invalid (nothing touched): bulk callers skip disabled slots
	// silently, direct script calls with a bad target still get the loud
	// error below. A direct call on a disabled slot stays silent (bulk
	// parity) — wake the bullet first, then replace.
	_ALWAYS_INLINE_ bool bullet_replace_homing_targets_with_new_target(int bullet_index, const Variant &node2d_or_global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_replace_homing_targets_with_new_target")) {
			return false;
		}
		if (bullet_index < 0 || bullet_index >= amount_bullets || !all_bullets_enabled_set.contains(bullet_index)) {
			return false;
		}
		Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position);
		const bool is_vec = node2d_or_global_position.get_type() == Variant::VECTOR2;
		if (node == nullptr && !is_vec) {
			UtilityFunctions::push_error("Invalid homing target type in bullet_replace_homing_targets_with_new_target. Use a Node2D or Vector2. Nothing was changed.");
			return false;
		}
		if (is_vec && !Vector2(node2d_or_global_position).is_finite()) {
			UtilityFunctions::push_error("Non-finite homing target in bullet_replace_homing_targets_with_new_target. Nothing was changed.");
			return false;
		}
		auto &queue = all_bullet_homing_targets[bullet_index];
		auto &count = all_homing_count[bullet_index];
		active_homing_count -= count;
		if (active_homing_count < 0) {
			active_homing_count = 0;
		}
		count = 0;
		queue.clear_homing_targets(cached_mouse_global_position);
		bullet_homing_push_back_homing_target(bullet_index, node2d_or_global_position);
		orbit_keep_lock_across_replace_for_bullet(bullet_index, queue);
		return true;
	}

	_ALWAYS_INLINE_ void orbit_keep_lock_across_replace_for_bullet(int bullet_index, HomingTargetDeque &deque) {
		if (bullet_index < 0 || bullet_index >= (int)all_orbiting_data.size() || bullet_index >= (int)all_orbiting_status.size()) {
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			return;
		}
		orbit_keep_lock_across_replace_for_bullet_front_only(bullet_index, deque);
	}

	// Front-change core: shared by pops (any position) and replaces.
	// front_changed tells whether the front target is a different target
	// than before: a back-pop leaves the front untouched, so only an empty
	// deque unlocks there, while RelockAlways still re-locks on a real
	// front swap.
	_ALWAYS_INLINE_ void orbit_keep_lock_across_replace_for_bullet_front_only(int bullet_index, HomingTargetDeque &deque, bool front_changed = true) {
		OrbitingData &o = all_orbiting_data[bullet_index];
		if (!o.is_locked_orbiting) {
			return;
		}
		if (deque.empty()) {
			if (o.lock_policy != StayLocked) {
				o.is_locked_orbiting = false;
			}
			return;
		}
		if (!front_changed) {
			return;
		}
		if (o.lock_policy == RelockAlways) {
			o.is_locked_orbiting = false;
			return;
		}
		if (!orbit_should_unlock_for_front_change(o, deque)) {
			o.locked_center = deque.get_cached_front_target_global_position();
			o.locked_target_type = orbit_target_type(deque);
			o.locked_target_identity = orbit_target_identity(deque);
		} else {
			o.is_locked_orbiting = false;
		}
	}

	// Single routing point for every per-bullet front change that is not an
	// explicit clear: manual pops (front/back) and the deferred auto-pop
	// flush funnel here. Empty deque = policy-aware unlock (StayLocked rides
	// out the gap); non-empty deque = keep or unlock per lock policy, so
	// RelockAlways never holds a stale lock on a new target and
	// StayLocked/RelockOnTargetChange never flicker on the same target.
	_ALWAYS_INLINE_ void orbit_route_front_change_for_bullet(int bullet_index, HomingTargetDeque &deque) {
		orbit_keep_lock_across_replace_for_bullet(bullet_index, deque);
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_new_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_new_target");

		// Validate BEFORE clearing: an invalid target must not wipe the user's targets.
		const bool is_node2d = Object::cast_to<Node2D>(node2d_or_global_position) != nullptr;
		if (!is_node2d && node2d_or_global_position.get_type() != Variant::VECTOR2) {
			UtilityFunctions::push_error("Invalid homing target type in all_bullets_replace_homing_targets_with_new_target. Use a Node2D or Vector2. Nothing was changed.");
			return;
		}
		if (node2d_or_global_position.get_type() == Variant::VECTOR2 && !Vector2(node2d_or_global_position).is_finite()) {
			UtilityFunctions::push_error("Non-finite homing target in all_bullets_replace_homing_targets_with_new_target. Nothing was changed.");
			return;
		}

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_replace_homing_targets_with_new_target(i, node2d_or_global_position);
		}
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
			if (target.get_type() == Variant::VECTOR2 && !Vector2(target).is_finite()) {
				UtilityFunctions::push_error("Non-finite homing target in all_bullets_replace_homing_targets_with_new_target_array at array index " + String::num_int64(k) + ". Nothing was changed.");
				return;
			}
		}

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (i < 0 || i >= amount_bullets || !all_bullets_enabled_set.contains(i)) {
				continue;
			}
			auto &queue = all_bullet_homing_targets[i];
			auto &count = all_homing_count[i];
			active_homing_count -= count;
			if (active_homing_count < 0) {
				active_homing_count = 0;
			}
			count = 0;
			queue.clear_homing_targets(cached_mouse_global_position);
		}
		all_bullets_push_back_homing_targets_array(node2ds_or_global_positions_array, bullet_index_start, bullet_index_end_inclusive);
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			orbit_keep_lock_across_replace_for_bullet(i, all_bullet_homing_targets[i]);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_mouse(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_mouse");

		// Cache once for the whole loop
		cached_mouse_global_position = get_global_mouse_position();

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (i < 0 || i >= amount_bullets || !all_bullets_enabled_set.contains(i)) {
				continue;
			}
			auto &queue = all_bullet_homing_targets[i];
			auto &count = all_homing_count[i];
			active_homing_count -= count;
			if (active_homing_count < 0) {
				active_homing_count = 0;
			}
			count = 0;
			queue.clear_homing_targets(cached_mouse_global_position);
			if (all_bullet_homing_targets[i].push_back_mouse_position_target(cached_mouse_global_position)) {
				++all_homing_count[i];
				++active_homing_count;
			}
			orbit_keep_lock_across_replace_for_bullet(i, all_bullet_homing_targets[i]);
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
		// Disabled slots are skipped silently (bulk path): the push would
		// otherwise error per bullet on every retarget pass.
		for (int k = 0; k < range_size; ++k) {
			const int bi = bullet_index_start + k;
			if (bi < 0 || bi >= amount_bullets || !all_bullets_enabled_set.contains(bi)) {
				continue;
			}
			bullet_homing_push_back_homing_target(bi, node2ds_or_global_positions_array[k]);
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
	// NOTE: the deferred coalesced auto-pop (_do_shared_auto_pop_front_target)
	// is stamped with the volley generation only. A manual clear/pop/push
	// between the queue and the flush therefore races it: keep manual edits and
	// auto-pop apart in the same frame (or re-push after the flush), otherwise
	// the stale pop can eat the fresh front target.

	_ALWAYS_INLINE_ Variant shared_homing_deque_pop_front_target() {
		Variant popped = shared_homing_deque.pop_front_target(cached_mouse_global_position);
		orbit_route_shared_front_change();
		return popped;
	}

	_ALWAYS_INLINE_ Variant shared_homing_deque_pop_back_target() {
		Variant popped = shared_homing_deque.pop_back_target(cached_mouse_global_position);
		if (shared_homing_deque.empty()) {
			// The sole (= front) element is gone: the routing below resets
			// the dangling front pointers (via reset) and unlocks per
			// policy, so the next push starts every bullet fresh.
			orbit_route_shared_front_change();
		}
		// Non-empty back-pop leaves the front untouched: reached flags and
		// locks both stay, nothing to route.
		return popped;
	}

	// SHARED BULLET HOMING DEQUE PUSH METHODS
	// Push-front always swaps the front target, so every bullet is re-armed for
	// it. Push-back only re-arms when the deque was empty (that push creates
	// the front); otherwise the front is unchanged and fired flags must stay.
	// A push onto a volley with zero enabled bullets is rejected: the tick
	// never moves disabled slots, so queuing there only inflates the shared
	// state (and the global mouse counter) with targets that never drain.

	_ALWAYS_INLINE_ bool orbit_reject_fully_disabled_volley(const char *function_name) const {
		for (int k = 0; k < amount_bullets; ++k) {
			if (all_bullets_enabled_set.contains(k)) {
				return false;
			}
		}
		UtilityFunctions::push_error(String(function_name) + ": volley has no enabled bullets. Wake a bullet with enable_bullet() first.");
		return true;
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_mouse_position_target() {
		// Always refresh on push (see bullet_homing_push_front_mouse_position_target).
		cached_mouse_global_position = get_global_mouse_position();

		if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_front_mouse_position_target")) {
			return;
		}
		if (shared_homing_deque.push_front_mouse_position_target(cached_mouse_global_position)) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_node2d_target(Node2D *new_homing_target) {
		if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_front_node2d_target")) {
			return;
		}
		const int before = shared_homing_deque.get_homing_targets_amount();
		shared_homing_deque.push_front_node2d_target(new_homing_target);
		if (shared_homing_deque.get_homing_targets_amount() != before) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_global_position_target(const Vector2 &global_position) {
		if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_front_global_position_target")) {
			return;
		}
		if (shared_homing_deque.push_front_global_position_target(global_position)) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_mouse_position_target() {
		// Always refresh on push (see bullet_homing_push_front_mouse_position_target).
		cached_mouse_global_position = get_global_mouse_position();

		if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_back_mouse_position_target")) {
			return;
		}
		const bool was_empty = shared_homing_deque.empty();
		if (shared_homing_deque.push_back_mouse_position_target(cached_mouse_global_position) && was_empty) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_node2d_target(Node2D *new_homing_target) {
		if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_back_node2d_target")) {
			return;
		}
		const bool was_empty = shared_homing_deque.empty();
		if (shared_homing_deque.push_back_node2d_target(new_homing_target) && was_empty && !shared_homing_deque.empty()) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_global_position_target(const Vector2 &global_position) {
		if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_back_global_position_target")) {
			return;
		}
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
		// Unlatch a queued-then-cleared auto-pop: without this the stale
		// flush pops whatever is pushed next, eating a fresh front target.
		shared_auto_pop_queued = false;
		orbit_unlock_on_empty_deque();
	}

	// Replace = clear + push, but locks must survive the intermediate empty
	// deque under StayLocked/RelockOnTargetChange: validate first (invalid
	// input keeps the old queue AND the old lock), then re-pin surviving
	// locks onto the fresh front instead of dropping them.
	_ALWAYS_INLINE_ void shared_homing_deque_replace_homing_targets_with_new_target(const Variant &node2d_or_global_position) {
		Node2D *node2d_target = Object::cast_to<Node2D>(node2d_or_global_position);

		if (node2d_target) {
			shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
			reset_shared_homing_reached_state();
			shared_homing_deque_push_back_node2d_target(node2d_target);
			orbit_keep_lock_across_replace(shared_homing_deque);
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			if (!Vector2(node2d_or_global_position).is_finite()) {
				UtilityFunctions::push_error("Non-finite homing target passed to shared_homing_deque_replace_homing_targets_with_new_target. Nothing was changed.");
				return;
			}
			shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
			reset_shared_homing_reached_state();
			shared_homing_deque_push_back_global_position_target(node2d_or_global_position);
			orbit_keep_lock_across_replace(shared_homing_deque);
		} else {
			UtilityFunctions::push_error("Invalid type passed to shared_homing_deque_replace_homing_targets_with_new_target");
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array) {
		for (auto &target : node2ds_or_global_positions_array) {
			if (Object::cast_to<Node2D>(target) == nullptr && target.get_type() != Variant::VECTOR2) {
				UtilityFunctions::push_error("Invalid type passed to shared_homing_deque_replace_homing_targets_with_new_target_array. Nothing was changed.");
				return;
			}
			if (target.get_type() == Variant::VECTOR2 && !Vector2(target).is_finite()) {
				UtilityFunctions::push_error("Non-finite homing target passed to shared_homing_deque_replace_homing_targets_with_new_target_array. Nothing was changed.");
				return;
			}
		}
		shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
		reset_shared_homing_reached_state();

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
		orbit_keep_lock_across_replace(shared_homing_deque);
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

	// Teleport bookkeeping for locked orbits: re-aim the ring slot from the
	// bullet's new offset around the current ring center, so the next tick
	// holds the teleported position instead of snapping back. Unlocked or
	// non-orbiting bullets are untouched. The shift delta is unused for the
	// angle itself (the new absolute offset decides it) but keeps the
	// signature symmetric with the teleport paths.
	_ALWAYS_INLINE_ void orbit_reflect_teleport(int bullet_index, const Vector2 &p_shift_delta) {
		(void)p_shift_delta;
		if (bullet_index < 0 || bullet_index >= (int)all_orbiting_data.size() || bullet_index >= (int)all_orbiting_status.size()) {
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			return;
		}
		OrbitingData &o = all_orbiting_data[bullet_index];
		if (!o.is_locked_orbiting) {
			return;
		}
		if (bullet_index >= (int)all_cached_instance_origin.size()) {
			return;
		}
		// Ring center from the same policy the tick uses: unlocked fallback
		// is live target, locked uses the effective (possibly pinned) center.
		const HomingTargetDeque *live_deque = nullptr;
		Vector2 center = o.locked_center;
		if (orbit_live_deque_for_bullet(bullet_index, live_deque) && live_deque != nullptr && !live_deque->empty()) {
			center = orbit_effective_center(o, live_deque->get_cached_front_target_global_position());
		}
		const Vector2 offset = all_cached_instance_origin[bullet_index] - center;
		if (offset.length_squared() < 0.00000001 || !offset.is_finite()) {
			return;
		}
		o.angle = offset.angle();
		// Radius follows an explicit user move: without this a teleport far
		// off-ring pulls the bullet back on the next tick. The stored radius
		// setting is left alone; only the live slot re-aims.
		o.locked_center = center;
	}

	// Teleports a bullet to a new global position. A locked orbit re-aims
	// its ring slot from the new offset (same angle convention as the lock
	// moment) instead of snapping the bullet back next tick: teleporting is
	// an explicit user move, not target motion.
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

		// A teleport moves the bullet, not the target: re-aim the locked
		// ring slot from the new offset so the next tick holds the new
		// position instead of pulling the bullet back to the old slot.
		orbit_reflect_teleport(bullet_index, origin_delta);
	}

	// Shifts a bullet's position by a certain amount. Same locked-ring
	// re-aim as teleport_bullet (see above).
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

		orbit_reflect_teleport(bullet_index, shift_amount);
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
		// Near-zero basis = degenerate transform (e.g. zero-scale teleport):
		// normalizing it would silently stall the bullet at the inherited
		// offset. Reject like set_bullet_transform does instead.
		if (all_cached_instance_transforms[bullet_index].columns[0].length_squared() < 0.00000001) {
			UtilityFunctions::push_error("bullet_set_velocity: bullet transform has near-zero scale, direction is undefined. Fix the transform first (set_bullet_transform).");
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
	virtual bool custom_additional_enable_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_disable_logic() override final;

	// Resolves the spawn data's shared movement pattern Path2D and applies its
	// Curve2D to every bullet through the existing helpers. Empty path = off.
	void apply_shared_movement_pattern_from_data(const DirectionalBulletsData2D &directional_data);

	// Shared fallback rule for per-bullet spawn-data arrays (same as
	// all_bullet_speed_data): empty = off (-1), size == amount = entry i,
	// otherwise the first entry drives all bullets.
	int resolve_per_bullet_data_index(int array_size, int bullet_index) const {
		if (array_size <= 0) {
			return -1;
		}
		if (array_size == amount_bullets) {
			return bullet_index;
		}
		return 0;
	}

	// Seeds per-bullet curves/patterns from spawn data through the regular
	// per-bullet helpers (null entries skipped). Called from the custom
	// spawn/enable logic alongside the shared application; storage is
	// separate so ordering between them is irrelevant.
	void apply_per_bullet_curves_from_data(const DirectionalBulletsData2D &directional_data);
	void apply_per_bullet_movement_patterns_from_data(const DirectionalBulletsData2D &directional_data);
	void apply_wobble_from_data(const DirectionalBulletsData2D &directional_data);

	// WOBBLE / GRAVITY / DRAG / HOMING-GATE RUNTIME API (spawn-data
	// equivalents, editable live on the instance). Wobble entries resolve
	// with the shared fallback rule; setters validate like spawn data.
	WobbleSeed make_wobble_seed(const Ref<BulletWobbleData2D> &wobble, int bullet_index) const {
		WobbleSeed seed;
		BulletWobbleData2D *w = wobble.ptr();
		if (w == nullptr || !w->enabled) {
			return seed;
		}
		if (!Math::is_finite(w->amplitude) || w->amplitude < 0.0 || !Math::is_finite(w->frequency_hz) || w->frequency_hz < 0.0) {
			return seed;
		}
		if (!Math::is_finite(w->phase_rad) || !Math::is_finite(w->phase_step_per_bullet) || !Math::is_finite(w->damping_per_sec) || w->damping_per_sec < 0.0) {
			return seed;
		}
		if (!Math::is_finite(w->delay_sec) || w->delay_sec < 0.0 || !Math::is_finite(w->duration_sec) || w->duration_sec < 0.0) {
			return seed;
		}
		seed.active = true;
		seed.mode = (w->mode == BulletWobbleData2D::WOBBLE_ANGULAR) ? 1 : 0;
		seed.amplitude = w->amplitude;
		seed.frequency_hz = w->frequency_hz;
		seed.phase = w->phase_rad + w->phase_step_per_bullet * (real_t)bullet_index;
		seed.distance_phased = w->distance_phased;
		seed.damping_per_sec = w->damping_per_sec;
		seed.delay_sec = w->delay_sec;
		seed.duration_sec = w->duration_sec;
		return seed;
	}
	void refresh_wobble_feature_flag() {
		is_wobble_feature_enabled = false;
		for (const auto &w : all_bullet_wobble) {
			if (w.active) {
				is_wobble_feature_enabled = true;
				break;
			}
		}
		if (!is_wobble_feature_enabled) {
			wobble_distance_traveled.assign(amount_bullets, 0.0);
		}
	}
	bool get_is_wobble_enabled() const { return is_wobble_feature_enabled; }
	Vector2 get_gravity() const { return gravity; }
	void set_gravity(const Vector2 &value) {
		if (!value.is_finite()) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_gravity: value must be finite, keeping the old value.");
			return;
		}
		gravity = value;
	}
	real_t get_linear_drag() const { return linear_drag; }
	void set_linear_drag(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_linear_drag: value must be finite and >= 0, keeping the old value.");
			return;
		}
		linear_drag = value;
	}
	real_t get_homing_delay_sec() const { return homing_delay_sec; }
	void set_homing_delay_sec(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_homing_delay_sec: value must be finite and >= 0, keeping the old value.");
			return;
		}
		homing_delay_sec = value;
	}
	real_t get_homing_duration_sec() const { return homing_duration_sec; }
	void set_homing_duration_sec(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_homing_duration_sec: value must be finite and >= 0 (0 = infinite), keeping the old value.");
			return;
		}
		homing_duration_sec = value;
	}
	real_t get_homing_lose_range_px() const { return homing_lose_range_px; }
	void set_homing_lose_range_px(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_homing_lose_range_px: value must be finite and >= 0 (0 = unlimited), keeping the old value.");
			return;
		}
		homing_lose_range_px = value;
	}

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

	// SHARED SPEED / ROTATION RUNTIME API (spawn-data equivalent, editable live
	// on the instance). While set they take precedence over the per-bullet
	// arrays; clearing them (null) hands control back, keeping already-seeded
	// values - re-spawn or re-set to change those.
	Ref<BulletSpeedData2D> get_shared_bullet_speed_data() const { return shared_bullet_speed_data; }
	void set_shared_bullet_speed_data(const Ref<BulletSpeedData2D> &new_speed_data) {
		shared_bullet_speed_data = new_speed_data;
		if (new_speed_data.is_null()) {
			return;
		}
		TypedArray<BulletSpeedData2D> single_speed;
		single_speed.push_back(new_speed_data);
		set_up_movement_data(single_speed);
	}
	bool has_shared_bullet_speed_data() const { return shared_bullet_speed_data.is_valid(); }
	void remove_shared_bullet_speed_data() { set_shared_bullet_speed_data(Ref<BulletSpeedData2D>()); }

	Ref<BulletRotationData2D> get_shared_bullet_rotation_data() const { return shared_bullet_rotation_data; }
	void set_shared_bullet_rotation_data(const Ref<BulletRotationData2D> &new_rotation_data) {
		shared_bullet_rotation_data = new_rotation_data;
		if (new_rotation_data.is_null()) {
			return;
		}
		TypedArray<BulletRotationData2D> single_rotation;
		single_rotation.push_back(new_rotation_data);
		set_rotation_data(single_rotation, rotate_only_textures);
	}
	bool has_shared_bullet_rotation_data() const { return shared_bullet_rotation_data.is_valid(); }
	void remove_shared_bullet_rotation_data() { set_shared_bullet_rotation_data(Ref<BulletRotationData2D>()); }

	bool get_adjust_direction_based_on_rotation() const { return adjust_direction_based_on_rotation; }
	void set_adjust_direction_based_on_rotation(bool value) { adjust_direction_based_on_rotation = value; }

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
		// Value-reset the whole orbit payload, not just the lock: a stale
		// radius/direction/angle must never ride into the next life.
		for (auto &o : all_orbiting_data) {
			o = OrbitingData();
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
		// A queued-then-invalidated shared auto-pop must not stay latched:
		// the drain bumps homing_operation_generation so the pop no-ops, and
		// a later manual wake (which skips the enable-path reset) would
		// otherwise never queue another one.
		shared_auto_pop_queued = false;
		// Shared movement/speed/rotation are per-owner runtime state like the
		// homing deques: a pooled instance must not steer the next owner along
		// the previous owner's pattern or speed. enable_multimesh() re-seeds
		// these from spawn data; an enable_bullet() wake has no data, so blank
		// them here to make the pool neutral on every reuse path.
		shared_movement_pattern_curve.unref();
		shared_movement_pattern_face_movement_direction = false;
		shared_movement_pattern_repeat = true;
		shared_movement_pattern_distances.assign(shared_movement_pattern_distances.size(), 0.0);
		shared_bullet_speed_data.unref();
		shared_bullet_rotation_data.unref();
	}

	// Single-bullet hook called from disable_bullet(): the tick only trims
	// active bullets, so a partially disabled multimesh would otherwise leak
	// this bullet's targets (and the global mouse counter) until full teardown.
	// Also bumps this bullet's homing epoch: deferred reached-emits/auto-pops
	// queued before the disable carry the old epoch and no-op, so a
	// disable -> enable -> push-new-target sequence before the flush can
	// neither eat the fresh front target nor fire a ghost signal. The
	// volley-wide generation is intentionally NOT bumped here - that would
	// invalidate every sibling's legitimately queued work.
	virtual void on_bullet_disabled(int bullet_index) override {
		if (bullet_index < 0 || bullet_index >= (int)all_bullet_homing_targets.size()) {
			return;
		}
		if (bullet_index >= 0 && bullet_index < (int)bullet_homing_epochs.size()) {
			++bullet_homing_epochs[bullet_index];
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
			all_orbiting_data[bullet_index] = OrbitingData();
		}
	}

protected:
	// Updates homing behavior for a bullet. Zero-delta ticks steer nothing:
	// with no time passing any direction or texture change would be motion
	// without movement, so the bullet holds its pose.
	_ALWAYS_INLINE_ void update_homing(HomingTargetDeque &homing_deque, int bullet_index, double delta, Vector2 &bullet_pos, Vector2 &target_pos) {
		if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_origin.size() || bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
			return;
		}
		if (!Math::is_finite(delta) || delta <= 0.0) {
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

		// Homing gating: delay (straight flight first), duration (escape
		// window), lose-range (pause while too far). All cheap float
		// compares; curves_elapsed_time is the volley clock.
		if (homing_delay_sec > 0.0 && curves_elapsed_time < homing_delay_sec) {
			return;
		}
		if (homing_duration_sec > 0.0 && curves_elapsed_time >= homing_delay_sec + homing_duration_sec) {
			return;
		}
		if (homing_lose_range_px > 0.0 && dist_sq > homing_lose_range_px * homing_lose_range_px) {
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
			// Symmetric check: rotation_speed may be negative (CCW), max is
			// always >= 0. Without abs(), negative spin never triggers the stop.
			bool max_reached = Math::abs(cache_rotation_speed) >= all_max_rotation_speed[bullet_index];

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

	// Generation-guarded deferred emit: the target travels as an instance id
	// and is resolved at fire time (null when freed) instead of carrying a
	// possibly-dangling raw pointer across the frame.
	_ALWAYS_INLINE_ void _do_emit_homing_target_reached(uint64_t p_generation, int p_bullet_index, uint64_t p_bullet_epoch, uint64_t p_target_instance_id, const Vector2 &p_target_global_position) {
		if (p_generation != homing_operation_generation) {
			return; // Scheduled by a previous life (pool reuse before the flush).
		}
		// No signal for a bullet whose life ended after the queue: single-bullet
		// disable/enable bumps the per-bullet epoch (but not the volley
		// generation), and a disabled bullet must stay silent even when its
		// epoch still matches (e.g. reach -> disable with no re-enable).
		if (p_bullet_index < 0 || p_bullet_index >= (int)bullet_homing_epochs.size() || bullet_homing_epochs[p_bullet_index] != p_bullet_epoch || !all_bullets_enabled_set.contains(p_bullet_index)) {
			return;
		}
		Node2D *target = nullptr;
		if (p_target_instance_id != 0) {
			target = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(p_target_instance_id)));
		}
		emit_signal("bullet_homing_target_reached", this, p_bullet_index, target, p_target_global_position);
	}

	_ALWAYS_INLINE_ void _do_shared_auto_pop_front_target(uint64_t p_generation) {
		if (p_generation != homing_operation_generation) {
			return; // Never touch the flag: it belongs to the new life now.
		}
		shared_auto_pop_queued = false;
		shared_homing_deque.pop_front_target(cached_mouse_global_position);
		orbit_route_shared_front_change();
	}

	// Generation-guarded deferred per-bullet pop: a stale call no-ops instead
	// of eating the new life's front target. Guards both the volley
	// generation (pool reuse) and the per-bullet epoch (single-bullet
	// disable/enable + fresh push before the flush).
	_ALWAYS_INLINE_ void _do_auto_pop_front_target(uint64_t p_generation, int p_bullet_index, uint64_t p_bullet_epoch) {
		if (p_generation != homing_operation_generation) {
			return;
		}
		if (p_bullet_index < 0 || p_bullet_index >= (int)bullet_homing_epochs.size() || bullet_homing_epochs[p_bullet_index] != p_bullet_epoch) {
			return;
		}
		bullet_homing_pop_front_target(p_bullet_index);
	}

	// Single routing point for every shared-deque front change: the deferred
	// shared auto-pop flush and the manual shared pops funnel here. Resets
	// the reached state (a new front target re-arms every bullet) and routes
	// the orbit lock per policy, so RelockAlways re-acquires on the new
	// target instead of holding a stale center.
	_ALWAYS_INLINE_ void orbit_route_shared_front_change() {
		reset_shared_homing_reached_state();
		orbit_keep_lock_across_replace(shared_homing_deque);
	}

	// Reached check against the post-move position: the caller passes the
	// already-advanced origin with a zero delta, so the test runs exactly
	// where the bullet landed this tick (orbit/pattern/curve output). A
	// point test cannot sweep-catch tunneling: when speed * delta exceeds
	// twice the threshold a bullet can jump clean over it, so size
	// homing_distance_before_reached for the fastest volleys.
	_ALWAYS_INLINE_ void try_to_emit_bullet_homing_target_reached_signal(HomingTargetDeque &homing_deque, bool is_using_shared_homing_deque, int bullet_index, const Vector2 &bullet_pos, const Vector2 &target_pos, const Vector2 &post_velocity_delta) {
		if (homing_deque.empty()) {
			return;
		}
		// Reached check against the predicted post-move position: at high speed a bullet
		// can tunnel past the threshold within one tick and never fire on pre-move pos.
		const Vector2 check_pos = bullet_pos + post_velocity_delta;
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
				const uint64_t bullet_epoch = (bullet_index >= 0 && bullet_index < (int)bullet_homing_epochs.size()) ? bullet_homing_epochs[bullet_index] : 0;
				switch (target.type) {
					case GlobalPositionTarget:
						// Deferred through the generation-guarded emitter: the target travels
						// as an instance id (resolved at fire time, null when freed) and a
						// stale generation no-ops, so pool reuse before the flush can neither
						// crash on a dangling pointer nor emit ghosts.
						call_deferred("_do_emit_homing_target_reached", homing_operation_generation, bullet_index, bullet_epoch, (uint64_t)0, target_pos);
						break;
					case Node2DTarget: {
						auto &target_data = target.node2d_target_data;

						// In case the target instance is freed - will still emit the signal, but with a nullptr as the target
						uint64_t target_id = 0;
						if (homing_deque.is_homing_target_valid(target_data.target, target_data.cached_valid_instance_id)) {
							target_id = target_data.cached_valid_instance_id;
						}
						call_deferred("_do_emit_homing_target_reached", homing_operation_generation, bullet_index, bullet_epoch, target_id, target_pos);
						break;
					}
					case NotHoming:
						break;
					case MousePositionTarget:
						call_deferred("_do_emit_homing_target_reached", homing_operation_generation, bullet_index, bullet_epoch, (uint64_t)0, target_pos);
						break;
				}

				// Pop the front target automatically if that's what the user wants.
				// Shared deque: N bullets reaching in the same tick must queue exactly
				// ONE deferred pop, otherwise the storm would drain every target the
				// user pushed. Per-bullet deques pop their own deque per bullet - no
				// storm there.
				if (is_using_shared_homing_deque) {
					// Both pops carry the generation: a pool reuse before the flush
					// no-ops instead of eating the new life's targets.
					if (shared_homing_deque_auto_pop_after_target_reached && !shared_auto_pop_queued) {
						shared_auto_pop_queued = true;
						call_deferred("_do_shared_auto_pop_front_target", homing_operation_generation);
					}
				} else {
					if (bullet_homing_auto_pop_after_target_reached) {
						const uint64_t pop_epoch = (bullet_index >= 0 && bullet_index < (int)bullet_homing_epochs.size()) ? bullet_homing_epochs[bullet_index] : 0;
						call_deferred("_do_auto_pop_front_target", homing_operation_generation, bullet_index, pop_epoch);
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
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::OrbitingFollowMode);
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::OrbitingLockPolicy);
