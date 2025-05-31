#pragma once

#include "../spawn-data/multimesh_bullets_data2d.hpp"
#include "../save-data/save_data_multimesh_bullets2d.hpp"
#include "../shared/bullet_rotation_data2d.hpp"
#include "../shared/bullet_attachment2d.hpp"
#include "../shared/bullet_attachment_object_pool2d.hpp"
#include "../factory/bullet_factory2d.hpp"
#include "../debugger/idebugger_data_provider2d.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance2d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/packed_scene.hpp>

namespace BlastBullets2D {
using namespace godot;

class MultiMeshObjectPool;

class MultiMeshBullets2D : public MultiMeshInstance2D, public IDebuggerDataProvider2D {
    GDCLASS(MultiMeshBullets2D, MultiMeshInstance2D)
public:
    // Having constructors with initializer lists would be very cool, but Godot's memnew keyword sadly does not support that, so I'm left with using custom spawn() methods - Remeber when you create a new instance of this class with memnew you have to call a spawn method in order for everything to work

    virtual ~MultiMeshBullets2D();

    // Whether all the bullets should be processed/moved/rotated etc.. or just skipped (basically this value should be equal to false only when ALL bullets are completely disabled)
    bool is_active = false; 

    // Gets the total amount of bullets that the multimesh always holds
    int get_amount_bullets() const { return amount_bullets; };

    // Gets the total amount of attachments that are active
    int get_amount_active_attachments() const;

    // Used to spawn brand new bullets that are active in the scene tree
    void spawn(const MultiMeshBulletsData2D &spawn_data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container);

    // Activates the multimesh
    void activate_multimesh(const MultiMeshBulletsData2D &data);

    // Populates an empty data class instance with the current state of the bullets and returns it so it can be saved
    Ref<SaveDataMultiMeshBullets2D> save(const Ref<SaveDataMultiMeshBullets2D> &empty_data);

    // Used to load bullets from a SaveDataMultiMeshBullets2D resource
    void load(const Ref<SaveDataMultiMeshBullets2D> &data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container);

    // Spawns as a disabled invisible multimesh that is ready to be activated at any time. Method is used for object pooling, because it sets up all necessary things (like physics shapes for example) without needing additional spawn data (which would be overriden anyways by the activate function's logic)
    void spawn_as_disabled_multimesh(int amount_bullets, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container);
    
    // Unsafe delete - only call this if all types of bullet processing have been disabled and all dangling pointers cleared, otherwise you will face weird issues/crashes
    void force_delete(bool pool_attachments) { 
        // Disable the area's shapes (ALL OF THEM no matter their bullets_enabled_status)
        for (int i = 0; i < amount_bullets; i++) {
            physics_server->area_set_shape_disabled(area, i, true);
        }

        physics_server->area_set_area_monitor_callback(area, Variant());
        physics_server->area_set_monitor_callback(area, Variant());
       
        if (is_bullet_attachment_provided)
        {
            for (int i = 0; i < amount_bullets; i++)
            {
                disable_attachment(i, pool_attachments);
            }
        }

        memdelete(this); // Immediate deletion
    }


    /// METHODS RESPONSIBLE FOR VARIOUS BULLET FEATURES

    // Use this method when you want to use physics interpolation - smooth rendering of textures despite physics ticks per second
    _ALWAYS_INLINE_ void interpolate_bullet_visuals() {
        float fraction = Engine::get_singleton()->get_physics_interpolation_fraction();

        for (int i = 0; i < amount_bullets; i++) {
            if (bullets_enabled_status[i] == false) {
                continue;
            }


            // Apply interpolated transform for the bullet
            const Transform2D &interpolated_bullet_texture_transf = get_interpolated_transform(all_cached_instance_transforms[i], all_previous_instance_transf[i], fraction);
            multi->set_instance_transform_2d(i, interpolated_bullet_texture_transf);

            if (!is_bullet_attachment_provided) {
                continue;
            }

            // Apply interpolated transform for the attachment
            const Transform2D &interpolated_attachment_transf = get_interpolated_transform(attachment_transforms[i], all_previous_attachment_transf[i], fraction);
            bullet_attachments[i]->set_global_transform(interpolated_attachment_transf);
        }
    }

