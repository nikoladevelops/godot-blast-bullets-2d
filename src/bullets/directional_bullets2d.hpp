#pragma once

#include "../shared/bullet_speed_data2d.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/core/defs.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "multimesh_bullets2d.hpp"

#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>

namespace BlastBullets2D {
using namespace godot;

class DirectionalBullets2D : public MultiMeshBullets2D {
	GDCLASS(DirectionalBullets2D, MultiMeshBullets2D)
public:
	_ALWAYS_INLINE_ void move_bullets(float delta) {
		// Cache first-rotation result for single-rotation mode
		float cache_first_rotation_result = 0.0f;
		bool max_rotation_speed_reached = false;
		if (is_rotation_active && use_only_first_rotation_data) {
			max_rotation_speed_reached = accelerate_bullet_rotation_speed(0, delta);
			cache_first_rotation_result = all_rotation_speed[0] * delta;
		}

		// Snapshot previous transforms for interpolation (only once per step)
		bool using_physics_interpolation = bullet_factory->use_physics_interpolation;
		if (using_physics_interpolation) {
			all_previous_instance_transf = all_cached_instance_transforms;
			if (is_bullet_attachment_provided) {
				all_previous_attachment_transf = attachment_transforms;
			}
		}

		// Tick homing timer - this only controls sampling rate of the target position
		homing_update_timer -= delta;
		bool homing_update_interval_reached = homing_update_timer <= 0.0f;

		// Localize config values for speed
		const float local_smoothing = homing_smoothing;
		const float local_rotation_radius = homing_rotation_radius;
		const float local_distance_threshold = homing_teleport_distance_threshold;
		const float local_angle_threshold = homing_teleport_angle_threshold;
		const float PI = 3.14159265358979323846f;

		const bool have_last_target_pos = (int)bullet_last_known_homing_target_pos.size() == amount_bullets;

		for (int i = 0; i < amount_bullets; ++i) {
			if (!bullets_enabled_status[i]) {
				continue;
			}

			Transform2D &curr_instance_transf = all_cached_instance_transforms[i];
			Transform2D &curr_shape_transf = all_cached_shape_transforms[i];
			Vector2 &curr_instance_origin = all_cached_instance_origin[i];
			Vector2 &curr_shape_origin = all_cached_shape_origin[i];

			// Previous transform for this bullet (snapshot from array)
			Transform2D prev_instance_transf;
			if (using_physics_interpolation) {
				prev_instance_transf = all_previous_instance_transf[i];
			}

			const Node2D *const homing_target = bullet_homing_targets[i];

			// Rotation when not controlled by homing
			float rotation_angle = 0.0f;
			if (is_rotation_active && (homing_target == nullptr || !homing_take_control_of_texture_rotation)) {
				if (!use_only_first_rotation_data) {
					max_rotation_speed_reached = accelerate_bullet_rotation_speed(i, delta);
					rotation_angle = all_rotation_speed[i] * delta;
				} else {
					rotation_angle = cache_first_rotation_result;
				}

				if (!(max_rotation_speed_reached && stop_rotation_when_max_reached)) {
					rotate_transform_locally(curr_instance_transf, rotation_angle);
				}

				if (adjust_direction_based_on_rotation) {
					Vector2 &current_direction = all_cached_direction[i];
					current_direction = curr_instance_transf[0].normalized();
					float current_speed = all_cached_velocity[i].length();
					all_cached_velocity[i] = current_direction * current_speed;
				}
			}

			// Homing: refresh target direction only on interval, but steer every frame
			if (UtilityFunctions::is_instance_id_valid(bullet_homing_target_instance_ids[i]) && homing_target != nullptr) {
				if (homing_update_interval_reached) {
					const Vector2 target_pos = homing_target->get_global_position();
					set_homing_bullet_direction_towards_target(i, target_pos);
					if (have_last_target_pos) {
						bullet_last_known_homing_target_pos[i] = target_pos;
					}
				}

				Vector2 &velo = all_cached_velocity[i];
				float speed = velo.length();
				if (speed > 0.0f) {
					Vector2 current_dir = velo.normalized();
					const Vector2 &target_dir = all_cached_homing_direction[i];

					// ignore invalid cached target direction
					if (target_dir.length_squared() > 0.0f) {
						// instant homing (snap) when smoothing <= 0
						if (local_smoothing <= 0.0f) {
							velo = target_dir * speed;
							all_cached_direction[i] = target_dir;

							if (homing_take_control_of_texture_rotation) {
								float new_rotation = target_dir.angle();
								// rotate by the delta to reach exact requested rotation
								float delta_rot = new_rotation + cache_texture_rotation_radians - curr_instance_transf.get_rotation();
								rotate_transform_locally(curr_instance_transf, delta_rot);
							}
						} else {
							// smoothed homing interpreted as max angular speed (radians/sec)
							float angle_diff = current_dir.angle_to(target_dir);
							float max_turn = local_smoothing * delta;

							// optionally scale turning speed when inside a radius
							if (local_rotation_radius > 0.0f && have_last_target_pos) {
								float dist = (bullet_last_known_homing_target_pos[i] - curr_instance_origin).length();
								float t = 1.0f - Math::clamp(dist / local_rotation_radius, 0.0f, 1.0f);
								// linear interpolation between 1.0 and radius_multiplier
								float radius_mult = 1.0f + (homing_rotation_radius_speed_multiplier - 1.0f) * t;
								max_turn *= radius_mult;
							}

							float turn = Math::clamp(angle_diff, -max_turn, max_turn);

							// apply incremental rotation to velocity and optionally to visual rotation
							Vector2 new_dir = current_dir.rotated(turn);
							velo = new_dir * speed;
							all_cached_direction[i] = new_dir;

							if (homing_take_control_of_texture_rotation) {
								rotate_transform_locally(curr_instance_transf, turn);
							}
						}
					}
				} else {
					// disable homing if bullet has no speed to avoid undefined behavior
					bullet_homing_targets[i] = nullptr;
					bullet_homing_target_instance_ids[i] = 0;
				}
			} else {
				// If the instance id is invalid, clear any dangling pointers/ids
				if (!UtilityFunctions::is_instance_id_valid(bullet_homing_target_instance_ids[i])) {
					bullet_homing_targets[i] = nullptr;
					bullet_homing_target_instance_ids[i] = 0;
				}
			}

			// update position using current velocity
			Vector2 cache_velocity_calc = all_cached_velocity[i] * delta;
			curr_instance_origin += cache_velocity_calc;
			curr_shape_origin += cache_velocity_calc;

			curr_instance_transf.set_origin(curr_instance_origin);

			// rebuild collision-shape transform from instance transform + rotated offset
			curr_shape_transf = curr_instance_transf;
			Vector2 rotated_offset = cache_collision_shape_offset.rotated(curr_instance_transf.get_rotation());
			curr_shape_transf.set_origin(curr_instance_origin + rotated_offset);

			// interpolation safety: avoid sweeping across huge jumps by resetting previous transform per-bullet
			if (using_physics_interpolation) {
				float translation_delta = (curr_instance_transf.get_origin() - prev_instance_transf.get_origin()).length();
				if (translation_delta > local_distance_threshold) {
					all_previous_instance_transf[i] = curr_instance_transf;
					if (is_bullet_attachment_provided) {
						all_previous_attachment_transf[i] = attachment_transforms[i];
					}
				} else {
					float prev_rot = prev_instance_transf.get_rotation();
					float curr_rot = curr_instance_transf.get_rotation();
					float rot_delta = curr_rot - prev_rot;
					while (rot_delta <= -PI) rot_delta += 2.0f * PI;
					while (rot_delta > PI) rot_delta -= 2.0f * PI;
					if (std::fabs(rot_delta) > local_angle_threshold) {
						all_previous_instance_transf[i] = curr_instance_transf;
						if (is_bullet_attachment_provided) {
							all_previous_attachment_transf[i] = attachment_transforms[i];
						}
					}
				}
			}

			// update physics server with the shape transform
			physics_server->area_set_shape_transform(area, i, curr_shape_transf);

			// render immediately when interpolation is disabled
			if (!using_physics_interpolation) {
				multi->set_instance_transform_2d(i, curr_instance_transf);
			}

			// attachments and speed updates
			move_bullet_attachment(cache_velocity_calc, i, rotation_angle);
			accelerate_bullet_speed(i, delta);
		}

		// reset homing timer when we sampled
		if (homing_update_interval_reached) {
			homing_update_timer = homing_update_interval;
		}
	}

protected:
	bool adjust_direction_based_on_rotation = false;

	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_bullet_homing_target", "bullet_index", "new_homing_target"), &DirectionalBullets2D::set_bullet_homing_target);
		ClassDB::bind_method(D_METHOD("stop_bullet_homing", "bullet_index"), &DirectionalBullets2D::stop_bullet_homing);

