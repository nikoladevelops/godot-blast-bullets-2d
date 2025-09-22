#pragma once

#include "../shared/bullet_rotation_data2d.hpp"

#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/texture2d.hpp>


namespace BlastBullets2D {
using namespace godot;

class SaveDataMultiMeshBullets2D : public Resource {
	GDCLASS(SaveDataMultiMeshBullets2D, Resource)

public:
	// Note: If you ever plan on extending and putting more values that need to be saved, always initialize them to a default value. This is incredibly important since Godot's ResourceSaver has some optimizations that rely on those values to save disk space. If you ignore this advice you will experience lots of random values being saved (unreliable save files) or even random crashes

	// TEXTURE RELATED
	TypedArray<Texture2D> textures = TypedArray<Texture2D>();

	Vector2 texture_size = Vector2(0, 0);

	double current_change_texture_time = 0.0f;

	int current_texture_index = 0;

	int z_index = 0;

	real_t cache_texture_rotation_radians = 0.0f;

	TypedArray<double> change_texture_times;

	// BULLET MOVEMENT RELATED

	// Holds all multimesh instance transforms. I am doing this so I don't have to call multi->get_instance_transform_2d() every frame
	TypedArray<Transform2D> all_cached_instance_transforms = TypedArray<Transform2D>();

	// Holds all collision shape transforms. I am doing this so I don't have to call physics_server->area_get_shape_transform() every frame
	TypedArray<Transform2D> all_cached_shape_transforms = TypedArray<Transform2D>();

	// Holds all multimesh instance transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
	TypedArray<Vector2> all_cached_instance_origin = TypedArray<Vector2>();

	// Holds all collision shape transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
	TypedArray<Vector2> all_cached_shape_origin = TypedArray<Vector2>();

	// Holds all calculated velocities for the bullets. I am doing this to avoid unnecessary calculations. If I know the direction -> calculate the velocity. Update the values only when the velocity changes, otherwise it's just unnecessary to always do Vector2(cos, sin) every frame..
	TypedArray<Vector2> all_cached_velocity = TypedArray<Vector2>();

	// Holds all cached directions of the bullets
	TypedArray<Vector2> all_cached_direction = TypedArray<Vector2>();

	TypedArray<BulletRotationData2D> all_bullet_rotation_data = TypedArray<BulletRotationData2D>();

	bool rotate_only_textures = false;

	bool stop_rotation_when_max_reached = false;

	// BULLET SPEED RELATED

	TypedArray<real_t> all_cached_speed = TypedArray<real_t>();
	TypedArray<real_t> all_cached_max_speed = TypedArray<real_t>();
	TypedArray<real_t> all_cached_acceleration = TypedArray<real_t>();

	// COLLISION RELATED

	int collision_layer = 1;
	int collision_mask = 1;

	Vector2 collision_shape_size = Vector2(1, 1);

	TypedArray<bool> bullets_enabled_status = TypedArray<bool>();

	bool monitorable = false;

	// BULLET ATTACHMENT RELATED
	int attachment_id = 0;

	bool cache_stick_relative_to_bullet = false;

	bool is_bullet_attachment_provided = false;

	Transform2D bullet_attachment_local_transform = Transform2D();

	Ref<PackedScene> bullet_attachment_scene = nullptr;

	TypedArray<Resource> bullet_attachments_custom_data = TypedArray<Resource>();

	TypedArray<Transform2D> attachment_transforms = TypedArray<Transform2D>();

	//

	// OTHER

	int light_mask = 1;

	int visibility_layer = 1;

	double max_life_time = 0.0f;

	bool is_life_time_over_signal_enabled = false;

	double current_life_time = 0.0f;

	int amount_bullets = 0;

	Ref<Resource> bullets_custom_data = nullptr;

	Ref<Material> material = nullptr;
	Ref<Mesh> mesh = nullptr;

	Dictionary instance_shader_parameters;

	// GETTERS AND SETTERS

	void set_max_life_time(double new_max_life_time);
	double get_max_life_time() const;

	void set_current_life_time(double new_current_life_time);
	double get_current_life_time() const;

	void set_size(int new_size);
	int get_size() const;

	void set_bullets_enabled_status(TypedArray<bool> new_bullets_enabled_status);
	TypedArray<bool> get_bullets_enabled_status() const;

	void set_bullets_custom_data(Ref<Resource> new_bullets_custom_data);
	Ref<Resource> get_bullets_custom_data() const;

