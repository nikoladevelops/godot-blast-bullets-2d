#include "bullet_spawner_data2d.hpp"
#include "godot_cpp/core/class_db.hpp"

using namespace godot;

namespace BlastBullets2D {

Ref<DirectionalBulletsData2D> BulletSpawnerData2D::get_data() const {
    return data;
}

void BulletSpawnerData2D::set_data(const Ref<DirectionalBulletsData2D> &new_data) {
    data = new_data;
}

Ref<SpriteFrames> BulletSpawnerData2D::get_sprite_frames() const {
    return sprite_frames;
}

void BulletSpawnerData2D::set_sprite_frames(const Ref<SpriteFrames> &new_sprite_frames) {
    sprite_frames = new_sprite_frames;
}

StringName BulletSpawnerData2D::get_animation() const {
    return animation;
}

void BulletSpawnerData2D::set_animation(const StringName &new_animation) {
    animation = new_animation;
}

Ref<BulletCurvesData2D> BulletSpawnerData2D::get_speed_curves() const {
    return speed_curves;
}

void BulletSpawnerData2D::set_speed_curves(const Ref<BulletCurvesData2D> &new_speed_curves) {
    speed_curves = new_speed_curves;
}

int BulletSpawnerData2D::get_collision_layer() const {
    return collision_layer;
}

void BulletSpawnerData2D::set_collision_layer(int new_collision_layer) {
    collision_layer = new_collision_layer;
}

int BulletSpawnerData2D::get_collision_mask() const {
    return collision_mask;
}

void BulletSpawnerData2D::set_collision_mask(int new_collision_mask) {
    collision_mask = new_collision_mask;
}

Ref<Shape2D> BulletSpawnerData2D::get_collision_shape() const {
    return collision_shape;
}

void BulletSpawnerData2D::set_collision_shape(const Ref<Shape2D> &new_shape) {
    collision_shape = new_shape;
}

Vector2 BulletSpawnerData2D::get_collision_shape_offset() const {
    return collision_shape_offset;
}

void BulletSpawnerData2D::set_collision_shape_offset(const Vector2 &new_offset) {
    collision_shape_offset = new_offset;
}

bool BulletSpawnerData2D::get_movement_pattern_face_movement_direction() const {
    return movement_pattern_face_movement_direction;
}

void BulletSpawnerData2D::set_movement_pattern_face_movement_direction(bool value) {
    movement_pattern_face_movement_direction = value;
}

bool BulletSpawnerData2D::get_movement_pattern_repeat() const {
    return movement_pattern_repeat;
}

void BulletSpawnerData2D::set_movement_pattern_repeat(bool value) {
    movement_pattern_repeat = value;
}

int BulletSpawnerData2D::get_light_mask() const {
    return light_mask;
}

void BulletSpawnerData2D::set_light_mask(int new_light_mask) {
    light_mask = new_light_mask;
}

int BulletSpawnerData2D::get_visibility_layer() const {
    return visibility_layer;
}

void BulletSpawnerData2D::set_visibility_layer(int new_visibility_layer) {
    visibility_layer = new_visibility_layer;
}

Ref<PackedScene> BulletSpawnerData2D::get_bullet_attachment() const {
    return bullet_attachment;
}

void BulletSpawnerData2D::set_bullet_attachment(const Ref<PackedScene> &new_attachment) {
    bullet_attachment = new_attachment;
}

Vector2 BulletSpawnerData2D::get_bullet_attachment_offset() const {
    return bullet_attachment_offset;
}

void BulletSpawnerData2D::set_bullet_attachment_offset(const Vector2 &new_offset) {
    bullet_attachment_offset = new_offset;
}

void BulletSpawnerData2D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_data"), &BulletSpawnerData2D::get_data);
    ClassDB::bind_method(D_METHOD("set_data", "new_data"), &BulletSpawnerData2D::set_data);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "data", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBulletsData2D"), "set_data", "get_data");

