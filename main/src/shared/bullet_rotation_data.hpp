#ifndef BULLET_ROTATION_DATA
#define BULLET_ROTATION_DATA

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace BlastBullets {
    
class BulletRotationData : public godot::Resource {
    GDCLASS(BulletRotationData, godot::Resource);

public:
    float rotation_speed;
    float max_rotation_speed;
    float rotation_acceleration;

    // Generates an amount of BulletRotationInfo classes that have random data that varies between ranges. (for example rotation_speed of each will be a random number between rotation_speed_min and rotation_speed_max (inclusive))
    static godot::TypedArray<BulletRotationData> generate_random_data(
        int amount_to_generate,
        float rotation_speed_min,
        float rotation_speed_max,
        float max_rotation_speed_min,
        float max_rotation_speed_max,
        float rotation_acceleration_min,
        float rotation_acceleration_max);

    float get_rotation_speed();
    void set_rotation_speed(float new_rotation_speed);

    float get_max_rotation_speed();
    void set_max_rotation_speed(float new_max_rotation_speed);

    float get_rotation_acceleration();
    void set_rotation_acceleration(float new_rotation_acceleration);

protected:
    static void _bind_methods();
};
} // namespace BlastBullets
#endif