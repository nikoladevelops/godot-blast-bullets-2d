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

Ref<Curve> BulletCurvesData2D::get_direction_curve() const {
	return direction_curve;
}
void BulletCurvesData2D::set_direction_curve(const Ref<Curve> &curve) {
	if (curve.is_valid()) {
		curve->bake();
	}

	direction_curve = curve;
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

void BulletCurvesData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_movement_speed_curve"), &BulletCurvesData2D::get_movement_speed_curve);
	ClassDB::bind_method(D_METHOD("set_movement_speed_curve", "curve"), &BulletCurvesData2D::set_movement_speed_curve);
	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "movement_speed_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"),
			"set_movement_speed_curve", "get_movement_speed_curve");

	ClassDB::bind_method(D_METHOD("get_movement_use_unit_curve"), &BulletCurvesData2D::get_movement_use_unit_curve);
	ClassDB::bind_method(D_METHOD("set_movement_use_unit_curve", "value"), &BulletCurvesData2D::set_movement_use_unit_curve);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "movement_use_unit_curve"), "set_movement_use_unit_curve", "get_movement_use_unit_curve");

	ClassDB::bind_method(D_METHOD("get_rotation_speed_curve"), &BulletCurvesData2D::get_rotation_speed_curve);
	ClassDB::bind_method(D_METHOD("set_rotation_speed_curve", "curve"), &BulletCurvesData2D::set_rotation_speed_curve);
	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "rotation_speed_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"),
			"set_rotation_speed_curve", "get_rotation_speed_curve");

	ClassDB::bind_method(D_METHOD("get_rotation_use_unit_curve"), &BulletCurvesData2D::get_rotation_use_unit_curve);
	ClassDB::bind_method(D_METHOD("set_rotation_use_unit_curve", "value"), &BulletCurvesData2D::set_rotation_use_unit_curve);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rotation_use_unit_curve"), "set_rotation_use_unit_curve", "get_rotation_use_unit_curve");

	ClassDB::bind_method(D_METHOD("get_direction_curve"), &BulletCurvesData2D::get_direction_curve);
	ClassDB::bind_method(D_METHOD("set_direction_curve", "curve"), &BulletCurvesData2D::set_direction_curve);
	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "direction_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"),
			"set_direction_curve", "get_direction_curve");
}
} //namespace BlastBullets2D
