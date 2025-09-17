#pragma once

#include "../shared/bullet_speed_data2d.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "multimesh_bullets2d.hpp"

#include <cstdint>
#include <godot_cpp/variant/utility_functions.hpp>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class DirectionalBullets2D : public MultiMeshBullets2D {
	GDCLASS(DirectionalBullets2D, MultiMeshBullets2D)
public:
	_ALWAYS_INLINE_ void move_bullets(float delta) {
		float cache_first_rotation_result = 0.0f;
		bool max_rotation_speed_reached = false;
		// Accelerate only the first bullet rotation speed
		if (is_rotation_active && use_only_first_rotation_data) {
			max_rotation_speed_reached = accelerate_bullet_rotation_speed(0, delta); // accelerate only the first one once
			cache_first_rotation_result = all_rotation_speed[0] * delta;
		}

		bool is_using_physics_interpolation = bullet_factory->use_physics_interpolation;

		// If using interpolation, store current transforms as previous ones
		if (is_using_physics_interpolation) {
			all_previous_instance_transf = all_cached_instance_transforms;
			if (is_bullet_attachment_provided) {
				all_previous_attachment_transf = attachment_transforms;
			}
		}

        homing_update_timer -= delta;
        bool homing_active = homing_update_timer <= 0.0f;
        if (homing_active) {
            homing_update_timer = homing_update_interval;
        }
        
		for (int i = 0; i < amount_bullets; i++) {
			if (bullets_enabled_status[i] == false) {
				continue;
			}

			Transform2D &curr_instance_transf = all_cached_instance_transforms[i];
			Transform2D &curr_shape_transf = all_cached_shape_transforms[i];

			Vector2 &curr_instance_origin = all_cached_instance_origin[i];
			Vector2 &curr_shape_origin = all_cached_shape_origin[i];

			// Get the homing target for the bullet if there is any
			const Node2D *const homing_target = bullet_homing_targets[i];

			// Handle bullet rotation and bullet rotation speed acceleration
			float rotation_angle = 0.0f;
			if (is_rotation_active) {
				if (!use_only_first_rotation_data) {
					max_rotation_speed_reached = accelerate_bullet_rotation_speed(i, delta);
					rotation_angle = all_rotation_speed[i] * delta;
				} else {
					rotation_angle = cache_first_rotation_result;
				}

				// If max rotation speed has been reached and the setting stop_rotation_when_max_reached has been set then rotation should be stopped
				if (max_rotation_speed_reached && stop_rotation_when_max_reached) {
					// Don't rotate
				} else {
					// In all other cases rotation should continue

					rotate_transform_locally(curr_instance_transf, rotation_angle);

					if (!rotate_only_textures) {
						rotate_transform_locally(curr_shape_transf, rotation_angle);
					}
				}

				// If we dont have a homing target and the user wants to adjust direction based on rotation then do it
				// TODO document this in XML
				if (adjust_direction_based_on_rotation && homing_target == nullptr) {
					// Update velocity direction based on current rotation
					Vector2 &current_direction = all_cached_direction[i];
					current_direction = curr_instance_transf[0].normalized(); // World-space forward direction
					float current_speed = all_cached_velocity[i].length(); // Preserve current speed

					all_cached_velocity[i] = current_direction * current_speed; // Set new velocity
				}
			}

            /// HOMING BEHAVIOR
			if (homing_target != nullptr && homing_active) { // if there is a homing target adjust the bullet's direction towards it
				// Check just in case if the instance was freed previously - prevents crashes
				if (UtilityFunctions::is_instance_id_valid(bullet_homing_target_instance_ids[i])) {
					// Apply smoothing to velocity
                    bool rotate_sprite_to_homing = true;

					// Get target direction
                    const Vector2 target_pos = homing_target->get_global_position();
                    Vector2 target_dir = (target_pos - curr_instance_origin).normalized();

                    // Apply smoothing to velocity
                    Vector2 &velo = all_cached_velocity[i];
                    float speed = velo.length();
                    if (speed > 0.0f) {
                        Vector2 current_dir = velo.normalized();

                        if (homing_smoothing <= 0.0f) {
                            // Instant homing
                            velo = target_dir * speed;
                            all_cached_direction[i] = target_dir;

                            // Rotate texture and shape instantly
                            if (rotate_sprite_to_homing) {
                                float new_rotation = target_dir.angle();
                                curr_instance_transf.set_rotation(new_rotation + cache_texture_rotation_radians);
                                if (!rotate_only_textures) {
                                    curr_shape_transf.set_rotation(new_rotation);
                                }
                            }
                        } else {
                            // Smooth homing
                            float angle_diff = current_dir.angle_to(target_dir);
                            float max_turn = homing_smoothing * delta;
                            float turn = Math::clamp(angle_diff, -max_turn, max_turn);
                            Vector2 new_dir = current_dir.rotated(turn);
                            velo = new_dir * speed;
                            all_cached_direction[i] = new_dir;

                            // Smoothly rotate texture and shape
                            if (rotate_sprite_to_homing) {
                                float current_angle = curr_instance_transf.get_rotation();
                                float target_angle = new_dir.angle();
                                float smoothed_angle = Math::lerp_angle(current_angle - cache_texture_rotation_radians, target_angle, homing_smoothing * delta);
                                curr_instance_transf.set_rotation(smoothed_angle + cache_texture_rotation_radians);
                                if (!rotate_only_textures) {
                                    curr_shape_transf.set_rotation(smoothed_angle);
                                }
                            }
                        }
					}
				} else {
					bullet_homing_targets[i] = nullptr;
				}
			}

            ///

			// Update position with the new velocity
			Vector2 cache_velocity_calc = all_cached_velocity[i] * delta;
			curr_instance_origin += cache_velocity_calc;
			curr_shape_origin += cache_velocity_calc;

			curr_instance_transf.set_origin(curr_instance_origin);
			curr_shape_transf.set_origin(curr_shape_origin);

			physics_server->area_set_shape_transform(area, i, curr_shape_transf);

			// If we are not using physics interpolation then just render the texture in the current physics frame
			if (!is_using_physics_interpolation) {
				multi->set_instance_transform_2d(i, curr_instance_transf);
			}

			move_bullet_attachment(cache_velocity_calc, i, rotation_angle);

			// Accelerate each bullet's speed
			accelerate_bullet_speed(i, delta);
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


    }

	void set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data);

	virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_activate_logic(const MultiMeshBulletsData2D &data) override final;

	//////////////////////////// Homing Behavior Related /////////////////////////////