		ClassDB::bind_method(D_METHOD("get_homing_smoothing"), &DirectionalBullets2D::get_homing_smoothing);
		ClassDB::bind_method(D_METHOD("set_homing_smoothing", "value"), &DirectionalBullets2D::set_homing_smoothing);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing"), "set_homing_smoothing", "get_homing_smoothing");

		ClassDB::bind_method(D_METHOD("get_homing_update_interval"), &DirectionalBullets2D::get_homing_update_interval);
		ClassDB::bind_method(D_METHOD("set_homing_update_interval", "value"), &DirectionalBullets2D::set_homing_update_interval);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_update_interval"), "set_homing_update_interval", "get_homing_update_interval");

		ClassDB::bind_method(D_METHOD("get_homing_take_control_of_texture_rotation"), &DirectionalBullets2D::get_homing_take_control_of_texture_rotation);
		ClassDB::bind_method(D_METHOD("set_homing_take_control_of_texture_rotation", "value"), &DirectionalBullets2D::set_homing_take_control_of_texture_rotation);
		ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_take_control_of_texture_rotation"), "set_homing_take_control_of_texture_rotation", "get_homing_take_control_of_texture_rotation");

		ClassDB::bind_method(D_METHOD("get_homing_rotation_radius"), &DirectionalBullets2D::get_homing_rotation_radius);
		ClassDB::bind_method(D_METHOD("set_homing_rotation_radius", "value"), &DirectionalBullets2D::set_homing_rotation_radius);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_rotation_radius"), "set_homing_rotation_radius", "get_homing_rotation_radius");

