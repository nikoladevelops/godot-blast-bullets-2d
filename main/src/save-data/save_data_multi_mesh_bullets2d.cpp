#include "./save_data_multi_mesh_bullets2d.hpp"

using namespace godot;

namespace BlastBullets {

void SaveDataMultiMeshBullets2D::set_max_life_time(float new_max_life_time) {
    max_life_time = new_max_life_time;
}

float SaveDataMultiMeshBullets2D::get_max_life_time() const {
    return max_life_time;
}

void SaveDataMultiMeshBullets2D::set_current_life_time(float new_current_life_time) {
    current_life_time = new_current_life_time;
}

float SaveDataMultiMeshBullets2D::get_current_life_time() const {
    return current_life_time;
}

void SaveDataMultiMeshBullets2D::set_size(int new_size) {
    size = new_size;
}

int SaveDataMultiMeshBullets2D::get_size() const {
    return size;
}

void SaveDataMultiMeshBullets2D::set_bullets_enabled_status(TypedArray<bool> new_bullets_enabled_status) {
    int new_enabled_size = new_bullets_enabled_status.size();
    bullets_enabled_status.resize(new_enabled_size);
    for (int i = 0; i < new_enabled_size; i++) {
        bullets_enabled_status[i] = new_bullets_enabled_status[i];
    }
}

TypedArray<bool> SaveDataMultiMeshBullets2D::get_bullets_enabled_status() const {
    return bullets_enabled_status;
}

void SaveDataMultiMeshBullets2D::set_bullets_custom_data(Ref<Resource> new_bullets_custom_data) {
    bullets_custom_data = new_bullets_custom_data;
}

Ref<Resource> SaveDataMultiMeshBullets2D::get_bullets_custom_data() const {
    return bullets_custom_data;
}

void SaveDataMultiMeshBullets2D::set_textures(TypedArray<Texture2D> new_textures) {
    for (int i = 0; i < new_textures.size(); i++) {
        textures.push_back(new_textures[i]);
    }
}

TypedArray<Texture2D> SaveDataMultiMeshBullets2D::get_textures() const {
    return textures;
}

void SaveDataMultiMeshBullets2D::set_texture_size(Vector2 new_texture_size) {
    texture_size = new_texture_size;
}

Vector2 SaveDataMultiMeshBullets2D::get_texture_size() const {
    return texture_size;
}

void SaveDataMultiMeshBullets2D::set_max_change_texture_time(float new_max_change_texture_time) {
    max_change_texture_time = new_max_change_texture_time;
}

float SaveDataMultiMeshBullets2D::get_max_change_texture_time() const {
    return max_change_texture_time;
}

void SaveDataMultiMeshBullets2D::set_current_change_texture_time(float new_current_change_texture_time) {
    current_change_texture_time = new_current_change_texture_time;
}

float SaveDataMultiMeshBullets2D::get_current_change_texture_time() const {
    return current_change_texture_time;
}

void SaveDataMultiMeshBullets2D::set_current_texture_index(int new_current_texture_index) {
    current_texture_index = new_current_texture_index;
}

int SaveDataMultiMeshBullets2D::get_current_texture_index() const {
    return current_texture_index;
}

void SaveDataMultiMeshBullets2D::set_collision_layer(int new_collision_layer) {
    collision_layer = new_collision_layer;
}

int SaveDataMultiMeshBullets2D::get_collision_layer() const {
    return collision_layer;
}

void SaveDataMultiMeshBullets2D::set_collision_mask(int new_collision_mask) {
    collision_mask = new_collision_mask;
}

int SaveDataMultiMeshBullets2D::get_collision_mask() const {
    return collision_mask;
}

void SaveDataMultiMeshBullets2D::set_collision_shape_size(Vector2 new_collision_shape_size) {
    collision_shape_size = new_collision_shape_size;
}

Vector2 SaveDataMultiMeshBullets2D::get_collision_shape_size() const {
    return collision_shape_size;
}

void SaveDataMultiMeshBullets2D::set_monitorable(bool new_monitorable) {
    monitorable = new_monitorable;
}

bool SaveDataMultiMeshBullets2D::get_monitorable() const {
    return monitorable;
}

void SaveDataMultiMeshBullets2D::set_material(Ref<Material> new_material) {
    material = new_material;
}

Ref<Material> SaveDataMultiMeshBullets2D::get_material() const {
    return material;
}

void SaveDataMultiMeshBullets2D::set_mesh(Ref<Mesh> new_mesh) {
    mesh = new_mesh;
}

Ref<Mesh> SaveDataMultiMeshBullets2D::get_mesh() const {
    return mesh;
}

TypedArray<BulletRotationData> SaveDataMultiMeshBullets2D::get_all_bullet_rotation_data() {
    return all_bullet_rotation_data;
}
void SaveDataMultiMeshBullets2D::set_all_bullet_rotation_data(const TypedArray<BulletRotationData> &new_bullet_rotation_data) {
    all_bullet_rotation_data.resize(new_bullet_rotation_data.size());
    for (int i = 0; i < new_bullet_rotation_data.size(); i++) {
        all_bullet_rotation_data[i] = new_bullet_rotation_data[i];
    }
}

bool SaveDataMultiMeshBullets2D::get_rotate_only_textures() {
    return rotate_only_textures;
}
void SaveDataMultiMeshBullets2D::set_rotate_only_textures(bool new_rotate_only_textures) {
    rotate_only_textures = new_rotate_only_textures;
}

// MOVEMENT RELATED

TypedArray<Transform2D> SaveDataMultiMeshBullets2D::get_all_cached_instance_transforms() {
    return all_cached_instance_transforms;
}
void SaveDataMultiMeshBullets2D::set_all_cached_instance_transforms(const TypedArray<Transform2D> new_data) {
    all_cached_instance_transforms = new_data;
}

TypedArray<Transform2D> SaveDataMultiMeshBullets2D::get_all_cached_shape_transforms() {
    return all_cached_shape_transforms;
}
void SaveDataMultiMeshBullets2D::set_all_cached_shape_transforms(const TypedArray<Transform2D> new_data) {
    all_cached_shape_transforms = new_data;
}

TypedArray<Vector2> SaveDataMultiMeshBullets2D::get_all_cached_instance_origin() {
    return all_cached_instance_origin;
}
void SaveDataMultiMeshBullets2D::set_all_cached_instance_origin(const TypedArray<Vector2> new_data) {
    all_cached_instance_origin = new_data;
}

TypedArray<Vector2> SaveDataMultiMeshBullets2D::get_all_cached_shape_origin() {
    return all_cached_shape_origin;
}
void SaveDataMultiMeshBullets2D::set_all_cached_shape_origin(const TypedArray<Vector2> new_data) {
    all_cached_shape_origin = new_data;
}

TypedArray<Vector2> SaveDataMultiMeshBullets2D::get_all_cached_velocity() {
    return all_cached_velocity;
}
void SaveDataMultiMeshBullets2D::set_all_cached_velocity(const TypedArray<Vector2> new_data) {
    all_cached_velocity = new_data;
}

TypedArray<Vector2> SaveDataMultiMeshBullets2D::get_all_cached_direction() {
    return all_cached_direction;
}
void SaveDataMultiMeshBullets2D::set_all_cached_direction(const TypedArray<Vector2> new_data) {
    all_cached_direction = new_data;
}

// SPEED RELATED

TypedArray<float> SaveDataMultiMeshBullets2D::get_all_cached_speed() {
    return all_cached_speed;
}
void SaveDataMultiMeshBullets2D::set_all_cached_speed(const TypedArray<float> new_data) {
    all_cached_speed = new_data;
}

TypedArray<float> SaveDataMultiMeshBullets2D::get_all_cached_max_speed() {
    return all_cached_max_speed;
}
void SaveDataMultiMeshBullets2D::set_all_cached_max_speed(const TypedArray<float> new_data) {
    all_cached_max_speed = new_data;
}

TypedArray<float> SaveDataMultiMeshBullets2D::get_all_cached_acceleration() {
    return all_cached_acceleration;
}
void SaveDataMultiMeshBullets2D::set_all_cached_acceleration(const TypedArray<float> new_data) {
    all_cached_acceleration = new_data;
}


void SaveDataMultiMeshBullets2D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_max_life_time"), &SaveDataMultiMeshBullets2D::set_max_life_time);
    ClassDB::bind_method(D_METHOD("get_max_life_time"), &SaveDataMultiMeshBullets2D::get_max_life_time);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_life_time"), "set_max_life_time", "get_max_life_time");

