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

bool DirectionalBulletsData2D::get_is_multimesh_auto_pooling_enabled() const {
    return is_multimesh_auto_pooling_enabled;
}
void DirectionalBulletsData2D::set_is_multimesh_auto_pooling_enabled(bool value) {
    is_multimesh_auto_pooling_enabled = value;
}

void DirectionalBulletsData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_all_bullet_speed_data"), &DirectionalBulletsData2D::get_all_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("set_all_bullet_speed_data", "new_data"), &DirectionalBulletsData2D::set_all_bullet_speed_data);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_speed_data"), "set_all_bullet_speed_data", "get_all_bullet_speed_data");

	ClassDB::bind_method(D_METHOD("get_adjust_direction_based_on_rotation"), &DirectionalBulletsData2D::get_adjust_direction_based_on_rotation);
	ClassDB::bind_method(D_METHOD("set_adjust_direction_based_on_rotation", "new_adjust_direction_based_on_rotation"), &DirectionalBulletsData2D::set_adjust_direction_based_on_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "adjust_direction_based_on_rotation"), "set_adjust_direction_based_on_rotation", "get_adjust_direction_based_on_rotation");

	ClassDB::bind_method(D_METHOD("get_is_multimesh_auto_pooling_enabled"), &DirectionalBulletsData2D::get_is_multimesh_auto_pooling_enabled);
	ClassDB::bind_method(D_METHOD("set_is_multimesh_auto_pooling_enabled", "value"), &DirectionalBulletsData2D::set_is_multimesh_auto_pooling_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_multimesh_auto_pooling_enabled"), "set_is_multimesh_auto_pooling_enabled", "get_is_multimesh_auto_pooling_enabled");
}
} //namespace BlastBullets2D
