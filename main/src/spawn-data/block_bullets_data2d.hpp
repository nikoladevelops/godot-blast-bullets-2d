#ifndef BLOCK_BULLETS_DATA2D_HPP
#define BLOCK_BULLETS_DATA2D_HPP

#include "./multi_mesh_bullets_data2d.hpp"
#include "../shared/bullet_rotation_data.hpp"
#include "../shared/bullet_speed_data.hpp"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/typed_array.hpp>


namespace BlastBullets {

class BlockBulletsData2D : public MultiMeshBulletsData2D {
    GDCLASS(BlockBulletsData2D, MultiMeshBulletsData2D)

public:
    // Used only when use_block_rotation_radians is set to true. Provides the rotation in which all bullets move as a block with the BulletSpeedData. Note that this uses radians, to convert degrees to radians you would do degrees*PI/180.
    float block_rotation_radians = 0.0f;

    // The speed at which the block of bullets is moving
    godot::Ref<BulletSpeedData> block_speed;

    float get_block_rotation_radians() const;
    void set_block_rotation_radians(float new_block_rotation_radians);

    godot::Ref<BulletSpeedData> get_block_speed() const;
    void set_block_speed(const godot::Ref<BulletSpeedData> &new_block_speed);

protected:
    static void _bind_methods();
};
}

#endif