#pragma once

#include "../shared/bullet_speed_data2d.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/defs.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/callable.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "multimesh_bullets2d.hpp"

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
	_ALWAYS_INLINE_ void move_bullets(double delta) {
		const bool using_physics_interpolation = bullet_factory->use_physics_interpolation;
		if (using_physics_interpolation) {
			all_previous_instance_transf = all_cached_instance_transforms;
			if (is_bullet_attachment_provided) {
				all_previous_attachment_transf = attachment_transforms;
			}
		}

		homing_update_timer -= delta;
		const bool homing_update_interval_reached = homing_update_timer <= 0.0f;
		const bool have_last_target_pos = ((int)bullet_last_known_homing_target_pos.size() == amount_bullets);

		// For every bullet, modify it's properties and behavior
		for (int i = 0; i < amount_bullets; i++) {
			if (!bullets_enabled_status[i]) {
				continue;
			}

			real_t rotation_angle = update_rotation(i, delta);
			update_homing(i, delta, homing_update_interval_reached, have_last_target_pos);
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

	// Teleport a given bullet to a new global position while preserving interpolation correctness.
	_ALWAYS_INLINE_ void teleport_bullet(int bullet_index, const Vector2 &new_global_pos) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			UtilityFunctions::printerr("Bullet index out of bounds in teleport_bullet");
			return;
		}

		if (!bullets_enabled_status[bullet_index]) {
			return;
		}

		temporary_disable_bullet(bullet_index);

		std::deque<HomingTarget> &queue_of_targets = all_bullet_homing_targets[bullet_index]; // TODO will create a queue if it doesnt exist upon access that's bad..

		if (!queue_of_targets.empty()) { // If I don't check, it will be undefined behavior when queue is empty.. and I try to access .front()
			HomingTarget current_bullet_target = queue_of_targets.front();

			switch (current_bullet_target.type) {
				case HomingTarget::GlobalPositionTarget:
					// TODO
					break;
				case HomingTarget::Node2DTarget:
					Node2DTargetData &target_data = current_bullet_target.node2d_target_data;

					Node2D *homing_target = target_data.target;
					uint64_t cached_homing_target_id = target_data.cached_valid_instance_id;

					all_cached_instance_origin[bullet_index] = new_global_pos;
					all_cached_instance_transforms[bullet_index].set_origin(new_global_pos);

					Transform2D shape_t = all_cached_instance_transforms[bullet_index];
					Vector2 rotated_offset = cache_collision_shape_offset.rotated(shape_t.get_rotation());
					shape_t.set_origin(new_global_pos + rotated_offset);
					physics_server->area_set_shape_transform(area, bullet_index, shape_t);

					if (is_bullet_homing_target_valid(homing_target, cached_homing_target_id)) {
						const Vector2 target_pos = homing_target->get_global_position();
						set_homing_bullet_direction_towards_target(bullet_index, target_pos);

						real_t speed = all_cached_velocity[bullet_index].length();
						if (speed <= 0.0f) {
							speed = all_cached_speed[bullet_index];
						}

						all_cached_velocity[bullet_index] = all_cached_homing_direction[bullet_index] * speed;

						// Immediately rotate the bullet transform to face the target
						if (homing_take_control_of_texture_rotation) {
							real_t new_rotation = all_cached_homing_direction[bullet_index].angle();
							real_t delta_rot = new_rotation + cache_texture_rotation_radians - all_cached_instance_transforms[bullet_index].get_rotation();
							normalize_angle(delta_rot);
							rotate_transform_locally(all_cached_instance_transforms[bullet_index], delta_rot);
						}

						if ((int)bullet_last_known_homing_target_pos.size() == amount_bullets) {
							bullet_last_known_homing_target_pos[bullet_index] = target_pos;
						}
					}

					break;
			}
		}

		// Ensure physics interpolation doesn't glich by rendering textures to old transform data..
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
			return 0.0f;
		}
		if (i >= (int)all_rotation_speed.size()) {
			return 0.0f;
		}

		real_t rotation_angle = 0.0f;
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

	_ALWAYS_INLINE_ void update_homing(int bullet_index, double delta, bool interval_reached, bool have_last_target_pos) {
		std::deque<HomingTarget> &queue_of_targets = all_bullet_homing_targets[bullet_index];

		// If there are no homing targets, skip all this logic
		if (queue_of_targets.empty()) {
			return;
		}

		HomingTarget current_bullet_target = queue_of_targets.front();

		switch (current_bullet_target.type) {
			case HomingTarget::GlobalPositionTarget:
				// TODO Behavior when homing towards a vector2d global position

				break;
			case HomingTarget::Node2DTarget:
				// Behavior when homing towards a node2d

				Node2DTargetData &target_data = current_bullet_target.node2d_target_data;

				Node2D *homing_target = target_data.target;
				uint64_t cached_valid_instance_id = target_data.cached_valid_instance_id;

				// In case the node2d was freed/deleted during runtime
				if (!is_bullet_homing_target_valid(homing_target, cached_valid_instance_id)) {
					queue_of_targets.pop_front();
					return;
				}

				// When the homing timer times out update the bullet's direction towards the homing target
				if (interval_reached) {
					const Vector2 target_pos = homing_target->get_global_position();
					set_homing_bullet_direction_towards_target(bullet_index, target_pos);
					if (have_last_target_pos) { // TODO maybe remove?
						bullet_last_known_homing_target_pos[bullet_index] = target_pos;
					}
				}

				Vector2 &velo = all_cached_velocity[bullet_index];
				real_t speed = velo.length();
				if (speed <= 0.0f) {
					return;
				}

				Vector2 current_dir = velo.normalized();
				const Vector2 &target_dir = all_cached_homing_direction[bullet_index];
				if (target_dir.length_squared() == 0.0f) {
					return;
				}

				// If no smoothing then rotation should be instant
				if (homing_smoothing <= 0.0f) {
					velo = target_dir * speed;
					all_cached_direction[bullet_index] = target_dir;

					if (homing_take_control_of_texture_rotation) {
						real_t new_rotation = target_dir.angle();
						real_t delta_rot = new_rotation + cache_texture_rotation_radians - all_cached_instance_transforms[bullet_index].get_rotation();
						normalize_angle(delta_rot);
						rotate_transform_locally(all_cached_instance_transforms[bullet_index], delta_rot);
					}
				} else { // If smoothing value was applied the rotation should be gradual / smoothing effect applied
					real_t angle_diff = current_dir.angle_to(target_dir);
					real_t max_turn = homing_smoothing * delta;
					if (max_turn < 0.0f) {
						max_turn = 0.0f;
					}

					real_t turn = Math::clamp(angle_diff, -max_turn, max_turn);
					Vector2 new_dir = current_dir.rotated(turn);

					velo = new_dir * speed;
					all_cached_direction[bullet_index] = new_dir;

					if (homing_take_control_of_texture_rotation) {
						real_t desired_rot = new_dir.angle();
						real_t delta_rot = desired_rot + cache_texture_rotation_radians - all_cached_instance_transforms[bullet_index].get_rotation();
						normalize_angle(delta_rot);
						rotate_transform_locally(all_cached_instance_transforms[bullet_index], delta_rot);
					}
				}
				break;
		}
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
		ClassDB::bind_method(D_METHOD("bullet_homing_push_node2d_target", "bullet_index", "new_homing_target"), &DirectionalBullets2D::bullet_homing_push_node2d_target);
		ClassDB::bind_method(D_METHOD("bullet_homing_clear_all_targets", "bullet_index"), &DirectionalBullets2D::bullet_homing_clear_all_targets);

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

		ClassDB::bind_method(D_METHOD("multimesh_attach_time_based_function", "time", "callable", "repeat"), &DirectionalBullets2D::multimesh_attach_time_based_function, DEFVAL(false));
		ClassDB::bind_method(D_METHOD("multimesh_detach_time_based_function", "callable"), &DirectionalBullets2D::multimesh_detach_time_based_function);
		ClassDB::bind_method(D_METHOD("multimesh_detach_all_time_based_functions"), &DirectionalBullets2D::multimesh_detach_all_time_based_functions);
	}

	void set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data);

	virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_activate_logic(const MultiMeshBulletsData2D &data) override final;

