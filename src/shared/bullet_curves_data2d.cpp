#include "./bullet_curves_data2d.hpp"

using namespace godot;

namespace BlastBullets2D {

Ref<Curve> BulletCurvesData2D::get_movement_speed_curve() const {
	return movement_speed_curve;
}

void BulletCurvesData2D::set_movement_speed_curve(const Ref<Curve> &curve) {
	if (curve.is_valid()) {
		curve->bake();
	}

	movement_speed_curve = curve;
}

Ref<Curve> BulletCurvesData2D::get_rotation_speed_curve() const {
	return rotation_speed_curve;
}
void BulletCurvesData2D::set_rotation_speed_curve(const Ref<Curve> &curve) {
	if (curve.is_valid()) {
		curve->bake();
	}

	rotation_speed_curve = curve;
}

Ref<Curve> BulletCurvesData2D::get_x_direction_curve() const {
	return x_direction_curve;
}
void BulletCurvesData2D::set_x_direction_curve(const Ref<Curve> &curve) {
	if (curve.is_valid()) {
		curve->bake();
	}

	x_direction_curve = curve;
}

bool BulletCurvesData2D::get_movement_use_unit_curve() const {
	return movement_use_unit_curve;
}
void BulletCurvesData2D::set_movement_use_unit_curve(bool value) {
	movement_use_unit_curve = value;
}

bool BulletCurvesData2D::get_rotation_use_unit_curve() const {
	return rotation_use_unit_curve;
}
void BulletCurvesData2D::set_rotation_use_unit_curve(bool value) {
	rotation_use_unit_curve = value;
}

bool BulletCurvesData2D::get_x_direction_use_unit_curve() const {
	return x_direction_use_unit_curve;
}

void BulletCurvesData2D::set_x_direction_use_unit_curve(bool value) {
	x_direction_use_unit_curve = value;
}

real_t BulletCurvesData2D::get_x_direction_curve_strength() const {
	return x_direction_curve_strength;
}

void BulletCurvesData2D::set_x_direction_curve_strength(real_t value) {
	x_direction_curve_strength = value;
}

DirectionCurveMode BulletCurvesData2D::get_x_direction_curve_mode() const {
	return x_direction_curve_mode;
}

void BulletCurvesData2D::set_x_direction_curve_mode(DirectionCurveMode mode) {
	x_direction_curve_mode = mode;
}

bool BulletCurvesData2D::get_rotate_towards_adjusted_direction() const {
	return rotate_towards_adjusted_direction;
}
void BulletCurvesData2D::set_rotate_towards_adjusted_direction(bool value) {
	rotate_towards_adjusted_direction = value;
}

real_t BulletCurvesData2D::get_direction_curve_rotation_speed() const {
	return direction_curve_rotation_speed;
}
void BulletCurvesData2D::set_direction_curve_rotation_speed(real_t value) {
	direction_curve_rotation_speed = value;
}

Ref<Curve> BulletCurvesData2D::get_y_direction_curve() const {
	return y_direction_curve;
}
void BulletCurvesData2D::set_y_direction_curve(const Ref<Curve> &curve) {
	if (curve.is_valid()) {
		curve->bake();
	}

	y_direction_curve = curve;
}

bool BulletCurvesData2D::get_y_direction_use_unit_curve() const {
	return y_direction_use_unit_curve;
}
void BulletCurvesData2D::set_y_direction_use_unit_curve(bool value) {
	y_direction_use_unit_curve = value;
}

real_t BulletCurvesData2D::get_y_direction_curve_strength() const {
	return y_direction_curve_strength;
}
void BulletCurvesData2D::set_y_direction_curve_strength(real_t value) {
	y_direction_curve_strength = value;
}

DirectionCurveMode BulletCurvesData2D::get_y_direction_curve_mode() const {
	return y_direction_curve_mode;
}
void BulletCurvesData2D::set_y_direction_curve_mode(DirectionCurveMode mode) {
	y_direction_curve_mode = mode;
}

void BulletCurvesData2D::_bind_methods() {
	// Movement Speed Curve

	ClassDB::bind_method(D_METHOD("get_movement_speed_curve"), &BulletCurvesData2D::get_movement_speed_curve);
	ClassDB::bind_method(D_METHOD("set_movement_speed_curve", "curve"), &BulletCurvesData2D::set_movement_speed_curve);
	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "movement_speed_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"),
			"set_movement_speed_curve", "get_movement_speed_curve");

	ClassDB::bind_method(D_METHOD("get_movement_use_unit_curve"), &BulletCurvesData2D::get_movement_use_unit_curve);
	ClassDB::bind_method(D_METHOD("set_movement_use_unit_curve", "value"), &BulletCurvesData2D::set_movement_use_unit_curve);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "movement_use_unit_curve"), "set_movement_use_unit_curve", "get_movement_use_unit_curve");

	//

	// Rotation Speed Curve

	ClassDB::bind_method(D_METHOD("get_rotation_speed_curve"), &BulletCurvesData2D::get_rotation_speed_curve);
	ClassDB::bind_method(D_METHOD("set_rotation_speed_curve", "curve"), &BulletCurvesData2D::set_rotation_speed_curve);
	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "rotation_speed_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"),
			"set_rotation_speed_curve", "get_rotation_speed_curve");

	ClassDB::bind_method(D_METHOD("get_rotation_use_unit_curve"), &BulletCurvesData2D::get_rotation_use_unit_curve);
	ClassDB::bind_method(D_METHOD("set_rotation_use_unit_curve", "value"), &BulletCurvesData2D::set_rotation_use_unit_curve);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rotation_use_unit_curve"), "set_rotation_use_unit_curve", "get_rotation_use_unit_curve");

	//

	// X Direction Curve

	ClassDB::bind_method(D_METHOD("get_x_direction_curve"), &BulletCurvesData2D::get_x_direction_curve);
	ClassDB::bind_method(D_METHOD("set_x_direction_curve", "curve"), &BulletCurvesData2D::set_x_direction_curve);
	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "x_direction_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"),
			"set_x_direction_curve", "get_x_direction_curve");

	ClassDB::bind_method(D_METHOD("get_x_direction_use_unit_curve"), &BulletCurvesData2D::get_x_direction_use_unit_curve);
	ClassDB::bind_method(D_METHOD("set_x_direction_use_unit_curve", "value"), &BulletCurvesData2D::set_x_direction_use_unit_curve);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "x_direction_use_unit_curve"), "set_x_direction_use_unit_curve", "get_x_direction_use_unit_curve");

	ClassDB::bind_method(D_METHOD("get_x_direction_curve_strength"), &BulletCurvesData2D::get_x_direction_curve_strength);
	ClassDB::bind_method(D_METHOD("set_x_direction_curve_strength", "value"), &BulletCurvesData2D::set_x_direction_curve_strength);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "x_direction_curve_strength"), "set_x_direction_curve_strength", "get_x_direction_curve_strength");

	ClassDB::bind_method(D_METHOD("get_x_direction_curve_mode"), &BulletCurvesData2D::get_x_direction_curve_mode);
	ClassDB::bind_method(D_METHOD("set_x_direction_curve_mode", "mode"), &BulletCurvesData2D::set_x_direction_curve_mode);
	ADD_PROPERTY(
			PropertyInfo(Variant::INT, "x_direction_curve_mode", PROPERTY_HINT_ENUM, "Additive,Override"),
			"set_x_direction_curve_mode", "get_x_direction_curve_mode");

	//

	// Y Direction Curve

	ClassDB::bind_method(D_METHOD("get_y_direction_curve"), &BulletCurvesData2D::get_y_direction_curve);
	ClassDB::bind_method(D_METHOD("set_y_direction_curve", "curve"), &BulletCurvesData2D::set_y_direction_curve);
	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "y_direction_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"),
			"set_y_direction_curve", "get_y_direction_curve");

	ClassDB::bind_method(D_METHOD("get_y_direction_use_unit_curve"), &BulletCurvesData2D::get_y_direction_use_unit_curve);
	ClassDB::bind_method(D_METHOD("set_y_direction_use_unit_curve", "value"), &BulletCurvesData2D::set_y_direction_use_unit_curve);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "y_direction_use_unit_curve"), "set_y_direction_use_unit_curve", "get_y_direction_use_unit_curve");

	ClassDB::bind_method(D_METHOD("get_y_direction_curve_strength"), &BulletCurvesData2D::get_y_direction_curve_strength);
	ClassDB::bind_method(D_METHOD("set_y_direction_curve_strength", "value"), &BulletCurvesData2D::set_y_direction_curve_strength);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "y_direction_curve_strength"), "set_y_direction_curve_strength", "get_y_direction_curve_strength");

	ClassDB::bind_method(D_METHOD("get_y_direction_curve_mode"), &BulletCurvesData2D::get_y_direction_curve_mode);
	ClassDB::bind_method(D_METHOD("set_y_direction_curve_mode", "mode"), &BulletCurvesData2D::set_y_direction_curve_mode);
	ADD_PROPERTY(
			PropertyInfo(Variant::INT, "y_direction_curve_mode", PROPERTY_HINT_ENUM, "Additive,Override"),
			"set_y_direction_curve_mode", "get_y_direction_curve_mode");

	//

	// Direction Curve related

	ClassDB::bind_method(D_METHOD("get_rotate_towards_adjusted_direction"), &BulletCurvesData2D::get_rotate_towards_adjusted_direction);
	ClassDB::bind_method(D_METHOD("set_rotate_towards_adjusted_direction", "value"), &BulletCurvesData2D::set_rotate_towards_adjusted_direction);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rotate_towards_adjusted_direction"), "set_rotate_towards_adjusted_direction", "get_rotate_towards_adjusted_direction");

	ClassDB::bind_method(D_METHOD("get_direction_curve_rotation_speed"), &BulletCurvesData2D::get_direction_curve_rotation_speed);
	ClassDB::bind_method(D_METHOD("set_direction_curve_rotation_speed", "value"), &BulletCurvesData2D::set_direction_curve_rotation_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "direction_curve_rotation_speed"), "set_direction_curve_rotation_speed", "get_direction_curve_rotation_speed");

	//

	BIND_ENUM_CONSTANT(Additive);
	BIND_ENUM_CONSTANT(Override);
}
} //namespace BlastBullets2D
