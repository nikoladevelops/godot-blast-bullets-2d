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

	// Store both the pointer to the actual node2d target as well as the instance id - just in case the node gets freed during runtime
	struct Node2DTargetData {
		Node2D *target;
		uint64_t cached_valid_instance_id;

		Node2DTargetData(Node2D *node, uint64_t valid_instance_id) :
				target(node), cached_valid_instance_id(valid_instance_id) {};
	};

	// Store either a global position or an actual node2d* that you need to follow as it moves
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

	_ALWAYS_INLINE_ void move_bullets(double delta) {
		const bool using_physics_interpolation = bullet_factory->use_physics_interpolation;
		if (using_physics_interpolation) {
			all_previous_instance_transf = all_cached_instance_transforms;
			if (is_bullet_attachment_provided) {
				all_previous_attachment_transf = attachment_transforms;
			}
		}

		homing_update_timer -= delta; // It's useless when tracking a global_position instead of a node2d, but it will ensure that teleporting works without any weird issues
		const bool homing_update_interval_reached = homing_update_timer <= 0.0;

		// For every bullet, modify it's properties and behavior
		for (int i = 0; i < amount_bullets; ++i) {
			if (!bullets_enabled_status[i]) {
				continue;
			}

			real_t rotation_angle = update_rotation(i, delta);
			update_homing(i, delta, homing_update_interval_reached);
			update_position(i, delta);
			update_collision_shape(i);
			update_attachment_and_speed(i, delta, rotation_angle);

			// If physics interpolation is disabled then just render it inside physics process
			if (!bullet_factory->use_physics_interpolation) {
				multi->set_instance_transform_2d(i, all_cached_instance_transforms[i]);
			}
		}

		run_multimesh_custom_timers(delta);

		if (homing_update_interval_reached) {
			homing_update_timer = homing_update_interval;
		}
	}

	// Teleports a bullet to a new global position, updating transforms and homing
	_ALWAYS_INLINE_ void teleport_bullet(int bullet_index, const Vector2 &new_global_pos) {
		if (!validate_bullet_index(bullet_index, "teleport_bullet")) {
			return;
		}

		if (!bullets_enabled_status[bullet_index]) {
			return;
		}

		temporary_disable_bullet(bullet_index);

		// Update bullet position and transforms
		all_cached_instance_origin[bullet_index] = new_global_pos;
		all_cached_instance_transforms[bullet_index].set_origin(new_global_pos);

		// Update collision shape transforms
		Transform2D shape_t = all_cached_instance_transforms[bullet_index];
		Vector2 rotated_offset = cache_collision_shape_offset.rotated(shape_t.get_rotation());
		shape_t.set_origin(new_global_pos + rotated_offset);
		physics_server->area_set_shape_transform(area, bullet_index, shape_t);

		// Update interpolation data to prevent glitches
		if (bullet_factory->use_physics_interpolation) {
			all_previous_instance_transf[bullet_index] = all_cached_instance_transforms[bullet_index];
			if (is_bullet_attachment_provided) {
				all_previous_attachment_transf[bullet_index] = attachment_transforms[bullet_index];
			}
		}

		temporary_enable_bullet(bullet_index);
	}

