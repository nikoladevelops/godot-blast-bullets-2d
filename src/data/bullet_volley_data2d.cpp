#include "data/bullet_volley_data2d.hpp"
#include "godot_cpp/core/class_db.hpp"
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

int BulletVolleyData2D::calculate_bitmask(const TypedArray<int> &numbers) {
	int bitmask_value = 0;
	for (int i = 0; i < numbers.size(); ++i) {
		int n = static_cast<int>(numbers[i]);
		if (n < 1 || n > 32) {
			UtilityFunctions::push_error("Invalid layer number " + String::num_int64(n) + " in calculate_bitmask. Valid range is 1..32. Ignoring.");
			continue;
		}
		// 1u to avoid signed overflow UB for bit 31 (layer 32)
		bitmask_value |= static_cast<int>(1u << (n - 1));
	}

	return bitmask_value;
}

TypedArray<Transform2D> BulletVolleyData2D::get_transforms() const {
	return transforms;
}
void BulletVolleyData2D::set_transforms(const TypedArray<Transform2D> &new_transforms) {
	transforms = new_transforms;
}

Ref<SpriteFrames> BulletVolleyData2D::get_sprite_frames() const {
	return sprite_frames;
}
void BulletVolleyData2D::set_sprite_frames(const Ref<SpriteFrames> &new_sprite_frames) {
	sprite_frames = new_sprite_frames;
}

StringName BulletVolleyData2D::get_animation() const {
	return animation;
}
void BulletVolleyData2D::set_animation(const StringName &new_animation) {
	animation = new_animation;
}

Vector2 BulletVolleyData2D::get_texture_size() const {
	return texture_size;
}
void BulletVolleyData2D::set_texture_size(Vector2 new_texture_size) {
	// texture_size is fed straight into the multimesh mesh buffer, so a
	// non-finite value would poison every instance transform. Guard here as
	// well as in resolve_quad_size (which also covers hand-built state).
	if (!new_texture_size.is_finite() || new_texture_size.x < 0.0 || new_texture_size.y < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: texture_size must be finite and >= 0, keeping the old value.");
		return;
	}
	texture_size = new_texture_size;
}

real_t BulletVolleyData2D::get_texture_rotation_radians() const {
	return texture_rotation_radians;
}
void BulletVolleyData2D::set_texture_rotation_radians(real_t new_texture_rotation_radians) {
	// Highest-impact guard in this file: this value is added to EVERY bullet's
	// transform rotation in generate_texture_transform, so a NaN here makes
	// the whole volley NaN - and validate_spawn_data only inspects
	// transforms[], so the spawn used to succeed and produce broken bullets.
	if (!Math::is_finite(new_texture_rotation_radians)) {
		UtilityFunctions::push_error("BulletVolleyData2D: texture_rotation_radians must be finite, keeping the old value.");
		return;
	}
	texture_rotation_radians = new_texture_rotation_radians;
}

int BulletVolleyData2D::get_collision_layer() const {
	return collision_layer;
}
void BulletVolleyData2D::set_collision_layer(int new_collision_layer) {
	collision_layer = new_collision_layer;
}

void BulletVolleyData2D::set_collision_layer_from_array(const TypedArray<int> &numbers) {
	int bitmask = calculate_bitmask(numbers);
	collision_layer = bitmask;
}

int BulletVolleyData2D::get_collision_mask() const {
	return collision_mask;
}
void BulletVolleyData2D::set_collision_mask(int new_collision_mask) {
	collision_mask = new_collision_mask;
}

void BulletVolleyData2D::set_collision_mask_from_array(const TypedArray<int> &numbers) {
	int bitmask = calculate_bitmask(numbers);
	collision_mask = bitmask;
}

Ref<Shape2D> BulletVolleyData2D::get_collision_shape() const {
	return collision_shape;
}
void BulletVolleyData2D::set_collision_shape(const Ref<Shape2D> &new_shape) {
	collision_shape = new_shape;
}

Vector2 BulletVolleyData2D::get_collision_shape_offset() const {
	return collision_shape_offset;
}
void BulletVolleyData2D::set_collision_shape_offset(const Vector2 &new_collision_shape_offset) {
	// Offsets every shape transform in generate_collision_shape_transform_for_area,
	// so a non-finite value poisons the physics shape exactly like a bad
	// texture_rotation does the visuals.
	if (!new_collision_shape_offset.is_finite()) {
		UtilityFunctions::push_error("BulletVolleyData2D: collision_shape_offset must be finite, keeping the old value.");
		return;
	}
	collision_shape_offset = new_collision_shape_offset;
}

bool BulletVolleyData2D::get_monitorable() const {
	return monitorable;
}
void BulletVolleyData2D::set_monitorable(bool new_monitorable) {
	monitorable = new_monitorable;
}

Ref<Resource> BulletVolleyData2D::get_shared_bullets_custom_data() const {
	return shared_bullets_custom_data;
}
void BulletVolleyData2D::set_shared_bullets_custom_data(const Ref<Resource> &new_shared_bullets_custom_data) {
	shared_bullets_custom_data = new_shared_bullets_custom_data;
}

TypedArray<Resource> BulletVolleyData2D::get_all_bullets_custom_data() const {
	return all_bullets_custom_data;
}
void BulletVolleyData2D::set_all_bullets_custom_data(const TypedArray<Resource> &new_custom_data) {
	all_bullets_custom_data = new_custom_data;
}

bool BulletVolleyData2D::get_tile_all_bullets_custom_data() const {
	return tile_all_bullets_custom_data;
}
void BulletVolleyData2D::set_tile_all_bullets_custom_data(bool value) {
	tile_all_bullets_custom_data = value;
}

Ref<PackedScene> BulletVolleyData2D::get_shared_bullet_attachment() const {
	return shared_bullet_attachment;
}
void BulletVolleyData2D::set_shared_bullet_attachment(const Ref<PackedScene> &new_attachment) {
	shared_bullet_attachment = new_attachment;
}

Vector2 BulletVolleyData2D::get_shared_bullet_attachment_offset() const {
	return shared_bullet_attachment_offset;
}
void BulletVolleyData2D::set_shared_bullet_attachment_offset(const Vector2 &new_offset) {
	if (!new_offset.is_finite()) {
		UtilityFunctions::push_error("BulletVolleyData2D: shared_bullet_attachment_offset must be finite, keeping the old value.");
		return;
	}
	shared_bullet_attachment_offset = new_offset;
}

bool BulletVolleyData2D::get_shared_bullet_attachment_stick_relative_to_bullet() const {
	return shared_bullet_attachment_stick_relative_to_bullet;
}
void BulletVolleyData2D::set_shared_bullet_attachment_stick_relative_to_bullet(bool value) {
	shared_bullet_attachment_stick_relative_to_bullet = value;
}

TypedArray<BulletEffectLayerData2D> BulletVolleyData2D::get_effect_layers() const {
	return effect_layers;
}
void BulletVolleyData2D::set_effect_layers(const TypedArray<BulletEffectLayerData2D> &new_layers) {
	effect_layers = new_layers;
}

double BulletVolleyData2D::get_max_life_time() const {
	return max_life_time;
}
void BulletVolleyData2D::set_max_life_time(double new_max_life_time) {
	if (!Math::is_finite(new_max_life_time)) {
		UtilityFunctions::push_error("BulletVolleyData2D: max_life_time must be finite, keeping the old value.");
		return;
	}
	if (!is_life_time_infinite && new_max_life_time <= 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: max_life_time must be > 0 when the lifetime is not infinite (or enable is_life_time_infinite), keeping the old value.");
		return;
	}
	max_life_time = new_max_life_time;
}

Ref<Material> BulletVolleyData2D::get_material() const {
	return material;
}
void BulletVolleyData2D::set_material(const Ref<Material> &new_material) {
	material = new_material;
}

Ref<Mesh> BulletVolleyData2D::get_mesh() const {
	return mesh;
}
void BulletVolleyData2D::set_mesh(const Ref<Mesh> &new_mesh) {
	mesh = new_mesh;
}

TypedArray<BulletRotationData2D> BulletVolleyData2D::get_all_bullet_rotation_data() const {
	return all_bullet_rotation_data;
}
void BulletVolleyData2D::set_all_bullet_rotation_data(const TypedArray<BulletRotationData2D> &new_bullet_rotation_data) {
	all_bullet_rotation_data = new_bullet_rotation_data;
}