    _ALWAYS_INLINE_ Transform2D get_interpolated_transform(const Transform2D &curr_transf, const Transform2D &prev_transf, float fraction) {
        // Interpolate position
        Vector2 prev_pos = prev_transf.get_origin();
        Vector2 curr_pos = curr_transf.get_origin();
        Vector2 interpolated_pos = prev_pos.lerp(curr_pos, fraction);

        // Interpolate rotation
        float prev_rot = prev_transf.get_rotation();
        float curr_rot = curr_transf.get_rotation();
        float interpolated_rot = godot::Math::lerp_angle(prev_rot, curr_rot, fraction);

        // Apply interpolated transform
        return Transform2D(interpolated_rot, interpolated_pos);
    }

    // Reduces the lifetime of the multimesh so it can eventually get disabled entirely
    _ALWAYS_INLINE_ void reduce_lifetime(float delta) {
        // Life time timer logic
        current_life_time -= delta;
        if (current_life_time <= 0) {

            // If the user wants to track when the life time is over we need to collect some additional info about the multimesh
            if (is_life_time_over_signal_enabled)
            {
                // Will hold all transforms of bullets that have not yet hit anything / the ones we are forced to disable due to life time being over
                TypedArray<Transform2D> transfs;

                for (int i = 0; i < amount_bullets; i++) {

                    // If the status is active it means that the bullet hasn't hit anything yet, so we need to disable it ourselves
                    if (bullets_enabled_status[i])
                    {
                        call_deferred("disable_bullet", i); // disable it
                        transfs.push_back(all_cached_instance_transforms[i]); // store the transform of the disabled bullet
                    }
                }

                // Emit a signal and pass all the transforms of bullets that were forcefully disabled / the ones that were disabled due to life time being over (NOT because they hit a collision shape/body)
                bullet_factory->emit_signal("life_time_over", bullets_custom_data, transfs);

            }
            else {
                // If we do not wish to emit the life_time_over signal, just disable the bullet and don't worry about having to pass additional data to the user
                for (int i = 0; i < amount_bullets; i++) {
                    // There is already a bullet status check inside the function so it's fine
                    call_deferred("disable_bullet", i); 
                }

            }


            
        }
    }

    // Changes the texture periodically
    _ALWAYS_INLINE_ void change_texture_periodically(float delta) {
        int64_t textures_amount = textures.size();
        
        // No need to change textures if there is nothing to animate..
        if (textures_amount <= 1)
        {
            return;
        }

        // Keep reducing the current change texture time every frame
        current_change_texture_time -= delta;

        // When the current change texture time reaches 0, it's time to switch to the next texture
        if (current_change_texture_time <= 0.0f) {
            // Change the texture to the new one
            if (current_texture_index + 1 < textures_amount) {
                current_texture_index++;
            } else { // Loop if you reach the end so you don't access invalid indexes
                current_texture_index = 0;
            }

            set_texture(textures[current_texture_index]);

            // If the user has provided same amount of change texture times as textures, it means he wants to have different wait time for each texture
            if(change_texture_times.size() == textures_amount){
                current_change_texture_time = change_texture_times[current_texture_index]; // use the next texture's time
            }else{ // Otherwise just use the default change texture time again which is saved in index 0
                current_change_texture_time = change_texture_times[0]; // use the default time
            }
        }
        
    }
    ///

    void set_physics_interpolation_related_data();

protected:
    // Counts all active bullets
    int active_bullets_counter = 0;

    BulletFactory2D *bullet_factory = nullptr;
    MultiMeshObjectPool *bullets_pool = nullptr;
    PhysicsServer2D *physics_server = nullptr;

    std::vector<RID> physics_shapes;

    // This is used to effectively hide a single bullet instance from being rendered by the multimesh
    const Transform2D zero_transform = Transform2D().scaled(Vector2(0, 0));

    ///

    /// ROTATION RELATED

    std::vector<float> all_rotation_speed;
    std::vector<float> all_max_rotation_speed;
    std::vector<float> all_rotation_acceleration;

    // If set to false it will also rotate the collision shapes
    bool rotate_only_textures = false;

    // Important. Determines if there was valid rotation data passed, if its true it means the rotation logic will work
    bool is_rotation_active = false;

    // If true it means that only a single BulletRotationData2D was provided, so it will be used for each bullet. If false it means that we have BulletRotationData2D for each bullet. It is determined by the amount of BulletRotationData2D passed to spawn()
    bool use_only_first_rotation_data = false;

    // If set to true, it will stop the rotation when the max rotation speed is reached
    bool stop_rotation_when_max_reached = false;

    ///

    /// BULLET ATTACHMENT RELATED

    // Stores the bullet attachment scene, from which it sets up BulletAttachment2D nodes for each bullet instance
    Ref<PackedScene> bullet_attachment_scene = nullptr;
    
