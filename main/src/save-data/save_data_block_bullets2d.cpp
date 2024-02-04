#include "save_data_block_bullets2d.hpp"

using namespace godot;
void SaveDataBlockBullets2D::set_transforms(TypedArray<Transform2D> new_transforms) {
    transforms = new_transforms;
}

TypedArray<Transform2D> SaveDataBlockBullets2D::get_transforms() const {
    return transforms;
}

void SaveDataBlockBullets2D::set_velocity(Vector2 new_velocity) {
    velocity = new_velocity;
}

Vector2 SaveDataBlockBullets2D::get_velocity() const {
    return velocity;
}

void SaveDataBlockBullets2D::set_current_position(Vector2 new_current_position) {
    current_position = new_current_position;
}

Vector2 SaveDataBlockBullets2D::get_current_position() const {
    return current_position;
}

void SaveDataBlockBullets2D::set_max_life_time(float new_max_life_time) {
    max_life_time = new_max_life_time;
}

float SaveDataBlockBullets2D::get_max_life_time() const {
    return max_life_time;
}

void SaveDataBlockBullets2D::set_current_life_time(float new_current_life_time) {
    current_life_time = new_current_life_time;
}

float SaveDataBlockBullets2D::get_current_life_time() const {
    return current_life_time;
}

void SaveDataBlockBullets2D::set_size(int new_size) {
    size = new_size;
}

int SaveDataBlockBullets2D::get_size() const {
    return size;
}

void SaveDataBlockBullets2D::set_bullets_enabled_status(TypedArray<bool> new_bullets_enabled_status) {
    bullets_enabled_status = new_bullets_enabled_status;
}

TypedArray<bool> SaveDataBlockBullets2D::get_bullets_enabled_status() const {
    return bullets_enabled_status;
}


void SaveDataBlockBullets2D::set_bullets_custom_data(Ref<Resource> new_bullets_custom_data) {
    bullets_custom_data = new_bullets_custom_data;
}

Ref<Resource> SaveDataBlockBullets2D::get_bullets_custom_data() const {
    return bullets_custom_data;
}

void SaveDataBlockBullets2D::set_textures(TypedArray<Texture2D> new_textures) {
    for (int i = 0; i < new_textures.size(); i++)
    {
        textures.push_back(new_textures[i]);
    }
}

TypedArray<Texture2D> SaveDataBlockBullets2D::get_textures() const {
    return textures;
}

void SaveDataBlockBullets2D::set_texture_rotation_radians(float new_texture_rotation_radians) {
    texture_rotation_radians = new_texture_rotation_radians;
}

float SaveDataBlockBullets2D::get_texture_rotation_radians() const {
    return texture_rotation_radians;
}

void SaveDataBlockBullets2D::set_texture_size(Vector2 new_texture_size) {
    texture_size = new_texture_size;
}

Vector2 SaveDataBlockBullets2D::get_texture_size() const {
    return texture_size;
}

void SaveDataBlockBullets2D::set_max_change_texture_time(float new_max_change_texture_time) {
    max_change_texture_time = new_max_change_texture_time;
}

float SaveDataBlockBullets2D::get_max_change_texture_time() const {
    return max_change_texture_time;
}

void SaveDataBlockBullets2D::set_current_change_texture_time(float new_current_change_texture_time) {
    current_change_texture_time = new_current_change_texture_time;
}

float SaveDataBlockBullets2D::get_current_change_texture_time() const {
    return current_change_texture_time;
}

void SaveDataBlockBullets2D::set_current_texture_index(int new_current_texture_index) {
    current_texture_index = new_current_texture_index;
}

int SaveDataBlockBullets2D::get_current_texture_index() const {
    return current_texture_index;
}

void SaveDataBlockBullets2D::set_block_rotation_radians(float new_block_rotation_radians) {
    block_rotation_radians = new_block_rotation_radians;
}

float SaveDataBlockBullets2D::get_block_rotation_radians() const {
    return block_rotation_radians;
}

void SaveDataBlockBullets2D::set_max_speed(float new_max_speed) {
    max_speed = new_max_speed;
}

float SaveDataBlockBullets2D::get_max_speed() const {
    return max_speed;
}

void SaveDataBlockBullets2D::set_speed(float new_speed) {
    speed = new_speed;
}

float SaveDataBlockBullets2D::get_speed() const {
    return speed;
}