bool BulletVolleyData2D::get_tile_all_bullet_rotation_data() const {
	return tile_all_bullet_rotation_data;
}
void BulletVolleyData2D::set_tile_all_bullet_rotation_data(bool value) {
	tile_all_bullet_rotation_data = value;
}

bool BulletVolleyData2D::get_rotate_only_textures() const {
	return rotate_only_textures;
}
void BulletVolleyData2D::set_rotate_only_textures(bool new_rotate_only_textures) {
	rotate_only_textures = new_rotate_only_textures;
}

bool BulletVolleyData2D::get_is_texture_rotation_permanent() const {
	return is_texture_rotation_permanent;
}
void BulletVolleyData2D::set_is_texture_rotation_permanent(bool new_is_texture_rotation_permanent) {
	is_texture_rotation_permanent = new_is_texture_rotation_permanent;
}

int BulletVolleyData2D::get_z_index() const {
	return z_index;
}

void BulletVolleyData2D::set_z_index(int new_z_index) {
	z_index = new_z_index;
}

int BulletVolleyData2D::get_light_mask() const {
	return light_mask;
}

void BulletVolleyData2D::set_light_mask(int new_light_mask) {
	light_mask = new_light_mask;
}

void BulletVolleyData2D::set_light_mask_from_array(const TypedArray<int> &numbers) {
	int bitmask = calculate_bitmask(numbers);
	light_mask = bitmask;
}

int BulletVolleyData2D::get_visibility_layer() const {
	return visibility_layer;
}

void BulletVolleyData2D::set_visibility_layer(int new_visibility_layer) {
	visibility_layer = new_visibility_layer;
}

void BulletVolleyData2D::set_visibility_layer_from_array(const TypedArray<int> &numbers) {
	int bitmask = calculate_bitmask(numbers);
	visibility_layer = bitmask;
}

Color BulletVolleyData2D::get_self_modulate() const {
	return self_modulate;
}
void BulletVolleyData2D::set_self_modulate(const Color &new_self_modulate) {
	// Multiplied into every instance tint (fx_refresh_slot_color /
	// set_instance_color), so a NaN channel would spread across the whole
	// volley. Peers in this file (offset, scale, fade_*) already guard.
	// godot-cpp's Color has no is_finite(), so check the channels directly.
	if (!Math::is_finite(new_self_modulate.r) || !Math::is_finite(new_self_modulate.g) ||
			!Math::is_finite(new_self_modulate.b) || !Math::is_finite(new_self_modulate.a)) {
		UtilityFunctions::push_error("BulletVolleyData2D: self_modulate must be finite, keeping the old value.");
		return;
	}
	self_modulate = new_self_modulate;
}
bool BulletVolleyData2D::get_override_frame_color() const {
	return override_frame_color;
}
void BulletVolleyData2D::set_override_frame_color(bool value) {
	override_frame_color = value;
}
double BulletVolleyData2D::get_fade_in_sec() const {
	return fade_in_sec;
}
void BulletVolleyData2D::set_fade_in_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: fade_in_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	fade_in_sec = value;
}
double BulletVolleyData2D::get_fade_out_sec() const {
	return fade_out_sec;
}
void BulletVolleyData2D::set_fade_out_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: fade_out_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	fade_out_sec = value;
}
Ref<Gradient> BulletVolleyData2D::get_modulate_ramp() const {
	return modulate_ramp;
}
void BulletVolleyData2D::set_modulate_ramp(const Ref<Gradient> &value) {
	modulate_ramp = value;
}

Dictionary BulletVolleyData2D::get_instance_shader_parameters() const {
	return instance_shader_parameters;
}

void BulletVolleyData2D::set_instance_shader_parameters(const Dictionary &new_instance_shader_parameters) {
	instance_shader_parameters = new_instance_shader_parameters;
}

bool BulletVolleyData2D::get_is_life_time_over_signal_enabled() const {
	return is_life_time_over_signal_enabled;
}

void BulletVolleyData2D::set_is_life_time_over_signal_enabled(bool new_is_life_time_over_signal_enabled) {
	is_life_time_over_signal_enabled = new_is_life_time_over_signal_enabled;
}

bool BulletVolleyData2D::get_stop_rotation_when_max_reached() const {
	return stop_rotation_when_max_reached;
}

void BulletVolleyData2D::set_stop_rotation_when_max_reached(bool new_stop_rotation_when_max_reached) {
	stop_rotation_when_max_reached = new_stop_rotation_when_max_reached;
}

int BulletVolleyData2D::get_bullet_max_collision_count() const {
	return bullet_max_collision_count;
}

void BulletVolleyData2D::set_bullet_max_collision_count(int new_max_collision_amount) {
	if (new_max_collision_amount < 0) {
		UtilityFunctions::push_error("BulletVolleyData2D: bullet_max_collision_count must be >= 0 (0 = infinite collisions), keeping the old value.");
		return;
	}
	bullet_max_collision_count = new_max_collision_amount;
}

bool BulletVolleyData2D::get_is_life_time_infinite() const {
	return is_life_time_infinite;
}
void BulletVolleyData2D::set_is_life_time_infinite(bool value) {
	is_life_time_infinite = value;
}

TypedArray<int> BulletVolleyData2D::get_bullets_current_collision_count() const {
	return bullets_current_collision_count;
}

void BulletVolleyData2D::set_bullets_current_collision_count(const TypedArray<int> &arr) {
	bullets_current_collision_count = arr;
}

bool BulletVolleyData2D::get_tile_bullets_current_collision_count() const {
	return tile_bullets_current_collision_count;
}
void BulletVolleyData2D::set_tile_bullets_current_collision_count(bool value) {
	tile_bullets_current_collision_count = value;
}

TypedArray<BulletSpeedData2D> BulletVolleyData2D::get_all_bullet_speed_data() const {
	return all_bullet_speed_data;
}
void BulletVolleyData2D::set_all_bullet_speed_data(const TypedArray<BulletSpeedData2D> &new_data) {
	all_bullet_speed_data = new_data;
}

bool BulletVolleyData2D::get_tile_all_bullet_speed_data() const {
	return tile_all_bullet_speed_data;
}
void BulletVolleyData2D::set_tile_all_bullet_speed_data(bool value) {
	tile_all_bullet_speed_data = value;
}

bool BulletVolleyData2D::get_adjust_direction_based_on_rotation() const {
	return adjust_direction_based_on_rotation;
}

void BulletVolleyData2D::set_adjust_direction_based_on_rotation(bool new_adjust_direction_based_on_rotation) {
	adjust_direction_based_on_rotation = new_adjust_direction_based_on_rotation;
}

Ref<BulletSpeedData2D> BulletVolleyData2D::get_shared_bullet_speed_data() const {
	return shared_bullet_speed_data;
}
void BulletVolleyData2D::set_shared_bullet_speed_data(const Ref<BulletSpeedData2D> &new_speed_data) {
	shared_bullet_speed_data = new_speed_data;
}

Ref<BulletRotationData2D> BulletVolleyData2D::get_shared_bullet_rotation_data() const {
	return shared_bullet_rotation_data;
}
void BulletVolleyData2D::set_shared_bullet_rotation_data(const Ref<BulletRotationData2D> &new_rotation_data) {
	shared_bullet_rotation_data = new_rotation_data;
}

Ref<BulletCurvesData2D> BulletVolleyData2D::get_shared_bullet_curves_data() const {
	return shared_bullet_curves_data;
}
void BulletVolleyData2D::set_shared_bullet_curves_data(const Ref<BulletCurvesData2D> &new_curves_data) {
	shared_bullet_curves_data = new_curves_data;
}

NodePath BulletVolleyData2D::get_shared_movement_pattern_path() const {
	return shared_movement_pattern_path;
}
void BulletVolleyData2D::set_shared_movement_pattern_path(const NodePath &new_path) {
	shared_movement_pattern_path = new_path;
}

bool BulletVolleyData2D::get_shared_movement_pattern_face_movement_direction() const {
	return shared_movement_pattern_face_movement_direction;
}
void BulletVolleyData2D::set_shared_movement_pattern_face_movement_direction(bool value) {
	shared_movement_pattern_face_movement_direction = value;
}

