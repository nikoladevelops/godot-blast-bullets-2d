#pragma once

#include "save_data_block_bullets2d.hpp"
#include "save_data_directional_bullets2d.hpp"

#include <godot_cpp/classes/resource.hpp>

namespace BlastBullets2D {
using namespace godot;

class SaveDataBulletFactory2D : public Resource {
    GDCLASS(SaveDataBulletFactory2D, Resource)

public:
    TypedArray<SaveDataBlockBullets2D> all_block_bullets = TypedArray<SaveDataBlockBullets2D>();
    TypedArray<SaveDataDirectionalBullets2D> all_directional_bullets = TypedArray<SaveDataDirectionalBullets2D>();

    void set_all_block_bullets(const TypedArray<SaveDataBlockBullets2D> &new_all_block_bullets);
    TypedArray<SaveDataBlockBullets2D> get_all_block_bullets() const;

    void set_all_directional_bullets(const TypedArray<SaveDataDirectionalBullets2D> &new_all_directional_bullets);
    TypedArray<SaveDataDirectionalBullets2D> get_all_directional_bullets() const;

protected:
    static void _bind_methods();
};

}