public:
	_ALWAYS_INLINE_ bool bullet_homing_push_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			UtilityFunctions::printerr("Bullet index out of bounds in push_homing_node2d_target");
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

		if (!UtilityFunctions::is_instance_id_valid(homing_target_instance_id)) {
			return false;
		}

		const Vector2 target_pos = new_homing_target->get_global_position();
		set_homing_bullet_direction_towards_target(bullet_index, target_pos);

		if (homing_take_control_of_texture_rotation) {
			real_t new_rotation = all_cached_homing_direction[bullet_index].angle();
			real_t delta_rot = new_rotation + cache_texture_rotation_radians - all_cached_instance_transforms[bullet_index].get_rotation();
			normalize_angle(delta_rot);
			rotate_transform_locally(all_cached_instance_transforms[bullet_index], delta_rot);
		}

		if (bullet_factory->use_physics_interpolation) {
			all_previous_instance_transf[bullet_index] = all_cached_instance_transforms[bullet_index];

			if (is_bullet_attachment_provided) {
				all_previous_attachment_transf[bullet_index] = attachment_transforms[bullet_index];
			}
		}

		// Create a node2d homing target and push it at the end of the queue
		HomingTarget node2d_target_data(new_homing_target, homing_target_instance_id);
		all_bullet_homing_targets[bullet_index].push_back(node2d_target_data);

		return true;
	}

	// TODO a pause homing method - pause homing temporarily even if there are targets

	_ALWAYS_INLINE_ void bullet_homing_clear_all_targets(int bullet_index) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			UtilityFunctions::printerr("Bullet index out of bounds in stop_bullet_homing");
			return;
		}

		// Clear the queue since it won't have any targets
		all_bullet_homing_targets[bullet_index].clear();

		all_cached_homing_direction[bullet_index] = Vector2(0, 0);
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

