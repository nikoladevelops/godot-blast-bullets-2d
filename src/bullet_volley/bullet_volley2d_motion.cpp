// Linear motion state: speed, direction, velocity, transforms, texture rotation and
// spin (rotation data), plus the shared fallbacks and per-bullet presence bits.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletVolley2D::set_inherited_velocity_offset(const Vector2 &new_offset) {
	if (!new_offset.is_finite()) {
		UtilityFunctions::push_error("set_inherited_velocity_offset: offset must be finite, keeping the old value.");
		return;
	}
	inherited_velocity_offset = new_offset;
	// Recompose live velocities now: the tick only recomputes when the
	// direction changes, so without this the getter stays stale until
	// the next steer (forever while paused or fully disabled).
	for (size_t k = 0; k < all_cached_velocity.size() && k < all_cached_direction.size() && k < all_cached_speed.size(); ++k) {
		all_cached_velocity[k] = all_cached_direction[k] * all_cached_speed[k] + inherited_velocity_offset;
	}
}

real_t BulletVolley2D::bullet_get_rotation_speed(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_rotation_speed")) {
		return 0.0;
	}
	if (bullet_index < 0 || bullet_index >= (int)all_rotation_speed.size()) {
		return 0.0;
	}
	return all_rotation_speed[bullet_index];
}

_ALWAYS_INLINE_ BulletCurvesData2D *BulletVolley2D::find_bullet_curves_data_ptr(int bullet_index) const {
	if (bullet_index < 0 || bullet_index >= (int)all_bullet_curves_data.size()) {
		return nullptr;
	}
	const Ref<BulletCurvesData2D> &r = all_bullet_curves_data[bullet_index];
	if (r.is_null()) {
		return nullptr;
	}
	return r.ptr();
}

void BulletVolley2D::bullet_set_velocity(int bullet_index, const Vector2 &new_velocity) {
	if (!validate_bullet_index(bullet_index, "bullet_set_velocity")) {
		return;
	}

	if (!new_velocity.is_finite()) {
		UtilityFunctions::push_error("bullet_set_velocity: new_velocity must be finite.");
		return;
	}
	// Near-zero or singular basis = degenerate transform (e.g. zero-scale
	// teleport): normalizing it would silently stall the bullet at the
	// inherited offset. A (0, 1) scale passes a columns[0] length check
	// but has determinant 0, so centralise on the invertibility check.
	// Reject like set_bullet_transform does instead.
	if (!is_transform_invertible_safe(all_cached_instance_transforms[bullet_index])) {
		UtilityFunctions::push_error("bullet_set_velocity: bullet transform is degenerate (zero or singular scale), direction is undefined. Fix the transform first (set_bullet_transform).");
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

	if (bullet_index < 0 || bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_speed.size() || bullet_index >= (int)all_cached_max_speed.size() || bullet_index >= (int)all_cached_velocity.size()) {
		return;
	}
	if (new_speed > 0.0001) {
		all_cached_direction[bullet_index] = without_offset / new_speed;
	}

	all_cached_speed[bullet_index] = new_speed;
	if (all_cached_max_speed[bullet_index] < new_speed) {
		all_cached_max_speed[bullet_index] = new_speed;
	}
	all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * new_speed + inherited_velocity_offset;
}

void BulletVolley2D::all_bullets_set_velocity(const Vector2 &new_velocity, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_velocity");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		bullet_set_velocity(i, new_velocity);
	}
}

TypedArray<Vector2> BulletVolley2D::all_bullets_get_velocity(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_velocity");

	TypedArray<Vector2> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		if (i < 0 || i >= (int)all_cached_velocity.size()) {
			arr.push_back(Vector2());
			continue;
		}
		arr.push_back(all_cached_velocity[i]);
	}

	return arr;
}

void BulletVolley2D::mark_per_bullet_rotation_presence(int bullet_index, bool present) {
	if (bullet_index < 0 || bullet_index >= (int)has_per_bullet_rotation_data.size()) {
		// Grow on demand: the seed loop marks before anything sized the
		// vector, and a late mark must not silently vanish.
		if (bullet_index >= 0 && bullet_index < amount_bullets) {
			has_per_bullet_rotation_data.resize(amount_bullets, 0);
		} else {
			return;
		}
	}
	has_per_bullet_rotation_data[bullet_index] = present ? 1 : 0;
}

