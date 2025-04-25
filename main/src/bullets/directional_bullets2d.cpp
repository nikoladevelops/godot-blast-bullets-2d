#include "directional_bullets2d.hpp"

#include "../save-data/save_data_directional_bullets2d.hpp"
#include "../spawn-data/directional_bullets_data2d.hpp"
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

void DirectionalBullets2D::set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data) {
    int speed_data_size = new_speed_data.size(); // the amount of speed data provided

    if (speed_data_size != amount_bullets) {
        UtilityFunctions::print("Error. When using DirectionalBullets2D you need to provide BulletSpeedData2D for every single bullet.");
        return;
    }

    // If there is any old data inside, it needs to be cleaned up
    if (all_cached_speed.size() > 0)
    {
        // Clear all old data
        all_cached_speed.clear();
        all_cached_max_speed.clear();
        all_cached_acceleration.clear();
        all_cached_direction.clear();
        all_cached_velocity.clear();

        // If the new data does not fit, then reserve enough space
        if (all_cached_speed.capacity() != speed_data_size)
        {
            all_cached_speed.reserve(speed_data_size);
            all_cached_max_speed.reserve(speed_data_size);
            all_cached_acceleration.reserve(speed_data_size);
            all_cached_direction.reserve(speed_data_size);
            all_cached_velocity.reserve(speed_data_size);
        }
    }

    for (size_t i = 0; i < speed_data_size; i++) {
        // The rotation of each transform
        float curr_bullet_rotation = all_cached_shape_transforms[i].get_rotation(); // Note: I am using the shape transforms, since the instance transforms might be rotated to account for bullet texture rotation

        const Ref<BulletSpeedData2D>& curr_speed_data = new_speed_data[i];
        all_cached_speed.emplace_back(curr_speed_data->speed);
        all_cached_max_speed.emplace_back(curr_speed_data->max_speed);
        all_cached_acceleration.emplace_back(curr_speed_data->acceleration);

        // Calculate the direction
        all_cached_direction.emplace_back(Vector2(Math::cos(curr_bullet_rotation), Math::sin(curr_bullet_rotation)));

        // Calculate the velocity
        all_cached_velocity.emplace_back(all_cached_direction[i] * all_cached_speed[i]);
    }
}

void DirectionalBullets2D::custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) {
    const DirectionalBulletsData2D &directional_data = static_cast<const DirectionalBulletsData2D&>(data);

    set_up_movement_data(directional_data.all_bullet_speed_data);
}

void DirectionalBullets2D::custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) {
}

void DirectionalBullets2D::custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) {
}

void DirectionalBullets2D::custom_additional_activate_logic(const MultiMeshBulletsData2D &data) {
    const DirectionalBulletsData2D &directional_data = static_cast<const DirectionalBulletsData2D&>(data);
    
    set_up_movement_data(directional_data.all_bullet_speed_data);
}

}