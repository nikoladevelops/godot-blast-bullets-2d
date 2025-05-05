#include "./save_data_directional_bullets2d.hpp"

namespace BlastBullets2D {

bool SaveDataDirectionalBullets2D::get_adjust_direction_based_on_rotation() const {
    return adjust_direction_based_on_rotation;
}

void SaveDataDirectionalBullets2D::set_adjust_direction_based_on_rotation(bool new_adjust_direction_based_on_rotation) {
    adjust_direction_based_on_rotation = new_adjust_direction_based_on_rotation;
}

void SaveDataDirectionalBullets2D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_adjust_direction_based_on_rotation"), &SaveDataDirectionalBullets2D::get_adjust_direction_based_on_rotation);
    ClassDB::bind_method(D_METHOD("set_adjust_direction_based_on_rotation", "new_adjust_direction_based_on_rotation"), &SaveDataDirectionalBullets2D::set_adjust_direction_based_on_rotation);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "adjust_direction_based_on_rotation"), "set_adjust_direction_based_on_rotation", "get_adjust_direction_based_on_rotation");
}

}
