// Homing: the per-bullet and shared target queues (HomingTargetDeque), steering
// knobs, reached-target signal and auto-pop. The per-bullet steering step itself
// runs inside move_bullets (bullet_volley2d_tick.cpp).

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

_ALWAYS_INLINE_ void BulletVolley2D::orbit_unlock_on_empty_deque() {
	for (int k = 0; k < (int)all_orbiting_data.size(); ++k) {
		orbit_unlock_on_empty_deque_for_bullet(k);
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
	return collect_range<PackedInt32Array>(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_homing_targets_amount", [&](int i) { return bullet_homing_check_targets_amount(i); });
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
	homing_resync_count(bullet_index);
	orbit_keep_lock_across_replace_for_bullet(bullet_index, queue);
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
	homing_resync_count(bullet_index);
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
	homing_drop_own_targets(bullet_index);
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
	if (bullet_index >= amount_bullets || !all_bullets_enabled_set.contains(bullet_index)) {
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
	homing_drop_own_targets(bullet_index);
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
		homing_drop_own_targets(i);
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
		homing_drop_own_targets(i);
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
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_clear_homing_targets", [&](int i) { bullet_clear_homing_targets(i); });
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
	// A parked volley (every bullet frozen) accepts shared targets for its
	// wake; a pooled one is a stale handle.
	if (is_pooled_in_pool) {
		UtilityFunctions::push_error(String(function_name) + ": this volley is in the pool (its last bullet died), so this handle is stale. Spawn a new volley instead.");
		return true;
	}
	return false;
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
	shared_pop_requested = false;
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
	return collect_range<TypedArray<real_t>>(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_homing_smoothing", [&](int i) { return bullet_get_homing_smoothing(i); });
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
	// A pending shared pop never outlives the targets it would pop.
	shared_pop_requested = false;
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
	bounce_ledger_clear();
	bounce_speed_scaled = false;
	bounce_mask_warning_issued = false;
}

bool BulletVolley2D::dispatch_homing_events() {
	homing_dispatch_scratch.clear();
	homing_dispatch_scratch.swap(homing_reached_events);
	const bool shared_pop = shared_pop_requested;
	const uint64_t shared_identity = shared_pop_identity;
	shared_pop_requested = false;
	const uint64_t self_id = get_instance_id();
	const StringName &reached_signal = CachedStringNames2D::get().bullet_homing_target_reached;
	const StringName &forward_signal = CachedStringNames2D::get().volley_bullet_homing_target_reached;
	for (size_t k = 0; k < homing_dispatch_scratch.size(); ++k) {
		const HomingReachedEvent ev = homing_dispatch_scratch[k];
		// An earlier handler disabled (or disabled + woke) this bullet: the
		// reach belongs to a bullet that is gone.
		if (!all_bullets_enabled_set.contains(ev.bullet_index) || collision_epoch_for_bullet(ev.bullet_index) != ev.epoch) {
			continue;
		}
		Node2D *target = nullptr;
		if (ev.target_instance_id != 0) {
			target = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(ev.target_instance_id)));
		}
		emit_signal(reached_signal, this, ev.bullet_index, target, ev.target_position);
		if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
			return false;
		}
		// Owner spawner forward: a direct emit on the owner (no per-volley
		// connection to maintain across pool reuse and adoption).
		if (owner_spawner_id != 0) {
			Object *owner = ObjectDB::get_instance(ObjectID(owner_spawner_id));
			if (owner != nullptr) {
				owner->emit_signal(forward_signal, this, ev.bullet_index, target, ev.target_position);
				if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
					return false;
				}
			}
		}
		if (is_queued_for_deletion()) {
			homing_dispatch_scratch.clear();
			return true;
		}
		// Per-bullet auto-pop: only while the reached target is still this
		// bullet's front (a target the handler pushed in front is spared).
		if (ev.auto_pop && ev.bullet_index < (int)all_bullet_homing_targets.size() && all_bullets_enabled_set.contains(ev.bullet_index)) {
			HomingTargetDeque &deque = all_bullet_homing_targets[ev.bullet_index];
			if (!deque.empty() && orbit_target_identity(deque) == ev.front_identity) {
				bullet_homing_pop_front_target(ev.bullet_index);
			}
		}
	}
	homing_dispatch_scratch.clear();
	if (shared_pop && !shared_homing_deque.empty() && orbit_target_identity(shared_homing_deque) == shared_identity) {
		shared_homing_deque.pop_front_target(cached_mouse_global_position);
		orbit_route_shared_front_change();
	}
	return true;
}

} // namespace BlastBullets2D
