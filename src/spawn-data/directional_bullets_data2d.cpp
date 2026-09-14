#include "./directional_bullets_data2d.hpp"
#include "godot_cpp/core/class_db.hpp"
#include <godot_cpp/variant/utility_functions.hpp>

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

TypedArray<NodePath> DirectionalBulletsData2D::get_all_bullet_movement_pattern_paths() const {
	return all_bullet_movement_pattern_paths;
}
void DirectionalBulletsData2D::set_all_bullet_movement_pattern_paths(const TypedArray<NodePath> &new_paths) {
	all_bullet_movement_pattern_paths = new_paths;
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

double DirectionalBulletsData2D::get_homing_smoothing() const {
	return homing_smoothing;
}
void DirectionalBulletsData2D::set_homing_smoothing(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("DirectionalBulletsData2D: homing_smoothing must be finite and >= 0 (0 snaps instantly), keeping the old value.");
		return;
	}
	homing_smoothing = value;
}

double DirectionalBulletsData2D::get_homing_update_interval() const {
	return homing_update_interval;
}
void DirectionalBulletsData2D::set_homing_update_interval(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("DirectionalBulletsData2D: homing_update_interval must be finite and >= 0 (0 refreshes every tick), keeping the old value.");
		return;
	}
	homing_update_interval = value;
}

double DirectionalBulletsData2D::get_homing_distance_before_reached() const {
	return homing_distance_before_reached;
}
void DirectionalBulletsData2D::set_homing_distance_before_reached(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("DirectionalBulletsData2D: homing_distance_before_reached must be finite and >= 0, keeping the old value.");
		return;
	}
	homing_distance_before_reached = value;
}

bool DirectionalBulletsData2D::get_homing_take_control_of_texture_rotation() const {
	return homing_take_control_of_texture_rotation;
}
void DirectionalBulletsData2D::set_homing_take_control_of_texture_rotation(bool value) {
	homing_take_control_of_texture_rotation = value;
}

bool DirectionalBulletsData2D::get_bullet_homing_auto_pop_after_target_reached() const {
	return bullet_homing_auto_pop_after_target_reached;
}
void DirectionalBulletsData2D::set_bullet_homing_auto_pop_after_target_reached(bool value) {
	bullet_homing_auto_pop_after_target_reached = value;
}

bool DirectionalBulletsData2D::get_shared_homing_deque_auto_pop_after_target_reached() const {
	return shared_homing_deque_auto_pop_after_target_reached;
}
void DirectionalBulletsData2D::set_shared_homing_deque_auto_pop_after_target_reached(bool value) {
	shared_homing_deque_auto_pop_after_target_reached = value;
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

	ClassDB::bind_method(D_METHOD("get_all_bullet_movement_pattern_paths"), &DirectionalBulletsData2D::get_all_bullet_movement_pattern_paths);
	ClassDB::bind_method(D_METHOD("set_all_bullet_movement_pattern_paths", "new_paths"), &DirectionalBulletsData2D::set_all_bullet_movement_pattern_paths);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_movement_pattern_paths", PROPERTY_HINT_ARRAY_TYPE, vformat("%s/%s:%s", Variant::NODE_PATH, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Path2D")), "set_all_bullet_movement_pattern_paths", "get_all_bullet_movement_pattern_paths");

	ClassDB::bind_method(D_METHOD("get_all_bullet_movement_pattern_face_movement_directions"), &DirectionalBulletsData2D::get_all_bullet_movement_pattern_face_movement_directions);
	ClassDB::bind_method(D_METHOD("set_all_bullet_movement_pattern_face_movement_directions", "new_flags"), &DirectionalBulletsData2D::set_all_bullet_movement_pattern_face_movement_directions);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_movement_pattern_face_movement_directions", PROPERTY_HINT_ARRAY_TYPE, "bool"), "set_all_bullet_movement_pattern_face_movement_directions", "get_all_bullet_movement_pattern_face_movement_directions");

	ClassDB::bind_method(D_METHOD("get_all_bullet_movement_pattern_repeats"), &DirectionalBulletsData2D::get_all_bullet_movement_pattern_repeats);
	ClassDB::bind_method(D_METHOD("set_all_bullet_movement_pattern_repeats", "new_flags"), &DirectionalBulletsData2D::set_all_bullet_movement_pattern_repeats);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_movement_pattern_repeats", PROPERTY_HINT_ARRAY_TYPE, "bool"), "set_all_bullet_movement_pattern_repeats", "get_all_bullet_movement_pattern_repeats");

	ClassDB::bind_method(D_METHOD("get_homing_smoothing"), &DirectionalBulletsData2D::get_homing_smoothing);
	ClassDB::bind_method(D_METHOD("set_homing_smoothing", "value"), &DirectionalBulletsData2D::set_homing_smoothing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing"), "set_homing_smoothing", "get_homing_smoothing");

	ClassDB::bind_method(D_METHOD("get_homing_update_interval"), &DirectionalBulletsData2D::get_homing_update_interval);
	ClassDB::bind_method(D_METHOD("set_homing_update_interval", "value"), &DirectionalBulletsData2D::set_homing_update_interval);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_update_interval"), "set_homing_update_interval", "get_homing_update_interval");

	ClassDB::bind_method(D_METHOD("get_homing_distance_before_reached"), &DirectionalBulletsData2D::get_homing_distance_before_reached);
	ClassDB::bind_method(D_METHOD("set_homing_distance_before_reached", "value"), &DirectionalBulletsData2D::set_homing_distance_before_reached);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_distance_before_reached"), "set_homing_distance_before_reached", "get_homing_distance_before_reached");

	ClassDB::bind_method(D_METHOD("get_homing_take_control_of_texture_rotation"), &DirectionalBulletsData2D::get_homing_take_control_of_texture_rotation);
	ClassDB::bind_method(D_METHOD("set_homing_take_control_of_texture_rotation", "value"), &DirectionalBulletsData2D::set_homing_take_control_of_texture_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_take_control_of_texture_rotation"), "set_homing_take_control_of_texture_rotation", "get_homing_take_control_of_texture_rotation");

	ClassDB::bind_method(D_METHOD("get_bullet_homing_auto_pop_after_target_reached"), &DirectionalBulletsData2D::get_bullet_homing_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_bullet_homing_auto_pop_after_target_reached", "value"), &DirectionalBulletsData2D::set_bullet_homing_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bullet_homing_auto_pop_after_target_reached"), "set_bullet_homing_auto_pop_after_target_reached", "get_bullet_homing_auto_pop_after_target_reached");

	ClassDB::bind_method(D_METHOD("get_shared_homing_deque_auto_pop_after_target_reached"), &DirectionalBulletsData2D::get_shared_homing_deque_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_shared_homing_deque_auto_pop_after_target_reached", "value"), &DirectionalBulletsData2D::set_shared_homing_deque_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_homing_deque_auto_pop_after_target_reached"), "set_shared_homing_deque_auto_pop_after_target_reached", "get_shared_homing_deque_auto_pop_after_target_reached");
}
} //namespace BlastBullets2D
