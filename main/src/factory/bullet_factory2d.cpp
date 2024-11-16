#include "./bullet_factory2d.hpp"

#include "../bullets/block_bullets2d.hpp"
#include "../bullets/normal_bullets2d.hpp"

#include "../spawn-data/block_bullets_data2d.hpp"
#include "../spawn-data/normal_bullets_data2d.hpp"

#include "../save-data/save_data_bullet_factory2d.hpp"
#include "../save-data/save_data_block_bullets2d.hpp"
#include "../save-data/save_data_multi_mesh_bullets2d.hpp"

#include "../shared/multimesh_object_pool2d.hpp"
#include "../debugger/multi_mesh_bullets_debugger2d.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets {

void BulletFactory2D::_ready() {
    // Ensure the code that is next will not be ran in the editor
    if(Engine::get_singleton()->is_editor_hint()){
        return;
    }

    // Use default physics space if physics_space is invalid
    if (physics_space.is_valid() == false) {
        physics_space = get_world_2d()->get_space();
    }

    // Create BlockBulletsContainer Node and add it as a child to factory
    block_bullets_container = memnew(Node);
    block_bullets_container->set_name("BlockBulletsContainer");
    add_child(block_bullets_container);

    // Create NormalBulletsContainer Node and add it as a child to factory
    normal_bullets_container = memnew(Node);
    normal_bullets_container->set_name("NormalBulletsContainer");
    add_child(normal_bullets_container);

    // Add the debuggers as a child to the factory, but only if debugging is enabled
    if (is_debugger_enabled) {
        spawn_debuggers();
    }

    is_ready = true;
}

void BulletFactory2D::spawn_debuggers(){
    // Configure BlockBullets2D debugger and add it as a child to factory
    block_bullets_debugger = memnew(MultiMeshBulletsDebugger2D);
    block_bullets_debugger->configure(block_bullets_container, "BlockBulletsDebugger", block_bullets_debugger_color);

    // Configure NormalBullets2D debugger and add it as a child to factory
    normal_bullets_debugger = memnew(MultiMeshBulletsDebugger2D);
    normal_bullets_debugger->configure(normal_bullets_container, "NormalBulletsDebugger", normal_bullets_debugger_color);

    // Add the debuggers as children of the factory
    add_child(block_bullets_debugger);
    add_child(normal_bullets_debugger);
}

void BulletFactory2D::spawn_block_bullets(const Ref<BlockBulletsData2D> &spawn_data) {
    int key = spawn_data->transforms.size();

    BlockBullets2D* bullets_instance = dynamic_cast<BlockBullets2D*>(block_bullets_pool.pop(key));
    if(bullets_instance != nullptr){
        bullets_instance->activate_multimesh(spawn_data);
        return;
    }

    bullets_instance = memnew(BlockBullets2D);

    bullets_instance->spawn(spawn_data, &block_bullets_pool, this, block_bullets_container);
}

void BulletFactory2D::spawn_normal_bullets(const godot::Ref<NormalBulletsData2D> &spawn_data){
    int key = spawn_data->transforms.size();

    NormalBullets2D* bullets_instance = dynamic_cast<NormalBullets2D*>(normal_bullets_pool.pop(key));
    if(bullets_instance != nullptr){
        bullets_instance->activate_multimesh(spawn_data);
        return;
    }

    bullets_instance = memnew(NormalBullets2D);
    bullets_instance->spawn(spawn_data, &normal_bullets_pool, this, normal_bullets_container);
}

