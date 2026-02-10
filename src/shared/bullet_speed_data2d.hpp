#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace BlastBullets2D {
using namespace godot;

class BulletSpeedData2D : public Resource {
	GDCLASS(BulletSpeedData2D, Resource)

public:
	real_t speed = 0;
	real_t max_speed = 0;
	real_t acceleration = 0;

	// Generates an amount of BulletSpeedData2D classes that have random data that varies between ranges. (for example speed of each will be a random number between speed_min and speed_max (inclusive))
	static TypedArray<BulletSpeedData2D> generate_random_data(
			int amount_to_generate,
			real_t speed_MIN,
			real_t speed_MAX,
			real_t max_speed_MIN,
			real_t max_speed_MAX,
			real_t acceleration_MIN,
			real_t acceleration_MAX);

	real_t get_speed();
	void set_speed(real_t new_speed);

	real_t get_max_speed();
	void set_max_speed(real_t new_max_speed);

	real_t get_acceleration();
	void set_acceleration(real_t new_acceleration);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D
