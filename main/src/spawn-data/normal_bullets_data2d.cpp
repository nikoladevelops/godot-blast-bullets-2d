#include "./normal_bullets_data2d.hpp"

using namespace godot;

namespace BlastBullets {

TypedArray<BulletSpeedData2D> NormalBulletsData2D::get_all_bullet_speed_data() {
    return all_bullet_speed_data;
}
void NormalBulletsData2D::set_all_bullet_speed_data(const TypedArray<BulletSpeedData2D> &new_data) {
    all_bullet_speed_data.resize(new_data.size());

    for (int i = 0; i < new_data.size(); i++) {
        all_bullet_speed_data[i] = new_data[i];
    }
}

void NormalBulletsData2D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_all_bullet_speed_data"), &NormalBulletsData2D::get_all_bullet_speed_data);
    ClassDB::bind_method(D_METHOD("set_all_bullet_speed_data", "new_data"), &NormalBulletsData2D::set_all_bullet_speed_data);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_speed_data"), "set_all_bullet_speed_data", "get_all_bullet_speed_data");
}
}