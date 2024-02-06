#include "block_bullets_data2d.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

using namespace godot;

TypedArray<Texture2D> BlockBulletsData2D::get_textures() const {
    return textures;
}

void BlockBulletsData2D::set_textures(const TypedArray<Texture2D>& new_textures) {
    textures.resize(new_textures.size());
    for (int i = 0; i < new_textures.size(); i++)
    {
        textures[i]=new_textures[i];
    }
}

Vector2 BlockBulletsData2D::get_texture_size() const{
    return texture_size;
}
void BlockBulletsData2D::set_texture_size(Vector2 new_texture_size){
    texture_size=new_texture_size;
}

float BlockBulletsData2D::get_texture_rotation_radians() const {
    return texture_rotation_radians;
}
void BlockBulletsData2D::set_texture_rotation_radians(float new_texture_rotation_radians) {
    texture_rotation_radians = new_texture_rotation_radians;
}

int BlockBulletsData2D::get_current_texture_index() const {
    return current_texture_index;
}
void BlockBulletsData2D::set_current_texture_index(int new_current_texture_index) {
    current_texture_index = new_current_texture_index;
}

TypedArray<Transform2D> BlockBulletsData2D::get_transforms() const {
    return transforms;
}
void BlockBulletsData2D::set_transforms(const TypedArray<Transform2D>& new_transforms) {
    transforms = new_transforms;
}

float BlockBulletsData2D::get_block_rotation_radians() const {
    return block_rotation_radians;
}
void BlockBulletsData2D::set_block_rotation_radians(float new_block_rotation_radians) {
    block_rotation_radians = new_block_rotation_radians;
}

float BlockBulletsData2D::get_speed() const {
    return speed;
}
void BlockBulletsData2D::set_speed(float new_speed) {
    speed = new_speed;
}

float BlockBulletsData2D::get_max_speed() const{
    return max_speed;
}
void BlockBulletsData2D::set_max_speed(float new_max_speed){
    max_speed=new_max_speed;
}

float BlockBulletsData2D::get_acceleration() const {
    return acceleration;
}
void BlockBulletsData2D::set_acceleration(float new_acceleration) {
    acceleration = new_acceleration;
}

float BlockBulletsData2D::get_max_acceleration_time() const{
    return max_acceleration_time;
}
void BlockBulletsData2D::set_max_acceleration_time(float new_max_acceleration_time){
    max_acceleration_time=new_max_acceleration_time;
}

float BlockBulletsData2D::get_max_change_texture_time() const {
    return max_change_texture_time;
}
void BlockBulletsData2D::set_max_change_texture_time(float new_max_change_texture_time) {
    max_change_texture_time = new_max_change_texture_time;
}

uint32_t BlockBulletsData2D::get_collision_layer() const {
    return collision_layer;
}
void BlockBulletsData2D::set_collision_layer(uint32_t new_collision_layer) {
    collision_layer = new_collision_layer;
}

uint32_t BlockBulletsData2D::get_collision_mask() const {
    return collision_mask;
}
void BlockBulletsData2D::set_collision_mask(uint32_t new_collision_mask) {
    collision_mask = new_collision_mask;
}

Vector2 BlockBulletsData2D::get_collision_shape_size() const {
    return collision_shape_size;
}
void BlockBulletsData2D::set_collision_shape_size(const Vector2& new_collision_shape_size) {
    collision_shape_size = new_collision_shape_size;
}

Vector2 BlockBulletsData2D::get_collision_shape_offset() const {
    return collision_shape_offset;
}
void BlockBulletsData2D::set_collision_shape_offset(const Vector2& new_collision_shape_offset) {
    collision_shape_offset = new_collision_shape_offset;
}

bool BlockBulletsData2D::get_monitorable() const {
    return monitorable;
}
void BlockBulletsData2D::set_monitorable(bool new_monitorable) {
    monitorable = new_monitorable;
}

Ref<Resource> BlockBulletsData2D::get_bullets_custom_data() const{
    return bullets_custom_data;
}
void BlockBulletsData2D::set_bullets_custom_data(const Ref<Resource>& new_bullets_custom_data){
    bullets_custom_data=new_bullets_custom_data;
}

float BlockBulletsData2D::get_max_life_time() const{
    return max_life_time;
}
void BlockBulletsData2D::set_max_life_time(float new_max_life_time){
    max_life_time=new_max_life_time;
}

Ref<Material> BlockBulletsData2D::get_material() const{
    return material;
}
void BlockBulletsData2D::set_material(const Ref<Material>& new_material){
    material=new_material;
}

