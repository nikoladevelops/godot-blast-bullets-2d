#pragma once

#include "./save_data_multimesh_bullets2d.hpp"

namespace BlastBullets2D{
using namespace godot;

    class SaveDataDirectionalBullets2D : public SaveDataMultiMeshBullets2D{
        GDCLASS(SaveDataDirectionalBullets2D, SaveDataMultiMeshBullets2D)
        // Just in case if it needs additional properties in the future
    public:
        static void _bind_methods();
    };
}