		ClassDB::bind_method(D_METHOD("get_homing_teleport_distance_threshold"), &DirectionalBullets2D::get_homing_teleport_distance_threshold);
		ClassDB::bind_method(D_METHOD("set_homing_teleport_distance_threshold", "value"), &DirectionalBullets2D::set_homing_teleport_distance_threshold);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_teleport_distance_threshold"), "set_homing_teleport_distance_threshold", "get_homing_teleport_distance_threshold");

		ClassDB::bind_method(D_METHOD("get_homing_teleport_angle_threshold"), &DirectionalBullets2D::get_homing_teleport_angle_threshold);
		ClassDB::bind_method(D_METHOD("set_homing_teleport_angle_threshold", "value"), &DirectionalBullets2D::set_homing_teleport_angle_threshold);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_teleport_angle_threshold"), "set_homing_teleport_angle_threshold", "get_homing_teleport_angle_threshold");
	}

	void set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data);

	virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_activate_logic(const MultiMeshBulletsData2D &data) override final;

public:
	_ALWAYS_INLINE_ void set_bullet_homing_target(int bullet_index, Node2D *new_homing_target) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			UtilityFunctions::printerr("Bullet index out of bounds in set_bullet_homing_target");
			return;
		}

		if (!bullets_enabled_status[bullet_index]) {
			return;
		}

		if (new_homing_target == nullptr) {
			stop_bullet_homing(bullet_index);
			return;
		}

		bullet_homing_targets[bullet_index] = new_homing_target;
		bullet_homing_target_instance_ids[bullet_index] = new_homing_target->get_instance_id();

		const Vector2 target_pos = new_homing_target->get_global_position();
		set_homing_bullet_direction_towards_target(bullet_index, target_pos);

		if ((int)bullet_last_known_homing_target_pos.size() == amount_bullets) {
			bullet_last_known_homing_target_pos[bullet_index] = target_pos;
		}
	}

	_ALWAYS_INLINE_ void stop_bullet_homing(int bullet_index) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			UtilityFunctions::printerr("Bullet index out of bounds in stop_bullet_homing");
			return;
		}

		bullet_homing_targets[bullet_index] = nullptr;
		bullet_homing_target_instance_ids[bullet_index] = 0;
		all_cached_homing_direction[bullet_index] = Vector2(0, 0);
		if ((int)bullet_last_known_homing_target_pos.size() == amount_bullets) {
			bullet_last_known_homing_target_pos[bullet_index] = Vector2(0, 0);
		}
	}

	float get_homing_smoothing() const {
		return homing_smoothing;
	}
	void set_homing_smoothing(float value) {
		homing_smoothing = value;
	}

	float get_homing_update_interval() const {
		return homing_update_interval;
	}
	void set_homing_update_interval(float value) {
		homing_update_interval = value;
	}

	bool get_homing_take_control_of_texture_rotation() const {
		return homing_take_control_of_texture_rotation;
	}
	void set_homing_take_control_of_texture_rotation(bool value) {
		homing_take_control_of_texture_rotation = value;
	}

	float get_homing_rotation_radius() const { return homing_rotation_radius; }
	void set_homing_rotation_radius(float v) { homing_rotation_radius = v; }

	float get_homing_teleport_distance_threshold() const { return homing_teleport_distance_threshold; }
	void set_homing_teleport_distance_threshold(float v) { homing_teleport_distance_threshold = v; }

	float get_homing_teleport_angle_threshold() const { return homing_teleport_angle_threshold; }
	void set_homing_teleport_angle_threshold(float v) { homing_teleport_angle_threshold = v; }

