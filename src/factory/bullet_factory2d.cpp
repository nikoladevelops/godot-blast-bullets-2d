#include "bullet_factory2d.hpp"
//#include "godot_cpp/classes/physics_server2d.hpp"
#include "godot_cpp/classes/world2d.hpp"

//#include "godot_cpp/classes/node.hpp"
//#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "../spawn-data/block_bullets_data2d.hpp"
#include "../save-data/save_data_bullet_factory2d.hpp"

#include "../bullets/block_bullets2d.hpp" 
#include "godot_cpp/classes/engine.hpp"

using namespace godot;

void BulletFactory2D::_ready(){
    if(Engine::get_singleton()->is_editor_hint() == false){
        if(physics_space.is_valid() == false){
            physics_space = get_world_2d()->get_space();
        }
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
    //blk_instance->queue_free();
}

Node* BulletFactory2D::get_bullets_container() const{
    return bullets_container;
}

void BulletFactory2D::set_bullets_container(Node* new_bullets_container){
    bullets_container = new_bullets_container;
}

RID BulletFactory2D::get_physics_space() const{
    return physics_space;
}
void BulletFactory2D::set_physics_space(RID new_space_rid){
    physics_space=new_space_rid;
}

Ref<SaveDataBulletFactory2D> BulletFactory2D::save(){
    Ref<SaveDataBulletFactory2D> data = memnew(SaveDataBulletFactory2D);

    int amount_bullets = bullets_container->get_child_count();
    data->all_block_bullets.resize(amount_bullets); // Todo need to contain the amount of block bullets to save/ a counter

    for (int i = 0; i < amount_bullets ; i++)
    {

        BlockBullets2D* bullet_instance = dynamic_cast<BlockBullets2D*>(bullets_container->get_child(i));
        
        data->all_block_bullets[i] = bullet_instance->save();
    }

    return data;
}
void BulletFactory2D::load(Ref<SaveDataBulletFactory2D> new_data){
    emit_signal("loading_began");
    //std::queue<BlockBullets2D*>& ptr = block_bullets_pool[8];

    //block_bullets_pool.clear(); // destroy the queues, otherwise they will just hold invalid pointers, because all old bullets will be deleted

    TypedArray<Node> allCurrentBullets(bullets_container->get_children());
    

    // for (int i = 0; i < bullets_container->get_child_count(); i++)
    // {
    //     bullets_container->get_child(0)->set_physics_process(false);
    //     bullets_container->get_child(0)->set_process(false);
    //     bullets_container->get_child(0)->queue_free();
    //     Object::cast_to<Node>(allCurrentBullets[i])->queue_free();
    // }
    

    // BlockBullets2D* blk_instance = memnew(BlockBullets2D);
    // add_child(blk_instance);
    // blk_instance->queue_free(); // works?

     int size = allCurrentBullets.size();
    //Free all old bullets
    for (int i = 0; i < size; i++) {
        Node* curr_bullet = Object::cast_to<Node>(allCurrentBullets[i]); // (Node*)&allCurrentBullets[i]; 

        curr_bullet->set_physics_process(false);
        curr_bullet->set_process(false);
        //bullets_container->remove_child(curr_bullet); // works
        curr_bullet->queue_free(); // does NOT work
        //curr_bullet->call_deferred("queue_free"); // also does NOT work
    }

    block_bullets_pool.clear();
    
    // Load all new bullets
    int amount_bullets =  new_data->all_block_bullets.size();
    for (int i = 0; i < amount_bullets ; i++)
    {
        BlockBullets2D* blk_instance = memnew(BlockBullets2D);
        blk_instance->load(new_data->all_block_bullets[i], this);
    }
}

void BulletFactory2D::add_bullets_to_pool(BlockBullets2D* new_bullets){
    int key = new_bullets->size;
    auto result = block_bullets_pool.find(key);

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




void BulletFactory2D::_bind_methods(){
    ClassDB::bind_method(D_METHOD("get_bullets_container"), &BulletFactory2D::get_bullets_container);
    ClassDB::bind_method(D_METHOD("set_bullets_container", "new_bullets_container"), &BulletFactory2D::set_bullets_container);

    ClassDB::bind_method(D_METHOD("get_physics_space"), &BulletFactory2D::get_physics_space);
    ClassDB::bind_method(D_METHOD("set_physics_space", "new_physics_space"), &BulletFactory2D::set_physics_space);
    ADD_PROPERTY(PropertyInfo(Variant::RID, "physics_space"), "set_physics_space", "get_physics_space");

    ClassDB::bind_method(D_METHOD("spawnBlockBullets2D", "spawn_data"), &BulletFactory2D::spawnBlockBullets2D);

    ClassDB::bind_method(D_METHOD("save"), &BulletFactory2D::save);
    ClassDB::bind_method(D_METHOD("load", "new_data"), &BulletFactory2D::load);



    ADD_SIGNAL(MethodInfo("area_entered", PropertyInfo(Variant::OBJECT, "area"), PropertyInfo(Variant::OBJECT, "custom_resource"), PropertyInfo(Variant::TRANSFORM2D, "bullet_last_transform")));
    ADD_SIGNAL(MethodInfo("body_entered", PropertyInfo(Variant::OBJECT, "body"), PropertyInfo(Variant::OBJECT, "custom_resource"), PropertyInfo(Variant::TRANSFORM2D, "bullet_last_transform")));

    // I need this signal so that the bullet debugger knows when to clear its vectors containing pointers as well as freeing the generated texture multimeshes
    ADD_SIGNAL(MethodInfo("loading_began"));
}