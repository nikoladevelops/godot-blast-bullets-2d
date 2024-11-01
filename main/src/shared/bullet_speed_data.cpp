#include "./bullet_speed_data.hpp"

#include <godot_cpp/classes/random_number_generator.hpp>

using namespace godot;
namespace BlastBullets {

TypedArray<BulletSpeedData> BulletSpeedData::generate_random_data(
    int amount_to_generate,
    float speed_min,
    float speed_max,
    float max_speed_min,
    float max_speed_max,
    float acceleration_min,
    float acceleration_max) {
    Ref<RandomNumberGenerator> rand_gen = memnew(RandomNumberGenerator);

    TypedArray<BulletSpeedData> data;
    data.resize(amount_to_generate);

    for (int i = 0; i < amount_to_generate; i++) {
        Ref<BulletSpeedData> bullet_data = memnew(BulletSpeedData);
        bullet_data->speed = rand_gen->randf_range(speed_min, speed_max);
        bullet_data->max_speed = rand_gen->randf_range(max_speed_min, max_speed_max);
        bullet_data->acceleration = rand_gen->randf_range(acceleration_min, acceleration_max);

        data[i] = bullet_data;
    }

    return data;
}

float BulletSpeedData::get_speed() {
    return speed;
}
void BulletSpeedData::set_speed(float new_speed) {
    speed = new_speed;
}

float BulletSpeedData::get_max_speed() {
    return max_speed;
}
void BulletSpeedData::set_max_speed(float new_max_speed) {
    max_speed = new_max_speed;
}

float BulletSpeedData::get_acceleration() {
    return acceleration;
}
void BulletSpeedData::set_acceleration(float new_acceleration) {
    acceleration = new_acceleration;
}

void BulletSpeedData::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_speed", "new_speed"), &BulletSpeedData::set_speed);
    ClassDB::bind_method(D_METHOD("get_speed"), &BulletSpeedData::get_speed);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed"), "set_speed", "get_speed");

    ClassDB::bind_method(D_METHOD("set_max_speed", "new_max_speed"), &BulletSpeedData::set_max_speed);
    ClassDB::bind_method(D_METHOD("get_max_speed"), &BulletSpeedData::get_max_speed);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_speed"), "set_max_speed", "get_max_speed");

    ClassDB::bind_method(D_METHOD("set_acceleration", "new_acceleration"), &BulletSpeedData::set_acceleration);
    ClassDB::bind_method(D_METHOD("get_acceleration"), &BulletSpeedData::get_acceleration);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "acceleration"), "set_acceleration", "get_acceleration");

    ClassDB::bind_static_method(
        "BulletSpeedData",
        D_METHOD(
            "generate_random_data",
            "amount_to_generate",
            "speed_min",
            "speed_max",
            "max_speed_min",
            "max_speed_max",
            "acceleration_min",
            "acceleration_max"),
        &BulletSpeedData::generate_random_data);
}
} // namespace BulletSpeedData
