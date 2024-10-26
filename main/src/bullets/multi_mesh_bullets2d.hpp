#ifndef MULTI_MESH_BULLETS2D
#define MULTI_MESH_BULLETS2D

#include "godot_cpp/classes/multi_mesh_instance2d.hpp"
#include "godot_cpp/classes/multi_mesh.hpp"
#include "godot_cpp/classes/physics_server2d.hpp"
#include "../shared/bullet_rotation_data.hpp"
#include "../shared/bullet_speed_data.hpp"
#include "../spawn-data/block_bullets_data2d.hpp"
#include "../save-data/save_data_block_bullets2d.hpp"

#define physics_server PhysicsServer2D::get_singleton()

using namespace godot;

class BulletFactory2D;

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

        // Used to spawn brand new bullets.
        void spawn(const Ref<BlockBulletsData2D>& spawn_data, BulletFactory2D* new_factory);
        
        // Used to retrieve a resource representing the bullets' data, so that it can be saved to a file.
        Ref<SaveDataBlockBullets2D> save();

        // Used to load a resource. Should be used instead of spawn when trying to load data from a file.
        void load(const Ref<SaveDataBlockBullets2D>& data, BulletFactory2D* new_factory);

        // Activates the multimesh
        void activate_multimesh(const Ref<BlockBulletsData2D>& new_bullets_data);

        // Called when all bullets have been disabled
        void disable_multi_mesh();

        // Safely delete the multimesh
        void safe_delete();


    protected:
        virtual ~MultiMeshBullets2D();

        static void _bind_methods();

        /// HELPER METHODS

        // Makes all bullet instances visible and also populates bullets_enabled_status with true values only
        void make_all_bullet_instances_visible();

        // Updates bullets_enabled_status with new values and makes certain instances visible/hidden depending on the new status values
        void make_bullet_instances_visible(const TypedArray<bool>& new_bullets_enabled_status);
        
        // Depending on bullets_enabled_status enables/disables collision for each instance. It registers area's entered signals so all bullet data must be set before calling it
        void enable_bullets_based_on_status();

        // Accelerates an individual bullet's speed
        void accelerate_bullet_speed(int speed_data_id, float delta);

        // Rotates a bullet's texture (and also the physics shape if rotate_only_textures is false)
        void rotate_bullet(int multi_instance_id, float rotated_angle);

        // Accelerates a bullet's rotation speed
        void accelerate_bullet_rotation_speed(int multi_instance_id, float delta);

        // Disables a single bullet
        void disable_bullet(int bullet_index);
    ///
    private:

    /// METHODS RESPONSIBLE FOR VARIOUS BULLET FEATURES

        // The physics process loop. Holds all logic that needs to be repeated every physics frame
        void _physics_process(float delta);

        // Reduces the lifetime of the multimesh so it can eventually get disabled entirely
        void reduce_lifetime(float delta);

        // Changes the texture periodically
        void change_texture_periodically(float delta);

        // Handles the rotation of the bullets
        void handle_bullet_rotation(float delta);

        // Holds bullet movement logic
        virtual void move_bullets(float delta) = 0;
    ///

    /// METHODS RESPONSIBLE FOR SETTING THE MULTIMESH IN CORRECT STATE

        // Generate methods are called only when spawning/loading data

        void generate_area();

        void set_up_area(
            RID new_world_space,
            uint32_t new_collision_layer,
            uint32_t new_collision_mask,
            bool new_monitorable
            );

        void generate_collision_shapes_for_area();

        bool set_up_bullets_state(
            Vector2 new_collision_shape_size,
            const TypedArray<Transform2D>& new_transforms, // make sure you are giving transforms that don't have collision offset applied, otherwise it will apply it twice
            float new_texture_rotation,
            Vector2 new_collision_shape_offset,
            bool new_is_texture_rotation_permanent,
            const TypedArray<BulletSpeedData>& all_speed_data,
            bool new_use_block_rotation_radians,
            float new_block_rotation_radians
            );
        
        void generate_multimesh();
        void set_up_multimesh(int new_instance_count, const Ref<Mesh>& new_mesh, Vector2 new_texture_size);
        
        void set_up_rotation(TypedArray<BulletRotationData>& new_data, bool new_rotate_only_textures);
        void set_up_life_time_timer(float new_max_life_time, float new_current_life_time);
        void set_up_change_texture_timer(int new_amount_textures, float new_max_change_texture_time, float new_current_change_texture_time);
        
        // Always called last
        void finalize_set_up(
            const Ref<Resource>& new_bullets_custom_data,
            const TypedArray<Texture2D>& new_textures,
            int new_current_texture_index,
            const Ref<Material>& new_material
            );

        // TODO better name for this one
        void load_bullets_state(const Ref<SaveDataBlockBullets2D>& data);
    ///

    /// COLLISION DETECTION METHODS

        void area_entered_func(int status, RID entered_rid, uint64_t entered_instance_id, int entered_shape_index, int bullet_shape_index);
        void body_entered_func(int status, RID entered_rid, uint64_t entered_instance_id, int entered_shape_index, int bullet_shape_index);
    ///

};
#endif