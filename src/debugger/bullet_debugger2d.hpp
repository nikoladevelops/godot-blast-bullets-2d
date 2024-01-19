#ifndef BULLET_DEBUGGER2D
#define BULLET_DEBUGGER2D

#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/multi_mesh.hpp"
#include "../bullets/multi_mesh_bullets2d.hpp"
#include "godot_cpp/classes/physics_server2d.hpp"
#include "godot_cpp/classes/multi_mesh_instance2d.hpp"
#include "../bullets/block_bullets2d.hpp"
#include "godot_cpp/classes/weak_ref.hpp"
#include "../factory/bullet_factory2d.hpp"

using namespace godot;

class BulletDebugger2D : public Node{
    GDCLASS(BulletDebugger2D, Node)

    // The node path to the Node container for the bullets.
    NodePath bullets_container;
    // A pointer to where the multi mesh bullets are stored in. This has to be refactored a lil bit
    Node* bullets_container_ptr;

    // The factory that the debugger is watching
    NodePath bullet_factory;
    // The pointer to the bullet factory
    BulletFactory2D* bullet_factory_ptr;

    // Determines whether the debugger is enabled or not.
    bool is_enabled;
    // Stores all multi mesh bullets' pointers, so that it can monitor their collision shapes.
    std::vector<BlockBullets2D*> bullets_multi_meshes;
    // Stores all spawned multi mesh's pointers for the visualization of the collision shapes.
    std::vector<MultiMeshInstance2D*> texture_multi_meshes;

    PhysicsServer2D* physics_server;
    public:
        void _ready();
        void _physics_process(float delta);

        NodePath get_bullets_container() const;
        void set_bullets_container(const NodePath& new_bullets_container);

        NodePath get_bullet_factory() const;
        void set_bullet_factory(const NodePath& new_bullet_factory);

        bool get_is_enabled();
        void set_is_enabled(bool new_is_enabled);

    private:
        // Executed when bullets entered the bullets container
        void bullets_entered_container(Node* node);
        void generate_texture_multimesh(BlockBullets2D* new_bullets_multi_mesh);

        void update_instance_transforms(MultiMeshInstance2D* texture_multi, BlockBullets2D* bullets_multi);
        void reset_debugger();

    protected:
        static void _bind_methods();
};



#endif