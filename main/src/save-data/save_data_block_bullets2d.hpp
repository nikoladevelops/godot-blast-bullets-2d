#ifndef SAVE_DATA_BLOCK_BULLETS2D_HPP
#define SAVE_DATA_BLOCK_BULLETS2D_HPP

#include "./save_data_multi_mesh_bullets2d.hpp"

namespace BlastBullets {

class SaveDataBlockBullets2D : public SaveDataMultiMeshBullets2D {
    GDCLASS(SaveDataBlockBullets2D, SaveDataMultiMeshBullets2D)

public:
    float block_rotation_radians;
    godot::Vector2 multi_mesh_position;

    void set_block_rotation_radians(float new_block_rotation_radians);
    float get_block_rotation_radians() const;

    godot::Vector2 get_multi_mesh_position() const;
    void set_multi_mesh_position(godot::Vector2 new_position);

protected:
    static void _bind_methods();
};
}
#endif