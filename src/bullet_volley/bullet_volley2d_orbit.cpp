// Orbiting: per-bullet orbit state around the homing front target (radius,
// direction, follow mode, locking). Orbit motion is applied inside move_bullets.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// True (after one "Bullet index N has orbiting disabled. Cannot <action>."
// warning) when the bullet's orbit is off. Call after validate_bullet_index.
bool BulletVolley2D::orbit_disabled_warn(int bullet_index, const char *action) const {
	if (all_orbiting_status[bullet_index] != 0) {
		return false;
	}
	UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot " + action + ".");
	return true;
}

void BulletVolley2D::bullet_enable_orbiting(int bullet_index, real_t orbiting_radius, OrbitingDirection orbiting_direction, OrbitingTextureRotation orbiting_texture_rotation, OrbitingFollowMode orbiting_follow_mode, real_t orbiting_follow_deadzone, OrbitingLockPolicy orbiting_lock_policy, bool orbiting_rigid_follow) {
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

void BulletVolley2D::bullet_disable_orbiting(int bullet_index) {
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

void BulletVolley2D::bullet_set_orbiting_radius(int bullet_index, real_t new_radius) {
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

	if (orbit_disabled_warn(bullet_index, "set orbiting radius")) {
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

real_t BulletVolley2D::bullet_get_orbiting_radius(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_radius")) {
		return 0.0;
	}

	if (orbit_disabled_warn(bullet_index, "get orbiting radius")) {
		return 0.0;
	}

	auto &orbiting_data = all_orbiting_data[bullet_index];
	return orbiting_data.radius;
}

bool BulletVolley2D::bullet_is_orbiting_enabled(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_is_orbiting_enabled")) {
		return false;
	}

	return all_orbiting_status[bullet_index] == 1;
}

void BulletVolley2D::bullet_set_orbiting_texture_rotation(int bullet_index, OrbitingTextureRotation new_texture_rotation) {
	if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_texture_rotation")) {
		return;
	}

	if (orbit_disabled_warn(bullet_index, "set orbiting texture rotation")) {
		return;
	}

	auto &orbiting_data = all_orbiting_data[bullet_index];
	if (new_texture_rotation < FaceTarget || new_texture_rotation > FaceOppositeOrbitingDirection) {
		UtilityFunctions::push_error("Invalid orbiting texture rotation " + String::num_int64(new_texture_rotation) + ". Use FaceTarget, FaceOppositeTarget, FaceOrbitingDirection or FaceOppositeOrbitingDirection.");
		return;
	}
	orbiting_data.texture_rotation = new_texture_rotation;
}

BulletVolley2D::OrbitingTextureRotation BulletVolley2D::bullet_get_orbiting_texture_rotation(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_texture_rotation")) {
		return FaceTarget;
	}

	if (orbit_disabled_warn(bullet_index, "get orbiting texture rotation")) {
		return FaceTarget;
	}

	auto &orbiting_data = all_orbiting_data[bullet_index];
	return orbiting_data.texture_rotation;
}

void BulletVolley2D::bullet_set_orbiting_direction(int bullet_index, OrbitingDirection new_direction) {
	if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_direction")) {
		return;
	}

	if (orbit_disabled_warn(bullet_index, "set orbiting direction")) {
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

BulletVolley2D::OrbitingDirection BulletVolley2D::bullet_get_orbiting_direction(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_direction")) {
		return DontMove;
	}

	if (orbit_disabled_warn(bullet_index, "get orbiting direction")) {
		return DontMove;
	}

	auto &orbiting_data = all_orbiting_data[bullet_index];
	return orbiting_data.direction;
}

void BulletVolley2D::all_bullets_enable_orbiting(real_t orbiting_radius, OrbitingDirection orbiting_direction, OrbitingTextureRotation orbiting_texture_rotation, int bullet_index_start, int bullet_index_end_inclusive, OrbitingFollowMode orbiting_follow_mode, real_t orbiting_follow_deadzone, OrbitingLockPolicy orbiting_lock_policy, bool orbiting_rigid_follow) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_enable_orbiting", [&](int i) { bullet_enable_orbiting(i, orbiting_radius, orbiting_direction, orbiting_texture_rotation, orbiting_follow_mode, orbiting_follow_deadzone, orbiting_lock_policy, orbiting_rigid_follow); });
}

