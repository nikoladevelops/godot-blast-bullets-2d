#ifndef BLOCK_BULLETS2D
#define BLOCK_BULLETS2D

#include "godot_cpp/classes/multi_mesh_instance2d.hpp"
#include "../spawn-data/block_bullets_data2d.hpp"
#include "godot_cpp/classes/physics_server2d.hpp"
#include "../save-data/save_data_block_bullets2d.hpp"
#include "./multi_mesh_bullets2d.hpp"
#include "../shared/bullet_rotation_data.hpp"

using namespace godot;

class BlockBullets2D: public MultiMeshBullets2D{
    GDCLASS(BlockBullets2D, MultiMeshBullets2D);
    
    public:
        ~BlockBullets2D();

        bool use_block_rotation_radians;
        // The block rotation. The direction of the bullets is determined by it. Only used if use_block_rotation_radians is set to true
        float block_rotation_radians;

        // Cached multimesh instance position.
        Vector2 current_position;

        void _physics_process(float delta);
        // Contains the necessary logic to move the bullets that are inside the multimesh
        void move_bullets(float delta);

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

        // Safely delete the bullet
        void safe_delete();
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

        // Called only when spawning/activating the bullets multimesh
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
            
        // Called when loading data
        void load_bullets_state(const Ref<SaveDataBlockBullets2D>& data);

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

        // Rotates a bullet's texture (and also the physics shape if rotate_only_textures is false)
        void rotate_bullet(int multi_instance_id, float rotated_angle);

        // Accelerates a bullet's rotation speed
        void accelerate_bullet_rotation_speed(int multi_instance_id, float delta);

        static void _bind_methods();
};

#endif