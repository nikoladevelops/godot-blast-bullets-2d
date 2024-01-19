#ifndef SAVE_DATA_BULLET_FACTORY2D
#define SAVE_DATA_BULLET_FACTORY2D

#include "godot_cpp/classes/resource.hpp"
#include "save_data_block_bullets2d.hpp"

using namespace godot;
class SaveDataBulletFactory2D : public Resource{
    GDCLASS(SaveDataBulletFactory2D, Resource)
    
    public:
        TypedArray<SaveDataBlockBullets2D> all_block_bullets;


        void set_all_block_bullets(const TypedArray<SaveDataBlockBullets2D> &new_all_block_bullets);
        TypedArray<SaveDataBlockBullets2D> get_all_block_bullets() const;
    protected:
        static void _bind_methods();
};

#endif