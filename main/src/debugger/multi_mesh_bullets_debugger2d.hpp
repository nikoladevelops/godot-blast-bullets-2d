#ifndef MULTI_MESH_BULLETS_DEBUGGER2D_HPP
#define MULTI_MESH_BULLETS_DEBUGGER2D_HPP

#include <godot_cpp/classes/node.hpp>

namespace godot {
class MultiMeshInstance2D;
class PhysicsServer2D;
}

namespace BlastBullets {

class MultiMeshBullets2D;

// Provides easy debugging for the collision shapes of the bullets. When testing performance, disable the debugger, because it tanks performance
class MultiMeshBulletsDebugger2D : public godot::Node {
    GDCLASS(MultiMeshBulletsDebugger2D, godot::Node)

public:
    // A pointer to where the multimesh bullets are stored in.
    godot::Node *bullets_container_ptr;

    // A pointer to the physics server
    godot::PhysicsServer2D *physics_server;

    // The color of the collision shapes represented by the texture multimeshes
    godot::Color multi_mesh_color;

    // Handles movement of the debug shapes of each texture multimesh
    void _physics_process(float delta);

    // Resets the debugger's state. Note that this frees all of the debugger's texuture multimeshes but DOES NOT free the actual MultiMeshBullets2D objects
    void reset_debugger();

    // Frees only the texture multimeshes that track currently disabled bullets (in other words - frees only the debug shapes for the pooled bullets). Note that this frees only debugger texuture multi_meshes but DOES NOT free the actual MultiMeshBullets2D objects
    void free_texture_multi_meshes_tracking_disabled_bullets();

    // Frees only the texture multimeshes that track currently disabled bullets, BUT only those that contain exactly `amount_bullets` instances. Note that this frees only debugger texuture multi_meshes but DOES NOT free the actual MultiMeshBullets2D objects
    void free_texture_multi_meshes_tracking_disabled_bullets(int amount_bullets);

    // Changes the color of all texture multimeshes/ the color of the debug shapes
    void change_texture_multimeshes_color(const godot::Color &new_multi_mesh_color);

    // Configures the debugger so that it tracks all MultiMeshBullets2D collision shapes inside a specific bullets container
    void configure(godot::Node* new_bullets_container, const godot::String& new_debugger_name, const godot::Color &new_multi_mesh_color);

    // Disables the debugger and frees all displayed texure multimeshes. Note that this frees all of the debugger's texuture multi_meshes but DOES NOT free the actual MultiMeshBullets2D objects
    void disable();

    // Activates the debugger and spawns all needed collision textures for the already spawned multimesh bullets
    void activate();

private:
    // Stores all multi mesh bullets' pointers, so that it can monitor their collision shapes
    std::vector<MultiMeshBullets2D *> bullets_multi_meshes;

    // Stores all spawned texture multi mesh's pointers for the visualization of the collision shapes
    std::vector<godot::MultiMeshInstance2D *> texture_multi_meshes;

    // Runs when a block bullet has been added to the bullets_container. Note that the bullet that has been added as a new child HAS to be set up completely
    void generate_texture_multimesh(MultiMeshBullets2D *new_bullets_multi_mesh);

    // Updates a specific texture multimesh's instance transforms so that each matches the corresponding bullets multimesh
    void update_instance_transforms(godot::MultiMeshInstance2D *texture_multi, MultiMeshBullets2D *bullets_multi);

protected:
    static void _bind_methods() {};
};

}
#endif