Ref<SaveDataBulletFactory2D> BulletFactory2D::save() {
    Ref<SaveDataBulletFactory2D> data = memnew(SaveDataBulletFactory2D);

    // Save all BlockBullets2D
    TypedArray<BlockBullets2D> block_bullets = block_bullets_container->get_children();

    for (int i = 0; i < block_bullets.size(); i++) {
        BlockBullets2D &bullet_instance = *Object::cast_to<BlockBullets2D>(block_bullets[i]);

        // I only want to save bullets that are still active (I don't want to save bullets that are in the pool)
        if (bullet_instance.active_bullets_counter == 0) {
            continue;
        }

        Ref<SaveDataBlockBullets2D> empty_data = memnew(SaveDataBlockBullets2D);
        
        // Saves only the active bullets
        data->all_block_bullets.push_back(bullet_instance.save(empty_data));
    }
    //

    // Save all NormalBullets2D
    TypedArray<NormalBullets2D> normal_bullets = normal_bullets_container->get_children();

    for (int i = 0; i < normal_bullets.size(); i++) {
        NormalBullets2D &bullet_instance = *Object::cast_to<NormalBullets2D>(normal_bullets[i]);

        // I only want to save bullets that are still active (I don't want to save bullets that are in the pool)
        if (bullet_instance.active_bullets_counter == 0) {
            continue;
        }

        Ref<SaveDataNormalBullets2D> empty_data = memnew(SaveDataNormalBullets2D);
        
        // Saves only the active bullets
        data->all_normal_bullets.push_back(bullet_instance.save(empty_data));
    }
    //

    emit_signal("finished_saving");
    return data;
}
void BulletFactory2D::load(const Ref<SaveDataBulletFactory2D> &new_data) {
    // Load all BlockBullets2D
    int amount_bullets = new_data->all_block_bullets.size();
    for (int i = 0; i < amount_bullets ; i++)
    {
        BlockBullets2D* blk_instance = memnew(BlockBullets2D);
        blk_instance->load(new_data->all_block_bullets[i], &block_bullets_pool, this, block_bullets_container);
    }
    //

    // Load all NormalBullets2D
    amount_bullets = new_data->all_normal_bullets.size();
    for (int i = 0; i < amount_bullets ; i++)
    {
        NormalBullets2D* blk_instance = memnew(NormalBullets2D);
        blk_instance->load(new_data->all_normal_bullets[i], &normal_bullets_pool, this, normal_bullets_container);
    }
    //

    emit_signal("finished_loading");
}

void BulletFactory2D::free_all_bullets() {
    // Empty out the object pools so they won't contain invalid pointers
    block_bullets_pool.clear_bullet_pointers();
    normal_bullets_pool.clear_bullet_pointers();

    // Reset all debuggers if they were enabled
    if(is_debugger_enabled){
        block_bullets_debugger->reset_debugger();
        normal_bullets_debugger->reset_debugger();
    }

    // Clear all BlockBullets2D
    TypedArray<Node> all_block_bullet_instances = block_bullets_container->get_children();

    int size = all_block_bullet_instances.size();
    for (int i = 0; i < size; i++) {
        BlockBullets2D* curr_bullet = Object::cast_to<BlockBullets2D>(all_block_bullet_instances[i]);
        if(curr_bullet != nullptr){
            curr_bullet->safe_delete();
        }
    }

    // Clear all NormalBullets2D
    TypedArray<Node> all_normal_bullet_instances = normal_bullets_container->get_children();

    size = all_normal_bullet_instances.size();
    for (int i = 0; i < size; i++) {
        NormalBullets2D* curr_bullet = Object::cast_to<NormalBullets2D>(all_normal_bullet_instances[i]);
        if(curr_bullet != nullptr){
            curr_bullet->safe_delete();
        }
    }

    // Notify the user that all bullets have been freed/deleted
    emit_signal("finished_freeing_all_bullets");
}

void BulletFactory2D::free_all_pools(){
    if(is_debugger_enabled){
        // Free the debug multimeshes representing the collision shapes of all disabled bullets (all bullets in the object pool)
        block_bullets_debugger->free_debug_multimeshes_tracking_disabled_bullets();
        normal_bullets_debugger->free_debug_multimeshes_tracking_disabled_bullets();
    }

    // Free all multimesh bullets that are disabled (and we already know that all disabled bullets are always stored in the object pools so we accomplish this easily)
    block_bullets_pool.free_all_bullets();
    normal_bullets_pool.free_all_bullets();
}

