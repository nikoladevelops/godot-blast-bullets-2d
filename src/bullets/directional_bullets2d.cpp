#include "directional_bullets2d.hpp"

#include "../spawn-data/directional_bullets_data2d.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "spawn-data/multimesh_bullets_data2d.hpp"
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {
void DirectionalBullets2D::set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data) {
	int speed_data_size = new_speed_data.size();

	// Ensure vectors are the correct size before we start indexing
	if ((int)all_cached_speed.size() != amount_bullets) {
		all_cached_speed.resize(amount_bullets);
		all_cached_max_speed.resize(amount_bullets);
		all_cached_acceleration.resize(amount_bullets);
		all_cached_direction.resize(amount_bullets);
		all_cached_velocity.resize(amount_bullets);
	}

	// In case not enough speed data was provided, we will use the first element as fallback for all bullets
	bool use_per_bullet = (speed_data_size == amount_bullets);
	Ref<BulletSpeedData2D> fallback_data;

	if (!use_per_bullet) {
		if (speed_data_size > 0) {
			fallback_data = new_speed_data[0];
		}

		// In case no speed data was provided at all, create a default one (everything set to 0 by default)
		if (fallback_data.is_null()) {
			fallback_data.instantiate();
		}
	}

	for (int i = 0; i < amount_bullets; ++i) {
		const real_t rot = all_cached_shape_transforms[i].get_rotation();

		Ref<BulletSpeedData2D> data = fallback_data;

		if (use_per_bullet) {
			data = new_speed_data[i];
		}

		// Extract values with null safety
		real_t s = 0.0, m = 0.0, acc = 0.0;

		if (data.is_valid()) {
			s = data->speed;
			m = data->max_speed;
			acc = data->acceleration;
		}

		// Overwrite existing memory slots
		all_cached_speed[i] = s;
		all_cached_max_speed[i] = m;
		all_cached_acceleration[i] = acc;

		Vector2 dir = Vector2(Math::cos(rot), Math::sin(rot));
		all_cached_direction[i] = dir;
		all_cached_velocity[i] = (dir * s) + inherited_velocity_offset;
	}
}

void DirectionalBullets2D::custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) {
	const DirectionalBulletsData2D &directional_data = static_cast<const DirectionalBulletsData2D &>(data);

	set_up_movement_data(directional_data.all_bullet_speed_data);

	adjust_direction_based_on_rotation = directional_data.adjust_direction_based_on_rotation;

	// Each bullet can have its own homing target
	all_bullet_homing_targets.resize(amount_bullets); // Create a vector that contains an empty queue for each bullet index
	all_homing_count.resize(amount_bullets, 0);

	// Orbiting
	all_orbiting_data.resize(amount_bullets); // Create a vector that contains an empty orbiting data for each bullet index
	all_orbiting_status.resize(amount_bullets, 0); // Initialize all orbiting status to disabled
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

	// Homing

	// Ensure all old homing targets are cleared. Do NOT clear the vector ever.
	for (auto &queue : all_bullet_homing_targets) {
		queue.clear_homing_targets(cached_mouse_global_position); // Passing garbage mouse global position but its fine
	}
	active_homing_count = 0;
	all_homing_count.assign(amount_bullets, 0);

	shared_homing_deque.clear_homing_targets(cached_mouse_global_position); // Passing garbage mouse global position but its fine
	//

	// Orbiting

	all_orbiting_status.assign(amount_bullets, 0); // Initialize all orbiting status to disabled
	active_orbiting_count = 0;

	//

	homing_update_interval = 0.0;
	homing_update_timer = 0.0;
	homing_smoothing = 0.0;
	homing_take_control_of_texture_rotation = false;

	homing_distance_before_reached = 5.0;
	bullet_homing_auto_pop_after_target_reached = false;
	shared_homing_deque_auto_pop_after_target_reached = false;
}