    // Stores pointers to all bullet attachments currently in the scene
    std::vector<BulletAttachment2D *> bullet_attachments;

    // Stores each attachment's transform data
    std::vector<Transform2D> attachment_transforms;

    // The bullet attachment's local transform
    Transform2D bullet_attachment_local_transform;
    
    // Whether the bullet attachment is being used
    bool is_bullet_attachment_provided = false;
    
    // Caches the value of stick_relative_to_bullet from the bullet attachment scene, so it's always available
    bool cache_stick_relative_to_bullet = false;

    ///

    /// OTHER

    // The amount of bullets the multimesh has
    int amount_bullets = 0;

    // Pointer to the multimesh instead of always calling the get method
    MultiMesh *multi = nullptr;

    // The user can pass any custom data they desire and have access to it in the area_entered and body_entered function callbacks
    Ref<Resource> bullets_custom_data;

    // The max life time before the multimesh gets disabled
    float max_life_time = 0.0f;

    // Whether the life_time_over signal will be emitted when the life time of the bullets is over. Tracked by BulletFactory2D
    bool is_life_time_over_signal_enabled = false;

    // The current life time being processed
    float current_life_time = 0.0f;

    // If a ShaderMaterial was provided and it has instance shader parameters, then they should get cached here
    Dictionary instance_shader_parameters;

    /// TEXTURE RELATED

    // Holds all textures
    TypedArray<Texture2D> textures;

    // Holds all change texture times (each time corresponds to each texture)
    TypedArray<float> change_texture_times;

    // The change texture time being processed now
    float current_change_texture_time;

    // Holds the current texture index (the index inside the array textures)
    int current_texture_index = 0;

    // This is the texture size of the bullets
    Vector2 texture_size = Vector2(0, 0);

    float cache_texture_rotation_radians = 0.0f;

    ///

    /// BULLET SPEED RELATED

    std::vector<float> all_cached_speed;
    std::vector<float> all_cached_max_speed;
    std::vector<float> all_cached_acceleration;

    ///

    /// CACHED CALCULATIONS FOR IMPROVED PERFORMANCE

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

    ///

    // PHYSICS INTERPOLATION RELATED
    
    // Stores previous bullets transforms for interpolation
    std::vector<Transform2D> all_previous_instance_transf;

    // Stores previous attachment transforms for interpolation
    std::vector<Transform2D> all_previous_attachment_transf;

    //

    /// COLLISION RELATED

    // The area that holds all collision shapes
    RID area;

    // Saves whether the bullets can detect bodies or not
    bool monitorable = false;

    // Holds a boolean value for each bullet that indicates whether its active
    std::vector<int8_t> bullets_enabled_status;

    //

    /// HELPER METHODS

    // Note: If you wish to debug these functions with the debugger, remove the _ALWAYS_INLINE_ temporarily

    // Accelerates an individual bullet's speed
    _ALWAYS_INLINE_ void accelerate_bullet_speed(int speed_data_index, float delta) {
        float &curr_bullet_speed = all_cached_speed[speed_data_index];
        float curr_max_bullet_speed = all_cached_max_speed[speed_data_index];

        if (curr_bullet_speed == curr_max_bullet_speed) {
            return;
        }

        float acceleration = all_cached_acceleration[speed_data_index] * delta;
        curr_bullet_speed = std::min<float>(curr_bullet_speed + acceleration, curr_max_bullet_speed);
        all_cached_velocity[speed_data_index] = all_cached_direction[speed_data_index] * curr_bullet_speed;
    }

    // Custom rotation function (I am doing this for performance reasons since Godot's rotated_local returns a brand new Transform2D, but I want to modify a reference without making copies)
    _ALWAYS_INLINE_ void rotate_transform_locally(Transform2D &transform, real_t angle) {
        // Precompute sin and cos of the angle
        const real_t sin_angle = sinf(angle);
        const real_t cos_angle = cosf(angle);

        // Extract the basis vectors by reference
        Vector2 &x_axis = transform.columns[0];
        Vector2 &y_axis = transform.columns[1];

        // Apply the rotation to the basis vectors
        x_axis = Vector2(
            x_axis.x * cos_angle - x_axis.y * sin_angle,
            x_axis.x * sin_angle + x_axis.y * cos_angle
        );
        y_axis = Vector2(
            y_axis.x * cos_angle - y_axis.y * sin_angle,
            y_axis.x * sin_angle + y_axis.y * cos_angle
        );

        // The origin (columns[2]) remains unchanged
    }

