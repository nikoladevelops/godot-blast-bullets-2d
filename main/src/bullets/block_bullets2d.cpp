#include "block_bullets2d.hpp"

#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/classes/quad_mesh.hpp"

#include "../factory/bullet_factory2d.hpp"
#include "godot_cpp/classes/engine.hpp"

using namespace godot;

_ALWAYS_INLINE_ void BlockBullets2D::move_bullets(float delta){
    Vector2 new_pos = current_position + all_cached_velocity[0] * delta;
    set_global_position(new_pos);

    for (int i = 0; i < size; i++)
    {
        // No point in editing transforms if the bullet has been disabled, that would mean moving disabled bullets and taking away from performance
        if(bullets_enabled_status[i] == false){
            continue;
        }

        Transform2D& new_transf = all_cached_shape_transforms[i];
        Vector2& new_shape_origin = all_cached_shape_origin[i];
        new_shape_origin = new_shape_origin + all_cached_velocity[0]*delta;

        new_transf.set_origin(new_shape_origin);

        physics_server->area_set_shape_transform(area, i, new_transf);

    }
    // Accelerate the shared singular bullet speed
    accelerate_bullet_speed(0, delta);
    
    current_position=new_pos;
}