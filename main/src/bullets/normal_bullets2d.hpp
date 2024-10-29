#ifndef NORMAL_BULLETS2D
#define NORMAL_BULLETS2D

#include "multi_mesh_bullets2d.hpp"

class NormalBullets2D: public MultiMeshBullets2D<NormalBullets2D, BlockBulletsData2D, SaveDataBlockBullets2D >{
    GDCLASS(NormalBullets2D, MultiMeshBullets2D<BlockBullets2D, BlockBulletsData2D, SaveDataBlockBullets2D>)
    public:
        // The physics process loop. Holds all logic that needs to be repeated every physics frame
        void _physics_process(float delta);
    protected:

        static void _bind_methods(){}

        _ALWAYS_INLINE_ void move_bullets(float delta);
        void set_up_movement_data(TypedArray<BulletSpeedData>& new_data) override final;

};

#endif