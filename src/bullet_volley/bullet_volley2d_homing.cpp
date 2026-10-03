// Homing: the per-bullet and shared target queues (HomingTargetDeque), steering
// knobs, reached-target signal and auto-pop. The per-bullet steering step itself
// runs inside move_bullets (bullet_volley2d_tick.cpp).

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

_ALWAYS_INLINE_ void BulletVolley2D::orbit_unlock_on_empty_deque() {
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

_ALWAYS_INLINE_ void BulletVolley2D::orbit_unlock_on_empty_deque_for_bullet(int bullet_index) {
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

PackedInt32Array BulletVolley2D::all_bullets_get_homing_targets_amount(int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_homing_targets_amount");

	PackedInt32Array arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(bullet_homing_check_targets_amount(i));
	}

	return arr;
}

Variant BulletVolley2D::bullet_homing_pop_front_target(int bullet_index) {
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

Variant BulletVolley2D::bullet_homing_pop_back_target(int bullet_index) {
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

bool BulletVolley2D::bullet_homing_push_front_mouse_position_target(int bullet_index) {
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

bool BulletVolley2D::bullet_homing_push_front_node2d_target(int bullet_index, Node2D *new_homing_target) {
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

bool BulletVolley2D::bullet_homing_push_front_global_position_target(int bullet_index, const Vector2 &global_position) {
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

bool BulletVolley2D::bullet_homing_push_back_mouse_position_target(int bullet_index) {
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

bool BulletVolley2D::bullet_homing_push_back_node2d_target(int bullet_index, Node2D *new_homing_target) {
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

bool BulletVolley2D::bullet_homing_push_back_global_position_target(int bullet_index, const Vector2 &global_position) {
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

bool BulletVolley2D::bullet_homing_push_back_homing_target(int bullet_index, const Variant &node2d_or_global_position) {
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

bool BulletVolley2D::bullet_homing_push_front_homing_target(int bullet_index, const Variant &node2d_or_global_position) {
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

void BulletVolley2D::bullet_clear_homing_targets(int bullet_index) {
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

Array BulletVolley2D::all_bullets_pop_front_target(int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_pop_front_target");
	Array popped_targets;

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		popped_targets.push_back(bullet_homing_pop_front_target(i)); // could push nullptr but that's expected
	}

	return popped_targets;
}

Array BulletVolley2D::all_bullets_pop_back_target(int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_pop_back_target");
	Array popped_targets;

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		popped_targets.push_back(bullet_homing_pop_back_target(i));
	}

	return popped_targets;
}

void BulletVolley2D::all_bullets_push_back_mouse_position_target(int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_mouse_position_target");
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		if (orbit_skip_disabled_in_bulk(i)) {
			continue;
		}
		bullet_homing_push_back_mouse_position_target(i);
	}
}

void BulletVolley2D::all_bullets_push_front_mouse_position_target(int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_mouse_position_target");
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		if (orbit_skip_disabled_in_bulk(i)) {
			continue;
		}
		bullet_homing_push_front_mouse_position_target(i);
	}
}

void BulletVolley2D::all_bullets_push_back_homing_target(const Variant &node2d_or_global_position, int bullet_index_start, int bullet_index_end_inclusive) {
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

void BulletVolley2D::all_bullets_push_front_homing_target(const Variant &node2d_or_global_position, int bullet_index_start, int bullet_index_end_inclusive) {
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

void BulletVolley2D::all_bullets_push_back_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_homing_targets_array");
	for (const Variant &target : node2ds_or_global_positions_array) {
		all_bullets_push_back_homing_target(target, bullet_index_start, bullet_index_end_inclusive);
	}
}

void BulletVolley2D::all_bullets_push_front_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_homing_targets_array");
	for (const Variant &target : node2ds_or_global_positions_array) {
		all_bullets_push_front_homing_target(target, bullet_index_start, bullet_index_end_inclusive);
	}
}

bool BulletVolley2D::bullet_replace_homing_targets_with_new_target(int bullet_index, const Variant &node2d_or_global_position) {
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

void BulletVolley2D::all_bullets_replace_homing_targets_with_new_target(const Variant &node2d_or_global_position, int bullet_index_start, int bullet_index_end_inclusive) {
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

void BulletVolley2D::all_bullets_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array, int bullet_index_start, int bullet_index_end_inclusive) {
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

void BulletVolley2D::all_bullets_replace_homing_targets_with_mouse(int bullet_index_start, int bullet_index_end_inclusive) {
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

void BulletVolley2D::all_bullets_assign_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start, int bullet_index_end_inclusive) {
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

void BulletVolley2D::all_bullets_clear_homing_targets(int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_clear_homing_targets");
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		bullet_clear_homing_targets(i);
	}
}

int BulletVolley2D::bullet_homing_check_targets_amount(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_homing_check_targets_amount")) {
		return 0;
	}

	return all_bullet_homing_targets[bullet_index].get_homing_targets_amount();
}

bool BulletVolley2D::bullet_check_has_homing_targets(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_check_has_homing_targets")) {
		return false;
	}

	return all_bullet_homing_targets[bullet_index].has_homing_targets();
}

HomingType BulletVolley2D::bullet_homing_check_current_target_type(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_homing_check_current_target_type")) {
		return HomingType::NotHoming;
	}

	auto &queue = all_bullet_homing_targets[bullet_index];

	return queue.get_current_target_type();
}

Variant BulletVolley2D::bullet_get_current_homing_target(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_current_homing_target") || !bullet_check_has_homing_targets(bullet_index)) {
		return nullptr;
	}

	auto &queue = all_bullet_homing_targets[bullet_index];

	return queue.get_current_homing_target();
}

Variant BulletVolley2D::shared_homing_deque_pop_front_target() {
	Variant popped = shared_homing_deque.pop_front_target(cached_mouse_global_position);
	orbit_route_shared_front_change();
	return popped;
}

Variant BulletVolley2D::shared_homing_deque_pop_back_target() {
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

_ALWAYS_INLINE_ bool BulletVolley2D::orbit_reject_fully_disabled_volley(const char *function_name) const {
	for (int k = 0; k < amount_bullets; ++k) {
		if (all_bullets_enabled_set.contains(k)) {
			return false;
		}
	}
	UtilityFunctions::push_error(String(function_name) + ": volley has no enabled bullets. Wake a bullet with enable_bullet() first.");
	return true;
}

void BulletVolley2D::shared_homing_deque_push_front_mouse_position_target() {
	// Always refresh on push (see bullet_homing_push_front_mouse_position_target).
	cached_mouse_global_position = get_global_mouse_position();

	if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_front_mouse_position_target")) {
		return;
	}
	if (shared_homing_deque.push_front_mouse_position_target(cached_mouse_global_position)) {
		reset_shared_homing_reached_state();
	}
}

void BulletVolley2D::shared_homing_deque_push_front_node2d_target(Node2D *new_homing_target) {
	if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_front_node2d_target")) {
		return;
	}
	const int before = shared_homing_deque.get_homing_targets_amount();
	shared_homing_deque.push_front_node2d_target(new_homing_target);
	if (shared_homing_deque.get_homing_targets_amount() != before) {
		reset_shared_homing_reached_state();
	}
}

void BulletVolley2D::shared_homing_deque_push_front_global_position_target(const Vector2 &global_position) {
	if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_front_global_position_target")) {
		return;
	}
	if (shared_homing_deque.push_front_global_position_target(global_position)) {
		reset_shared_homing_reached_state();
	}
}

void BulletVolley2D::shared_homing_deque_push_back_mouse_position_target() {
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

void BulletVolley2D::shared_homing_deque_push_back_node2d_target(Node2D *new_homing_target) {
	if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_back_node2d_target")) {
		return;
	}
	const bool was_empty = shared_homing_deque.empty();
	if (shared_homing_deque.push_back_node2d_target(new_homing_target) && was_empty && !shared_homing_deque.empty()) {
		reset_shared_homing_reached_state();
	}
}

void BulletVolley2D::shared_homing_deque_push_back_global_position_target(const Vector2 &global_position) {
	if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_back_global_position_target")) {
		return;
	}
	const bool was_empty = shared_homing_deque.empty();
	if (shared_homing_deque.push_back_global_position_target(global_position) && was_empty) {
		reset_shared_homing_reached_state();
	}
}

void BulletVolley2D::shared_homing_deque_push_back_homing_targets_array(const Array &node2ds_or_global_positions_array) {
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

void BulletVolley2D::shared_homing_deque_push_front_homing_targets_array(const Array &node2ds_or_global_positions_array) {
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

void BulletVolley2D::shared_homing_deque_clear_homing_targets() {
	shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
	reset_shared_homing_reached_state();
	// Unlatch a queued-then-cleared auto-pop: without this the stale
	// flush pops whatever is pushed next, eating a fresh front target.
	shared_auto_pop_queued = false;
	orbit_unlock_on_empty_deque();
}

void BulletVolley2D::shared_homing_deque_replace_homing_targets_with_new_target(const Variant &node2d_or_global_position) {
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

void BulletVolley2D::shared_homing_deque_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array) {
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

void BulletVolley2D::set_homing_smoothing(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("homing_smoothing must be a finite value >= 0 (0 snaps instantly).");
		return;
	}
	homing_smoothing = value;
}

void BulletVolley2D::set_homing_update_interval(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("homing_update_interval must be a finite value >= 0 (0 refreshes every tick).");
		return;
	}
	homing_update_interval = value;
}

void BulletVolley2D::set_homing_distance_before_reached(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("homing_distance_before_reached must be a finite value >= 0.");
		return;
	}
	homing_distance_before_reached = value;
}

real_t BulletVolley2D::bullet_get_homing_smoothing(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_homing_smoothing")) {
		return 0.0;
	}
	if (use_per_bullet_homing_smoothing && bullet_index >= 0 && bullet_index < (int)all_bullet_homing_smoothing.size()) {
		return all_bullet_homing_smoothing[bullet_index];
	}
	return homing_smoothing;
}

void BulletVolley2D::bullet_set_homing_smoothing(int bullet_index, real_t value) {
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

void BulletVolley2D::all_bullets_set_homing_smoothing(real_t value, int bullet_index_start, int bullet_index_end_inclusive) {
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

TypedArray<real_t> BulletVolley2D::all_bullets_get_homing_smoothing(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_homing_smoothing");
	TypedArray<real_t> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(bullet_get_homing_smoothing(i));
	}
	return arr;
}

void BulletVolley2D::set_homing_delay_sec(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolley2D.set_homing_delay_sec: value must be finite and >= 0, keeping the old value.");
		return;
	}
	homing_delay_sec = value;
}

void BulletVolley2D::set_homing_duration_sec(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolley2D.set_homing_duration_sec: value must be finite and >= 0 (0 = infinite), keeping the old value.");
		return;
	}
	homing_duration_sec = value;
}

void BulletVolley2D::set_homing_lose_range_px(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolley2D.set_homing_lose_range_px: value must be finite and >= 0 (0 = unlimited), keeping the old value.");
		return;
	}
	homing_lose_range_px = value;
}

