#include "./save_data_block_bullets2d.hpp"

using namespace godot;

namespace BlastBullets {

void SaveDataBlockBullets2D::set_block_rotation_radians(float new_block_rotation_radians) {
    block_rotation_radians = new_block_rotation_radians;
}

float SaveDataBlockBullets2D::get_block_rotation_radians() const {
    return block_rotation_radians;
}

Vector2 SaveDataBlockBullets2D::get_multi_mesh_position() const {
    return multi_mesh_position;
}
void SaveDataBlockBullets2D::set_multi_mesh_position(Vector2 new_position) {
    multi_mesh_position = new_position;
}

void SaveDataBlockBullets2D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_block_rotation_radians"), &SaveDataBlockBullets2D::set_block_rotation_radians);
    ClassDB::bind_method(D_METHOD("get_block_rotation_radians"), &SaveDataBlockBullets2D::get_block_rotation_radians);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "block_rotation_radians"), "set_block_rotation_radians", "get_block_rotation_radians");

    ClassDB::bind_method(D_METHOD("get_multi_mesh_position"), &SaveDataBlockBullets2D::get_multi_mesh_position);
    ClassDB::bind_method(D_METHOD("set_multi_mesh_position"), &SaveDataBlockBullets2D::set_multi_mesh_position);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "multi_mesh_position"), "set_multi_mesh_position", "get_multi_mesh_position");
}
}