protected:
	bool adjust_direction_based_on_rotation = false;

	_ALWAYS_INLINE_ real_t update_rotation(int i, double delta) {
		if (!is_rotation_active) {
			return 0.0;
		}
		if (i >= (int)all_rotation_speed.size()) {
			return 0.0;
		}

		real_t rotation_angle = 0.0;
		bool max_rotation_speed_reached = accelerate_bullet_rotation_speed(i, delta);
		rotation_angle = all_rotation_speed[i] * delta;

		if (!(max_rotation_speed_reached && stop_rotation_when_max_reached)) {
			rotate_transform_locally(all_cached_instance_transforms[i], rotation_angle);
		}

		if (adjust_direction_based_on_rotation) {
			Vector2 &current_direction = all_cached_direction[i];
			current_direction = all_cached_instance_transforms[i][0].normalized();
			real_t current_speed = all_cached_velocity[i].length();
			all_cached_velocity[i] = current_direction * current_speed;
		}

		return rotation_angle;
	}

	_ALWAYS_INLINE_ void update_position(int i, double delta) {
		Vector2 cache_velocity_calc = all_cached_velocity[i] * delta;
		all_cached_instance_origin[i] += cache_velocity_calc;
		all_cached_shape_origin[i] += cache_velocity_calc;
		all_cached_instance_transforms[i].set_origin(all_cached_instance_origin[i]);
	}

	_ALWAYS_INLINE_ void update_collision_shape(int i) {
		all_cached_shape_transforms[i] = all_cached_instance_transforms[i];
		Vector2 rotated_offset = cache_collision_shape_offset.rotated(all_cached_instance_transforms[i].get_rotation());
		all_cached_shape_transforms[i].set_origin(all_cached_instance_origin[i] + rotated_offset);
		physics_server->area_set_shape_transform(area, i, all_cached_shape_transforms[i]);
	}

	_ALWAYS_INLINE_ void update_attachment_and_speed(int i, double delta, real_t rotation_angle) {
		Vector2 velocity_delta = all_cached_velocity[i] * delta;
		move_bullet_attachment(velocity_delta, i, rotation_angle);
		accelerate_bullet_speed(i, delta);
	}

	_ALWAYS_INLINE_ void normalize_angle(real_t &angle) {
		angle = Math::wrapf(angle, -Math_PI, Math_PI);
	}

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
		ADD_PROPERTY(PropertyInfo(Variant::BOOL, "are_bullets_homing_towards_mouse_global_position"), "set_are_bullets_homing_towards_mouse_global_position", "get_are_bullets_homing_towards_mouse_global_position");

		// Batch methods
		ClassDB::bind_method(D_METHOD("all_bullets_clear_homing_targets", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_clear_homing_targets, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_push_back_homing_target", "node_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_back_homing_target, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_push_front_homing_target", "node_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_front_homing_target, DEFVAL(0), DEFVAL(-1));

		ClassDB::bind_method(D_METHOD("all_bullets_push_back_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_back_homing_targets_array, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_push_front_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_front_homing_targets_array, DEFVAL(0), DEFVAL(-1));

		ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_new_target", "node_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_replace_homing_targets_with_new_target, DEFVAL(0), DEFVAL(-1));
		ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_new_target_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_replace_homing_targets_with_new_target_array, DEFVAL(0), DEFVAL(-1));

		ClassDB::bind_method(D_METHOD("get_homing_smoothing"), &DirectionalBullets2D::get_homing_smoothing);
		ClassDB::bind_method(D_METHOD("set_homing_smoothing", "value"), &DirectionalBullets2D::set_homing_smoothing);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing"), "set_homing_smoothing", "get_homing_smoothing");

		ClassDB::bind_method(D_METHOD("get_homing_update_interval"), &DirectionalBullets2D::get_homing_update_interval);
		ClassDB::bind_method(D_METHOD("set_homing_update_interval", "value"), &DirectionalBullets2D::set_homing_update_interval);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_update_interval"), "set_homing_update_interval", "get_homing_update_interval");

		ClassDB::bind_method(D_METHOD("get_homing_take_control_of_texture_rotation"), &DirectionalBullets2D::get_homing_take_control_of_texture_rotation);
		ClassDB::bind_method(D_METHOD("set_homing_take_control_of_texture_rotation", "value"), &DirectionalBullets2D::set_homing_take_control_of_texture_rotation);
		ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_take_control_of_texture_rotation"), "set_homing_take_control_of_texture_rotation", "get_homing_take_control_of_texture_rotation");

		ClassDB::bind_method(D_METHOD("teleport_bullet", "bullet_index", "new_global_pos"), &DirectionalBullets2D::teleport_bullet);

		BIND_ENUM_CONSTANT(GlobalPositionTarget);
		BIND_ENUM_CONSTANT(Node2DTarget);
		BIND_ENUM_CONSTANT(NotHoming);
	}

	void set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data);

	virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_activate_logic(const MultiMeshBulletsData2D &data) override final;

