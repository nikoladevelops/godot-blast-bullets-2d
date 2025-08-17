#pragma once

#include "./save_data_multimesh_bullets2d.hpp"

namespace BlastBullets2D {
using namespace godot;

class SaveDataBlockBullets2D : public SaveDataMultiMeshBullets2D {
    GDCLASS(SaveDataBlockBullets2D, SaveDataMultiMeshBullets2D)

public:
    float block_rotation_radians = 0.0f;

    void set_block_rotation_radians(float new_block_rotation_radians);
    float get_block_rotation_radians() const;

protected:
    static void _bind_methods();
};
}