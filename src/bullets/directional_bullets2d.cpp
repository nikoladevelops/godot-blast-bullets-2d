#include "directional_bullets2d.hpp"

#include "../save-data/save_data_directional_bullets2d.hpp"
#include "../spawn-data/directional_bullets_data2d.hpp"
#include "godot_cpp/core/class_db.hpp"
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
		UtilityFunctions::push_error("When using DirectionalBullets2D you need to provide BulletSpeedData2D for every single bullet.");
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

	for (int i = 0; i < speed_data_size; ++i) {
		// The rotation of each transform
		real_t curr_bullet_rotation = all_cached_shape_transforms[i].get_rotation(); // Note: I am using the shape transforms, since the instance transforms might be rotated to account for bullet texture rotation

		const Ref<BulletSpeedData2D> &curr_speed_data = new_speed_data[i];
		all_cached_speed.emplace_back(curr_speed_data->speed);
		all_cached_max_speed.emplace_back(curr_speed_data->max_speed);
		all_cached_acceleration.emplace_back(curr_speed_data->acceleration);

		// Calculate the direction
		all_cached_direction.emplace_back(Vector2(Math::cos(curr_bullet_rotation), Math::sin(curr_bullet_rotation)));

		// Calculate the velocity
		all_cached_velocity.emplace_back(all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset);
	}
}

void DirectionalBullets2D::custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) {
	const DirectionalBulletsData2D &directional_data = static_cast<const DirectionalBulletsData2D &>(data);

	set_up_movement_data(directional_data.all_bullet_speed_data);

	adjust_direction_based_on_rotation = directional_data.adjust_direction_based_on_rotation;

	is_multimesh_auto_pooling_enabled = directional_data.is_multimesh_auto_pooling_enabled;

	// Homing behavior related //

	// Each bullet can have its own homing target
	all_bullet_homing_targets.resize(amount_bullets); // Create a vector that contains an empty queue for each bullet index
	//
}

void DirectionalBullets2D::custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) {
	SaveDataDirectionalBullets2D &directional_save_data = static_cast<SaveDataDirectionalBullets2D &>(data);
	directional_save_data.adjust_direction_based_on_rotation = adjust_direction_based_on_rotation;

	directional_save_data.is_multimesh_auto_pooling_enabled = is_multimesh_auto_pooling_enabled;
	// TODO saving of homing behavior
	// TODO all_cached_homing_direction
}

void DirectionalBullets2D::custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) {
	const SaveDataDirectionalBullets2D &directional_save_data = static_cast<const SaveDataDirectionalBullets2D &>(data);
	adjust_direction_based_on_rotation = directional_save_data.adjust_direction_based_on_rotation;

	is_multimesh_auto_pooling_enabled = directional_save_data.is_multimesh_auto_pooling_enabled;

	// TODO loading of homing behavior
	// TODO all_cached_homing_direction

	all_bullet_homing_targets.resize(amount_bullets); // Create a vector that contains an empty queue for each bullet index
}

void DirectionalBullets2D::custom_additional_enable_logic(const MultiMeshBulletsData2D &data) {
	const DirectionalBulletsData2D &directional_data = static_cast<const DirectionalBulletsData2D &>(data);

	// Get the list of connections for the signal
	TypedArray<Dictionary> connections = get_signal_connection_list("bullet_homing_target_reached");

	// Iterate through all connections and disconnect them
	for (int i = 0; i < connections.size(); ++i) {
		Dictionary connection = connections[i];
		Callable callable = connection["callable"];
		disconnect("bullet_homing_target_reached", callable);
	}

	set_up_movement_data(directional_data.all_bullet_speed_data);

	adjust_direction_based_on_rotation = directional_data.adjust_direction_based_on_rotation;
	// Ensure all old homing targets are cleared. Do NOT clear the vector ever.
	for (auto &queue : all_bullet_homing_targets) {
		queue.clear_homing_targets(cached_mouse_global_position); // Passing garbage mouse global position but its fine
	}

	shared_homing_deque.clear_homing_targets(cached_mouse_global_position); // Passing garbage mouse global position but its fine

	homing_update_interval = 0.0;
	homing_update_timer = 0.0;
	homing_smoothing = 0.0;
	homing_take_control_of_texture_rotation = false;

	homing_boundary_behavior = BoundaryDontMove;
	homing_boundary_facing_direction = FaceTarget;
	homing_boundary_distance_away_from_target = 0.0;

	distance_from_target_before_considering_as_reached = 5.0;
	bullet_homing_auto_pop_after_target_reached = false;
	shared_deque_auto_pop_after_target_reached = false;

	is_multimesh_auto_pooling_enabled = directional_data.is_multimesh_auto_pooling_enabled;
}

