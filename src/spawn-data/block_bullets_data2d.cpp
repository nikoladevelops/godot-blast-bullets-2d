#include "block_bullets_data2d.hpp"

using namespace godot;

namespace BlastBullets2D {

real_t BlockBulletsData2D::get_block_rotation_radians() const {
	return block_rotation_radians;
}
void BlockBulletsData2D::set_block_rotation_radians(real_t new_block_rotation_radians) {
	block_rotation_radians = new_block_rotation_radians;
}

Ref<BulletSpeedData2D> BlockBulletsData2D::get_block_speed() const {
	return block_speed;
}
void BlockBulletsData2D::set_block_speed(const Ref<BulletSpeedData2D> &new_block_speed) {
	block_speed = new_block_speed;
}

void BlockBulletsData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_block_rotation_radians"), &BlockBulletsData2D::get_block_rotation_radians);
	ClassDB::bind_method(D_METHOD("set_block_rotation_radians", "new_block_rotation_radians"), &BlockBulletsData2D::set_block_rotation_radians);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "block_rotation_radians"), "set_block_rotation_radians", "get_block_rotation_radians");

	ClassDB::bind_method(D_METHOD("get_block_speed"), &BlockBulletsData2D::get_block_speed);
	ClassDB::bind_method(D_METHOD("set_block_speed", "new_block_speed"), &BlockBulletsData2D::set_block_speed);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "block_speed"), "set_block_speed", "get_block_speed");
}
} //namespace BlastBullets2D