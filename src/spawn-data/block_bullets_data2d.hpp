#pragma once

#include "../shared/bullet_speed_data2d.hpp"
#include "./multimesh_bullets_data2d.hpp"


#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace BlastBullets2D {
using namespace godot;

class BlockBulletsData2D : public MultiMeshBulletsData2D {
	GDCLASS(BlockBulletsData2D, MultiMeshBulletsData2D)

public:
	// Used only when use_block_rotation_radians is set to true. Provides the rotation in which all bullets move as a block with the BulletSpeedData2D. Note that this uses radians, to convert degrees to radians you would do degrees*PI/180.
	real_t block_rotation_radians = 0.0f;

	// The speed at which the block of bullets is moving
	Ref<BulletSpeedData2D> block_speed;

	real_t get_block_rotation_radians() const;
	void set_block_rotation_radians(real_t new_block_rotation_radians);

	Ref<BulletSpeedData2D> get_block_speed() const;
	void set_block_speed(const Ref<BulletSpeedData2D> &new_block_speed);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D