bool BulletVolleyData2D::get_shared_movement_pattern_repeat() const {
	return shared_movement_pattern_repeat;
}
void BulletVolleyData2D::set_shared_movement_pattern_repeat(bool value) {
	shared_movement_pattern_repeat = value;
}

TypedArray<BulletCurvesData2D> BulletVolleyData2D::get_all_bullet_curves_data() const {
	return all_bullet_curves_data;
}
void BulletVolleyData2D::set_all_bullet_curves_data(const TypedArray<BulletCurvesData2D> &new_data) {
	all_bullet_curves_data = new_data;
}

bool BulletVolleyData2D::get_tile_all_bullet_curves_data() const {
	return tile_all_bullet_curves_data;
}
void BulletVolleyData2D::set_tile_all_bullet_curves_data(bool value) {
	tile_all_bullet_curves_data = value;
}

TypedArray<NodePath> BulletVolleyData2D::get_all_bullet_movement_pattern_paths() const {
	return all_bullet_movement_pattern_paths;
}
void BulletVolleyData2D::set_all_bullet_movement_pattern_paths(const TypedArray<NodePath> &new_paths) {
	all_bullet_movement_pattern_paths = new_paths;
}

bool BulletVolleyData2D::get_tile_all_bullet_movement_pattern_paths() const {
	return tile_all_bullet_movement_pattern_paths;
}
void BulletVolleyData2D::set_tile_all_bullet_movement_pattern_paths(bool value) {
	tile_all_bullet_movement_pattern_paths = value;
}

TypedArray<bool> BulletVolleyData2D::get_all_bullet_movement_pattern_face_movement_directions() const {
	return all_bullet_movement_pattern_face_movement_directions;
}
void BulletVolleyData2D::set_all_bullet_movement_pattern_face_movement_directions(const TypedArray<bool> &new_flags) {
	all_bullet_movement_pattern_face_movement_directions = new_flags;
}

bool BulletVolleyData2D::get_tile_all_bullet_movement_pattern_face_movement_directions() const {
	return tile_all_bullet_movement_pattern_face_movement_directions;
}
void BulletVolleyData2D::set_tile_all_bullet_movement_pattern_face_movement_directions(bool value) {
	tile_all_bullet_movement_pattern_face_movement_directions = value;
}

TypedArray<bool> BulletVolleyData2D::get_all_bullet_movement_pattern_repeats() const {
	return all_bullet_movement_pattern_repeats;
}
void BulletVolleyData2D::set_all_bullet_movement_pattern_repeats(const TypedArray<bool> &new_flags) {
	all_bullet_movement_pattern_repeats = new_flags;
}

bool BulletVolleyData2D::get_tile_all_bullet_movement_pattern_repeats() const {
	return tile_all_bullet_movement_pattern_repeats;
}
void BulletVolleyData2D::set_tile_all_bullet_movement_pattern_repeats(bool value) {
	tile_all_bullet_movement_pattern_repeats = value;
}

double BulletVolleyData2D::get_homing_smoothing() const {
	return homing_smoothing;
}
void BulletVolleyData2D::set_homing_smoothing(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: homing_smoothing must be finite and >= 0 (0 snaps instantly), keeping the old value.");
		return;
	}
	homing_smoothing = value;
}

double BulletVolleyData2D::get_homing_update_interval() const {
	return homing_update_interval;
}
void BulletVolleyData2D::set_homing_update_interval(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: homing_update_interval must be finite and >= 0 (0 refreshes every tick), keeping the old value.");
		return;
	}
	homing_update_interval = value;
}

double BulletVolleyData2D::get_homing_distance_before_reached() const {
	return homing_distance_before_reached;
}
void BulletVolleyData2D::set_homing_distance_before_reached(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: homing_distance_before_reached must be finite and >= 0, keeping the old value.");
		return;
	}
	homing_distance_before_reached = value;
}

bool BulletVolleyData2D::get_homing_take_control_of_texture_rotation() const {
	return homing_take_control_of_texture_rotation;
}
void BulletVolleyData2D::set_homing_take_control_of_texture_rotation(bool value) {
	homing_take_control_of_texture_rotation = value;
}

bool BulletVolleyData2D::get_bullet_homing_auto_pop_after_target_reached() const {
	return bullet_homing_auto_pop_after_target_reached;
}
void BulletVolleyData2D::set_bullet_homing_auto_pop_after_target_reached(bool value) {
	bullet_homing_auto_pop_after_target_reached = value;
}

bool BulletVolleyData2D::get_shared_homing_deque_auto_pop_after_target_reached() const {
	return shared_homing_deque_auto_pop_after_target_reached;
}
void BulletVolleyData2D::set_shared_homing_deque_auto_pop_after_target_reached(bool value) {
	shared_homing_deque_auto_pop_after_target_reached = value;
}

Ref<BulletWobbleData2D> BulletVolleyData2D::get_shared_bullet_wobble_data() const {
	return shared_bullet_wobble_data;
}
void BulletVolleyData2D::set_shared_bullet_wobble_data(const Ref<BulletWobbleData2D> &new_wobble_data) {
	shared_bullet_wobble_data = new_wobble_data;
}

TypedArray<BulletWobbleData2D> BulletVolleyData2D::get_all_bullet_wobble_data() const {
	return all_bullet_wobble_data;
}
void BulletVolleyData2D::set_all_bullet_wobble_data(const TypedArray<BulletWobbleData2D> &new_data) {
	all_bullet_wobble_data = new_data;
}

bool BulletVolleyData2D::get_tile_all_bullet_wobble_data() const {
	return tile_all_bullet_wobble_data;
}
void BulletVolleyData2D::set_tile_all_bullet_wobble_data(bool value) {
	tile_all_bullet_wobble_data = value;
}

Vector2 BulletVolleyData2D::get_gravity() const {
	return gravity;
}
void BulletVolleyData2D::set_gravity(const Vector2 &value) {
	if (!value.is_finite()) {
		UtilityFunctions::push_error("BulletVolleyData2D: gravity must be finite, keeping the old value.");
		return;
	}
	gravity = value;
}

TypedArray<Vector2> BulletVolleyData2D::get_all_bullet_gravity() const {
	return all_bullet_gravity;
}
void BulletVolleyData2D::set_all_bullet_gravity(const TypedArray<Vector2> &new_data) {
	all_bullet_gravity = new_data;
}

bool BulletVolleyData2D::get_tile_all_bullet_gravity() const {
	return tile_all_bullet_gravity;
}
void BulletVolleyData2D::set_tile_all_bullet_gravity(bool value) {
	tile_all_bullet_gravity = value;
}

double BulletVolleyData2D::get_gravity_delay_sec() const {
	return gravity_delay_sec;
}
void BulletVolleyData2D::set_gravity_delay_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: gravity_delay_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	gravity_delay_sec = value;
}

double BulletVolleyData2D::get_gravity_duration_sec() const {
	return gravity_duration_sec;
}
void BulletVolleyData2D::set_gravity_duration_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: gravity_duration_sec must be finite and >= 0 (0 = infinite), keeping the old value.");
		return;
	}
	gravity_duration_sec = value;
}

double BulletVolleyData2D::get_linear_drag() const {
	return linear_drag;
}
void BulletVolleyData2D::set_linear_drag(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: linear_drag must be finite and >= 0, keeping the old value.");
		return;
	}
	linear_drag = value;
}

double BulletVolleyData2D::get_homing_delay_sec() const {
	return homing_delay_sec;
}
void BulletVolleyData2D::set_homing_delay_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: homing_delay_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	homing_delay_sec = value;
}

double BulletVolleyData2D::get_homing_duration_sec() const {
	return homing_duration_sec;
}
void BulletVolleyData2D::set_homing_duration_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: homing_duration_sec must be finite and >= 0 (0 = infinite), keeping the old value.");
		return;
	}
	homing_duration_sec = value;
}

double BulletVolleyData2D::get_homing_lose_range_px() const {
	return homing_lose_range_px;
}
void BulletVolleyData2D::set_homing_lose_range_px(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: homing_lose_range_px must be finite and >= 0 (0 = unlimited), keeping the old value.");
		return;
	}
	homing_lose_range_px = value;
}