    // Accelerates a bullet's rotation speed, returns whether the max speed has been reached or not
    _ALWAYS_INLINE_ bool accelerate_bullet_rotation_speed(int bullet_index, float delta) {
        float &cache_rotation_speed = all_rotation_speed[bullet_index];
        float cache_max_rotation_speed = all_max_rotation_speed[bullet_index];

        if (cache_rotation_speed == cache_max_rotation_speed) {
            return true;
        }

        float acceleration = all_rotation_acceleration[bullet_index] * delta;
        cache_rotation_speed = std::min<float>(cache_rotation_speed + acceleration, cache_max_rotation_speed);
        return false;
    }

    // Disables a single bullet. Always call this method using call_deferred or you will face weird synch issues
    _ALWAYS_INLINE_ void disable_bullet(int bullet_index) {
        int8_t &curr_bullet_status = bullets_enabled_status[bullet_index];

        // I am doing this, because there is a chance that the bullet collides with more than 1 thing at the same exact time (if I didn't have this check then the active_bullets_counter would be set wrong)
        if (curr_bullet_status == false) {
            return;
        }

        active_bullets_counter--;

        curr_bullet_status = false;
        
        multi->set_instance_transform_2d(bullet_index, zero_transform); // Stops rendering the instance

        physics_server->area_set_shape_disabled(area, bullet_index, true);

        // If a bullet attachment is actually being used, disable it and put it in the object pool
        if (is_bullet_attachment_provided) {
            disable_attachment(bullet_index);
        }
        
        if (active_bullets_counter == 0) {
            disable_multimesh();
        }
    }

    // Disables a single bullet attachment by either putting it in an object pool or completely freeing it
    _ALWAYS_INLINE_ void disable_attachment(int bullet_index, bool should_pool_attachment = true) {
        BulletAttachment2D*& attachment_ptr = bullet_attachments[bullet_index];

        if (attachment_ptr == nullptr) {
            return;
        }

        if (should_pool_attachment)
        {
            attachment_ptr->call_on_bullet_disable(); // call GDScript custom virtual method to ensure proper disable behavior
            bullet_factory->bullet_attachments_pool.push(attachment_ptr); // the bullet attachment is ready to be re-used/activated after disabling so push it to the object pool
            attachment_ptr = nullptr; // important to set that it's not being used anymore, so it can skip it when disabling the entire multimesh (this is so that the call_on_bullet_disable function doesn't get called twice by mistake)
        }
        else { // Otherwise just delete it
            memdelete(attachment_ptr);
        }
    }

    // Moves a single bullet attachment
    _ALWAYS_INLINE_ void move_bullet_attachment(const Vector2 &translate_by, int bullet_index, float rotation_angle) {
        if (!is_bullet_attachment_provided) {
            return;
        }

        bool is_using_physics_interpolation = bullet_factory->use_physics_interpolation;

        Transform2D new_attachment_transf;
        if (cache_stick_relative_to_bullet) {
            const Transform2D &bullet_global_transf = all_cached_instance_transforms[bullet_index];
            new_attachment_transf = calculate_attachment_global_transf(bullet_global_transf);
        }
        else {
            new_attachment_transf = attachment_transforms[bullet_index];
            new_attachment_transf = new_attachment_transf.translated(translate_by);
        }

        // Store the new transform as the current one
        attachment_transforms[bullet_index] = new_attachment_transf;

        // Apply immediately only if not using interpolation
        if (!is_using_physics_interpolation) {
            bullet_attachments[bullet_index]->set_global_transform(new_attachment_transf);
        }
        
    }

    // Calculates the global transform of the bullet attachment. Note that this function relies on bullet_attachment_local_transform being set already
    _ALWAYS_INLINE_ Transform2D calculate_attachment_global_transf(const Transform2D &original_data_transf) {
        // If there was additional texture rotation applied, this should not affect the bullet attachments
        if (cache_texture_rotation_radians != 0.0f)
        {
            // So just remove that rotation and then calculate the actual global transform of the bullet attachment
            return original_data_transf.rotated_local(-cache_texture_rotation_radians) * bullet_attachment_local_transform;
        }

        return original_data_transf * bullet_attachment_local_transform;
    }

    /// METHODS THAT ARE SUPPOSED TO BE OVERRIDEN TO PROVIDE CUSTOM LOGIC

    // Exposes methods that should be available in Godot engine
    static void _bind_methods(){
        ClassDB::bind_method(D_METHOD("disable_bullet", "bullet_index"), &MultiMeshBullets2D::disable_bullet);
    };