void DirectionalBullets2D::_bind_methods() {
	// PER BULLET HOMING DEQUE POP METHODS
	ClassDB::bind_method(D_METHOD("bullet_homing_pop_front_target", "bullet_index"), &DirectionalBullets2D::bullet_homing_pop_front_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_pop_back_target", "bullet_index"), &DirectionalBullets2D::bullet_homing_pop_back_target);

	// PER BULLET HOMING DEQUE PUSH METHODS
	ClassDB::bind_method(D_METHOD("bullet_homing_push_front_mouse_position_target", "bullet_index"), &DirectionalBullets2D::bullet_homing_push_front_mouse_position_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_front_node2d_target", "bullet_index", "new_homing_target"), &DirectionalBullets2D::bullet_homing_push_front_node2d_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_front_global_position_target", "bullet_index", "global_position"), &DirectionalBullets2D::bullet_homing_push_front_global_position_target);

	ClassDB::bind_method(D_METHOD("bullet_homing_push_back_mouse_position_target", "bullet_index"), &DirectionalBullets2D::bullet_homing_push_back_mouse_position_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_back_node2d_target", "bullet_index", "new_homing_target"), &DirectionalBullets2D::bullet_homing_push_back_node2d_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_back_global_position_target", "bullet_index", "global_position"), &DirectionalBullets2D::bullet_homing_push_back_global_position_target);

	// PER BULLET HOMING DEQUE HELPERS
	ClassDB::bind_method(D_METHOD("all_bullets_push_front_mouse_position_target", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_front_mouse_position_target, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_push_back_mouse_position_target", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_back_mouse_position_target, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_push_front_homing_target", "node2d_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_front_homing_target, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_push_back_homing_target", "node2d_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_back_homing_target, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_push_front_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_front_homing_targets_array, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_push_back_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_back_homing_targets_array, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_new_target", "node2d_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_replace_homing_targets_with_new_target, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_new_target_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_replace_homing_targets_with_new_target_array, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_clear_homing_targets", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_clear_homing_targets, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("bullet_clear_homing_targets", "bullet_index"), &DirectionalBullets2D::bullet_clear_homing_targets);

	ClassDB::bind_method(D_METHOD("bullet_homing_check_targets_amount", "bullet_index"), &DirectionalBullets2D::bullet_homing_check_targets_amount);

	ClassDB::bind_method(D_METHOD("bullet_check_has_homing_targets", "bullet_index"), &DirectionalBullets2D::bullet_check_has_homing_targets);

	ClassDB::bind_method(D_METHOD("bullet_homing_check_current_target_type", "bullet_index"), &DirectionalBullets2D::bullet_homing_check_current_target_type);

	ClassDB::bind_method(D_METHOD("bullet_get_current_homing_target", "bullet_index"), &DirectionalBullets2D::bullet_get_current_homing_target);

	ClassDB::bind_method(D_METHOD("get_bullet_homing_auto_pop_after_target_reached"), &DirectionalBullets2D::get_bullet_homing_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_bullet_homing_auto_pop_after_target_reached", "value"), &DirectionalBullets2D::set_bullet_homing_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bullet_homing_auto_pop_after_target_reached"), "set_bullet_homing_auto_pop_after_target_reached", "get_bullet_homing_auto_pop_after_target_reached");

	// SHARED HOMING DEQUE POP METHODS
	ClassDB::bind_method(D_METHOD("shared_homing_deque_pop_front_target"), &DirectionalBullets2D::shared_homing_deque_pop_front_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_pop_back_target"), &DirectionalBullets2D::shared_homing_deque_pop_back_target);

	// SHARED HOMING DEQUE PUSH METHODS
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_front_mouse_position_target"), &DirectionalBullets2D::shared_homing_deque_push_front_mouse_position_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_front_node2d_target", "new_homing_target"), &DirectionalBullets2D::shared_homing_deque_push_front_node2d_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_front_global_position_target", "global_position"), &DirectionalBullets2D::shared_homing_deque_push_front_global_position_target);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_back_mouse_position_target"), &DirectionalBullets2D::shared_homing_deque_push_back_mouse_position_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_back_node2d_target", "new_homing_target"), &DirectionalBullets2D::shared_homing_deque_push_back_node2d_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_back_global_position_target", "global_position"), &DirectionalBullets2D::shared_homing_deque_push_back_global_position_target);

	// SHARED HOMING DEQUE HELPERS
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_front_homing_targets_array", "node2ds_or_global_positions_array"), &DirectionalBullets2D::shared_homing_deque_push_front_homing_targets_array);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_back_homing_targets_array", "node2ds_or_global_positions_array"), &DirectionalBullets2D::shared_homing_deque_push_back_homing_targets_array);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_replace_homing_targets_with_new_target", "node2d_or_global_position"), &DirectionalBullets2D::shared_homing_deque_replace_homing_targets_with_new_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_replace_homing_targets_with_new_target_array", "node2ds_or_global_positions_array"), &DirectionalBullets2D::shared_homing_deque_replace_homing_targets_with_new_target_array);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_clear_homing_targets"), &DirectionalBullets2D::shared_homing_deque_clear_homing_targets);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_check_homing_targets_amount"), &DirectionalBullets2D::shared_homing_deque_check_homing_targets_amount);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_check_has_homing_targets"), &DirectionalBullets2D::shared_homing_deque_check_has_homing_targets);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_check_current_target_type"), &DirectionalBullets2D::shared_homing_deque_check_current_target_type);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_get_current_homing_target"), &DirectionalBullets2D::shared_homing_deque_get_current_homing_target);

	ClassDB::bind_method(D_METHOD("get_shared_deque_auto_pop_after_target_reached"), &DirectionalBullets2D::get_shared_deque_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_shared_deque_auto_pop_after_target_reached", "value"), &DirectionalBullets2D::set_shared_deque_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_deque_auto_pop_after_target_reached"), "set_shared_deque_auto_pop_after_target_reached", "get_shared_deque_auto_pop_after_target_reached");

	// OTHER HOMING RELATED

	ClassDB::bind_method(D_METHOD("get_distance_from_target_before_considering_as_reached"), &DirectionalBullets2D::get_distance_from_target_before_considering_as_reached);
	ClassDB::bind_method(D_METHOD("set_distance_from_target_before_considering_as_reached", "value"), &DirectionalBullets2D::set_distance_from_target_before_considering_as_reached);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance_from_target_before_considering_as_reached"), "set_distance_from_target_before_considering_as_reached", "get_distance_from_target_before_considering_as_reached");

	ClassDB::bind_method(D_METHOD("get_homing_boundary_behavior"), &DirectionalBullets2D::get_homing_boundary_behavior);
	ClassDB::bind_method(D_METHOD("set_homing_boundary_behavior", "value"), &DirectionalBullets2D::set_homing_boundary_behavior);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_boundary_behavior"), "set_homing_boundary_behavior", "get_homing_boundary_behavior");

	ClassDB::bind_method(D_METHOD("get_homing_boundary_facing_direction"), &DirectionalBullets2D::get_homing_boundary_facing_direction);
	ClassDB::bind_method(D_METHOD("set_homing_boundary_facing_direction", "value"), &DirectionalBullets2D::set_homing_boundary_facing_direction);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_boundary_facing_direction"), "set_homing_boundary_facing_direction", "get_homing_boundary_facing_direction");

	ClassDB::bind_method(D_METHOD("get_homing_smoothing"), &DirectionalBullets2D::get_homing_smoothing);
	ClassDB::bind_method(D_METHOD("set_homing_smoothing", "value"), &DirectionalBullets2D::set_homing_smoothing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing"), "set_homing_smoothing", "get_homing_smoothing");

	ClassDB::bind_method(D_METHOD("get_homing_update_interval"), &DirectionalBullets2D::get_homing_update_interval);
	ClassDB::bind_method(D_METHOD("set_homing_update_interval", "value"), &DirectionalBullets2D::set_homing_update_interval);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_update_interval"), "set_homing_update_interval", "get_homing_update_interval");

	ClassDB::bind_method(D_METHOD("get_homing_take_control_of_texture_rotation"), &DirectionalBullets2D::get_homing_take_control_of_texture_rotation);
	ClassDB::bind_method(D_METHOD("set_homing_take_control_of_texture_rotation", "value"), &DirectionalBullets2D::set_homing_take_control_of_texture_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_take_control_of_texture_rotation"), "set_homing_take_control_of_texture_rotation", "get_homing_take_control_of_texture_rotation");

	ClassDB::bind_method(D_METHOD("get_homing_boundary_distance_away_from_target"), &DirectionalBullets2D::get_homing_boundary_distance_away_from_target);
	ClassDB::bind_method(D_METHOD("set_homing_boundary_distance_away_from_target", "value"), &DirectionalBullets2D::set_homing_boundary_distance_away_from_target);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_boundary_distance_away_from_target"), "set_homing_boundary_distance_away_from_target", "get_homing_boundary_distance_away_from_target");

	// OTHER USEFUL METHODS
	ClassDB::bind_method(D_METHOD("teleport_bullet", "bullet_index", "new_global_pos"), &DirectionalBullets2D::teleport_bullet);
	ClassDB::bind_method(D_METHOD("teleport_shift_bullet", "bullet_index", "shift_value"), &DirectionalBullets2D::teleport_shift_bullet);
	ClassDB::bind_method(D_METHOD("teleport_shift_all_bullets", "shift_value", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::teleport_shift_all_bullets, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_inherited_velocity_offset"), &DirectionalBullets2D::get_inherited_velocity_offset);
	ClassDB::bind_method(D_METHOD("set_inherited_velocity_offset", "new_offset"), &DirectionalBullets2D::set_inherited_velocity_offset);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "inherited_velocity_offset"), "set_inherited_velocity_offset", "get_inherited_velocity_offset");

	BIND_ENUM_CONSTANT(GlobalPositionTarget);
	BIND_ENUM_CONSTANT(Node2DTarget);
	BIND_ENUM_CONSTANT(NotHoming);

	BIND_ENUM_CONSTANT(BoundaryDontMove);
	BIND_ENUM_CONSTANT(BoundaryOrbitLeft);
	BIND_ENUM_CONSTANT(BoundaryOrbitRight);

	BIND_ENUM_CONSTANT(FaceTarget);
	BIND_ENUM_CONSTANT(FaceOppositeTarget);
	BIND_ENUM_CONSTANT(FaceOrbitingDirection);

	ADD_SIGNAL(MethodInfo("bullet_homing_target_reached",
			PropertyInfo(Variant::OBJECT, "multimesh_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
			PropertyInfo(Variant::INT, "bullet_index"),
			PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node2D"),
			PropertyInfo(Variant::VECTOR2, "target_global_position")));
}

} //namespace BlastBullets2D
