#ifndef BULLET_FACTORY2D_HPP
#define BULLET_FACTORY2D_HPP

#include "../shared/multimesh_object_pool.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node2d.hpp>

namespace BlastBullets {
// Using forward declaration to avoid circular dependencies
class BlockBulletsData2D;
class NormalBulletsData2D;
class MultiMeshBulletsDebugger2D;
class SaveDataBulletFactory2D;

// Creates bullets with different behaviour
class BulletFactory2D : public godot::Node2D {
    GDCLASS(BulletFactory2D, godot::Node2D)

public:
    enum BulletType{
        NORMAL_BULLETS,
        BLOCK_BULLETS
    };

    // Whether the factory was spawned correctly and the ready function finished
    bool is_ready=false;
    // Holds all disabled BlockBullets2D
    MultiMeshObjectPool block_bullets_pool;
    // Holds all disabled NormalBullets2D
    MultiMeshObjectPool normal_bullets_pool;

    // The physics space where the bullets multimeshes are interacting with the world
    godot::RID physics_space;

    // Contains all BlockBullets2D in the scene tree
    godot::Node *block_bullets_container = nullptr;
    // Contains all NormalBullets2D in the scene tree
    godot::Node *normal_bullets_container = nullptr;

    // Debugs the collision shapes of all BlockBullets2D when enabled
    MultiMeshBulletsDebugger2D *block_bullets_debugger = nullptr;
    // Debugs the collision shapes of all NormalBullets2D when enabled
    MultiMeshBulletsDebugger2D *normal_bullets_debugger = nullptr;

    // The color for the collision shapes of all BlockBullets2D
    godot::Color block_bullets_debugger_color = godot::Color(0, 0, 2, 0.8);
    // The color for the collision shapes of all NormalBullets2D
    godot::Color normal_bullets_debugger_color = godot::Color(0, 0, 2, 0.8);

    void _ready();
    // Spawns NormalBullets2D when given a resource containing all needed data
    void spawn_normal_bullets(const godot::Ref<NormalBulletsData2D> &spawn_data);
    // Spawns BlockBullets2D when given a resource containing all needed data
    void spawn_block_bullets(const godot::Ref<BlockBulletsData2D> &spawn_data);

    // Generates a Resource that contains every bullet's state
    godot::Ref<SaveDataBulletFactory2D> save();

    // Loads bullets by using a Resource that contains every bullet's state. Call this method using call_deffered to avoid crashes
    void load(const godot::Ref<SaveDataBulletFactory2D> &new_data);

    // Clears all bullets no matter if they are active/disabled. Call this method using call_deffered to avoid crashes
    void clear_all_bullets();

    // Spawns fully configured debuggers as children of the factory
    void spawn_debuggers();

    // Determines whether the debugger should be created and added to the scene tree
    bool is_debugger_enabled;

    // Clears every single object pool of every single bullet type
    void clear_all_pools();

    // Getters and setters

    godot::RID get_physics_space() const;
    void set_physics_space(godot::RID new_space_rid);

    bool get_is_debugger_enabled() const;
    void set_is_debugger_enabled(bool new_is_enabled);

    godot::Color get_block_bullets_debugger_color() const;
    void set_block_bullets_debugger_color(const godot::Color& new_color);

    godot::Color get_normal_bullets_debugger_color() const;
    void set_normal_bullets_debugger_color(const godot::Color& new_color);

protected:
    static void _bind_methods();
};

}

// Need this in order to expose the enum to Godot Engine
VARIANT_ENUM_CAST(BlastBullets::BulletFactory2D::BulletType);

#endif