public:
	_ALWAYS_INLINE_ void apply_homing_physics(int bullet_index, double delta) {
		Vector2 &velo = all_cached_velocity[bullet_index];
		real_t speed = velo.length();
		if (speed <= 0.0) {
			return;
		}

		Vector2 current_dir = velo.normalized();
		const Vector2 &target_dir = all_cached_homing_direction[bullet_index];
		if (target_dir.length_squared() <= 0.0) {
			return;
		}

		if (homing_smoothing <= 0.0) {
			velo = target_dir * speed;
			all_cached_direction[bullet_index] = target_dir;

			if (homing_take_control_of_texture_rotation) {
				real_t new_rotation = target_dir.angle();
				real_t delta_rot = new_rotation + cache_texture_rotation_radians - all_cached_instance_transforms[bullet_index].get_rotation();
				normalize_angle(delta_rot);
				rotate_transform_locally(all_cached_instance_transforms[bullet_index], delta_rot);
			}
		} else {
			real_t cross = current_dir.x * target_dir.y - current_dir.y * target_dir.x;
			real_t dot = current_dir.dot(target_dir);
			real_t angle_diff = Math::atan2(cross, dot);

			real_t max_turn = homing_smoothing * static_cast<real_t>(delta);
			if (max_turn < 0.0) {
				max_turn = 0.0;
			}

			real_t turn = Math::clamp(angle_diff, -max_turn, max_turn);
			Vector2 new_dir = current_dir.rotated(turn).normalized();

			velo = new_dir * speed;
			all_cached_direction[bullet_index] = new_dir;

			if (homing_take_control_of_texture_rotation) {
				real_t desired_rot = new_dir.angle();
				real_t delta_rot = desired_rot + cache_texture_rotation_radians - all_cached_instance_transforms[bullet_index].get_rotation();
				normalize_angle(delta_rot);
				rotate_transform_locally(all_cached_instance_transforms[bullet_index], delta_rot);
			}
		}
	}

	_ALWAYS_INLINE_ void update_homing(int bullet_index, double delta, bool interval_reached) {
		if (!is_bullet_homing(bullet_index) && !are_bullets_homing_towards_mouse_global_position) {
			return;
		}

		// If we are not tracking the mouse and the interval was reached then handle normal homing behavior
		if (!are_bullets_homing_towards_mouse_global_position && interval_reached) {
			Vector2 target_pos;
			std::deque<HomingTarget> &queue_of_targets = all_bullet_homing_targets[bullet_index];
			HomingTarget current_bullet_target = queue_of_targets.front();

			switch (current_bullet_target.type) {
				case HomingType::GlobalPositionTarget: {
					target_pos = current_bullet_target.global_position_target;
					break;
				}
				case HomingType::Node2DTarget: {
					Node2DTargetData &target_data = current_bullet_target.node2d_target_data;
					Node2D *homing_target = target_data.target;
					uint64_t cached_valid_instance_id = target_data.cached_valid_instance_id;

					if (!is_bullet_homing_target_valid(homing_target, cached_valid_instance_id)) {
						queue_of_targets.pop_front();
						return;
					}
					target_pos = homing_target->get_global_position();
					break;
				}
				case HomingType::NotHoming:
					return;
			}

			set_homing_bullet_direction_towards_target(bullet_index, target_pos);
		} else if (are_bullets_homing_towards_mouse_global_position && interval_reached) { // If we are indeed tracking the mouse, then this ovverides the normal homing behavior
			set_homing_bullet_direction_towards_target(bullet_index, get_global_mouse_position());
		}

		// We apply any sort of rotation/smoothing no matter what
		apply_homing_physics(bullet_index, delta);
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_node2d_target")) {
			return false;
		}

		// Avoid trying to push a homing target to a bullet that is already disabled / inside the object pool - there is no point
		if (!bullets_enabled_status[bullet_index]) {
			return false;
		}

		if (new_homing_target == nullptr) {
			return false;
		}

		uint64_t homing_target_instance_id = new_homing_target->get_instance_id();

		// Create a node2d homing target and push it at the end of the queue
		all_bullet_homing_targets[bullet_index].emplace_back(new_homing_target, homing_target_instance_id);

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_node2d_target()")) {
			return false;
		}

		if (!bullets_enabled_status[bullet_index]) {
			return false;
		}
		if (new_homing_target == nullptr) {
			return false;
		}

		uint64_t homing_target_instance_id = new_homing_target->get_instance_id();

		// Create a node2d homing target and push it to the front of the queue
		all_bullet_homing_targets[bullet_index].emplace_front(new_homing_target, homing_target_instance_id);
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_global_position_target()")) {
			return false;
		}

		if (!bullets_enabled_status[bullet_index]) {
			return false;
		}

		all_bullet_homing_targets[bullet_index].emplace_back(global_position);

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_global_position_target()")) {
			return false;
		}

		if (!bullets_enabled_status[bullet_index]) {
			return false;
		}

		all_bullet_homing_targets[bullet_index].emplace_front(global_position);

		return true;
	}

	_ALWAYS_INLINE_ Variant bullet_homing_pop_front_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_front_target()")) {
			return nullptr;
		}

		if (!is_bullet_homing(bullet_index)) {
			return nullptr;
		}

		HomingTarget curr_target = all_bullet_homing_targets[bullet_index].front();
		all_bullet_homing_targets[bullet_index].pop_front();

		switch (curr_target.type) {
			case GlobalPositionTarget:
				return curr_target.global_position_target;
			case Node2DTarget:
				if (!UtilityFunctions::is_instance_id_valid(curr_target.node2d_target_data.cached_valid_instance_id)) {
					return nullptr;
				}
				return curr_target.node2d_target_data.target;
			case NotHoming:
				return nullptr;
		}
		return nullptr;
	}

	_ALWAYS_INLINE_ Variant bullet_homing_pop_back_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_back_target()")) {
			return nullptr;
		}

		if (!is_bullet_homing(bullet_index)) {
			return nullptr;
		}

		HomingTarget curr_target = all_bullet_homing_targets[bullet_index].back();
		all_bullet_homing_targets[bullet_index].pop_back();

		switch (curr_target.type) {
			case GlobalPositionTarget:
				return curr_target.global_position_target;
			case Node2DTarget:
				if (!UtilityFunctions::is_instance_id_valid(curr_target.node2d_target_data.cached_valid_instance_id)) {
					return nullptr;
				}
				return curr_target.node2d_target_data.target;
			case NotHoming:
				return nullptr;
		}
		return nullptr;
	}

	_ALWAYS_INLINE_ void bullet_clear_homing_targets(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_clear_homing_targets()")) {
			return;
		}

		// Clear the queue since it won't have any targets
		all_bullet_homing_targets[bullet_index].clear();

		all_cached_homing_direction[bullet_index] = Vector2(0, 0);
	}

	// Will validate the indexes by changing their values accordingly to never go outside the range of the amount of bullets available in the multimesh. Will display error message if bullet_index_start > bullet_index_end_inclusive to warn the user, and will set the range to the default full range from 0 to amount_bullets
	_ALWAYS_INLINE_ void ensure_indexes_match_amount_bullets_range(int &bullet_index_start, int &bullet_index_end_inclusive, const String &function_name_where_error_occured) {
		if (bullet_index_start < 0 || bullet_index_start >= amount_bullets) {
			bullet_index_start = 0;
		}
		if (bullet_index_end_inclusive < 0 || bullet_index_end_inclusive >= amount_bullets) {
			bullet_index_end_inclusive = amount_bullets - 1;
		}
		if (bullet_index_start > bullet_index_end_inclusive) {
			bullet_index_start = 0;
			bullet_index_end_inclusive = amount_bullets - 1;

			UtilityFunctions::printerr("Invalid index range: bullet_index_start > bullet_index_end_inclusive. Error happened when calling function: ", function_name_where_error_occured);
			return;
		}
	}

	_ALWAYS_INLINE_ void all_bullets_clear_homing_targets(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_clear_homing_targets()");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			// If the bullet is disabled or isn't homing there is no point in editing its target queue so skip it and go to the next
			if (!bullets_enabled_status[i] || !is_bullet_homing(i)) {
				continue;
			}

			bullet_clear_homing_targets(i);
		}
	}

	_ALWAYS_INLINE_ bool validate_bullet_index(int bullet_index, const String &function_name) const {
		if (bullet_index >= amount_bullets || bullet_index < 0) {
			UtilityFunctions::printerr("Invalid bullet index was entered when calling", function_name);
			return false;
		}

		return true;
	}

	_ALWAYS_INLINE_ int get_bullet_homing_targets_amount(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "get_bullet_homing_targets_amount()")) {
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
		if (!validate_bullet_index(bullet_index, "get_bullet_homing_current_target_type()")) {
			return HomingType::NotHoming;
		}

		if (get_bullet_homing_targets_amount(bullet_index) <= 0) {
			return HomingType::NotHoming;
		}

		return all_bullet_homing_targets[bullet_index].front().type;
	}

	_ALWAYS_INLINE_ bool is_bullet_homing_node2d_target_valid(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "is_bullet_homing_node2d_target_valid")) {
			return false;
		}

		return is_bullet_homing(bullet_index) && // if bullet is marked as homing
				get_bullet_homing_current_target_type(bullet_index) == HomingType::Node2DTarget && // if the type is a node2d
				UtilityFunctions::is_instance_id_valid(all_bullet_homing_targets[bullet_index].front().node2d_target_data.cached_valid_instance_id); // if the object id is still valid
	}

	_ALWAYS_INLINE_ Variant get_bullet_current_homing_target(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "get_bullet_current_homing_target")) {
			return nullptr;
		}

		if (!is_bullet_homing(bullet_index)) {
			return nullptr;
		}

		const HomingTarget &curr_target = all_bullet_homing_targets[bullet_index].front();

		switch (curr_target.type) {
			case GlobalPositionTarget:
				return curr_target.global_position_target;

			case Node2DTarget:

				if (!UtilityFunctions::is_instance_id_valid(curr_target.node2d_target_data.cached_valid_instance_id)) {
					return nullptr;
				}
				return curr_target.node2d_target_data.target;

			case NotHoming:
				return nullptr;
				break;
		}

		return nullptr;
	};

	_ALWAYS_INLINE_ void all_bullets_push_back_homing_target(const Variant &node_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_homing_target");

		// Check if its actually a node2d homing target
		Node2D *node = Object::cast_to<Node2D>(node_or_global_position);

		// If it is a node2d
		if (node != nullptr) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				// If the bullet is disabled there is no point in editing its target queue so skip it and go to the next
				if (!bullets_enabled_status[i]) {
					continue;
				}

				bullet_homing_push_back_node2d_target(i, node);
			}
		} else if (node_or_global_position.get_type() == Variant::VECTOR2) { // If it's a Vector2
			Vector2 global_pos = node_or_global_position;

			HomingTarget target(global_pos);

			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				// If the bullet is disabled there is no point in editing its target queue so skip it and go to the next bullet
				if (!bullets_enabled_status[i]) {
					continue;
				}

				// For the current queue of a particular active bullet, just push the new target
				bullet_homing_push_back_global_position_target(i, global_pos);
			}
		} else {
			UtilityFunctions::printerr("Invalid homing target type passed to all_bullets_push_back_homing_target (expected Node2D or Vector2)");
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_homing_target(const Variant &node_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_homing_target");

		// Check if its actually a node2d homing target
		Node2D *node = Object::cast_to<Node2D>(node_or_global_position);

		// If it is a node2d
		if (node != nullptr) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				// If the bullet is disabled there is no point in editing its target queue so skip it and go to the next
				if (!bullets_enabled_status[i]) {
					continue;
				}

				bullet_homing_push_front_node2d_target(i, node);
			}
		} else if (node_or_global_position.get_type() == Variant::VECTOR2) { // If it's a Vector2
			Vector2 global_pos = node_or_global_position;

			HomingTarget target(global_pos);

			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				// If the bullet is disabled there is no point in editing its target queue so skip it and go to the next bullet
				if (!bullets_enabled_status[i]) {
					continue;
				}

				// For the current queue of a particular active bullet, just push the new target
				bullet_homing_push_front_global_position_target(i, global_pos);
			}
		} else {
			UtilityFunctions::printerr("Invalid homing target type passed to all_bullets_push_front_homing_target (expected Node2D or Vector2)");
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_back_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_homing_targets_array");

		// For every target that was given
		for (const Variant &curr_node_or_global_pos : node2ds_or_global_positions_array) {
			// Push it in each queue of each bullet (in the valid range of course)
			all_bullets_push_back_homing_target(curr_node_or_global_pos, bullet_index_start, bullet_index_end_inclusive);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_homing_targets_array");

		// For every target that was given
		for (const Variant &curr_node_or_global_pos : node2ds_or_global_positions_array) {
			// Push it in each queue of each bullet (in the valid range of course)
			all_bullets_push_front_homing_target(curr_node_or_global_pos, bullet_index_start, bullet_index_end_inclusive);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_new_target(const Variant &node_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_new_target");

		// Clear all previous targets that might've been in each bullet's queue
		all_bullets_clear_homing_targets(bullet_index_start, bullet_index_end_inclusive);

		// Push a brand new homing target for each bullet (in each bullet's queue in the valid range)
		all_bullets_push_back_homing_target(node_or_global_position, bullet_index_start, bullet_index_end_inclusive);
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_new_target_array");

		// Clear all previous targets that might've been in each bullet's queue
		all_bullets_clear_homing_targets(bullet_index_start, bullet_index_end_inclusive);

		// For each bullet, push the array of targets
		all_bullets_push_back_homing_targets_array(node2ds_or_global_positions_array, bullet_index_start, bullet_index_end_inclusive);
	}

	real_t get_homing_smoothing() const {
		return homing_smoothing;
	}
	void set_homing_smoothing(real_t value) {
		homing_smoothing = value;
	}
	real_t get_homing_update_interval() const {
		return homing_update_interval;
	}
	void set_homing_update_interval(real_t value) {
		homing_update_interval = value;
	}
	bool get_homing_take_control_of_texture_rotation() const {
		return homing_take_control_of_texture_rotation;
	}
	void set_homing_take_control_of_texture_rotation(bool value) {
		homing_take_control_of_texture_rotation = value;
	}
	bool get_are_bullets_homing_towards_mouse_global_position() const {
		return are_bullets_homing_towards_mouse_global_position;
	}
	void set_are_bullets_homing_towards_mouse_global_position(bool value) {
		are_bullets_homing_towards_mouse_global_position = value;
	}

private:
	// Stores a queue of targets per each bullet - each bullet can track several targets going from one to the other etc..
	std::vector<std::deque<HomingTarget>> all_bullet_homing_targets;
	std::vector<Vector2> all_cached_homing_direction;

	double homing_update_interval = 0.0;
	double homing_update_timer = 0.0;

	real_t homing_smoothing = 0.0;
	bool homing_take_control_of_texture_rotation = false;

	bool are_bullets_homing_towards_mouse_global_position = false;

	_ALWAYS_INLINE_ void set_homing_bullet_direction_towards_target(int bullet_index, const Vector2 &target_global_position) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return;
		}

		Vector2 diff = target_global_position - all_cached_instance_origin[bullet_index];
		if (diff.length_squared() > 0.0) {
			all_cached_homing_direction[bullet_index] = diff.normalized();
		}
	}

	_ALWAYS_INLINE_ bool is_bullet_homing_target_valid(const Node *const target, const uint64_t cached_instance_id) {
		return target != nullptr && UtilityFunctions::is_instance_id_valid(cached_instance_id);
	};
};
} // namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::HomingType);
