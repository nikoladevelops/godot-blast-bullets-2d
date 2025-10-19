#pragma once

#include "../shared/bullet_rotation_data2d.hpp"

#include <godot_cpp/classes/canvas_item_material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/texture2d.hpp>

namespace BlastBullets2D {
using namespace godot;

class MultiMeshBulletsData2D : public Resource {
	GDCLASS(MultiMeshBulletsData2D, Resource)
public:
	// TEXTURE RELATED

	// All textures. If you give an array containing more than 1 texture then the default_change_texture_time will be used to periodically change the texture to the next one in the array.
	TypedArray<Texture2D> textures;

	// The default texture that is used if no textures were given inside the textures array
	Ref<Texture2D> default_texture = nullptr;

	// The texture amount_bullets. Keep in mind that this will be used only if a mesh was NOT provided
	Vector2 texture_size = Vector2(32, 32);

	// The texture rotation in radians. Change the value of this if you see that your texture is not rotated correctly. Example: If you want to rotate the texture 90 degrees more you would set the value to 90*PI/180
	real_t texture_rotation_radians = 0.0f;

	// Determines the starting texture in the textures array (by default it's the first texture in the array, so it's index 0). Make sure to provide an index that actually exists.
	int current_texture_index = 0;

	// Determines the time before the multimesh changes its texture to the next one in the array of textures. Because of this, animation is possible.
	double default_change_texture_time = 0.3f;

	// Determines the time before the multimesh changes its texture to the next one in the array of textures. Each time saved in the array corresponds to each texture inside the textures array. If you do NOT provide any data here then default_change_texture_time will be used by default
	TypedArray<double> change_texture_times;

	// Whether the rotation of the texture should never change depending on the direction the bullets move in
	bool is_texture_rotation_permanent = false;

	// The Z index of all bullets being spawned
	int z_index = 0;

	// BULLET MOVEMENT RELATED

	// Determines the rotation and position of each bullet. The array amount_bullets determines the amount of projectiles to render.
	TypedArray<Transform2D> transforms;

	// BULLET ROTATION RELATED

	// Stores optional BulletRotationData2D for each bullet. If you want the bullets to rotate, you HAVE to provide AT LEAST 1 BulletRotationData2D that will be used for every single bullet. If you want to have bullets that rotate differently then you need to provide the same amount of BulletRotationData2D as the .size() of the transforms (in other words for every bullet). If you provide less than .size() only the first data will be used for all bullets. Note that BulletRotationData2D has a helper static method that you can use to generate random rotation data - BulletRotationData2D.generate_random_data()
	TypedArray<BlastBullets2D::BulletRotationData2D> all_bullet_rotation_data;

	// If set to false, it will also rotate the collision shapes according to the BulletRotationData2D that was provided (it might decrease performance a little bit)
	bool rotate_only_textures = true;

	// If set to true, it will stop the rotation when the max rotation speed is reached
	bool stop_rotation_when_max_reached = false;

	// COLLISION RELATED

	// How many times a single bullet can collide before being disabled. If you set to 0 the bullet will never be disabled due to collisions.
	int bullet_max_collision_amount = 1;

	// The collision layer that all bullets share. Note: pass a bitmask, it's not just a simple int. Use the calculate_bitmask function.
	int collision_layer = 1;

	// The collision mask that all bullets share. Note: pass a bitmask, it's not just a simple int. Use the calculate_bitmask function.
	int collision_mask = 1;

	// The collision shape is always a rectangle. This determines the width and height it has.
	Vector2 collision_shape_size = Vector2(5, 5);
	// Determines the offset of the collision shape (the collision shape is by default at the center of the texture, but with this you are able to control it's position)
	Vector2 collision_shape_offset = Vector2(0, 0);

	// If set to true it would mean it can detect bodies. I suggest you do NOT enable it, because it tanks performance, but I left it just in case someone is stubborn and has that need. Instead consider adding an Area2D to the body that you are trying to damage and set up its collision layer correctly so that the bullets can interact with it.
	bool monitorable = false;

	// The idea is that you can enter additional data (base damage,armor damage,maybe healing factor,vampire bullets etc..). I am not going to force every single bullet to have a damage, because I don't know what kind of game you're making, so you are free to give any data here that will be available inside the area_entered and body_entered callbacks inside factory :) Also note that if you want that data to also be saved you should include @export keywords for each member inside your custom data resource.
	Ref<Resource> bullets_custom_data;

	// OTHER

	// Light mask. Note: pass a bitmask, it's not just a simple int. Use the calculate_bitmask function.
	int light_mask = 1;

	// Visibility layer. Note: pass a bitmask, it's not just a simple int. Use the calculate_bitmask function.
	int visibility_layer = 1;

	// How long will the bullets last, before being disabled. Depending on whether the bullets pool has reached its limit, it will either add the bullets to the pool or it will queue_free them.
	double max_life_time = 2.0f;

	// Whether the life_time_over signal will be emitted when the life time of the bullets is over. Tracked by BulletFactory2D
	bool is_life_time_over_signal_enabled = false;

	// Whether the lifetime is infinite
	bool is_life_time_infinite = false;

	// You can assign a custom material that uses a shader. Note that you may also want to provide a custom mesh as well, but if you do so, then the texture_size property won't be used, instead handle scaling in the shader as well.
	Ref<Material> material;

