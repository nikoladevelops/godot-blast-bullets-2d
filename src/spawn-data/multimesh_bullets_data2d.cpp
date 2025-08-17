#include "./multimesh_bullets_data2d.hpp"

using namespace godot;

namespace BlastBullets2D {

int MultiMeshBulletsData2D::calculate_bitmask(const TypedArray<int> &numbers) {
    int bitmask_value = 0;
    for (int i = 0; i < numbers.size(); i++) {
        // From the current number calculate which bit it corresponds to inside the bitmask (number 5 = 5th bit from right to left = 10000 in binary). This is the same as the formula 2 to the power of N-1. Example: if we have the number 4, then its 2 to the power of 4-1 -> this is equal to -> 2 to the power of 3 = 8  -> turn 8 into binary = 1000. I am doing exactly the same thing here by saying I have the number 1, shift it to the left by N-1 = 1000  (because the number one got shifted to the left by 3 positions, those 3 positions are now filled with zeros)
        bitmask_value |= 1 << ((int)(numbers[i]) - 1); // this is the more inefficient way of doing the same thing: static_cast<int>(pow(2, (int)(numbers[i]) - 1));
    }

    return bitmask_value;
}


TypedArray<Transform2D> MultiMeshBulletsData2D::get_transforms() const {
    return transforms;
}
void MultiMeshBulletsData2D::set_transforms(const TypedArray<Transform2D> new_transforms) {
    transforms = new_transforms;
}

TypedArray<Texture2D> MultiMeshBulletsData2D::get_textures() const {
    return textures;
}

void MultiMeshBulletsData2D::set_textures(const TypedArray<Texture2D> &new_textures) {
    textures.resize(new_textures.size());
    for (int i = 0; i < new_textures.size(); i++) {
        textures[i] = new_textures[i];
    }
}

Vector2 MultiMeshBulletsData2D::get_texture_size() const {
    return texture_size;
}
void MultiMeshBulletsData2D::set_texture_size(Vector2 new_texture_size) {
    texture_size = new_texture_size;
}

float MultiMeshBulletsData2D::get_texture_rotation_radians() const {
    return texture_rotation_radians;
}
void MultiMeshBulletsData2D::set_texture_rotation_radians(float new_texture_rotation_radians) {
    texture_rotation_radians = new_texture_rotation_radians;
}

int MultiMeshBulletsData2D::get_current_texture_index() const {
    return current_texture_index;
}
void MultiMeshBulletsData2D::set_current_texture_index(int new_current_texture_index) {
    current_texture_index = new_current_texture_index;
}

float MultiMeshBulletsData2D::get_default_change_texture_time() const {
    return default_change_texture_time;
}
void MultiMeshBulletsData2D::set_default_change_texture_time(float new_default_change_texture_time) {
    default_change_texture_time = new_default_change_texture_time;
}

int MultiMeshBulletsData2D::get_collision_layer() const {
    return collision_layer;
}
void MultiMeshBulletsData2D::set_collision_layer(int new_collision_layer) {
    collision_layer = new_collision_layer;
}

void MultiMeshBulletsData2D::set_collision_layer_from_array(const TypedArray<int> &numbers)
{
    int bitmask = calculate_bitmask(numbers);
    collision_layer = bitmask;
}

int MultiMeshBulletsData2D::get_collision_mask() const {
    return collision_mask;
}
void MultiMeshBulletsData2D::set_collision_mask(int new_collision_mask) {
    collision_mask = new_collision_mask;
}

void MultiMeshBulletsData2D::set_collision_mask_from_array(const TypedArray<int> &numbers)
{
    int bitmask = calculate_bitmask(numbers);
    collision_mask = bitmask;
}

Vector2 MultiMeshBulletsData2D::get_collision_shape_size() const {
    return collision_shape_size;
}
void MultiMeshBulletsData2D::set_collision_shape_size(const Vector2 &new_collision_shape_size) {
    collision_shape_size = new_collision_shape_size;
}

Vector2 MultiMeshBulletsData2D::get_collision_shape_offset() const {
    return collision_shape_offset;
}
void MultiMeshBulletsData2D::set_collision_shape_offset(const Vector2 &new_collision_shape_offset) {
    collision_shape_offset = new_collision_shape_offset;
}

bool MultiMeshBulletsData2D::get_monitorable() const {
    return monitorable;
}
void MultiMeshBulletsData2D::set_monitorable(bool new_monitorable) {
    monitorable = new_monitorable;
}

Ref<Resource> MultiMeshBulletsData2D::get_bullets_custom_data() const {
    return bullets_custom_data;
}
void MultiMeshBulletsData2D::set_bullets_custom_data(const Ref<Resource> &new_bullets_custom_data) {
    bullets_custom_data = new_bullets_custom_data;
}

float MultiMeshBulletsData2D::get_max_life_time() const {
    return max_life_time;
}
void MultiMeshBulletsData2D::set_max_life_time(float new_max_life_time) {
    max_life_time = new_max_life_time;
}

Ref<Material> MultiMeshBulletsData2D::get_material() const {
    return material;
}
void MultiMeshBulletsData2D::set_material(const Ref<Material> &new_material) {
    material = new_material;
}

Ref<Mesh> MultiMeshBulletsData2D::get_mesh() const {
    return mesh;
}
void MultiMeshBulletsData2D::set_mesh(const Ref<Mesh> &new_mesh) {
    mesh = new_mesh;
}

TypedArray<BulletRotationData2D> MultiMeshBulletsData2D::get_all_bullet_rotation_data() const {
    return all_bullet_rotation_data;
}
void MultiMeshBulletsData2D::set_all_bullet_rotation_data(const TypedArray<BulletRotationData2D> &new_bullet_rotation_data) {
    all_bullet_rotation_data.resize(new_bullet_rotation_data.size());
    for (int i = 0; i < new_bullet_rotation_data.size(); i++) {
        all_bullet_rotation_data[i] = new_bullet_rotation_data[i];
    }
}

bool MultiMeshBulletsData2D::get_rotate_only_textures() const {
    return rotate_only_textures;
}
void MultiMeshBulletsData2D::set_rotate_only_textures(bool new_rotate_only_textures) {
    rotate_only_textures = new_rotate_only_textures;
}

bool MultiMeshBulletsData2D::get_is_texture_rotation_permanent() const {
    return is_texture_rotation_permanent;
}
void MultiMeshBulletsData2D::set_is_texture_rotation_permanent(bool new_is_texture_rotation_permanent) {
    is_texture_rotation_permanent = new_is_texture_rotation_permanent;
}

Vector2 MultiMeshBulletsData2D::get_bullet_attachment_offset() const
{
    return bullet_attachment_offset;   
}

void MultiMeshBulletsData2D::set_bullet_attachment_offset(const Vector2 &new_bullet_attachment_offset)
{
    bullet_attachment_offset = new_bullet_attachment_offset;
}


Ref<PackedScene> MultiMeshBulletsData2D::get_bullet_attachment_scene() const{
    return bullet_attachment_scene;
}
void MultiMeshBulletsData2D::set_bullet_attachment_scene(const Ref<PackedScene> &new_bullet_attachment_scene){
    bullet_attachment_scene = new_bullet_attachment_scene;
}

int MultiMeshBulletsData2D::get_z_index() const{
    return z_index;
}

void MultiMeshBulletsData2D::set_z_index(int new_z_index){
    z_index = new_z_index;
}

int MultiMeshBulletsData2D::get_light_mask() const{
    return light_mask;
}

void MultiMeshBulletsData2D::set_light_mask(int new_light_mask){
    light_mask = new_light_mask;
}

void MultiMeshBulletsData2D::set_light_mask_from_array(const TypedArray<int>& numbers){
    int bitmask = calculate_bitmask(numbers);
    light_mask = bitmask;
}


int MultiMeshBulletsData2D::get_visibility_layer() const{
    return visibility_layer;
}

void MultiMeshBulletsData2D::set_visibility_layer(int new_visibility_layer){
    visibility_layer = new_visibility_layer;
}

void MultiMeshBulletsData2D::set_visibility_layer_from_array(const TypedArray<int> &numbers){
    int bitmask = calculate_bitmask(numbers);
    visibility_layer = bitmask;
}

Dictionary MultiMeshBulletsData2D::get_instance_shader_parameters() const
{
    return instance_shader_parameters;
}

void MultiMeshBulletsData2D::set_instance_shader_parameters(const Dictionary &new_instance_shader_parameters)
{
    instance_shader_parameters = new_instance_shader_parameters;
}

TypedArray<float> MultiMeshBulletsData2D::get_change_texture_times() const{
    return change_texture_times;
}

void MultiMeshBulletsData2D::set_change_texture_times(const TypedArray<float> &new_change_texture_times){
    change_texture_times = new_change_texture_times;
}

Ref<Texture2D> MultiMeshBulletsData2D::get_default_texture() const{
    return default_texture;
}

void MultiMeshBulletsData2D::set_default_texture(const Ref<Texture2D> &new_default_texture){
    default_texture = new_default_texture;
}

bool MultiMeshBulletsData2D::get_is_life_time_over_signal_enabled() const{
    return is_life_time_over_signal_enabled;
}

void MultiMeshBulletsData2D::set_is_life_time_over_signal_enabled(bool new_is_life_time_over_signal_enabled){
    is_life_time_over_signal_enabled = new_is_life_time_over_signal_enabled;
}

bool MultiMeshBulletsData2D::get_stop_rotation_when_max_reached() const{
    return stop_rotation_when_max_reached;
}

void MultiMeshBulletsData2D::set_stop_rotation_when_max_reached(bool new_stop_rotation_when_max_reached){
    stop_rotation_when_max_reached = new_stop_rotation_when_max_reached;
}

void MultiMeshBulletsData2D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_transforms"), &MultiMeshBulletsData2D::set_transforms);
    ClassDB::bind_method(D_METHOD("get_transforms"), &MultiMeshBulletsData2D::get_transforms);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "transforms"), "set_transforms", "get_transforms");

    ClassDB::bind_method(D_METHOD("get_textures"), &MultiMeshBulletsData2D::get_textures);
    ClassDB::bind_method(D_METHOD("set_textures", "new_textures"), &MultiMeshBulletsData2D::set_textures);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "textures"), "set_textures", "get_textures");

    ClassDB::bind_method(D_METHOD("get_texture_size"), &MultiMeshBulletsData2D::get_texture_size);
    ClassDB::bind_method(D_METHOD("set_texture_size", "new_texture_size"), &MultiMeshBulletsData2D::set_texture_size);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "texture_size"), "set_texture_size", "get_texture_size");

    ClassDB::bind_method(D_METHOD("get_texture_rotation_radians"), &MultiMeshBulletsData2D::get_texture_rotation_radians);
    ClassDB::bind_method(D_METHOD("set_texture_rotation_radians", "new_texture_rotation_radians"), &MultiMeshBulletsData2D::set_texture_rotation_radians);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "texture_rotation_radians"), "set_texture_rotation_radians", "get_texture_rotation_radians");

    ClassDB::bind_method(D_METHOD("get_current_texture_index"), &MultiMeshBulletsData2D::get_current_texture_index);
    ClassDB::bind_method(D_METHOD("set_current_texture_index", "new_current_texture_index"), &MultiMeshBulletsData2D::set_current_texture_index);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "current_texture_index"), "set_current_texture_index", "get_current_texture_index");

    ClassDB::bind_method(D_METHOD("get_default_change_texture_time"), &MultiMeshBulletsData2D::get_default_change_texture_time);
    ClassDB::bind_method(D_METHOD("set_default_change_texture_time", "new_default_change_texture_time"), &MultiMeshBulletsData2D::set_default_change_texture_time);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "default_change_texture_time"), "set_default_change_texture_time", "get_default_change_texture_time");

    ClassDB::bind_method(D_METHOD("get_collision_layer"), &MultiMeshBulletsData2D::get_collision_layer);
    ClassDB::bind_method(D_METHOD("set_collision_layer", "new_collision_layer"), &MultiMeshBulletsData2D::set_collision_layer);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer"), "set_collision_layer", "get_collision_layer");

    ClassDB::bind_method(D_METHOD("get_collision_mask"), &MultiMeshBulletsData2D::get_collision_mask);
    ClassDB::bind_method(D_METHOD("set_collision_mask", "new_collision_mask"), &MultiMeshBulletsData2D::set_collision_mask);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask"), "set_collision_mask", "get_collision_mask");

    ClassDB::bind_method(D_METHOD("get_collision_shape_size"), &MultiMeshBulletsData2D::get_collision_shape_size);
    ClassDB::bind_method(D_METHOD("set_collision_shape_size", "new_collision_shape_size"), &MultiMeshBulletsData2D::set_collision_shape_size);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "collision_shape_size"), "set_collision_shape_size", "get_collision_shape_size");

    ClassDB::bind_method(D_METHOD("get_collision_shape_offset"), &MultiMeshBulletsData2D::get_collision_shape_offset);
    ClassDB::bind_method(D_METHOD("set_collision_shape_offset", "new_collision_shape_offset"), &MultiMeshBulletsData2D::set_collision_shape_offset);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "collision_shape_offset"), "set_collision_shape_offset", "get_collision_shape_offset");

    ClassDB::bind_method(D_METHOD("get_monitorable"), &MultiMeshBulletsData2D::get_monitorable);
    ClassDB::bind_method(D_METHOD("set_monitorable", "new_monitorable"), &MultiMeshBulletsData2D::set_monitorable);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitorable"), "set_monitorable", "get_monitorable");

    ClassDB::bind_method(D_METHOD("get_bullets_custom_data"), &MultiMeshBulletsData2D::get_bullets_custom_data);
    ClassDB::bind_method(D_METHOD("set_bullets_custom_data", "new_bullets_custom_data"), &MultiMeshBulletsData2D::set_bullets_custom_data);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bullets_custom_data"), "set_bullets_custom_data", "get_bullets_custom_data");

    ClassDB::bind_method(D_METHOD("get_max_life_time"), &MultiMeshBulletsData2D::get_max_life_time);
    ClassDB::bind_method(D_METHOD("set_max_life_time", "new_max_life_time"), &MultiMeshBulletsData2D::set_max_life_time);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_life_time"), "set_max_life_time", "get_max_life_time");

    ClassDB::bind_method(D_METHOD("get_material"), &MultiMeshBulletsData2D::get_material);
    ClassDB::bind_method(D_METHOD("set_material", "new_material"), &MultiMeshBulletsData2D::set_material);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material"), "set_material", "get_material");

    ClassDB::bind_method(D_METHOD("get_mesh"), &MultiMeshBulletsData2D::get_mesh);
    ClassDB::bind_method(D_METHOD("set_mesh", "new_mesh"), &MultiMeshBulletsData2D::set_mesh);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh"), "set_mesh", "get_mesh");

    ClassDB::bind_method(D_METHOD("get_all_bullet_rotation_data"), &MultiMeshBulletsData2D::get_all_bullet_rotation_data);
    ClassDB::bind_method(D_METHOD("set_all_bullet_rotation_data", "new_data"), &MultiMeshBulletsData2D::set_all_bullet_rotation_data);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_rotation_data"), "set_all_bullet_rotation_data", "get_all_bullet_rotation_data");

    ClassDB::bind_method(D_METHOD("get_rotate_only_textures"), &MultiMeshBulletsData2D::get_rotate_only_textures);
    ClassDB::bind_method(D_METHOD("set_rotate_only_textures", "new_rotate_only_textures"), &MultiMeshBulletsData2D::set_rotate_only_textures);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rotate_only_textures"), "set_rotate_only_textures", "get_rotate_only_textures");

    ClassDB::bind_method(D_METHOD("get_is_texture_rotation_permanent"), &MultiMeshBulletsData2D::get_is_texture_rotation_permanent);
    ClassDB::bind_method(D_METHOD("set_is_texture_rotation_permanent", "new_is_texture_rotation_permanent"), &MultiMeshBulletsData2D::set_is_texture_rotation_permanent);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_texture_rotation_permanent"), "set_is_texture_rotation_permanent", "get_is_texture_rotation_permanent");

    ClassDB::bind_method(D_METHOD("get_bullet_attachment_scene"), &MultiMeshBulletsData2D::get_bullet_attachment_scene);
    ClassDB::bind_method(D_METHOD("set_bullet_attachment_scene", "new_bullet_attachment_scene"), &MultiMeshBulletsData2D::set_bullet_attachment_scene);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bullet_attachment_scene"), "set_bullet_attachment_scene", "get_bullet_attachment_scene");

    ClassDB::bind_method(D_METHOD("get_bullet_attachment_offset"), &MultiMeshBulletsData2D::get_bullet_attachment_offset);
    ClassDB::bind_method(D_METHOD("set_bullet_attachment_offset", "new_bullet_attachment_offset"), &MultiMeshBulletsData2D::set_bullet_attachment_offset);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "bullet_attachment_offset"), "set_bullet_attachment_offset", "get_bullet_attachment_offset");

    ClassDB::bind_method(D_METHOD("get_z_index"), &MultiMeshBulletsData2D::get_z_index);
    ClassDB::bind_method(D_METHOD("set_z_index", "new_z_index"), &MultiMeshBulletsData2D::set_z_index);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "z_index"), "set_z_index", "get_z_index");
    
    
    ClassDB::bind_method(D_METHOD("get_light_mask"), &MultiMeshBulletsData2D::get_light_mask);
    ClassDB::bind_method(D_METHOD("set_light_mask", "new_light_mask"), &MultiMeshBulletsData2D::set_light_mask);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "light_mask"), "set_light_mask", "get_light_mask");
    
    ClassDB::bind_method(D_METHOD("get_visibility_layer"), &MultiMeshBulletsData2D::get_visibility_layer);
    ClassDB::bind_method(D_METHOD("set_visibility_layer", "new_visibility_layer"), &MultiMeshBulletsData2D::set_visibility_layer);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "visibility_layer"), "set_visibility_layer", "get_visibility_layer");

    ClassDB::bind_method(D_METHOD("set_collision_layer_from_array", "array_of_layers"), &MultiMeshBulletsData2D::set_collision_layer_from_array);
    ClassDB::bind_method(D_METHOD("set_collision_mask_from_array", "array_of_masks"), &MultiMeshBulletsData2D::set_collision_mask_from_array);
    ClassDB::bind_method(D_METHOD("set_light_mask_from_array", "array_of_light_masks"), &MultiMeshBulletsData2D::set_light_mask_from_array);
    ClassDB::bind_method(D_METHOD("set_visibility_layer_from_array", "array_of_visibility_layers"), &MultiMeshBulletsData2D::set_visibility_layer_from_array);

    ClassDB::bind_method(D_METHOD("get_instance_shader_parameters"), &MultiMeshBulletsData2D::get_instance_shader_parameters);
    ClassDB::bind_method(D_METHOD("set_instance_shader_parameters", "new_instance_shader_parameters"), &MultiMeshBulletsData2D::set_instance_shader_parameters);
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "instance_shader_parameters", PROPERTY_HINT_TYPE_STRING, "String:Variant"), "set_instance_shader_parameters", "get_instance_shader_parameters");

    ClassDB::bind_method(D_METHOD("get_change_texture_times"), &MultiMeshBulletsData2D::get_change_texture_times);
    ClassDB::bind_method(D_METHOD("set_change_texture_times"), &MultiMeshBulletsData2D::set_change_texture_times);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "change_texture_times"), "set_change_texture_times", "get_change_texture_times");

    ClassDB::bind_method(D_METHOD("get_default_texture"), &MultiMeshBulletsData2D::get_default_texture);
    ClassDB::bind_method(D_METHOD("set_default_texture"), &MultiMeshBulletsData2D::set_default_texture);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "default_texture"), "set_default_texture", "get_default_texture");

    ClassDB::bind_method(D_METHOD("get_is_life_time_over_signal_enabled"), &MultiMeshBulletsData2D::get_is_life_time_over_signal_enabled);
    ClassDB::bind_method(D_METHOD("set_is_life_time_over_signal_enabled"), &MultiMeshBulletsData2D::set_is_life_time_over_signal_enabled);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_life_time_over_signal_enabled"), "set_is_life_time_over_signal_enabled", "get_is_life_time_over_signal_enabled");

    ClassDB::bind_method(D_METHOD("get_stop_rotation_when_max_reached"), &MultiMeshBulletsData2D::get_stop_rotation_when_max_reached);
    ClassDB::bind_method(D_METHOD("set_stop_rotation_when_max_reached"), &MultiMeshBulletsData2D::set_stop_rotation_when_max_reached);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "stop_rotation_when_max_reached"), "set_stop_rotation_when_max_reached", "get_stop_rotation_when_max_reached");

    ClassDB::bind_static_method("MultiMeshBulletsData2D", D_METHOD("calculate_bitmask", "numbers"), &MultiMeshBulletsData2D::calculate_bitmask);
}
}