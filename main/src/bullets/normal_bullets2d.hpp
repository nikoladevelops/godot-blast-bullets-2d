#ifndef NORMAL_BULLETS2D
#define NORMAL_BULLETS2D

#include "multi_mesh_bullets2d.hpp"

class NormalBullets2D : public MultiMeshBullets2D{
    GDCLASS(NormalBullets2D, MultiMeshBullets2D)
    
    protected:
        static void _bind_methods();
        void move_bullets(float delta) override;
        void set_up_movement_data(TypedArray<BulletSpeedData>& new_data) override;
};

#endif