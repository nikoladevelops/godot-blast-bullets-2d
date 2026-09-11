#include "./directional_bullets_data2d.hpp"
#include "godot_cpp/core/class_db.hpp"

using namespace godot;

namespace BlastBullets2D {

TypedArray<BulletSpeedData2D> DirectionalBulletsData2D::get_all_bullet_speed_data() const {
	return all_bullet_speed_data;
}
void DirectionalBulletsData2D::set_all_bullet_speed_data(const TypedArray<BulletSpeedData2D> &new_data) {
	all_bullet_speed_data = new_data;
}

bool DirectionalBulletsData2D::get_adjust_direction_based_on_rotation() const {
	return adjust_direction_based_on_rotation;
}

void DirectionalBulletsData2D::set_adjust_direction_based_on_rotation(bool new_adjust_direction_based_on_rotation) {
	adjust_direction_based_on_rotation = new_adjust_direction_based_on_rotation;
}

Ref<BulletSpeedData2D> DirectionalBulletsData2D::get_shared_bullet_speed_data() const {
	return shared_bullet_speed_data;
}
void DirectionalBulletsData2D::set_shared_bullet_speed_data(const Ref<BulletSpeedData2D> &new_speed_data) {
	shared_bullet_speed_data = new_speed_data;
}

Ref<BulletRotationData2D> DirectionalBulletsData2D::get_shared_bullet_rotation_data() const {
	return shared_bullet_rotation_data;
}
void DirectionalBulletsData2D::set_shared_bullet_rotation_data(const Ref<BulletRotationData2D> &new_rotation_data) {
	shared_bullet_rotation_data = new_rotation_data;
}

Ref<BulletCurvesData2D> DirectionalBulletsData2D::get_shared_bullet_curves_data() const {
	return shared_bullet_curves_data;
}
void DirectionalBulletsData2D::set_shared_bullet_curves_data(const Ref<BulletCurvesData2D> &new_curves_data) {
	shared_bullet_curves_data = new_curves_data;
}

NodePath DirectionalBulletsData2D::get_shared_movement_pattern_path() const {
	return shared_movement_pattern_path;
}
void DirectionalBulletsData2D::set_shared_movement_pattern_path(const NodePath &new_path) {
	shared_movement_pattern_path = new_path;
}

bool DirectionalBulletsData2D::get_shared_movement_pattern_face_movement_direction() const {
	return shared_movement_pattern_face_movement_direction;
}
void DirectionalBulletsData2D::set_shared_movement_pattern_face_movement_direction(bool value) {
	shared_movement_pattern_face_movement_direction = value;
}

bool DirectionalBulletsData2D::get_shared_movement_pattern_repeat() const {
	return shared_movement_pattern_repeat;
}
void DirectionalBulletsData2D::set_shared_movement_pattern_repeat(bool value) {
	shared_movement_pattern_repeat = value;
}

TypedArray<BulletCurvesData2D> DirectionalBulletsData2D::get_all_bullet_curves_data() const {
	return all_bullet_curves_data;
}
void DirectionalBulletsData2D::set_all_bullet_curves_data(const TypedArray<BulletCurvesData2D> &new_data) {
	all_bullet_curves_data = new_data;
}

TypedArray<Curve2D> DirectionalBulletsData2D::get_all_bullet_movement_pattern_curves() const {
	return all_bullet_movement_pattern_curves;
}
void DirectionalBulletsData2D::set_all_bullet_movement_pattern_curves(const TypedArray<Curve2D> &new_curves) {
	all_bullet_movement_pattern_curves = new_curves;
}

TypedArray<bool> DirectionalBulletsData2D::get_all_bullet_movement_pattern_face_movement_directions() const {
	return all_bullet_movement_pattern_face_movement_directions;
}
void DirectionalBulletsData2D::set_all_bullet_movement_pattern_face_movement_directions(const TypedArray<bool> &new_flags) {
	all_bullet_movement_pattern_face_movement_directions = new_flags;
}

TypedArray<bool> DirectionalBulletsData2D::get_all_bullet_movement_pattern_repeats() const {
	return all_bullet_movement_pattern_repeats;
}
void DirectionalBulletsData2D::set_all_bullet_movement_pattern_repeats(const TypedArray<bool> &new_flags) {
	all_bullet_movement_pattern_repeats = new_flags;
}

void DirectionalBulletsData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_all_bullet_speed_data"), &DirectionalBulletsData2D::get_all_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("set_all_bullet_speed_data", "new_data"), &DirectionalBulletsData2D::set_all_bullet_speed_data);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_speed_data", PROPERTY_HINT_ARRAY_TYPE, "BulletSpeedData2D"), "set_all_bullet_speed_data", "get_all_bullet_speed_data");

	ClassDB::bind_method(D_METHOD("get_adjust_direction_based_on_rotation"), &DirectionalBulletsData2D::get_adjust_direction_based_on_rotation);
	ClassDB::bind_method(D_METHOD("set_adjust_direction_based_on_rotation", "new_adjust_direction_based_on_rotation"), &DirectionalBulletsData2D::set_adjust_direction_based_on_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "adjust_direction_based_on_rotation"), "set_adjust_direction_based_on_rotation", "get_adjust_direction_based_on_rotation");

	ClassDB::bind_method(D_METHOD("get_shared_bullet_speed_data"), &DirectionalBulletsData2D::get_shared_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_speed_data", "new_speed_data"), &DirectionalBulletsData2D::set_shared_bullet_speed_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_speed_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletSpeedData2D"), "set_shared_bullet_speed_data", "get_shared_bullet_speed_data");

	ClassDB::bind_method(D_METHOD("get_shared_bullet_rotation_data"), &DirectionalBulletsData2D::get_shared_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_rotation_data", "new_rotation_data"), &DirectionalBulletsData2D::set_shared_bullet_rotation_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_rotation_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletRotationData2D"), "set_shared_bullet_rotation_data", "get_shared_bullet_rotation_data");

	ClassDB::bind_method(D_METHOD("get_shared_bullet_curves_data"), &DirectionalBulletsData2D::get_shared_bullet_curves_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_curves_data", "new_curves_data"), &DirectionalBulletsData2D::set_shared_bullet_curves_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_curves_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletCurvesData2D"), "set_shared_bullet_curves_data", "get_shared_bullet_curves_data");

	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_path"), &DirectionalBulletsData2D::get_shared_movement_pattern_path);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_path", "new_path"), &DirectionalBulletsData2D::set_shared_movement_pattern_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "shared_movement_pattern_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Path2D"), "set_shared_movement_pattern_path", "get_shared_movement_pattern_path");

	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_face_movement_direction"), &DirectionalBulletsData2D::get_shared_movement_pattern_face_movement_direction);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_face_movement_direction", "value"), &DirectionalBulletsData2D::set_shared_movement_pattern_face_movement_direction);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_movement_pattern_face_movement_direction"), "set_shared_movement_pattern_face_movement_direction", "get_shared_movement_pattern_face_movement_direction");

	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_repeat"), &DirectionalBulletsData2D::get_shared_movement_pattern_repeat);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_repeat", "value"), &DirectionalBulletsData2D::set_shared_movement_pattern_repeat);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_movement_pattern_repeat"), "set_shared_movement_pattern_repeat", "get_shared_movement_pattern_repeat");

	ClassDB::bind_method(D_METHOD("get_all_bullet_curves_data"), &DirectionalBulletsData2D::get_all_bullet_curves_data);
	ClassDB::bind_method(D_METHOD("set_all_bullet_curves_data", "new_data"), &DirectionalBulletsData2D::set_all_bullet_curves_data);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_curves_data", PROPERTY_HINT_ARRAY_TYPE, "BulletCurvesData2D"), "set_all_bullet_curves_data", "get_all_bullet_curves_data");

	ClassDB::bind_method(D_METHOD("get_all_bullet_movement_pattern_curves"), &DirectionalBulletsData2D::get_all_bullet_movement_pattern_curves);
	ClassDB::bind_method(D_METHOD("set_all_bullet_movement_pattern_curves", "new_curves"), &DirectionalBulletsData2D::set_all_bullet_movement_pattern_curves);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_movement_pattern_curves", PROPERTY_HINT_ARRAY_TYPE, "Curve2D"), "set_all_bullet_movement_pattern_curves", "get_all_bullet_movement_pattern_curves");

	ClassDB::bind_method(D_METHOD("get_all_bullet_movement_pattern_face_movement_directions"), &DirectionalBulletsData2D::get_all_bullet_movement_pattern_face_movement_directions);
	ClassDB::bind_method(D_METHOD("set_all_bullet_movement_pattern_face_movement_directions", "new_flags"), &DirectionalBulletsData2D::set_all_bullet_movement_pattern_face_movement_directions);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_movement_pattern_face_movement_directions", PROPERTY_HINT_ARRAY_TYPE, "bool"), "set_all_bullet_movement_pattern_face_movement_directions", "get_all_bullet_movement_pattern_face_movement_directions");

	ClassDB::bind_method(D_METHOD("get_all_bullet_movement_pattern_repeats"), &DirectionalBulletsData2D::get_all_bullet_movement_pattern_repeats);
	ClassDB::bind_method(D_METHOD("set_all_bullet_movement_pattern_repeats", "new_flags"), &DirectionalBulletsData2D::set_all_bullet_movement_pattern_repeats);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_movement_pattern_repeats", PROPERTY_HINT_ARRAY_TYPE, "bool"), "set_all_bullet_movement_pattern_repeats", "get_all_bullet_movement_pattern_repeats");
}
} //namespace BlastBullets2D
