// Godot bindings: every bound method, property (inspector groups, in workflow
// order), signal and enum constant of BulletVolley2D.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletVolley2D::_bind_methods() {
	// Methods without an inspector row first (every ADD_PROPERTY below needs
	// its getter/setter bound before it), then the property groups.
	ClassDB::bind_method(D_METHOD("get_bullet_speed_data", "bullet_index"), &BulletVolley2D::get_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("set_bullet_speed_data", "bullet_index", "new_bullet_speed_data"), &BulletVolley2D::set_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("get_bullet_rotation_data", "bullet_index"), &BulletVolley2D::get_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("set_bullet_rotation_data", "bullet_index", "new_bullet_rotation_data"), &BulletVolley2D::set_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("all_bullets_get_rotation_data", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_rotation_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_rotation_data", "new_bullet_rotation_data", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_rotation_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("clear_bullet_rotation_data"), &BulletVolley2D::clear_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("all_bullets_get_speed_data", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_speed_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_speed_data", "new_bullet_speed_data", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_speed_data, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_bullet_direction", "bullet_index"), &BulletVolley2D::get_bullet_direction);
	ClassDB::bind_method(D_METHOD("set_bullet_direction", "bullet_index", "new_direction"), &BulletVolley2D::set_bullet_direction);
	ClassDB::bind_method(D_METHOD("all_bullets_get_direction", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_direction, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_direction", "new_direction", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_direction, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("set_bullet_direction_towards_position", "bullet_index", "target_position"), &BulletVolley2D::set_bullet_direction_towards_position);
	ClassDB::bind_method(D_METHOD("all_bullets_set_direction_towards_position", "target_position", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_direction_towards_position, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("set_bullet_direction_towards_node2d", "bullet_index", "target_node"), &BulletVolley2D::set_bullet_direction_towards_node2d);
	ClassDB::bind_method(D_METHOD("all_bullets_set_direction_towards_node2d", "target_node", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_direction_towards_node2d, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_bullet_texture_rotation_radians", "bullet_index"), &BulletVolley2D::get_bullet_texture_rotation_radians);
	ClassDB::bind_method(D_METHOD("set_bullet_texture_rotation_radians", "bullet_index", "new_rotation_radians"), &BulletVolley2D::set_bullet_texture_rotation_radians);
	ClassDB::bind_method(D_METHOD("all_bullets_get_texture_rotation_radians", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_texture_rotation_radians, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_texture_rotation_radians", "new_rotation_radians", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_texture_rotation_radians, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_bullet_texture_rotation_degrees", "bullet_index"), &BulletVolley2D::get_bullet_texture_rotation_degrees);
	ClassDB::bind_method(D_METHOD("set_bullet_texture_rotation_degrees", "bullet_index", "new_rotation_degrees"), &BulletVolley2D::set_bullet_texture_rotation_degrees);
	ClassDB::bind_method(D_METHOD("all_bullets_get_texture_rotation_degrees", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_texture_rotation_degrees, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_texture_rotation_degrees", "new_rotation_degrees", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_texture_rotation_degrees, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("set_bullet_texture_rotation_towards_position", "bullet_index", "target_position"), &BulletVolley2D::set_bullet_texture_rotation_towards_position);
	ClassDB::bind_method(D_METHOD("all_bullets_set_texture_rotation_towards_position", "target_position", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_texture_rotation_towards_position, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("set_bullet_texture_rotation_towards_node2d", "bullet_index", "target_node"), &BulletVolley2D::set_bullet_texture_rotation_towards_node2d);
	ClassDB::bind_method(D_METHOD("all_bullets_set_texture_rotation_towards_node2d", "target_node", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_texture_rotation_towards_node2d, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_bullet_transform", "bullet_index"), &BulletVolley2D::get_bullet_transform);
	ClassDB::bind_method(D_METHOD("get_bullet_global_transform", "bullet_index"), &BulletVolley2D::get_bullet_global_transform);
	ClassDB::bind_method(D_METHOD("get_bullet_velocity", "bullet_index"), &BulletVolley2D::get_bullet_velocity);
	ClassDB::bind_method(D_METHOD("set_bullet_transform", "bullet_index", "new_transform", "set_direction_based_on_transform"), &BulletVolley2D::set_bullet_transform, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("all_bullets_get_transforms", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_transforms, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_transforms", "new_transform", "set_direction_based_on_transform", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_transforms, DEFVAL(false), DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("play_sprite_animation", "sprite_frames", "animation"), &BulletVolley2D::play_sprite_animation, DEFVAL(StringName("default")));
	ClassDB::bind_method(D_METHOD("play_sprite_animation_name", "animation"), &BulletVolley2D::play_sprite_animation_name);
	ClassDB::bind_method(D_METHOD("restart_sprite_animation"), &BulletVolley2D::restart_sprite_animation);
	ClassDB::bind_method(D_METHOD("stop_sprite_animation"), &BulletVolley2D::stop_sprite_animation);
	ClassDB::bind_method(D_METHOD("resume_sprite_animation"), &BulletVolley2D::resume_sprite_animation);
	ClassDB::bind_method(D_METHOD("is_sprite_animation_playing"), &BulletVolley2D::is_sprite_animation_playing);
	ClassDB::bind_method(D_METHOD("is_sprite_animation_finished"), &BulletVolley2D::is_sprite_animation_finished);
	ClassDB::bind_method(D_METHOD("get_sprite_animation"), &BulletVolley2D::get_sprite_animation);
	ClassDB::bind_method(D_METHOD("get_sprite_frames"), &BulletVolley2D::get_sprite_frames);
	ClassDB::bind_method(D_METHOD("get_sprite_frame"), &BulletVolley2D::get_sprite_frame);
	ClassDB::bind_method(D_METHOD("get_sprite_frame_count"), &BulletVolley2D::get_sprite_frame_count);

	ClassDB::bind_method(D_METHOD("disable_bullet", "bullet_index", "release_attachment", "reset_state"), &BulletVolley2D::disable_bullet, DEFVAL(true), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("bullet_reset_state", "bullet_index"), &BulletVolley2D::bullet_reset_state);
	ClassDB::bind_method(D_METHOD("all_bullets_reset_state", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_reset_state, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("is_pooled"), &BulletVolley2D::is_pooled);
	ClassDB::bind_method(D_METHOD("is_parked"), &BulletVolley2D::is_parked);
	ClassDB::bind_method(D_METHOD("get_life_id"), &BulletVolley2D::get_life_id);
	ClassDB::bind_method(D_METHOD("debug_get_life_state"), &BulletVolley2D::debug_get_life_state);
	ClassDB::bind_method(D_METHOD("debug_get_clocks"), &BulletVolley2D::debug_get_clocks);
	ClassDB::bind_method(D_METHOD("clear_bullet", "bullet_index"), &BulletVolley2D::clear_bullet);
	ClassDB::bind_method(D_METHOD("clear_all_bullets"), &BulletVolley2D::clear_all_bullets);
	ClassDB::bind_method(D_METHOD("enable_bullet", "bullet_index", "collision_amount", "enable_attachment"), &BulletVolley2D::enable_bullet, DEFVAL(-1), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("wake_bullet", "bullet_index", "collision_amount", "enable_attachment"), &BulletVolley2D::wake_bullet, DEFVAL(-1), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("debug_get_volley_info"), &BulletVolley2D::debug_get_volley_info);
	ClassDB::bind_method(D_METHOD("debug_dedup_reset"), &BulletVolley2D::debug_dedup_reset);
	ClassDB::bind_method(D_METHOD("debug_dedup_probe", "bullet_index", "target_instance_id"), &BulletVolley2D::debug_dedup_probe);
	ClassDB::bind_method(D_METHOD("debug_dedup_mark", "bullet_index", "target_instance_id"), &BulletVolley2D::debug_dedup_mark);
	ClassDB::bind_method(D_METHOD("debug_dedup_stats"), &BulletVolley2D::debug_dedup_stats);
	ClassDB::bind_method(D_METHOD("debug_dedup_find_collision", "probe_count"), &BulletVolley2D::debug_dedup_find_collision);
	ClassDB::bind_method(D_METHOD("debug_get_timer_count"), &BulletVolley2D::debug_get_timer_count);
	ClassDB::bind_method(D_METHOD("debug_get_shape_state"), &BulletVolley2D::debug_get_shape_state);
	ClassDB::bind_method(D_METHOD("debug_get_attachment_info", "bullet_index"), &BulletVolley2D::debug_get_attachment_info);
	ClassDB::bind_method(D_METHOD("debug_run_interpolation_pass"), &BulletVolley2D::debug_run_interpolation_pass);
	ClassDB::bind_method(D_METHOD("bullet_free_attachment", "bullet_index"), &BulletVolley2D::bullet_free_attachment);
	ClassDB::bind_method(D_METHOD("bullet_disable_attachment", "bullet_index"), &BulletVolley2D::bullet_disable_attachment);
	ClassDB::bind_method(D_METHOD("bullet_enable_attachment", "bullet_index"), &BulletVolley2D::bullet_enable_attachment);
	ClassDB::bind_method(D_METHOD("get_amount_active_attachments"), &BulletVolley2D::get_amount_active_attachments);

	ClassDB::bind_method(D_METHOD("get_amount_bullets"), &BulletVolley2D::get_amount_bullets);

	// The inherited_velocity_offset property row is added in the Movement
	// Speed group below.
	ClassDB::bind_method(D_METHOD("get_inherited_velocity_offset"), &BulletVolley2D::get_inherited_velocity_offset);
	ClassDB::bind_method(D_METHOD("set_inherited_velocity_offset", "new_offset"), &BulletVolley2D::set_inherited_velocity_offset);

	ClassDB::bind_method(D_METHOD("get_all_bullets_status"), &BulletVolley2D::get_all_bullets_status);
	ClassDB::bind_method(D_METHOD("is_bullet_status_enabled", "bullet_index"), &BulletVolley2D::is_bullet_status_enabled);

	ClassDB::bind_method(D_METHOD("get_shared_bullets_custom_data"), &BulletVolley2D::get_shared_bullets_custom_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullets_custom_data", "new_shared_bullets_custom_data"), &BulletVolley2D::set_shared_bullets_custom_data);
	// PER BULLET HOMING DEQUE POP METHODS
	ClassDB::bind_method(D_METHOD("bullet_homing_pop_front_target", "bullet_index"), &BulletVolley2D::bullet_homing_pop_front_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_pop_back_target", "bullet_index"), &BulletVolley2D::bullet_homing_pop_back_target);

	// PER BULLET HOMING DEQUE PUSH METHODS
	ClassDB::bind_method(D_METHOD("bullet_homing_push_front_mouse_position_target", "bullet_index"), &BulletVolley2D::bullet_homing_push_front_mouse_position_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_front_node2d_target", "bullet_index", "new_homing_target"), &BulletVolley2D::bullet_homing_push_front_node2d_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_front_global_position_target", "bullet_index", "global_position"), &BulletVolley2D::bullet_homing_push_front_global_position_target);

	ClassDB::bind_method(D_METHOD("bullet_homing_push_back_mouse_position_target", "bullet_index"), &BulletVolley2D::bullet_homing_push_back_mouse_position_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_back_node2d_target", "bullet_index", "new_homing_target"), &BulletVolley2D::bullet_homing_push_back_node2d_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_back_global_position_target", "bullet_index", "global_position"), &BulletVolley2D::bullet_homing_push_back_global_position_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_back_homing_target", "bullet_index", "node2d_or_global_position"), &BulletVolley2D::bullet_homing_push_back_homing_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_front_homing_target", "bullet_index", "node2d_or_global_position"), &BulletVolley2D::bullet_homing_push_front_homing_target);
	ClassDB::bind_method(D_METHOD("all_bullets_assign_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_assign_homing_targets_array, DEFVAL(0), DEFVAL(-1));

	// PER BULLET HOMING DEQUE HELPERS
	ClassDB::bind_method(D_METHOD("all_bullets_push_front_mouse_position_target", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_push_front_mouse_position_target, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_push_back_mouse_position_target", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_push_back_mouse_position_target, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_push_front_homing_target", "node2d_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_push_front_homing_target, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_push_back_homing_target", "node2d_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_push_back_homing_target, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_push_front_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_push_front_homing_targets_array, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_push_back_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_push_back_homing_targets_array, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_new_target", "node2d_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_replace_homing_targets_with_new_target, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_new_target_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_replace_homing_targets_with_new_target_array, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_pop_front_target", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_pop_front_target, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_pop_back_target", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_pop_back_target, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_mouse", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_replace_homing_targets_with_mouse, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_clear_homing_targets", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_clear_homing_targets, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("bullet_clear_homing_targets", "bullet_index"), &BulletVolley2D::bullet_clear_homing_targets);

	ClassDB::bind_method(D_METHOD("bullet_homing_check_targets_amount", "bullet_index"), &BulletVolley2D::bullet_homing_check_targets_amount);

	ClassDB::bind_method(D_METHOD("bullet_check_has_homing_targets", "bullet_index"), &BulletVolley2D::bullet_check_has_homing_targets);

	ClassDB::bind_method(D_METHOD("bullet_homing_check_current_target_type", "bullet_index"), &BulletVolley2D::bullet_homing_check_current_target_type);

	ClassDB::bind_method(D_METHOD("bullet_get_current_homing_target", "bullet_index"), &BulletVolley2D::bullet_get_current_homing_target);

	ADD_GROUP("Movement Speed", "");
	ClassDB::bind_method(D_METHOD("get_shared_bullet_speed_data"), &BulletVolley2D::get_shared_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_speed_data", "new_speed_data"), &BulletVolley2D::set_shared_bullet_speed_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_speed_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletSpeedData2D"), "set_shared_bullet_speed_data", "get_shared_bullet_speed_data");

	// get/set_inherited_velocity_offset are bound with the methods above.
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "inherited_velocity_offset"), "set_inherited_velocity_offset", "get_inherited_velocity_offset");

	ADD_GROUP("Bullet Rotation", "");

	ClassDB::bind_method(D_METHOD("get_adjust_direction_based_on_rotation"), &BulletVolley2D::get_adjust_direction_based_on_rotation);
	ClassDB::bind_method(D_METHOD("set_adjust_direction_based_on_rotation", "value"), &BulletVolley2D::set_adjust_direction_based_on_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "adjust_direction_based_on_rotation"), "set_adjust_direction_based_on_rotation", "get_adjust_direction_based_on_rotation");

	ClassDB::bind_method(D_METHOD("get_shared_bullet_rotation_data"), &BulletVolley2D::get_shared_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_rotation_data", "new_rotation_data"), &BulletVolley2D::set_shared_bullet_rotation_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_rotation_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletRotationData2D"), "set_shared_bullet_rotation_data", "get_shared_bullet_rotation_data");

	ADD_GROUP("Wobble", "");
	ClassDB::bind_method(D_METHOD("get_shared_bullet_wobble_data"), &BulletVolley2D::get_shared_bullet_wobble_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_wobble_data", "new_wobble_data"), &BulletVolley2D::set_shared_bullet_wobble_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_wobble_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletWobbleData2D"), "set_shared_bullet_wobble_data", "get_shared_bullet_wobble_data");

	ADD_GROUP("Gravity", "");
	ClassDB::bind_method(D_METHOD("get_gravity"), &BulletVolley2D::get_gravity);
	ClassDB::bind_method(D_METHOD("set_gravity", "value"), &BulletVolley2D::set_gravity);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "gravity"), "set_gravity", "get_gravity");

	ClassDB::bind_method(D_METHOD("bullet_get_gravity", "bullet_index"), &BulletVolley2D::bullet_get_gravity);
	ClassDB::bind_method(D_METHOD("bullet_set_gravity", "bullet_index", "value"), &BulletVolley2D::bullet_set_gravity);
	ClassDB::bind_method(D_METHOD("all_bullets_set_gravity", "value", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_gravity, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_get_gravity", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_gravity, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_gravity_delay_sec"), &BulletVolley2D::get_gravity_delay_sec);
	ClassDB::bind_method(D_METHOD("set_gravity_delay_sec", "value"), &BulletVolley2D::set_gravity_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_delay_sec"), "set_gravity_delay_sec", "get_gravity_delay_sec");

	ClassDB::bind_method(D_METHOD("get_gravity_duration_sec"), &BulletVolley2D::get_gravity_duration_sec);
	ClassDB::bind_method(D_METHOD("set_gravity_duration_sec", "value"), &BulletVolley2D::set_gravity_duration_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_duration_sec"), "set_gravity_duration_sec", "get_gravity_duration_sec");

	ClassDB::bind_method(D_METHOD("bullet_get_fall_speed", "bullet_index"), &BulletVolley2D::bullet_get_fall_speed);
	ClassDB::bind_method(D_METHOD("all_bullets_get_fall_speed", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_fall_speed, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_get_gravity_info", "bullet_index"), &BulletVolley2D::debug_get_gravity_info);
	ClassDB::bind_method(D_METHOD("debug_get_bullet_info", "bullet_index"), &BulletVolley2D::debug_get_bullet_info);

	ClassDB::bind_method(D_METHOD("get_linear_drag"), &BulletVolley2D::get_linear_drag);
	ClassDB::bind_method(D_METHOD("set_linear_drag", "value"), &BulletVolley2D::set_linear_drag);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "linear_drag"), "set_linear_drag", "get_linear_drag");

	ADD_GROUP("Bounce and Ricochet", "");
	// BOUNCE / RICOCHET RUNTIME API (spawn-data equivalents, editable live).
	ClassDB::bind_method(D_METHOD("get_bounce_mask"), &BulletVolley2D::get_bounce_mask);
	ClassDB::bind_method(D_METHOD("set_bounce_mask", "value"), &BulletVolley2D::set_bounce_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bounce_mask", PROPERTY_HINT_LAYERS_2D_PHYSICS), "set_bounce_mask", "get_bounce_mask");
	ClassDB::bind_method(D_METHOD("set_bounce_mask_from_array", "array_of_masks"), &BulletVolley2D::set_bounce_mask_from_array);
	ClassDB::bind_method(D_METHOD("get_bounce_tilemap_layers"), &BulletVolley2D::get_bounce_tilemap_layers);
	ClassDB::bind_method(D_METHOD("set_bounce_tilemap_layers", "value"), &BulletVolley2D::set_bounce_tilemap_layers);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_tilemap_layers"), "set_bounce_tilemap_layers", "get_bounce_tilemap_layers");
	ClassDB::bind_method(D_METHOD("get_bounce_strength"), &BulletVolley2D::get_bounce_strength);
	ClassDB::bind_method(D_METHOD("set_bounce_strength", "value"), &BulletVolley2D::set_bounce_strength);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_strength"), "set_bounce_strength", "get_bounce_strength");
	ClassDB::bind_method(D_METHOD("get_bounce_push_assist"), &BulletVolley2D::get_bounce_push_assist);
	ClassDB::bind_method(D_METHOD("set_bounce_push_assist", "value"), &BulletVolley2D::set_bounce_push_assist);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_push_assist"), "set_bounce_push_assist", "get_bounce_push_assist");
	ClassDB::bind_method(D_METHOD("get_bounce_charge_amplify"), &BulletVolley2D::get_bounce_charge_amplify);
	ClassDB::bind_method(D_METHOD("set_bounce_charge_amplify", "value"), &BulletVolley2D::set_bounce_charge_amplify);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_charge_amplify"), "set_bounce_charge_amplify", "get_bounce_charge_amplify");
	ClassDB::bind_method(D_METHOD("get_bounce_hit_consumed"), &BulletVolley2D::get_bounce_hit_consumed);
	ClassDB::bind_method(D_METHOD("set_bounce_hit_consumed", "value"), &BulletVolley2D::set_bounce_hit_consumed);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_hit_consumed"), "set_bounce_hit_consumed", "get_bounce_hit_consumed");
	ClassDB::bind_method(D_METHOD("get_bounce_max_count"), &BulletVolley2D::get_bounce_max_count);
	ClassDB::bind_method(D_METHOD("set_bounce_max_count", "value"), &BulletVolley2D::set_bounce_max_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bounce_max_count", PROPERTY_HINT_RANGE, "0,1000000,1"), "set_bounce_max_count", "get_bounce_max_count");
	ClassDB::bind_method(D_METHOD("get_bounce_mode"), &BulletVolley2D::get_bounce_mode);
	ClassDB::bind_method(D_METHOD("set_bounce_mode", "value"), &BulletVolley2D::set_bounce_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bounce_mode", PROPERTY_HINT_ENUM, "Simple Radial,Precise Shape"), "set_bounce_mode", "get_bounce_mode");
	ClassDB::bind_method(D_METHOD("get_bounce_rotate_texture"), &BulletVolley2D::get_bounce_rotate_texture);
	ClassDB::bind_method(D_METHOD("set_bounce_rotate_texture", "value"), &BulletVolley2D::set_bounce_rotate_texture);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_rotate_texture"), "set_bounce_rotate_texture", "get_bounce_rotate_texture");
	ClassDB::bind_method(D_METHOD("get_bounce_rotation_smooth"), &BulletVolley2D::get_bounce_rotation_smooth);
	ClassDB::bind_method(D_METHOD("set_bounce_rotation_smooth", "value"), &BulletVolley2D::set_bounce_rotation_smooth);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_rotation_smooth"), "set_bounce_rotation_smooth", "get_bounce_rotation_smooth");
	ClassDB::bind_method(D_METHOD("get_bounce_randomness_deg"), &BulletVolley2D::get_bounce_randomness_deg);
	ClassDB::bind_method(D_METHOD("set_bounce_randomness_deg", "value"), &BulletVolley2D::set_bounce_randomness_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_randomness_deg", PROPERTY_HINT_RANGE, "0,180,0.1"), "set_bounce_randomness_deg", "get_bounce_randomness_deg");
	ClassDB::bind_method(D_METHOD("get_bounce_cooldown_sec"), &BulletVolley2D::get_bounce_cooldown_sec);
	ClassDB::bind_method(D_METHOD("set_bounce_cooldown_sec", "value"), &BulletVolley2D::set_bounce_cooldown_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_cooldown_sec"), "set_bounce_cooldown_sec", "get_bounce_cooldown_sec");
	ClassDB::bind_method(D_METHOD("get_bounce_debounce_sec"), &BulletVolley2D::get_bounce_debounce_sec);
	ClassDB::bind_method(D_METHOD("set_bounce_debounce_sec", "value"), &BulletVolley2D::set_bounce_debounce_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_debounce_sec"), "set_bounce_debounce_sec", "get_bounce_debounce_sec");

	ClassDB::bind_method(D_METHOD("bullet_get_bounce_count", "bullet_index"), &BulletVolley2D::bullet_get_bounce_count);
	ClassDB::bind_method(D_METHOD("all_bullets_get_bounce_count", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_bounce_count, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_get_bounce_info", "bullet_index"), &BulletVolley2D::debug_get_bounce_info);
	ClassDB::bind_method(D_METHOD("debug_get_previous_origin", "bullet_index"), &BulletVolley2D::debug_get_previous_origin);

	ClassDB::bind_method(D_METHOD("get_is_wobble_enabled"), &BulletVolley2D::get_is_wobble_enabled);
	ClassDB::bind_method(D_METHOD("bullet_get_wobble_amplitude", "bullet_index"), &BulletVolley2D::bullet_get_wobble_amplitude);
	ClassDB::bind_method(D_METHOD("bullet_get_wobble_face_movement_direction", "bullet_index"), &BulletVolley2D::bullet_get_wobble_face_movement_direction);
	ClassDB::bind_method(D_METHOD("bullet_get_wobble_face_rotation_speed", "bullet_index"), &BulletVolley2D::bullet_get_wobble_face_rotation_speed);
	ClassDB::bind_method(D_METHOD("bullet_set_wobble_data", "bullet_index", "wobble_data"), &BulletVolley2D::bullet_set_wobble_data);
	ClassDB::bind_method(D_METHOD("all_bullets_set_wobble_data", "wobble_data", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_wobble_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("bullet_get_wobble_data", "bullet_index"), &BulletVolley2D::bullet_get_wobble_data);
	ClassDB::bind_method(D_METHOD("all_bullets_get_wobble_data", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_wobble_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("has_shared_bullet_wobble_data"), &BulletVolley2D::has_shared_bullet_wobble_data);
	ClassDB::bind_method(D_METHOD("remove_shared_bullet_wobble_data"), &BulletVolley2D::remove_shared_bullet_wobble_data);
	ClassDB::bind_method(D_METHOD("debug_get_wobble_info", "bullet_index"), &BulletVolley2D::debug_get_wobble_info);

	// ORBITING RELATED

	ClassDB::bind_method(D_METHOD("bullet_enable_orbiting", "bullet_index", "orbiting_radius", "orbiting_direction", "orbiting_texture_rotation", "orbiting_follow_mode", "orbiting_follow_deadzone", "orbiting_lock_policy", "orbiting_rigid_follow"), &BulletVolley2D::bullet_enable_orbiting, DEFVAL(OrbitRight), DEFVAL(FaceTarget), DEFVAL(FollowTarget), DEFVAL(0.0), DEFVAL(RelockAlways), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("bullet_disable_orbiting", "bullet_index"), &BulletVolley2D::bullet_disable_orbiting);
	ClassDB::bind_method(D_METHOD("bullet_is_orbiting_enabled", "bullet_index"), &BulletVolley2D::bullet_is_orbiting_enabled);
	ClassDB::bind_method(D_METHOD("bullet_is_orbiting_locked", "bullet_index"), &BulletVolley2D::bullet_is_orbiting_locked);
	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_center", "bullet_index"), &BulletVolley2D::bullet_get_orbiting_center);
	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_angle", "bullet_index"), &BulletVolley2D::bullet_get_orbiting_angle);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_center", "bullet_index", "new_center"), &BulletVolley2D::bullet_set_orbiting_center);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_radius", "bullet_index"), &BulletVolley2D::bullet_get_orbiting_radius);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_radius", "bullet_index", "new_radius"), &BulletVolley2D::bullet_set_orbiting_radius);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_texture_rotation", "bullet_index"), &BulletVolley2D::bullet_get_orbiting_texture_rotation);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_texture_rotation", "bullet_index", "new_texture_rotation"), &BulletVolley2D::bullet_set_orbiting_texture_rotation);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_direction", "bullet_index"), &BulletVolley2D::bullet_get_orbiting_direction);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_direction", "bullet_index", "new_direction"), &BulletVolley2D::bullet_set_orbiting_direction);

	ClassDB::bind_method(D_METHOD("bullet_replace_homing_targets_with_new_target", "bullet_index", "node2d_or_global_position"), &BulletVolley2D::bullet_replace_homing_targets_with_new_target);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_follow_mode", "bullet_index"), &BulletVolley2D::bullet_get_orbiting_follow_mode);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_follow_mode", "bullet_index", "new_follow_mode"), &BulletVolley2D::bullet_set_orbiting_follow_mode);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_follow_deadzone", "bullet_index"), &BulletVolley2D::bullet_get_orbiting_follow_deadzone);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_follow_deadzone", "bullet_index", "new_deadzone"), &BulletVolley2D::bullet_set_orbiting_follow_deadzone);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_lock_policy", "bullet_index"), &BulletVolley2D::bullet_get_orbiting_lock_policy);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_lock_policy", "bullet_index", "new_lock_policy"), &BulletVolley2D::bullet_set_orbiting_lock_policy);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_rigid_follow", "bullet_index"), &BulletVolley2D::bullet_get_orbiting_rigid_follow);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_rigid_follow", "bullet_index", "new_rigid_follow"), &BulletVolley2D::bullet_set_orbiting_rigid_follow);
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_rigid_follow", "new_rigid_follow", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_orbiting_rigid_follow, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_enable_orbiting", "orbiting_radius", "orbiting_direction", "orbiting_texture_rotation", "bullet_index_start", "bullet_index_end_inclusive", "orbiting_follow_mode", "orbiting_follow_deadzone", "orbiting_lock_policy", "orbiting_rigid_follow"), &BulletVolley2D::all_bullets_enable_orbiting, DEFVAL(OrbitRight), DEFVAL(FaceTarget), DEFVAL(0), DEFVAL(-1), DEFVAL(FollowTarget), DEFVAL(0.0), DEFVAL(RelockAlways), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("all_bullets_enable_orbiting_linear", "radius_start", "radius_step", "orbiting_direction", "orbiting_texture_rotation", "bullet_index_start", "bullet_index_end_inclusive", "orbiting_follow_mode", "orbiting_follow_deadzone", "orbiting_lock_policy", "orbiting_rigid_follow"), &BulletVolley2D::all_bullets_enable_orbiting_linear, DEFVAL(OrbitRight), DEFVAL(FaceTarget), DEFVAL(0), DEFVAL(-1), DEFVAL(FollowTarget), DEFVAL(0.0), DEFVAL(RelockAlways), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("all_bullets_get_orbiting_radius", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_orbiting_radius, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_get_orbiting_center", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_orbiting_center, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_get_orbiting_angle", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_orbiting_angle, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_get_orbiting_info", "bullet_index"), &BulletVolley2D::debug_get_orbiting_info);
	ClassDB::bind_method(D_METHOD("all_bullets_get_homing_targets_amount", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_homing_targets_amount, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_get_curves_info", "bullet_index"), &BulletVolley2D::debug_get_curves_info);
	ClassDB::bind_method(D_METHOD("debug_get_pattern_info", "bullet_index"), &BulletVolley2D::debug_get_pattern_info);
	ClassDB::bind_method(D_METHOD("all_bullets_is_orbiting_enabled", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_is_orbiting_enabled, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_is_orbiting_locked", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_is_orbiting_locked, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_disable_orbiting", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_disable_orbiting, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_radius", "new_radius", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_orbiting_radius, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_direction", "new_direction", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_orbiting_direction, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_texture_rotation", "new_rotation", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_orbiting_texture_rotation, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_follow_mode", "new_follow_mode", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_orbiting_follow_mode, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_follow_deadzone", "new_deadzone", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_orbiting_follow_deadzone, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_lock_policy", "new_lock_policy", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_orbiting_lock_policy, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_center", "new_center", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_orbiting_center, DEFVAL(0), DEFVAL(-1));

	// OTHER USEFUL METHODS
	ClassDB::bind_method(D_METHOD("teleport_bullet", "bullet_index", "new_global_pos"), &BulletVolley2D::teleport_bullet);
	ClassDB::bind_method(D_METHOD("teleport_shift_bullet", "bullet_index", "shift_value"), &BulletVolley2D::teleport_shift_bullet);
	ClassDB::bind_method(D_METHOD("teleport_shift_all_bullets", "shift_value", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::teleport_shift_all_bullets, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("bullet_set_velocity", "bullet_index", "new_velocity"), &BulletVolley2D::bullet_set_velocity);
	ClassDB::bind_method(D_METHOD("all_bullets_set_velocity", "new_velocity", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_velocity, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_get_velocity", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_velocity, DEFVAL(0), DEFVAL(-1));

	// SHARED MOVEMENT PATTERN RUNTIME API (spawn-data equivalent, editable live).

	// SHARED MOVEMENT PATTERN RUNTIME API (spawn-data equivalent, editable live).
	ADD_GROUP("Movement Pattern Paths", "");
	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_curve"), &BulletVolley2D::get_shared_movement_pattern_curve);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_curve", "new_curve"), &BulletVolley2D::set_shared_movement_pattern_curve);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_movement_pattern_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve2D"), "set_shared_movement_pattern_curve", "get_shared_movement_pattern_curve");

	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_face_movement_direction"), &BulletVolley2D::get_shared_movement_pattern_face_movement_direction);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_face_movement_direction", "value"), &BulletVolley2D::set_shared_movement_pattern_face_movement_direction);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_movement_pattern_face_movement_direction"), "set_shared_movement_pattern_face_movement_direction", "get_shared_movement_pattern_face_movement_direction");

	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_repeat"), &BulletVolley2D::get_shared_movement_pattern_repeat);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_repeat", "value"), &BulletVolley2D::set_shared_movement_pattern_repeat);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_movement_pattern_repeat"), "set_shared_movement_pattern_repeat", "get_shared_movement_pattern_repeat");

	ADD_GROUP("Homing", "");
	ClassDB::bind_method(D_METHOD("get_bullet_homing_auto_pop_after_target_reached"), &BulletVolley2D::get_bullet_homing_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_bullet_homing_auto_pop_after_target_reached", "value"), &BulletVolley2D::set_bullet_homing_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bullet_homing_auto_pop_after_target_reached"), "set_bullet_homing_auto_pop_after_target_reached", "get_bullet_homing_auto_pop_after_target_reached");

	// SHARED HOMING DEQUE POP METHODS
	ClassDB::bind_method(D_METHOD("shared_homing_deque_pop_front_target"), &BulletVolley2D::shared_homing_deque_pop_front_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_pop_back_target"), &BulletVolley2D::shared_homing_deque_pop_back_target);

	// SHARED HOMING DEQUE PUSH METHODS
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_front_mouse_position_target"), &BulletVolley2D::shared_homing_deque_push_front_mouse_position_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_front_node2d_target", "new_homing_target"), &BulletVolley2D::shared_homing_deque_push_front_node2d_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_front_global_position_target", "global_position"), &BulletVolley2D::shared_homing_deque_push_front_global_position_target);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_back_mouse_position_target"), &BulletVolley2D::shared_homing_deque_push_back_mouse_position_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_back_node2d_target", "new_homing_target"), &BulletVolley2D::shared_homing_deque_push_back_node2d_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_back_global_position_target", "global_position"), &BulletVolley2D::shared_homing_deque_push_back_global_position_target);

	// SHARED HOMING DEQUE HELPERS
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_front_homing_targets_array", "node2ds_or_global_positions_array"), &BulletVolley2D::shared_homing_deque_push_front_homing_targets_array);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_back_homing_targets_array", "node2ds_or_global_positions_array"), &BulletVolley2D::shared_homing_deque_push_back_homing_targets_array);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_replace_homing_targets_with_new_target", "node2d_or_global_position"), &BulletVolley2D::shared_homing_deque_replace_homing_targets_with_new_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_replace_homing_targets_with_new_target_array", "node2ds_or_global_positions_array"), &BulletVolley2D::shared_homing_deque_replace_homing_targets_with_new_target_array);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_clear_homing_targets"), &BulletVolley2D::shared_homing_deque_clear_homing_targets);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_check_homing_targets_amount"), &BulletVolley2D::shared_homing_deque_check_homing_targets_amount);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_check_has_homing_targets"), &BulletVolley2D::shared_homing_deque_check_has_homing_targets);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_check_current_target_type"), &BulletVolley2D::shared_homing_deque_check_current_target_type);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_get_current_homing_target"), &BulletVolley2D::shared_homing_deque_get_current_homing_target);

	ClassDB::bind_method(D_METHOD("get_shared_homing_deque_auto_pop_after_target_reached"), &BulletVolley2D::get_shared_homing_deque_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_shared_homing_deque_auto_pop_after_target_reached", "value"), &BulletVolley2D::set_shared_homing_deque_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_homing_deque_auto_pop_after_target_reached"), "set_shared_homing_deque_auto_pop_after_target_reached", "get_shared_homing_deque_auto_pop_after_target_reached");

	// OTHER HOMING RELATED

	ClassDB::bind_method(D_METHOD("get_homing_distance_before_reached"), &BulletVolley2D::get_homing_distance_before_reached);
	ClassDB::bind_method(D_METHOD("set_homing_distance_before_reached", "value"), &BulletVolley2D::set_homing_distance_before_reached);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_distance_before_reached"), "set_homing_distance_before_reached", "get_homing_distance_before_reached");

	ClassDB::bind_method(D_METHOD("get_homing_smoothing"), &BulletVolley2D::get_homing_smoothing);
	ClassDB::bind_method(D_METHOD("set_homing_smoothing", "value"), &BulletVolley2D::set_homing_smoothing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing"), "set_homing_smoothing", "get_homing_smoothing");

	ClassDB::bind_method(D_METHOD("bullet_get_homing_smoothing", "bullet_index"), &BulletVolley2D::bullet_get_homing_smoothing);
	ClassDB::bind_method(D_METHOD("bullet_set_homing_smoothing", "bullet_index", "value"), &BulletVolley2D::bullet_set_homing_smoothing);
	ClassDB::bind_method(D_METHOD("all_bullets_set_homing_smoothing", "value", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_homing_smoothing, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_get_homing_smoothing", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_homing_smoothing, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("clear_per_bullet_homing_smoothing"), &BulletVolley2D::clear_per_bullet_homing_smoothing);

	ClassDB::bind_method(D_METHOD("get_homing_update_interval"), &BulletVolley2D::get_homing_update_interval);
	ClassDB::bind_method(D_METHOD("set_homing_update_interval", "value"), &BulletVolley2D::set_homing_update_interval);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_update_interval"), "set_homing_update_interval", "get_homing_update_interval");

	ClassDB::bind_method(D_METHOD("get_homing_take_control_of_texture_rotation"), &BulletVolley2D::get_homing_take_control_of_texture_rotation);
	ClassDB::bind_method(D_METHOD("set_homing_take_control_of_texture_rotation", "value"), &BulletVolley2D::set_homing_take_control_of_texture_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_take_control_of_texture_rotation"), "set_homing_take_control_of_texture_rotation", "get_homing_take_control_of_texture_rotation");

	ClassDB::bind_method(D_METHOD("get_homing_delay_sec"), &BulletVolley2D::get_homing_delay_sec);
	ClassDB::bind_method(D_METHOD("set_homing_delay_sec", "value"), &BulletVolley2D::set_homing_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_delay_sec"), "set_homing_delay_sec", "get_homing_delay_sec");

	ClassDB::bind_method(D_METHOD("get_homing_duration_sec"), &BulletVolley2D::get_homing_duration_sec);
	ClassDB::bind_method(D_METHOD("set_homing_duration_sec", "value"), &BulletVolley2D::set_homing_duration_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_duration_sec"), "set_homing_duration_sec", "get_homing_duration_sec");

	ClassDB::bind_method(D_METHOD("get_homing_lose_range_px"), &BulletVolley2D::get_homing_lose_range_px);
	ClassDB::bind_method(D_METHOD("set_homing_lose_range_px", "value"), &BulletVolley2D::set_homing_lose_range_px);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_lose_range_px"), "set_homing_lose_range_px", "get_homing_lose_range_px");

	ClassDB::bind_method(D_METHOD("has_shared_movement_pattern"), &BulletVolley2D::has_shared_movement_pattern);
	ClassDB::bind_method(D_METHOD("remove_shared_movement_pattern"), &BulletVolley2D::remove_shared_movement_pattern);

	// SHARED SPEED / ROTATION RUNTIME API.

	ClassDB::bind_method(D_METHOD("has_shared_bullet_speed_data"), &BulletVolley2D::has_shared_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("remove_shared_bullet_speed_data"), &BulletVolley2D::remove_shared_bullet_speed_data);

	ClassDB::bind_method(D_METHOD("has_shared_bullet_rotation_data"), &BulletVolley2D::has_shared_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("remove_shared_bullet_rotation_data"), &BulletVolley2D::remove_shared_bullet_rotation_data);

	BIND_ENUM_CONSTANT(GlobalPositionTarget);
	BIND_ENUM_CONSTANT(Node2DTarget);
	BIND_ENUM_CONSTANT(MousePositionTarget);
	BIND_ENUM_CONSTANT(NotHoming);

	BIND_ENUM_CONSTANT(DontMove);
	BIND_ENUM_CONSTANT(OrbitLeft);
	BIND_ENUM_CONSTANT(OrbitRight);
	BIND_ENUM_CONSTANT(OrbitRandom);

	BIND_ENUM_CONSTANT(FaceTarget);
	BIND_ENUM_CONSTANT(FaceOppositeTarget);
	BIND_ENUM_CONSTANT(FaceOrbitingDirection);
	BIND_ENUM_CONSTANT(FaceOppositeOrbitingDirection);

	BIND_ENUM_CONSTANT(FollowTarget);
	BIND_ENUM_CONSTANT(FollowDeadzone);
	BIND_ENUM_CONSTANT(Anchored);

	BIND_ENUM_CONSTANT(RelockAlways);
	BIND_ENUM_CONSTANT(StayLocked);
	BIND_ENUM_CONSTANT(RelockOnTargetChange);

	// NOTE: signal args use PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) so the
	// class name reaches ClassDB and --doctool; see the note on the factory
	// signals.
	ADD_SIGNAL(MethodInfo("bullet_homing_target_reached",
			PropertyInfo(Variant::OBJECT, "multimesh_instance", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index"),
			PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node2D"),
			PropertyInfo(Variant::VECTOR2, "target_global_position")));
	ADD_GROUP("Custom Data", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullets_custom_data"), "set_shared_bullets_custom_data", "get_shared_bullets_custom_data");

	ClassDB::bind_method(D_METHOD("bullet_get_custom_data", "bullet_index"), &BulletVolley2D::bullet_get_custom_data);
	ClassDB::bind_method(D_METHOD("bullet_set_custom_data", "bullet_index", "new_custom_data"), &BulletVolley2D::bullet_set_custom_data);
	ClassDB::bind_method(D_METHOD("all_bullets_get_custom_data", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_custom_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_custom_data", "new_custom_data", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_custom_data, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_is_life_time_infinite"), &BulletVolley2D::get_is_life_time_infinite);
	ClassDB::bind_method(D_METHOD("get_fade_in_sec"), &BulletVolley2D::get_fade_in_sec);
	ClassDB::bind_method(D_METHOD("set_fade_in_sec", "value"), &BulletVolley2D::set_fade_in_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_in_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_fade_in_sec", "get_fade_in_sec");
	ClassDB::bind_method(D_METHOD("get_fade_out_sec"), &BulletVolley2D::get_fade_out_sec);
	ClassDB::bind_method(D_METHOD("set_fade_out_sec", "value"), &BulletVolley2D::set_fade_out_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_out_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_fade_out_sec", "get_fade_out_sec");
	ClassDB::bind_method(D_METHOD("get_modulate_ramp"), &BulletVolley2D::get_modulate_ramp);
	ClassDB::bind_method(D_METHOD("set_modulate_ramp", "value"), &BulletVolley2D::set_modulate_ramp);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "modulate_ramp", PROPERTY_HINT_RESOURCE_TYPE, "Gradient"), "set_modulate_ramp", "get_modulate_ramp");
	ClassDB::bind_method(D_METHOD("get_fade_base_modulate"), &BulletVolley2D::get_fade_base_modulate);
	ClassDB::bind_method(D_METHOD("set_fade_base_modulate", "value"), &BulletVolley2D::set_fade_base_modulate);
	ClassDB::bind_method(D_METHOD("get_override_frame_color"), &BulletVolley2D::get_override_frame_color);
	ClassDB::bind_method(D_METHOD("set_override_frame_color", "value"), &BulletVolley2D::set_override_frame_color);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "override_frame_color"), "set_override_frame_color", "get_override_frame_color");
	ClassDB::bind_method(D_METHOD("set_is_life_time_infinite", "value"), &BulletVolley2D::set_is_life_time_infinite);
	ClassDB::bind_method(D_METHOD("get_life_time_left"), &BulletVolley2D::get_life_time_left);
	ClassDB::bind_method(D_METHOD("set_life_time_left", "seconds"), &BulletVolley2D::set_life_time_left);
	ADD_GROUP("Lifetime", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_life_time_infinite"), "set_is_life_time_infinite", "get_is_life_time_infinite");

	// Time based functions
	ClassDB::bind_method(D_METHOD("attach_time_based_function", "time", "callable", "repeat", "execute_only_if_volley_is_active"), &BulletVolley2D::attach_time_based_function, DEFVAL(false), DEFVAL(true));

	ClassDB::bind_method(D_METHOD("detach_time_based_function", "callable"), &BulletVolley2D::detach_time_based_function);

	ClassDB::bind_method(D_METHOD("detach_all_time_based_functions"), &BulletVolley2D::detach_all_time_based_functions);

	ClassDB::bind_method(D_METHOD("get_is_auto_pooling_enabled"), &BulletVolley2D::get_is_auto_pooling_enabled);
	ClassDB::bind_method(D_METHOD("set_is_auto_pooling_enabled", "value"), &BulletVolley2D::set_is_auto_pooling_enabled);
	ADD_GROUP("Pooling", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_auto_pooling_enabled"), "set_is_auto_pooling_enabled", "get_is_auto_pooling_enabled");

	ClassDB::bind_method(D_METHOD("get_is_attachments_auto_pooling_enabled"), &BulletVolley2D::get_is_attachments_auto_pooling_enabled);
	ClassDB::bind_method(D_METHOD("set_is_attachments_auto_pooling_enabled", "value"), &BulletVolley2D::set_is_attachments_auto_pooling_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_attachments_auto_pooling_enabled"), "set_is_attachments_auto_pooling_enabled", "get_is_attachments_auto_pooling_enabled");

	ClassDB::bind_method(D_METHOD("reset_pooling_flags_to_default"), &BulletVolley2D::reset_pooling_flags_to_default);
	// Manual-pooling reseed: reactivates a fully-disabled volley with fresh
	// spawn data (same amount_bullets required; same-shape only inside physics
	// sweeps, shape-type changes need idle via call_deferred). Fully validated:
	// refuses on active/queued/wrong-type/NaN-offset input without touching
	// state. This is the cross-owner reuse path for manual poolers.
	ClassDB::bind_method(D_METHOD("enable_volley", "data", "inherited_velocity_offset", "spawner_id"), &BulletVolley2D::enable_volley_for_script, DEFVAL(Vector2(0, 0)), DEFVAL(0));

	// Collision
	ClassDB::bind_method(D_METHOD("get_bullet_max_collision_count"), &BulletVolley2D::get_bullet_max_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullet_max_collision_count", "value"), &BulletVolley2D::set_bullet_max_collision_count);
	ADD_GROUP("Collision", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bullet_max_collision_count"), "set_bullet_max_collision_count", "get_bullet_max_collision_count");

	ClassDB::bind_method(D_METHOD("get_bullet_collision_count", "bullet_index"), &BulletVolley2D::get_bullet_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullet_collision_count", "bullet_index", "value"), &BulletVolley2D::set_bullet_collision_count);
	ClassDB::bind_method(D_METHOD("get_bullets_current_collision_count"), &BulletVolley2D::get_bullets_current_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullets_current_collision_count_no_return", "arr"), &BulletVolley2D::set_bullets_current_collision_count_no_return);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bullets_current_collision_count", PROPERTY_HINT_ARRAY_TYPE, "int"), "set_bullets_current_collision_count_no_return", "get_bullets_current_collision_count");

	ClassDB::bind_method(D_METHOD("is_rotation_data_active"), &BulletVolley2D::get_is_rotation_data_active);
	ClassDB::bind_method(D_METHOD("bullet_get_rotation_speed", "bullet_index"), &BulletVolley2D::bullet_get_rotation_speed);

	ClassDB::bind_method(D_METHOD("get_collision_layer"), &BulletVolley2D::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "new_collision_layer"), &BulletVolley2D::set_collision_layer);

	ClassDB::bind_method(D_METHOD("get_collision_mask"), &BulletVolley2D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "new_collision_mask"), &BulletVolley2D::set_collision_mask);

	ClassDB::bind_method(D_METHOD("set_collision_layer_from_array", "array_of_layers"), &BulletVolley2D::set_collision_layer_from_array);
	ClassDB::bind_method(D_METHOD("set_collision_mask_from_array", "array_of_masks"), &BulletVolley2D::set_collision_mask_from_array);

	ClassDB::bind_method(D_METHOD("get_monitorable"), &BulletVolley2D::get_monitorable);
	ClassDB::bind_method(D_METHOD("set_monitorable", "value"), &BulletVolley2D::set_monitorable);

	ClassDB::bind_method(D_METHOD("get_collision_shape"), &BulletVolley2D::get_collision_shape);
	ClassDB::bind_method(D_METHOD("set_collision_shape_runtime", "new_shape"), &BulletVolley2D::set_collision_shape_runtime);
	ClassDB::bind_method(D_METHOD("set_collision_shape_runtime_deferred", "new_shape"), &BulletVolley2D::set_collision_shape_runtime_deferred);

	//

	ClassDB::bind_method(D_METHOD("bullet_get_attachment", "bullet_index"), &BulletVolley2D::bullet_get_attachment);
	ClassDB::bind_method(D_METHOD("bullet_set_attachment_to_null", "bullet_index"), &BulletVolley2D::bullet_set_attachment_to_null);

	ClassDB::bind_method(D_METHOD("all_bullets_get_attachments", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_attachments, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_attachment_to_null", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_attachment_to_null, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_attachment", "attachment_scene", "bullet_attachment_offset", "stick_relative_to_bullet", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_attachment, DEFVAL(true), DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("bullet_set_attachment", "bullet_index", "attachment_scene", "bullet_attachment_offset", "stick_relative_to_bullet"), &BulletVolley2D::bullet_set_attachment, DEFVAL(Vector2(0, 0)), DEFVAL(true));

	ClassDB::bind_method(D_METHOD("set_shared_bullet_curves_data", "data"), &BulletVolley2D::set_shared_bullet_curves_data);
	ClassDB::bind_method(D_METHOD("get_shared_bullet_curves_data"), &BulletVolley2D::get_shared_bullet_curves_data);

	ClassDB::bind_method(D_METHOD("get_collision_dedup_by_object"), &BulletVolley2D::get_collision_dedup_by_object);
	ClassDB::bind_method(D_METHOD("set_collision_dedup_by_object", "value"), &BulletVolley2D::set_collision_dedup_by_object);
	ClassDB::bind_method(D_METHOD("get_emit_collision_signals"), &BulletVolley2D::get_emit_collision_signals);
	ClassDB::bind_method(D_METHOD("set_emit_collision_signals", "value"), &BulletVolley2D::set_emit_collision_signals);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collision_dedup_by_object"), "set_collision_dedup_by_object", "get_collision_dedup_by_object");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "emit_collision_signals"), "set_emit_collision_signals", "get_emit_collision_signals");

	ADD_GROUP("Curves", "");
	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "shared_bullet_curves_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletCurvesData2D"),
			"set_shared_bullet_curves_data", "get_shared_bullet_curves_data");

	ClassDB::bind_method(D_METHOD("has_shared_bullet_curves_data"), &BulletVolley2D::has_shared_bullet_curves_data);
	ClassDB::bind_method(D_METHOD("remove_shared_bullet_curves_data"), &BulletVolley2D::remove_shared_bullet_curves_data);

	ClassDB::bind_method(D_METHOD("bullet_set_curves_data", "bullet_index", "data"), &BulletVolley2D::bullet_set_curves_data);
	ClassDB::bind_method(D_METHOD("bullet_get_curves_data", "bullet_index"), &BulletVolley2D::bullet_get_curves_data);
	ClassDB::bind_method(D_METHOD("clear_per_bullet_curves_data", "bullet_index"), &BulletVolley2D::clear_per_bullet_curves_data);
	ClassDB::bind_method(D_METHOD("all_bullets_get_curves_data", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_get_curves_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_curves_data", "curves_data", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_curves_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_clear_curves_data", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_clear_curves_data, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_curves_elapsed_time"), &BulletVolley2D::get_curves_elapsed_time);
	ClassDB::bind_method(D_METHOD("set_curves_elapsed_time", "new_time"), &BulletVolley2D::set_curves_elapsed_time);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "curves_elapsed_time"), "set_curves_elapsed_time", "get_curves_elapsed_time");

	ClassDB::bind_method(D_METHOD("get_bullet_movement_pattern_curve", "bullet_index"), &BulletVolley2D::get_bullet_movement_pattern_curve);

	ClassDB::bind_method(D_METHOD("set_bullet_movement_pattern_from_path", "bullet_index", "path_holding_pattern", "face_movement_direction", "repeat_pattern"), &BulletVolley2D::set_bullet_movement_pattern_from_path, DEFVAL(false), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("all_bullets_set_movement_pattern_from_path", "path_holding_pattern", "face_movement_direction", "repeat_pattern", "start_index", "end_index_inclusive"), &BulletVolley2D::all_bullets_set_movement_pattern_from_path, DEFVAL(false), DEFVAL(true), DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("set_bullet_movement_pattern_from_curve", "bullet_index", "curve_pattern", "face_movement_direction", "repeat_pattern"), &BulletVolley2D::set_bullet_movement_pattern_from_curve, DEFVAL(false), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("all_bullets_set_movement_pattern_from_curve", "curve_pattern", "face_movement_direction", "repeat_pattern", "start_index", "end_index_inclusive"), &BulletVolley2D::all_bullets_set_movement_pattern_from_curve, DEFVAL(false), DEFVAL(true), DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("remove_bullet_movement_pattern", "bullet_index"), &BulletVolley2D::remove_bullet_movement_pattern);
	ClassDB::bind_method(D_METHOD("all_bullets_remove_movement_pattern", "start_index", "end_index_inclusive"), &BulletVolley2D::all_bullets_remove_movement_pattern, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("has_bullet_movement_pattern", "bullet_index"), &BulletVolley2D::check_exists_bullet_movement_pattern_data);

	// Sprite effect layers (stackable trails + one-shot spawn/hit/destroy/
	// bounce visuals). Configured on the spawn data, mirrored live here:
	// editing the array rebakes without a spawn flash.
	ADD_GROUP("Sprite Effects", "");
	ClassDB::bind_method(D_METHOD("get_effect_layers"), &BulletVolley2D::get_effect_layers);
	ClassDB::bind_method(D_METHOD("set_effect_layers", "new_layers"), &BulletVolley2D::set_effect_layers);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "effect_layers", PROPERTY_HINT_ARRAY_TYPE, "BulletEffectLayerData2D"), "set_effect_layers", "get_effect_layers");

	ClassDB::bind_method(D_METHOD("has_trail_effects"), &BulletVolley2D::has_trail_effects);
	ClassDB::bind_method(D_METHOD("bullet_set_trail_enabled", "layer_index", "bullet_index", "trail_on"), &BulletVolley2D::bullet_set_trail_enabled);
	ClassDB::bind_method(D_METHOD("all_bullets_set_trail_enabled", "layer_index", "trail_on", "bullet_index_start", "bullet_index_end_inclusive"), &BulletVolley2D::all_bullets_set_trail_enabled, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("play_effect_animation", "layer_index", "animation"), &BulletVolley2D::play_effect_animation);
	ClassDB::bind_method(D_METHOD("debug_get_effect_layers_info"), &BulletVolley2D::debug_get_effect_layers_info);
	ClassDB::bind_method(D_METHOD("debug_get_trail_transform", "layer_index", "bullet_index"), &BulletVolley2D::debug_get_trail_transform);

	// NOTE: signal args use PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) so the
	// class name reaches ClassDB and --doctool; see the note on the factory
	// signals.
	ADD_SIGNAL(MethodInfo("sprite_animation_finished",
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D")));
}

} // namespace BlastBullets2D
