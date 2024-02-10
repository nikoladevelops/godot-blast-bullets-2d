#ifndef BULLET_SPEED_DATA
#define BULLET_SPEED_DATA

#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/variant/typed_array.hpp"

using namespace godot;
class BulletSpeedData : public Resource{
    GDCLASS(BulletSpeedData, Resource);

    public:
        float speed;
        float max_speed;
        float acceleration;

        // Generates an amount of BulletSpeedData classes that have random data that varies between ranges. (for example speed of each will be a random number between speed_min and speed_max (inclusive))
        static TypedArray<BulletSpeedData> generate_random_data(
            int amount_to_generate,
            float speed_min,
            float speed_max,
            float max_speed_min,
            float max_speed_max,
            float acceleration_min,
            float acceleration_max
            );

        float get_speed();
        void set_speed(float new_speed);

        float get_max_speed();
        void set_max_speed(float new_max_speed);

        float get_acceleration();
        void set_acceleration(float new_acceleration);

    protected:
        static void _bind_methods();

};

#endif
