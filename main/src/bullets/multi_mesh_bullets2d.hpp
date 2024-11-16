#ifndef MULTI_MESH_BULLETS2D_HPP
#define MULTI_MESH_BULLETS2D_HPP

#include "../spawn-data/multi_mesh_bullets_data2d.hpp"
#include "../save-data/save_data_multi_mesh_bullets2d.hpp"
#include "../shared/bullet_rotation_data2d.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance2d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace BlastBullets {

class BulletFactory2D;
class MultiMeshObjectPool;

class MultiMeshBullets2D : public godot::MultiMeshInstance2D {
    GDCLASS(MultiMeshBullets2D, godot::MultiMeshInstance2D)

    // Only reason I am leaving everything public is because GDExtension is not perfect and setting things to protected causes issues with engine methods like _physics_process
public:
    BulletFactory2D *bullet_factory;
    MultiMeshObjectPool *bullets_pool;
    godot::PhysicsServer2D *physics_server;

    /// TEXTURE RELATED

    // Holds all textures
    godot::TypedArray<godot::Texture2D> textures;

    // Time before the texture gets changed to the next one. If 0 it means that the timer is not active
    float max_change_texture_time = 0.0f;

    // The change texture time being processed now
    float current_change_texture_time;

    // Holds the current texture index (the index inside the array textures)
    int current_texture_index;

    // This is the texture size of the bullets
    godot::Vector2 texture_size = godot::Vector2(0, 0);

    ///

    /// BULLET SPEED RELATED

    godot::PackedFloat32Array all_cached_speed;
    godot::PackedFloat32Array all_cached_max_speed;
    godot::PackedFloat32Array all_cached_acceleration;

    ///

    /// CACHED CALCULATIONS FOR IMPROVED PERFORMANCE

    // Holds all multimesh instance transforms. I am doing this so I don't have to call multi->get_instance_transform_2d() every frame
    std::vector<godot::Transform2D> all_cached_instance_transforms;

    // Holds all collision shape transforms. I am doing this so I don't have to call physics_server->area_get_shape_transform() every frame
    std::vector<godot::Transform2D> all_cached_shape_transforms;

    // Holds all multimesh instance transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
    godot::PackedVector2Array all_cached_instance_origin;

    // Holds all collision shape transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
    godot::PackedVector2Array all_cached_shape_origin;

    // Holds all calculated velocities for the bullets. I am doing this to avoid unnecessary calculations. If I know the direction -> calculate the velocity. Update the values only when the velocity changes, otherwise it's just unnecessary to always do Vector2(cos, sin) every frame..
    godot::PackedVector2Array all_cached_velocity;

    // Holds all cached directions of the bullets
    godot::PackedVector2Array all_cached_direction;

    ///

    /// COLLISION RELATED

    // The area that holds all collision shapes
    godot::RID area;

    // Saves whether the bullets can detect bodies or not
    bool monitorable;

    // Holds a boolean value for each bullet that indicates whether its active
    std::vector<char> bullets_enabled_status;

    // Counts all active bullets. If equal to size, every single bullet will be disabled
    int active_bullets_counter = 0;

    ///

    /// ROTATION RELATED

    // SOA vs AOS, I picked SOA, because it offers better cache performance
    
    godot::PackedFloat32Array all_rotation_speed;
    godot::PackedFloat32Array all_max_rotation_speed;
    godot::PackedFloat32Array all_rotation_acceleration;

    // If set to false it will also rotate the collision shapes
    bool rotate_only_textures;

    // Important. Determines if there was valid rotation data passed, if its true it means the rotation logic will work
    bool is_rotation_active;

    // If true it means that only a single BulletRotationData2D was provided, so it will be used for each bullet. If false it means that we have BulletRotationData2D for each bullet. It is determined by the amount of BulletRotationData2D passed to spawn()
    bool use_only_first_rotation_data = false;

    ///

    /// OTHER

    // The amount of bullets the multimesh has
    int size;

    // Pointer to the multimesh instead of always calling the get method
    godot::MultiMesh *multi;

    // The user can pass any custom data they desire and have access to it in the area_entered and body_entered function callbacks
    godot::Ref<godot::Resource> bullets_custom_data;

    // The max life time before the multimesh gets disabled
    float max_life_time;

    // The current life time being processed
    float current_life_time = 0.0f;

    ///
    virtual ~MultiMeshBullets2D();

    // Used to spawn brand new bullets
    void spawn(const godot::Ref<MultiMeshBulletsData2D> &spawn_data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container);

    // Populates an empty data class instance with the current state of the bullets and returns it so it can be saved
    godot::Ref<SaveDataMultiMeshBullets2D> save(const godot::Ref<SaveDataMultiMeshBullets2D>& empty_data);