int BulletVolleyData2D::get_bounce_mask() const {
	return bounce_mask;
}
void BulletVolleyData2D::set_bounce_mask(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletVolleyData2D: bounce_mask must be >= 0 (0 = bouncing disabled), keeping the old value.");
		return;
	}
	bounce_mask = value;
}
void BulletVolleyData2D::set_bounce_mask_from_array(const TypedArray<int> &numbers) {
	int bitmask = BulletVolleyData2D::calculate_bitmask(numbers);
	if (bitmask < 0) {
		return;
	}
	bounce_mask = bitmask;
}

bool BulletVolleyData2D::get_bounce_tilemap_layers() const {
	return bounce_tilemap_layers;
}
void BulletVolleyData2D::set_bounce_tilemap_layers(bool value) {
	bounce_tilemap_layers = value;
}

double BulletVolleyData2D::get_bounce_strength() const {
	return bounce_strength;
}
void BulletVolleyData2D::set_bounce_strength(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: bounce_strength must be finite and >= 0 (1 = elastic), keeping the old value.");
		return;
	}
	bounce_strength = value;
}

bool BulletVolleyData2D::get_bounce_push_assist() const {
	return bounce_push_assist;
}
void BulletVolleyData2D::set_bounce_push_assist(bool value) {
	bounce_push_assist = value;
}

bool BulletVolleyData2D::get_bounce_charge_amplify() const {
	return bounce_charge_amplify;
}
void BulletVolleyData2D::set_bounce_charge_amplify(bool value) {
	bounce_charge_amplify = value;
}

bool BulletVolleyData2D::get_bounce_hit_consumed() const {
	return bounce_hit_consumed;
}
void BulletVolleyData2D::set_bounce_hit_consumed(bool value) {
	bounce_hit_consumed = value;
}

int BulletVolleyData2D::get_bounce_max_count() const {
	return bounce_max_count;
}
void BulletVolleyData2D::set_bounce_max_count(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletVolleyData2D: bounce_max_count must be >= 0 (0 = unlimited), keeping the old value.");
		return;
	}
	bounce_max_count = value;
}

int BulletVolleyData2D::get_bounce_mode() const {
	return bounce_mode;
}
void BulletVolleyData2D::set_bounce_mode(int value) {
	if (value != BOUNCE_SIMPLE_RADIAL && value != BOUNCE_PRECISE_SHAPE) {
		UtilityFunctions::push_error("BulletVolleyData2D: bounce_mode must be 0 (radial) or 1 (precise shape), keeping the old value.");
		return;
	}
	bounce_mode = value;
}

bool BulletVolleyData2D::get_bounce_rotate_texture() const {
	return bounce_rotate_texture;
}
void BulletVolleyData2D::set_bounce_rotate_texture(bool value) {
	bounce_rotate_texture = value;
}

double BulletVolleyData2D::get_bounce_rotation_smooth() const {
	return bounce_rotation_smooth;
}
void BulletVolleyData2D::set_bounce_rotation_smooth(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: bounce_rotation_smooth must be finite and >= 0 (0 = instant snap), keeping the old value.");
		return;
	}
	bounce_rotation_smooth = value;
}

double BulletVolleyData2D::get_bounce_randomness_deg() const {
	return bounce_randomness_deg;
}
void BulletVolleyData2D::set_bounce_randomness_deg(double value) {
	if (!Math::is_finite(value) || value < 0.0 || value > 180.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: bounce_randomness_deg must be finite in [0, 180], keeping the old value.");
		return;
	}
	bounce_randomness_deg = value;
}

double BulletVolleyData2D::get_bounce_cooldown_sec() const {
	return bounce_cooldown_sec;
}
void BulletVolleyData2D::set_bounce_cooldown_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0 || value > 1.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: bounce_cooldown_sec must be finite in [0, 1], keeping the old value.");
		return;
	}
	bounce_cooldown_sec = value;
}

double BulletVolleyData2D::get_bounce_debounce_sec() const {
	return bounce_debounce_sec;
}
void BulletVolleyData2D::set_bounce_debounce_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolleyData2D: bounce_debounce_sec must be finite and >= 0 (0 = off), keeping the old value.");
		return;
	}
	bounce_debounce_sec = value;
}