void DirectionalBullets2D::custom_additional_disable_logic() {
	bullet_factory->directional_bullets_set.disable_data(sparse_set_id);
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

	ClassDB::bind_method(D_METHOD("all_bullets_pop_front_target", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_pop_front_target, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_pop_back_target", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_pop_back_target, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_mouse", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_replace_homing_targets_with_mouse, DEFVAL(0), DEFVAL(-1));

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

	ClassDB::bind_method(D_METHOD("get_shared_homing_deque_auto_pop_after_target_reached"), &DirectionalBullets2D::get_shared_homing_deque_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_shared_homing_deque_auto_pop_after_target_reached", "value"), &DirectionalBullets2D::set_shared_homing_deque_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_homing_deque_auto_pop_after_target_reached"), "set_shared_homing_deque_auto_pop_after_target_reached", "get_shared_homing_deque_auto_pop_after_target_reached");

	// OTHER HOMING RELATED

	ClassDB::bind_method(D_METHOD("get_homing_distance_before_reached"), &DirectionalBullets2D::get_homing_distance_before_reached);
	ClassDB::bind_method(D_METHOD("set_homing_distance_before_reached", "value"), &DirectionalBullets2D::set_homing_distance_before_reached);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_distance_before_reached"), "set_homing_distance_before_reached", "get_homing_distance_before_reached");

	ClassDB::bind_method(D_METHOD("get_homing_smoothing"), &DirectionalBullets2D::get_homing_smoothing);
	ClassDB::bind_method(D_METHOD("set_homing_smoothing", "value"), &DirectionalBullets2D::set_homing_smoothing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing"), "set_homing_smoothing", "get_homing_smoothing");

	ClassDB::bind_method(D_METHOD("get_homing_update_interval"), &DirectionalBullets2D::get_homing_update_interval);
	ClassDB::bind_method(D_METHOD("set_homing_update_interval", "value"), &DirectionalBullets2D::set_homing_update_interval);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_update_interval"), "set_homing_update_interval", "get_homing_update_interval");

	ClassDB::bind_method(D_METHOD("get_homing_take_control_of_texture_rotation"), &DirectionalBullets2D::get_homing_take_control_of_texture_rotation);
	ClassDB::bind_method(D_METHOD("set_homing_take_control_of_texture_rotation", "value"), &DirectionalBullets2D::set_homing_take_control_of_texture_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_take_control_of_texture_rotation"), "set_homing_take_control_of_texture_rotation", "get_homing_take_control_of_texture_rotation");

	// ORBITING RELATED

	ClassDB::bind_method(D_METHOD("bullet_enable_orbiting", "bullet_index", "orbiting_radius", "orbiting_direction", "orbiting_texture_rotation"), &DirectionalBullets2D::bullet_enable_orbiting);
	ClassDB::bind_method(D_METHOD("bullet_disable_orbiting", "bullet_index"), &DirectionalBullets2D::bullet_disable_orbiting);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_radius", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_radius);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_radius", "bullet_index", "new_radius"), &DirectionalBullets2D::bullet_set_orbiting_radius);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_texture_rotation", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_texture_rotation);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_texture_rotation", "bullet_index", "new_texture_rotation"), &DirectionalBullets2D::bullet_set_orbiting_texture_rotation);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_direction", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_direction);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_direction", "bullet_index", "new_direction"), &DirectionalBullets2D::bullet_set_orbiting_direction);

	ClassDB::bind_method(D_METHOD("all_bullets_enable_orbiting", "orbiting_radius", "orbiting_direction", "orbiting_texture_rotation", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_enable_orbiting, DEFVAL(0), DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("all_bullets_disable_orbiting", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_disable_orbiting, DEFVAL(0), DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_radius", "new_radius", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_radius, DEFVAL(0), DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_direction", "new_direction", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_direction, DEFVAL(0), DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_texture_rotation", "new_rotation", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_texture_rotation, DEFVAL(0), DEFVAL(-1));

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

	BIND_ENUM_CONSTANT(DontMove);
	BIND_ENUM_CONSTANT(OrbitLeft);
	BIND_ENUM_CONSTANT(OrbitRight);

	BIND_ENUM_CONSTANT(FaceTarget);
	BIND_ENUM_CONSTANT(FaceOppositeTarget);
	BIND_ENUM_CONSTANT(FaceOrbitingDirection);
	BIND_ENUM_CONSTANT(FaceOppositeOrbitingDirection);

	ADD_SIGNAL(MethodInfo("bullet_homing_target_reached",
			PropertyInfo(Variant::OBJECT, "multimesh_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
			PropertyInfo(Variant::INT, "bullet_index"),
			PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node2D"),
			PropertyInfo(Variant::VECTOR2, "target_global_position")));
}

} //namespace BlastBullets2D
