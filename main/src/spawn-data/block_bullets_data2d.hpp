#ifndef BLOCK_BULLETS_DATA2D
#define BLOCK_BULLETS_DATA2D

#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/classes/texture2d.hpp"
#include "godot_cpp/classes/canvas_item_material.hpp"
#include "godot_cpp/classes/mesh.hpp"

using namespace godot;

class BlockBulletsData2D : public Resource{
    GDCLASS(BlockBulletsData2D, Resource);

    public:
        // All textures. If you give an array containing more than 1 texture then the change_texture_time will be used to periodically change the texture.
        TypedArray<Texture2D> textures;
        // The texture size. Keep in mind that this will be used only if a mesh was NOT provided. 
        Vector2 texture_size = Vector2(32,32);
        // The texture rotation in radians
        float texture_rotation_radians=0.0f;
        // Determines the starting texture.
        int current_texture_index=0;
        // Determines the rotation and position of each bullet. The array size determines the amount of projectiles to render.
        TypedArray<Transform2D> transforms;
        // The rotation of the whole block of projectiles. It determines the direction in which the projectiles move. The projectiles always go towards the X axis according to this rotation.
        float block_rotation_radians=0.0f;
        // Determines the speed of all projectiles.
        float speed=10.0f;
        // Determines the max_speed of all projectiles. If max_speed is set to -1, it means there is no max speed so no acceleration will be used. Note that it's possible to enter negative values if you wish to implement bullets slowing down/moving backwards eventually. Make sure that the speed reaches the max_speed, otherwise the bullets will continue accelerating.
        float max_speed = -1.0f; 
        // Determines the acceleration rate every acceleration_time seconds by which the speed increases until reaching max_speed. Note that it's possible to enter negative values if you wish to implement bullets slowing down/moving backwards eventually. Make sure that the speed reaches the max_speed, otherwise the bullets will continue accelerating.
        float acceleration=2.0f;
        // The time needed for the acceleration to be applied to the speed. It basically controls how fast the max_speed will be reached by applying acceleration every N seconds.
        float max_acceleration_time=0.3f;
        
        // Determines the time before the multimesh changes its texture to the next one in the array of textures.
        float max_change_texture_time=0.3f;

        // Note: pass a bitmask, it's not just a simple int. Use the function inside the bullet_factory if you don't know what that means.
        uint32_t collision_layer=0;
        // Note: pass a bitmask, it's not just a simple int. Use the function inside the bullet_factory if you don't know what that means.
        uint32_t collision_mask=0;

        // The collision shape is always a rectangle. This determines the width and height it has.
        Vector2 collision_shape_size=Vector2(5,5);
        // Determines the offset of the collision shape (the collision shape is by default at the center of the texture, but with this you are able to control it's position)
        Vector2 collision_shape_offset = Vector2(0,0);

        // If set to true it would mean it can detect bodies. I suggest you do NOT enable it, because it tanks performance, but I left it just in case someone is stubborn and has that need. Instead consider adding an Area2D to the body that you are trying to damage and set up its collision layer correctly so that the bullets can interact with it.
        bool monitorable = false;

        // The idea is that you can enter additional data (base damage,armor damage,maybe healing factor,vampire bullets etc..). I am not going to force every single bullet to have a damage, because I don't know what kind of game you're making, so you are free to give any data here that will be available inside the area_entered and body_entered callbacks inside factory :) Also note that if you want that data to also be saved you should include @export keywords for each member inside your custom data resource.
        Ref<Resource> bullets_custom_data;

        // How long will the bullets last, before being disabled. Depending on whether the bullets pool has reached its limit, it will either add the bullets to the pool or it will queue_free them, make sure to look inside the bullet factory to understand more.
        float max_life_time = 2.0f;

        // You can assign a custom material that uses a shader. Note that you may also want to provide a custom mesh as well, but if you do so, then the texture_size property won't be used, instead handle scaling in the shader as well.
        Ref<Material> material;
        // Custom mesh, if it isn't provided then a Quadmesh will be generated and it will use the texture_size. If you DO provide a mesh then you should handle the scaling of the bullets yourself using a shader for best quality.
        Ref<Mesh> mesh;


        // Setters and getters
        TypedArray<Texture2D> get_textures() const;
        void set_textures(const TypedArray<Texture2D>& new_textures);

        Vector2 get_texture_size() const;
        void set_texture_size(Vector2 new_texture_size);

        float get_texture_rotation_radians() const;
        void set_texture_rotation_radians(float new_texture_rotation_radians);

        int get_current_texture_index() const;
        void set_current_texture_index(int new_current_texture_index);

        TypedArray<Transform2D> get_transforms() const;
        void set_transforms(const TypedArray<Transform2D>& new_transforms);

        float get_block_rotation_radians() const;
        void set_block_rotation_radians(float new_block_rotation_radians);

        float get_speed() const;
        void set_speed(float new_speed);

        float get_max_speed() const;
        void set_max_speed(float new_max_speed);

        float get_acceleration() const;
        void set_acceleration(float new_acceleration);

        float get_max_acceleration_time() const;
        void set_max_acceleration_time(float new_max_acceleration_time);

        float get_max_change_texture_time() const;
        void set_max_change_texture_time(float new_max_change_texture_time);

        uint32_t get_collision_layer() const;
        void set_collision_layer(uint32_t new_collision_layer);

        uint32_t get_collision_mask() const;
        void set_collision_mask(uint32_t new_collision_mask);

        Vector2 get_collision_shape_size() const;
        void set_collision_shape_size(const Vector2& new_collision_shape_size);

        Vector2 get_collision_shape_offset() const;
        void set_collision_shape_offset(const Vector2& new_collision_shape_offset);

        bool get_monitorable() const;
        void set_monitorable(bool new_monitorable);

        Ref<Resource> get_bullets_custom_data() const;
        void set_bullets_custom_data(const Ref<Resource>& new_bullets_custom_data);

        float get_max_life_time() const;
        void set_max_life_time(float new_max_life_time);

        Ref<Material> get_material() const;
        void set_material(const Ref<Material>& new_material);

        Ref<Mesh> get_mesh() const;
        void set_mesh(const Ref<Mesh>& new_mesh);

    protected:
        static void _bind_methods();
};

#endif