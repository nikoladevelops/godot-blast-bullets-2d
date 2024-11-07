#ifndef SAVE_DATA_NORMAL_BULLETS_HPP
#define SAVE_DATA_NORMAL_BULLETS_HPP

#include "./save_data_multi_mesh_bullets2d.hpp"

namespace BlastBullets{
    class SaveDataNormalBullets2D : public SaveDataMultiMeshBullets2D{
        GDCLASS(SaveDataNormalBullets2D, SaveDataMultiMeshBullets2D)
        // Just in case if in the future normal bullets needs additional properties
    public:
        static void _bind_methods();
    };
}
#endif