void BulletVolley2D::mark_per_bullet_speed_presence(int bullet_index, bool present) {
	if (bullet_index < 0 || bullet_index >= (int)has_per_bullet_speed_data.size()) {
		if (bullet_index >= 0 && bullet_index < amount_bullets) {
			has_per_bullet_speed_data.resize(amount_bullets, 0);
		} else {
			return;
		}
	}
	has_per_bullet_speed_data[bullet_index] = present ? 1 : 0;
}

int BulletVolley2D::resolve_strict_data_index(int array_size, int bullet_index) const {
	if (array_size <= 0) {
		return -1;
	}
	if (bullet_index >= 0 && bullet_index < array_size) {
		return bullet_index;
	}
	return -1;
}

int BulletVolley2D::resolve_tiled_data_index(int array_size, int bullet_index) const {
	if (array_size <= 0) {
		return -1;
	}
	if (bullet_index < 0) {
		return -1;
	}
	return bullet_index % array_size;
}

void BulletVolley2D::set_shared_bullet_speed_data(const Ref<BulletSpeedData2D> &new_speed_data) {
	shared_bullet_speed_data = new_speed_data;
	if (new_speed_data.is_null()) {
		return;
	}
	apply_shared_speed_fallback(new_speed_data);
}

void BulletVolley2D::set_shared_bullet_rotation_data(const Ref<BulletRotationData2D> &new_rotation_data) {
	shared_bullet_rotation_data = new_rotation_data;
	if (new_rotation_data.is_null()) {
		return;
	}
	apply_shared_rotation_fallback(new_rotation_data, rotate_only_textures);
}

// OTHER

void BulletVolley2D::set_rotation_data(const TypedArray<BulletRotationData2D> &rotation_data, bool new_rotate_only_textures, bool tile_short_arrays) {
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
		WarnOnce2D::warn(warn_data_id, 4u, amount_rotation_data, amount_bullets, "BulletVolley2D: all_bullet_rotation_data size (" + String::num_int64(amount_rotation_data) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets get zero spin unless shared rotation fills them" + String(tile_short_arrays ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_rotation_data to wrap, or provide one entry per bullet)."));
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

Ref<BulletRotationData2D> BulletVolley2D::get_bullet_rotation_data(int bullet_index) const {
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

void BulletVolley2D::set_bullet_rotation_data(int bullet_index, const Ref<BulletRotationData2D> &new_bullet_rotation_data) {
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

TypedArray<BulletRotationData2D> BulletVolley2D::all_bullets_get_rotation_data(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_rotation_data");

	TypedArray<BulletRotationData2D> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_rotation_data(i));
	}

	return arr;
}

void BulletVolley2D::all_bullets_set_rotation_data(const Ref<BulletRotationData2D> &new_bullet_rotation_data, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_rotation_data");

	if (new_bullet_rotation_data.is_null()) {
		UtilityFunctions::push_error("all_bullets_set_rotation_data: new_bullet_rotation_data is null.");
		return;
	}

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_rotation_data(i, new_bullet_rotation_data);
	}
}

void BulletVolley2D::clear_bullet_rotation_data() {
	is_rotation_data_active = false;
	all_rotation_speed.clear();
	all_max_rotation_speed.clear();
	all_rotation_acceleration.clear();
	// Presence must go with the values: without this, the volley keeps stale
	// per-bullet bits and a later shared fallback skips slots that are now
	// genuine gaps (rotation cleared means "no seed", not "authored zero").
	reset_per_bullet_rotation_presence();
}

Ref<BulletSpeedData2D> BulletVolley2D::get_bullet_speed_data(int bullet_index) const {
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

void BulletVolley2D::set_bullet_speed_data(int bullet_index, const Ref<BulletSpeedData2D> &new_bullet_speed_data) {
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

TypedArray<BulletSpeedData2D> BulletVolley2D::all_bullets_get_speed_data(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_speed_data");

	TypedArray<BulletSpeedData2D> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_speed_data(i));
	}

	return arr;
}

void BulletVolley2D::all_bullets_set_speed_data(const Ref<BulletSpeedData2D> &new_bullet_speed_data, int bullet_index_start, int bullet_index_end_inclusive) {
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

Vector2 BulletVolley2D::get_bullet_direction(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_direction")) {
		return Vector2();
	}

	if (bullet_index < 0 || bullet_index >= (int)all_cached_direction.size()) {
		return Vector2();
	}
	return all_cached_direction[bullet_index];
}

void BulletVolley2D::set_bullet_direction(int bullet_index, const Vector2 &new_direction) {
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

TypedArray<Vector2> BulletVolley2D::all_bullets_get_direction(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_direction");

	TypedArray<Vector2> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_direction(i));
	}

	return arr;
}

