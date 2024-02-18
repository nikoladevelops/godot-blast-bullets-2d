#ifndef BULLET_FACTORY2D
#define BULLET_FACTORY2D

#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "../spawn-data/block_bullets_data2d.hpp"
#include "../save-data/save_data_bullet_factory2d.hpp"
#include <queue>

class BlockBullets2D; // using forward declaration to avoid circular dependencies

using namespace godot;

// Creates bullets with different behaviour
class BulletFactory2D:public Node2D{
    GDCLASS(BulletFactory2D, Node2D);
    public:
        std::vector<BlockBullets2D*> all_bullets;

        // The physics space where the bullets multimeshes are interacting with the world.
        RID physics_space;
        // Contains all bullet multi meshes. This is where the multimeshes get added as a child when calling a spawn method.
        Node* bullets_container;
        
        void _ready();
        void _physics_process(float delta);

        // If I pass by const reference it would be a big problem if I am reusing the same resource data to spawn multiple block bullets, that's why I prefer to copy it
        void spawnBlockBullets2D(const Ref<BlockBulletsData2D> spawn_data);

        RID get_physics_space() const;
        void set_physics_space(RID new_space_rid);

        Ref<SaveDataBulletFactory2D> save();

        // You should consider calling this method using .call_deferred() to avoid crashes
        void load(Ref<SaveDataBulletFactory2D> new_data);

        // Adds BlockBullets2D to pool
        void add_bullets_to_pool(BlockBullets2D* new_bullets);
        // Retrieves BlockBullets2D from pool
        BlockBullets2D* remove_bullets_from_pool(int key);

        // Clears all bullets. You should consider calling this method using .call_deferred() to avoid crashes
        void clear_all_bullets();

    protected:
        static void _bind_methods();
    private:
        // The key corresponds to the amount of bullets a bullets multimesh has, meanwhile the value corresponds to a queue that holds all of those that have that amount of bullets. Example: If key is 5, that means it holds all deactivated BlockBullets2D that each have 5 bullets (5 collision shapes, 5 texture instances that are currently invisible).
        std::unordered_map<int,std::queue<BlockBullets2D*>> block_bullets_pool;
};

#endif