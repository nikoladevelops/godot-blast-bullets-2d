#ifndef BULLET_DEBUGGER2D_HPP
#define BULLET_DEBUGGER2D_HPP

#include <godot_cpp/classes/node.hpp>

namespace godot {
class MultiMeshInstance2D;
class PhysicsServer2D;
}

namespace BlastBullets {

class MultiMeshBullets2D;

// Provides easy debugging for the collision shapes of the bullets. When testing performance, disable the debugger, because it tanks performance
class BulletDebugger2D : public godot::Node {
    GDCLASS(BulletDebugger2D, godot::Node)

public:
    // A pointer to where the multi mesh bullets are stored in.
    godot::Node *bullets_container_ptr;
    godot::PhysicsServer2D *physics_server;

    void _ready();
    void _physics_process(float delta);
    // Clears all bullet collision shapes
    void reset_debugger();

private:
    // Stores all multi mesh bullets' pointers, so that it can monitor their collision shapes
    std::vector<MultiMeshBullets2D *> bullets_multi_meshes;
    // Stores all spawned multi mesh's pointers for the visualization of the collision shapes
    std::vector<godot::MultiMeshInstance2D *> texture_multi_meshes;

    // Runs when a block bullet has been added to the bullets_container. Note that the bullet that has been added as a new child HAS to be set up completely
    void generate_texture_multimesh(MultiMeshBullets2D *new_bullets_multi_mesh);

    void update_instance_transforms(godot::MultiMeshInstance2D *texture_multi, MultiMeshBullets2D *bullets_multi);

protected:
    static void _bind_methods() {};
};

}
#endif