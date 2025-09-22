#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace BlastBullets2D {
using namespace godot;

class BulletRotationData2D : public Resource {
	GDCLASS(BulletRotationData2D, Resource)

public:
	real_t rotation_speed = 0;
	real_t max_rotation_speed = 0;
	real_t rotation_acceleration = 0;

	// Generates an amount of BulletRotationInfo classes that have random data that varies between ranges. (for example rotation_speed of each will be a random number between rotation_speed_min and rotation_speed_max (inclusive))
	static TypedArray<BulletRotationData2D> generate_random_data(
			int amount_to_generate,
			real_t rotation_speed_MIN,
			real_t rotation_speed_MAX,
			real_t max_rotation_speed_MIN,
			real_t max_rotation_speed_MAX,
			real_t rotation_acceleration_MIN,
			real_t rotation_acceleration_MAX);

	real_t get_rotation_speed();
	void set_rotation_speed(real_t new_rotation_speed);

	real_t get_max_rotation_speed();
	void set_max_rotation_speed(real_t new_max_rotation_speed);

	real_t get_rotation_acceleration();
	void set_rotation_acceleration(real_t new_rotation_acceleration);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D