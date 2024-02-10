#include "./bullet_rotation_data.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/classes/random_number_generator.hpp"

TypedArray<BulletRotationData> BulletRotationData::generate_random_data(
    int amount_to_generate,
    float rotation_speed_min,
    float rotation_speed_max,
    float max_rotation_speed_min,
    float max_rotation_speed_max,
    float rotation_acceleration_min,
    float rotation_acceleration_max
){
    Ref<RandomNumberGenerator> rand_gen = memnew(RandomNumberGenerator);
    
    TypedArray<BulletRotationData> data;
    data.resize(amount_to_generate);
    
    for (int i = 0; i < amount_to_generate; i++)
    {
        Ref<BulletRotationData> bullet_data = memnew(BulletRotationData);
        bullet_data->rotation_speed = rand_gen->randf_range(rotation_speed_min, rotation_speed_max);
        bullet_data->max_rotation_speed = rand_gen->randf_range(max_rotation_speed_min, max_rotation_speed_max);
        bullet_data->rotation_acceleration = rand_gen->randf_range(rotation_acceleration_min, rotation_acceleration_max);
        bullet_data->is_rotation_enabled=true;
        
        data[i] = bullet_data;
    }
    
    return data;
};

bool BulletRotationData::get_is_rotation_enabled(){
    return is_rotation_enabled;
}
void BulletRotationData::set_is_rotation_enabled(bool new_is_rotation_enabled){
    is_rotation_enabled=new_is_rotation_enabled;
}

float BulletRotationData::get_rotation_speed(){
    return rotation_speed;

}
void BulletRotationData::set_rotation_speed(float new_rotation_speed){
    rotation_speed=new_rotation_speed;
}

float BulletRotationData::get_max_rotation_speed(){
    return max_rotation_speed;
}
void BulletRotationData::set_max_rotation_speed(float new_max_rotation_speed){
    max_rotation_speed=new_max_rotation_speed;
}

float BulletRotationData::get_rotation_acceleration(){
    return rotation_acceleration;
}
void BulletRotationData::set_rotation_acceleration(float new_rotation_acceleration){
    rotation_acceleration=new_rotation_acceleration;
}

void BulletRotationData::_bind_methods(){
    ClassDB::bind_method(D_METHOD("set_is_rotation_enabled"), &BulletRotationData::set_is_rotation_enabled);
    ClassDB::bind_method(D_METHOD("get_is_rotation_enabled"), &BulletRotationData::get_is_rotation_enabled);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_rotation_enabled"), "set_is_rotation_enabled", "get_is_rotation_enabled");

    ClassDB::bind_method(D_METHOD("set_rotation_speed"), &BulletRotationData::set_rotation_speed);
    ClassDB::bind_method(D_METHOD("get_rotation_speed"), &BulletRotationData::get_rotation_speed);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_speed"), "set_rotation_speed", "get_rotation_speed");

    ClassDB::bind_method(D_METHOD("set_max_rotation_speed"), &BulletRotationData::set_max_rotation_speed);
    ClassDB::bind_method(D_METHOD("get_max_rotation_speed"), &BulletRotationData::get_max_rotation_speed);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_rotation_speed"), "set_max_rotation_speed", "get_max_rotation_speed");

    ClassDB::bind_method(D_METHOD("set_rotation_acceleration"), &BulletRotationData::set_rotation_acceleration);
    ClassDB::bind_method(D_METHOD("get_rotation_acceleration"), &BulletRotationData::get_rotation_acceleration);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_acceleration"), "set_rotation_acceleration", "get_rotation_acceleration");
    
    ClassDB::bind_static_method("BulletRotationData",
    D_METHOD("generate_random_data",
    "amount_to_generate",
    "rotation_speed_min",
    "rotation_speed_max",
    "max_rotation_speed_min",
    "max_rotation_speed_max",
    "rotation_acceleration_min",
    "rotation_acceleration_max"
    ), &BulletRotationData::generate_random_data);
}