private:
	// Store both the pointer to the actual node2d target as well as the instance id - just in case the node gets freed during runtime
	struct Node2DTargetData {
		Node2D *target;
		uint64_t cached_valid_instance_id;

		Node2DTargetData(Node2D *node, uint64_t valid_instance_id) :
				target(node), cached_valid_instance_id(valid_instance_id) {};
	};

	// Store either a global position or an actual node2d* that you need to follow as it moves
	struct HomingTarget {
		enum Type {
			GlobalPositionTarget,
			Node2DTarget 
		} type;

		union {
			godot::Vector2 global_position_target;
			Node2DTargetData node2d_target_data;
		};

		HomingTarget(Vector2 pos) : type(GlobalPositionTarget), global_position_target(pos) {}
        HomingTarget(Node2D *node, uint64_t id) : type(Node2DTarget), node2d_target_data(node, id) {}
	};

	// Timer logic
	struct CustomTimer {
		godot::Callable _callback;
		double _current_time;
		double _initial_time;
		bool _repeating;

		CustomTimer(const godot::Callable &callback, double initial_time, bool repeating) :
				_callback(callback), _current_time(initial_time), _initial_time(initial_time), _repeating(repeating) {};
	};

	_ALWAYS_INLINE_ void multimesh_attach_time_based_function(double time, const Callable &callable, bool repeat = false) {
		call_deferred("_do_attach_time_based_function", time, callable, repeat);
	}

	_ALWAYS_INLINE_ void _do_attach_time_based_function(double time, const Callable &callable, bool repeat) {
		if (time <= 0.0f) {
			UtilityFunctions::printerr("When calling multimesh_attach_time_based_function(), you need to provide a time value that is above 0");
			return;
		}

		if (!callable.is_valid()) {
			UtilityFunctions::printerr("Invalid callable was passed to multimesh_attach_time_based_function()");
			return;
		}

		multimesh_custom_timers.emplace_back(callable, time, repeat);
	}

	_ALWAYS_INLINE_ void multimesh_detach_time_based_function(const Callable &callable) {
		call_deferred("_do_detach_time_based_function", callable);
	}

	_ALWAYS_INLINE_ void _do_detach_time_based_function(const Callable &callable) {
		for (auto it = multimesh_custom_timers.begin(); it != multimesh_custom_timers.end();) {
			if (it->_callback == callable) {
				it = multimesh_custom_timers.erase(it); // Order-preserving
			} else {
				++it;
			}
		}
	}

	_ALWAYS_INLINE_ void multimesh_detach_all_time_based_functions() {
		call_deferred("_do_detach_all_time_based_functions");
	}

	_ALWAYS_INLINE_ void _do_detach_all_time_based_functions() {
		multimesh_custom_timers.clear();
	}

	_ALWAYS_INLINE_ void run_multimesh_custom_timers(double delta) {
		for (auto it = multimesh_custom_timers.begin(); it != multimesh_custom_timers.end();) {
			it->_current_time -= delta;
			if (it->_current_time <= 0.0f) {
				it->_callback.call_deferred(); // Always deferred, non-blocking
				if (it->_repeating) {
					it->_current_time = it->_initial_time;
					++it;
				} else {
					it = multimesh_custom_timers.erase(it);
				}
			} else {
				++it;
			}
		}
	}

	////

	// Stores a queue of targets per each bullet - each bullet can track several targets going from one to the other etc..
	std::vector<std::deque<HomingTarget>> all_bullet_homing_targets;
	std::vector<Vector2> all_cached_homing_direction;
	std::vector<Vector2> bullet_last_known_homing_target_pos;

	// Stores a bunch of timers for the multimesh that should execute
	std::vector<CustomTimer> multimesh_custom_timers;

	double homing_update_interval = 0.0f;
	double homing_update_timer = 0.0f;

	real_t homing_smoothing = 0.0f;
	bool homing_take_control_of_texture_rotation = false;

	_ALWAYS_INLINE_ void set_homing_bullet_direction_towards_target(int bullet_index, const Vector2 &target_global_position) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return;
		}

		Vector2 diff = target_global_position - all_cached_instance_origin[bullet_index];
		if (diff.length_squared() > 0.0f) {
			all_cached_homing_direction[bullet_index] = diff.normalized();
		}
		if ((int)bullet_last_known_homing_target_pos.size() == amount_bullets) { // TODO maybe remove?
			bullet_last_known_homing_target_pos[bullet_index] = target_global_position;
		}
	}

	_ALWAYS_INLINE_ bool is_bullet_homing_target_valid(const Node *const target, const uint64_t cached_instance_id) {
		return target != nullptr && UtilityFunctions::is_instance_id_valid(cached_instance_id);
	};
};
} // namespace BlastBullets2D
