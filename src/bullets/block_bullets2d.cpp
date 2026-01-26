#include "block_bullets2d.hpp"
#include "../factory/bullet_factory2d.hpp"
#include "../spawn-data/block_bullets_data2d.hpp"

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/quad_mesh.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

using namespace godot;

namespace BlastBullets2D {

void BlockBullets2D::set_up_movement_data(const BulletSpeedData2D &new_speed_data) {
	// Ensure all old data is removed
	if (all_cached_speed.size() > 0) {
		all_cached_speed.clear();
		all_cached_max_speed.clear();
		all_cached_acceleration.clear();
		all_cached_direction.clear();
		all_cached_velocity.clear();
	}

	// BlockBullets work by moving with the same speed in the same direction
	all_cached_speed.emplace_back(new_speed_data.speed);
	all_cached_max_speed.emplace_back(new_speed_data.max_speed);
	all_cached_acceleration.emplace_back(new_speed_data.acceleration);

	all_cached_direction.emplace_back(Vector2(Math::cos(block_rotation_radians), Math::sin(block_rotation_radians)));
	all_cached_velocity.emplace_back(all_cached_direction[0] * all_cached_speed[0]);
}

void BlockBullets2D::custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) {
	const BlockBulletsData2D &block_data = static_cast<const BlockBulletsData2D &>(data);

	block_rotation_radians = block_data.block_rotation_radians;
	set_up_movement_data(*block_data.block_speed.ptr());
}

void BlockBullets2D::custom_additional_enable_logic(const MultiMeshBulletsData2D &data) {
	const BlockBulletsData2D &block_data = static_cast<const BlockBulletsData2D &>(data);

	block_rotation_radians = block_data.block_rotation_radians;
	set_up_movement_data(*block_data.block_speed.ptr());
}

void BlockBullets2D::custom_additional_disable_logic(){
	bullet_factory->block_bullets_set.disable_data(sparse_set_id);
}

void BlockBullets2D::_bind_methods() {
	// Expose methods to Godot here
}

} //namespace BlastBullets2D
