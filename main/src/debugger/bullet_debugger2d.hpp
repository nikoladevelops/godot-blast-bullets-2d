#ifndef BULLET_DEBUGGER2D
#define BULLET_DEBUGGER2D

#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/multi_mesh.hpp"
#include "../bullets/multi_mesh_bullets2d.hpp"
#include "godot_cpp/classes/multi_mesh_instance2d.hpp"
#include "../bullets/block_bullets2d.hpp"
#include "godot_cpp/classes/weak_ref.hpp"
#include "../factory/bullet_factory2d.hpp"

using namespace godot;

// Provides easy debugging for the collision shapes of the bullets. When testing performance, disable the debugger, because it tanks performance
class BulletDebugger2D : public Node{
    GDCLASS(BulletDebugger2D, Node)

    public:
        // The pointer to the bullet factory
        BulletFactory2D* bullet_factory_ptr;

        // Determines whether the debugger is enabled or not
        bool is_enabled = false;

        void _ready();
        void _physics_process(float delta);
        // Clears all bullet collision shapes
        void reset_debugger();
    private:
        // A pointer to where the multi mesh bullets are stored in. This has to be refactored a lil bit
        Node* bullets_container_ptr;

        // Stores all multi mesh bullets' pointers, so that it can monitor their collision shapes
        std::vector<BlockBullets2D*> bullets_multi_meshes;
        // Stores all spawned multi mesh's pointers for the visualization of the collision shapes
        std::vector<MultiMeshInstance2D*> texture_multi_meshes;

        // Runs when a block bullet has been added to the bullets_container. Note that the bullet that has been added as a new child HAS to be set up completely
        void generate_texture_multimesh(BlockBullets2D* new_bullets_multi_mesh);

        void update_instance_transforms(MultiMeshInstance2D* texture_multi, BlockBullets2D* bullets_multi);
    protected:
        static void _bind_methods(){};
};



#endif