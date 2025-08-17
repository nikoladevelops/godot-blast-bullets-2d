#pragma once

#include "./save_data_multimesh_bullets2d.hpp"

namespace BlastBullets2D{
using namespace godot;

    class SaveDataDirectionalBullets2D : public SaveDataMultiMeshBullets2D{
        GDCLASS(SaveDataDirectionalBullets2D, SaveDataMultiMeshBullets2D)
        // Just in case if it needs additional properties in the future
    public:

        bool adjust_direction_based_on_rotation = false;


        bool get_adjust_direction_based_on_rotation() const;
        void set_adjust_direction_based_on_rotation(bool new_adjust_direction_based_on_rotation);

        static void _bind_methods();
    };
}