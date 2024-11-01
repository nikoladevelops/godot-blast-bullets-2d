#ifndef SAVE_DATA_BULLET_FACTORY2D
#define SAVE_DATA_BULLET_FACTORY2D

#include "save_data_block_bullets2d.hpp"
#include <godot_cpp/classes/resource.hpp>

namespace BlastBullets {

class SaveDataBulletFactory2D : public godot::Resource {
    GDCLASS(SaveDataBulletFactory2D, godot::Resource)

public:
    godot::TypedArray<SaveDataBlockBullets2D> all_block_bullets;

    void set_all_block_bullets(const godot::TypedArray<SaveDataBlockBullets2D> &new_all_block_bullets);
    godot::TypedArray<SaveDataBlockBullets2D> get_all_block_bullets() const;

protected:
    static void _bind_methods();
};

}
#endif