public:
	// Sets an individual bullet's homing target
	// If the target is nullptr then the homing behavior is disabled for that bullet
	// If the bullet is already disabled then the homing will not be set
	// If the target is freed during runtime you will experience a crash, instead you should call stop_bullet_homing for that bullet first
	_ALWAYS_INLINE_ void set_bullet_homing_target(int bullet_index, Node2D *new_homing_target) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			UtilityFunctions::printerr("Bullet index out of bounds in set_bullet_homing_target");
			return;
		}

		// No point in setting a homing target if the bullet is already disabled
		if (!bullets_enabled_status[bullet_index]) {
			return;
		}

		if (new_homing_target == nullptr) {
			stop_bullet_homing(bullet_index);
			return;
		}

		bullet_homing_targets[bullet_index] = new_homing_target;
		bullet_homing_target_instance_ids[bullet_index] = new_homing_target->get_instance_id();
	}

	_ALWAYS_INLINE_ void stop_bullet_homing(int bullet_index) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			UtilityFunctions::printerr("Bullet index out of bounds in stop_bullet_homing");
			return;
		}

		bullet_homing_targets[bullet_index] = nullptr;
		bullet_homing_target_instance_ids[bullet_index] = 0;
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


private:
	float homing_update_interval = 0.0f;
	float homing_update_timer = 0.0f;
    
    float homing_smoothing = 0.0f;

	// Tracks each bullet's homing target - if target is nullptr it means that the bullet is not homing
	std::vector<Node2D *> bullet_homing_targets;

	std::vector<uint64_t> bullet_homing_target_instance_ids;

	/////////////////////////////////////////////////////////////////////
};
}