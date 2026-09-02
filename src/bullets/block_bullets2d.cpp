#include "block_bullets2d.hpp"
#include "../factory/bullet_factory2d.hpp"
#include "../spawn-data/block_bullets_data2d.hpp"

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/quad_mesh.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

using namespace godot;

namespace BlastBullets2D {

void BlockBullets2D::set_up_movement_data(const BulletSpeedData2D &new_speed_data) {
	// Keep vectors sized to amount_bullets and fill identically (Block moves as one)
	if ((int)all_cached_speed.size() != amount_bullets) {
		all_cached_speed.resize(amount_bullets);
		all_cached_max_speed.resize(amount_bullets);
		all_cached_acceleration.resize(amount_bullets);
		all_cached_direction.resize(amount_bullets);
		all_cached_velocity.resize(amount_bullets);
	}

	Vector2 dir = Vector2(Math::cos(block_rotation_radians), Math::sin(block_rotation_radians));
	for (int i = 0; i < amount_bullets; ++i) {
		all_cached_speed[i] = new_speed_data.speed;
		all_cached_max_speed[i] = new_speed_data.max_speed;
		all_cached_acceleration[i] = new_speed_data.acceleration;
		all_cached_direction[i] = dir;
		all_cached_velocity[i] = dir * new_speed_data.speed + inherited_velocity_offset;
	}
}

void BlockBullets2D::custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) {
	const BlockBulletsData2D &block_data = static_cast<const BlockBulletsData2D &>(data);

	block_rotation_radians = block_data.block_rotation_radians;
	if (block_data.block_speed.is_valid()) {
		set_up_movement_data(*block_data.block_speed.ptr());
	} else {
		UtilityFunctions::push_error("BlockBulletsData2D.block_speed is null in spawn - using default speed 0. Set block_speed to avoid this.");
		BulletSpeedData2D default_data;
		set_up_movement_data(default_data);
	}
}

void BlockBullets2D::custom_additional_enable_logic(const MultiMeshBulletsData2D &data) {
	const BlockBulletsData2D &block_data = static_cast<const BlockBulletsData2D &>(data);

	block_rotation_radians = block_data.block_rotation_radians;
	if (block_data.block_speed.is_valid()) {
		set_up_movement_data(*block_data.block_speed.ptr());
	} else {
		UtilityFunctions::push_error("BlockBulletsData2D.block_speed is null in enable - using default speed 0.");
		BulletSpeedData2D default_data;
		set_up_movement_data(default_data);
	}
}

void BlockBullets2D::custom_additional_disable_logic() {
	bullet_factory->block_bullets_set.disable_data(sparse_set_id);
}

void BlockBullets2D::_bind_methods() {
	// Expose methods to Godot here
}

} //namespace BlastBullets2D
