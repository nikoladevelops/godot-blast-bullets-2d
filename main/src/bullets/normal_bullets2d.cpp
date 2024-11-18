#include "normal_bullets2d.hpp"

#include "../save-data/save_data_normal_bullets2d.hpp"
#include "../spawn-data/normal_bullets_data2d.hpp"
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets {

void NormalBullets2D::set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data) {
    int speed_data_size = new_speed_data.size(); // the amount of speed data provided

    if (speed_data_size != size) {
        UtilityFunctions::print("Error. When using NormalBullets2D you need to provide BulletSpeedData2D for every single bullet.");
        return;
    }

    // Every single bullet will have its own direction/velocity/speed..
    // We already know how many bullets we have so reserve memory at once to avoid unnecessary reallocations when pushing.
    all_cached_speed.reserve(speed_data_size);
    all_cached_max_speed.reserve(speed_data_size);
    all_cached_acceleration.reserve(speed_data_size);

    all_cached_direction.reserve(speed_data_size);
    all_cached_velocity.reserve(speed_data_size);

    for (int i = 0; i < speed_data_size; i++) {
        // The rotation of each transform
        float curr_bullet_rotation = all_cached_shape_transforms[i].get_rotation();

        const Ref<BulletSpeedData2D> &curr_speed_data = new_speed_data[i];

        all_cached_speed.push_back(curr_speed_data->speed);
        all_cached_max_speed.push_back(curr_speed_data->max_speed);
        all_cached_acceleration.push_back(curr_speed_data->acceleration);

        // Calculate the direction
        all_cached_direction.emplace_back(Math::cos(curr_bullet_rotation), Math::sin(curr_bullet_rotation));

        // Calculate the velocity
        all_cached_velocity.push_back(all_cached_direction[i] * all_cached_speed[i]);
    }
}

void NormalBullets2D::activate_movement_data(const godot::TypedArray<BulletSpeedData2D> &new_speed_data){
    int speed_data_size = new_speed_data.size(); // the amount of speed data provided

    if (speed_data_size != size) {
        UtilityFunctions::print("Error. When using NormalBullets2D you need to provide BulletSpeedData2D for every single bullet.");
        return;
    }

    for (int i = 0; i < speed_data_size; i++) {
        // The rotation of each transform
        float curr_bullet_rotation = all_cached_shape_transforms[i].get_rotation();

        const Ref<BulletSpeedData2D> &curr_speed_data = new_speed_data[i];

        all_cached_speed[i] = curr_speed_data->speed;
        all_cached_max_speed[i] = curr_speed_data->max_speed;
        all_cached_acceleration[i] = curr_speed_data->acceleration;

        // Calculate the direction
        all_cached_direction[i] = Vector2(Math::cos(curr_bullet_rotation), Math::sin(curr_bullet_rotation));

        // Calculate the velocity
        all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i];
    }
}

void NormalBullets2D::custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) {
    const NormalBulletsData2D &normal_data = static_cast<const NormalBulletsData2D&>(data);

    set_up_movement_data(normal_data.all_bullet_speed_data);
}

void NormalBullets2D::custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) {
}

void NormalBullets2D::custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) {
}

void NormalBullets2D::custom_additional_activate_logic(const MultiMeshBulletsData2D &data) {
    const NormalBulletsData2D &normal_data = static_cast<const NormalBulletsData2D&>(data);
    
    activate_movement_data(normal_data.all_bullet_speed_data);
}

}