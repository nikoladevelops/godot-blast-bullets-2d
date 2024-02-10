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

class BlockBullets2D:public MultiMeshInstance2D, public MultiMeshBullets2D{
    GDCLASS(BlockBullets2D, MultiMeshInstance2D);
    
    public:
        ~BlockBullets2D();

        // TEXTURE RELATED

        // Holds all textures
        TypedArray<Texture2D> textures;

        // Time before the texture gets changed to the next one. If 0 it means that the timer is not active
        float max_change_texture_time = 0.0f;

        // The change texture time being processed now
        float current_change_texture_time;

        // Holds the current texture index (the index inside the array textures)
        int current_texture_index;

        // The default behaviour is for the texture of each bullet to be rotated according to the rotation of the bullet's transform + texture_rotation_radians OR block_rotation_radians + texture_rotation_radians if use_block_rotation_radians is true. If for some reason you ONLY want the texture_rotation_radians to be used, then set this to true (basically all your textures will always be rotated in a specific angle, no matter in which direction they travel)
        bool is_texture_rotation_permanent=false;

        // The texure rotation in radians. Check is_texture_rotation_permanent for more info
        float texture_rotation_radians;
        
        // This is the texture size of the bullets
        Vector2 texture_size = Vector2(0,0);

        // BULLET SPEED RELATED
        std::vector<float> all_cached_speed;
        std::vector<float> all_cached_max_speed;
        std::vector<float> all_cached_acceleration;

        // BULLET MOVEMENT RELATED

        // It will be set to true automatically if only a single BulletSpeedData was provided OR if the amount of BulletSpeedData provided was not equal to transforms.size(). It basically means that all bullets will have the same speed/max_speed/acceleration, so an optimization where the multimesh itself will be moved is applied (bullets move as a block in the same direction which is determined by block_rotation_radians)
        bool use_block_rotation_radians = false;

        // The block rotation. The direction of the bullets is determined by it. Only used if use_block_rotation_radians is set to true
        float block_rotation_radians;

        // Cached multimesh instance position. Used only if use_block_rotation_radians is true
        Vector2 current_position;

        // Holds all multimesh instance transforms. I am doing this so I don't have to call multi->get_instance_transform_2d() every frame
        std::vector<Transform2D> all_cached_instance_transforms;
        
        // Holds all collision shape transforms. I am doing this so I don't have to call physics_server->area_get_shape_transform() every frame
        std::vector<Transform2D> all_cached_shape_transforms;

        // Holds all multimesh instance transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
        std::vector<Vector2> all_cached_instance_origin;
        
        // Holds all collision shape transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
        std::vector<Vector2> all_cached_shape_origin;

        // Holds all calculated velocities for the bullets. I am doing this to avoid unnecessary calculations. If I know the direction -> calculate the velocity. Update the values only when the velocity changes, otherwise it's just unnecessary to always do Vector2(cos, sin) every frame..
        std::vector<Vector2> all_cached_velocity;

        // Holds all cached directions of the bullets
        std::vector<Vector2> all_cached_direction;

        // COLLISION RELATED

        // Collision shape offset from the center (by default the collision shape is at the center of the texture)
        Vector2 collision_shape_offset = Vector2(0,0);

        // Saves whether the bullets can detect bodies or not
        bool monitorable;

        // Holds a boolean value for each bullet that indicates whether its active
        std::vector<char> bullets_enabled_status;

        // Counts all active bullets. If equal to size, every single bullet will be disabled.
        int active_bullets_counter=0;

        // ROTATION RELATED

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

        // OTHER

        // Pointer to the multimesh instead of always calling the get method
        MultiMesh* multi;

        // The factory by which the bullets were spawned.
        BulletFactory2D* factory;

        // The user can pass any custom data they desire and have access to it in the area_entered and body_entered function callbacks
        Ref<Resource> bullets_custom_data;

        // The life time of all bullets
        float max_life_time;
        // The current life time being processed
        float current_life_time;
        

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

    protected:

        // BULLET STATE METHODS
        
        // Generate methods are called only when spawning/loading data

        void generate_area();
        void set_up_area(
            RID new_world_space,
            uint32_t new_collision_layer,
            uint32_t new_collision_mask,
            bool new_monitorable
            );
        void generate_collision_shapes_for_area();
        bool set_up_bullets(
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

        // HELPER METHODS

        // Makes all bullet instances visible and also populates bullets_enabled_status with true values only
        void make_all_bullet_instances_visible();
        
        // Updates bullets_enabled_status with new values and makes certain instances visible/hidden depending on the new status values
        void make_bullet_instances_visible(const TypedArray<bool>& new_bullets_enabled_status);
        
        // Depending on bullets_enabled_status enables/disables collision for each instance. It registers area's entered signals so all bullet data must be set before calling it
        void enable_bullets_based_on_status();

        // Accelerates an individual bullet's speed
        void accelerate_bullet_speed(int speed_data_id, float delta);

        // Rotates a bullet's texture
        void rotate_bullet(int multi_instance_id, float rotated_angle);

        // Accelerates a bullet's rotation speed
        void accelerate_bullet_rotation_speed(int multi_instance_id, float delta);
        
        static void _bind_methods();
};

#endif