	// If you use a ShaderMaterial with instance uniforms, you can pass them here as String - Variant pairs
	Dictionary instance_shader_parameters;

	// Custom mesh, if it isn't provided then a Quadmesh will be generated and it will use the texture_size. If you DO provide a mesh then you should handle the scaling of the bullets yourself using a shader for best quality.
	Ref<Mesh> mesh;

	// If you want your bullet to have other things attached to it (particles/effects whatever you want), you should pass here a packed scene that contains a BulletAttachment2D node. Note that if you pass a packed scene that contains something different from a BulletAttachment2D then the project will crash (just ensure that your packed scene contains an actual BulletAttachment2D and attach as many children as you want to it - particles, other nodes, whatever you want)
	Ref<PackedScene> bullet_attachment_scene = nullptr;

	// The offset of the bullet attachment relative to the center of the bullet
	Vector2 bullet_attachment_offset;

	// Used to acquire a bitmask from an array of integer values. Useful when setting the collision layer and collision mask. Example: you want your bullets to be in collision layer 1,2,3,7, you would pass an array of these numbers and the value that gets returned is the value you need to set to the collision_layer. Pass ONLY POSITIVE NUMBERS (NEVER PASS NEGATIVE OR ZERO)
	static int calculate_bitmask(const TypedArray<int> &numbers);

	// GETTERS AND SETTERS

	bool get_is_life_time_infinite() const;
	void set_is_life_time_infinite(bool value);

	TypedArray<Transform2D> get_transforms() const;
	void set_transforms(const TypedArray<Transform2D> &new_transforms);

	TypedArray<Texture2D> get_textures() const;
	void set_textures(const TypedArray<Texture2D> &new_textures);

	Vector2 get_texture_size() const;
	void set_texture_size(Vector2 new_texture_size);

	real_t get_texture_rotation_radians() const;
	void set_texture_rotation_radians(real_t new_texture_rotation_radians);

	int get_current_texture_index() const;
	void set_current_texture_index(int new_current_texture_index);

	double get_default_change_texture_time() const;
	void set_default_change_texture_time(double new_default_change_texture_time);

	int get_collision_layer() const;
	void set_collision_layer(int new_collision_layer);
	void set_collision_layer_from_array(const TypedArray<int> &numbers);

	int get_collision_mask() const;
	void set_collision_mask(int new_collision_mask);
	void set_collision_mask_from_array(const TypedArray<int> &numbers);

	Vector2 get_collision_shape_size() const;
	void set_collision_shape_size(const Vector2 &new_collision_shape_size);

	Vector2 get_collision_shape_offset() const;
	void set_collision_shape_offset(const Vector2 &new_collision_shape_offset);

	bool get_monitorable() const;
	void set_monitorable(bool new_monitorable);

	Ref<Resource> get_bullets_custom_data() const;
	void set_bullets_custom_data(const Ref<Resource> &new_bullets_custom_data);

	double get_max_life_time() const;
	void set_max_life_time(double new_max_life_time);

	Ref<Material> get_material() const;
	void set_material(const Ref<Material> &new_material);

	Ref<Mesh> get_mesh() const;
	void set_mesh(const Ref<Mesh> &new_mesh);

	TypedArray<BulletRotationData2D> get_all_bullet_rotation_data() const;
	void set_all_bullet_rotation_data(const TypedArray<BulletRotationData2D> &new_data);

	bool get_rotate_only_textures() const;
	void set_rotate_only_textures(bool new_rotate_only_textures);

	bool get_is_texture_rotation_permanent() const;
	void set_is_texture_rotation_permanent(bool new_is_texture_rotation_permanent);

	Vector2 get_bullet_attachment_offset() const;
	void set_bullet_attachment_offset(const Vector2 &new_bullet_attachment_offset);

	Ref<PackedScene> get_bullet_attachment_scene() const;
	void set_bullet_attachment_scene(const Ref<PackedScene> &new_bullet_attachment_scene);

	int get_z_index() const;
	void set_z_index(int new_z_index);

	int get_light_mask() const;
	void set_light_mask(int new_light_mask);
	void set_light_mask_from_array(const TypedArray<int> &numbers);

	int get_visibility_layer() const;
	void set_visibility_layer(int new_visibility_layer);
	void set_visibility_layer_from_array(const TypedArray<int> &numbers);

	Dictionary get_instance_shader_parameters() const;
	void set_instance_shader_parameters(const Dictionary &new_instance_shader_parameters);

	TypedArray<double> get_change_texture_times() const;
	void set_change_texture_times(const TypedArray<double> &new_change_texture_times);

	Ref<Texture2D> get_default_texture() const;
	void set_default_texture(const Ref<Texture2D> &new_default_texture);

	bool get_is_life_time_over_signal_enabled() const;
	void set_is_life_time_over_signal_enabled(bool new_is_life_time_over_signal_enabled);

	bool get_stop_rotation_when_max_reached() const;
	void set_stop_rotation_when_max_reached(bool new_stop_rotation_when_max_reached);

	int get_bullet_max_collision_amount() const;
	void set_bullet_max_collision_amount(int new_max_collision_amount);

	static void _bind_methods();
};
} //namespace BlastBullets2D
