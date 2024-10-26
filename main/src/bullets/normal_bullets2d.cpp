#include "normal_bullets2d.hpp"

#include <godot_cpp/variant/utility_functions.hpp>

void NormalBullets2D::move_bullets(float delta){
    for (int i = 0; i < size; i++)
    {
        if(bullets_enabled_status[i] == false){
            continue;
        }

        Transform2D& curr_instance_transf = all_cached_instance_transforms[i];
        Transform2D& curr_shape_transf = all_cached_shape_transforms[i];

        Vector2& curr_instance_origin = all_cached_instance_origin[i];
        Vector2& curr_shape_origin = all_cached_shape_origin[i];

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

void NormalBullets2D::_bind_methods(){

}