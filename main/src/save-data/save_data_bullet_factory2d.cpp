#include "save_data_bullet_factory2d.hpp"


void SaveDataBulletFactory2D::set_all_block_bullets(const TypedArray<SaveDataBlockBullets2D> &new_all_block_bullets) {
    all_block_bullets = new_all_block_bullets;
}

// Getter implementation
TypedArray<SaveDataBlockBullets2D> SaveDataBulletFactory2D::get_all_block_bullets() const {
    return all_block_bullets;
}

void SaveDataBulletFactory2D::_bind_methods(){
    ClassDB::bind_method(D_METHOD("set_all_block_bullets"), &SaveDataBulletFactory2D::set_all_block_bullets);
    ClassDB::bind_method(D_METHOD("get_all_block_bullets"), &SaveDataBulletFactory2D::get_all_block_bullets);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_block_bullets", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_all_block_bullets", "get_all_block_bullets");
}