    // Holds custom logic that runs before the spawn function finalizes. Note that the multimesh is not yet added to the scene tree here
    virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) {}

    // Holds custom logic that runs before the save function finalizes
    virtual void custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) {}

    // Holds custom logic that runs before the load function finalizes. Note that the multimesh is not yet added to the scene tree here
    virtual void custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) {}

    // Holds custom logic that runs before activating this multimesh when retrieved from the object pool
    virtual void custom_additional_activate_logic(const MultiMeshBulletsData2D &data) {}

    // Holds custom logic that runs before disabling and pushing this multimesh inside an object pool
    virtual void custom_additional_disable_logic() {}
    ///
private:
    
    // BULLET ATTACHMENTS RELATED
    
    // The current attachment's id
    int cache_attachment_id = 0;

    // Acquires data from the bullet attachment scene and gives it to the arguments passed by reference - acquires attachment_id and attachment_rotation
    int set_attachment_related_data(const Ref<PackedScene> &new_bullet_attachment_scene, const Vector2 &bullet_attachment_offset);

    // Changes the value of is_bullet_attachment_provided and bullet_attachment_scene appropriately
    void set_bullet_attachment(const Ref<PackedScene> &attachment_scene);

    //
    
    
    // Ensures the multimesh is fully disabled - no processing, no longer visible
    void disable_multimesh();

    // Reserves enough memory and populates all needed data structures keeping track of rotation data
    void set_rotation_data(const TypedArray<BulletRotationData2D> &rotation_data, bool new_rotate_only_textures);

    // Creates a brand new bullet attachment from the bullet attachment scene and finally saves it to the bullet_attachments vector
    void create_new_bullet_attachment(const Transform2D &attachment_global_transf);

    // Tries to find and reuse a bullet attachment from the object pool. If successful returns true 
    bool reuse_attachment_from_object_pool(BulletAttachmentObjectPool2D& pool, const Transform2D &attachment_global_transf, int attachment_id);

    // Generates texture transform with correct rotation and sets it to the correct bullet on the multimesh
    Transform2D generate_texture_transform(Transform2D transf, bool is_texture_rotation_permanent, float texture_rotation_radians, int bullet_index);
    
    // Generates a collision shape transform for a particular bullet and attaches it to the area
    Transform2D generate_collision_shape_transform_for_area(Transform2D transf, const RID &shape, const Vector2 &collision_shape_size, const Vector2 &collision_shape_offset, int bullet_index);

    // Sets up the area correctly with collision related data
    void set_up_area(const int collision_layer, const int collision_mask, bool new_monitorable, const RID &physics_space);

    void generate_physics_shapes_for_area(int amount);

    void set_all_physics_shapes_enabled_for_area(bool enable);

    void generate_multimesh();

    void set_up_multimesh(int new_instance_count, const Ref<Mesh> &new_mesh, Vector2 new_texture_size);

    void set_up_bullet_instances(const MultiMeshBulletsData2D &data);
    
    void load_bullet_instances(const SaveDataMultiMeshBullets2D &data);

    void set_up_life_time_timer(float new_max_life_time, float new_current_life_time);

    void set_up_change_texture_timer(int64_t new_amount_textures, float new_default_change_texture_time, const TypedArray<float> &new_change_texture_times);

    // Always called last
    void finalize_set_up(
        const Ref<Resource> &new_bullets_custom_data,
        const TypedArray<Texture2D> &new_textures,
        const Ref<Texture2D>& new_default_texture,
        int new_current_texture_index,
        const Ref<Material> &new_material,
        int new_z_index,
        int new_light_mask,
        int new_visibility_layer,
        const Dictionary &new_instance_shader_parameters
    );

    ///

    /// COLLISION DETECTION METHODS

    void area_entered_func(int status, RID entered_rid, int64_t entered_instance_id, int entered_shape_index, int bullet_shape_index);
    void body_entered_func(int status, RID entered_rid, int64_t entered_instance_id, int entered_shape_index, int bullet_shape_index);

    ///

    /// METHODS COMING FROM THE IDebuggerDataProvider2D INTERFACE

    const Vector2 get_collision_shape_size_for_debugging() const override {
        return static_cast<Vector2>(physics_server->shape_get_data(physics_shapes[0]));
    }

    const std::vector<Transform2D>& get_all_collision_shape_transforms_for_debugging() const override {
        return all_cached_shape_transforms;
    }

    bool get_skip_debugging() const override{
        return !is_active; // Skip debugging for the multimesh if it's not active (meaning bullets have stopped moving so no need for the debugger to update transforms)
    }

    ///


};
}