#pragma once

#include "../shared/bullet_speed_data2d.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/defs.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/print_string.hpp"
#include "godot_cpp/variant/callable.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/variant.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "multimesh_bullets2d.hpp"
#include "spawn-data/multimesh_bullets_data2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class DirectionalBullets2D : public MultiMeshBullets2D {
	GDCLASS(DirectionalBullets2D, MultiMeshBullets2D)

public:
	enum HomingType {
		NotHoming,
		GlobalPositionTarget,
		Node2DTarget,
		MousePositionTarget
	};

	// Stores a Node2D target and its instance ID for validation
	struct Node2DTargetData {
		Node2D *target;
		uint64_t cached_valid_instance_id;

		Node2DTargetData(Node2D *node, uint64_t valid_instance_id) :
				target(node), cached_valid_instance_id(valid_instance_id) {}
	};

	// Represents a homing target, either a global position or a Node2D
	struct HomingTarget {
		HomingType type = HomingType::NotHoming;
		bool has_bullet_reached_target = false;

		union {
			Vector2 global_position_target;
			Node2DTargetData node2d_target_data;
		};

		HomingTarget() :
				type(NotHoming), has_bullet_reached_target(false) {
			global_position_target = Vector2(0, 0); // Safe fallback
		}

		HomingTarget(Vector2 pos) :
				type(GlobalPositionTarget), global_position_target(pos) {}
		HomingTarget(Node2D *node, uint64_t id) :
				type(Node2DTarget), node2d_target_data(node, id) {}
	};

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