    // Used to load bullets from a SaveDataMultiMeshBullets2D resource
    void load(const godot::Ref<SaveDataMultiMeshBullets2D> &data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container);

    // Activates the multimesh
    void activate_multimesh(const godot::Ref<MultiMeshBulletsData2D> &data);

    // Ensures the multimesh is fully disabled - no processing, no longer visible
    void disable_multi_mesh();

    // Safely deletes itself
    void safe_delete();

    // Spawns as a disabled invisible multimesh that is ready to be activated at any time. Method is used for object pooling, because it sets up all necessary things (like physics shapes for example) without needing additional spawn data (which would be overriden anyways by the activate function's logic)
    void spawn_as_disabled_multimesh(int amount_bullets, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container);

    /// METHODS RESPONSIBLE FOR VARIOUS BULLET FEATURES

    // Reduces the lifetime of the multimesh so it can eventually get disabled entirely
    _ALWAYS_INLINE_ void reduce_lifetime(float delta) {
        // Life time timer logic
        current_life_time -= delta;
        if (current_life_time <= 0) {
            // Disable still active bullets (I'm not checking for bullet status,because disable_bullet already has that logic so I would be doing double checks for no reason)
            for (int i = 0; i < size; i++) {
                disable_bullet(i);
            }
            return;
        }
    }

    // Changes the texture periodically
    _ALWAYS_INLINE_ void change_texture_periodically(float delta) {
        // The texture change timer is active only if more than 1 texture has been provided (and if that's the case then max_change_texture_time will never be 0)
        if (max_change_texture_time != 0.0f) {
            current_change_texture_time -= delta;
            if (current_change_texture_time <= 0.0f) {
                if (current_texture_index + 1 < textures.size()) {
                    current_texture_index++;
                } else {
                    current_texture_index = 0;
                }

                set_texture(textures[current_texture_index]);          // set new texture
                current_change_texture_time = max_change_texture_time; // reset timer
            }
        }
    }

    // Handles the rotation of the bullets
    _ALWAYS_INLINE_ void handle_bullet_rotation(float delta) {
        if (is_rotation_active) {
            if (use_only_first_rotation_data) {
                accelerate_bullet_rotation_speed(0, delta); // acceleration should be applied once every frame for the SINGULAR rotation speed that all bullets share
                float cache_speed = all_rotation_speed[0] * delta;
                for (int i = 0; i < size; i++) {
                    // No point in rotating if the bullet has been disabled, performance will just be lost for deactivated bullets..
                    if (bullets_enabled_status[i] == false) {
                        continue;
                    }

                    rotate_bullet(i, cache_speed);
                }
            } else {
                for (int i = 0; i < size; i++) {
                    // No point in rotating if the bullet has been disabled, performance will just be lost for deactivated bullets..
                    if (bullets_enabled_status[i] == false) {
                        continue;
                    }

                    rotate_bullet(i, all_rotation_speed[i] * delta);
                    accelerate_bullet_rotation_speed(i, delta); // each bullet has its own BulletRotationData2D (meaning INDIVIDUAL rotation_speed that has to be accelerated every frame)
                }
            }
        }
    }
    ///

    /// HELPER METHODS

    // Accelerates an individual bullet's speed
    _ALWAYS_INLINE_ void accelerate_bullet_speed(int speed_data_id, float delta) {
        float curr_bullet_speed = all_cached_speed[speed_data_id];
        float curr_max_bullet_speed = all_cached_max_speed[speed_data_id];

        if (curr_bullet_speed == curr_max_bullet_speed) {
            return;
        }

        all_cached_speed[speed_data_id] = std::min<float>(curr_bullet_speed + all_cached_acceleration[speed_data_id] * delta, curr_max_bullet_speed);
        all_cached_velocity[speed_data_id] = all_cached_direction[speed_data_id] * all_cached_speed[speed_data_id];
    }

    // Rotates a bullet's texture (and also the physics shape if rotate_only_textures is false)
    _ALWAYS_INLINE_ void rotate_bullet(int multi_instance_id, float rotated_angle) {
        godot::Transform2D &rotated_transf = all_cached_instance_transforms[multi_instance_id];
        rotated_transf = rotated_transf.rotated_local(rotated_angle);

        multi->set_instance_transform_2d(multi_instance_id, rotated_transf);

        if (rotate_only_textures == false) {
            godot::Transform2D &rotated_shape_transf = all_cached_shape_transforms[multi_instance_id];
            rotated_shape_transf = rotated_shape_transf.rotated_local(rotated_angle);

            physics_server->area_set_shape_transform(area, multi_instance_id, rotated_shape_transf);
        }
    }

