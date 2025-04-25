#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace BlastBullets2D {
using namespace godot;

class BulletSpeedData2D : public Resource {
    GDCLASS(BulletSpeedData2D, Resource)

public:
    float speed = 0;
    float max_speed = 0;
    float acceleration = 0;

    // Generates an amount of BulletSpeedData2D classes that have random data that varies between ranges. (for example speed of each will be a random number between speed_min and speed_max (inclusive))
    static TypedArray<BulletSpeedData2D> generate_random_data(
        int amount_to_generate,
        float speed_MIN,
        float speed_MAX,
        float max_speed_MIN,
        float max_speed_MAX,
        float acceleration_MIN,
        float acceleration_MAX);

    float get_speed();
    void set_speed(float new_speed);

    float get_max_speed();
    void set_max_speed(float new_max_speed);

    float get_acceleration();
    void set_acceleration(float new_acceleration);

protected:
    static void _bind_methods();
};
}