void BulletFactory2D::free_multi_mesh_pool(MultiMeshBulletType bullet_multi_mesh_type, int amount_bullets){
    if(is_debugger_enabled){
        // Free the texture multimeshes representing the collision shapes of all disabled bullets that have exactly `amount_bullets` instances
        switch (bullet_multi_mesh_type){
            case BLOCK_BULLETS:
                block_bullets_debugger->free_debug_multimeshes_tracking_disabled_bullets(amount_bullets);
                break;
            case NORMAL_BULLETS:
                normal_bullets_debugger->free_debug_multimeshes_tracking_disabled_bullets(amount_bullets);
                break;
            default:
                break;
        }
    }

    // Free the actual multimesh bullet objects that are in the object pool and have exactly `amount_bullets` bullet instances each
    switch (bullet_multi_mesh_type){
        case BLOCK_BULLETS:
            block_bullets_pool.free_specific_bullets(amount_bullets);
            break;
        case NORMAL_BULLETS:
            normal_bullets_pool.free_specific_bullets(amount_bullets);
            break;
        default:
            break;
    }   
}

void BulletFactory2D::populate_normal_bullets_pool(int amount_multimesh_instances, int amount_bullets_each_multimesh_holds){
    for (int i = 0; i < amount_multimesh_instances; i++)
    {
        NormalBullets2D *multimesh_instance = memnew(NormalBullets2D);
        multimesh_instance->spawn_as_disabled_multimesh(amount_bullets_each_multimesh_holds, &normal_bullets_pool, this, normal_bullets_container);
    }
}

void BulletFactory2D::populate_block_bullets_pool(int amount_multimesh_instances, int amount_bullets_each_multimesh_holds){
    for (int i = 0; i < amount_multimesh_instances; i++)
    {
        BlockBullets2D *multimesh_instance = memnew(BlockBullets2D);
        multimesh_instance->spawn_as_disabled_multimesh(amount_bullets_each_multimesh_holds, &block_bullets_pool, this, block_bullets_container);
    }
}

RID BulletFactory2D::get_physics_space() const {
    return physics_space;
}
void BulletFactory2D::set_physics_space(RID new_space_rid) {
    physics_space = new_space_rid;
}

Color BulletFactory2D::get_block_bullets_debugger_color() const{
    return block_bullets_debugger_color;
}
void BulletFactory2D::set_block_bullets_debugger_color(const Color& new_color){
    block_bullets_debugger_color = new_color;

    // Ensure the code that is next will never run in the editor and never run unless the factory is ready in the scene tree
    if(Engine::get_singleton()->is_editor_hint() || is_ready == false){
        return;
    }

    if(block_bullets_debugger != nullptr){
        block_bullets_debugger->change_debug_multimeshes_color(new_color);
    }
}

Color BulletFactory2D::get_normal_bullets_debugger_color() const{
    return normal_bullets_debugger_color;
}
void BulletFactory2D::set_normal_bullets_debugger_color(const Color& new_color){
    normal_bullets_debugger_color = new_color;

    // Ensure the code that is next will never run in the editor and never run unless the factory is ready in the scene tree
    if(Engine::get_singleton()->is_editor_hint() || is_ready == false){
        return;
    }

    if(normal_bullets_debugger != nullptr){
        normal_bullets_debugger->change_debug_multimeshes_color(new_color);
    }
}

bool BulletFactory2D::get_is_debugger_enabled() const{
    return is_debugger_enabled;
}

void BulletFactory2D::set_is_debugger_enabled(bool new_is_enabled) {
    is_debugger_enabled = new_is_enabled;

    // Ensure the code that is next will never run in the editor and never run unless the factory is ready in the scene tree
    if(Engine::get_singleton()->is_editor_hint() || is_ready == false){
        return;
    }

    // If the user wants to enable the debuggers, but they dont exist, spawn them fully configured and also activate them so that if the bullets containers have any bullets, they get the corresponding debug texture shapes as well
    if(is_debugger_enabled && block_bullets_debugger == nullptr){
        spawn_debuggers();

        block_bullets_debugger->activate();
        normal_bullets_debugger->activate();
       
        return;
    }

    // If the user wants to enable the debuggers that were previously disabled at run time, just activate them
    if(is_debugger_enabled){
        block_bullets_debugger->activate();
        normal_bullets_debugger->activate();
    }   

    // If the user wants to disable the debuggers
    if(is_debugger_enabled == false){ // no need to check for nullptr since the debuggers 100% exist if we are doing this at run-time (to disable them, they have to be already enabled..)
        block_bullets_debugger->disable();
        normal_bullets_debugger->disable();
    }

}


