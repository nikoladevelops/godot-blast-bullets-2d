#ifndef BLOCK_BULLETS2D
#define BLOCK_BULLETS2D

#include "godot_cpp/classes/multi_mesh_instance2d.hpp"
#include "../spawn-data/block_bullets_data2d.hpp"
#include "godot_cpp/classes/physics_server2d.hpp"
#include "../save-data/save_data_block_bullets2d.hpp"
#include "./multi_mesh_bullets2d.hpp"
#include "../shared/bullet_rotation_data.hpp"

class BulletFactory2D;

using namespace godot;

// Bullets act as a block - same speed, same direction
class BlockBullets2D:public MultiMeshInstance2D, public MultiMeshBullets2D{
    GDCLASS(BlockBullets2D, MultiMeshInstance2D);
    
    public:
        BlockBullets2D();
        ~BlockBullets2D();

        // Cached velocity
        Vector2 velocity;
        // Cached multimesh instance position
        Vector2 current_position;

        // The life time of all bullets
        float max_life_time;
        // The current life time being processed
        float current_life_time;
        
        // Collision shape offset from the center (by default the collision shape is at the center of the texture)
        Vector2 collision_shape_offset = Vector2(0,0);

        bool monitorable; // I wish I didn't need to save this property, but there is no option to get a hold of it from PhysicsServer. I only need it when returning save data ughhh.

        // Pointer to the multimesh instead of always calling the get method
        MultiMesh* multi;
        
        // Holds a boolean value for each bullet that indicates whether its active
        std::vector<char> bullets_enabled_status;
        // Counts all active bullets. If equal to size, every single bullet will be disabled.
        int active_bullets_counter=0;

        // The user can pass any custom data they desire and have access to it in the area_entered and body_entered function callbacks
        Ref<Resource> bullets_custom_data;

        // TEXTURE RELATED

        // Holds all textures
        TypedArray<Texture2D> textures;
        // Time before the texture gets changed to the next one. If 0 it means that the timer is not active.
        float max_change_texture_time = 0.0f;
        // The change texture time being processed now
        float current_change_texture_time;
        // Holds the current texture index (the index inside the array textures)
        int current_texture_index;

        // The factory by which the bullets were spawned.
        BulletFactory2D* factory;

        // SPEED RELATED

        // The block rotation. The direction of the bullets is determined by it.
        float block_rotation_radians;
        // Max speed of all bullets
        float max_speed;
        // The current speed at which they are moving
        float speed;
        // The rate at which speed increases every max_acceleration_time seconds until reaching max_speed
        float acceleration;

        // The amount of time needed in order to apply acceleration to speed. If equal to 0 won't be active
        float max_acceleration_time = 0.0f;
        // The current acceleration time
        float current_acceleration_time;

        // The texure rotation in radians. I need to keep this, so it can be saved to file
        float texture_rotation_radians;
        // Same thing over here
        Vector2 texture_size = Vector2(0,0);

        /// ROTATION RELATED

        // SOA vs AOS, I picked SOA, because it offers better cache performance

        std::vector<char> all_is_rotation_enabled; // I am using char instead of bool, because it offers better performance when trying to access it compared to a vector<bool> which packs the boolean values and uses bitwise operations internally (could've used a uint8_t too)
        std::vector<float> all_rotation_speed;
        std::vector<float> all_max_rotation_speed;
        std::vector<float> all_rotation_acceleration;
        
        // If set to false it will also rotate the collision shapes
        bool rotate_only_textures;
        // Important. Determines if there was valid rotation data passed, if its true it means the rotation logic will work.
        bool is_rotation_active;
        // If true it means that only a single BulletRotationData was provided, so it will be used for each bullet. If false it means that we have BulletRotationData for each bullet. It is determined by the amount of BulletRotationData passed to spawn()
        bool use_only_first_rotation_data=false;

        // The default behaviour is for the texture of each bullet to be rotated according to the rotation of the bullet's transform + texture_rotation_radians. If for some reason you want only the texture_rotation_radians to be used, no matter how the transform is rotated then you need to set this to true.
        bool is_texture_rotation_permanent=false;

        std::vector<Transform2D> all_cached_instance_transforms;
        ///

        void _ready();
        void _physics_process(float delta);
        // Used to spawn brand new bullets.
        void spawn(const Ref<BlockBulletsData2D>& spawn_data, BulletFactory2D* new_factory);

        // Used to retrieve a resource representing the bullets' data, so that it can be saved to a file.
        Ref<SaveDataBlockBullets2D> save();
        // Used to load a resource. Should be used instead of spawn when trying to load data from a file.
        void load(const Ref<SaveDataBlockBullets2D>& data, BulletFactory2D* new_factory);

        // Disables a single bullet
        void disable_bullet(int bullet_index);
        // Called when all bullets have been disabled
        void disable_multi_mesh();
        // Activates the multimesh
        void activate_multimesh(const Ref<BlockBulletsData2D>& new_bullets_data);

        void area_entered_func(int status, RID entered_rid, uint64_t entered_instance_id, int entered_shape_index, int bullet_shape_index);
        void body_entered_func(int status, RID entered_rid, uint64_t entered_instance_id, int entered_shape_index, int bullet_shape_index);

    // Methods that start with "generate" will generate things, so they will 100% be used when trying to spawn/load data, but they won't be used
    // when activating the multimesh, because... those "things"(example: collision shapes, singleton pointers, area) have already been generated (they are already there, because in order for a multimesh to be disabled, it first has to be spawned by calling the spawn method that takes care of generating them), so only thing left to do is populate them with the new data

    protected:
        void generate_area();
        void set_up_area(
            RID new_world_space,
            uint32_t new_collision_layer,
            uint32_t new_collision_mask,
            bool new_monitorable
            );
        void generate_collision_shapes_for_area();
        void set_up_collision_shapes_for_area(
            Vector2 new_collision_shape_size,
            const TypedArray<Transform2D>& new_original_collision_shape_transforms, // make sure you are giving transforms that don't have collision offset applied, otherwise it will apply it twice
            float new_texture_rotation,
            Vector2 new_collision_shape_offset,
            bool new_is_texture_rotation_permanent
            );
        
        
        // Makes all bullet instances visible and also populates bullets_enabled_status with true values only
        void make_all_bullet_instances_visible();
        // Updates bullets_enabled_status with new values and makes certain instances visible/hidden depending on the new status values
        void make_bullet_instances_visible(const TypedArray<bool>& new_bullets_enabled_status);
        
        // Depending on bullets_enabled_status enables/disables collision for each instance. It registers area's entered signals so all bullet data must be set before calling it.
        void enable_bullets_based_on_status();

        void generate_multimesh();
        void set_up_multimesh(int new_instance_count, const Ref<Mesh>& new_mesh, Vector2 new_texture_size);
        void set_up_rotation(TypedArray<BulletRotationData>& new_data, bool new_rotate_only_textures);
        void set_up_life_time_timer(float new_max_life_time, float new_current_life_time);
        void set_up_change_texture_timer(int new_amount_textures, float new_max_change_texture_time, float new_current_change_texture_time);
        void set_up_acceleration_timer(float new_max_speed, float new_acceleration, float new_max_acceleration_time, float new_current_acceleration_time);
        
        void finalize_set_up(
            const Ref<Resource>& new_bullets_custom_data,
            const TypedArray<Texture2D>& new_textures,
            int new_current_texture_index,
            const Ref<Material>& new_material
            );
        void rotate_bullet(int multi_instance_id, float rotated_angle);
        void accelerate_bullet_rotation_speed(int multi_instance_id);
        static void _bind_methods();
};

#endif