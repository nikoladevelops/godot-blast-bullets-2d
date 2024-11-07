#include "block_bullets_data2d.hpp"

using namespace godot;

namespace BlastBullets {


float BlockBulletsData2D::get_block_rotation_radians() const {
    return block_rotation_radians;
}
void BlockBulletsData2D::set_block_rotation_radians(float new_block_rotation_radians) {
    block_rotation_radians = new_block_rotation_radians;
}


Ref<BulletSpeedData> BlockBulletsData2D::get_block_speed() const{
    return block_speed;
}
void BlockBulletsData2D::set_block_speed(const godot::Ref<BulletSpeedData>& new_block_speed){
    block_speed = new_block_speed;
}

int BlockBulletsData2D::calculate_bitmask(const TypedArray<int> &numbers) {
    int bitmask_value = 0;
    for (int i = 0; i < numbers.size(); i++) {
        // From the current number calculate which bit it corresponds to inside the bitmask (number 5 = 5th bit from right to left = 10000 in binary). This is the same as the formula 2 to the power of N-1. Example: if we have the number 4, then its 2 to the power of 4-1 -> this is equal to -> 2 to the power of 3 = 8  -> turn 8 into binary = 1000. I am doing exactly the same thing here by saying I have the number 1, shift it to the left by N-1 = 1000  (because the number one got shifted to the left by 3 positions, those 3 positions are now filled with zeros)
        bitmask_value |= 1 << ((int)(numbers[i]) - 1); // this is the more inefficient way of doing the same thing: static_cast<int>(pow(2, (int)(numbers[i]) - 1));
    }

    return bitmask_value;
}

void BlockBulletsData2D::_bind_methods() {
    
    ClassDB::bind_method(D_METHOD("get_block_rotation_radians"), &BlockBulletsData2D::get_block_rotation_radians);
    ClassDB::bind_method(D_METHOD("set_block_rotation_radians", "new_block_rotation_radians"), &BlockBulletsData2D::set_block_rotation_radians);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "block_rotation_radians"), "set_block_rotation_radians", "get_block_rotation_radians");
    
    ClassDB::bind_method(D_METHOD("get_block_speed"), &BlockBulletsData2D::get_block_speed);
    ClassDB::bind_method(D_METHOD("set_block_speed", "new_block_speed"), &BlockBulletsData2D::set_block_speed);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "block_speed"), "set_block_speed", "get_block_speed");
    
    // TODO remove this
    ClassDB::bind_static_method("BlockBulletsData2D", D_METHOD("calculate_bitmask", "numbers"), &BlockBulletsData2D::calculate_bitmask);
}
}