void BulletVolley2D::all_bullets_enable_orbiting_linear(real_t radius_start, real_t radius_step, OrbitingDirection orbiting_direction, OrbitingTextureRotation orbiting_texture_rotation, int bullet_index_start, int bullet_index_end_inclusive, OrbitingFollowMode orbiting_follow_mode, real_t orbiting_follow_deadzone, OrbitingLockPolicy orbiting_lock_policy, bool orbiting_rigid_follow) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_enable_orbiting_linear");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		const real_t want_radius = radius_start + radius_step * (real_t)(i - bullet_index_start);
		if (i >= 0 && i < (int)all_orbiting_status.size() && all_orbiting_status[i] == 1) {
			bullet_set_orbiting_radius(i, want_radius);
			if (orbiting_direction != OrbitRandom) {
				bullet_set_orbiting_direction(i, orbiting_direction);
			}
			bullet_set_orbiting_texture_rotation(i, orbiting_texture_rotation);
			bullet_set_orbiting_follow_mode(i, orbiting_follow_mode);
			bullet_set_orbiting_follow_deadzone(i, orbiting_follow_deadzone);
			bullet_set_orbiting_lock_policy(i, orbiting_lock_policy);
			bullet_set_orbiting_rigid_follow(i, orbiting_rigid_follow);
			continue;
		}
		bullet_enable_orbiting(i, want_radius, orbiting_direction, orbiting_texture_rotation, orbiting_follow_mode, orbiting_follow_deadzone, orbiting_lock_policy, orbiting_rigid_follow);
	}
}

bool BulletVolley2D::bullet_is_orbiting_locked(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_is_orbiting_locked")) {
		return false;
	}
	if (all_orbiting_status[bullet_index] == 0) {
		return false;
	}
	return all_orbiting_data[bullet_index].is_locked_orbiting;
}

Vector2 BulletVolley2D::bullet_get_orbiting_center(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_center")) {
		return Vector2(0, 0);
	}
	if (orbit_disabled_warn(bullet_index, "get orbiting center")) {
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

real_t BulletVolley2D::bullet_get_orbiting_angle(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_angle")) {
		return 0.0;
	}
	if (orbit_disabled_warn(bullet_index, "get orbiting angle")) {
		return 0.0;
	}
	return all_orbiting_data[bullet_index].angle;
}

void BulletVolley2D::bullet_set_orbiting_center(int bullet_index, const Vector2 &new_center) {
	if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_center")) {
		return;
	}
	if (!new_center.is_finite()) {
		UtilityFunctions::push_error("bullet_set_orbiting_center: new_center must be finite (NaN/Inf is rejected).");
		return;
	}
	if (orbit_disabled_warn(bullet_index, "set orbiting center")) {
		return;
	}
	auto &orbiting_data = all_orbiting_data[bullet_index];
	if (!orbiting_data.is_locked_orbiting) {
		UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " is not locked onto its ring yet. Cannot set orbiting center.");
		return;
	}
	orbiting_data.locked_center = new_center;
}

void BulletVolley2D::bullet_set_orbiting_follow_mode(int bullet_index, OrbitingFollowMode new_follow_mode) {
	if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_follow_mode")) {
		return;
	}
	if (new_follow_mode < FollowTarget || new_follow_mode > Anchored) {
		UtilityFunctions::push_error("Invalid orbiting follow mode " + String::num_int64(new_follow_mode) + ". Use FollowTarget, FollowDeadzone or Anchored.");
		return;
	}
	if (orbit_disabled_warn(bullet_index, "set orbiting follow mode")) {
		return;
	}
	all_orbiting_data[bullet_index].follow_mode = new_follow_mode;
}

BulletVolley2D::OrbitingFollowMode BulletVolley2D::bullet_get_orbiting_follow_mode(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_follow_mode")) {
		return FollowTarget;
	}
	if (orbit_disabled_warn(bullet_index, "get orbiting follow mode")) {
		return FollowTarget;
	}
	return all_orbiting_data[bullet_index].follow_mode;
}

void BulletVolley2D::bullet_set_orbiting_follow_deadzone(int bullet_index, real_t new_deadzone) {
	if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_follow_deadzone")) {
		return;
	}
	if (!Math::is_finite(new_deadzone) || new_deadzone < 0.0) {
		UtilityFunctions::push_error("Orbiting follow deadzone must be finite and >= 0, got " + String::num(new_deadzone) + ".");
		return;
	}
	if (orbit_disabled_warn(bullet_index, "set orbiting follow deadzone")) {
		return;
	}
	all_orbiting_data[bullet_index].follow_deadzone = new_deadzone;
}

real_t BulletVolley2D::bullet_get_orbiting_follow_deadzone(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_follow_deadzone")) {
		return 0.0;
	}
	if (orbit_disabled_warn(bullet_index, "get orbiting follow deadzone")) {
		return 0.0;
	}
	return all_orbiting_data[bullet_index].follow_deadzone;
}

void BulletVolley2D::bullet_set_orbiting_lock_policy(int bullet_index, OrbitingLockPolicy new_lock_policy) {
	if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_lock_policy")) {
		return;
	}
	if (new_lock_policy < RelockAlways || new_lock_policy > RelockOnTargetChange) {
		UtilityFunctions::push_error("Invalid orbiting lock policy " + String::num_int64(new_lock_policy) + ". Use RelockAlways, StayLocked or RelockOnTargetChange.");
		return;
	}
	if (orbit_disabled_warn(bullet_index, "set orbiting lock policy")) {
		return;
	}
	all_orbiting_data[bullet_index].lock_policy = new_lock_policy;
}

