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

    // The color of the collision shapes represented by the debug multimeshes
    godot::Color multi_mesh_color;

    // Handles movement of the debug shapes of each debug multimesh
    void _physics_process(float delta);

    // Resets the debugger's state. Note that this frees all of the debugger's texuture multimeshes but DOES NOT free the actual MultiMeshBullets2D objects
    void reset_debugger();

    // Frees only the debug multimeshes that track currently disabled bullets (in other words - frees only the debug shapes for the pooled bullets). Note that this frees only debugger texuture multi_meshes but DOES NOT free the actual MultiMeshBullets2D objects
    void free_texture_multi_meshes_tracking_disabled_bullets();

    // Frees only the debug multimeshes that track currently disabled bullets, BUT only those that contain exactly `amount_bullets` instances. Note that this frees only debugger texuture multi_meshes but DOES NOT free the actual MultiMeshBullets2D objects
    void free_texture_multi_meshes_tracking_disabled_bullets(int amount_bullets);

    // Changes the color of all debug multimeshes/ the color of the debug shapes
    void change_texture_multimeshes_color(const godot::Color &new_multi_mesh_color);

    // Configures the debugger so that it tracks all MultiMeshBullets2D collision shapes inside a specific bullets container
    void configure(godot::Node* new_bullets_container, const godot::String& new_debugger_name, const godot::Color &new_multi_mesh_color);

    // Disables the debugger and frees all displayed texure multimeshes. Note that this frees all of the debugger's texuture multi_meshes but DOES NOT free the actual MultiMeshBullets2D objects
    void disable();

    // Activates the debugger and spawns all needed collision textures for the already spawned multimesh bullets
    void activate();

private:
    // Stores pointers to the spawned bullet multimeshes
    std::vector<MultiMeshBullets2D *> bullets_multi_meshes;

    // Stores pointers to the spawned debug multimeshes
    std::vector<godot::MultiMeshInstance2D *> texture_multi_meshes;

    // Runs when a block bullet has been added to the bullets_container. Note that the bullet that has been added as a new child HAS to be set up completely
    void generate_texture_multimesh(MultiMeshBullets2D *new_bullets_multi_mesh);

    // Ensures that the quadmesh of the debug multimesh matches the size of the physics shape of the bullets multimesh bullets
    void ensure_quadmesh_matches_physics_shape_size(godot::MultiMeshInstance2D &debug_multimesh_instance, MultiMeshBullets2D &bullet_multimesh_instance);

    // Updates a specific debug multimesh's instance transforms so that each matches the corresponding bullets multimesh
    void update_debugger_shape_transforms(godot::MultiMeshInstance2D &debug_multimesh_instance, MultiMeshBullets2D &bullet_multimesh_instance);

protected:
    static void _bind_methods() {};
};

}
#endif