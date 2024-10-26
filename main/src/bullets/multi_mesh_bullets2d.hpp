#ifndef MULTI_MESH_BULLETS2D
#define MULTI_MESH_BULLETS2D

#include "godot_cpp/classes/multi_mesh_instance2d.hpp"

class BulletFactory2D;

using namespace godot;
class MultiMeshBullets2D: public MultiMeshInstance2D{
    GDCLASS(MultiMeshBullets2D, MultiMeshInstance2D)
    public:

        /// TEXTURE RELATED

        // Holds all textures
        TypedArray<Texture2D> textures;

        // Time before the texture gets changed to the next one. If 0 it means that the timer is not active
        float max_change_texture_time = 0.0f;

        // The change texture time being processed now
        float current_change_texture_time;

        // Holds the current texture index (the index inside the array textures)
        int current_texture_index;
        
        // This is the texture size of the bullets
        Vector2 texture_size = Vector2(0,0);

        ///

        /// BULLET SPEED RELATED

        PackedFloat32Array all_cached_speed;
        PackedFloat32Array all_cached_max_speed;
        PackedFloat32Array all_cached_acceleration;

        ///

        /// CACHED CALCULATIONS FOR IMPROVED PERFORMANCE
        
        // Holds all multimesh instance transforms. I am doing this so I don't have to call multi->get_instance_transform_2d() every frame
        std::vector<Transform2D> all_cached_instance_transforms;
        
        // Holds all collision shape transforms. I am doing this so I don't have to call physics_server->area_get_shape_transform() every frame
        std::vector<Transform2D> all_cached_shape_transforms;

        // Holds all multimesh instance transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
        PackedVector2Array all_cached_instance_origin;
        
        // Holds all collision shape transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
        PackedVector2Array all_cached_shape_origin;

        // Holds all calculated velocities for the bullets. I am doing this to avoid unnecessary calculations. If I know the direction -> calculate the velocity. Update the values only when the velocity changes, otherwise it's just unnecessary to always do Vector2(cos, sin) every frame..
        PackedVector2Array all_cached_velocity;

        // Holds all cached directions of the bullets
        PackedVector2Array all_cached_direction;

        ///

        
        /// COLLISION RELATED

        // The area that holds all collision shapes
        RID area;

        // Saves whether the bullets can detect bodies or not
        bool monitorable;

        // Holds a boolean value for each bullet that indicates whether its active
        std::vector<char> bullets_enabled_status;

        // Counts all active bullets. If equal to size, every single bullet will be disabled
        int active_bullets_counter=0;

        ///

        /// ROTATION RELATED

        // SOA vs AOS, I picked SOA, because it offers better cache performance
        PackedFloat32Array all_rotation_speed;
        PackedFloat32Array all_max_rotation_speed;
        PackedFloat32Array all_rotation_acceleration;
        
        // If set to false it will also rotate the collision shapes
        bool rotate_only_textures;

        // Important. Determines if there was valid rotation data passed, if its true it means the rotation logic will work
        bool is_rotation_active;

        // If true it means that only a single BulletRotationData was provided, so it will be used for each bullet. If false it means that we have BulletRotationData for each bullet. It is determined by the amount of BulletRotationData passed to spawn()
        bool use_only_first_rotation_data=false;

        ///

        /// OTHER

        // The amount of bullets the multimesh has
        int size;

        // Pointer to the multimesh instead of always calling the get method
        MultiMesh* multi;

        // The factory by which the bullets were spawned.
        BulletFactory2D* factory;

        // The user can pass any custom data they desire and have access to it in the area_entered and body_entered function callbacks
        Ref<Resource> bullets_custom_data;

        // The life time of all bullets
        float max_life_time;
        // The current life time being processed
        float current_life_time=0.0f;

        ///


    protected:
        static void _bind_methods();
};
#endif