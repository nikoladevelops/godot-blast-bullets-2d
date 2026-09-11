#include "block_bullets2d.hpp"
#include "../factory/bullet_factory2d.hpp"
#include "../spawn-data/block_bullets_data2d.hpp"

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/quad_mesh.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

using namespace godot;

namespace BlastBullets2D {

// Shared zeroed fallback. Must be a memnew'd Ref: engine classes
// (BulletSpeedData2D extends Resource) cannot live on the stack - a
// stack temporary aborts with "created without binding callbacks".
static Ref<BulletSpeedData2D> get_default_block_speed_data() {
	static Ref<BulletSpeedData2D> cached;
	if (cached.is_null()) {
		cached.instantiate();
	}
	return cached;
}

void BlockBullets2D::set_up_movement_data(const BulletSpeedData2D &new_speed_data) {
	// Block moves as one, but every bullet holds its own copy of the shared
	// values (like DirectionalBullets2D): per-bullet helpers can then index [i]
	// safely, and per-bullet overrides via the setters are honored by the tick.
	if ((int)all_cached_speed.size() != amount_bullets) {
		all_cached_speed.resize(amount_bullets);
		all_cached_max_speed.resize(amount_bullets);
		all_cached_acceleration.resize(amount_bullets);
		all_cached_direction.resize(amount_bullets);
		all_cached_velocity.resize(amount_bullets);
	}

	Vector2 dir = Vector2(Math::cos(block_rotation_radians), Math::sin(block_rotation_radians));
	if (!Math::is_finite(block_rotation_radians) || !Math::is_finite(new_speed_data.speed) || !Math::is_finite(new_speed_data.max_speed) || !Math::is_finite(new_speed_data.acceleration)) {
		UtilityFunctions::push_error("BlockBullets2D movement data contains NaN/Inf, using zeros for this setup.");
		for (int i = 0; i < amount_bullets; ++i) {
			all_cached_speed[i] = 0.0;
			all_cached_max_speed[i] = 0.0;
			all_cached_acceleration[i] = 0.0;
			all_cached_direction[i] = Vector2(1, 0);
			all_cached_velocity[i] = inherited_velocity_offset;
		}
		return;
	}
	for (int i = 0; i < amount_bullets; ++i) {
		all_cached_speed[i] = new_speed_data.speed;
		all_cached_max_speed[i] = new_speed_data.max_speed;
		all_cached_acceleration[i] = new_speed_data.acceleration;
		all_cached_direction[i] = dir;
		all_cached_velocity[i] = dir * new_speed_data.speed + inherited_velocity_offset;
	}
}

void BlockBullets2D::custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) {
	const BlockBulletsData2D *block_data = Object::cast_to<BlockBulletsData2D>(&data);
	// Size per-bullet movement SoA up front (same as DirectionalBullets2D), so
	// even a wrong-type early-return leaves valid memory for the helpers.
	set_up_movement_data(*get_default_block_speed_data().ptr());
	if (block_data == nullptr) {
		UtilityFunctions::push_error("BlockBullets2D::spawn got wrong spawn data type, expected BlockBulletsData2D.");
		return;
	}

	block_rotation_radians = block_data->block_rotation_radians;
	if (block_data->block_speed.is_valid()) {
		set_up_movement_data(*block_data->block_speed.ptr());
	} else {
		UtilityFunctions::push_error("BlockBulletsData2D.block_speed is null in spawn - using default speed 0. Set block_speed to avoid this.");
		set_up_movement_data(*get_default_block_speed_data().ptr());
	}
}

void BlockBullets2D::custom_additional_enable_logic(const MultiMeshBulletsData2D &data) {
	const BlockBulletsData2D *block_data = Object::cast_to<BlockBulletsData2D>(&data);
	// Same defensive sizing as spawn (a wrong-type enable must not leave the
	// movement SoA empty for the tick path).
	set_up_movement_data(*get_default_block_speed_data().ptr());
	if (block_data == nullptr) {
		UtilityFunctions::push_error("BlockBullets2D::enable got wrong spawn data type, expected BlockBulletsData2D.");
		return;
	}

	block_rotation_radians = block_data->block_rotation_radians;
	if (block_data->block_speed.is_valid()) {
		set_up_movement_data(*block_data->block_speed.ptr());
	} else {
		UtilityFunctions::push_error("BlockBulletsData2D.block_speed is null in enable - using default speed 0.");
		set_up_movement_data(*get_default_block_speed_data().ptr());
	}
}

void BlockBullets2D::custom_additional_disable_logic() {
	if (bullet_factory != nullptr) {
		bullet_factory->block_bullets_set.disable_data(sparse_set_id);
	}
}

void BlockBullets2D::_bind_methods() {
	// Expose methods to Godot here
}

} //namespace BlastBullets2D
