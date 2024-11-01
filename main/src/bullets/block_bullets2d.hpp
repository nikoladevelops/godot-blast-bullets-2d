#ifndef BLOCK_BULLETS2D
#define BLOCK_BULLETS2D

#include "./multi_mesh_bullets2d.hpp"

#include "../save-data/save_data_block_bullets2d.hpp"
#include "../shared/bullet_rotation_data.hpp"
#include "../shared/bullet_speed_data.hpp"
#include "../spawn-data/block_bullets_data2d.hpp"

namespace BlastBullets {

class BlockBullets2D : public MultiMeshBullets2D {
    GDCLASS(BlockBullets2D, MultiMeshBullets2D);

public:
    // The block rotation. The direction of the bullets is determined by it. Only used if use_block_rotation_radians is set to true
    float block_rotation_radians;

    // Cached multimesh instance position.
    godot::Vector2 current_position;

    // The physics process loop. Holds all logic that needs to be repeated every physics frame
    void _physics_process(float delta);

protected:
    static void _bind_methods() {}

    _ALWAYS_INLINE_ void move_bullets(float delta) {
        godot::Vector2 new_pos = current_position + all_cached_velocity[0] * delta;
        set_global_position(new_pos);

        for (int i = 0; i < size; i++) {
            // No point in editing transforms if the bullet has been disabled, that would mean moving disabled bullets and taking away from performance
            if (bullets_enabled_status[i] == false) {
                continue;
            }

            godot::Transform2D &new_transf = all_cached_shape_transforms[i];
            godot::Vector2 &new_shape_origin = all_cached_shape_origin[i];
            new_shape_origin = new_shape_origin + all_cached_velocity[0] * delta;

            new_transf.set_origin(new_shape_origin);

            physics_server->area_set_shape_transform(area, i, new_transf);
        }
        // Accelerate the shared singular bullet speed
        accelerate_bullet_speed(0, delta);

        current_position = new_pos;
    }
    void set_up_movement_data(godot::TypedArray<BulletSpeedData> &new_data) override final;
    virtual void custom_additional_spawn_logic(const godot::Ref<BlockBulletsData2D> &data) override final;
    void custom_additional_save_logic(const godot::Ref<SaveDataBlockBullets2D> &data) override final;
    virtual void custom_additional_load_logic(const godot::Ref<SaveDataBlockBullets2D> &data) override final;
    virtual void custom_additional_activate_logic(const godot::Ref<BlockBulletsData2D> &data) override final;
};
}
#endif