    // Accelerates a bullet's rotation speed
    _ALWAYS_INLINE_ void accelerate_bullet_rotation_speed(int multi_instance_id, float delta) {
        if (all_rotation_speed[multi_instance_id] == all_max_rotation_speed[multi_instance_id]) {
            return;
        }

        all_rotation_speed[multi_instance_id] = std::min<float>(all_rotation_speed[multi_instance_id] + (all_rotation_acceleration[multi_instance_id] * delta), all_max_rotation_speed[multi_instance_id]);
    }

    // Disables a single bullet
    _ALWAYS_INLINE_ void disable_bullet(int bullet_index) {
        // I am doing this, because there is a chance that the bullet collides with more than 1 thing at the same exact time (if I didn't have this check then the active_bullets_counter would be set wrong).
        if (bullets_enabled_status[bullet_index] == false) {
            return;
        }

        active_bullets_counter--;

        bullets_enabled_status[bullet_index] = false;

        godot::Transform2D zero_transform = godot::Transform2D().scaled(godot::Vector2(0, 0));

        multi->set_instance_transform_2d(bullet_index, zero_transform); // Stops rendering the instance

        physics_server->call_deferred("area_set_shape_disabled", area, bullet_index, true);
        if (active_bullets_counter == 0) {
            disable_multi_mesh();
        }
    }

    ///

    /// METHODS RESPONSIBLE FOR SETTING THE MULTIMESH IN CORRECT STATE

    void generate_multimesh();

    void set_up_multimesh(int new_instance_count, const godot::Ref<godot::Mesh> &new_mesh, godot::Vector2 new_texture_size);

    void spawn_bullet_instances(
        godot::Vector2 new_collision_shape_size,
        const godot::TypedArray<godot::Transform2D> &new_transforms,
        float new_texture_rotation,
        godot::Vector2 new_collision_shape_offset,
        bool new_is_texture_rotation_permanent,
        bool new_monitorable,
        godot::RID new_world_space,
        uint32_t new_collision_layer,
        uint32_t new_collision_mask
        );
    
    void activate_bullet_instances(
        godot::Vector2 new_collision_shape_size,
        const godot::TypedArray<godot::Transform2D> &new_transforms,
        float new_texture_rotation,
        godot::Vector2 new_collision_shape_offset,
        bool new_is_texture_rotation_permanent,
        bool new_monitorable,
        godot::RID new_world_space,
        uint32_t new_collision_layer,
        uint32_t new_collision_mask
        );

    void load_bullet_instances(const godot::Ref<SaveDataMultiMeshBullets2D> &data);

    void set_up_rotation(godot::TypedArray<BulletRotationData2D> &new_data, bool new_rotate_only_textures);

    void set_up_life_time_timer(float new_max_life_time, float new_current_life_time);

    void set_up_change_texture_timer(int new_amount_textures, float new_max_change_texture_time, float new_current_change_texture_time);

    // Always called last
    void finalize_set_up(
        const godot::Ref<godot::Resource> &new_bullets_custom_data,
        const godot::TypedArray<godot::Texture2D> &new_textures,
        int new_current_texture_index,
        const godot::Ref<godot::Material> &new_material);

    ///

    /// COLLISION DETECTION METHODS

    void area_entered_func(int status, godot::RID entered_rid, uint64_t entered_instance_id, int entered_shape_index, int bullet_shape_index);
    void body_entered_func(int status, godot::RID entered_rid, uint64_t entered_instance_id, int entered_shape_index, int bullet_shape_index);
    ///

    /// METHODS THAT ARE SUPPOSED TO BE OVERRIDEN TO PROVIDE CUSTOM LOGIC

    // Exposes methods that should be available in Godot engine
    static void _bind_methods(){};

    // Holds custom logic that runs before the spawn function finalizes
    virtual void custom_additional_spawn_logic(const godot::Ref<MultiMeshBulletsData2D> &data) {}

    // Holds custom logic that runs before the save function finalizes
    virtual void custom_additional_save_logic(const godot::Ref<SaveDataMultiMeshBullets2D> &data) {}

    // Holds custom logic that runs before the load function finalizes
    virtual void custom_additional_load_logic(const godot::Ref<SaveDataMultiMeshBullets2D> &data) {}

    // Holds custom logic that runs before activating this multimesh when retrieved from the object pool
    virtual void custom_additional_activate_logic(const godot::Ref<MultiMeshBulletsData2D> &data) {}

    // Holds custom logic that runs before disabling and pushing this multimesh inside an object pool
    virtual void custom_additional_disable_logic() {}
    ///
};
}
#endif