protected:
	// Configuration flags
	bool adjust_direction_based_on_rotation = false;
	bool homing_take_control_of_texture_rotation = false;
	bool are_bullets_homing_towards_mouse_global_position = false;
	bool bullet_homing_auto_pop_after_target_reached = false;

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

	// Bullet state data
	std::vector<std::deque<HomingTarget>> all_bullet_homing_targets;

	// For each queue inside all_bullet_homing_targets, caches the current front target's global position to avoid unnecessary fetches
	std::vector<Vector2> cached_bullet_homing_deque_front_target_global_positions;

	// Validates bullet index and logs error if invalid
	_ALWAYS_INLINE_ bool validate_bullet_index(int bullet_index, const String &function_name) const {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			UtilityFunctions::printerr("Invalid bullet index in " + function_name);
			return false;
		}
		return true;
	}

	// Normalizes an angle to [-PI, PI]
	_ALWAYS_INLINE_ void normalize_angle(real_t &angle) const {
		angle = Math::wrapf(angle, -Math_PI, Math_PI);
	}

	// Updates interpolation data for physics
	_ALWAYS_INLINE_ void update_physics_interpolation() {
		if (bullet_factory->use_physics_interpolation) {
			all_previous_instance_transf = all_cached_instance_transforms;
			if (is_bullet_attachment_provided) {
				all_previous_attachment_transf = attachment_transforms;
			}
		}
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

	// Checks whether the bullet is homing a valid target and updates the target_pos with the proper global_position of the target. If the target is invalid node2d it will pop it.
	_ALWAYS_INLINE_ bool get_homing_target_global_position(int bullet_index, Vector2 &target_pos) const {
		if (are_bullets_homing_towards_mouse_global_position) {
			target_pos = cached_mouse_global_position;
			return true;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		if (queue.empty()) {
			return false;
		}

		// Prefer cache for performance
		target_pos = cached_bullet_homing_deque_front_target_global_positions[bullet_index];

		auto &target = queue.front();
		if (target.type == HomingType::Node2DTarget &&
				!is_bullet_homing_target_valid(target.node2d_target_data.target, target.node2d_target_data.cached_valid_instance_id)) {
			return false;
		}

		if (target.type == HomingType::NotHoming) {
			return false;
		}

		return true;
	}

	// Updates bullet rotation based on rotation speed
	_ALWAYS_INLINE_ real_t update_rotation(int bullet_index, double delta) {
		if (!is_rotation_active || bullet_index >= (int)all_rotation_speed.size()) {
			return 0.0;
		}

		bool max_rotation_speed_reached = accelerate_bullet_rotation_speed(bullet_index, delta);
		real_t rotation_angle = all_rotation_speed[bullet_index] * delta;

		if (!(max_rotation_speed_reached && stop_rotation_when_max_reached)) {
			rotate_transform_locally(all_cached_instance_transforms[bullet_index], rotation_angle);
		}

		if (adjust_direction_based_on_rotation) {
			Vector2 &current_direction = all_cached_direction[bullet_index];
			current_direction = all_cached_instance_transforms[bullet_index][0].normalized();

			real_t current_speed = all_cached_speed[bullet_index];
			all_cached_velocity[bullet_index] = current_direction * current_speed;
		}

		return rotation_angle;
	}

	// Updates bullet position based on velocity
	_ALWAYS_INLINE_ void update_position(int bullet_index, double delta) {
		Vector2 velocity_delta = all_cached_velocity[bullet_index] * delta;
		all_cached_instance_origin[bullet_index] += velocity_delta;
		all_cached_shape_origin[bullet_index] += velocity_delta;
		all_cached_instance_transforms[bullet_index].set_origin(all_cached_instance_origin[bullet_index]);
	}

	// Updates collision shape transform
	_ALWAYS_INLINE_ void update_collision_shape(int bullet_index) {
		all_cached_shape_transforms[bullet_index] = all_cached_instance_transforms[bullet_index];
		Vector2 rotated_offset = cache_collision_shape_offset.rotated(all_cached_instance_transforms[bullet_index].get_rotation());
		all_cached_shape_transforms[bullet_index].set_origin(all_cached_instance_origin[bullet_index] + rotated_offset);
		physics_server->area_set_shape_transform(area, bullet_index, all_cached_shape_transforms[bullet_index]);
	}

	// Updates bullet attachment and speed
	_ALWAYS_INLINE_ void update_attachment_and_speed(int bullet_index, double delta, real_t rotation_angle) {
		Vector2 velocity_delta = all_cached_velocity[bullet_index] * delta;
		move_bullet_attachment(velocity_delta, bullet_index, rotation_angle);
		accelerate_bullet_speed(bullet_index, delta);
	}

	// Checks if a homing target is valid
	_ALWAYS_INLINE_ bool is_bullet_homing_target_valid(const Node *target, uint64_t cached_instance_id) const {
		return target != nullptr && UtilityFunctions::is_instance_id_valid(cached_instance_id);
	}

public:
	// Updates all bullets' positions, rotations, and homing
	_ALWAYS_INLINE_ void move_bullets(double delta) {
		update_physics_interpolation();
		bool homing_interval_reached = update_homing_timer(delta);

		// Cache the global mouse position for performance reasons (otherwise I would be fetching it per bullet when it doesn't even change..)
		if (homing_interval_reached && are_bullets_homing_towards_mouse_global_position) {
			cached_mouse_global_position = get_global_mouse_position();
		}

		for (int i = 0; i < amount_bullets; ++i) {
			if (!bullets_enabled_status[i]) {
				continue;
			}

			real_t rotation_angle = update_rotation(i, delta);
			update_homing(i, delta, homing_interval_reached);

			update_position(i, delta);

			update_collision_shape(i);
			update_attachment_and_speed(i, delta, rotation_angle);

			if (!bullet_factory->use_physics_interpolation) {
				multi->set_instance_transform_2d(i, all_cached_instance_transforms[i]);
			}
		}

		run_multimesh_custom_timers(delta);
	}

	_ALWAYS_INLINE_ void bullet_homing_trim_front_invalid_targets(int bullet_index) {
		auto &queue = all_bullet_homing_targets[bullet_index];

		while (!queue.empty()) {
			HomingTarget &target = queue.front();

			switch (target.type) {
				case NotHoming:
				case GlobalPositionTarget:
				case MousePositionTarget:
					return; // valid, stop trimming

				case Node2DTarget:
					if (!UtilityFunctions::is_instance_id_valid(target.node2d_target_data.cached_valid_instance_id)) {
						bullet_homing_pop_front_target(bullet_index);
						continue;
					}

					// As soon as you find an instance that is valid, stop looping
					return;
				default:
					UtilityFunctions::push_error(
							"Unsupported HomingTarget type in bullet_homing_trim_front_invalid_targets");
					return;
			}
		}
	}

	_ALWAYS_INLINE_ void try_to_emit_bullet_homing_target_reached_signal(int bullet_index, const Vector2 &bullet_pos, const Vector2 &target_pos) {
		// Reached check: Post-move, direct to actual target (ignores boundary offset)
		Vector2 post_to_target = target_pos - bullet_pos;
		real_t post_dist_sq = post_to_target.length_squared();
		real_t threshold_sq = distance_from_target_before_considering_as_reached * distance_from_target_before_considering_as_reached;
		if (post_dist_sq <= threshold_sq) { // Fully squared for perf

			// TODO maybe implement this per bullet so it doesn't crash
			if (are_bullets_homing_towards_mouse_global_position) {
				return;
			}

			HomingTarget &target = all_bullet_homing_targets[bullet_index].front();

			// Ensure that the signal is emitted only ONCE when the target is reached by the bullet
			if (!target.has_bullet_reached_target) {
				target.has_bullet_reached_target = true;
				switch (target.type) {
					case GlobalPositionTarget:
						call_deferred("emit_signal", "bullet_homing_target_reached", this, bullet_index, nullptr, target_pos);
						break;
					case Node2DTarget:
						// In case the target instance is freed - will still emit the signal, but with a nullptr as the target
						if (!UtilityFunctions::is_instance_id_valid(target.node2d_target_data.cached_valid_instance_id)) {
							call_deferred("emit_signal", "bullet_homing_target_reached", this, bullet_index, nullptr, target_pos);
							break;
						}

						call_deferred("emit_signal", "bullet_homing_target_reached", this, bullet_index, target.node2d_target_data.target, target_pos);
						break;
					case NotHoming:
						break;
				}

				// Pop the front target automatically if that's what the user wants
				if (bullet_homing_auto_pop_after_target_reached) {
					call_deferred("bullet_homing_pop_front_target", bullet_index);
				}
			}
		}
	}

	// Rotates bullet to face target with smoothing (boundary-agnostic version)
	_ALWAYS_INLINE_ void rotate_to_target(int bullet_index, const Vector2 &diff, real_t max_turn, bool use_smoothing) {
		if (!homing_take_control_of_texture_rotation || diff.length_squared() <= 0.0f) {
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

	// Updates homing behavior for a bullet
	_ALWAYS_INLINE_ void update_homing(int bullet_index, double delta, bool interval_reached) {
		if (!is_bullet_homing(bullet_index) && !are_bullets_homing_towards_mouse_global_position) {
			return;
		}

		real_t cached_speed = all_cached_speed[bullet_index];
		if (cached_speed <= 0.0f) { // Early zero-speed exit
			return;
		}

		bullet_homing_trim_front_invalid_targets(bullet_index);

		Vector2 target_pos;
		if (!get_homing_target_global_position(bullet_index, target_pos)) {
			return;
		}

		// Refresh cache on interval for dynamic targets
		if (interval_reached) {
			const auto &queue = all_bullet_homing_targets[bullet_index];
			if (!queue.empty()) {
				const HomingTarget &front = queue.front();

				switch (front.type) {
					case HomingType::GlobalPositionTarget:
						// No need to refresh cache since the global position will never change
						//cached_bullet_homing_deque_front_target_global_positions[bullet_index] = front.global_position_target;
						break;
					case HomingType::Node2DTarget:
						// In this case the target is 100% valid since get_homing_target_global_position checks it
						cached_bullet_homing_deque_front_target_global_positions[bullet_index] = front.node2d_target_data.target->get_global_position();
						break;
					case HomingType::NotHoming:
						break;
				}
				target_pos = cached_bullet_homing_deque_front_target_global_positions[bullet_index];
			}
		}

		const Vector2 &bullet_pos = all_cached_instance_origin[bullet_index];
		Vector2 diff = target_pos - bullet_pos;
		if (diff.length_squared() <= 0.0f) { // Early exit if already on target
			try_to_emit_bullet_homing_target_reached_signal(bullet_index, bullet_pos, target_pos);
			return;
		}

		real_t max_turn = homing_smoothing * delta;
		if (max_turn < 0.0f) {
			max_turn = 0.0f;
		}

		bool use_smoothing = (homing_smoothing > 0.0f); // Hoist for clamp

		// Rotate toward target with smoothing
		rotate_to_target(bullet_index, diff, max_turn, use_smoothing);

		// Update direction from transform (assume unit-length for perf)
		Vector2 &current_direction = all_cached_direction[bullet_index];

		current_direction = all_cached_instance_transforms[bullet_index][0].normalized();

		// Thrust with cached speed
		if (cached_speed > 0.0f) { // Redundant but explicit
			all_cached_velocity[bullet_index] = current_direction * cached_speed;
		}

		// Emit if target reached
		try_to_emit_bullet_homing_target_reached_signal(bullet_index, bullet_pos, target_pos);
	}

	// Teleports a bullet to a new global position
	_ALWAYS_INLINE_ void teleport_bullet(int bullet_index, const Vector2 &new_global_pos) {
		if (!validate_bullet_index(bullet_index, "teleport_bullet") || !bullets_enabled_status[bullet_index]) {
			return;
		}

		temporary_disable_bullet(bullet_index);

		all_cached_instance_origin[bullet_index] = new_global_pos;
		all_cached_instance_transforms[bullet_index].set_origin(new_global_pos);

		Transform2D shape_t = all_cached_instance_transforms[bullet_index];
		Vector2 rotated_offset = cache_collision_shape_offset.rotated(shape_t.get_rotation());
		shape_t.set_origin(new_global_pos + rotated_offset);
		physics_server->area_set_shape_transform(area, bullet_index, shape_t);

		if (bullet_factory->use_physics_interpolation) {
			all_previous_instance_transf[bullet_index] = all_cached_instance_transforms[bullet_index];
			if (is_bullet_attachment_provided) {
				all_previous_attachment_transf[bullet_index] = attachment_transforms[bullet_index];
			}
		}

		temporary_enable_bullet(bullet_index);
	}

	// Homing target management
	_ALWAYS_INLINE_ bool bullet_homing_push_back_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_node2d_target") ||
				!bullets_enabled_status[bullet_index] || new_homing_target == nullptr) {
			return false;
		}

		auto &bullet_queue = all_bullet_homing_targets[bullet_index];

		// Update the cached global position since it will be used - target is at the front of the queue
		if (bullet_queue.empty()) {
			cached_bullet_homing_deque_front_target_global_positions[bullet_index] = new_homing_target->get_global_position();
		}

		bullet_queue.emplace_back(new_homing_target, new_homing_target->get_instance_id());

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_node2d_target") ||
				!bullets_enabled_status[bullet_index] || new_homing_target == nullptr) {
			return false;
		}

		all_bullet_homing_targets[bullet_index].emplace_front(new_homing_target, new_homing_target->get_instance_id());

		cached_bullet_homing_deque_front_target_global_positions[bullet_index] = new_homing_target->get_global_position();

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_global_position_target") ||
				!bullets_enabled_status[bullet_index]) {
			return false;
		}

		auto &bullet_queue = all_bullet_homing_targets[bullet_index];

		// Update the cached global position since it will be used - target is at the front of the queue
		if (bullet_queue.empty()) {
			cached_bullet_homing_deque_front_target_global_positions[bullet_index] = global_position;
		}

		bullet_queue.emplace_back(global_position);
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_global_position_target") ||
				!bullets_enabled_status[bullet_index]) {
			return false;
		}

		all_bullet_homing_targets[bullet_index].emplace_front(global_position);

		cached_bullet_homing_deque_front_target_global_positions[bullet_index] = global_position;
		return true;
	}

	_ALWAYS_INLINE_ Variant bullet_homing_pop_front_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_front_target") || !is_bullet_homing(bullet_index)) {
			return nullptr;
		}

		auto &bullet_queue = all_bullet_homing_targets[bullet_index];

		uint64_t queue_size = all_bullet_homing_targets[bullet_index].size();

		if (queue_size == 0) {
			return nullptr;
		}

		HomingTarget target = bullet_queue.front();
		bullet_queue.pop_front();

		if (queue_size > 1) { // If the old size was bigger than 1 it means there is an element that will now be the front of the queue
			HomingTarget next_target = bullet_queue.front();

			switch (next_target.type) {
				case GlobalPositionTarget:
					cached_bullet_homing_deque_front_target_global_positions[bullet_index] = next_target.global_position_target;
					break;
				case Node2DTarget:
					if (UtilityFunctions::is_instance_id_valid(target.node2d_target_data.cached_valid_instance_id)) {
						cached_bullet_homing_deque_front_target_global_positions[bullet_index] = next_target.node2d_target_data.target->get_global_position();
					}
					// If its not valid no need to edit cache since it wont be used either way..
					break;
				case NotHoming:
					break;
			}
		}

		switch (target.type) {
			case GlobalPositionTarget: {
				return target.global_position_target;
			}
			case Node2DTarget: {
				if (UtilityFunctions::is_instance_id_valid(target.node2d_target_data.cached_valid_instance_id)) {
					return target.node2d_target_data.target;
				}
				return nullptr;
			}
			case NotHoming: {
				return nullptr;
			}
		}
		return nullptr;
	}

	_ALWAYS_INLINE_ Variant bullet_homing_pop_back_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_back_target") || !is_bullet_homing(bullet_index)) {
			return nullptr;
		}

		auto &bullet_queue = all_bullet_homing_targets[bullet_index];

		uint64_t queue_size = all_bullet_homing_targets[bullet_index].size();

		if (queue_size == 0) {
			return nullptr;
		}

		HomingTarget target = bullet_queue.back();
		bullet_queue.pop_back();

		switch (target.type) {
			case GlobalPositionTarget: {
				return target.global_position_target;
			}
			case Node2DTarget: {
				if (UtilityFunctions::is_instance_id_valid(target.node2d_target_data.cached_valid_instance_id)) {
					return target.node2d_target_data.target;
				}
				return nullptr;
			}
			case NotHoming: {
				return nullptr;
			}
		}
		return nullptr;
	}

	_ALWAYS_INLINE_ void bullet_clear_homing_targets(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_clear_homing_targets")) {
			return;
		}
		all_bullet_homing_targets[bullet_index].clear();
	}

	_ALWAYS_INLINE_ void ensure_indexes_match_amount_bullets_range(int &bullet_index_start, int &bullet_index_end_inclusive, const String &function_name) {
		if (bullet_index_start < 0 || bullet_index_start >= amount_bullets) {
			bullet_index_start = 0;
		}
		if (bullet_index_end_inclusive < 0 || bullet_index_end_inclusive >= amount_bullets) {
			bullet_index_end_inclusive = amount_bullets - 1;
		}
		if (bullet_index_start > bullet_index_end_inclusive) {
			bullet_index_start = 0;
			bullet_index_end_inclusive = amount_bullets - 1;
			UtilityFunctions::printerr("Invalid index range in " + function_name);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_clear_homing_targets(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_clear_homing_targets");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (bullets_enabled_status[i] && is_bullet_homing(i)) {
				bullet_clear_homing_targets(i);
			}
		}
	}

	_ALWAYS_INLINE_ int get_bullet_homing_targets_amount(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "get_bullet_homing_targets_amount")) {
			return 0;
		}
		return static_cast<int>(all_bullet_homing_targets[bullet_index].size());
	}

	_ALWAYS_INLINE_ bool is_bullet_homing(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "is_bullet_homing")) {
			return false;
		}
		return get_bullet_homing_targets_amount(bullet_index) > 0;
	}

	_ALWAYS_INLINE_ HomingType get_bullet_homing_current_target_type(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "get_bullet_homing_current_target_type") ||
				get_bullet_homing_targets_amount(bullet_index) <= 0) {
			return HomingType::NotHoming;
		}
		return all_bullet_homing_targets[bullet_index].front().type;
	}

	_ALWAYS_INLINE_ bool is_bullet_homing_node2d_target_valid(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "is_bullet_homing_node2d_target_valid")) {
			return false;
		}
		return is_bullet_homing(bullet_index) &&
				get_bullet_homing_current_target_type(bullet_index) == HomingType::Node2DTarget &&
				UtilityFunctions::is_instance_id_valid(all_bullet_homing_targets[bullet_index].front().node2d_target_data.cached_valid_instance_id);
	}

	_ALWAYS_INLINE_ Variant get_bullet_current_homing_target(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "get_bullet_current_homing_target") || !is_bullet_homing(bullet_index)) {
			return nullptr;
		}

		const HomingTarget &target = all_bullet_homing_targets[bullet_index].front();
		switch (target.type) {
			case GlobalPositionTarget: {
				return target.global_position_target;
			}
			case Node2DTarget: {
				if (UtilityFunctions::is_instance_id_valid(target.node2d_target_data.cached_valid_instance_id)) {
					return target.node2d_target_data.target;
				}
				return nullptr;
			}
			case NotHoming: {
				return nullptr;
			}
		}
		return nullptr;
	}

	_ALWAYS_INLINE_ void all_bullets_push_back_homing_target(const Variant &node_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_homing_target");
		if (Node2D *node = Object::cast_to<Node2D>(node_or_global_position)) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (bullets_enabled_status[i]) {
					bullet_homing_push_back_node2d_target(i, node);
				}
			}
		} else if (node_or_global_position.get_type() == Variant::VECTOR2) {
			Vector2 global_pos = node_or_global_position;
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (bullets_enabled_status[i]) {
					bullet_homing_push_back_global_position_target(i, global_pos);
				}
			}
		} else {
			UtilityFunctions::printerr("Invalid homing target type in all_bullets_push_back_homing_target");
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_homing_target(const Variant &node_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_homing_target");
		if (Node2D *node = Object::cast_to<Node2D>(node_or_global_position)) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (bullets_enabled_status[i]) {
					bullet_homing_push_front_node2d_target(i, node);
				}
			}
		} else if (node_or_global_position.get_type() == Variant::VECTOR2) {
			Vector2 global_pos = node_or_global_position;
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (bullets_enabled_status[i]) {
					bullet_homing_push_front_global_position_target(i, global_pos);
				}
			}
		} else {
			UtilityFunctions::printerr("Invalid homing target type in all_bullets_push_front_homing_target");
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

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_new_target(const Variant &node_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_new_target");
		all_bullets_clear_homing_targets(bullet_index_start, bullet_index_end_inclusive);
		all_bullets_push_back_homing_target(node_or_global_position, bullet_index_start, bullet_index_end_inclusive);
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_new_target_array");
		all_bullets_clear_homing_targets(bullet_index_start, bullet_index_end_inclusive);
		all_bullets_push_back_homing_targets_array(node2ds_or_global_positions_array, bullet_index_start, bullet_index_end_inclusive);
	}

	// Property getters and setters
	real_t get_homing_smoothing() const { return homing_smoothing; }
	void set_homing_smoothing(real_t value) { homing_smoothing = value; }
	real_t get_homing_update_interval() const { return homing_update_interval; }
	void set_homing_update_interval(real_t value) { homing_update_interval = value; }
	bool get_homing_take_control_of_texture_rotation() const { return homing_take_control_of_texture_rotation; }
	void set_homing_take_control_of_texture_rotation(bool value) { homing_take_control_of_texture_rotation = value; }
	bool get_are_bullets_homing_towards_mouse_global_position() const { return are_bullets_homing_towards_mouse_global_position; }
	void set_are_bullets_homing_towards_mouse_global_position(bool value) { are_bullets_homing_towards_mouse_global_position = value; }
	real_t get_homing_boundary_distance_away_from_target() const { return homing_boundary_distance_away_from_target; }
	void set_homing_boundary_distance_away_from_target(real_t value) { homing_boundary_distance_away_from_target = value; }
	HomingBoundaryBehavior get_homing_boundary_behavior() const { return homing_boundary_behavior; }
	void set_homing_boundary_behavior(HomingBoundaryBehavior value) { homing_boundary_behavior = value; }
	HomingBoundaryFacingDirection get_homing_boundary_facing_direction() const { return homing_boundary_facing_direction; }
	void set_homing_boundary_facing_direction(HomingBoundaryFacingDirection value) { homing_boundary_facing_direction = value; }
	bool get_bullet_homing_auto_pop_after_target_reached() const { return bullet_homing_auto_pop_after_target_reached; }
	void set_bullet_homing_auto_pop_after_target_reached(bool value) { bullet_homing_auto_pop_after_target_reached = value; }

	real_t get_distance_from_target_before_considering_as_reached() const {
		return distance_from_target_before_considering_as_reached;
	}
	void set_distance_from_target_before_considering_as_reached(real_t value) {
		distance_from_target_before_considering_as_reached = value;
	}

	// Virtual methods
	void set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data);
	virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_activate_logic(const MultiMeshBulletsData2D &data) override final;

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("bullet_homing_push_back_node2d_target", "bullet_index", "new_homing_target"), &DirectionalBullets2D::bullet_homing_push_back_node2d_target);
		ClassDB::bind_method(D_METHOD("bullet_homing_push_front_node2d_target", "bullet_index", "new_homing_target"), &DirectionalBullets2D::bullet_homing_push_front_node2d_target);
		ClassDB::bind_method(D_METHOD("bullet_homing_push_back_global_position_target", "bullet_index", "global_position"), &DirectionalBullets2D::bullet_homing_push_back_global_position_target);
		ClassDB::bind_method(D_METHOD("bullet_homing_push_front_global_position_target", "bullet_index", "global_position"), &DirectionalBullets2D::bullet_homing_push_front_global_position_target);
		ClassDB::bind_method(D_METHOD("bullet_clear_homing_targets", "bullet_index"), &DirectionalBullets2D::bullet_clear_homing_targets);
		ClassDB::bind_method(D_METHOD("bullet_homing_pop_back_target", "bullet_index"), &DirectionalBullets2D::bullet_homing_pop_back_target);
		ClassDB::bind_method(D_METHOD("bullet_homing_pop_front_target", "bullet_index"), &DirectionalBullets2D::bullet_homing_pop_front_target);
		ClassDB::bind_method(D_METHOD("get_bullet_homing_targets_amount", "bullet_index"), &DirectionalBullets2D::get_bullet_homing_targets_amount);
		ClassDB::bind_method(D_METHOD("is_bullet_homing", "bullet_index"), &DirectionalBullets2D::is_bullet_homing);
		ClassDB::bind_method(D_METHOD("get_bullet_homing_current_target_type", "bullet_index"), &DirectionalBullets2D::get_bullet_homing_current_target_type);
		ClassDB::bind_method(D_METHOD("is_bullet_homing_node2d_target_valid", "bullet_index"), &DirectionalBullets2D::is_bullet_homing_node2d_target_valid);
		ClassDB::bind_method(D_METHOD("get_bullet_current_homing_target", "bullet_index"), &DirectionalBullets2D::get_bullet_current_homing_target);
		ClassDB::bind_method(D_METHOD("get_are_bullets_homing_towards_mouse_global_position"), &DirectionalBullets2D::get_are_bullets_homing_towards_mouse_global_position);
		ClassDB::bind_method(D_METHOD("set_are_bullets_homing_towards_mouse_global_position", "value"), &DirectionalBullets2D::set_are_bullets_homing_towards_mouse_global_position);
		ClassDB::bind_method(D_METHOD("get_homing_smoothing"), &DirectionalBullets2D::get_homing_smoothing);
		ClassDB::bind_method(D_METHOD("set_homing_smoothing", "value"), &DirectionalBullets2D::set_homing_smoothing);
		ClassDB::bind_method(D_METHOD("get_homing_update_interval"), &DirectionalBullets2D::get_homing_update_interval);
		ClassDB::bind_method(D_METHOD("set_homing_update_interval", "value"), &DirectionalBullets2D::set_homing_update_interval);
		ClassDB::bind_method(D_METHOD("get_homing_take_control_of_texture_rotation"), &DirectionalBullets2D::get_homing_take_control_of_texture_rotation);
		ClassDB::bind_method(D_METHOD("set_homing_take_control_of_texture_rotation", "value"), &DirectionalBullets2D::set_homing_take_control_of_texture_rotation);
		ClassDB::bind_method(D_METHOD("get_homing_boundary_distance_away_from_target"), &DirectionalBullets2D::get_homing_boundary_distance_away_from_target);
		ClassDB::bind_method(D_METHOD("set_homing_boundary_distance_away_from_target", "value"), &DirectionalBullets2D::set_homing_boundary_distance_away_from_target);
		ClassDB::bind_method(D_METHOD("teleport_bullet", "bullet_index", "new_global_pos"), &DirectionalBullets2D::teleport_bullet);
		ClassDB::bind_method(D_METHOD("all_bullets_clear_homing_targets", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_clear_homing_targets, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_push_back_homing_target", "node_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_back_homing_target, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_push_front_homing_target", "node_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_front_homing_target, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_push_back_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_back_homing_targets_array, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_push_front_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_front_homing_targets_array, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_new_target", "node_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_replace_homing_targets_with_new_target, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_new_target_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_replace_homing_targets_with_new_target_array, DEFVAL(0), DEFVAL(-1));

		ClassDB::bind_method(D_METHOD("get_distance_from_target_before_considering_as_reached"), &DirectionalBullets2D::get_distance_from_target_before_considering_as_reached);
		ClassDB::bind_method(D_METHOD("set_distance_from_target_before_considering_as_reached", "value"), &DirectionalBullets2D::set_distance_from_target_before_considering_as_reached);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance_from_target_before_considering_as_reached"), "set_distance_from_target_before_considering_as_reached", "get_distance_from_target_before_considering_as_reached");

		ClassDB::bind_method(D_METHOD("get_homing_boundary_behavior"), &DirectionalBullets2D::get_homing_boundary_behavior);
		ClassDB::bind_method(D_METHOD("set_homing_boundary_behavior", "value"), &DirectionalBullets2D::set_homing_boundary_behavior);
		ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_boundary_behavior"), "set_homing_boundary_behavior", "get_homing_boundary_behavior");

		ClassDB::bind_method(D_METHOD("get_homing_boundary_facing_direction"), &DirectionalBullets2D::get_homing_boundary_facing_direction);
		ClassDB::bind_method(D_METHOD("set_homing_boundary_facing_direction", "value"), &DirectionalBullets2D::set_homing_boundary_facing_direction);
		ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_boundary_facing_direction"), "set_homing_boundary_facing_direction", "get_homing_boundary_facing_direction");

		ClassDB::bind_method(D_METHOD("get_bullet_homing_auto_pop_after_target_reached"), &DirectionalBullets2D::get_bullet_homing_auto_pop_after_target_reached);
		ClassDB::bind_method(D_METHOD("set_bullet_homing_auto_pop_after_target_reached", "value"), &DirectionalBullets2D::set_bullet_homing_auto_pop_after_target_reached);
		ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bullet_homing_auto_pop_after_target_reached"), "set_bullet_homing_auto_pop_after_target_reached", "get_bullet_homing_auto_pop_after_target_reached");

		ADD_PROPERTY(PropertyInfo(Variant::BOOL, "are_bullets_homing_towards_mouse_global_position"), "set_are_bullets_homing_towards_mouse_global_position", "get_are_bullets_homing_towards_mouse_global_position");
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing"), "set_homing_smoothing", "get_homing_smoothing");
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_update_interval"), "set_homing_update_interval", "get_homing_update_interval");
		ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_take_control_of_texture_rotation"), "set_homing_take_control_of_texture_rotation", "get_homing_take_control_of_texture_rotation");
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_boundary_distance_away_from_target"), "set_homing_boundary_distance_away_from_target", "get_homing_boundary_distance_away_from_target");

		BIND_ENUM_CONSTANT(GlobalPositionTarget);
		BIND_ENUM_CONSTANT(Node2DTarget);
		BIND_ENUM_CONSTANT(NotHoming);

		BIND_ENUM_CONSTANT(BoundaryDontMove);
		BIND_ENUM_CONSTANT(BoundaryOrbitLeft);
		BIND_ENUM_CONSTANT(BoundaryOrbitRight);

		BIND_ENUM_CONSTANT(FaceTarget);
		BIND_ENUM_CONSTANT(FaceOppositeTarget);
		BIND_ENUM_CONSTANT(FaceOrbitingDirection);

		ADD_SIGNAL(MethodInfo("bullet_homing_target_reached",
				PropertyInfo(Variant::OBJECT, "multimesh_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
				PropertyInfo(Variant::INT, "bullet_index"),
				PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node2D"),
				PropertyInfo(Variant::VECTOR2, "target_global_position")));
	}
};

} // namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::HomingType);
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::HomingBoundaryBehavior);
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::HomingBoundaryFacingDirection);