Ref<Mesh> BlockBulletsData2D::get_mesh() const{
    return mesh;
}
void BlockBulletsData2D::set_mesh(const Ref<Mesh>& new_mesh){
    mesh=new_mesh;
}

TypedArray<BulletRotationData> BlockBulletsData2D::get_all_bullet_rotation_data(){
    return all_bullet_rotation_data;
}
void BlockBulletsData2D::set_all_bullet_rotation_data(const TypedArray<BulletRotationData>& new_bullet_rotation_data){
    
    all_bullet_rotation_data.resize(new_bullet_rotation_data.size());
    for (int i = 0; i < new_bullet_rotation_data.size(); i++)
    {
        all_bullet_rotation_data[i] = new_bullet_rotation_data[i];
    }
}

bool BlockBulletsData2D::get_rotate_only_textures(){
    return rotate_only_textures;
}
void BlockBulletsData2D::set_rotate_only_textures(bool new_rotate_only_textures){
    rotate_only_textures=new_rotate_only_textures;
}

bool BlockBulletsData2D::get_is_texture_rotation_permanent(){
    return is_texture_rotation_permanent;
}
void BlockBulletsData2D::set_is_texture_rotation_permanent(bool new_is_texture_rotation_permanent){
    is_texture_rotation_permanent=new_is_texture_rotation_permanent;
}

void BlockBulletsData2D::_bind_methods() {
ClassDB::bind_method(D_METHOD("get_textures"), &BlockBulletsData2D::get_textures);
ClassDB::bind_method(D_METHOD("set_textures", "new_textures"), &BlockBulletsData2D::set_textures);
ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "textures"), "set_textures", "get_textures");

ClassDB::bind_method(D_METHOD("get_texture_size"), &BlockBulletsData2D::get_texture_size);
ClassDB::bind_method(D_METHOD("set_texture_size", "new_texture_size"), &BlockBulletsData2D::set_texture_size);
ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "texture_size"), "set_texture_size", "get_texture_size");

ClassDB::bind_method(D_METHOD("get_texture_rotation_radians"), &BlockBulletsData2D::get_texture_rotation_radians);
ClassDB::bind_method(D_METHOD("set_texture_rotation_radians", "new_texture_rotation_radians"), &BlockBulletsData2D::set_texture_rotation_radians);
ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "texture_rotation_radians"), "set_texture_rotation_radians", "get_texture_rotation_radians");

ClassDB::bind_method(D_METHOD("get_current_texture_index"), &BlockBulletsData2D::get_current_texture_index);
ClassDB::bind_method(D_METHOD("set_current_texture_index", "new_current_texture_index"), &BlockBulletsData2D::set_current_texture_index);
ADD_PROPERTY(PropertyInfo(Variant::INT, "current_texture_index"), "set_current_texture_index", "get_current_texture_index");

ClassDB::bind_method(D_METHOD("get_transforms"), &BlockBulletsData2D::get_transforms);
ClassDB::bind_method(D_METHOD("set_transforms", "new_transforms"), &BlockBulletsData2D::set_transforms);
ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "transforms"), "set_transforms", "get_transforms");

ClassDB::bind_method(D_METHOD("get_block_rotation_radians"), &BlockBulletsData2D::get_block_rotation_radians);
ClassDB::bind_method(D_METHOD("set_block_rotation_radians", "new_block_rotation_radians"), &BlockBulletsData2D::set_block_rotation_radians);
ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "block_rotation_radians"), "set_block_rotation_radians", "get_block_rotation_radians");

ClassDB::bind_method(D_METHOD("get_speed"), &BlockBulletsData2D::get_speed);
ClassDB::bind_method(D_METHOD("set_speed", "new_speed"), &BlockBulletsData2D::set_speed);
ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed"), "set_speed", "get_speed");

ClassDB::bind_method(D_METHOD("get_max_speed"), &BlockBulletsData2D::get_max_speed);
ClassDB::bind_method(D_METHOD("set_max_speed", "new_max_speed"), &BlockBulletsData2D::set_max_speed);
ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_speed"), "set_max_speed", "get_max_speed");

ClassDB::bind_method(D_METHOD("get_acceleration"), &BlockBulletsData2D::get_acceleration);
ClassDB::bind_method(D_METHOD("set_acceleration", "new_acceleration"), &BlockBulletsData2D::set_acceleration);
ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "acceleration"), "set_acceleration", "get_acceleration");

ClassDB::bind_method(D_METHOD("get_max_acceleration_time"), &BlockBulletsData2D::get_max_acceleration_time);
ClassDB::bind_method(D_METHOD("set_max_acceleration_time", "new_max_acceleration_time"), &BlockBulletsData2D::set_max_acceleration_time);
ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_acceleration_time"), "set_max_acceleration_time", "get_max_acceleration_time");

