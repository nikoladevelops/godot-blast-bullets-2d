#ifndef SAVE_DATA_BULLET_FACTORY2D_HPP
#define SAVE_DATA_BULLET_FACTORY2D_HPP

#include "save_data_block_bullets2d.hpp"
#include "save_data_normal_bullets2d.hpp"

#include <godot_cpp/classes/resource.hpp>

namespace BlastBullets {

class SaveDataBulletFactory2D : public godot::Resource {
    GDCLASS(SaveDataBulletFactory2D, godot::Resource)

public:
    godot::TypedArray<SaveDataBlockBullets2D> all_block_bullets;
    godot::TypedArray<SaveDataNormalBullets2D> all_normal_bullets;

    void set_all_block_bullets(const godot::TypedArray<SaveDataBlockBullets2D> &new_all_block_bullets);
    godot::TypedArray<SaveDataBlockBullets2D> get_all_block_bullets() const;

    void set_all_normal_bullets(const godot::TypedArray<SaveDataNormalBullets2D> &new_all_normal_bullets);
    godot::TypedArray<SaveDataNormalBullets2D> get_all_normal_bullets() const;

protected:
    static void _bind_methods();
};

}
#endif