void BulletVolleyData2D::_bind_methods() {
	// Inspector order follows the setup workflow: what the bullets are and look
	// like, how they move, then collision, attachments, effects, rendering.
	ADD_GROUP("Bullets", "");
	ClassDB::bind_method(D_METHOD("set_transforms", "new_transforms"), &BulletVolleyData2D::set_transforms);
	ClassDB::bind_method(D_METHOD("get_transforms"), &BulletVolleyData2D::get_transforms);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "transforms", PROPERTY_HINT_ARRAY_TYPE, "Transform2D"), "set_transforms", "get_transforms");

	ADD_GROUP("Appearance", "");
	ClassDB::bind_method(D_METHOD("get_sprite_frames"), &BulletVolleyData2D::get_sprite_frames);
	ClassDB::bind_method(D_METHOD("set_sprite_frames", "new_sprite_frames"), &BulletVolleyData2D::set_sprite_frames);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sprite_frames", PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames"), "set_sprite_frames", "get_sprite_frames");

	ClassDB::bind_method(D_METHOD("get_animation"), &BulletVolleyData2D::get_animation);
	ClassDB::bind_method(D_METHOD("set_animation", "new_animation"), &BulletVolleyData2D::set_animation);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "animation"), "set_animation", "get_animation");

	ClassDB::bind_method(D_METHOD("get_texture_size"), &BulletVolleyData2D::get_texture_size);
	ClassDB::bind_method(D_METHOD("set_texture_size", "new_texture_size"), &BulletVolleyData2D::set_texture_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "texture_size"), "set_texture_size", "get_texture_size");

	ClassDB::bind_method(D_METHOD("get_texture_rotation_radians"), &BulletVolleyData2D::get_texture_rotation_radians);
	ClassDB::bind_method(D_METHOD("set_texture_rotation_radians", "new_texture_rotation_radians"), &BulletVolleyData2D::set_texture_rotation_radians);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "texture_rotation_radians"), "set_texture_rotation_radians", "get_texture_rotation_radians");

	ClassDB::bind_method(D_METHOD("get_rotate_only_textures"), &BulletVolleyData2D::get_rotate_only_textures);
	ClassDB::bind_method(D_METHOD("set_rotate_only_textures", "new_rotate_only_textures"), &BulletVolleyData2D::set_rotate_only_textures);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rotate_only_textures"), "set_rotate_only_textures", "get_rotate_only_textures");

	ClassDB::bind_method(D_METHOD("get_is_texture_rotation_permanent"), &BulletVolleyData2D::get_is_texture_rotation_permanent);
	ClassDB::bind_method(D_METHOD("set_is_texture_rotation_permanent", "new_is_texture_rotation_permanent"), &BulletVolleyData2D::set_is_texture_rotation_permanent);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_texture_rotation_permanent"), "set_is_texture_rotation_permanent", "get_is_texture_rotation_permanent");

	ClassDB::bind_method(D_METHOD("get_max_life_time"), &BulletVolleyData2D::get_max_life_time);
	ClassDB::bind_method(D_METHOD("set_max_life_time", "new_max_life_time"), &BulletVolleyData2D::set_max_life_time);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_life_time"), "set_max_life_time", "get_max_life_time");

	ClassDB::bind_method(D_METHOD("get_is_life_time_over_signal_enabled"), &BulletVolleyData2D::get_is_life_time_over_signal_enabled);
	ClassDB::bind_method(D_METHOD("set_is_life_time_over_signal_enabled", "value"), &BulletVolleyData2D::set_is_life_time_over_signal_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_life_time_over_signal_enabled"), "set_is_life_time_over_signal_enabled", "get_is_life_time_over_signal_enabled");

	ClassDB::bind_method(D_METHOD("get_is_life_time_infinite"), &BulletVolleyData2D::get_is_life_time_infinite);
	ClassDB::bind_method(D_METHOD("set_is_life_time_infinite", "value"), &BulletVolleyData2D::set_is_life_time_infinite);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_life_time_infinite"), "set_is_life_time_infinite", "get_is_life_time_infinite");

	ClassDB::bind_method(D_METHOD("get_z_index"), &BulletVolleyData2D::get_z_index);
	ClassDB::bind_method(D_METHOD("set_z_index", "new_z_index"), &BulletVolleyData2D::set_z_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "z_index"), "set_z_index", "get_z_index");

	ClassDB::bind_method(D_METHOD("get_light_mask"), &BulletVolleyData2D::get_light_mask);
	ClassDB::bind_method(D_METHOD("set_light_mask", "new_light_mask"), &BulletVolleyData2D::set_light_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "light_mask", PROPERTY_HINT_LAYERS_2D_RENDER), "set_light_mask", "get_light_mask");

	ClassDB::bind_method(D_METHOD("get_visibility_layer"), &BulletVolleyData2D::get_visibility_layer);
	ClassDB::bind_method(D_METHOD("set_visibility_layer", "new_visibility_layer"), &BulletVolleyData2D::set_visibility_layer);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "visibility_layer", PROPERTY_HINT_LAYERS_2D_RENDER), "set_visibility_layer", "get_visibility_layer");

	ClassDB::bind_method(D_METHOD("get_self_modulate"), &BulletVolleyData2D::get_self_modulate);
	ClassDB::bind_method(D_METHOD("set_self_modulate", "new_self_modulate"), &BulletVolleyData2D::set_self_modulate);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "self_modulate"), "set_self_modulate", "get_self_modulate");

	ClassDB::bind_method(D_METHOD("get_override_frame_color"), &BulletVolleyData2D::get_override_frame_color);
	ClassDB::bind_method(D_METHOD("set_override_frame_color", "value"), &BulletVolleyData2D::set_override_frame_color);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "override_frame_color"), "set_override_frame_color", "get_override_frame_color");

	ClassDB::bind_method(D_METHOD("get_fade_in_sec"), &BulletVolleyData2D::get_fade_in_sec);
	ClassDB::bind_method(D_METHOD("set_fade_in_sec", "value"), &BulletVolleyData2D::set_fade_in_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_in_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_fade_in_sec", "get_fade_in_sec");

	ClassDB::bind_method(D_METHOD("get_fade_out_sec"), &BulletVolleyData2D::get_fade_out_sec);
	ClassDB::bind_method(D_METHOD("set_fade_out_sec", "value"), &BulletVolleyData2D::set_fade_out_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_out_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_fade_out_sec", "get_fade_out_sec");

	ClassDB::bind_method(D_METHOD("get_modulate_ramp"), &BulletVolleyData2D::get_modulate_ramp);
	ClassDB::bind_method(D_METHOD("set_modulate_ramp", "value"), &BulletVolleyData2D::set_modulate_ramp);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "modulate_ramp", PROPERTY_HINT_RESOURCE_TYPE, "Gradient"), "set_modulate_ramp", "get_modulate_ramp");

	ADD_GROUP("Movement Speed", "");

	ClassDB::bind_method(D_METHOD("get_shared_bullet_speed_data"), &BulletVolleyData2D::get_shared_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_speed_data", "new_speed_data"), &BulletVolleyData2D::set_shared_bullet_speed_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_speed_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletSpeedData2D"), "set_shared_bullet_speed_data", "get_shared_bullet_speed_data");

	ClassDB::bind_method(D_METHOD("get_all_bullet_speed_data"), &BulletVolleyData2D::get_all_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("set_all_bullet_speed_data", "new_data"), &BulletVolleyData2D::set_all_bullet_speed_data);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_speed_data", PROPERTY_HINT_ARRAY_TYPE, "BulletSpeedData2D"), "set_all_bullet_speed_data", "get_all_bullet_speed_data");

	ClassDB::bind_method(D_METHOD("get_tile_all_bullet_speed_data"), &BulletVolleyData2D::get_tile_all_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("set_tile_all_bullet_speed_data", "value"), &BulletVolleyData2D::set_tile_all_bullet_speed_data);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tile_all_bullet_speed_data"), "set_tile_all_bullet_speed_data", "get_tile_all_bullet_speed_data");

	ClassDB::bind_method(D_METHOD("get_shared_bullet_curves_data"), &BulletVolleyData2D::get_shared_bullet_curves_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_curves_data", "new_curves_data"), &BulletVolleyData2D::set_shared_bullet_curves_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_curves_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletCurvesData2D"), "set_shared_bullet_curves_data", "get_shared_bullet_curves_data");

	ClassDB::bind_method(D_METHOD("get_all_bullet_curves_data"), &BulletVolleyData2D::get_all_bullet_curves_data);
	ClassDB::bind_method(D_METHOD("set_all_bullet_curves_data", "new_data"), &BulletVolleyData2D::set_all_bullet_curves_data);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_curves_data", PROPERTY_HINT_ARRAY_TYPE, "BulletCurvesData2D"), "set_all_bullet_curves_data", "get_all_bullet_curves_data");

	ClassDB::bind_method(D_METHOD("get_tile_all_bullet_curves_data"), &BulletVolleyData2D::get_tile_all_bullet_curves_data);
	ClassDB::bind_method(D_METHOD("set_tile_all_bullet_curves_data", "value"), &BulletVolleyData2D::set_tile_all_bullet_curves_data);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tile_all_bullet_curves_data"), "set_tile_all_bullet_curves_data", "get_tile_all_bullet_curves_data");

	ADD_GROUP("Bullet Rotation", "");

	ClassDB::bind_method(D_METHOD("get_adjust_direction_based_on_rotation"), &BulletVolleyData2D::get_adjust_direction_based_on_rotation);
	ClassDB::bind_method(D_METHOD("set_adjust_direction_based_on_rotation", "new_adjust_direction_based_on_rotation"), &BulletVolleyData2D::set_adjust_direction_based_on_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "adjust_direction_based_on_rotation"), "set_adjust_direction_based_on_rotation", "get_adjust_direction_based_on_rotation");

	ClassDB::bind_method(D_METHOD("get_shared_bullet_rotation_data"), &BulletVolleyData2D::get_shared_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_rotation_data", "new_rotation_data"), &BulletVolleyData2D::set_shared_bullet_rotation_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_rotation_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletRotationData2D"), "set_shared_bullet_rotation_data", "get_shared_bullet_rotation_data");

	ClassDB::bind_method(D_METHOD("get_all_bullet_rotation_data"), &BulletVolleyData2D::get_all_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("set_all_bullet_rotation_data", "new_data"), &BulletVolleyData2D::set_all_bullet_rotation_data);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_rotation_data", PROPERTY_HINT_ARRAY_TYPE, "BulletRotationData2D"), "set_all_bullet_rotation_data", "get_all_bullet_rotation_data");

	ClassDB::bind_method(D_METHOD("get_tile_all_bullet_rotation_data"), &BulletVolleyData2D::get_tile_all_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("set_tile_all_bullet_rotation_data", "value"), &BulletVolleyData2D::set_tile_all_bullet_rotation_data);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tile_all_bullet_rotation_data"), "set_tile_all_bullet_rotation_data", "get_tile_all_bullet_rotation_data");

	ClassDB::bind_method(D_METHOD("get_stop_rotation_when_max_reached"), &BulletVolleyData2D::get_stop_rotation_when_max_reached);
	ClassDB::bind_method(D_METHOD("set_stop_rotation_when_max_reached", "value"), &BulletVolleyData2D::set_stop_rotation_when_max_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "stop_rotation_when_max_reached"), "set_stop_rotation_when_max_reached", "get_stop_rotation_when_max_reached");
	ADD_GROUP("Wobble", "");

	ClassDB::bind_method(D_METHOD("get_shared_bullet_wobble_data"), &BulletVolleyData2D::get_shared_bullet_wobble_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_wobble_data", "new_wobble_data"), &BulletVolleyData2D::set_shared_bullet_wobble_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_wobble_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletWobbleData2D"), "set_shared_bullet_wobble_data", "get_shared_bullet_wobble_data");

	ClassDB::bind_method(D_METHOD("get_all_bullet_wobble_data"), &BulletVolleyData2D::get_all_bullet_wobble_data);
	ClassDB::bind_method(D_METHOD("set_all_bullet_wobble_data", "new_data"), &BulletVolleyData2D::set_all_bullet_wobble_data);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_wobble_data", PROPERTY_HINT_ARRAY_TYPE, "BulletWobbleData2D"), "set_all_bullet_wobble_data", "get_all_bullet_wobble_data");

	ClassDB::bind_method(D_METHOD("get_tile_all_bullet_wobble_data"), &BulletVolleyData2D::get_tile_all_bullet_wobble_data);
	ClassDB::bind_method(D_METHOD("set_tile_all_bullet_wobble_data", "value"), &BulletVolleyData2D::set_tile_all_bullet_wobble_data);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tile_all_bullet_wobble_data"), "set_tile_all_bullet_wobble_data", "get_tile_all_bullet_wobble_data");

	ADD_GROUP("Gravity", "");
	ClassDB::bind_method(D_METHOD("get_gravity"), &BulletVolleyData2D::get_gravity);
	ClassDB::bind_method(D_METHOD("set_gravity", "value"), &BulletVolleyData2D::set_gravity);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "gravity"), "set_gravity", "get_gravity");

	ClassDB::bind_method(D_METHOD("get_all_bullet_gravity"), &BulletVolleyData2D::get_all_bullet_gravity);
	ClassDB::bind_method(D_METHOD("set_all_bullet_gravity", "new_data"), &BulletVolleyData2D::set_all_bullet_gravity);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_gravity", PROPERTY_HINT_ARRAY_TYPE, "Vector2"), "set_all_bullet_gravity", "get_all_bullet_gravity");

	ClassDB::bind_method(D_METHOD("get_tile_all_bullet_gravity"), &BulletVolleyData2D::get_tile_all_bullet_gravity);
	ClassDB::bind_method(D_METHOD("set_tile_all_bullet_gravity", "value"), &BulletVolleyData2D::set_tile_all_bullet_gravity);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tile_all_bullet_gravity"), "set_tile_all_bullet_gravity", "get_tile_all_bullet_gravity");

	ClassDB::bind_method(D_METHOD("get_gravity_delay_sec"), &BulletVolleyData2D::get_gravity_delay_sec);
	ClassDB::bind_method(D_METHOD("set_gravity_delay_sec", "value"), &BulletVolleyData2D::set_gravity_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_delay_sec"), "set_gravity_delay_sec", "get_gravity_delay_sec");

	ClassDB::bind_method(D_METHOD("get_gravity_duration_sec"), &BulletVolleyData2D::get_gravity_duration_sec);
	ClassDB::bind_method(D_METHOD("set_gravity_duration_sec", "value"), &BulletVolleyData2D::set_gravity_duration_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_duration_sec"), "set_gravity_duration_sec", "get_gravity_duration_sec");

	ClassDB::bind_method(D_METHOD("get_linear_drag"), &BulletVolleyData2D::get_linear_drag);
	ClassDB::bind_method(D_METHOD("set_linear_drag", "value"), &BulletVolleyData2D::set_linear_drag);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "linear_drag"), "set_linear_drag", "get_linear_drag");

	ADD_GROUP("Bounce and Ricochet", "");

	ClassDB::bind_method(D_METHOD("get_bounce_mask"), &BulletVolleyData2D::get_bounce_mask);
	ClassDB::bind_method(D_METHOD("set_bounce_mask", "value"), &BulletVolleyData2D::set_bounce_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bounce_mask", PROPERTY_HINT_LAYERS_2D_PHYSICS), "set_bounce_mask", "get_bounce_mask");
	ClassDB::bind_method(D_METHOD("set_bounce_mask_from_array", "array_of_masks"), &BulletVolleyData2D::set_bounce_mask_from_array);

	ClassDB::bind_method(D_METHOD("get_bounce_tilemap_layers"), &BulletVolleyData2D::get_bounce_tilemap_layers);
	ClassDB::bind_method(D_METHOD("set_bounce_tilemap_layers", "value"), &BulletVolleyData2D::set_bounce_tilemap_layers);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_tilemap_layers"), "set_bounce_tilemap_layers", "get_bounce_tilemap_layers");

	ClassDB::bind_method(D_METHOD("get_bounce_strength"), &BulletVolleyData2D::get_bounce_strength);
	ClassDB::bind_method(D_METHOD("set_bounce_strength", "value"), &BulletVolleyData2D::set_bounce_strength);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_strength"), "set_bounce_strength", "get_bounce_strength");

	ClassDB::bind_method(D_METHOD("get_bounce_push_assist"), &BulletVolleyData2D::get_bounce_push_assist);
	ClassDB::bind_method(D_METHOD("set_bounce_push_assist", "value"), &BulletVolleyData2D::set_bounce_push_assist);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_push_assist"), "set_bounce_push_assist", "get_bounce_push_assist");

	ClassDB::bind_method(D_METHOD("get_bounce_charge_amplify"), &BulletVolleyData2D::get_bounce_charge_amplify);
	ClassDB::bind_method(D_METHOD("set_bounce_charge_amplify", "value"), &BulletVolleyData2D::set_bounce_charge_amplify);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_charge_amplify"), "set_bounce_charge_amplify", "get_bounce_charge_amplify");

	ClassDB::bind_method(D_METHOD("get_bounce_hit_consumed"), &BulletVolleyData2D::get_bounce_hit_consumed);
	ClassDB::bind_method(D_METHOD("set_bounce_hit_consumed", "value"), &BulletVolleyData2D::set_bounce_hit_consumed);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_hit_consumed"), "set_bounce_hit_consumed", "get_bounce_hit_consumed");

	ClassDB::bind_method(D_METHOD("get_bounce_max_count"), &BulletVolleyData2D::get_bounce_max_count);
	ClassDB::bind_method(D_METHOD("set_bounce_max_count", "value"), &BulletVolleyData2D::set_bounce_max_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bounce_max_count", PROPERTY_HINT_RANGE, "0,1000000,1,or_greater"), "set_bounce_max_count", "get_bounce_max_count");

	ClassDB::bind_method(D_METHOD("get_bounce_mode"), &BulletVolleyData2D::get_bounce_mode);
	ClassDB::bind_method(D_METHOD("set_bounce_mode", "value"), &BulletVolleyData2D::set_bounce_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bounce_mode", PROPERTY_HINT_ENUM, "Simple Radial,Precise Shape"), "set_bounce_mode", "get_bounce_mode");

	ClassDB::bind_method(D_METHOD("get_bounce_rotate_texture"), &BulletVolleyData2D::get_bounce_rotate_texture);
	ClassDB::bind_method(D_METHOD("set_bounce_rotate_texture", "value"), &BulletVolleyData2D::set_bounce_rotate_texture);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_rotate_texture"), "set_bounce_rotate_texture", "get_bounce_rotate_texture");

	ClassDB::bind_method(D_METHOD("get_bounce_rotation_smooth"), &BulletVolleyData2D::get_bounce_rotation_smooth);
	ClassDB::bind_method(D_METHOD("set_bounce_rotation_smooth", "value"), &BulletVolleyData2D::set_bounce_rotation_smooth);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_rotation_smooth"), "set_bounce_rotation_smooth", "get_bounce_rotation_smooth");

	ClassDB::bind_method(D_METHOD("get_bounce_randomness_deg"), &BulletVolleyData2D::get_bounce_randomness_deg);
	ClassDB::bind_method(D_METHOD("set_bounce_randomness_deg", "value"), &BulletVolleyData2D::set_bounce_randomness_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_randomness_deg", PROPERTY_HINT_RANGE, "0,180,0.1"), "set_bounce_randomness_deg", "get_bounce_randomness_deg");

	ClassDB::bind_method(D_METHOD("get_bounce_cooldown_sec"), &BulletVolleyData2D::get_bounce_cooldown_sec);
	ClassDB::bind_method(D_METHOD("set_bounce_cooldown_sec", "value"), &BulletVolleyData2D::set_bounce_cooldown_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_cooldown_sec"), "set_bounce_cooldown_sec", "get_bounce_cooldown_sec");

	ClassDB::bind_method(D_METHOD("get_bounce_debounce_sec"), &BulletVolleyData2D::get_bounce_debounce_sec);
	ClassDB::bind_method(D_METHOD("set_bounce_debounce_sec", "value"), &BulletVolleyData2D::set_bounce_debounce_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_debounce_sec"), "set_bounce_debounce_sec", "get_bounce_debounce_sec");

	ADD_GROUP("Movement Pattern Paths", "");
	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_path"), &BulletVolleyData2D::get_shared_movement_pattern_path);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_path", "new_path"), &BulletVolleyData2D::set_shared_movement_pattern_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "shared_movement_pattern_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Path2D"), "set_shared_movement_pattern_path", "get_shared_movement_pattern_path");

	ClassDB::bind_method(D_METHOD("get_all_bullet_movement_pattern_paths"), &BulletVolleyData2D::get_all_bullet_movement_pattern_paths);
	ClassDB::bind_method(D_METHOD("set_all_bullet_movement_pattern_paths", "new_paths"), &BulletVolleyData2D::set_all_bullet_movement_pattern_paths);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_movement_pattern_paths", PROPERTY_HINT_ARRAY_TYPE, vformat("%s/%s:%s", Variant::NODE_PATH, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Path2D")), "set_all_bullet_movement_pattern_paths", "get_all_bullet_movement_pattern_paths");

	ClassDB::bind_method(D_METHOD("get_tile_all_bullet_movement_pattern_paths"), &BulletVolleyData2D::get_tile_all_bullet_movement_pattern_paths);
	ClassDB::bind_method(D_METHOD("set_tile_all_bullet_movement_pattern_paths", "value"), &BulletVolleyData2D::set_tile_all_bullet_movement_pattern_paths);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tile_all_bullet_movement_pattern_paths"), "set_tile_all_bullet_movement_pattern_paths", "get_tile_all_bullet_movement_pattern_paths");

	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_face_movement_direction"), &BulletVolleyData2D::get_shared_movement_pattern_face_movement_direction);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_face_movement_direction", "value"), &BulletVolleyData2D::set_shared_movement_pattern_face_movement_direction);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_movement_pattern_face_movement_direction"), "set_shared_movement_pattern_face_movement_direction", "get_shared_movement_pattern_face_movement_direction");

	ClassDB::bind_method(D_METHOD("get_all_bullet_movement_pattern_face_movement_directions"), &BulletVolleyData2D::get_all_bullet_movement_pattern_face_movement_directions);
	ClassDB::bind_method(D_METHOD("set_all_bullet_movement_pattern_face_movement_directions", "new_flags"), &BulletVolleyData2D::set_all_bullet_movement_pattern_face_movement_directions);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_movement_pattern_face_movement_directions", PROPERTY_HINT_ARRAY_TYPE, "bool"), "set_all_bullet_movement_pattern_face_movement_directions", "get_all_bullet_movement_pattern_face_movement_directions");

	ClassDB::bind_method(D_METHOD("get_tile_all_bullet_movement_pattern_face_movement_directions"), &BulletVolleyData2D::get_tile_all_bullet_movement_pattern_face_movement_directions);
	ClassDB::bind_method(D_METHOD("set_tile_all_bullet_movement_pattern_face_movement_directions", "value"), &BulletVolleyData2D::set_tile_all_bullet_movement_pattern_face_movement_directions);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tile_all_bullet_movement_pattern_face_movement_directions"), "set_tile_all_bullet_movement_pattern_face_movement_directions", "get_tile_all_bullet_movement_pattern_face_movement_directions");

	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_repeat"), &BulletVolleyData2D::get_shared_movement_pattern_repeat);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_repeat", "value"), &BulletVolleyData2D::set_shared_movement_pattern_repeat);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_movement_pattern_repeat"), "set_shared_movement_pattern_repeat", "get_shared_movement_pattern_repeat");

	ClassDB::bind_method(D_METHOD("get_all_bullet_movement_pattern_repeats"), &BulletVolleyData2D::get_all_bullet_movement_pattern_repeats);
	ClassDB::bind_method(D_METHOD("set_all_bullet_movement_pattern_repeats", "new_flags"), &BulletVolleyData2D::set_all_bullet_movement_pattern_repeats);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_movement_pattern_repeats", PROPERTY_HINT_ARRAY_TYPE, "bool"), "set_all_bullet_movement_pattern_repeats", "get_all_bullet_movement_pattern_repeats");

	ClassDB::bind_method(D_METHOD("get_tile_all_bullet_movement_pattern_repeats"), &BulletVolleyData2D::get_tile_all_bullet_movement_pattern_repeats);
	ClassDB::bind_method(D_METHOD("set_tile_all_bullet_movement_pattern_repeats", "value"), &BulletVolleyData2D::set_tile_all_bullet_movement_pattern_repeats);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tile_all_bullet_movement_pattern_repeats"), "set_tile_all_bullet_movement_pattern_repeats", "get_tile_all_bullet_movement_pattern_repeats");

	ADD_GROUP("Homing", "");
	ClassDB::bind_method(D_METHOD("get_homing_smoothing"), &BulletVolleyData2D::get_homing_smoothing);
	ClassDB::bind_method(D_METHOD("set_homing_smoothing", "value"), &BulletVolleyData2D::set_homing_smoothing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing"), "set_homing_smoothing", "get_homing_smoothing");

	ClassDB::bind_method(D_METHOD("get_homing_update_interval"), &BulletVolleyData2D::get_homing_update_interval);
	ClassDB::bind_method(D_METHOD("set_homing_update_interval", "value"), &BulletVolleyData2D::set_homing_update_interval);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_update_interval"), "set_homing_update_interval", "get_homing_update_interval");

	ClassDB::bind_method(D_METHOD("get_homing_distance_before_reached"), &BulletVolleyData2D::get_homing_distance_before_reached);
	ClassDB::bind_method(D_METHOD("set_homing_distance_before_reached", "value"), &BulletVolleyData2D::set_homing_distance_before_reached);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_distance_before_reached"), "set_homing_distance_before_reached", "get_homing_distance_before_reached");

	ClassDB::bind_method(D_METHOD("get_homing_take_control_of_texture_rotation"), &BulletVolleyData2D::get_homing_take_control_of_texture_rotation);
	ClassDB::bind_method(D_METHOD("set_homing_take_control_of_texture_rotation", "value"), &BulletVolleyData2D::set_homing_take_control_of_texture_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_take_control_of_texture_rotation"), "set_homing_take_control_of_texture_rotation", "get_homing_take_control_of_texture_rotation");

	ClassDB::bind_method(D_METHOD("get_bullet_homing_auto_pop_after_target_reached"), &BulletVolleyData2D::get_bullet_homing_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_bullet_homing_auto_pop_after_target_reached", "value"), &BulletVolleyData2D::set_bullet_homing_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bullet_homing_auto_pop_after_target_reached"), "set_bullet_homing_auto_pop_after_target_reached", "get_bullet_homing_auto_pop_after_target_reached");

	ClassDB::bind_method(D_METHOD("get_shared_homing_deque_auto_pop_after_target_reached"), &BulletVolleyData2D::get_shared_homing_deque_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_shared_homing_deque_auto_pop_after_target_reached", "value"), &BulletVolleyData2D::set_shared_homing_deque_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_homing_deque_auto_pop_after_target_reached"), "set_shared_homing_deque_auto_pop_after_target_reached", "get_shared_homing_deque_auto_pop_after_target_reached");

	ClassDB::bind_method(D_METHOD("get_homing_delay_sec"), &BulletVolleyData2D::get_homing_delay_sec);
	ClassDB::bind_method(D_METHOD("set_homing_delay_sec", "value"), &BulletVolleyData2D::set_homing_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_delay_sec"), "set_homing_delay_sec", "get_homing_delay_sec");

	ClassDB::bind_method(D_METHOD("get_homing_duration_sec"), &BulletVolleyData2D::get_homing_duration_sec);
	ClassDB::bind_method(D_METHOD("set_homing_duration_sec", "value"), &BulletVolleyData2D::set_homing_duration_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_duration_sec"), "set_homing_duration_sec", "get_homing_duration_sec");

	ClassDB::bind_method(D_METHOD("get_homing_lose_range_px"), &BulletVolleyData2D::get_homing_lose_range_px);
	ClassDB::bind_method(D_METHOD("set_homing_lose_range_px", "value"), &BulletVolleyData2D::set_homing_lose_range_px);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_lose_range_px"), "set_homing_lose_range_px", "get_homing_lose_range_px");

	BIND_ENUM_CONSTANT(BOUNCE_SIMPLE_RADIAL);
	BIND_ENUM_CONSTANT(BOUNCE_PRECISE_SHAPE);
	ADD_GROUP("Collision", "");
	ClassDB::bind_method(D_METHOD("get_collision_layer"), &BulletVolleyData2D::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "new_collision_layer"), &BulletVolleyData2D::set_collision_layer);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_2D_PHYSICS), "set_collision_layer", "get_collision_layer");

	ClassDB::bind_method(D_METHOD("get_collision_mask"), &BulletVolleyData2D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "new_collision_mask"), &BulletVolleyData2D::set_collision_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_2D_PHYSICS), "set_collision_mask", "get_collision_mask");

	ClassDB::bind_method(D_METHOD("get_collision_shape"), &BulletVolleyData2D::get_collision_shape);
	ClassDB::bind_method(D_METHOD("set_collision_shape", "new_shape"), &BulletVolleyData2D::set_collision_shape);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "collision_shape", PROPERTY_HINT_RESOURCE_TYPE, "Shape2D"), "set_collision_shape", "get_collision_shape");

	ClassDB::bind_method(D_METHOD("get_collision_shape_offset"), &BulletVolleyData2D::get_collision_shape_offset);
	ClassDB::bind_method(D_METHOD("set_collision_shape_offset", "new_collision_shape_offset"), &BulletVolleyData2D::set_collision_shape_offset);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "collision_shape_offset"), "set_collision_shape_offset", "get_collision_shape_offset");

	ClassDB::bind_method(D_METHOD("get_monitorable"), &BulletVolleyData2D::get_monitorable);
	ClassDB::bind_method(D_METHOD("set_monitorable", "new_monitorable"), &BulletVolleyData2D::set_monitorable);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitorable"), "set_monitorable", "get_monitorable");

	ClassDB::bind_method(D_METHOD("get_emit_collision_signals"), &BulletVolleyData2D::get_emit_collision_signals);
	ClassDB::bind_method(D_METHOD("set_emit_collision_signals", "value"), &BulletVolleyData2D::set_emit_collision_signals);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "emit_collision_signals"), "set_emit_collision_signals", "get_emit_collision_signals");

	ClassDB::bind_method(D_METHOD("get_bullet_max_collision_count"), &BulletVolleyData2D::get_bullet_max_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullet_max_collision_count", "value"), &BulletVolleyData2D::set_bullet_max_collision_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bullet_max_collision_count"), "set_bullet_max_collision_count", "get_bullet_max_collision_count");

	ClassDB::bind_method(D_METHOD("get_bullets_current_collision_count"), &BulletVolleyData2D::get_bullets_current_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullets_current_collision_count", "arr"), &BulletVolleyData2D::set_bullets_current_collision_count);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bullets_current_collision_count", PROPERTY_HINT_ARRAY_TYPE, "int"), "set_bullets_current_collision_count", "get_bullets_current_collision_count");

	ClassDB::bind_method(D_METHOD("get_tile_bullets_current_collision_count"), &BulletVolleyData2D::get_tile_bullets_current_collision_count);
	ClassDB::bind_method(D_METHOD("set_tile_bullets_current_collision_count", "value"), &BulletVolleyData2D::set_tile_bullets_current_collision_count);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tile_bullets_current_collision_count"), "set_tile_bullets_current_collision_count", "get_tile_bullets_current_collision_count");

	ClassDB::bind_method(D_METHOD("get_shared_bullets_custom_data"), &BulletVolleyData2D::get_shared_bullets_custom_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullets_custom_data", "new_shared_bullets_custom_data"), &BulletVolleyData2D::set_shared_bullets_custom_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullets_custom_data", PROPERTY_HINT_RESOURCE_TYPE, "Resource"), "set_shared_bullets_custom_data", "get_shared_bullets_custom_data");

	ClassDB::bind_method(D_METHOD("get_all_bullets_custom_data"), &BulletVolleyData2D::get_all_bullets_custom_data);
	ClassDB::bind_method(D_METHOD("set_all_bullets_custom_data", "new_custom_data"), &BulletVolleyData2D::set_all_bullets_custom_data);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullets_custom_data", PROPERTY_HINT_ARRAY_TYPE, "Resource"), "set_all_bullets_custom_data", "get_all_bullets_custom_data");

	ClassDB::bind_method(D_METHOD("get_tile_all_bullets_custom_data"), &BulletVolleyData2D::get_tile_all_bullets_custom_data);
	ClassDB::bind_method(D_METHOD("set_tile_all_bullets_custom_data", "value"), &BulletVolleyData2D::set_tile_all_bullets_custom_data);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tile_all_bullets_custom_data"), "set_tile_all_bullets_custom_data", "get_tile_all_bullets_custom_data");

	ADD_GROUP("Attachments", "");
	ClassDB::bind_method(D_METHOD("get_shared_bullet_attachment"), &BulletVolleyData2D::get_shared_bullet_attachment);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_attachment", "new_attachment"), &BulletVolleyData2D::set_shared_bullet_attachment);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_attachment", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_shared_bullet_attachment", "get_shared_bullet_attachment");

	ClassDB::bind_method(D_METHOD("get_shared_bullet_attachment_offset"), &BulletVolleyData2D::get_shared_bullet_attachment_offset);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_attachment_offset", "new_offset"), &BulletVolleyData2D::set_shared_bullet_attachment_offset);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "shared_bullet_attachment_offset"), "set_shared_bullet_attachment_offset", "get_shared_bullet_attachment_offset");

	ClassDB::bind_method(D_METHOD("get_shared_bullet_attachment_stick_relative_to_bullet"), &BulletVolleyData2D::get_shared_bullet_attachment_stick_relative_to_bullet);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_attachment_stick_relative_to_bullet", "value"), &BulletVolleyData2D::set_shared_bullet_attachment_stick_relative_to_bullet);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_bullet_attachment_stick_relative_to_bullet"), "set_shared_bullet_attachment_stick_relative_to_bullet", "get_shared_bullet_attachment_stick_relative_to_bullet");

	ADD_GROUP("Sprite Effects", "");
	ClassDB::bind_method(D_METHOD("get_effect_layers"), &BulletVolleyData2D::get_effect_layers);
	ClassDB::bind_method(D_METHOD("set_effect_layers", "new_layers"), &BulletVolleyData2D::set_effect_layers);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "effect_layers", PROPERTY_HINT_ARRAY_TYPE, "BulletEffectLayerData2D"), "set_effect_layers", "get_effect_layers");

	ADD_GROUP("Rendering and Material", "");
	ClassDB::bind_method(D_METHOD("get_material"), &BulletVolleyData2D::get_material);
	ClassDB::bind_method(D_METHOD("set_material", "new_material"), &BulletVolleyData2D::set_material);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial,CanvasItemMaterial"), "set_material", "get_material");

	ClassDB::bind_method(D_METHOD("get_mesh"), &BulletVolleyData2D::get_mesh);
	ClassDB::bind_method(D_METHOD("set_mesh", "new_mesh"), &BulletVolleyData2D::set_mesh);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_mesh", "get_mesh");

	ClassDB::bind_method(D_METHOD("get_instance_shader_parameters"), &BulletVolleyData2D::get_instance_shader_parameters);
	ClassDB::bind_method(D_METHOD("set_instance_shader_parameters", "new_instance_shader_parameters"), &BulletVolleyData2D::set_instance_shader_parameters);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "instance_shader_parameters", PROPERTY_HINT_TYPE_STRING, "String:Variant"), "set_instance_shader_parameters", "get_instance_shader_parameters");

	ClassDB::bind_method(D_METHOD("set_collision_layer_from_array", "array_of_layers"), &BulletVolleyData2D::set_collision_layer_from_array);
	ClassDB::bind_method(D_METHOD("set_collision_mask_from_array", "array_of_masks"), &BulletVolleyData2D::set_collision_mask_from_array);
	ClassDB::bind_method(D_METHOD("set_light_mask_from_array", "array_of_light_masks"), &BulletVolleyData2D::set_light_mask_from_array);
	ClassDB::bind_method(D_METHOD("set_visibility_layer_from_array", "array_of_visibility_layers"), &BulletVolleyData2D::set_visibility_layer_from_array);

	ClassDB::bind_static_method("BulletVolleyData2D", D_METHOD("calculate_bitmask", "numbers"), &BulletVolleyData2D::calculate_bitmask);
}
} //namespace BlastBullets2D
