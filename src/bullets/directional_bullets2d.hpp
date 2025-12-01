#pragma once

#include "../shared/bullet_speed_data2d.hpp"
#include "../shared/homing_target_deque.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/defs.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/callable.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/variant.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "multimesh_bullets2d.hpp"
#include "shared/bullet_curves_data2d.hpp"
#include "spawn-data/multimesh_bullets_data2d.hpp"

#include <vector>

namespace BlastBullets2D {
using namespace godot;

class DirectionalBullets2D : public MultiMeshBullets2D {
	GDCLASS(DirectionalBullets2D, MultiMeshBullets2D)

public:
	enum HomingBoundaryBehavior {
		BoundaryDontMove = 0,
		BoundaryOrbitLeft,
		BoundaryOrbitRight
	};

	enum HomingBoundaryFacingDirection {
		FaceTarget,
		FaceOppositeTarget,
		FaceOrbitingDirection
	};

	// Updates all bullets' positions, rotations, and homing
	_ALWAYS_INLINE_ void move_bullets(double delta) {
		const bool is_using_physics_interpolation = bullet_factory->use_physics_interpolation;

		update_all_previous_transforms_for_interpolation();

		const bool homing_interval_reached = update_homing_timer(delta);

		// Cache the global mouse position for performance reasons (otherwise I would be fetching it per bullet when it doesn't even change..)
		if (homing_interval_reached && HomingTargetDeque::mouse_homing_targets_amount > 0) { // Update the cache only when homing interval has been reached and only if there are targets that do follow the mouse
			cached_mouse_global_position = get_global_mouse_position();
		}

		const bool shared_homing_deque_enabled = !shared_homing_deque.empty();
		Vector2 homing_bullet_pos;
		Vector2 homing_target_pos;

		const bool shared_curves_data_enabled = shared_bullet_curves_data.is_valid();
		const BulletCurvesData2D *const shared_curves_ptr = shared_bullet_curves_data.ptr();

		bool is_per_bullet_curves_valid = false;
		const BulletCurvesData2D *per_bullet_curves_data = nullptr;

		for (int i = 0; i < amount_bullets; ++i) {
			if (!bullets_enabled_status[i]) {
				continue;
			}

			bool direction_got_updated = false;

			// Handle homing
			if (shared_homing_deque_enabled) { // Using the shared homing deque to update homing
				update_homing(shared_homing_deque, true, i, delta, homing_interval_reached, homing_bullet_pos, homing_target_pos);
				try_to_emit_bullet_homing_target_reached_signal(shared_homing_deque, shared_homing_deque_enabled, i, homing_bullet_pos, homing_target_pos);
				direction_got_updated = true;

			} else if (!all_bullet_homing_targets[i].empty()) { // If no shared homing deque, check if bullets have individual homing dequeues and use those instead
				auto &curr_homing_deque = all_bullet_homing_targets[i];
				update_homing(curr_homing_deque, false, i, delta, homing_interval_reached, homing_bullet_pos, homing_target_pos);
				try_to_emit_bullet_homing_target_reached_signal(curr_homing_deque, shared_homing_deque_enabled, i, homing_bullet_pos, homing_target_pos);
				direction_got_updated = true;
			}

			auto &curr_bullet_transf = all_cached_instance_transforms[i];
			auto &curr_bullet_direction = all_cached_direction[i];

			// Handle direction curves
			if (shared_curves_data_enabled) {
				apply_x_direction_curve(curr_bullet_direction, shared_curves_ptr);
				apply_y_direction_curve(curr_bullet_direction, shared_curves_ptr);
				apply_direction_curve_texture_rotation_if_needed(curr_bullet_direction, curr_bullet_transf, delta, shared_curves_ptr);

				direction_got_updated = true;
			} else {
				per_bullet_curves_data = find_bullet_curves_data(i);
				is_per_bullet_curves_valid = per_bullet_curves_data != nullptr;

				if (is_per_bullet_curves_valid) {
					per_bullet_curves_data = all_bullet_curves_data[i].ptr();

					apply_x_direction_curve(curr_bullet_direction, per_bullet_curves_data);
					apply_y_direction_curve(curr_bullet_direction, per_bullet_curves_data);
					apply_direction_curve_texture_rotation_if_needed(curr_bullet_direction, curr_bullet_transf, delta, per_bullet_curves_data);

					direction_got_updated = true;
				}
			}

			// Handle rotation
			if (shared_curves_data_enabled) {
				update_rotation_using_curve(i, delta, shared_curves_ptr);
				bullet_accelerate_rotation_speed_using_curve(i, delta, shared_curves_ptr);
			} else if (is_per_bullet_curves_valid && per_bullet_curves_data->rotation_speed_curve.is_valid()) {
				update_rotation_using_curve(i, delta, per_bullet_curves_data);
				bullet_accelerate_rotation_speed_using_curve(i, delta, per_bullet_curves_data);
			} else if (is_rotation_data_active) {
				update_rotation(i, delta);
				bullet_accelerate_rotation_speed(i, delta);
			}

			if (adjust_direction_based_on_rotation) {
				curr_bullet_direction = all_cached_instance_transforms[i].columns[0].normalized();
				direction_got_updated = true;
			}

			// The velocity at which the bullet will move this frame
			Vector2 &velocity_delta = all_cached_velocity[i];

			if (direction_got_updated) {
				const real_t cached_speed = all_cached_speed[i];
				velocity_delta = curr_bullet_direction * cached_speed + inherited_velocity_offset;
			}

			velocity_delta *= delta;

			auto &curr_shape_transf = all_cached_shape_transforms[i];
			auto &curr_bullet_origin = all_cached_instance_origin[i];
			auto &curr_shape_origin = all_cached_shape_origin[i];

			// Update the bullet origin and transform
			curr_bullet_origin += velocity_delta;
			curr_bullet_transf.set_origin(curr_bullet_origin);

			// Update the collision shape
			// The shape transform is based on the bullet transform plus an offset so it should always follow it no matter how the bullet moves
			curr_shape_transf = curr_bullet_transf;

			// The user had previously set a collision shape offset relative to the center of the texture, so it needs to be re-calculated by taking into account the new rotation of the bullet
			Vector2 rotated_offset = cache_collision_shape_offset.rotated(curr_shape_transf.get_rotation());

			// Update the shape origin
			curr_shape_origin = curr_bullet_origin + rotated_offset;

			// Update the shape transform origin with the rotated offset
			curr_shape_transf.set_origin(curr_shape_origin);

			// Always update the physics shape (since it doesn't depend on interpolation or anything)
			physics_server->area_set_shape_transform(area, i, curr_shape_transf);

			// Updates bullet attachment and speed
			move_bullet_attachment(velocity_delta, i);

			// Handle bullet speed acceleration
			if (shared_curves_data_enabled) {
				bullet_accelerate_speed_using_curve(i, delta, shared_curves_ptr);
			} else {
				if (is_per_bullet_curves_valid) {
					bullet_accelerate_speed_using_curve(i, delta, per_bullet_curves_data);
				} else {
					bullet_accelerate_speed(i, delta);
				}
			}

			if (!is_using_physics_interpolation) {
				multi->set_instance_transform_2d(i, all_cached_instance_transforms[i]);
			}
		}
	}

