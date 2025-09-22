#include "save_data_bullet_factory2d.hpp"

using namespace godot;

namespace BlastBullets2D {

void SaveDataBulletFactory2D::set_all_block_bullets(const TypedArray<SaveDataBlockBullets2D> &new_all_block_bullets) {
	all_block_bullets = new_all_block_bullets;
}

TypedArray<SaveDataBlockBullets2D> SaveDataBulletFactory2D::get_all_block_bullets() const {
	return all_block_bullets;
}

void SaveDataBulletFactory2D::set_all_directional_bullets(const TypedArray<SaveDataDirectionalBullets2D> &new_all_directional_bullets) {
	all_directional_bullets = new_all_directional_bullets;
}
TypedArray<SaveDataDirectionalBullets2D> SaveDataBulletFactory2D::get_all_directional_bullets() const {
	return all_directional_bullets;
}

void SaveDataBulletFactory2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_all_block_bullets"), &SaveDataBulletFactory2D::set_all_block_bullets);
	ClassDB::bind_method(D_METHOD("get_all_block_bullets"), &SaveDataBulletFactory2D::get_all_block_bullets);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_block_bullets", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_all_block_bullets", "get_all_block_bullets");

	ClassDB::bind_method(D_METHOD("set_all_directional_bullets"), &SaveDataBulletFactory2D::set_all_directional_bullets);
	ClassDB::bind_method(D_METHOD("get_all_directional_bullets"), &SaveDataBulletFactory2D::get_all_directional_bullets);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_directional_bullets", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_all_directional_bullets", "get_all_directional_bullets");
}
} //namespace BlastBullets2D
