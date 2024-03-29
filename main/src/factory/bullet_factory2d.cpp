#include "bullet_factory2d.hpp"
#include "godot_cpp/classes/world2d.hpp"

#include "godot_cpp/variant/utility_functions.hpp"
#include "../spawn-data/block_bullets_data2d.hpp"
#include "../save-data/save_data_bullet_factory2d.hpp"

#include "../bullets/block_bullets2d.hpp" 
#include "godot_cpp/classes/engine.hpp"

#include "../debugger/bullet_debugger2d.hpp"

using namespace godot;

void BulletFactory2D::_ready(){
    if(Engine::get_singleton()->is_editor_hint()){
        return;
    }

    if(physics_space.is_valid() == false){
        physics_space = get_world_2d()->get_space();
    }

    bullets_container = memnew(Node);
    bullets_container->set_name("BulletsContainer");

    add_child(bullets_container);

    if(is_debugger_enabled){
        debugger=memnew(BulletDebugger2D);
        debugger->bullets_container_ptr = bullets_container;
        add_child(debugger);
    }
}

void BulletFactory2D::spawnBlockBullets2D(const Ref<BlockBulletsData2D> spawn_data){
    int key = spawn_data->transforms.size();
    
    BlockBullets2D* bullets = remove_bullets_from_pool(key);
    if(bullets != nullptr){
        bullets->activate_multimesh(spawn_data);
        return;
    }


    BlockBullets2D* blk_instance = memnew(BlockBullets2D);
    blk_instance->spawn(spawn_data, this);
}

RID BulletFactory2D::get_physics_space() const{
    return physics_space;
}
void BulletFactory2D::set_physics_space(RID new_space_rid){
    physics_space=new_space_rid;
}

Ref<SaveDataBulletFactory2D> BulletFactory2D::save(){
    Ref<SaveDataBulletFactory2D> data = memnew(SaveDataBulletFactory2D);

    TypedArray<BlockBullets2D> bullets = bullets_container->get_children();

    for (int i = 0; i < bullets.size(); i++)
    {
        BlockBullets2D& bullet_instance = *Object::cast_to<BlockBullets2D>(bullets[i]);

        // I only want to save bullets that are still active (I don't want to save bullets that are in the pool).
        if(bullet_instance.current_life_time == 0.0f){
            continue;
        }
        // Saves only the active bullets currently
        data->all_block_bullets.push_back(bullet_instance.save());
    }
    emit_signal("finished_saving");
    return data;
}
void BulletFactory2D::load(Ref<SaveDataBulletFactory2D> new_data){
    // Load all new bullets
    int amount_bullets = new_data->all_block_bullets.size();
    for (int i = 0; i < amount_bullets ; i++)
    {
        BlockBullets2D* blk_instance = memnew(BlockBullets2D);
        blk_instance->load(new_data->all_block_bullets[i], this);
    }
    emit_signal("finished_loading");
}

void BulletFactory2D::add_bullets_to_pool(BlockBullets2D* new_bullets){
    int key = new_bullets->size;
    block_bullets_pool[key].push(new_bullets);
}

BlockBullets2D* BulletFactory2D::remove_bullets_from_pool(int key){
    auto result = block_bullets_pool.find(key);
    // If the block_bullets_pool doesn't contain a queue with that key or if it does but the queue is empty (meaning no bullets) return a nullptr
    if(result == block_bullets_pool.end() || result->second.size() == 0){
        return nullptr;
    }

    // Get the first bullets in the queue
    BlockBullets2D* bullets = result->second.front();
    // Remove them from the queue
    result->second.pop();

    return bullets;
}

void BulletFactory2D::clear_all_bullets(){
    // It's important to reset the debugger
    if(debugger != nullptr){
        debugger->reset_debugger();
    }
    TypedArray<Node> allCurrentBullets = bullets_container->get_children();
    
    int size = allCurrentBullets.size();

    for (int i = 0; i < size; i++) {
        BlockBullets2D* curr_bullet = Object::cast_to<BlockBullets2D>(allCurrentBullets[i]);
        if(curr_bullet != nullptr){
            curr_bullet->safe_delete();
        }
    }

    block_bullets_pool.clear();
    emit_signal("finished_clearing");
}

bool BulletFactory2D::get_is_debugger_enabled(){
    return is_debugger_enabled;
}

void BulletFactory2D::set_is_debugger_enabled(bool new_is_enabled){
    is_debugger_enabled=new_is_enabled;
}

void BulletFactory2D::_bind_methods(){
    ClassDB::bind_method(D_METHOD("get_physics_space"), &BulletFactory2D::get_physics_space);
    ClassDB::bind_method(D_METHOD("set_physics_space", "new_physics_space"), &BulletFactory2D::set_physics_space);
    ADD_PROPERTY(PropertyInfo(Variant::RID, "physics_space"), "set_physics_space", "get_physics_space");

    ClassDB::bind_method(D_METHOD("get_is_debugger_enabled"), &BulletFactory2D::get_is_debugger_enabled);
    ClassDB::bind_method(D_METHOD("set_is_debugger_enabled", "new_is_enabled"), &BulletFactory2D::set_is_debugger_enabled);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_debugger_enabled"), "set_is_debugger_enabled", "get_is_debugger_enabled");
    
    ClassDB::bind_method(D_METHOD("spawnBlockBullets2D", "spawn_data"), &BulletFactory2D::spawnBlockBullets2D);

    ClassDB::bind_method(D_METHOD("save"), &BulletFactory2D::save);
    ClassDB::bind_method(D_METHOD("load", "new_data"), &BulletFactory2D::load);

    ClassDB::bind_method(D_METHOD("clear_all_bullets"), &BulletFactory2D::clear_all_bullets);

    ADD_SIGNAL(MethodInfo("area_entered", PropertyInfo(Variant::OBJECT, "enemy_area"), PropertyInfo(Variant::OBJECT, "bullets_custom_data", PROPERTY_HINT_RESOURCE_TYPE, "Resource"), PropertyInfo(Variant::VECTOR2, "bullet_global_position")));
    ADD_SIGNAL(MethodInfo("body_entered", PropertyInfo(Variant::OBJECT, "enemy_body"), PropertyInfo(Variant::OBJECT, "bullets_custom_data", PROPERTY_HINT_RESOURCE_TYPE, "Resource"), PropertyInfo(Variant::VECTOR2, "bullet_global_position")));

    ADD_SIGNAL(MethodInfo("finished_saving"));
    ADD_SIGNAL(MethodInfo("finished_loading"));
    ADD_SIGNAL(MethodInfo("finished_clearing"));
}