void BulletFactory2D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_physics_space"), &BulletFactory2D::get_physics_space);
    ClassDB::bind_method(D_METHOD("set_physics_space", "new_physics_space"), &BulletFactory2D::set_physics_space);

    ClassDB::bind_method(D_METHOD("get_is_debugger_enabled"), &BulletFactory2D::get_is_debugger_enabled);
    ClassDB::bind_method(D_METHOD("set_is_debugger_enabled", "new_is_enabled"), &BulletFactory2D::set_is_debugger_enabled);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_debugger_enabled"), "set_is_debugger_enabled", "get_is_debugger_enabled");

    ClassDB::bind_method(D_METHOD("spawn_block_bullets", "spawn_data"), &BulletFactory2D::spawn_block_bullets);
    ClassDB::bind_method(D_METHOD("spawn_normal_bullets", "spawn_data"), &BulletFactory2D::spawn_normal_bullets);

    ClassDB::bind_method(D_METHOD("save"), &BulletFactory2D::save);
    ClassDB::bind_method(D_METHOD("load", "new_data"), &BulletFactory2D::load);

    ClassDB::bind_method(D_METHOD("free_all_bullets"), &BulletFactory2D::free_all_bullets);
    ClassDB::bind_method(D_METHOD("free_all_pools"), &BulletFactory2D::free_all_pools);
    ClassDB::bind_method(D_METHOD("free_multi_mesh_pool", "bullet_multi_mesh_type", "amount_bullets"), &BulletFactory2D::free_multi_mesh_pool);
    
    ClassDB::bind_method(D_METHOD("populate_normal_bullets_pool", "amount_multimesh_instances", "amount_bullets_each_multimesh_holds"), &BulletFactory2D::populate_normal_bullets_pool);
    ClassDB::bind_method(D_METHOD("populate_block_bullets_pool", "amount_multimesh_instances", "amount_bullets_each_multimesh_holds"), &BulletFactory2D::populate_block_bullets_pool);

    ClassDB::bind_method(D_METHOD("get_normal_bullets_debugger_color"), &BulletFactory2D::get_normal_bullets_debugger_color);
    ClassDB::bind_method(D_METHOD("set_normal_bullets_debugger_color", "new_color"), &BulletFactory2D::set_normal_bullets_debugger_color);
    ADD_PROPERTY(PropertyInfo(Variant::COLOR, "normal_bullets_debugger_color"), "set_normal_bullets_debugger_color", "get_normal_bullets_debugger_color");

    ClassDB::bind_method(D_METHOD("get_block_bullets_debugger_color"), &BulletFactory2D::get_block_bullets_debugger_color);
    ClassDB::bind_method(D_METHOD("set_block_bullets_debugger_color", "new_color"), &BulletFactory2D::set_block_bullets_debugger_color);
    ADD_PROPERTY(PropertyInfo(Variant::COLOR, "block_bullets_debugger_color"), "set_block_bullets_debugger_color", "get_block_bullets_debugger_color");


    ADD_SIGNAL(MethodInfo("area_entered", PropertyInfo(Variant::OBJECT, "enemy_area"), PropertyInfo(Variant::OBJECT, "bullets_custom_data", PROPERTY_HINT_RESOURCE_TYPE, "Resource"), PropertyInfo(Variant::VECTOR2, "bullet_global_position")));
    ADD_SIGNAL(MethodInfo("body_entered", PropertyInfo(Variant::OBJECT, "enemy_body"), PropertyInfo(Variant::OBJECT, "bullets_custom_data", PROPERTY_HINT_RESOURCE_TYPE, "Resource"), PropertyInfo(Variant::VECTOR2, "bullet_global_position")));

    ADD_SIGNAL(MethodInfo("finished_saving"));
    ADD_SIGNAL(MethodInfo("finished_loading"));
    ADD_SIGNAL(MethodInfo("finished_freeing_all_bullets"));

    // Need this in order to expose the enum constants to Godot Engine
    BIND_ENUM_CONSTANT(NORMAL_BULLETS);
	BIND_ENUM_CONSTANT(BLOCK_BULLETS);
}
}