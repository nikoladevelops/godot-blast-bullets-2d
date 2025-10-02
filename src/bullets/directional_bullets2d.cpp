#include "directional_bullets2d.hpp"

#include "../save-data/save_data_directional_bullets2d.hpp"
#include "../spawn-data/directional_bullets_data2d.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include <algorithm>
#include <cstddef>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

void DirectionalBullets2D::set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data) {
	int speed_data_size = new_speed_data.size(); // the amount of speed data provided

	if (speed_data_size != amount_bullets) {
		UtilityFunctions::print("Error. When using DirectionalBullets2D you need to provide BulletSpeedData2D for every single bullet.");
		return;
	}

	// If there is any old data inside, it needs to be cleaned up
	if (all_cached_speed.size() > 0) {
		// Clear all old data
		all_cached_speed.clear();
		all_cached_max_speed.clear();
		all_cached_acceleration.clear();
		all_cached_direction.clear();
		all_cached_velocity.clear();

		// If the new data does not fit, then reserve enough space
		if (all_cached_speed.capacity() != speed_data_size) {
			all_cached_speed.reserve(speed_data_size);
			all_cached_max_speed.reserve(speed_data_size);
			all_cached_acceleration.reserve(speed_data_size);
			all_cached_direction.reserve(speed_data_size);
			all_cached_velocity.reserve(speed_data_size);
		}
	}

	for (int i = 0; i < speed_data_size; i++) {
		// The rotation of each transform
		real_t curr_bullet_rotation = all_cached_shape_transforms[i].get_rotation(); // Note: I am using the shape transforms, since the instance transforms might be rotated to account for bullet texture rotation

		const Ref<BulletSpeedData2D> &curr_speed_data = new_speed_data[i];
		all_cached_speed.emplace_back(curr_speed_data->speed);
		all_cached_max_speed.emplace_back(curr_speed_data->max_speed);
		all_cached_acceleration.emplace_back(curr_speed_data->acceleration);

		// Calculate the direction
		all_cached_direction.emplace_back(Vector2(Math::cos(curr_bullet_rotation), Math::sin(curr_bullet_rotation)));

		// Calculate the velocity
		all_cached_velocity.emplace_back(all_cached_direction[i] * all_cached_speed[i]);
	}
}

void DirectionalBullets2D::custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) {
	const DirectionalBulletsData2D &directional_data = static_cast<const DirectionalBulletsData2D &>(data);

	set_up_movement_data(directional_data.all_bullet_speed_data);

	adjust_direction_based_on_rotation = directional_data.adjust_direction_based_on_rotation;

	// Homing behavior related //

	// Each bullet can have its own homing target
	all_bullet_homing_targets.resize(amount_bullets); // Create a vector that contains an empty queue for each bullet index
	cached_bullet_homing_deque_front_target_global_positions.resize(amount_bullets, Vector2(0, 0));
	//
}

void DirectionalBullets2D::custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) {
	SaveDataDirectionalBullets2D &directional_save_data = static_cast<SaveDataDirectionalBullets2D &>(data);
	directional_save_data.adjust_direction_based_on_rotation = adjust_direction_based_on_rotation;

	// TODO saving of homing behavior
	// TODO all_cached_homing_direction
}

void DirectionalBullets2D::custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) {
	const SaveDataDirectionalBullets2D &directional_save_data = static_cast<const SaveDataDirectionalBullets2D &>(data);
	adjust_direction_based_on_rotation = directional_save_data.adjust_direction_based_on_rotation;

	// TODO loading of homing behavior
	// TODO all_cached_homing_direction

	all_bullet_homing_targets.resize(amount_bullets); // Create a vector that contains an empty queue for each bullet index
	cached_bullet_homing_deque_front_target_global_positions.resize(amount_bullets, Vector2(0, 0));
}

void DirectionalBullets2D::custom_additional_activate_logic(const MultiMeshBulletsData2D &data) {
	const DirectionalBulletsData2D &directional_data = static_cast<const DirectionalBulletsData2D &>(data);

	// Get the list of connections for the signal
    TypedArray<Dictionary> connections = get_signal_connection_list("bullet_homing_target_reached");
    
    // Iterate through all connections and disconnect them
    for (int i = 0; i < connections.size(); i++) {
        Dictionary connection = connections[i];
        Callable callable = connection["callable"];
        disconnect("bullet_homing_target_reached", callable);
    }

	multimesh_bullets_unique_id = generate_unique_id();
	multimesh_custom_timers.clear();

	set_up_movement_data(directional_data.all_bullet_speed_data);

	adjust_direction_based_on_rotation = directional_data.adjust_direction_based_on_rotation;
	// Ensure all old homing targets are cleared. Do NOT clear the vector ever.
	for (auto &queue : all_bullet_homing_targets) {
		queue.clear();
	}

	// TODO When activating a multimesh it never has homing soo no need for this - instead when pushing or popping that's when you use the cache
	//std::fill(cached_bullet_homing_deque_front_target_global_positions.begin(), cached_bullet_homing_deque_front_target_global_positions.end(), Vector2(0, 0));

	homing_update_interval = 0.0;
	homing_update_timer = 0.0;
	homing_smoothing = 0.0;
	homing_take_control_of_texture_rotation = false;

	homing_boundary_behavior = BoundaryDontMove;
	homing_boundary_facing_direction = FaceTarget;
	homing_boundary_distance_away_from_target = 0.0;

	distance_from_target_before_considering_as_reached = 5.0;
	are_bullets_homing_towards_mouse_global_position = false;
	bullet_homing_auto_pop_after_target_reached = false;


	

}
} //namespace BlastBullets2D