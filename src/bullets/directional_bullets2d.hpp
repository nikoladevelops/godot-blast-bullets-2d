#pragma once

#include "../shared/bullet_speed_data2d.hpp"
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
		GlobalPositionTarget,
		Node2DTarget,
		NotHoming
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
		union {
			Vector2 global_position_target;
			Node2DTargetData node2d_target_data;
		};

		HomingTarget(Vector2 pos) :
				type(GlobalPositionTarget), global_position_target(pos) {}
		HomingTarget(Node2D *node, uint64_t id) :
				type(Node2DTarget), node2d_target_data(node, id) {}
	};

	enum HomingBoundaryBehavior {
		StopAtBoundary = 0,
		OrbitLeft,
		OrbitRight
	};

protected:
	// Configuration flags
	bool adjust_direction_based_on_rotation = false;
	bool homing_take_control_of_texture_rotation = false;
	bool are_bullets_homing_towards_mouse_global_position = false;

	// Homing parameters
	double homing_update_interval = 0.0;
	double homing_update_timer = 0.0;
	real_t homing_smoothing = 0.0;
	real_t homing_distance_radius_away_from_target = 0.0f;

	HomingBoundaryBehavior homing_distance_radius_away_from_target_boundary_behavior = StopAtBoundary;

	// Bullet state data
	std::vector<std::deque<HomingTarget>> all_bullet_homing_targets;
	std::vector<Vector2> all_cached_homing_direction;

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

	// Gets the current target position for a bullet
	_ALWAYS_INLINE_ bool get_target_position(int bullet_index, Vector2 &target_pos) const {
		if (are_bullets_homing_towards_mouse_global_position) {
			target_pos = get_global_mouse_position();
			return true;
		}

		const std::deque<HomingTarget> &queue = all_bullet_homing_targets[bullet_index];
		if (queue.empty()) {
			return false;
		}

		const HomingTarget &target = queue.front();
		switch (target.type) {
			case HomingType::GlobalPositionTarget: {
				target_pos = target.global_position_target;
				return true;
			}
			case HomingType::Node2DTarget: {
				if (is_bullet_homing_target_valid(target.node2d_target_data.target, target.node2d_target_data.cached_valid_instance_id)) {
					target_pos = target.node2d_target_data.target->get_global_position();
					return true;
				}
				return false;
			}
			case HomingType::NotHoming: {
				return false;
			}
		}
		return false;
	}

	// Rotates bullet to face target with smoothing
	_ALWAYS_INLINE_ void rotate_to_target(int bullet_index, const Vector2 &diff, real_t max_turn) {
		if (!homing_take_control_of_texture_rotation || diff.length_squared() <= 0.0) {
			return;
		}

		Vector2 to_target = diff.normalized();
		real_t face_angle = to_target.angle();
		real_t current_rot = all_cached_instance_transforms[bullet_index].get_rotation();
		real_t desired_rot = face_angle + cache_texture_rotation_radians;
		real_t delta_rot = desired_rot - current_rot;
		normalize_angle(delta_rot);
		delta_rot = Math::clamp(delta_rot, -max_turn, max_turn);
		rotate_transform_locally(all_cached_instance_transforms[bullet_index], delta_rot);
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
			real_t current_speed = all_cached_velocity[bullet_index].length();
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

	// Applies homing physics to move bullet toward the desired position
	_ALWAYS_INLINE_ void apply_homing_physics(int bullet_index, real_t delta) {
		if (!validate_bullet_index(bullet_index, "apply_homing_physics"))
			return;

		Vector2 &bullet_pos = all_cached_instance_origin[bullet_index];
		Vector2 &homing_dir = all_cached_homing_direction[bullet_index];
		Vector2 &velocity = all_cached_velocity[bullet_index];
		Vector2 &direction = all_cached_direction[bullet_index];
		real_t speed = all_cached_speed[bullet_index];

		if (homing_dir == Vector2(0, 0) || speed <= 0.0f) {
			bullet_pos += velocity * delta;
			return;
		}

		Vector2 target_pos;
		if (!get_target_position(bullet_index, target_pos)) {
			bullet_pos += velocity * delta;
			return;
		}

		Vector2 to_target = target_pos - bullet_pos;
		real_t dist_to_target = to_target.length();
		if (dist_to_target <= 0.0f)
			return;

		Vector2 to_target_dir = to_target / dist_to_target;
		Vector2 desired_pos = target_pos;

		// Maintain radius if configured
		if (homing_distance_radius_away_from_target > 0.0f) {
			desired_pos -= to_target_dir * homing_distance_radius_away_from_target;
		}

		Vector2 to_desired = desired_pos - bullet_pos;
		real_t rem_dist = to_desired.length();
		Vector2 target_dir = (rem_dist > 0.0f) ? to_desired / rem_dist : Vector2(0, 0);

		real_t max_move = speed * delta;

		// If within reach this frame
		if (rem_dist <= max_move) {
			bullet_pos = desired_pos;

			switch (homing_distance_radius_away_from_target_boundary_behavior) {
				case StopAtBoundary:
					homing_dir = Vector2(0, 0);
					velocity = Vector2(0, 0);
					direction = Vector2(0, 0);
					break;
				case OrbitLeft:
				case OrbitRight: {
					homing_dir = Vector2(0, 0);
					real_t angle = (homing_distance_radius_away_from_target_boundary_behavior == OrbitLeft) ? +Math_PI / 2 : -Math_PI / 2;
					Vector2 tangent = to_target_dir.rotated(angle).normalized();
					velocity = tangent * speed;
					direction = tangent;
					break;
				}
			}
			return;
		}

		// Normal homing motion
		if (homing_smoothing <= 0.0f) {
			velocity = target_dir * speed;
			direction = target_dir;
		} else {
			Vector2 current_dir = (velocity.length_squared() <= 0.0f) ? target_dir : velocity.normalized();
			real_t max_turn = homing_smoothing * delta;
			real_t cross = current_dir.x * target_dir.y - current_dir.y * target_dir.x;
			real_t dot = current_dir.dot(target_dir);
			real_t angle_diff = Math::atan2(cross, dot);
			real_t turn = Math::clamp(angle_diff, -max_turn, max_turn);
			current_dir = current_dir.rotated(turn).normalized();
			velocity = current_dir * speed;
			direction = current_dir;
		}

		bullet_pos += velocity * delta;
	}

	// Sets the homing direction towards the target
	_ALWAYS_INLINE_ void set_homing_bullet_direction_towards_target(int bullet_index, const Vector2 &target_global_position) {
		if (!validate_bullet_index(bullet_index, "set_homing_bullet_direction_towards_target")) {
			return;
		}

		Vector2 bullet_pos = all_cached_instance_origin[bullet_index];
		Vector2 diff = target_global_position - bullet_pos;
		real_t dist_squared = diff.length_squared();

		// If exactly on top of target, no meaningful direction
		if (dist_squared <= 0.0) {
			all_cached_homing_direction[bullet_index] = Vector2(0, 0);
			return;
		}

		real_t dist = Math::sqrt(dist_squared);
		Vector2 normalized_diff = diff / dist;

		// Compute desired point:
		// - If radius == 0 => aim at target
		// - If radius > 0 => aim at the point on the target's circle along the same radial line as the bullet
		Vector2 desired_pos = target_global_position;
		if (homing_distance_radius_away_from_target > 0.0f) {
			// target_global_position - normalized_diff * r
			// This yields the point on the circle at distance r from the target,
			// on the same radial line as the bullet (works both when bullet is inside or outside).
			desired_pos = target_global_position - normalized_diff * homing_distance_radius_away_from_target;
		}

		// Direction from bullet to desired point
		Vector2 new_diff = desired_pos - bullet_pos;
		if (new_diff.length_squared() > 0.0) {
			all_cached_homing_direction[bullet_index] = new_diff.normalized();
		} else {
			all_cached_homing_direction[bullet_index] = Vector2(0, 0);
		}
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

	// Updates homing behavior for a bullet
	_ALWAYS_INLINE_ void update_homing(int bullet_index, double delta, bool interval_reached) {
		if (!is_bullet_homing(bullet_index) && !are_bullets_homing_towards_mouse_global_position) {
			return;
		}

		Vector2 target_pos;
		if (!get_target_position(bullet_index, target_pos)) {
			if (interval_reached && !all_bullet_homing_targets[bullet_index].empty()) {
				all_bullet_homing_targets[bullet_index].pop_front();
				all_cached_homing_direction[bullet_index] = Vector2(0, 0);
			}
			return;
		}

		bool is_moving_target = are_bullets_homing_towards_mouse_global_position ||
				get_bullet_homing_current_target_type(bullet_index) == HomingType::Node2DTarget;
		if (interval_reached || is_moving_target) {
			set_homing_bullet_direction_towards_target(bullet_index, target_pos);
		}

		apply_homing_physics(bullet_index, delta);

		Vector2 diff = target_pos - all_cached_instance_origin[bullet_index];
		real_t max_turn = homing_smoothing * static_cast<real_t>(delta);
		if (max_turn < 0.0) {
			max_turn = 0.0;
		}
		rotate_to_target(bullet_index, diff, max_turn);
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
		all_bullet_homing_targets[bullet_index].emplace_back(new_homing_target, new_homing_target->get_instance_id());
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_node2d_target") ||
				!bullets_enabled_status[bullet_index] || new_homing_target == nullptr) {
			return false;
		}
		all_bullet_homing_targets[bullet_index].emplace_front(new_homing_target, new_homing_target->get_instance_id());
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_global_position_target") ||
				!bullets_enabled_status[bullet_index]) {
			return false;
		}
		all_bullet_homing_targets[bullet_index].emplace_back(global_position);
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_global_position_target") ||
				!bullets_enabled_status[bullet_index]) {
			return false;
		}
		all_bullet_homing_targets[bullet_index].emplace_front(global_position);
		return true;
	}

	_ALWAYS_INLINE_ Variant bullet_homing_pop_front_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_front_target") || !is_bullet_homing(bullet_index)) {
			return nullptr;
		}

		HomingTarget target = all_bullet_homing_targets[bullet_index].front();
		all_bullet_homing_targets[bullet_index].pop_front();

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

		HomingTarget target = all_bullet_homing_targets[bullet_index].back();
		all_bullet_homing_targets[bullet_index].pop_back();

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
		all_cached_homing_direction[bullet_index] = Vector2(0, 0);
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
	real_t get_homing_distance_radius_away_from_target() const { return homing_distance_radius_away_from_target; }
	void set_homing_distance_radius_away_from_target(real_t value) { homing_distance_radius_away_from_target = value; }
	HomingBoundaryBehavior get_homing_distance_radius_away_from_target_boundary_behavior() const { return homing_distance_radius_away_from_target_boundary_behavior; }
	void set_homing_distance_radius_away_from_target_boundary_behavior(HomingBoundaryBehavior value) { homing_distance_radius_away_from_target_boundary_behavior = value; }
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
		ClassDB::bind_method(D_METHOD("get_homing_distance_radius_away_from_target"), &DirectionalBullets2D::get_homing_distance_radius_away_from_target);
		ClassDB::bind_method(D_METHOD("set_homing_distance_radius_away_from_target", "value"), &DirectionalBullets2D::set_homing_distance_radius_away_from_target);
		ClassDB::bind_method(D_METHOD("teleport_bullet", "bullet_index", "new_global_pos"), &DirectionalBullets2D::teleport_bullet);
		ClassDB::bind_method(D_METHOD("all_bullets_clear_homing_targets", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_clear_homing_targets, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_push_back_homing_target", "node_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_back_homing_target, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_push_front_homing_target", "node_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_front_homing_target, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_push_back_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_back_homing_targets_array, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_push_front_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_front_homing_targets_array, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_new_target", "node_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_replace_homing_targets_with_new_target, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_new_target_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_replace_homing_targets_with_new_target_array, DEFVAL(0), DEFVAL(-1));

		ClassDB::bind_method(D_METHOD("get_homing_distance_radius_away_from_target_boundary_behavior"), &DirectionalBullets2D::get_homing_distance_radius_away_from_target_boundary_behavior);
		ClassDB::bind_method(D_METHOD("set_homing_distance_radius_away_from_target_boundary_behavior", "value"), &DirectionalBullets2D::set_homing_distance_radius_away_from_target_boundary_behavior);
		ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_distance_radius_away_from_target_boundary_behavior"), "set_homing_distance_radius_away_from_target_boundary_behavior", "get_homing_distance_radius_away_from_target_boundary_behavior");

		ADD_PROPERTY(PropertyInfo(Variant::BOOL, "are_bullets_homing_towards_mouse_global_position"), "set_are_bullets_homing_towards_mouse_global_position", "get_are_bullets_homing_towards_mouse_global_position");
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing"), "set_homing_smoothing", "get_homing_smoothing");
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_update_interval"), "set_homing_update_interval", "get_homing_update_interval");
		ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_take_control_of_texture_rotation"), "set_homing_take_control_of_texture_rotation", "get_homing_take_control_of_texture_rotation");
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_distance_radius_away_from_target"), "set_homing_distance_radius_away_from_target", "get_homing_distance_radius_away_from_target");

		BIND_ENUM_CONSTANT(GlobalPositionTarget);
		BIND_ENUM_CONSTANT(Node2DTarget);
		BIND_ENUM_CONSTANT(NotHoming);

		BIND_ENUM_CONSTANT(StopAtBoundary);
		BIND_ENUM_CONSTANT(OrbitLeft);
		BIND_ENUM_CONSTANT(OrbitRight);
	}
};

} // namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::HomingType);
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::HomingBoundaryBehavior);