void BulletVolley2D::all_bullets_set_direction(const Vector2 &new_direction, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_direction");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_direction(i, new_direction);
	}
}

real_t BulletVolley2D::get_bullet_texture_rotation_radians(int bullet_index) const {
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

void BulletVolley2D::set_bullet_texture_rotation_radians(int bullet_index, real_t new_rotation_radians) {
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

TypedArray<real_t> BulletVolley2D::all_bullets_get_texture_rotation_radians(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_texture_rotation_radians");

	TypedArray<real_t> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_texture_rotation_radians(i));
	}

	return arr;
}

void BulletVolley2D::all_bullets_set_texture_rotation_radians(real_t new_rotation_radians, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_texture_rotation_radians");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_texture_rotation_radians(i, new_rotation_radians);
	}
}

real_t BulletVolley2D::get_bullet_texture_rotation_degrees(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_texture_rotation_degrees")) {
		return 0.0;
	}
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return 0.0;
	}

	return Math::rad_to_deg(get_bullet_texture_rotation_radians(bullet_index));
}

void BulletVolley2D::set_bullet_texture_rotation_degrees(int bullet_index, real_t new_rotation_degrees) {
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

TypedArray<real_t> BulletVolley2D::all_bullets_get_texture_rotation_degrees(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_texture_rotation_degrees");

	TypedArray<real_t> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_texture_rotation_degrees(i));
	}

	return arr;
}

void BulletVolley2D::all_bullets_set_texture_rotation_degrees(real_t new_rotation_degrees, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_texture_rotation_degrees");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_texture_rotation_degrees(i, new_rotation_degrees);
	}
}

Transform2D BulletVolley2D::get_bullet_transform(int bullet_index) const {
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

Transform2D BulletVolley2D::get_bullet_global_transform(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_global_transform")) {
		return Transform2D();
	}
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return Transform2D();
	}

	return all_cached_instance_transforms[bullet_index];
}

Vector2 BulletVolley2D::get_bullet_velocity(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_velocity")) {
		return Vector2();
	}

	if (bullet_index < 0 || bullet_index >= (int)all_cached_velocity.size()) {
		return Vector2();
	}
	return all_cached_velocity[bullet_index];
}

void BulletVolley2D::set_bullet_transform(int bullet_index, const Transform2D &new_transform, bool set_direction_based_on_transform) {
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

TypedArray<Transform2D> BulletVolley2D::all_bullets_get_transforms(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_transforms");

	TypedArray<Transform2D> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_transform(i));
	}

	return arr;
}

void BulletVolley2D::all_bullets_set_transforms(const Transform2D &new_transform, bool set_direction_based_on_transform, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_transforms");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_transform(i, new_transform, set_direction_based_on_transform);
	}
}

void BulletVolley2D::set_bullet_direction_towards_position(int bullet_index, const Vector2 &target_position) {
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

void BulletVolley2D::all_bullets_set_direction_towards_position(const Vector2 &target_position, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_direction_towards_position");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_direction_towards_position(i, target_position);
	}
}