	///////////// PER BULLET HOMING DEQUE POP METHODS

	_ALWAYS_INLINE_ Variant bullet_homing_pop_front_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_front_target") || !bullet_check_has_homing_targets(bullet_index)) {
			return nullptr;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		return queue.pop_front_target(cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ Variant bullet_homing_pop_back_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_back_target") || !bullet_check_has_homing_targets(bullet_index)) {
			return nullptr;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		return queue.pop_back_target(cached_mouse_global_position);
	}
	/////////////////////

	//////////////// PER BULLET HOMING DEQUE PUSH METHODS

	_ALWAYS_INLINE_ bool bullet_homing_push_front_mouse_position_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_mouse_position_target") ||
				!bullets_enabled_status[bullet_index]) {
			return false;
		}

		if (HomingTargetDeque::mouse_homing_targets_amount <= 0) {
			cached_mouse_global_position = get_global_mouse_position();
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_front_mouse_position_target(cached_mouse_global_position);

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_node2d_target") ||
				!bullets_enabled_status[bullet_index] || new_homing_target == nullptr) {
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_front_node2d_target(new_homing_target);

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_global_position_target") ||
				!bullets_enabled_status[bullet_index]) {
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_front_global_position_target(global_position);

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_mouse_position_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_mouse_position_target") ||
				!bullets_enabled_status[bullet_index]) {
			return false;
		}

