#ifndef NORMAL_BULLETS2D_HPP
#define NORMAL_BULLETS2D_HPP

#include "../shared/bullet_speed_data2d.hpp"
#include "multi_mesh_bullets2d.hpp"

namespace BlastBullets {

class NormalBullets2D : public MultiMeshBullets2D {
    GDCLASS(NormalBullets2D, MultiMeshBullets2D)
public:
    // The physics process loop. Holds all logic that needs to be repeated every physics frame
    void _physics_process(float delta);

protected:
    static void _bind_methods() {}

    _ALWAYS_INLINE_ void move_bullets(float delta) {
        for (int i = 0; i < size; i++) {
            if (bullets_enabled_status[i] == false) {
                continue;
            }

            godot::Transform2D &curr_instance_transf = all_cached_instance_transforms[i];
            godot::Transform2D &curr_shape_transf = all_cached_shape_transforms[i];

            godot::Vector2 &curr_instance_origin = all_cached_instance_origin[i];
            godot::Vector2 &curr_shape_origin = all_cached_shape_origin[i];

            curr_instance_origin += all_cached_velocity[i] * delta;
            curr_shape_origin += all_cached_velocity[i] * delta;

            curr_instance_transf.set_origin(curr_instance_origin);
            curr_shape_transf.set_origin(curr_shape_origin);

            multi->set_instance_transform_2d(i, curr_instance_transf);
            physics_server->area_set_shape_transform(area, i, curr_shape_transf);

            // Accelerate each bullet's speed
            accelerate_bullet_speed(i, delta);
        }
    }

    void set_up_movement_data(const godot::TypedArray<BulletSpeedData2D> &new_speed_data);
    virtual void custom_additional_spawn_logic(const godot::Ref<MultiMeshBulletsData2D> &data) override final;
    void custom_additional_save_logic(const godot::Ref<SaveDataMultiMeshBullets2D> &data) override final;
    virtual void custom_additional_load_logic(const godot::Ref<SaveDataMultiMeshBullets2D> &data) override final;
    virtual void custom_additional_activate_logic(const godot::Ref<MultiMeshBulletsData2D> &data) override final;
};
}

#endif