void BulletVolley2D::clear_homing_state_for_teardown() {
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
	// the previous owner's pattern or speed. enable_volley() re-seeds
	// these from spawn data; an enable_bullet() wake has no data, so blank
	// them here to make the pool neutral on every reuse path.
	shared_movement_pattern_curve.unref();
	shared_movement_pattern_face_movement_direction = false;
	shared_movement_pattern_repeat = true;
	shared_movement_pattern_distances.assign(shared_movement_pattern_distances.size(), 0.0);
	shared_bullet_speed_data.unref();
	shared_bullet_rotation_data.unref();
	// Bounce is per-owner runtime state like homing: a pooled instance
	// must not ricochet the next owner off stale layers. Vectors drop
	// entirely while disarmed (see ensure_bounce_vectors).
	bounce_mask = 0;
	bounce_strength = 1.0;
	bounce_tilemap_layers = false;
	bounce_push_assist = true;
	bounce_charge_amplify = true;
	bounce_hit_consumed = false;
	bounce_max_count = 0;
	bounce_mode = 0;
	bounce_rotate_texture = true;
	bounce_rotation_smooth = 0.0;
	bounce_randomness_deg = 0.0;
	bounce_cooldown_sec = 0.05;
	bounce_debounce_sec = 0.15;
	all_bounce_count.clear();
	all_bounce_cooldown.clear();
	all_bounce_last_tick.clear();
	all_bounce_last_target.clear();
	all_bounce_last_time.clear();
	bounce_visual_pending.clear();
	bounce_visual_target.clear();
	all_bounce_speed_multiplier.clear();
	bounce_speed_scaled = false;
	bounce_mask_warning_issued = false;
}