void SaveDataBlockBullets2D::set_acceleration(float new_acceleration) {
    acceleration = new_acceleration;
}

float SaveDataBlockBullets2D::get_acceleration() const {
    return acceleration;
}

void SaveDataBlockBullets2D::set_max_acceleration_time(float new_max_acceleration_time) {
    max_acceleration_time = new_max_acceleration_time;
}

float SaveDataBlockBullets2D::get_max_acceleration_time() const {
    return max_acceleration_time;
}

void SaveDataBlockBullets2D::set_current_acceleration_time(float new_current_acceleration_time) {
    current_acceleration_time = new_current_acceleration_time;
}

float SaveDataBlockBullets2D::get_current_acceleration_time() const {
    return current_acceleration_time;
}

void SaveDataBlockBullets2D::set_collision_layer(int new_collision_layer) {
    collision_layer = new_collision_layer;
}

int SaveDataBlockBullets2D::get_collision_layer() const {
    return collision_layer;
}

void SaveDataBlockBullets2D::set_collision_mask(int new_collision_mask) {
    collision_mask = new_collision_mask;
}

int SaveDataBlockBullets2D::get_collision_mask() const {
    return collision_mask;
}

void SaveDataBlockBullets2D::set_collision_shape_size(Vector2 new_collision_shape_size) {
    collision_shape_size = new_collision_shape_size;
}

Vector2 SaveDataBlockBullets2D::get_collision_shape_size() const {
    return collision_shape_size;
}

void SaveDataBlockBullets2D::set_collision_shape_offset(Vector2 new_collision_shape_offset) {
    collision_shape_offset = new_collision_shape_offset;
}

Vector2 SaveDataBlockBullets2D::get_collision_shape_offset() const {
    return collision_shape_offset;
}

void SaveDataBlockBullets2D::set_monitorable(bool new_monitorable) {
    monitorable = new_monitorable;
}

bool SaveDataBlockBullets2D::get_monitorable() const {
    return monitorable;
}

void SaveDataBlockBullets2D::set_material(Ref<Material> new_material) {
    material = new_material;
}

Ref<Material> SaveDataBlockBullets2D::get_material() const {
    return material;
}

void SaveDataBlockBullets2D::set_mesh(Ref<Mesh> new_mesh) {
    mesh = new_mesh;
}

Ref<Mesh> SaveDataBlockBullets2D::get_mesh() const {
    return mesh;
}

TypedArray<BulletRotationData> SaveDataBlockBullets2D::get_all_bullet_rotation_data(){
    return all_bullet_rotation_data;
}
void SaveDataBlockBullets2D::set_all_bullet_rotation_data(const TypedArray<BulletRotationData>& new_bullet_rotation_data){
    
    all_bullet_rotation_data.resize(new_bullet_rotation_data.size());
    for (int i = 0; i < new_bullet_rotation_data.size(); i++)
    {
        all_bullet_rotation_data[i] = new_bullet_rotation_data[i];
    }
}

bool SaveDataBlockBullets2D::get_rotate_only_textures(){
    return rotate_only_textures;
}
void SaveDataBlockBullets2D::set_rotate_only_textures(bool new_rotate_only_textures){
    rotate_only_textures=new_rotate_only_textures;
}



