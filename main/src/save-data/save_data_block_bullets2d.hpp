#ifndef SAVE_DATA_BLOCK_BULLETS2D
#define SAVE_DATA_BLOCK_BULLETS2D

#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/classes/texture2d.hpp"
#include "godot_cpp/classes/material.hpp"
#include "godot_cpp/classes/mesh.hpp"

#include "../shared/bullet_rotation_data.hpp"
#include "../shared/bullet_speed_data.hpp"

using namespace godot;
class SaveDataBlockBullets2D : public Resource{
    GDCLASS(SaveDataBlockBullets2D, Resource);
    public:
        // TEXTURE RELATED
        TypedArray<Texture2D> textures;

        Vector2 texture_size;

        float max_change_texture_time = 0.0f;

        float current_change_texture_time;

        int current_texture_index=0;

        // BULLET SPEED RELATED

        TypedArray<float> all_cached_speed;
        TypedArray<float> all_cached_max_speed;
        TypedArray<float> all_cached_acceleration;

        // BULLET MOVEMENT RELATED

        // Holds all multimesh instance transforms. I am doing this so I don't have to call multi->get_instance_transform_2d() every frame
        TypedArray<Transform2D> all_cached_instance_transforms;
        
        // Holds all collision shape transforms. I am doing this so I don't have to call physics_server->area_get_shape_transform() every frame
        TypedArray<Transform2D> all_cached_shape_transforms;

        // Holds all multimesh instance transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
        TypedArray<Vector2> all_cached_instance_origin;
        
        // Holds all collision shape transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
        TypedArray<Vector2> all_cached_shape_origin;

        // Holds all calculated velocities for the bullets. I am doing this to avoid unnecessary calculations. If I know the direction -> calculate the velocity. Update the values only when the velocity changes, otherwise it's just unnecessary to always do Vector2(cos, sin) every frame..
        TypedArray<Vector2> all_cached_velocity;

        // Holds all cached directions of the bullets
        TypedArray<Vector2> all_cached_direction;

        float block_rotation_radians;

        bool use_block_rotation_radians=false;

        // BULLET ROTATION RELATED

        TypedArray<BulletRotationData> all_bullet_rotation_data;

        bool rotate_only_textures=true;

        // COLLISION RELATED

        int collision_layer;
        int collision_mask;

        Vector2 collision_shape_size;

        TypedArray<bool> bullets_enabled_status;

        bool monitorable;

        // OTHER

        Vector2 multimesh_position;

        float max_life_time;

        float current_life_time;

        int size;

        Ref<Resource> bullets_custom_data; // yes even custom data will be saved as long as you've put @export keywords
    
        Ref<Material> material;
        Ref<Mesh> mesh;


        // GETTERS AND SETTERS

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

        TypedArray<BulletRotationData> get_all_bullet_rotation_data();
        void set_all_bullet_rotation_data(const TypedArray<BulletRotationData>& new_data);

        bool get_rotate_only_textures();
        void set_rotate_only_textures(bool new_rotate_only_textures);

        bool get_use_block_rotation_radians();
        void set_use_block_rotation_radians(bool new_use_block_rotation_radians);

        // MOVEMENT RELATED
        TypedArray<Transform2D> get_all_cached_instance_transforms();
        void set_all_cached_instance_transforms(const TypedArray<Transform2D> new_data);

        TypedArray<Transform2D> get_all_cached_shape_transforms();
        void set_all_cached_shape_transforms(const TypedArray<Transform2D> new_data);
        
        TypedArray<Vector2> get_all_cached_instance_origin();
        void set_all_cached_instance_origin(const TypedArray<Vector2> new_data);

        TypedArray<Vector2> get_all_cached_shape_origin();
        void set_all_cached_shape_origin(const TypedArray<Vector2> new_data);

        TypedArray<Vector2> get_all_cached_velocity();
        void set_all_cached_velocity(const TypedArray<Vector2> new_data);

        TypedArray<Vector2> get_all_cached_direction();
        void set_all_cached_direction(const TypedArray<Vector2> new_data);

        // SPEED RELATED

        TypedArray<float> get_all_cached_speed();
        void set_all_cached_speed(const TypedArray<float> new_data);

        TypedArray<float> get_all_cached_max_speed();
        void set_all_cached_max_speed(const TypedArray<float> new_data);

        TypedArray<float> get_all_cached_acceleration();
        void set_all_cached_acceleration(const TypedArray<float> new_data);

        Vector2 get_multimesh_position();
        void set_multimesh_position(Vector2 new_position);

    protected:
        static void _bind_methods();
};

#endif