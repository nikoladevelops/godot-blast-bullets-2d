#include "multi_mesh_bullets_debugger2d.hpp"
#include "../bullets/multi_mesh_bullets2d.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multi_mesh_instance2d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>

using namespace godot;

namespace BlastBullets {

void MultiMeshBulletsDebugger2D::configure(godot::Node* new_bullets_container, const godot::String& new_debugger_name, const godot::Color &new_multimesh_color){
    physics_server = PhysicsServer2D::get_singleton();
    bullets_container_ptr = new_bullets_container;
    multimesh_color = new_multimesh_color;
    
    // When a bullet multimesh gets added to the bullet container, run generate_texture_multimesh
    bullets_container_ptr->connect("child_entered_tree", callable_mp(this, &MultiMeshBulletsDebugger2D::generate_debug_multimesh));
    set_name(new_debugger_name);
}

void MultiMeshBulletsDebugger2D::reset_debugger() {
    set_physics_process(false);

    for (int i = 0; i < debugger_multimeshes.size(); i++) {
        debugger_multimeshes[i]->queue_free();
    }
    debugger_multimeshes.resize(0);
    bullet_multimeshes.resize(0);

    set_physics_process(true);
}

void MultiMeshBulletsDebugger2D::activate(){
    set_physics_process(true); // very important to do this first, because of is_physics_processing() check in the generate texture multimesh method
    
    // In case the bullets container already has multimesh bullets generate a texture multimesh for each
    TypedArray<Node> already_spawned_multimeshes = bullets_container_ptr->get_children();
    for (int i = 0; i < already_spawned_multimeshes.size(); i++)
    {
        MultiMeshBullets2D *bullet_instance = Object::cast_to<MultiMeshBullets2D>(already_spawned_multimeshes[i]);
        generate_debug_multimesh(bullet_instance);
    }
}

void MultiMeshBulletsDebugger2D::disable(){
    set_physics_process(false);

    for (int i = 0; i < debugger_multimeshes.size(); i++) {
        debugger_multimeshes[i]->queue_free(); // again, I'm only freeing debugger_multimeshes, but leaving the bullets multimeshes alive
    }

    // Resize both vectors so they contain 0 elements
    debugger_multimeshes.resize(0);
    bullet_multimeshes.resize(0);
}

void MultiMeshBulletsDebugger2D::generate_debug_multimesh(MultiMeshBullets2D *new_bullets_multimesh) {
    // In case the debugger was disabled, don't generate anything - this is needed because I have a signal that is registered with this method so it will run it even when physics processing is disabled (another way of protecting against this is just disconnecting the signal, but I chose this way)
    if(is_physics_processing() == false){
        return;
    }


    // Get the physics rectangular shape of the first bullet inside bullet multimesh (basically gets it's collision shape)
    RID first_bullet_shape = physics_server->area_get_shape(new_bullets_multimesh->area, 0);

    // Get the size of the collision shape
    Vector2 shape_size = physics_server->shape_get_data(first_bullet_shape);
    
    // The physics server in Godot creates shape sizes by half extents when dealing with RectangleShape and it also returns half extents every time you use shape_get_data or shape_set_data..
    // Example: If the bullet physics rectangle shape was originally created using shape_set_data with argument Vector2(16,16) this would mean that Godot has created a shape with half the width being 16 and half the height being also 16, meaning we are dealing with a rectangle shape with the size Vector2(32,32)
    // So because shape_get_data returns the half extents, I need to multiply it by 2 to get the actual size that the QuadMesh I'm trying to create will have (this is so I can use a QuadMesh to represent the actual bullet collision shape)
    // This is because setting the QuadMesh size works with normal sizing settings - meaning it expects the ACTUAL SIZE and not THE HALF EXTENTS that we get from shape_get_data
    // Note that I only know that shape_get_data returns half_extents because BlastBullets2D is hard coded to use only RectangleShape as the physics collision shape type for all bullets (everything is Rectangles basically)
    // So if it changes it the future there is a lot of code including this one over here that would need more complex logic to deal with each different physics shape type

    shape_size = shape_size*2; // now this represents the actual size that the QuadMesh will have

    // Create QuadMesh that will match the size of the physics collision shape exactly
    Ref<QuadMesh> new_mesh = memnew(QuadMesh);
    new_mesh->set_size(shape_size);

    // Create a multimesh
    Ref<MultiMesh> multi = memnew(MultiMesh);

    multi->set_mesh(new_mesh);
    multi->set_use_colors(true);

    int instance_count = new_bullets_multimesh->size;
    multi->set_instance_count(instance_count);

    // Set up the multimesh instance
    MultiMeshInstance2D *texture_multi_instance = memnew(MultiMeshInstance2D);
    texture_multi_instance->set_multimesh(multi);

    RID area = new_bullets_multimesh->area;

    for (int i = 0; i < instance_count; i++) {
        multi->set_instance_color(i, multimesh_color);
        Transform2D &transf = new_bullets_multimesh->all_cached_shape_transforms[i];
        multi->set_instance_transform_2d(i, transf);
    }

    bullet_multimeshes.push_back(new_bullets_multimesh);
    debugger_multimeshes.push_back(texture_multi_instance);

    add_child(texture_multi_instance);
}

void MultiMeshBulletsDebugger2D::ensure_quadmesh_matches_physics_shape_size(MultiMeshInstance2D &debug_multimesh_instance, MultiMeshBullets2D &bullet_multimesh_instance){
    Ref<MultiMesh> &debug_inner_multi = debug_multimesh_instance.get_multimesh();
    Ref<Mesh> &debug_mesh = debug_inner_multi->get_mesh();

    // Get the physics rectangular shape of the first bullet inside bullet multimesh (basically gets it's collision shape)
    RID first_bullet_shape = physics_server->area_get_shape(bullet_multimesh_instance.area, 0);

    // Get the size of the collision shape
    Vector2 shape_size = physics_server->shape_get_data(first_bullet_shape);
    
    shape_size = shape_size*2; // now this represents the actual size that the QuadMesh supposedly already has

    // Cast the mesh ptr to a quadmesh ptr because we already know that the debugger uses QuadMeshes only
    QuadMesh *quad_mesh_ptr = static_cast<QuadMesh *>(debug_mesh.ptr());

    // If the physics shape size has changed inside the bullet multimesh, then the debug multimesh should also match the change
    if(shape_size != quad_mesh_ptr->get_size()){
        quad_mesh_ptr->set_size(shape_size);
    }
}

void MultiMeshBulletsDebugger2D::update_debugger_shape_transforms(MultiMeshInstance2D &debug_multimesh_instance, MultiMeshBullets2D &bullet_multimesh_instance) {
    Ref<MultiMesh> &debug_inner_multi = debug_multimesh_instance.get_multimesh();
    int amount_debug_shapes = debug_inner_multi->get_instance_count();

    for (int i = 0; i < amount_debug_shapes; i++) {
        Transform2D &bullet_collision_shape_transf = bullet_multimesh_instance.all_cached_shape_transforms[i];
        debug_inner_multi->set_instance_transform_2d(i, bullet_collision_shape_transf);
    }
}

void MultiMeshBulletsDebugger2D::free_debug_multimeshes_tracking_disabled_bullets(){
    set_physics_process(false);

    int size = debugger_multimeshes.size();

    // Need to create 2 brand new vectors containing only the valid active multimeshes.
    std::vector<MultiMeshBullets2D *> active_bullet_multimeshes;
    std::vector<MultiMeshInstance2D *> active_debugger_multimeshes;

    for (int i = 0; i < size; i++)
    {
        if(bullet_multimeshes[i]->active_bullets_counter == 0){ // if the bullets multimesh is disabled
            debugger_multimeshes[i]->queue_free(); // free the texture multimesh that corresponds to the disabled bullet multimesh
            continue;
        }
        
        // Push only active multimeshes to these vectors
        active_bullet_multimeshes.push_back(bullet_multimeshes[i]);
        active_debugger_multimeshes.push_back(debugger_multimeshes[i]);
    }

    bullet_multimeshes = active_bullet_multimeshes;
    debugger_multimeshes = active_debugger_multimeshes;

    set_physics_process(true);
}

void MultiMeshBulletsDebugger2D::free_debug_multimeshes_tracking_disabled_bullets(int amount_bullets){
    set_physics_process(false);

    int size = debugger_multimeshes.size();

    // Need to create 2 brand new vectors containing only the valid active multimeshes.
    std::vector<MultiMeshBullets2D *> active_bullet_multimeshes;
    std::vector<MultiMeshInstance2D *> active_debugger_multimeshes;

    for (int i = 0; i < size; i++)
    {
        if(bullet_multimeshes[i]->active_bullets_counter == 0 && bullet_multimeshes[i]->size == amount_bullets){ // if the multimesh is disabled and it has the exact `amount_bullets` instances, then the texture multimesh has to be freed
            debugger_multimeshes[i]->queue_free(); // free the texture multimesh that corresponds to the disabled bullet multimesh that also has `amount_bullets` instances
            continue;
        }

        active_bullet_multimeshes.push_back(bullet_multimeshes[i]);
        active_debugger_multimeshes.push_back(debugger_multimeshes[i]);
        
    }

    bullet_multimeshes = active_bullet_multimeshes;
    debugger_multimeshes = active_debugger_multimeshes;

    set_physics_process(true);
}

void MultiMeshBulletsDebugger2D::change_debug_multimeshes_color(const Color &new_multimesh_color){
    multimesh_color = new_multimesh_color;
    int amount_texture_multis = debugger_multimeshes.size();

    // For each texture multi mesh
    for (int i = 0; i < amount_texture_multis; i++)
    {
        Ref<MultiMesh> &multi = debugger_multimeshes[i]->get_multimesh();
        int amount_bullet_instances = multi->get_instance_count();

        // For each debug shape inside texture multi mesh
        for (int j = 0; j < amount_bullet_instances; j++)
        {
            // Set its color to the new one
            multi->set_instance_color(j, multimesh_color);
        }
    }
}

void MultiMeshBulletsDebugger2D::_physics_process(float delta) {
    int amount_debug_multimeshes = debugger_multimeshes.size();

    for (int i = 0; i < amount_debug_multimeshes; i++) {
        
        MultiMeshBullets2D &current_bulllets_multimesh_instance = *bullet_multimeshes[i];

        // If the bullets multimesh is disabled then there is no need to update the debug shapes so they match the physics shapes' transforms
        if(current_bulllets_multimesh_instance.active_bullets_counter == 0){
            continue;
        }

        MultiMeshInstance2D &current_debug_multimesh_instance = *debugger_multimeshes[i];

        // Ensure the quadmesh is created and matches the physics collision shape size of the bullets
        ensure_quadmesh_matches_physics_shape_size(current_debug_multimesh_instance, current_bulllets_multimesh_instance);
        
        // Move each debug shape to match the transform of the bullet multimesh's physics shape
        update_debugger_shape_transforms(current_debug_multimesh_instance, current_bulllets_multimesh_instance);
    }
}
}