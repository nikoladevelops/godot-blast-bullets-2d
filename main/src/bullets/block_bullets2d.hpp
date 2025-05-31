#pragma once

#include "./multimesh_bullets2d.hpp"

#include "../save-data/save_data_block_bullets2d.hpp"
#include "../shared/bullet_rotation_data2d.hpp"
#include "../shared/bullet_speed_data2d.hpp"

namespace BlastBullets2D {
using namespace godot;

class BlockBullets2D : public MultiMeshBullets2D {
    GDCLASS(BlockBullets2D, MultiMeshBullets2D)

public:
    // The block rotation. The direction of the bullets is determined by it.
    float block_rotation_radians = 0.0f;

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
            if (is_bullet_attachment_provided)
            {
                all_previous_attachment_transf = attachment_transforms;
            }
        }


        Vector2 cache_velocity_calc = all_cached_velocity[0] * delta;

        for (int i = 0; i < amount_bullets; i++) {
            if (bullets_enabled_status[i] == false) {
                continue;
            }

            Transform2D& curr_instance_transf = all_cached_instance_transforms[i];
            Transform2D& curr_shape_transf = all_cached_shape_transforms[i];

            Vector2& curr_instance_origin = all_cached_instance_origin[i];
            Vector2& curr_shape_origin = all_cached_shape_origin[i];

            curr_instance_origin += cache_velocity_calc;
            curr_shape_origin += cache_velocity_calc;

            // Handle bullet rotation and bullet rotation speed acceleration
            float rotation_angle = 0.0f;
            if (is_rotation_active) {
                if (!use_only_first_rotation_data) {
                    max_rotation_speed_reached = accelerate_bullet_rotation_speed(i, delta);
                    rotation_angle = all_rotation_speed[i] * delta;
                }
                else {
                    rotation_angle = cache_first_rotation_result;
                }

                // If max rotation speed has been reached and the setting stop_rotation_when_max_reached has been set then rotation should be stopped
                if (max_rotation_speed_reached && stop_rotation_when_max_reached) {
                    // Don't rotate
                }
                else {
                    // In all other cases rotation should continue

                    rotate_transform_locally(curr_instance_transf, rotation_angle);

                    if (!rotate_only_textures) {
                        rotate_transform_locally(curr_shape_transf, rotation_angle);
                    }
                }
            }

            curr_instance_transf.set_origin(curr_instance_origin);
            curr_shape_transf.set_origin(curr_shape_origin);

            physics_server->area_set_shape_transform(area, i, curr_shape_transf);

            // If we are not using physics interpolation then just render the texture in the current physics frame
            if (!is_using_physics_interpolation)
            {
                multi->set_instance_transform_2d(i, curr_instance_transf);
            }

            move_bullet_attachment(cache_velocity_calc, i, rotation_angle);
        }

        accelerate_bullet_speed(0, delta);
    }

protected:
    static void _bind_methods() {}
    void set_up_movement_data(const BulletSpeedData2D &new_speed_data);
    
    virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) override final;
    virtual void custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) override final;
    virtual void custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) override final;
    virtual void custom_additional_activate_logic(const MultiMeshBulletsData2D &data) override final;
};
}
