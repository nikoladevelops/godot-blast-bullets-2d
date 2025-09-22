#include "./save_data_block_bullets2d.hpp"

using namespace godot;

namespace BlastBullets2D {

void SaveDataBlockBullets2D::set_block_rotation_radians(real_t new_block_rotation_radians) {
	block_rotation_radians = new_block_rotation_radians;
}

real_t SaveDataBlockBullets2D::get_block_rotation_radians() const {
	return block_rotation_radians;
}

void SaveDataBlockBullets2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_block_rotation_radians"), &SaveDataBlockBullets2D::set_block_rotation_radians);
	ClassDB::bind_method(D_METHOD("get_block_rotation_radians"), &SaveDataBlockBullets2D::get_block_rotation_radians);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "block_rotation_radians"), "set_block_rotation_radians", "get_block_rotation_radians");
}
} //namespace BlastBullets2D