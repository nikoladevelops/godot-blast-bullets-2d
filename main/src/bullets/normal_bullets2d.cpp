// #include "normal_bullets2d.hpp"

// #include <godot_cpp/variant/utility_functions.hpp>


// void NormalBullets2D::_physics_process(float delta){
//     move_bullets(delta);
//     reduce_lifetime(delta);
//     change_texture_periodically(delta);
//     handle_bullet_rotation(delta);
// }

// void NormalBullets2D::move_bullets(float delta){
//     for (int i = 0; i < size; i++)
//     {
//         if(bullets_enabled_status[i] == false){
//             continue;
//         }

//         Transform2D& curr_instance_transf = all_cached_instance_transforms[i];
//         Transform2D& curr_shape_transf = all_cached_shape_transforms[i];

//         Vector2& curr_instance_origin = all_cached_instance_origin[i];
//         Vector2& curr_shape_origin = all_cached_shape_origin[i];

//         curr_instance_origin += all_cached_velocity[i] * delta; 
//         curr_shape_origin += all_cached_velocity[i] * delta;

//         curr_instance_transf.set_origin(curr_instance_origin);
//         curr_shape_transf.set_origin(curr_shape_origin);

//         multi->set_instance_transform_2d(i, curr_instance_transf);
//         physics_server->area_set_shape_transform(area, i, curr_shape_transf);

//         // Accelerate each bullet's speed
//         accelerate_bullet_speed(i, delta);
//     }
// }

// void NormalBullets2D::set_up_movement_data(TypedArray<BulletSpeedData>& new_data){
//     int speed_data_size = new_data.size(); // the amount of speed data provided

//     if (speed_data_size != size){
//         UtilityFunctions::print("Error. When using NormalBullets2D you need to provide BulletSpeedData for every single bullet.");
//         return;
//     }

//     // Every single bullet will have its own direction/velocity/speed..
//     // We already know how many bullets we have so reserve memory at once to avoid unnecessary reallocations when pushing.
//     all_cached_speed.resize(speed_data_size);
//     all_cached_max_speed.resize(speed_data_size);
//     all_cached_acceleration.resize(speed_data_size);

//     all_cached_direction.resize(speed_data_size);
//     all_cached_velocity.resize(speed_data_size);

//     for (int i = 0; i < speed_data_size; i++)
//     {
//         // The rotation of each transform
//         float curr_bullet_rotation = all_cached_shape_transforms[i].get_rotation();

//         Ref<BulletSpeedData> curr_speed_data = new_data[i];
//         all_cached_speed[i] = curr_speed_data->speed;
//         all_cached_max_speed[i] = curr_speed_data->max_speed;
//         all_cached_acceleration[i] = curr_speed_data->acceleration;

//         // Calculate the direction
//         all_cached_direction[i] = Vector2(Math::cos(curr_bullet_rotation), Math::sin(curr_bullet_rotation));

//         // Calculate the velocity
//         all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i];
//     }
// }