void BulletVolley2D::set_bullet_direction_towards_node2d(int bullet_index, const Node2D *target_node) {
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

void BulletVolley2D::all_bullets_set_direction_towards_node2d(const Node2D *target_node, int bullet_index_start, int bullet_index_end_inclusive) {
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

void BulletVolley2D::set_bullet_texture_rotation_towards_position(int bullet_index, const Vector2 &target_position) {
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

void BulletVolley2D::all_bullets_set_texture_rotation_towards_position(const Vector2 &target_position, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_texture_rotation_towards_position");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_texture_rotation_towards_position(i, target_position);
	}
}

void BulletVolley2D::set_bullet_texture_rotation_towards_node2d(int bullet_index, const Node2D *target_node) {
	if (!validate_bullet_index(bullet_index, "set_bullet_texture_rotation_towards_node2d"))
		return;

	if (target_node == nullptr) {
		UtilityFunctions::push_error("The target_node provided to set_bullet_texture_rotation_towards_node2d is null. Cannot set texture rotation towards a null node.");
		return;
	}

	const Vector2 target_position = target_node->get_global_position();
	set_bullet_texture_rotation_towards_position(bullet_index, target_position);
}

void BulletVolley2D::all_bullets_set_texture_rotation_towards_node2d(const Node2D *target_node, int bullet_index_start, int bullet_index_end_inclusive) {
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

void BulletVolley2D::set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data, bool tile_short_arrays) {
	int speed_data_size = new_speed_data.size();

	// Ensure vectors are the correct size before we start indexing
	if ((int)all_cached_speed.size() != amount_bullets) {
		all_cached_speed.resize(amount_bullets);
		all_cached_max_speed.resize(amount_bullets);
		all_cached_acceleration.resize(amount_bullets);
		all_cached_direction.resize(amount_bullets);
		all_cached_velocity.resize(amount_bullets);
	}
	has_per_bullet_speed_data.assign(amount_bullets, 0);
	// Fresh ballistics every seed - leftover fall speed from the last owner would make the new volley drop instantly.
	all_gravity_velocity.assign(amount_bullets, Vector2(0, 0));

	// Strict rule: entry i drives bullet i only. Slots past the end keep
	// zeros here and fall back to shared afterwards (see the gap filler
	// below). With the tile checkbox, short arrays wrap (i % size).
	const bool exact_speed = (speed_data_size == amount_bullets);
	const bool tiled_speed = tile_short_arrays && speed_data_size > 0 && !exact_speed;
	Ref<BulletSpeedData2D> fallback_data;

	if (!exact_speed) {
		if (tiled_speed && speed_data_size > 0) {
			fallback_data = new_speed_data[0];
		}
		if (speed_data_size != 0 && !exact_speed) {
			WarnOnce2D::warn(warn_data_id, 5u, speed_data_size, amount_bullets, "BulletVolley2D: all_bullet_speed_data size (" + String::num_int64(speed_data_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets fall back to shared/default" + String(tile_short_arrays ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_speed_data to wrap, or provide one entry per bullet)."));
		}

		// In case no speed data was provided at all, create a default one (everything set to 0 by default)
		if (fallback_data.is_null()) {
			fallback_data.instantiate();
		}
	}

	for (int i = 0; i < amount_bullets; ++i) {
		const real_t rot = all_cached_shape_transforms[i].get_rotation();

		Ref<BulletSpeedData2D> data = fallback_data;
		// True only when this slot reads an entry the USER authored (exact,
		// tiled, or in-range short array). A synthesized default below is not
		// a user intent, so it must not claim presence - otherwise a volley
		// with no per-bullet speed data at all would freeze permanently
		// instead of picking up shared speed.
		bool user_authored_entry = false;

		if (exact_speed) {
			data = new_speed_data[i];
			user_authored_entry = true;
		} else if (tiled_speed && speed_data_size > 0) {
			data = new_speed_data[i % speed_data_size];
			user_authored_entry = true;
		} else if (i >= 0 && i < speed_data_size) {
			data = new_speed_data[i];
			user_authored_entry = true;
		}

		// Extract values with null safety
		real_t s = 0.0, m = 0.0, acc = 0.0;

		if (data.is_valid()) {
			s = data->speed;
			m = data->max_speed;
			acc = data->acceleration;
		}

		// Non-finite values would poison the tick path, so fail open to zeros like a missing entry.
		bool valid_entry = user_authored_entry && data.is_valid();
		if (!Math::is_finite(s) || !Math::is_finite(m) || !Math::is_finite(acc)) {
			UtilityFunctions::push_error("BulletVolley2D movement data contains NaN/Inf, using zeros for bullet index " + String::num_int64(i) + ".");
			s = 0.0;
			m = 0.0;
			acc = 0.0;
			valid_entry = false;
		}

		// Overwrite existing memory slots
		all_cached_speed[i] = s;
		all_cached_max_speed[i] = m;
		all_cached_acceleration[i] = acc;
		// Explicit presence: a VALID entry (even all-zero "don't move") must
		// never be mistaken for a gap by the shared fallback below.
		has_per_bullet_speed_data[i] = valid_entry ? 1 : 0;

		Vector2 dir = Vector2(Math::cos(rot), Math::sin(rot));
		all_cached_direction[i] = dir;
		all_cached_velocity[i] = (dir * s) + inherited_velocity_offset;
	}
}

void BulletVolley2D::apply_shared_speed_fallback(const Ref<BulletSpeedData2D> &shared) {
	if (shared.is_null() || !Math::is_finite(shared->speed) || !Math::is_finite(shared->max_speed) || !Math::is_finite(shared->acceleration)) {
		UtilityFunctions::push_error("BulletVolley2D shared_bullet_speed_data contains NaN/Inf, ignoring shared fallback.");
		return;
	}
	if ((int)all_cached_speed.size() != amount_bullets) {
		return;
	}
	// The vector is sized in set_up_movement_data, but the fallback can also
	// run on a volley whose movement data was never seeded (or was cleared), so
	// normalize the size here rather than trusting the seed path.
	if ((int)has_per_bullet_speed_data.size() != amount_bullets) {
		has_per_bullet_speed_data.assign(amount_bullets, 0);
	}
	for (int i = 0; i < amount_bullets; ++i) {
		// Presence, not a zero-triple test. Bit 1 covers BOTH a valid per-bullet
		// entry and a slot this fallback already filled, so shared data never
		// overwrites a deliberate all-zero ("don't move") and never re-fills.
		// Bit 0 is a genuine gap (invalid entry, or never seeded).
		if (has_per_bullet_speed_data[i]) {
			continue;
		}
		if (shared->speed == 0.0 && shared->max_speed == 0.0 && shared->acceleration == 0.0) {
			continue;
		}
		all_cached_speed[i] = shared->speed;
		all_cached_max_speed[i] = shared->max_speed;
		all_cached_acceleration[i] = shared->acceleration;
		// Fill-once: mark the slot so a later set_shared_* does not re-apply.
		has_per_bullet_speed_data[i] = 1;
		const real_t rot = (i >= 0 && i < (int)all_cached_shape_transforms.size()) ? all_cached_shape_transforms[i].get_rotation() : 0.0;
		Vector2 dir = Vector2(Math::cos(rot), Math::sin(rot));
		all_cached_direction[i] = dir;
		all_cached_velocity[i] = (dir * shared->speed) + inherited_velocity_offset;
	}
}

void BulletVolley2D::apply_shared_rotation_fallback(const Ref<BulletRotationData2D> &shared, bool new_rotate_only_textures) {
	if (shared.is_null() || !Math::is_finite(shared->rotation_speed) || !Math::is_finite(shared->max_rotation_speed) || !Math::is_finite(shared->rotation_acceleration)) {
		UtilityFunctions::push_error("BulletVolley2D shared_bullet_rotation_data contains NaN/Inf, ignoring shared fallback.");
		return;
	}
	// No rotation seeded at all (empty array path): shared seeds everything.
	// Fan out to all N slots directly (a 1-entry set_rotation_data call
	// would only cover slot 0 under strict indexing). This also activates
	// rotation so the tick spins every slot, not just slot 0.
	if (!is_rotation_data_active) {
		if ((int)all_rotation_speed.size() != amount_bullets) {
			all_rotation_speed.assign(amount_bullets, 0.0);
			all_max_rotation_speed.assign(amount_bullets, 0.0);
			all_rotation_acceleration.assign(amount_bullets, 0.0);
		}
		has_per_bullet_rotation_data.assign(amount_bullets, 1);
		for (int i = 0; i < amount_bullets; ++i) {
			all_rotation_speed[i] = shared->rotation_speed;
			all_max_rotation_speed[i] = shared->max_rotation_speed;
			all_rotation_acceleration[i] = shared->rotation_acceleration;
		}
		is_rotation_data_active = true;
		rotate_only_textures = new_rotate_only_textures;
		return;
	}
	// Rotation active from per-bullet seeding: only fill slots the seed marked
	// as gaps. Presence, not a zero-triple test - a valid all-zero entry
	// ("no spin") must survive, and a slot this fallback already filled must
	// not be re-filled.
	if ((int)all_rotation_speed.size() != amount_bullets) {
		return;
	}
	if ((int)has_per_bullet_rotation_data.size() != amount_bullets) {
		has_per_bullet_rotation_data.assign(amount_bullets, 0);
	}
	bool filled_any = false;
	for (int i = 0; i < amount_bullets; ++i) {
		if (has_per_bullet_rotation_data[i]) {
			continue;
		}
		if (shared->rotation_speed == 0.0 && shared->max_rotation_speed == 0.0 && shared->rotation_acceleration == 0.0) {
			continue;
		}
		all_rotation_speed[i] = shared->rotation_speed;
		all_max_rotation_speed[i] = shared->max_rotation_speed;
		all_rotation_acceleration[i] = shared->rotation_acceleration;
		has_per_bullet_rotation_data[i] = 1;
		filled_any = true;
	}
	// Visual follow mode only changes when the fallback actually filled
	// something; flipping it on a no-op write would silently re-target
	// shapes mid-flight.
	if (filled_any) {
		rotate_only_textures = new_rotate_only_textures;
	}
}

} // namespace BlastBullets2D