void SaveDataBlockBullets2D::_bind_methods(){
    ClassDB::bind_method(D_METHOD("set_transforms"), &SaveDataBlockBullets2D::set_transforms);
    ClassDB::bind_method(D_METHOD("get_transforms"), &SaveDataBlockBullets2D::get_transforms);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "transforms"), "set_transforms", "get_transforms");

    ClassDB::bind_method(D_METHOD("set_velocity"), &SaveDataBlockBullets2D::set_velocity);
    ClassDB::bind_method(D_METHOD("get_velocity"), &SaveDataBlockBullets2D::get_velocity);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "velocity"), "set_velocity", "get_velocity");

    ClassDB::bind_method(D_METHOD("set_current_position"), &SaveDataBlockBullets2D::set_current_position);
    ClassDB::bind_method(D_METHOD("get_current_position"), &SaveDataBlockBullets2D::get_current_position);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "current_position"), "set_current_position", "get_current_position");

    ClassDB::bind_method(D_METHOD("set_max_life_time"), &SaveDataBlockBullets2D::set_max_life_time);
    ClassDB::bind_method(D_METHOD("get_max_life_time"), &SaveDataBlockBullets2D::get_max_life_time);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_life_time"), "set_max_life_time", "get_max_life_time");

    ClassDB::bind_method(D_METHOD("set_current_life_time"), &SaveDataBlockBullets2D::set_current_life_time);
    ClassDB::bind_method(D_METHOD("get_current_life_time"), &SaveDataBlockBullets2D::get_current_life_time);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "current_life_time"), "set_current_life_time", "get_current_life_time");

    ClassDB::bind_method(D_METHOD("set_size"), &SaveDataBlockBullets2D::set_size);
    ClassDB::bind_method(D_METHOD("get_size"), &SaveDataBlockBullets2D::get_size);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "size"), "set_size", "get_size");

    ClassDB::bind_method(D_METHOD("set_bullets_enabled_status"), &SaveDataBlockBullets2D::set_bullets_enabled_status);
    ClassDB::bind_method(D_METHOD("get_bullets_enabled_status"), &SaveDataBlockBullets2D::get_bullets_enabled_status);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bullets_enabled_status"), "set_bullets_enabled_status", "get_bullets_enabled_status");

    ClassDB::bind_method(D_METHOD("set_bullets_custom_data"), &SaveDataBlockBullets2D::set_bullets_custom_data);
    ClassDB::bind_method(D_METHOD("get_bullets_custom_data"), &SaveDataBlockBullets2D::get_bullets_custom_data);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bullets_custom_data", PROPERTY_HINT_RESOURCE_TYPE, "Resource"), "set_bullets_custom_data", "get_bullets_custom_data");

    ClassDB::bind_method(D_METHOD("set_textures"), &SaveDataBlockBullets2D::set_textures);
    ClassDB::bind_method(D_METHOD("get_textures"), &SaveDataBlockBullets2D::get_textures);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "textures", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_textures", "get_textures");

    ClassDB::bind_method(D_METHOD("set_texture_rotation_radians"), &SaveDataBlockBullets2D::set_texture_rotation_radians);
    ClassDB::bind_method(D_METHOD("get_texture_rotation_radians"), &SaveDataBlockBullets2D::get_texture_rotation_radians);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "texture_rotation_radians"), "set_texture_rotation_radians", "get_texture_rotation_radians");

    ClassDB::bind_method(D_METHOD("set_texture_size"), &SaveDataBlockBullets2D::set_texture_size);
    ClassDB::bind_method(D_METHOD("get_texture_size"), &SaveDataBlockBullets2D::get_texture_size);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "texture_size"), "set_texture_size", "get_texture_size");

    ClassDB::bind_method(D_METHOD("set_max_change_texture_time"), &SaveDataBlockBullets2D::set_max_change_texture_time);
    ClassDB::bind_method(D_METHOD("get_max_change_texture_time"), &SaveDataBlockBullets2D::get_max_change_texture_time);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_change_texture_time"), "set_max_change_texture_time", "get_max_change_texture_time");

    ClassDB::bind_method(D_METHOD("set_current_change_texture_time"), &SaveDataBlockBullets2D::set_current_change_texture_time);
    ClassDB::bind_method(D_METHOD("get_current_change_texture_time"), &SaveDataBlockBullets2D::get_current_change_texture_time);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "current_change_texture_time"), "set_current_change_texture_time", "get_current_change_texture_time");

    ClassDB::bind_method(D_METHOD("set_current_texture_index"), &SaveDataBlockBullets2D::set_current_texture_index);
    ClassDB::bind_method(D_METHOD("get_current_texture_index"), &SaveDataBlockBullets2D::get_current_texture_index);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "current_texture_index"), "set_current_texture_index", "get_current_texture_index");

    ClassDB::bind_method(D_METHOD("set_block_rotation_radians"), &SaveDataBlockBullets2D::set_block_rotation_radians);
    ClassDB::bind_method(D_METHOD("get_block_rotation_radians"), &SaveDataBlockBullets2D::get_block_rotation_radians);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "block_rotation_radians"), "set_block_rotation_radians", "get_block_rotation_radians");

    ClassDB::bind_method(D_METHOD("set_max_speed"), &SaveDataBlockBullets2D::set_max_speed);
    ClassDB::bind_method(D_METHOD("get_max_speed"), &SaveDataBlockBullets2D::get_max_speed);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_speed"), "set_max_speed", "get_max_speed");

    ClassDB::bind_method(D_METHOD("set_speed"), &SaveDataBlockBullets2D::set_speed);
    ClassDB::bind_method(D_METHOD("get_speed"), &SaveDataBlockBullets2D::get_speed);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed"), "set_speed", "get_speed");

    ClassDB::bind_method(D_METHOD("set_acceleration"), &SaveDataBlockBullets2D::set_acceleration);
    ClassDB::bind_method(D_METHOD("get_acceleration"), &SaveDataBlockBullets2D::get_acceleration);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "acceleration"), "set_acceleration", "get_acceleration");

    ClassDB::bind_method(D_METHOD("set_max_acceleration_time"), &SaveDataBlockBullets2D::set_max_acceleration_time);
    ClassDB::bind_method(D_METHOD("get_max_acceleration_time"), &SaveDataBlockBullets2D::get_max_acceleration_time);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_acceleration_time"), "set_max_acceleration_time", "get_max_acceleration_time");

    ClassDB::bind_method(D_METHOD("set_current_acceleration_time"), &SaveDataBlockBullets2D::set_current_acceleration_time);
    ClassDB::bind_method(D_METHOD("get_current_acceleration_time"), &SaveDataBlockBullets2D::get_current_acceleration_time);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "current_acceleration_time"), "set_current_acceleration_time", "get_current_acceleration_time");

    ClassDB::bind_method(D_METHOD("set_collision_layer"), &SaveDataBlockBullets2D::set_collision_layer);
    ClassDB::bind_method(D_METHOD("get_collision_layer"), &SaveDataBlockBullets2D::get_collision_layer);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer"), "set_collision_layer", "get_collision_layer");

    ClassDB::bind_method(D_METHOD("set_collision_mask"), &SaveDataBlockBullets2D::set_collision_mask);
    ClassDB::bind_method(D_METHOD("get_collision_mask"), &SaveDataBlockBullets2D::get_collision_mask);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask"), "set_collision_mask", "get_collision_mask");

    ClassDB::bind_method(D_METHOD("set_collision_shape_size"), &SaveDataBlockBullets2D::set_collision_shape_size);
    ClassDB::bind_method(D_METHOD("get_collision_shape_size"), &SaveDataBlockBullets2D::get_collision_shape_size);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "collision_shape_size"), "set_collision_shape_size", "get_collision_shape_size");

    ClassDB::bind_method(D_METHOD("set_collision_shape_offset"), &SaveDataBlockBullets2D::set_collision_shape_offset);
    ClassDB::bind_method(D_METHOD("get_collision_shape_offset"), &SaveDataBlockBullets2D::get_collision_shape_offset);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "collision_shape_offset"), "set_collision_shape_offset", "get_collision_shape_offset");

    ClassDB::bind_method(D_METHOD("set_monitorable"), &SaveDataBlockBullets2D::set_monitorable);
    ClassDB::bind_method(D_METHOD("get_monitorable"), &SaveDataBlockBullets2D::get_monitorable);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitorable"), "set_monitorable", "get_monitorable");

    ClassDB::bind_method(D_METHOD("set_material"), &SaveDataBlockBullets2D::set_material);
    ClassDB::bind_method(D_METHOD("get_material"), &SaveDataBlockBullets2D::get_material);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_material", "get_material");

    ClassDB::bind_method(D_METHOD("set_mesh"), &SaveDataBlockBullets2D::set_mesh);
    ClassDB::bind_method(D_METHOD("get_mesh"), &SaveDataBlockBullets2D::get_mesh);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_mesh", "get_mesh");

    ClassDB::bind_method(D_METHOD("get_all_bullet_rotation_data"), &SaveDataBlockBullets2D::get_all_bullet_rotation_data);
    ClassDB::bind_method(D_METHOD("set_all_bullet_rotation_data", "new_data"), &SaveDataBlockBullets2D::set_all_bullet_rotation_data);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_rotation_data"), "set_all_bullet_rotation_data", "get_all_bullet_rotation_data");

    ClassDB::bind_method(D_METHOD("get_rotate_only_textures"), &SaveDataBlockBullets2D::get_rotate_only_textures);
    ClassDB::bind_method(D_METHOD("set_rotate_only_textures", "new_rotate_only_textures"), &SaveDataBlockBullets2D::set_rotate_only_textures);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rotate_only_textures"), "set_rotate_only_textures", "get_rotate_only_textures");


}