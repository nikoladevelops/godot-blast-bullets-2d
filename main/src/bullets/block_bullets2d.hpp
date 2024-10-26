#ifndef BLOCK_BULLETS2D
#define BLOCK_BULLETS2D

#include "./multi_mesh_bullets2d.hpp"

using namespace godot;

class BlockBullets2D: public MultiMeshBullets2D{
    GDCLASS(BlockBullets2D, MultiMeshBullets2D);
    
    public:
        bool use_block_rotation_radians; // TODO Need to be completely removed

        // The block rotation. The direction of the bullets is determined by it. Only used if use_block_rotation_radians is set to true
        float block_rotation_radians;

        // Cached multimesh instance position.
        Vector2 current_position;
    protected:
        static void _bind_methods(){};

    private:
        // Contains the necessary logic to move the bullets that are inside the multimesh
        void move_bullets(float delta) override;
};

#endif