		if (HomingTargetDeque::mouse_homing_targets_amount <= 0) {
			cached_mouse_global_position = get_global_mouse_position();
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_back_mouse_position_target(cached_mouse_global_position);

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_node2d_target") ||
				!bullets_enabled_status[bullet_index] || new_homing_target == nullptr) {
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_back_node2d_target(new_homing_target);

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_global_position_target") ||
				!bullets_enabled_status[bullet_index]) {
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_back_global_position_target(global_position);

		return true;
	}
	/////////////////////////////

	///  PER BULLET HOMING DEQUE HELPERS

	_ALWAYS_INLINE_ void bullet_clear_homing_targets(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_clear_homing_targets")) {
			return;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.clear_homing_targets(cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ void all_bullets_push_back_mouse_position_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_mouse_position_target");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (bullets_enabled_status[i]) {
				bullet_homing_push_back_mouse_position_target(i);
			}
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_mouse_position_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_mouse_position_target");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (bullets_enabled_status[i]) {
				bullet_homing_push_front_mouse_position_target(i);
			}
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_back_homing_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_homing_target");
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (bullets_enabled_status[i]) {
					bullet_homing_push_back_node2d_target(i, node);
				}
			}
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			Vector2 global_pos = node2d_or_global_position;
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (bullets_enabled_status[i]) {
					bullet_homing_push_back_global_position_target(i, global_pos);
				}
			}
		} else {
			UtilityFunctions::push_error("Invalid homing target type in all_bullets_push_back_homing_target");
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_homing_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_homing_target");
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (bullets_enabled_status[i]) {
					bullet_homing_push_front_node2d_target(i, node);
				}
			}
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			Vector2 global_pos = node2d_or_global_position;
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (bullets_enabled_status[i]) {
					bullet_homing_push_front_global_position_target(i, global_pos);
				}
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
		all_bullets_clear_homing_targets(bullet_index_start, bullet_index_end_inclusive);
		all_bullets_push_back_homing_target(node2d_or_global_position, bullet_index_start, bullet_index_end_inclusive);
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_new_target_array");
		all_bullets_clear_homing_targets(bullet_index_start, bullet_index_end_inclusive);
		all_bullets_push_back_homing_targets_array(node2ds_or_global_positions_array, bullet_index_start, bullet_index_end_inclusive);
	}

	_ALWAYS_INLINE_ void all_bullets_clear_homing_targets(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_clear_homing_targets");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (bullets_enabled_status[i] && bullet_check_has_homing_targets(i)) {
				bullet_clear_homing_targets(i);
			}
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
		return shared_homing_deque.pop_front_target(cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ Variant shared_homing_deque_pop_back_target() {
		return shared_homing_deque.pop_back_target(cached_mouse_global_position);
	}

	// SHARED BULLET HOMING DEQUE PUSH METHODS

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_mouse_position_target() {
		if (HomingTargetDeque::mouse_homing_targets_amount <= 0) {
			cached_mouse_global_position = get_global_mouse_position();
		}

		shared_homing_deque.push_front_mouse_position_target(cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_node2d_target(Node2D *new_homing_target) {
		shared_homing_deque.push_front_node2d_target(new_homing_target);
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_global_position_target(const Vector2 &global_position) {
		shared_homing_deque.push_front_global_position_target(global_position);
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_mouse_position_target() {
		if (HomingTargetDeque::mouse_homing_targets_amount <= 0) {
			cached_mouse_global_position = get_global_mouse_position();
		}

		shared_homing_deque.push_back_mouse_position_target(cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_node2d_target(Node2D *new_homing_target) {
		shared_homing_deque.push_back_node2d_target(new_homing_target);
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_global_position_target(const Vector2 &global_position) {
		shared_homing_deque.push_back_global_position_target(global_position);
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
			}
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_clear_homing_targets() {
		shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
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

		// Bullet texture related
		auto &curr_bullet_transf = all_cached_instance_transforms[bullet_index];
		auto &curr_bullet_origin = all_cached_instance_origin[bullet_index];

		// Bullet shape related
		auto &curr_shape_transf = all_cached_shape_transforms[bullet_index];
		auto &curr_shape_origin = all_cached_shape_origin[bullet_index];

		// Update the bullet origin and transform
		curr_bullet_origin = new_global_pos;
		curr_bullet_transf.set_origin(curr_bullet_origin);

		// Update the collision shape
		// The shape transform is based on the bullet transform plus an offset so it should always follow it no matter how the bullet moves
		curr_shape_transf = curr_bullet_transf;

		// The user had previously set a collision shape offset relative to the center of the texture, so it needs to be re-calculated by taking into account the new rotation of the bullet
		Vector2 rotated_offset = cache_collision_shape_offset.rotated(curr_shape_transf.get_rotation());

		// Update the shape origin
		curr_shape_origin = curr_bullet_origin + rotated_offset;

		// Update the shape transform origin with the rotated offset
		curr_shape_transf.set_origin(curr_shape_origin);

		// Instantly apply the updated transforms
		if (bullets_enabled_status[bullet_index]) { // Apply to multi only if the bullet is enabled (if disabled the transform is zero which prevents the multimesh from rendering it)
			multi->set_instance_transform_2d(bullet_index, curr_bullet_transf);
		}

		physics_server->area_set_shape_transform(area, bullet_index, curr_shape_transf);

		// Reset physics interpolation data
		update_bullet_previous_transform_for_interpolation(bullet_index);
	}

	// Shifts a bullet's position by a certain amount
	_ALWAYS_INLINE_ void teleport_shift_bullet(int bullet_index, const Vector2 &shift_amount) {
		if (!validate_bullet_index(bullet_index, "teleport_shift_bullet")) {
			return;
		}

		// Bullet texture related
		auto &curr_bullet_transf = all_cached_instance_transforms[bullet_index];
		auto &curr_bullet_origin = all_cached_instance_origin[bullet_index];

		// Bullet shape related
		auto &curr_shape_transf = all_cached_shape_transforms[bullet_index];
		auto &curr_shape_origin = all_cached_shape_origin[bullet_index];

		// Update the bullet origin and transform
		curr_bullet_origin += shift_amount;
		curr_bullet_transf.set_origin(curr_bullet_origin);

		// Update the collision shape
		// The shape transform is based on the bullet transform plus an offset so it should always follow it no matter how the bullet moves
		curr_shape_transf = curr_bullet_transf;

		// The user had previously set a collision shape offset relative to the center of the texture, so it needs to be re-calculated by taking into account the new rotation of the bullet
		Vector2 rotated_offset = cache_collision_shape_offset.rotated(curr_shape_transf.get_rotation());

		// Update the shape origin
		curr_shape_origin = curr_bullet_origin + rotated_offset;

		// Update the shape transform origin with the rotated offset
		curr_shape_transf.set_origin(curr_shape_origin);

		// Instantly apply the updated transforms
		if (bullets_enabled_status[bullet_index]) { // Apply to multi only if the bullet is enabled (if disabled the transform is zero which prevents the multimesh from rendering it)
			multi->set_instance_transform_2d(bullet_index, curr_bullet_transf);
		}

		physics_server->area_set_shape_transform(area, bullet_index, curr_shape_transf);

		// Reset physics interpolation data
		update_bullet_previous_transform_for_interpolation(bullet_index);
	}

	_ALWAYS_INLINE_ void teleport_shift_all_bullets(const Vector2 &shift_amount, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "teleport_shift_all_bullets");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			teleport_shift_bullet(i, shift_amount);
		}
	}

	// Property getters and setters
	real_t get_homing_smoothing() const { return homing_smoothing; }
	void set_homing_smoothing(real_t value) { homing_smoothing = value; }
	real_t get_homing_update_interval() const { return homing_update_interval; }
	void set_homing_update_interval(real_t value) { homing_update_interval = value; }
	bool get_homing_take_control_of_texture_rotation() const { return homing_take_control_of_texture_rotation; }
	void set_homing_take_control_of_texture_rotation(bool value) { homing_take_control_of_texture_rotation = value; }
	real_t get_homing_boundary_distance_away_from_target() const { return homing_boundary_distance_away_from_target; }
	void set_homing_boundary_distance_away_from_target(real_t value) { homing_boundary_distance_away_from_target = value; }
	HomingBoundaryBehavior get_homing_boundary_behavior() const { return homing_boundary_behavior; }
	void set_homing_boundary_behavior(HomingBoundaryBehavior value) { homing_boundary_behavior = value; }
	HomingBoundaryFacingDirection get_homing_boundary_facing_direction() const { return homing_boundary_facing_direction; }
	void set_homing_boundary_facing_direction(HomingBoundaryFacingDirection value) { homing_boundary_facing_direction = value; }
	bool get_bullet_homing_auto_pop_after_target_reached() const { return bullet_homing_auto_pop_after_target_reached; }
	void set_bullet_homing_auto_pop_after_target_reached(bool value) { bullet_homing_auto_pop_after_target_reached = value; }
	real_t get_distance_from_target_before_considering_as_reached() const { return distance_from_target_before_considering_as_reached; }
	void set_distance_from_target_before_considering_as_reached(real_t value) { distance_from_target_before_considering_as_reached = value; }
	bool get_shared_homing_deque_auto_pop_after_target_reached() const { return shared_homing_deque_auto_pop_after_target_reached; }
	void set_shared_homing_deque_auto_pop_after_target_reached(bool value) { shared_homing_deque_auto_pop_after_target_reached = value; }

	// Virtual methods
	void set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data);
	virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_enable_logic(const MultiMeshBulletsData2D &data) override final;

protected:
	// Configuration flags
	bool adjust_direction_based_on_rotation = false;
	bool homing_take_control_of_texture_rotation = false;
	bool bullet_homing_auto_pop_after_target_reached = false;
	bool shared_homing_deque_auto_pop_after_target_reached = false;

	Vector2 cached_mouse_global_position{ 0, 0 };

	// Homing parameters
	double homing_update_interval = 0.0;
	double homing_update_timer = 0.0;
	real_t homing_smoothing = 0.0;
	real_t homing_boundary_distance_away_from_target = 0.0;

	// Minimum distance (in pixels) from the homing target at which the bullet is considered to have reached it. Once within this distance, the bullet_homing_target_reached signal is emitted
	real_t distance_from_target_before_considering_as_reached = 5.0;

	HomingBoundaryBehavior homing_boundary_behavior = BoundaryDontMove;
	HomingBoundaryFacingDirection homing_boundary_facing_direction = FaceTarget;

	// This tracks each bullet's homing deque - allows each bullet to have its own separate homing targets
	std::vector<HomingTargetDeque> all_bullet_homing_targets;

	// This is a shared homing deque - allows the bullets to share the same target
	HomingTargetDeque shared_homing_deque;

	// Updates homing behavior for a bullet
	_ALWAYS_INLINE_ void update_homing(HomingTargetDeque &homing_deque, bool is_using_shared_homing_deque, int bullet_index, double delta, bool interval_reached, Vector2 &bullet_pos, Vector2 &target_pos) {
		// Trim invalid homing targets (dangling pointers of already freed node2ds etc..)
		homing_deque.bullet_homing_trim_front_invalid_targets(cached_mouse_global_position);

		// If after trimming it's empty then skip homing logic
		if (homing_deque.empty()) {
			return;
		}

		// Refresh cache on interval for dynamic targets - avoids calling godot get methods every physics frame..
		if (interval_reached) {
			homing_deque.refresh_cached_front_target_global_position(cached_mouse_global_position);
		}

		// Get the front target's cached position
		target_pos = homing_deque.get_cached_front_target_global_position();

		bullet_pos = all_cached_instance_origin[bullet_index];
		Vector2 diff = target_pos - bullet_pos;
		
		real_t max_turn = homing_smoothing * delta;
		if (max_turn < 0.0) {
			max_turn = 0.0;
		}

		bool use_smoothing = (homing_smoothing > 0.0); // Hoist for clamp

		Vector2 &current_direction = all_cached_direction[bullet_index];

		auto &curr_transf = all_cached_instance_transforms[bullet_index];

		// If rotation is not active then rotate the bullet by applying smoothing
		if (!is_rotation_data_active) {
			// Rotate toward target with smoothing
			rotate_to_target(bullet_index, diff, max_turn, use_smoothing);

			// Get the new direction based on the rotated transform
			current_direction = curr_transf[0].normalized();
		} else {
			// If rotation is indeed active there is no need to handle rotation yourself, the user wants spinning bullets
			current_direction = diff.normalized();
		}
	}

	// Rotates bullet to face target with smoothing (boundary-agnostic version)
	_ALWAYS_INLINE_ void rotate_to_target(int bullet_index, const Vector2 &diff, real_t max_turn, bool use_smoothing) {
		if (!homing_take_control_of_texture_rotation || diff.length_squared() <= 0.0) {
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

		// Apply smoothing clamp
		if (use_smoothing) {
			delta_rot = Math::clamp(delta_rot, -max_turn, max_turn);
		}

		// Rotate locally
		rotate_transform_locally(all_cached_instance_transforms[bullet_index], delta_rot);
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
	_ALWAYS_INLINE_ void update_rotation_using_curve(int bullet_index, double delta, const BulletCurvesData2D *curves_data) {
		real_t cache_rotation_speed = all_rotation_speed[bullet_index];
		real_t rot_delta = cache_rotation_speed * (real_t)delta;

		// If the curves data is valid it means we are using it, so just apply it
		if (curves_data != nullptr && curves_data->rotation_speed_curve.is_valid()) {
			rotate_transform_locally(all_cached_instance_transforms[bullet_index], rot_delta);
		}
	}

	_ALWAYS_INLINE_ void try_to_emit_bullet_homing_target_reached_signal(HomingTargetDeque &homing_deque, bool is_using_shared_homing_deque, int bullet_index, const Vector2 &bullet_pos, const Vector2 &target_pos) {
		// Reached check: Post-move, direct to actual target
		Vector2 post_to_target = target_pos - bullet_pos;
		real_t post_dist_sq = post_to_target.length_squared();
		real_t threshold_sq = distance_from_target_before_considering_as_reached * distance_from_target_before_considering_as_reached;
		if (post_dist_sq <= threshold_sq) { // Fully squared for perf

			HomingTarget &target = homing_deque.front();

			// Ensure that the signal is emitted only ONCE when the target is reached by the bullet
			if (!target.has_bullet_reached_target) {
				target.has_bullet_reached_target = true;
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

				// Pop the front target automatically if that's what the user wants
				if (is_using_shared_homing_deque) {
					if (shared_homing_deque_auto_pop_after_target_reached) {
						call_deferred("shared_homing_deque_pop_front_target");
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
		angle = Math::wrapf(angle, -Math_PI, Math_PI);
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
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::HomingBoundaryBehavior);
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::HomingBoundaryFacingDirection);
