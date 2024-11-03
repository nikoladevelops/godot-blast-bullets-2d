#include "block_bullets2d.hpp"
#include "../factory/bullet_factory2d.hpp"

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/quad_mesh.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

using namespace godot;

namespace BlastBullets {

void BlockBullets2D::_physics_process(float delta) {
    move_bullets(delta);
    reduce_lifetime(delta);
    change_texture_periodically(delta);
    handle_bullet_rotation(delta);
}

void BlockBullets2D::set_up_movement_data(TypedArray<BulletSpeedData> &new_data) {
    if (new_data.size() <= 0) {
        UtilityFunctions::print("Error. When using BlockBullets2D you have to provide a single BulletSpeedData.");
        return;
    }

    // Since the block bullets work by moving the whole multimesh in a single direction we basically work with a single speed data for every single bullet
    int speed_data_size = 1;

    all_cached_speed.resize(speed_data_size);
    all_cached_max_speed.resize(speed_data_size);
    all_cached_acceleration.resize(speed_data_size);

    all_cached_direction.resize(speed_data_size);
    all_cached_velocity.resize(speed_data_size);

    Ref<BulletSpeedData> curr_speed_data = new_data[0];
    all_cached_speed[0] = curr_speed_data->speed;
    all_cached_max_speed[0] = curr_speed_data->max_speed;
    all_cached_acceleration[0] = curr_speed_data->acceleration;

    // Calculate the direction
    all_cached_direction[0] = Vector2(Math::cos(block_rotation_radians), Math::sin(block_rotation_radians));

    // Calculate the velocity
    all_cached_velocity[0] = all_cached_direction[0] * all_cached_speed[0];
}

void BlockBullets2D::custom_additional_spawn_logic(const Ref<BlockBulletsData2D> &data) {
    block_rotation_radians = data->block_rotation_radians;

    // TODO remove use_block_rotation_radians from data
}

void BlockBullets2D::custom_additional_save_logic(const Ref<SaveDataBlockBullets2D> &data) {
    data->block_rotation_radians = block_rotation_radians;
    data->multimesh_position = current_position;
}

void BlockBullets2D::custom_additional_load_logic(const Ref<SaveDataBlockBullets2D> &data) {
    current_position = data->multimesh_position;
    block_rotation_radians = data->block_rotation_radians;

    set_global_position(current_position);
}

void BlockBullets2D::custom_additional_activate_logic(const Ref<BlockBulletsData2D> &data) {
    current_position = Vector2(0, 0);
    set_global_position(current_position);

    block_rotation_radians = data->block_rotation_radians;
}
}