    ClassDB::bind_method(D_METHOD("set_current_life_time"), &SaveDataMultiMeshBullets2D::set_current_life_time);
    ClassDB::bind_method(D_METHOD("get_current_life_time"), &SaveDataMultiMeshBullets2D::get_current_life_time);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "current_life_time"), "set_current_life_time", "get_current_life_time");

    ClassDB::bind_method(D_METHOD("set_size"), &SaveDataMultiMeshBullets2D::set_size);
    ClassDB::bind_method(D_METHOD("get_size"), &SaveDataMultiMeshBullets2D::get_size);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "size"), "set_size", "get_size");

    ClassDB::bind_method(D_METHOD("set_bullets_enabled_status"), &SaveDataMultiMeshBullets2D::set_bullets_enabled_status);
    ClassDB::bind_method(D_METHOD("get_bullets_enabled_status"), &SaveDataMultiMeshBullets2D::get_bullets_enabled_status);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bullets_enabled_status"), "set_bullets_enabled_status", "get_bullets_enabled_status");

    ClassDB::bind_method(D_METHOD("set_bullets_custom_data"), &SaveDataMultiMeshBullets2D::set_bullets_custom_data);
    ClassDB::bind_method(D_METHOD("get_bullets_custom_data"), &SaveDataMultiMeshBullets2D::get_bullets_custom_data);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bullets_custom_data", PROPERTY_HINT_RESOURCE_TYPE, "Resource"), "set_bullets_custom_data", "get_bullets_custom_data");

    ClassDB::bind_method(D_METHOD("set_textures"), &SaveDataMultiMeshBullets2D::set_textures);
    ClassDB::bind_method(D_METHOD("get_textures"), &SaveDataMultiMeshBullets2D::get_textures);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "textures", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_textures", "get_textures");

    ClassDB::bind_method(D_METHOD("set_texture_size"), &SaveDataMultiMeshBullets2D::set_texture_size);
    ClassDB::bind_method(D_METHOD("get_texture_size"), &SaveDataMultiMeshBullets2D::get_texture_size);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "texture_size"), "set_texture_size", "get_texture_size");

    ClassDB::bind_method(D_METHOD("set_max_change_texture_time"), &SaveDataMultiMeshBullets2D::set_max_change_texture_time);
    ClassDB::bind_method(D_METHOD("get_max_change_texture_time"), &SaveDataMultiMeshBullets2D::get_max_change_texture_time);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_change_texture_time"), "set_max_change_texture_time", "get_max_change_texture_time");

    ClassDB::bind_method(D_METHOD("set_current_change_texture_time"), &SaveDataMultiMeshBullets2D::set_current_change_texture_time);
    ClassDB::bind_method(D_METHOD("get_current_change_texture_time"), &SaveDataMultiMeshBullets2D::get_current_change_texture_time);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "current_change_texture_time"), "set_current_change_texture_time", "get_current_change_texture_time");

    ClassDB::bind_method(D_METHOD("set_current_texture_index"), &SaveDataMultiMeshBullets2D::set_current_texture_index);
    ClassDB::bind_method(D_METHOD("get_current_texture_index"), &SaveDataMultiMeshBullets2D::get_current_texture_index);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "current_texture_index"), "set_current_texture_index", "get_current_texture_index");

    ClassDB::bind_method(D_METHOD("set_collision_layer"), &SaveDataMultiMeshBullets2D::set_collision_layer);
    ClassDB::bind_method(D_METHOD("get_collision_layer"), &SaveDataMultiMeshBullets2D::get_collision_layer);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer"), "set_collision_layer", "get_collision_layer");

    ClassDB::bind_method(D_METHOD("set_collision_mask"), &SaveDataMultiMeshBullets2D::set_collision_mask);
    ClassDB::bind_method(D_METHOD("get_collision_mask"), &SaveDataMultiMeshBullets2D::get_collision_mask);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask"), "set_collision_mask", "get_collision_mask");

    ClassDB::bind_method(D_METHOD("set_collision_shape_size"), &SaveDataMultiMeshBullets2D::set_collision_shape_size);
    ClassDB::bind_method(D_METHOD("get_collision_shape_size"), &SaveDataMultiMeshBullets2D::get_collision_shape_size);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "collision_shape_size"), "set_collision_shape_size", "get_collision_shape_size");

    ClassDB::bind_method(D_METHOD("set_monitorable"), &SaveDataMultiMeshBullets2D::set_monitorable);
    ClassDB::bind_method(D_METHOD("get_monitorable"), &SaveDataMultiMeshBullets2D::get_monitorable);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitorable"), "set_monitorable", "get_monitorable");

    ClassDB::bind_method(D_METHOD("set_material"), &SaveDataMultiMeshBullets2D::set_material);
    ClassDB::bind_method(D_METHOD("get_material"), &SaveDataMultiMeshBullets2D::get_material);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_material", "get_material");

    ClassDB::bind_method(D_METHOD("set_mesh"), &SaveDataMultiMeshBullets2D::set_mesh);
    ClassDB::bind_method(D_METHOD("get_mesh"), &SaveDataMultiMeshBullets2D::get_mesh);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_mesh", "get_mesh");

    ClassDB::bind_method(D_METHOD("get_all_bullet_rotation_data"), &SaveDataMultiMeshBullets2D::get_all_bullet_rotation_data);
    ClassDB::bind_method(D_METHOD("set_all_bullet_rotation_data", "new_data"), &SaveDataMultiMeshBullets2D::set_all_bullet_rotation_data);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_rotation_data"), "set_all_bullet_rotation_data", "get_all_bullet_rotation_data");

    ClassDB::bind_method(D_METHOD("get_rotate_only_textures"), &SaveDataMultiMeshBullets2D::get_rotate_only_textures);
    ClassDB::bind_method(D_METHOD("set_rotate_only_textures", "new_rotate_only_textures"), &SaveDataMultiMeshBullets2D::set_rotate_only_textures);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rotate_only_textures"), "set_rotate_only_textures", "get_rotate_only_textures");

    // MOVEMENT RELATED

    ClassDB::bind_method(D_METHOD("get_all_cached_instance_transforms"), &SaveDataMultiMeshBullets2D::get_all_cached_instance_transforms);
    ClassDB::bind_method(D_METHOD("set_all_cached_instance_transforms", "new_data"), &SaveDataMultiMeshBullets2D::set_all_cached_instance_transforms);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_cached_instance_transforms"), "set_all_cached_instance_transforms", "get_all_cached_instance_transforms");

    ClassDB::bind_method(D_METHOD("get_all_cached_shape_transforms"), &SaveDataMultiMeshBullets2D::get_all_cached_shape_transforms);
    ClassDB::bind_method(D_METHOD("set_all_cached_shape_transforms", "new_data"), &SaveDataMultiMeshBullets2D::set_all_cached_shape_transforms);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_cached_shape_transforms"), "set_all_cached_shape_transforms", "get_all_cached_shape_transforms");

    ClassDB::bind_method(D_METHOD("get_all_cached_instance_origin"), &SaveDataMultiMeshBullets2D::get_all_cached_instance_origin);
    ClassDB::bind_method(D_METHOD("set_all_cached_instance_origin", "new_data"), &SaveDataMultiMeshBullets2D::set_all_cached_instance_origin);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_cached_instance_origin"), "set_all_cached_instance_origin", "get_all_cached_instance_origin");

    ClassDB::bind_method(D_METHOD("get_all_cached_shape_origin"), &SaveDataMultiMeshBullets2D::get_all_cached_shape_origin);
    ClassDB::bind_method(D_METHOD("set_all_cached_shape_origin", "new_data"), &SaveDataMultiMeshBullets2D::set_all_cached_shape_origin);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_cached_shape_origin"), "set_all_cached_shape_origin", "get_all_cached_shape_origin");

    ClassDB::bind_method(D_METHOD("get_all_cached_velocity"), &SaveDataMultiMeshBullets2D::get_all_cached_velocity);
    ClassDB::bind_method(D_METHOD("set_all_cached_velocity", "new_data"), &SaveDataMultiMeshBullets2D::set_all_cached_velocity);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_cached_velocity"), "set_all_cached_velocity", "get_all_cached_velocity");

    ClassDB::bind_method(D_METHOD("get_all_cached_direction"), &SaveDataMultiMeshBullets2D::get_all_cached_direction);
    ClassDB::bind_method(D_METHOD("set_all_cached_direction", "new_data"), &SaveDataMultiMeshBullets2D::set_all_cached_direction);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_cached_direction"), "set_all_cached_direction", "get_all_cached_direction");

    // SPEED RELATED

    ClassDB::bind_method(D_METHOD("get_all_cached_speed"), &SaveDataMultiMeshBullets2D::get_all_cached_speed);
    ClassDB::bind_method(D_METHOD("set_all_cached_speed", "new_data"), &SaveDataMultiMeshBullets2D::set_all_cached_speed);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_cached_speed"), "set_all_cached_speed", "get_all_cached_speed");

    ClassDB::bind_method(D_METHOD("get_all_cached_max_speed"), &SaveDataMultiMeshBullets2D::get_all_cached_max_speed);
    ClassDB::bind_method(D_METHOD("set_all_cached_max_speed", "new_data"), &SaveDataMultiMeshBullets2D::set_all_cached_max_speed);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_cached_max_speed"), "set_all_cached_max_speed", "get_all_cached_max_speed");

    ClassDB::bind_method(D_METHOD("get_all_cached_acceleration"), &SaveDataMultiMeshBullets2D::get_all_cached_acceleration);
    ClassDB::bind_method(D_METHOD("set_all_cached_acceleration", "new_data"), &SaveDataMultiMeshBullets2D::set_all_cached_acceleration);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_cached_acceleration"), "set_all_cached_acceleration", "get_all_cached_acceleration");
}
}