	void set_textures(TypedArray<Texture2D> new_textures);
	TypedArray<Texture2D> get_textures() const;

	void set_texture_size(Vector2 new_texture_size);
	Vector2 get_texture_size() const;

	void set_current_change_texture_time(double new_current_change_texture_time);
	double get_current_change_texture_time() const;

	void set_current_texture_index(int new_current_texture_index);
	int get_current_texture_index() const;

	void set_collision_layer(int new_collision_layer);
	int get_collision_layer() const;

	void set_collision_mask(int new_collision_mask);
	int get_collision_mask() const;

	void set_collision_shape_size(Vector2 new_collision_shape_size);
	Vector2 get_collision_shape_size() const;

	void set_monitorable(bool new_monitorable);
	bool get_monitorable() const;

	void set_material(Ref<Material> new_material);
	Ref<Material> get_material() const;

	void set_mesh(Ref<Mesh> new_mesh);
	Ref<Mesh> get_mesh() const;

	TypedArray<BulletRotationData2D> get_all_bullet_rotation_data();
	void set_all_bullet_rotation_data(const TypedArray<BulletRotationData2D> &new_data);

	bool get_rotate_only_textures();
	void set_rotate_only_textures(bool new_rotate_only_textures);

	// BULLET ATTACHMENT RELATED

	void set_cache_stick_relative_to_bullet(bool value);
	bool get_cache_stick_relative_to_bullet() const;

	void set_is_bullet_attachment_provided(bool value);
	bool get_is_bullet_attachment_provided() const;

	void set_bullet_attachment_local_transform(const Transform2D &value);
	Transform2D get_bullet_attachment_local_transform() const;

	void set_bullet_attachment_scene(const Ref<PackedScene> &value);
	Ref<PackedScene> get_bullet_attachment_scene() const;

	void set_bullet_attachments_custom_data(const TypedArray<Resource> &value);
	TypedArray<Resource> get_bullet_attachments_custom_data() const;

	void set_attachment_transforms(const TypedArray<Transform2D> new_data);
	TypedArray<Transform2D> get_attachment_transforms() const;

	// MOVEMENT RELATED
	void set_all_cached_instance_transforms(const TypedArray<Transform2D> new_data);
	TypedArray<Transform2D> get_all_cached_instance_transforms();

	void set_all_cached_shape_transforms(const TypedArray<Transform2D> new_data);
	TypedArray<Transform2D> get_all_cached_shape_transforms();

	void set_all_cached_instance_origin(const TypedArray<Vector2> new_data);
	TypedArray<Vector2> get_all_cached_instance_origin();

	void set_all_cached_shape_origin(const TypedArray<Vector2> new_data);
	TypedArray<Vector2> get_all_cached_shape_origin();

	void set_all_cached_velocity(const TypedArray<Vector2> new_data);
	TypedArray<Vector2> get_all_cached_velocity();

	void set_all_cached_direction(const TypedArray<Vector2> new_data);
	TypedArray<Vector2> get_all_cached_direction();

	// SPEED RELATED

	void set_all_cached_speed(const TypedArray<real_t> new_data);
	TypedArray<real_t> get_all_cached_speed();

	void set_all_cached_max_speed(const TypedArray<real_t> new_data);
	TypedArray<real_t> get_all_cached_max_speed();

	void set_all_cached_acceleration(const TypedArray<real_t> new_data);
	TypedArray<real_t> get_all_cached_acceleration();

	void set_attachment_id(int new_attachment_id);
	int get_attachment_id();

	int get_z_index() const;
	void set_z_index(int new_z_index);

	int get_light_mask() const;
	void set_light_mask(int new_light_mask);

	int get_visibility_layer() const;
	void set_visibility_layer(int new_visibility_layer);

	Dictionary get_instance_shader_parameters() const;
	void set_instance_shader_parameters(const Dictionary &new_instance_shader_parameters);

	real_t get_cache_texture_rotation_radians() const;
	void set_cache_texture_rotation_radians(real_t new_cache_texture_rotation_radians);

	TypedArray<double> get_change_texture_times() const;
	void set_change_texture_times(const TypedArray<double> &new_change_texture_times);

	bool get_is_life_time_over_signal_enabled() const;
	void set_is_life_time_over_signal_enabled(bool new_is_life_time_over_signal_enabled);

	bool get_stop_rotation_when_max_reached() const;
	void set_stop_rotation_when_max_reached(bool new_stop_rotation_when_max_reached);

	static void _bind_methods();
};
} //namespace BlastBullets2D