void BulletVolley2D::_do_emit_homing_target_reached(uint64_t p_generation, int p_bullet_index, uint64_t p_bullet_epoch, uint64_t p_target_instance_id, const Vector2 &p_target_global_position) {
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
	emit_signal(CachedStringNames2D::get().bullet_homing_target_reached, this, p_bullet_index, target, p_target_global_position);
}

void BulletVolley2D::_do_shared_auto_pop_front_target(uint64_t p_generation, uint64_t p_front_epoch) {
	if (p_generation != homing_operation_generation) {
		return; // Never touch the flag: it belongs to the new life now.
	}
	// The deque's front changed since this pop was queued, so a manual
	// push/pop/clear already superseded it. Unlatch (so a later real reach
	// can queue again) but do NOT pop: popping here would eat the target
	// the user just queued deliberately.
	if (p_front_epoch != shared_homing_front_epoch) {
		shared_auto_pop_queued = false;
		return;
	}
	shared_auto_pop_queued = false;
	shared_homing_deque.pop_front_target(cached_mouse_global_position);
	// orbit_route_shared_front_change() calls reset_shared_homing_reached_state(),
	// which is the single place that bumps shared_homing_front_epoch - so the
	// pop itself does not need (and must not add) a second bump.
	orbit_route_shared_front_change();
}

void BulletVolley2D::_do_auto_pop_front_target(uint64_t p_generation, int p_bullet_index, uint64_t p_bullet_epoch) {
	if (p_generation != homing_operation_generation) {
		return;
	}
	if (p_bullet_index < 0 || p_bullet_index >= (int)bullet_homing_epochs.size() || bullet_homing_epochs[p_bullet_index] != p_bullet_epoch) {
		return;
	}
	bullet_homing_pop_front_target(p_bullet_index);
}

} // namespace BlastBullets2D
