#include "multi_mesh_bullets_debugger2d.hpp"
#include "../bullets/multi_mesh_bullets2d.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multi_mesh_instance2d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>

using namespace godot;

namespace BlastBullets {

void MultiMeshBulletsDebugger2D::configure(godot::Node* new_bullets_container, const godot::String& new_debugger_name, const godot::Color &new_multi_mesh_color){
    physics_server = PhysicsServer2D::get_singleton();
    bullets_container_ptr = new_bullets_container;
    multi_mesh_color = new_multi_mesh_color;
    
    // When a bullet multimesh gets added to the bullet container, run generate_texture_multimesh
    bullets_container_ptr->connect("child_entered_tree", callable_mp(this, &MultiMeshBulletsDebugger2D::generate_texture_multimesh));
    set_name(new_debugger_name);
}

void MultiMeshBulletsDebugger2D::reset_debugger() {
    set_physics_process(false);

    for (int i = 0; i < texture_multi_meshes.size(); i++) {
        texture_multi_meshes[i]->queue_free();
    }
    texture_multi_meshes.resize(0);
    bullets_multi_meshes.resize(0);

    set_physics_process(true);
}

void MultiMeshBulletsDebugger2D::activate(){
    set_physics_process(true); // very important to do this first, because of is_physics_processing() check in the generate texture multimesh method
    
    // In case the bullets container already has multimesh bullets generate a texture multimesh for each
    TypedArray<Node> already_spawned_multimeshes = bullets_container_ptr->get_children();
    for (int i = 0; i < already_spawned_multimeshes.size(); i++)
    {
        MultiMeshBullets2D *bullet_instance = Object::cast_to<MultiMeshBullets2D>(already_spawned_multimeshes[i]);
        generate_texture_multimesh(bullet_instance);
    }
}

void MultiMeshBulletsDebugger2D::disable(){
    set_physics_process(false);

    for (int i = 0; i < texture_multi_meshes.size(); i++) {
        texture_multi_meshes[i]->queue_free(); // again, I'm only freeing texture_multi_meshes, but leaving the bullets multimeshes alive
    }

    // Resize both vectors so they contain 0 elements
    texture_multi_meshes.resize(0);
    bullets_multi_meshes.resize(0);
}

void MultiMeshBulletsDebugger2D::generate_texture_multimesh(MultiMeshBullets2D *new_bullets_multi_mesh) {
    // In case the debugger was disabled, don't generate anything - this is needed because I have a signal that is registered with this method so it will run it even when physics processing is disabled (another way of protecting against this is just disconnecting the signal, but I chose this way)
    if(is_physics_processing() == false){
        return;
    }

    bullets_multi_meshes.push_back(new_bullets_multi_mesh);
    // Set up a mesh
    Ref<QuadMesh> mesh = memnew(QuadMesh);

    RID shape = physics_server->area_get_shape(new_bullets_multi_mesh->area, 0);
    Vector2 size = physics_server->shape_get_data(shape);
    mesh->set_size(size * 2); // because I'm using rectangles as the collision shape, the function will give me the half extents of width/height so I need to multiply by 2 to get the actual size

    // Set up the multimesh
    Ref<MultiMesh> multi = memnew(MultiMesh);
    multi->set_mesh(mesh);

    multi->set_use_colors(true);
    int instance_count = new_bullets_multi_mesh->size;
    multi->set_instance_count(instance_count);

    // Set up the multimesh instance
    MultiMeshInstance2D *texture_multi_instance = memnew(MultiMeshInstance2D);
    texture_multi_instance->set_multimesh(multi);

    RID area = new_bullets_multi_mesh->area;

    for (int i = 0; i < instance_count; i++) {
        multi->set_instance_color(i, multi_mesh_color);
        Transform2D &transf = new_bullets_multi_mesh->all_cached_shape_transforms[i];
        multi->set_instance_transform_2d(i, transf);
    }

    texture_multi_meshes.push_back(texture_multi_instance); // store it in the vector
    add_child(texture_multi_instance);
}

void MultiMeshBulletsDebugger2D::update_instance_transforms(
    MultiMeshInstance2D *texture_multi,
    MultiMeshBullets2D *bullets_multi) {

    Ref<MultiMesh> texture_inner_multi = texture_multi->get_multimesh();
    for (int i = 0; i < texture_inner_multi->get_instance_count(); i++) {
        Transform2D &bullet_collision_shape_transf = bullets_multi->all_cached_shape_transforms[i];
        texture_inner_multi->set_instance_transform_2d(i, bullet_collision_shape_transf);
    }
}

void MultiMeshBulletsDebugger2D::free_texture_multi_meshes_tracking_disabled_bullets(){
    set_physics_process(false);

    int size = texture_multi_meshes.size();

    // Need to create 2 brand new vectors containing only the valid active multimeshes.
    std::vector<MultiMeshBullets2D *> active_bullet_multi_meshes;
    std::vector<MultiMeshInstance2D *> active_texture_multi_meshes;

    for (int i = 0; i < size; i++)
    {
        if(bullets_multi_meshes[i]->active_bullets_counter == 0){ // if the bullets multimesh is disabled
            texture_multi_meshes[i]->queue_free(); // free the texture multimesh that corresponds to the disabled bullet multimesh
            continue;
        }
        
        // Push only active multimeshes to these vectors
        active_bullet_multi_meshes.push_back(bullets_multi_meshes[i]);
        active_texture_multi_meshes.push_back(texture_multi_meshes[i]);
    }

    bullets_multi_meshes = active_bullet_multi_meshes;
    texture_multi_meshes = active_texture_multi_meshes;

    set_physics_process(true);
}

void MultiMeshBulletsDebugger2D::free_texture_multi_meshes_tracking_disabled_bullets(int amount_bullets){
    set_physics_process(false);

    int size = texture_multi_meshes.size();

    // Need to create 2 brand new vectors containing only the valid active multimeshes.
    std::vector<MultiMeshBullets2D *> active_bullet_multi_meshes;
    std::vector<MultiMeshInstance2D *> active_texture_multi_meshes;

    for (int i = 0; i < size; i++)
    {
        if(bullets_multi_meshes[i]->active_bullets_counter == 0 && bullets_multi_meshes[i]->size == amount_bullets){ // if the multimesh is disabled and it has the exact `amount_bullets` instances, then the texture multimesh has to be freed
            texture_multi_meshes[i]->queue_free(); // free the texture multimesh that corresponds to the disabled bullet multimesh that also has `amount_bullets` instances
            continue;
        }

        active_bullet_multi_meshes.push_back(bullets_multi_meshes[i]);
        active_texture_multi_meshes.push_back(texture_multi_meshes[i]);
        
    }

    bullets_multi_meshes = active_bullet_multi_meshes;
    texture_multi_meshes = active_texture_multi_meshes;

    set_physics_process(true);
}

void MultiMeshBulletsDebugger2D::change_texture_multimeshes_color(const Color &new_multi_mesh_color){
    multi_mesh_color = new_multi_mesh_color;
    int amount_texture_multis = texture_multi_meshes.size();

    // For each texture multi mesh
    for (int i = 0; i < amount_texture_multis; i++)
    {
        Ref<MultiMesh> &multi = texture_multi_meshes[i]->get_multimesh();
        int amount_bullet_instances = multi->get_instance_count();

        // For each debug shape inside texture multi mesh
        for (int j = 0; j < amount_bullet_instances; j++)
        {
            // Set its color to the new one
            multi->set_instance_color(j, multi_mesh_color);
        }
    }
}

void MultiMeshBulletsDebugger2D::_physics_process(float delta) {
    int size = texture_multi_meshes.size();

    for (int i = 0; i < size; i++) {
        update_instance_transforms(texture_multi_meshes[i], bullets_multi_meshes[i]);
    }
}
}