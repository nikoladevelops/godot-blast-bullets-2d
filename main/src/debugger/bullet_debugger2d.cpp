#include "bullet_debugger2d.hpp"
#include "../bullets/block_bullets2d.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/classes/engine.hpp"

#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/classes/window.hpp"
#include "godot_cpp/classes/quad_mesh.hpp"

#define physics_server PhysicsServer2D::get_singleton()

using namespace godot;
void BulletDebugger2D::_ready(){
    if(Engine::get_singleton()->is_editor_hint()){
        return;
    }

    if(is_enabled == false){
        set_physics_process(false);
        set_process(false);
        return;
    }
    

    if(bullet_factory.is_empty()){
        UtilityFunctions::print("You haven't provided the bullet_factory for the debugger");
        set_physics_process(false);
        set_process(false);
        return;
    }

    bullet_factory_ptr=get_node<BulletFactory2D>(bullet_factory);
    bullet_factory_ptr->connect("bullets_got_cleared", callable_mp(this, &BulletDebugger2D::reset_debugger));

    bullets_container_ptr = bullet_factory_ptr->bullets_container;
    bullets_container_ptr->connect("child_entered_tree", callable_mp(this, &BulletDebugger2D::bullets_entered_container));
    
}

void BulletDebugger2D::reset_debugger(){
    set_physics_process(false);

    for (int i = 0; i < texture_multi_meshes.size(); i++)
    {
        texture_multi_meshes[i]->queue_free();
    }
    texture_multi_meshes.clear();
    bullets_multi_meshes.clear();

    set_physics_process(true);
}


void BulletDebugger2D::bullets_entered_container(Node* node){
    BlockBullets2D* new_bullets_multi_mesh = reinterpret_cast<BlockBullets2D*>(node);

    new_bullets_multi_mesh->connect("spawned", callable_mp(this, &BulletDebugger2D::generate_texture_multimesh).bind(new_bullets_multi_mesh));
    
}
void BulletDebugger2D::generate_texture_multimesh(BlockBullets2D* new_bullets_multi_mesh){
    bullets_multi_meshes.push_back(new_bullets_multi_mesh);
    // Set up a mesh
    Ref<QuadMesh> mesh = memnew(QuadMesh);

    RID shape = physics_server->area_get_shape(new_bullets_multi_mesh->area, 0);
    Vector2 size = physics_server->shape_get_data(shape);
    mesh->set_size(size*2); // because I'm using rectangles as the collision shape, the function will give me the half extents of width/height so I need to multiply by 2 to get the actual size

    // Set up the multimesh
    Ref<MultiMesh>multi = memnew(MultiMesh);
    multi->set_mesh(mesh);
    
    multi->set_use_colors(true);
    int instance_count = new_bullets_multi_mesh->size;
    multi->set_instance_count(instance_count);

    // Set up the multimesh instance
    MultiMeshInstance2D* texture_multi_instance = memnew(MultiMeshInstance2D);
    texture_multi_instance->set_multimesh(multi);

    RID area = new_bullets_multi_mesh->area;

    for (int i = 0; i < instance_count; i++)
    {
        multi->set_instance_color(i, Color(0,0,2,0.5));
        Transform2D transf = physics_server->area_get_shape_transform(area, i);
        multi->set_instance_transform_2d(i,transf);
    }
    

    texture_multi_meshes.push_back(texture_multi_instance); // make sure to actually add it to the array that stores them
    add_child(texture_multi_instance); // add it as a child, so that it is in the game
}


void BulletDebugger2D::update_instance_transforms(
    MultiMeshInstance2D* texture_multi, 
    BlockBullets2D* bullets_multi){
    
    RID area = bullets_multi->area;
    Ref<MultiMesh> texture_inner_multi = texture_multi->get_multimesh();
    for (int i = 0; i < texture_multi->get_multimesh()->get_instance_count(); i++)
    { 
        Transform2D bullet_collision_shape_transf = physics_server->area_get_shape_transform(area,i);
        texture_inner_multi->set_instance_transform_2d(i, bullet_collision_shape_transf);
    }
}

void BulletDebugger2D::_physics_process(float delta){
    for (int i = 0; i < texture_multi_meshes.size(); i++)
    {
        update_instance_transforms(texture_multi_meshes[i], bullets_multi_meshes[i]);
    }
}

NodePath BulletDebugger2D::get_bullet_factory() const{
    return bullet_factory;
}
void BulletDebugger2D::set_bullet_factory(const NodePath& new_bullet_factory){
    bullet_factory=new_bullet_factory;
}

bool BulletDebugger2D::get_is_enabled(){
    return is_enabled;
}

void BulletDebugger2D::set_is_enabled(bool new_is_enabled){
    is_enabled=new_is_enabled;
}

void BulletDebugger2D::_bind_methods(){
    ClassDB::bind_method(D_METHOD("get_is_enabled"), &BulletDebugger2D::get_is_enabled);
    ClassDB::bind_method(D_METHOD("set_is_enabled", "new_is_enabled"), &BulletDebugger2D::set_is_enabled);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_enabled"), "set_is_enabled", "get_is_enabled");

    ClassDB::bind_method(D_METHOD("get_bullet_factory"), &BulletDebugger2D::get_bullet_factory);
    ClassDB::bind_method(D_METHOD("set_bullet_factory", "new_bullet_factory"), &BulletDebugger2D::set_bullet_factory);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "bullet_factory"), "set_bullet_factory", "get_bullet_factory");
}