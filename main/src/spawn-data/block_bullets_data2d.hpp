#ifndef BLOCK_BULLETS_DATA2D
#define BLOCK_BULLETS_DATA2D

#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/classes/texture2d.hpp"
#include "godot_cpp/classes/canvas_item_material.hpp"
#include "godot_cpp/classes/mesh.hpp"

#include "../shared/bullet_rotation_data.hpp"
#include "../shared/bullet_speed_data.hpp"

using namespace godot;

class BlockBulletsData2D : public Resource{
    GDCLASS(BlockBulletsData2D, Resource);

    public:
        // TEXTURE RELATED

        // All textures. If you give an array containing more than 1 texture then the max_change_texture_time will be used to periodically change the texture to the next one in the array.
        TypedArray<Texture2D> textures;

        // The texture size. Keep in mind that this will be used only if a mesh was NOT provided
        Vector2 texture_size = Vector2(32,32);

        // The texture rotation in radians. Change the value of this if you see that your texture is not rotated correctly. Example: If you want to rotate the texture 90 degrees more you would set the value to 90*PI/180
        float texture_rotation_radians=0.0f;

        // Determines the starting texture in the textures array (by default it's the first texture in the array, so it's index 0). Make sure to provide an index that actually exists.
        int current_texture_index=0;

        // Determines the time before the multimesh changes its texture to the next one in the array of textures. Because of this, animation is possible.
        float max_change_texture_time=0.3f;

        // The default behaviour is for the texture of each bullet to be rotated according to the rotation of the bullet's transform + texture_rotation_radians OR block_rotation_radians + texture_rotation_radians if use_block_rotation_radians is true. If for some reason you ONLY want the texture_rotation_radians to be used, then set this to true (basically all your textures will always be rotated in a specific angle, no matter in which direction they travel).
        bool is_texture_rotation_permanent=false;

        
        // BULLET MOVEMENT RELATED

        // Determines the rotation and position of each bullet. The array size determines the amount of projectiles to render. Each rotation in each transform is used to determine the corresponding bullet's direction (unless the use_block_rotation_radians is set to true)
        TypedArray<Transform2D> transforms;

        // Used only when use_block_rotation_radians is set to true. Provides the rotation in which all bullets move as a block with the BulletSpeedData. Note that this uses radians, to convert degrees to radians you would do degrees*PI/180.
        float block_rotation_radians=0.0f;

        // It FORCES all bullets to move as a block with the same exact speed, max_speed, acceleration and rotation (basically FORCES the use of only the first BulletSpeedData in the all_bullet_speed_data array). This actually boosts performance signifacantly, but might not look as good as bullets that travel in their own directions. This is literally the same as giving all_bullets_speed_data only one BulletSpeedData, it will apply the same optimization where the whole multimesh itself is being moved instead of each individual bullet instance. If set to false then each bullet's direction is determined by the bullet's transform's rotation inside the transforms array (which is the default way).
        bool use_block_rotation_radians=false;

        // You are required to pass AT LEAST 1 BulletSpeedData in order for the bullets to work. If you want each bullet to have different data (different speed/max speed/ acceleration for each bullet), you would provide the same amount of BulletSpeedData as the .size() of the transforms. If you provide less than .size(), the bullets will use only the first BulletSpeedData. Note that BulletSpeedData has a helper static method that you can use to generate random speed data - BulletSpeedData.generate_random_data()
        TypedArray<BulletSpeedData> all_bullet_speed_data;

        // BULLET ROTATION RELATED

        // Stores optional BulletRotationData for each bullet. If you want the bullets to rotate, you HAVE to provide AT LEAST 1 BulletRotationData that will be used for every single bullet. If you want to have bullets that rotate differently then you need to provide the same amount of BulletRotationData as the .size() of the transforms (in other words for every bullet). If you provide less than .size() only the first data will be used for all bullets. Note that BulletRotationData has a helper static method that you can use to generate random rotation data - BulletRotationData.generate_random_data()
        TypedArray<BulletRotationData> all_bullet_rotation_data;

        // If set to false, it will also rotate the collision shapes according to the BulletRotationData that was provided (it might decrease performance a little bit)
        bool rotate_only_textures=true;

        // COLLISION RELATED

        // Note: pass a bitmask, it's not just a simple int. Use the function inside the bullet_factory if you don't know what that means.
        uint32_t collision_layer=0;
        // Note: pass a bitmask, it's not just a simple int. Use the function inside the bullet_factory if you don't know what that means.
        uint32_t collision_mask=0;

        // The collision shape is always a rectangle. This determines the width and height it has.
        Vector2 collision_shape_size = Vector2(5,5);
        // Determines the offset of the collision shape (the collision shape is by default at the center of the texture, but with this you are able to control it's position)
        Vector2 collision_shape_offset = Vector2(0,0);

        // If set to true it would mean it can detect bodies. I suggest you do NOT enable it, because it tanks performance, but I left it just in case someone is stubborn and has that need. Instead consider adding an Area2D to the body that you are trying to damage and set up its collision layer correctly so that the bullets can interact with it.
        bool monitorable = false;

        // The idea is that you can enter additional data (base damage,armor damage,maybe healing factor,vampire bullets etc..). I am not going to force every single bullet to have a damage, because I don't know what kind of game you're making, so you are free to give any data here that will be available inside the area_entered and body_entered callbacks inside factory :) Also note that if you want that data to also be saved you should include @export keywords for each member inside your custom data resource.
        Ref<Resource> bullets_custom_data;

        // OTHER

        // How long will the bullets last, before being disabled. Depending on whether the bullets pool has reached its limit, it will either add the bullets to the pool or it will queue_free them.
        float max_life_time = 2.0f;

        // You can assign a custom material that uses a shader. Note that you may also want to provide a custom mesh as well, but if you do so, then the texture_size property won't be used, instead handle scaling in the shader as well.
        Ref<Material> material;
        // Custom mesh, if it isn't provided then a Quadmesh will be generated and it will use the texture_size. If you DO provide a mesh then you should handle the scaling of the bullets yourself using a shader for best quality.
        Ref<Mesh> mesh;
        

        // GETTERS AND SETTERS
        
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

        TypedArray<BulletRotationData> get_all_bullet_rotation_data();
        void set_all_bullet_rotation_data(const TypedArray<BulletRotationData>& new_data);

        bool get_rotate_only_textures();
        void set_rotate_only_textures(bool new_rotate_only_textures);

        bool get_is_texture_rotation_permanent();
        void set_is_texture_rotation_permanent(bool new_is_texture_rotation_permanent);

        bool get_use_block_rotation_radians();
        void set_use_block_rotation_radians(bool new_use_block_rotation_radians);

        TypedArray<BulletSpeedData> get_all_bullet_speed_data();
        void set_all_bullet_speed_data(const TypedArray<BulletSpeedData>& new_data);
    protected:
        static void _bind_methods();
};

#endif