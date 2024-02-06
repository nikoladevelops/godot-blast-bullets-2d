#ifndef SAVE_DATA_BLOCK_BULLETS2D
#define SAVE_DATA_BLOCK_BULLETS2D

#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/classes/texture2d.hpp"
#include "godot_cpp/classes/material.hpp"
#include "godot_cpp/classes/mesh.hpp"
#include "../shared/bullet_rotation_data.hpp"

using namespace godot;
class SaveDataBlockBullets2D : public Resource{
    GDCLASS(SaveDataBlockBullets2D, Resource);
    public:
        TypedArray<Transform2D> transforms;
        Vector2 velocity;
        Vector2 current_position;

        float max_life_time;
        float current_life_time;

        int size;
        // Notice how I have a typed array here , not std::vector<bool>, this is so that im sure the values will be saved/loaded by ResourceSaver!
        TypedArray<bool> bullets_enabled_status;

        Ref<Resource> bullets_custom_data; // yes even custom data will be saved as long as you've put @export keywords

        TypedArray<Texture2D> textures;
        float texture_rotation_radians;
        Vector2 texture_size;
        float max_change_texture_time = 0.0f;
        float current_change_texture_time;
        int current_texture_index=0;

        float block_rotation_radians;
        float max_speed;
        float speed;
        float acceleration;

        float max_acceleration_time = 0.0f;
        float current_acceleration_time;

        int collision_layer;
        int collision_mask;

        Vector2 collision_shape_size;
        Vector2 collision_shape_offset; // todo

        bool monitorable;
        Ref<Material> material;
        Ref<Mesh> mesh;

        TypedArray<BulletRotationData> all_bullet_rotation_data;

        // If set to false, it will also rotate the collision shapes
        bool rotate_only_textures=true;

        // The default behaviour is for the texture of each bullet to be rotated according to the rotation of the bullet's transform + texture_rotation_radians. If for some reason you want only the texture_rotation_radians to be used, no matter how the transform is rotated then you need to set this to true.
        bool is_texture_rotation_permanent=false;

        void set_transforms(TypedArray<Transform2D> new_transforms);
        TypedArray<Transform2D> get_transforms() const;

        void set_velocity(Vector2 new_velocity);
        Vector2 get_velocity() const;

        void set_current_position(Vector2 new_current_position);
        Vector2 get_current_position() const;

        void set_max_life_time(float new_max_life_time);
        float get_max_life_time() const;

        void set_current_life_time(float new_current_life_time);
        float get_current_life_time() const;

        void set_size(int new_size);
        int get_size() const;

        void set_bullets_enabled_status(TypedArray<bool> new_bullets_enabled_status);
        TypedArray<bool> get_bullets_enabled_status() const;

        void set_bullets_custom_data(Ref<Resource> new_bullets_custom_data);
        Ref<Resource> get_bullets_custom_data() const;

        void set_textures(TypedArray<Texture2D> new_textures);
        TypedArray<Texture2D> get_textures() const;

        void set_texture_rotation_radians(float new_texture_rotation_radians);
        float get_texture_rotation_radians() const;

        void set_texture_size(Vector2 new_texture_size);
        Vector2 get_texture_size() const;

        void set_max_change_texture_time(float new_max_change_texture_time);
        float get_max_change_texture_time() const;

        void set_current_change_texture_time(float new_current_change_texture_time);
        float get_current_change_texture_time() const;

        void set_current_texture_index(int new_current_texture_index);
        int get_current_texture_index() const;

        void set_block_rotation_radians(float new_block_rotation_radians);
        float get_block_rotation_radians() const;

        void set_max_speed(float new_max_speed);
        float get_max_speed() const;

        void set_speed(float new_speed);
        float get_speed() const;

        void set_acceleration(float new_acceleration);
        float get_acceleration() const;

        void set_max_acceleration_time(float new_max_acceleration_time);
        float get_max_acceleration_time() const;

        void set_current_acceleration_time(float new_current_acceleration_time);
        float get_current_acceleration_time() const;

        void set_collision_layer(int new_collision_layer);
        int get_collision_layer() const;

        void set_collision_mask(int new_collision_mask);
        int get_collision_mask() const;

        void set_collision_shape_size(Vector2 new_collision_shape_size);
        Vector2 get_collision_shape_size() const;

        void set_collision_shape_offset(Vector2 new_collision_shape_offset);
        Vector2 get_collision_shape_offset() const;

        void set_monitorable(bool new_monitorable);
        bool get_monitorable() const;

        void set_material(Ref<Material> new_material);
        Ref<Material> get_material() const;

        void set_mesh(Ref<Mesh> new_mesh);
        Ref<Mesh> get_mesh() const;

        TypedArray<BulletRotationData> get_all_bullet_rotation_data();
        void set_all_bullet_rotation_data(const TypedArray<BulletRotationData>& new_data);

        bool get_rotate_only_textures();
        void set_rotate_only_textures(bool new_rotate_only_textures);

        bool get_is_texture_rotation_permanent();
        void set_is_texture_rotation_permanent(bool new_is_texture_rotation_permanent);

    protected:
        static void _bind_methods();
};

#endif