#include "./multimesh_bullets_data2d.hpp"
#include "godot_cpp/core/class_db.hpp"

using namespace godot;

namespace BlastBullets2D {

int MultiMeshBulletsData2D::calculate_bitmask(const TypedArray<int> &numbers) {
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

TypedArray<Transform2D> MultiMeshBulletsData2D::get_transforms() const {
	return transforms;
}
void MultiMeshBulletsData2D::set_transforms(const TypedArray<Transform2D> &new_transforms) {
	transforms = new_transforms;
}

Ref<SpriteFrames> MultiMeshBulletsData2D::get_sprite_frames() const {
	return sprite_frames;
}
void MultiMeshBulletsData2D::set_sprite_frames(const Ref<SpriteFrames> &new_sprite_frames) {
	sprite_frames = new_sprite_frames;
}

StringName MultiMeshBulletsData2D::get_animation() const {
	return animation;
}
void MultiMeshBulletsData2D::set_animation(const StringName &new_animation) {
	animation = new_animation;
}

Vector2 MultiMeshBulletsData2D::get_texture_size() const {
	return texture_size;
}
void MultiMeshBulletsData2D::set_texture_size(Vector2 new_texture_size) {
	texture_size = new_texture_size;
}

real_t MultiMeshBulletsData2D::get_texture_rotation_radians() const {
	return texture_rotation_radians;
}
void MultiMeshBulletsData2D::set_texture_rotation_radians(real_t new_texture_rotation_radians) {
	texture_rotation_radians = new_texture_rotation_radians;
}

int MultiMeshBulletsData2D::get_collision_layer() const {
	return collision_layer;
}
void MultiMeshBulletsData2D::set_collision_layer(int new_collision_layer) {
	collision_layer = new_collision_layer;
}

void MultiMeshBulletsData2D::set_collision_layer_from_array(const TypedArray<int> &numbers) {
	int bitmask = calculate_bitmask(numbers);
	collision_layer = bitmask;
}

int MultiMeshBulletsData2D::get_collision_mask() const {
	return collision_mask;
}
void MultiMeshBulletsData2D::set_collision_mask(int new_collision_mask) {
	collision_mask = new_collision_mask;
}

void MultiMeshBulletsData2D::set_collision_mask_from_array(const TypedArray<int> &numbers) {
	int bitmask = calculate_bitmask(numbers);
	collision_mask = bitmask;
}

Ref<Shape2D> MultiMeshBulletsData2D::get_collision_shape() const {
	return collision_shape;
}
void MultiMeshBulletsData2D::set_collision_shape(const Ref<Shape2D> &new_shape) {
	collision_shape = new_shape;
}

Vector2 MultiMeshBulletsData2D::get_collision_shape_offset() const {
	return collision_shape_offset;
}
void MultiMeshBulletsData2D::set_collision_shape_offset(const Vector2 &new_collision_shape_offset) {
	collision_shape_offset = new_collision_shape_offset;
}

bool MultiMeshBulletsData2D::get_monitorable() const {
	return monitorable;
}
void MultiMeshBulletsData2D::set_monitorable(bool new_monitorable) {
	monitorable = new_monitorable;
}

Ref<Resource> MultiMeshBulletsData2D::get_bullets_custom_data() const {
	return bullets_custom_data;
}
void MultiMeshBulletsData2D::set_bullets_custom_data(const Ref<Resource> &new_bullets_custom_data) {
	bullets_custom_data = new_bullets_custom_data;
}

double MultiMeshBulletsData2D::get_max_life_time() const {
	return max_life_time;
}
void MultiMeshBulletsData2D::set_max_life_time(double new_max_life_time) {
	max_life_time = new_max_life_time;
}

Ref<Material> MultiMeshBulletsData2D::get_material() const {
	return material;
}
void MultiMeshBulletsData2D::set_material(const Ref<Material> &new_material) {
	material = new_material;
}

Ref<Mesh> MultiMeshBulletsData2D::get_mesh() const {
	return mesh;
}
void MultiMeshBulletsData2D::set_mesh(const Ref<Mesh> &new_mesh) {
	mesh = new_mesh;
}

TypedArray<BulletRotationData2D> MultiMeshBulletsData2D::get_all_bullet_rotation_data() const {
	return all_bullet_rotation_data;
}
void MultiMeshBulletsData2D::set_all_bullet_rotation_data(const TypedArray<BulletRotationData2D> &new_bullet_rotation_data) {
	all_bullet_rotation_data = new_bullet_rotation_data;
}

bool MultiMeshBulletsData2D::get_rotate_only_textures() const {
	return rotate_only_textures;
}
void MultiMeshBulletsData2D::set_rotate_only_textures(bool new_rotate_only_textures) {
	rotate_only_textures = new_rotate_only_textures;
}

bool MultiMeshBulletsData2D::get_is_texture_rotation_permanent() const {
	return is_texture_rotation_permanent;
}
void MultiMeshBulletsData2D::set_is_texture_rotation_permanent(bool new_is_texture_rotation_permanent) {
	is_texture_rotation_permanent = new_is_texture_rotation_permanent;
}

int MultiMeshBulletsData2D::get_z_index() const {
	return z_index;
}

void MultiMeshBulletsData2D::set_z_index(int new_z_index) {
	z_index = new_z_index;
}

int MultiMeshBulletsData2D::get_light_mask() const {
	return light_mask;
}

void MultiMeshBulletsData2D::set_light_mask(int new_light_mask) {
	light_mask = new_light_mask;
}

void MultiMeshBulletsData2D::set_light_mask_from_array(const TypedArray<int> &numbers) {
	int bitmask = calculate_bitmask(numbers);
	light_mask = bitmask;
}

int MultiMeshBulletsData2D::get_visibility_layer() const {
	return visibility_layer;
}

void MultiMeshBulletsData2D::set_visibility_layer(int new_visibility_layer) {
	visibility_layer = new_visibility_layer;
}

void MultiMeshBulletsData2D::set_visibility_layer_from_array(const TypedArray<int> &numbers) {
	int bitmask = calculate_bitmask(numbers);
	visibility_layer = bitmask;
}

Dictionary MultiMeshBulletsData2D::get_instance_shader_parameters() const {
	return instance_shader_parameters;
}

void MultiMeshBulletsData2D::set_instance_shader_parameters(const Dictionary &new_instance_shader_parameters) {
	instance_shader_parameters = new_instance_shader_parameters;
}

bool MultiMeshBulletsData2D::get_is_life_time_over_signal_enabled() const {
	return is_life_time_over_signal_enabled;
}

void MultiMeshBulletsData2D::set_is_life_time_over_signal_enabled(bool new_is_life_time_over_signal_enabled) {
	is_life_time_over_signal_enabled = new_is_life_time_over_signal_enabled;
}

bool MultiMeshBulletsData2D::get_stop_rotation_when_max_reached() const {
	return stop_rotation_when_max_reached;
}

void MultiMeshBulletsData2D::set_stop_rotation_when_max_reached(bool new_stop_rotation_when_max_reached) {
	stop_rotation_when_max_reached = new_stop_rotation_when_max_reached;
}

int MultiMeshBulletsData2D::get_bullet_max_collision_count() const {
	return bullet_max_collision_count;
}

void MultiMeshBulletsData2D::set_bullet_max_collision_count(int new_max_collision_amount) {
	bullet_max_collision_count = new_max_collision_amount;
}

bool MultiMeshBulletsData2D::get_is_life_time_infinite() const {
	return is_life_time_infinite;
}
void MultiMeshBulletsData2D::set_is_life_time_infinite(bool value) {
	is_life_time_infinite = value;
}

TypedArray<int> MultiMeshBulletsData2D::get_bullets_current_collision_count() const {
	return bullets_current_collision_count;
}

void MultiMeshBulletsData2D::set_bullets_current_collision_count(const TypedArray<int> &arr) {
	bullets_current_collision_count = arr;
}

void MultiMeshBulletsData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_transforms"), &MultiMeshBulletsData2D::set_transforms);
	ClassDB::bind_method(D_METHOD("get_transforms"), &MultiMeshBulletsData2D::get_transforms);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "transforms"), "set_transforms", "get_transforms");

	ClassDB::bind_method(D_METHOD("get_sprite_frames"), &MultiMeshBulletsData2D::get_sprite_frames);
	ClassDB::bind_method(D_METHOD("set_sprite_frames", "new_sprite_frames"), &MultiMeshBulletsData2D::set_sprite_frames);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sprite_frames", PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames"), "set_sprite_frames", "get_sprite_frames");

	ClassDB::bind_method(D_METHOD("get_animation"), &MultiMeshBulletsData2D::get_animation);
	ClassDB::bind_method(D_METHOD("set_animation", "new_animation"), &MultiMeshBulletsData2D::set_animation);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "animation"), "set_animation", "get_animation");

	ClassDB::bind_method(D_METHOD("get_texture_size"), &MultiMeshBulletsData2D::get_texture_size);
	ClassDB::bind_method(D_METHOD("set_texture_size", "new_texture_size"), &MultiMeshBulletsData2D::set_texture_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "texture_size"), "set_texture_size", "get_texture_size");

	ClassDB::bind_method(D_METHOD("get_texture_rotation_radians"), &MultiMeshBulletsData2D::get_texture_rotation_radians);
	ClassDB::bind_method(D_METHOD("set_texture_rotation_radians", "new_texture_rotation_radians"), &MultiMeshBulletsData2D::set_texture_rotation_radians);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "texture_rotation_radians"), "set_texture_rotation_radians", "get_texture_rotation_radians");

	ClassDB::bind_method(D_METHOD("get_collision_layer"), &MultiMeshBulletsData2D::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "new_collision_layer"), &MultiMeshBulletsData2D::set_collision_layer);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer"), "set_collision_layer", "get_collision_layer");

	ClassDB::bind_method(D_METHOD("get_collision_mask"), &MultiMeshBulletsData2D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "new_collision_mask"), &MultiMeshBulletsData2D::set_collision_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask"), "set_collision_mask", "get_collision_mask");

	ClassDB::bind_method(D_METHOD("get_collision_shape"), &MultiMeshBulletsData2D::get_collision_shape);
	ClassDB::bind_method(D_METHOD("set_collision_shape", "new_shape"), &MultiMeshBulletsData2D::set_collision_shape);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "collision_shape", PROPERTY_HINT_RESOURCE_TYPE, "Shape2D"), "set_collision_shape", "get_collision_shape");

	ClassDB::bind_method(D_METHOD("get_collision_shape_offset"), &MultiMeshBulletsData2D::get_collision_shape_offset);
	ClassDB::bind_method(D_METHOD("set_collision_shape_offset", "new_collision_shape_offset"), &MultiMeshBulletsData2D::set_collision_shape_offset);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "collision_shape_offset"), "set_collision_shape_offset", "get_collision_shape_offset");

	ClassDB::bind_method(D_METHOD("get_monitorable"), &MultiMeshBulletsData2D::get_monitorable);
	ClassDB::bind_method(D_METHOD("set_monitorable", "new_monitorable"), &MultiMeshBulletsData2D::set_monitorable);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitorable"), "set_monitorable", "get_monitorable");

	ClassDB::bind_method(D_METHOD("get_bullets_custom_data"), &MultiMeshBulletsData2D::get_bullets_custom_data);
	ClassDB::bind_method(D_METHOD("set_bullets_custom_data", "new_bullets_custom_data"), &MultiMeshBulletsData2D::set_bullets_custom_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bullets_custom_data"), "set_bullets_custom_data", "get_bullets_custom_data");

	ClassDB::bind_method(D_METHOD("get_max_life_time"), &MultiMeshBulletsData2D::get_max_life_time);
	ClassDB::bind_method(D_METHOD("set_max_life_time", "new_max_life_time"), &MultiMeshBulletsData2D::set_max_life_time);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_life_time"), "set_max_life_time", "get_max_life_time");

	ClassDB::bind_method(D_METHOD("get_material"), &MultiMeshBulletsData2D::get_material);
	ClassDB::bind_method(D_METHOD("set_material", "new_material"), &MultiMeshBulletsData2D::set_material);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material"), "set_material", "get_material");

	ClassDB::bind_method(D_METHOD("get_mesh"), &MultiMeshBulletsData2D::get_mesh);
	ClassDB::bind_method(D_METHOD("set_mesh", "new_mesh"), &MultiMeshBulletsData2D::set_mesh);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh"), "set_mesh", "get_mesh");

	ClassDB::bind_method(D_METHOD("get_all_bullet_rotation_data"), &MultiMeshBulletsData2D::get_all_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("set_all_bullet_rotation_data", "new_data"), &MultiMeshBulletsData2D::set_all_bullet_rotation_data);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "all_bullet_rotation_data"), "set_all_bullet_rotation_data", "get_all_bullet_rotation_data");

	ClassDB::bind_method(D_METHOD("get_rotate_only_textures"), &MultiMeshBulletsData2D::get_rotate_only_textures);
	ClassDB::bind_method(D_METHOD("set_rotate_only_textures", "new_rotate_only_textures"), &MultiMeshBulletsData2D::set_rotate_only_textures);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rotate_only_textures"), "set_rotate_only_textures", "get_rotate_only_textures");

	ClassDB::bind_method(D_METHOD("get_is_texture_rotation_permanent"), &MultiMeshBulletsData2D::get_is_texture_rotation_permanent);
	ClassDB::bind_method(D_METHOD("set_is_texture_rotation_permanent", "new_is_texture_rotation_permanent"), &MultiMeshBulletsData2D::set_is_texture_rotation_permanent);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_texture_rotation_permanent"), "set_is_texture_rotation_permanent", "get_is_texture_rotation_permanent");

	ClassDB::bind_method(D_METHOD("get_z_index"), &MultiMeshBulletsData2D::get_z_index);
	ClassDB::bind_method(D_METHOD("set_z_index", "new_z_index"), &MultiMeshBulletsData2D::set_z_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "z_index"), "set_z_index", "get_z_index");

	ClassDB::bind_method(D_METHOD("get_light_mask"), &MultiMeshBulletsData2D::get_light_mask);
	ClassDB::bind_method(D_METHOD("set_light_mask", "new_light_mask"), &MultiMeshBulletsData2D::set_light_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "light_mask"), "set_light_mask", "get_light_mask");

	ClassDB::bind_method(D_METHOD("get_visibility_layer"), &MultiMeshBulletsData2D::get_visibility_layer);
	ClassDB::bind_method(D_METHOD("set_visibility_layer", "new_visibility_layer"), &MultiMeshBulletsData2D::set_visibility_layer);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "visibility_layer"), "set_visibility_layer", "get_visibility_layer");

	ClassDB::bind_method(D_METHOD("set_collision_layer_from_array", "array_of_layers"), &MultiMeshBulletsData2D::set_collision_layer_from_array);
	ClassDB::bind_method(D_METHOD("set_collision_mask_from_array", "array_of_masks"), &MultiMeshBulletsData2D::set_collision_mask_from_array);
	ClassDB::bind_method(D_METHOD("set_light_mask_from_array", "array_of_light_masks"), &MultiMeshBulletsData2D::set_light_mask_from_array);
	ClassDB::bind_method(D_METHOD("set_visibility_layer_from_array", "array_of_visibility_layers"), &MultiMeshBulletsData2D::set_visibility_layer_from_array);

	ClassDB::bind_method(D_METHOD("get_instance_shader_parameters"), &MultiMeshBulletsData2D::get_instance_shader_parameters);
	ClassDB::bind_method(D_METHOD("set_instance_shader_parameters", "new_instance_shader_parameters"), &MultiMeshBulletsData2D::set_instance_shader_parameters);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "instance_shader_parameters", PROPERTY_HINT_TYPE_STRING, "String:Variant"), "set_instance_shader_parameters", "get_instance_shader_parameters");

	ClassDB::bind_method(D_METHOD("get_is_life_time_over_signal_enabled"), &MultiMeshBulletsData2D::get_is_life_time_over_signal_enabled);
	ClassDB::bind_method(D_METHOD("set_is_life_time_over_signal_enabled"), &MultiMeshBulletsData2D::set_is_life_time_over_signal_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_life_time_over_signal_enabled"), "set_is_life_time_over_signal_enabled", "get_is_life_time_over_signal_enabled");

	ClassDB::bind_method(D_METHOD("get_is_life_time_infinite"), &MultiMeshBulletsData2D::get_is_life_time_infinite);
	ClassDB::bind_method(D_METHOD("set_is_life_time_infinite", "value"), &MultiMeshBulletsData2D::set_is_life_time_infinite);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_life_time_infinite"), "set_is_life_time_infinite", "get_is_life_time_infinite");

	ClassDB::bind_method(D_METHOD("get_stop_rotation_when_max_reached"), &MultiMeshBulletsData2D::get_stop_rotation_when_max_reached);
	ClassDB::bind_method(D_METHOD("set_stop_rotation_when_max_reached"), &MultiMeshBulletsData2D::set_stop_rotation_when_max_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "stop_rotation_when_max_reached"), "set_stop_rotation_when_max_reached", "get_stop_rotation_when_max_reached");

	ClassDB::bind_method(D_METHOD("get_bullet_max_collision_count"), &MultiMeshBulletsData2D::get_bullet_max_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullet_max_collision_count", "value"), &MultiMeshBulletsData2D::set_bullet_max_collision_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bullet_max_collision_count"), "set_bullet_max_collision_count", "get_bullet_max_collision_count");

	ClassDB::bind_method(D_METHOD("get_bullets_current_collision_count"), &MultiMeshBulletsData2D::get_bullets_current_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullets_current_collision_count", "arr"), &MultiMeshBulletsData2D::set_bullets_current_collision_count);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bullets_current_collision_count"), "set_bullets_current_collision_count", "get_bullets_current_collision_count");

	ClassDB::bind_static_method("MultiMeshBulletsData2D", D_METHOD("calculate_bitmask", "numbers"), &MultiMeshBulletsData2D::calculate_bitmask);


}
} //namespace BlastBullets2D
