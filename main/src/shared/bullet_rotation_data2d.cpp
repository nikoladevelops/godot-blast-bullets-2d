#include "./bullet_rotation_data2d.hpp"

#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;

namespace BlastBullets2D {

TypedArray<BulletRotationData2D> BulletRotationData2D::generate_random_data(
    int amount_to_generate,
    float rotation_speed_MIN,
    float rotation_speed_MAX,
    float max_rotation_speed_MIN,
    float max_rotation_speed_MAX,
    float rotation_acceleration_MIN,
    float rotation_acceleration_MAX) {
    Ref<RandomNumberGenerator> rand_gen = memnew(RandomNumberGenerator);

    TypedArray<BulletRotationData2D> data;
    data.resize(amount_to_generate);

    for (int i = 0; i < amount_to_generate; i++) {
        Ref<BulletRotationData2D> bullet_data = memnew(BulletRotationData2D);
        bullet_data->rotation_speed = rand_gen->randf_range(rotation_speed_MIN, rotation_speed_MAX);
        bullet_data->max_rotation_speed = rand_gen->randf_range(max_rotation_speed_MIN, max_rotation_speed_MAX);
        bullet_data->rotation_acceleration = rand_gen->randf_range(rotation_acceleration_MIN, rotation_acceleration_MAX);

        data[i] = bullet_data;
    }

    return data;
};

float BulletRotationData2D::get_rotation_speed() {
    return rotation_speed;
}
void BulletRotationData2D::set_rotation_speed(float new_rotation_speed) {
    rotation_speed = new_rotation_speed;
}

float BulletRotationData2D::get_max_rotation_speed() {
    return max_rotation_speed;
}
void BulletRotationData2D::set_max_rotation_speed(float new_max_rotation_speed) {
    max_rotation_speed = new_max_rotation_speed;
}

float BulletRotationData2D::get_rotation_acceleration() {
    return rotation_acceleration;
}
void BulletRotationData2D::set_rotation_acceleration(float new_rotation_acceleration) {
    rotation_acceleration = new_rotation_acceleration;
}

void BulletRotationData2D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_rotation_speed"), &BulletRotationData2D::set_rotation_speed);
    ClassDB::bind_method(D_METHOD("get_rotation_speed"), &BulletRotationData2D::get_rotation_speed);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_speed"), "set_rotation_speed", "get_rotation_speed");

    ClassDB::bind_method(D_METHOD("set_max_rotation_speed"), &BulletRotationData2D::set_max_rotation_speed);
    ClassDB::bind_method(D_METHOD("get_max_rotation_speed"), &BulletRotationData2D::get_max_rotation_speed);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_rotation_speed"), "set_max_rotation_speed", "get_max_rotation_speed");

    ClassDB::bind_method(D_METHOD("set_rotation_acceleration"), &BulletRotationData2D::set_rotation_acceleration);
    ClassDB::bind_method(D_METHOD("get_rotation_acceleration"), &BulletRotationData2D::get_rotation_acceleration);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_acceleration"), "set_rotation_acceleration", "get_rotation_acceleration");

    ClassDB::bind_static_method(
        "BulletRotationData2D",
        D_METHOD(
            "generate_random_data",
            "amount_to_generate",
            "rotation_speed_MIN",
            "rotation_speed_MAX",
            "max_rotation_speed_MIN",
            "max_rotation_speed_MAX",
            "rotation_acceleration_MIN",
            "rotation_acceleration_MAX"),
        &BulletRotationData2D::generate_random_data);
}
}