#include "./save_data_directional_bullets2d.hpp"

namespace BlastBullets2D {

bool SaveDataDirectionalBullets2D::get_adjust_direction_based_on_rotation() const {
	return adjust_direction_based_on_rotation;
}

void SaveDataDirectionalBullets2D::set_adjust_direction_based_on_rotation(bool new_adjust_direction_based_on_rotation) {
	adjust_direction_based_on_rotation = new_adjust_direction_based_on_rotation;
}

bool SaveDataDirectionalBullets2D::get_is_multimesh_auto_pooling_enabled() const {
	return is_multimesh_auto_pooling_enabled;
}
void SaveDataDirectionalBullets2D::set_is_multimesh_auto_pooling_enabled(bool value) {
	is_multimesh_auto_pooling_enabled = value;
}

void SaveDataDirectionalBullets2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_adjust_direction_based_on_rotation"), &SaveDataDirectionalBullets2D::get_adjust_direction_based_on_rotation);
	ClassDB::bind_method(D_METHOD("set_adjust_direction_based_on_rotation", "new_adjust_direction_based_on_rotation"), &SaveDataDirectionalBullets2D::set_adjust_direction_based_on_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "adjust_direction_based_on_rotation"), "set_adjust_direction_based_on_rotation", "get_adjust_direction_based_on_rotation");

	ClassDB::bind_method(D_METHOD("get_is_multimesh_auto_pooling_enabled"), &SaveDataDirectionalBullets2D::get_is_multimesh_auto_pooling_enabled);
	ClassDB::bind_method(D_METHOD("set_is_multimesh_auto_pooling_enabled", "value"), &SaveDataDirectionalBullets2D::set_is_multimesh_auto_pooling_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_multimesh_auto_pooling_enabled"), "set_is_multimesh_auto_pooling_enabled", "get_is_multimesh_auto_pooling_enabled");
}

} //namespace BlastBullets2D