ClassDB::bind_method(D_METHOD("get_max_change_texture_time"), &BlockBulletsData2D::get_max_change_texture_time);
ClassDB::bind_method(D_METHOD("set_max_change_texture_time", "new_max_change_texture_time"), &BlockBulletsData2D::set_max_change_texture_time);
ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_change_texture_time"), "set_max_change_texture_time", "get_max_change_texture_time");

ClassDB::bind_method(D_METHOD("get_collision_layer"), &BlockBulletsData2D::get_collision_layer);
ClassDB::bind_method(D_METHOD("set_collision_layer", "new_collision_layer"), &BlockBulletsData2D::set_collision_layer);
ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer"), "set_collision_layer", "get_collision_layer");

ClassDB::bind_method(D_METHOD("get_collision_mask"), &BlockBulletsData2D::get_collision_mask);
ClassDB::bind_method(D_METHOD("set_collision_mask", "new_collision_mask"), &BlockBulletsData2D::set_collision_mask);
ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask"), "set_collision_mask", "get_collision_mask");

ClassDB::bind_method(D_METHOD("get_collision_shape_size"), &BlockBulletsData2D::get_collision_shape_size);
ClassDB::bind_method(D_METHOD("set_collision_shape_size", "new_collision_shape_size"), &BlockBulletsData2D::set_collision_shape_size);
ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "collision_shape_size"), "set_collision_shape_size", "get_collision_shape_size");

ClassDB::bind_method(D_METHOD("get_collision_shape_offset"), &BlockBulletsData2D::get_collision_shape_offset);
ClassDB::bind_method(D_METHOD("set_collision_shape_offset", "new_collision_shape_offset"), &BlockBulletsData2D::set_collision_shape_offset);
ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "collision_shape_offset"), "set_collision_shape_offset", "get_collision_shape_offset");

ClassDB::bind_method(D_METHOD("get_monitorable"), &BlockBulletsData2D::get_monitorable);
ClassDB::bind_method(D_METHOD("set_monitorable", "new_monitorable"), &BlockBulletsData2D::set_monitorable);
ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitorable"), "set_monitorable", "get_monitorable");

ClassDB::bind_method(D_METHOD("get_bullets_custom_data"), &BlockBulletsData2D::get_bullets_custom_data);
ClassDB::bind_method(D_METHOD("set_bullets_custom_data", "new_bullets_custom_data"), &BlockBulletsData2D::set_bullets_custom_data);
ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bullets_custom_data"), "set_bullets_custom_data", "get_bullets_custom_data");

ClassDB::bind_method(D_METHOD("get_max_life_time"), &BlockBulletsData2D::get_max_life_time);
ClassDB::bind_method(D_METHOD("set_max_life_time", "new_max_life_time"), &BlockBulletsData2D::set_max_life_time);
ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_life_time"), "set_max_life_time", "get_max_life_time");

ClassDB::bind_method(D_METHOD("get_material"), &BlockBulletsData2D::get_material);
ClassDB::bind_method(D_METHOD("set_material", "new_material"), &BlockBulletsData2D::set_material);
ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material"), "set_material", "get_material");

ClassDB::bind_method(D_METHOD("get_mesh"), &BlockBulletsData2D::get_mesh);
ClassDB::bind_method(D_METHOD("set_mesh", "new_mesh"), &BlockBulletsData2D::set_mesh);
ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh"), "set_mesh", "get_mesh");

ClassDB::bind_method(D_METHOD("get_all_bullet_rotation_data"), &BlockBulletsData2D::get_all_bullet_rotation_data);
ClassDB::bind_method(D_METHOD("set_all_bullet_rotation_data", "new_data"), &BlockBulletsData2D::set_all_bullet_rotation_data);
ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_rotation_data"), "set_all_bullet_rotation_data", "get_all_bullet_rotation_data");

ClassDB::bind_method(D_METHOD("get_rotate_only_textures"), &BlockBulletsData2D::get_rotate_only_textures);
ClassDB::bind_method(D_METHOD("set_rotate_only_textures", "new_rotate_only_textures"), &BlockBulletsData2D::set_rotate_only_textures);
ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rotate_only_textures"), "set_rotate_only_textures", "get_rotate_only_textures");

ClassDB::bind_method(D_METHOD("get_is_texture_rotation_permanent"), &BlockBulletsData2D::get_is_texture_rotation_permanent);
ClassDB::bind_method(D_METHOD("set_is_texture_rotation_permanent", "new_is_texture_rotation_permanent"), &BlockBulletsData2D::set_is_texture_rotation_permanent);
ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_texture_rotation_permanent"), "set_is_texture_rotation_permanent", "get_is_texture_rotation_permanent");

}