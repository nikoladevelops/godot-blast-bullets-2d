#ifndef BULLET_FACTORY2D_HPP
#define BULLET_FACTORY2D_HPP

#include "../shared/multimesh_object_pool.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node2d.hpp>

namespace BlastBullets {
// Using forward declaration to avoid circular dependencies
class BlockBulletsData2D;
class NormalBulletsData2D;
class BulletDebugger2D;
class SaveDataBulletFactory2D;

// Creates bullets with different behaviour
class BulletFactory2D : public godot::Node2D {
    GDCLASS(BulletFactory2D, godot::Node2D)

public:
    // Holds all disabled BlockBullets2D
    MultiMeshObjectPool block_bullets_pool;
    // Holds all disabled NormalBullets2D
    MultiMeshObjectPool normal_bullets_pool;

    // The physics space where the bullets multimeshes are interacting with the world
    godot::RID physics_space;

    // Contains all block bullet multimesh instances
    godot::Node *block_bullets_container = nullptr;
    // Contains all normal bullet multimesh instances
    godot::Node *normal_bullets_container = nullptr;

    // The bullet debugger. It is enabled only if is_debugger_enabled is set to true
    BulletDebugger2D *debugger = nullptr;

    void _ready();

    void spawn_normal_bullets(const godot::Ref<NormalBulletsData2D> &spawn_data);
    void spawn_block_bullets(const godot::Ref<BlockBulletsData2D> &spawn_data);

    godot::RID get_physics_space() const;
    void set_physics_space(godot::RID new_space_rid);

    // Generates a Resource that contains every bullet's state
    godot::Ref<SaveDataBulletFactory2D> save();

    // Loads bullets by using a Resource that contains every bullet's state. Call this method using call_deffered to avoid crashes
    void load(const godot::Ref<SaveDataBulletFactory2D> &new_data);

    // Clears all bullets. Call this method using call_deffered to avoid crashes
    void clear_all_bullets();

    // Determines whether the debugger should be created and added to the scene tree
    bool is_debugger_enabled = false;

    bool get_is_debugger_enabled();
    void set_is_debugger_enabled(bool new_is_enabled);

protected:
    static void _bind_methods();
};
}
#endif