BulletVolley2D::OrbitingLockPolicy BulletVolley2D::bullet_get_orbiting_lock_policy(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_lock_policy")) {
		return RelockAlways;
	}
	if (orbit_disabled_warn(bullet_index, "get orbiting lock policy")) {
		return RelockAlways;
	}
	return all_orbiting_data[bullet_index].lock_policy;
}

void BulletVolley2D::bullet_set_orbiting_rigid_follow(int bullet_index, bool new_rigid_follow) {
	if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_rigid_follow")) {
		return;
	}
	if (orbit_disabled_warn(bullet_index, "set orbiting rigid follow")) {
		return;
	}
	all_orbiting_data[bullet_index].rigid_follow = new_rigid_follow;
}

bool BulletVolley2D::bullet_get_orbiting_rigid_follow(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_rigid_follow")) {
		return true;
	}
	if (orbit_disabled_warn(bullet_index, "get orbiting rigid follow")) {
		return true;
	}
	return all_orbiting_data[bullet_index].rigid_follow;
}

void BulletVolley2D::all_bullets_set_orbiting_rigid_follow(bool new_rigid_follow, int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_rigid_follow", [&](int i) { bullet_set_orbiting_rigid_follow(i, new_rigid_follow); });
}

PackedFloat32Array BulletVolley2D::all_bullets_get_orbiting_radius(int bullet_index_start, int bullet_index_end_inclusive) {
	return collect_range<PackedFloat32Array>(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_orbiting_radius", [&](int i) { return bullet_get_orbiting_radius(i); });
}

PackedVector2Array BulletVolley2D::all_bullets_get_orbiting_center(int bullet_index_start, int bullet_index_end_inclusive) {
	return collect_range<PackedVector2Array>(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_orbiting_center", [&](int i) { return bullet_get_orbiting_center(i); });
}

PackedFloat32Array BulletVolley2D::all_bullets_get_orbiting_angle(int bullet_index_start, int bullet_index_end_inclusive) {
	return collect_range<PackedFloat32Array>(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_orbiting_angle", [&](int i) { return bullet_get_orbiting_angle(i); });
}

TypedArray<bool> BulletVolley2D::all_bullets_is_orbiting_enabled(int bullet_index_start, int bullet_index_end_inclusive) {
	return collect_range<TypedArray<bool>>(bullet_index_start, bullet_index_end_inclusive, "all_bullets_is_orbiting_enabled", [&](int i) { return bullet_is_orbiting_enabled(i); });
}

void BulletVolley2D::all_bullets_disable_orbiting(int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_disable_orbiting", [&](int i) { bullet_disable_orbiting(i); });
}

void BulletVolley2D::all_bullets_set_orbiting_radius(real_t new_radius, int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_radius", [&](int i) { bullet_set_orbiting_radius(i, new_radius); });
}

void BulletVolley2D::all_bullets_set_orbiting_direction(OrbitingDirection new_direction, int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_direction", [&](int i) { bullet_set_orbiting_direction(i, new_direction); });
}

void BulletVolley2D::all_bullets_set_orbiting_texture_rotation(OrbitingTextureRotation new_rotation, int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_texture_rotation", [&](int i) { bullet_set_orbiting_texture_rotation(i, new_rotation); });
}

void BulletVolley2D::all_bullets_set_orbiting_follow_mode(OrbitingFollowMode new_follow_mode, int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_follow_mode", [&](int i) { bullet_set_orbiting_follow_mode(i, new_follow_mode); });
}

void BulletVolley2D::all_bullets_set_orbiting_follow_deadzone(real_t new_deadzone, int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_follow_deadzone", [&](int i) { bullet_set_orbiting_follow_deadzone(i, new_deadzone); });
}

void BulletVolley2D::all_bullets_set_orbiting_lock_policy(OrbitingLockPolicy new_lock_policy, int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_lock_policy", [&](int i) { bullet_set_orbiting_lock_policy(i, new_lock_policy); });
}

void BulletVolley2D::all_bullets_set_orbiting_center(const Vector2 &new_center, int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_center", [&](int i) { bullet_set_orbiting_center(i, new_center); });
}

TypedArray<bool> BulletVolley2D::all_bullets_is_orbiting_locked(int bullet_index_start, int bullet_index_end_inclusive) {
	return collect_range<TypedArray<bool>>(bullet_index_start, bullet_index_end_inclusive, "all_bullets_is_orbiting_locked", [&](int i) { return bullet_is_orbiting_locked(i); });
}

} // namespace BlastBullets2D
