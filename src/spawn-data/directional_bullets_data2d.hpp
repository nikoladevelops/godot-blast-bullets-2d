#pragma once

#include "../shared/bullet_speed_data2d.hpp"
#include "./multimesh_bullets_data2d.hpp"


namespace BlastBullets2D {
using namespace godot;

class DirectionalBulletsData2D : public MultiMeshBulletsData2D {
	GDCLASS(DirectionalBulletsData2D, MultiMeshBulletsData2D)

public:
	// You are required to pass AT LEAST 1 BulletSpeedData2D in order for the bullets to work. If you want each bullet to have different data (different speed/max speed/ acceleration for each bullet), you would provide the same amount of BulletSpeedData2D as the .size() of the transforms. If you provide less than .size(), the bullets will use only the first BulletSpeedData2D. Note that BulletSpeedData2D has a helper static method that you can use to generate random speed data - BulletSpeedData2D.generate_random_data()
	TypedArray<BulletSpeedData2D> all_bullet_speed_data;

	// Whether each bullet's direction should be adjusted based on the rotation data provided by the user (bullet rotates, so it now moves in that direction)
	bool adjust_direction_based_on_rotation = false;

	static void _bind_methods();

	TypedArray<BulletSpeedData2D> get_all_bullet_speed_data() const;
	void set_all_bullet_speed_data(const TypedArray<BulletSpeedData2D> &new_data);

	bool get_adjust_direction_based_on_rotation() const;
	void set_adjust_direction_based_on_rotation(bool new_adjust_direction_based_on_rotation);
};
} //namespace BlastBullets2D