    ClassDB::bind_method(D_METHOD("get_sprite_frames"), &BulletSpawnerData2D::get_sprite_frames);
    ClassDB::bind_method(D_METHOD("set_sprite_frames", "new_sprite_frames"), &BulletSpawnerData2D::set_sprite_frames);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sprite_frames", PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames"), "set_sprite_frames", "get_sprite_frames");

    ClassDB::bind_method(D_METHOD("get_animation"), &BulletSpawnerData2D::get_animation);
    ClassDB::bind_method(D_METHOD("set_animation", "new_animation"), &BulletSpawnerData2D::set_animation);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "animation"), "set_animation", "get_animation");

    ClassDB::bind_method(D_METHOD("get_speed_curves"), &BulletSpawnerData2D::get_speed_curves);
    ClassDB::bind_method(D_METHOD("set_speed_curves", "new_speed_curves"), &BulletSpawnerData2D::set_speed_curves);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "speed_curves", PROPERTY_HINT_RESOURCE_TYPE, "BulletCurvesData2D"), "set_speed_curves", "get_speed_curves");

    ClassDB::bind_method(D_METHOD("get_collision_layer"), &BulletSpawnerData2D::get_collision_layer);
    ClassDB::bind_method(D_METHOD("set_collision_layer", "new_collision_layer"), &BulletSpawnerData2D::set_collision_layer);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_2D_PHYSICS), "set_collision_layer", "get_collision_layer");

    ClassDB::bind_method(D_METHOD("get_collision_mask"), &BulletSpawnerData2D::get_collision_mask);
    ClassDB::bind_method(D_METHOD("set_collision_mask", "new_collision_mask"), &BulletSpawnerData2D::set_collision_mask);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_2D_PHYSICS), "set_collision_mask", "get_collision_mask");

    ClassDB::bind_method(D_METHOD("get_collision_shape"), &BulletSpawnerData2D::get_collision_shape);
    ClassDB::bind_method(D_METHOD("set_collision_shape", "new_shape"), &BulletSpawnerData2D::set_collision_shape);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "collision_shape", PROPERTY_HINT_RESOURCE_TYPE, "Shape2D"), "set_collision_shape", "get_collision_shape");

    ClassDB::bind_method(D_METHOD("get_collision_shape_offset"), &BulletSpawnerData2D::get_collision_shape_offset);
    ClassDB::bind_method(D_METHOD("set_collision_shape_offset", "new_offset"), &BulletSpawnerData2D::set_collision_shape_offset);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "collision_shape_offset"), "set_collision_shape_offset", "get_collision_shape_offset");

    ClassDB::bind_method(D_METHOD("get_movement_pattern_face_movement_direction"), &BulletSpawnerData2D::get_movement_pattern_face_movement_direction);
    ClassDB::bind_method(D_METHOD("set_movement_pattern_face_movement_direction", "value"), &BulletSpawnerData2D::set_movement_pattern_face_movement_direction);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "movement_pattern_face_movement_direction"), "set_movement_pattern_face_movement_direction", "get_movement_pattern_face_movement_direction");

    ClassDB::bind_method(D_METHOD("get_movement_pattern_repeat"), &BulletSpawnerData2D::get_movement_pattern_repeat);
    ClassDB::bind_method(D_METHOD("set_movement_pattern_repeat", "value"), &BulletSpawnerData2D::set_movement_pattern_repeat);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "movement_pattern_repeat"), "set_movement_pattern_repeat", "get_movement_pattern_repeat");

    ClassDB::bind_method(D_METHOD("get_light_mask"), &BulletSpawnerData2D::get_light_mask);
    ClassDB::bind_method(D_METHOD("set_light_mask", "new_light_mask"), &BulletSpawnerData2D::set_light_mask);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "light_mask", PROPERTY_HINT_LAYERS_2D_RENDER), "set_light_mask", "get_light_mask");

    ClassDB::bind_method(D_METHOD("get_visibility_layer"), &BulletSpawnerData2D::get_visibility_layer);
    ClassDB::bind_method(D_METHOD("set_visibility_layer", "new_visibility_layer"), &BulletSpawnerData2D::set_visibility_layer);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "visibility_layer", PROPERTY_HINT_LAYERS_2D_RENDER), "set_visibility_layer", "get_visibility_layer");

    ClassDB::bind_method(D_METHOD("get_bullet_attachment"), &BulletSpawnerData2D::get_bullet_attachment);
    ClassDB::bind_method(D_METHOD("set_bullet_attachment", "new_attachment"), &BulletSpawnerData2D::set_bullet_attachment);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bullet_attachment", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_bullet_attachment", "get_bullet_attachment");

    ClassDB::bind_method(D_METHOD("get_bullet_attachment_offset"), &BulletSpawnerData2D::get_bullet_attachment_offset);
    ClassDB::bind_method(D_METHOD("set_bullet_attachment_offset", "new_offset"), &BulletSpawnerData2D::set_bullet_attachment_offset);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "bullet_attachment_offset"), "set_bullet_attachment_offset", "get_bullet_attachment_offset");
}

} //namespace BlastBullets2D
