#ifndef BLOCK_BULLETS2D
#define BLOCK_BULLETS2D

#include "./multi_mesh_bullets2d.hpp"

using namespace godot;

class BlockBullets2D: public MultiMeshBullets2D<BlockBullets2D>{
    GDCLASS(BlockBullets2D, MultiMeshBullets2D);
    
    public:
        // The block rotation. The direction of the bullets is determined by it. Only used if use_block_rotation_radians is set to true
        float block_rotation_radians;

        // Cached multimesh instance position.
        Vector2 current_position;

        // The physics process loop. Holds all logic that needs to be repeated every physics frame
        void _physics_process(float delta);
    protected:
        static void _bind_methods(){};
        _ALWAYS_INLINE_ void move_bullets(float delta);
        void set_up_movement_data(TypedArray<BulletSpeedData>& new_data) override final;
        virtual void custom_additional_spawn_logic(const Ref<BlockBulletsData2D>& data) override final;
        void custom_additional_save_logic(Ref<SaveDataBlockBullets2D>& data) override final;
        virtual void custom_additional_load_logic(const Ref<SaveDataBlockBullets2D>& data) override final;
        virtual void custom_additional_activate_logic(const Ref<BlockBulletsData2D>& data) override final;
};

#endif