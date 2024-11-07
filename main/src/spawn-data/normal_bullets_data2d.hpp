#ifndef NORMAL_BULLETS_DATA2D_HPP
#define NORMAL_BULLETS_DATA2D_HPP

#include "./multi_mesh_bullets_data2d.hpp"
#include "../shared/bullet_speed_data.hpp"

namespace BlastBullets{
    
    class NormalBulletsData2D : public MultiMeshBulletsData2D{
        GDCLASS(NormalBulletsData2D, MultiMeshBulletsData2D)
        
        public:
            // You are required to pass AT LEAST 1 BulletSpeedData in order for the bullets to work. If you want each bullet to have different data (different speed/max speed/ acceleration for each bullet), you would provide the same amount of BulletSpeedData as the .size() of the transforms. If you provide less than .size(), the bullets will use only the first BulletSpeedData. Note that BulletSpeedData has a helper static method that you can use to generate random speed data - BulletSpeedData.generate_random_data()
            godot::TypedArray<BulletSpeedData> all_bullet_speed_data;

            static void _bind_methods();

            godot::TypedArray<BulletSpeedData> get_all_bullet_speed_data();
            void set_all_bullet_speed_data(const godot::TypedArray<BulletSpeedData> &new_data);
    };
}
#endif