private:
	// Controls how often we refresh the sampled target position (seconds)
	float homing_update_interval = 0.0f;
	float homing_update_timer = 0.0f;

	// When <= 0 => instant homing. When > 0 => maximum angular velocity in radians/second.
	float homing_smoothing = 0.0f;

	bool homing_take_control_of_texture_rotation = false;

	// optional radius to improve turn aggressiveness near target
	float homing_rotation_radius = 0.0f;
	float homing_rotation_radius_speed_multiplier = 1.0f;

	// thresholds for deciding if a change is effectively a teleport (used to keep interpolation looking good)
	float homing_teleport_distance_threshold = 2048.0f;
	float homing_teleport_angle_threshold = 3.14159265358979323846f;

	// optional per-bullet cached target positions
	std::vector<Vector2> bullet_last_known_homing_target_pos;

	// homing targets & cached directions
	std::vector<Node2D *> bullet_homing_targets;
	std::vector<uint64_t> bullet_homing_target_instance_ids;
	std::vector<Vector2> all_cached_homing_direction;

	_ALWAYS_INLINE_ void set_homing_bullet_direction_towards_target(int bullet_index, const Vector2 &target_global_position) {
		Vector2 diff = target_global_position - all_cached_instance_origin[bullet_index];
		if (diff.length_squared() > 0.0f) {
			all_cached_homing_direction[bullet_index] = diff.normalized();
		} else {
			// if target coincides with bullet origin, leave previous cached direction unchanged
		}
		if ((int)bullet_last_known_homing_target_pos.size() == amount_bullets) {
			bullet_last_known_homing_target_pos[bullet_index] = target_global_position;
		}
	}
};
} // namespace BlastBullets2D
