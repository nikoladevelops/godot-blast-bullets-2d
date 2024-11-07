#include "./multi_mesh_bullets2d.hpp"
#include "../factory/bullet_factory2d.hpp"
#include "../shared/multimesh_object_pool.hpp"

#include <godot_cpp/classes/physics_server2d.hpp>


using namespace godot;

namespace BlastBullets {

MultiMeshBullets2D::~MultiMeshBullets2D() {
    // For some reason the destructor runs on project start up by default, so avoid doing that
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }

    // Avoid memory leaks if you've used the PhysicsServer2D to generate area and shapes
    for (int i = 0; i < size; i++) {
        RID shape = physics_server->area_get_shape(area, 0);
        physics_server->free_rid(shape);
    }
    physics_server->free_rid(area);
}

// Used to spawn brand new bullets.
void MultiMeshBullets2D::spawn(const Ref<MultiMeshBulletsData2D> &spawn_data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container) {
    set_physics_process(false);

    bullets_pool = pool;
    bullet_factory = factory;
    physics_server = PhysicsServer2D::get_singleton();

    size = spawn_data->transforms.size(); // important, because some set_up methods use this

    set_up_rotation(spawn_data->all_bullet_rotation_data, spawn_data->rotate_only_textures);

    set_up_life_time_timer(spawn_data->max_life_time, spawn_data->max_life_time);
    set_up_change_texture_timer(spawn_data->textures.size(), spawn_data->max_change_texture_time, spawn_data->max_change_texture_time);

    generate_multimesh();
    set_up_multimesh(size, spawn_data->mesh, spawn_data->texture_size);

    spawn_bullet_instances(
        spawn_data->collision_shape_size,
        spawn_data->transforms,
        spawn_data->texture_rotation_radians,
        spawn_data->collision_shape_offset,
        spawn_data->is_texture_rotation_permanent,
        spawn_data->monitorable,
        factory->physics_space,
        spawn_data->collision_layer,
        spawn_data->collision_mask
        );

    custom_additional_spawn_logic(spawn_data);

    finalize_set_up(spawn_data->bullets_custom_data, spawn_data->textures, spawn_data->current_texture_index, spawn_data->material);

    bullets_container->add_child(this);

    set_physics_process(true);
    set_visible(true);
}

// Used to retrieve a resource representing the bullets' data, so that it can be saved to a file.
Ref<SaveDataMultiMeshBullets2D> MultiMeshBullets2D::save(const Ref<SaveDataMultiMeshBullets2D>& empty_data) {
    set_physics_process(false);

    // TEXTURE RELATED

    int amount_textures = textures.size();
    empty_data->textures.resize(amount_textures);
    for (int i = 0; i < amount_textures; i++) {
        empty_data->textures[i] = textures[i];
    }
    empty_data->texture_size = texture_size;
    empty_data->max_change_texture_time = max_change_texture_time;
    empty_data->current_change_texture_time = current_change_texture_time;
    empty_data->current_texture_index = current_texture_index;

    // BULLET MOVEMENT RELATED

    empty_data->all_cached_instance_transforms.resize(size);
    for (int i = 0; i < size; i++) {
        empty_data->all_cached_instance_transforms[i] = all_cached_instance_transforms[i];
    }

    empty_data->all_cached_shape_transforms.resize(size);
    for (int i = 0; i < size; i++) {
        empty_data->all_cached_shape_transforms[i] = all_cached_shape_transforms[i];
    }

    empty_data->all_cached_instance_origin.resize(size);
    for (int i = 0; i < size; i++) {
        empty_data->all_cached_instance_origin[i] = all_cached_instance_origin[i];
    }

    empty_data->all_cached_shape_origin.resize(size);
    for (int i = 0; i < size; i++) {
        empty_data->all_cached_shape_origin[i] = all_cached_shape_origin[i];
    }

    int speed_data_size = all_cached_velocity.size();

    empty_data->all_cached_velocity.resize(speed_data_size);
    for (int i = 0; i < speed_data_size; i++) {
        empty_data->all_cached_velocity[i] = all_cached_velocity[i];
    }

    empty_data->all_cached_direction.resize(speed_data_size);
    for (int i = 0; i < speed_data_size; i++) {
        empty_data->all_cached_direction[i] = all_cached_direction[i];
    }

    // BULLET SPEED RELATED

    empty_data->all_cached_speed.resize(speed_data_size);
    for (int i = 0; i < speed_data_size; i++) {
        empty_data->all_cached_speed[i] = all_cached_speed[i];
    }

    empty_data->all_cached_max_speed.resize(speed_data_size);
    for (int i = 0; i < speed_data_size; i++) {
        empty_data->all_cached_max_speed[i] = all_cached_max_speed[i];
    }

    empty_data->all_cached_acceleration.resize(speed_data_size);
    for (int i = 0; i < speed_data_size; i++) {
        empty_data->all_cached_acceleration[i] = all_cached_acceleration[i];
    }

    // BULLET ROTATION RELATED
    if (is_rotation_active) {
        empty_data->all_bullet_rotation_data.resize(all_rotation_speed.size());
        for (int i = 0; i < all_rotation_speed.size(); i++) {
            Ref<BulletRotationData> bullet_data = memnew(BulletRotationData);
            bullet_data->rotation_speed = all_rotation_speed[i];
            bullet_data->max_rotation_speed = all_max_rotation_speed[i];
            bullet_data->rotation_acceleration = all_rotation_acceleration[i];

            empty_data->all_bullet_rotation_data[i] = bullet_data;
        }
        empty_data->rotate_only_textures = rotate_only_textures;
    }

    // COLLISION RELATED

    empty_data->collision_layer = physics_server->area_get_collision_layer(area);
    empty_data->collision_mask = physics_server->area_get_collision_mask(area);
    empty_data->monitorable = monitorable;

    RID shape = physics_server->area_get_shape(area, 0);
    empty_data->collision_shape_size = (Vector2)(physics_server->shape_get_data(shape));

    // OTHER
    empty_data->max_life_time = max_life_time;
    empty_data->current_life_time = current_life_time;
    empty_data->size = size;

    empty_data->material = get_material();
    empty_data->mesh = multi->get_mesh();
    empty_data->bullets_custom_data = bullets_custom_data;

    // Save the enabled status so you can determine which bullets were active/disabled
    empty_data->bullets_enabled_status.resize(size);
    for (int i = 0; i < size; i++) {
        empty_data->bullets_enabled_status[i] = (bool)(bullets_enabled_status[i]);
    }

    custom_additional_save_logic(empty_data);

    set_physics_process(true); // TODO maybe do this only if the bullets were enabled previously?
    return empty_data;
}

// Used to load a resource. Should be used instead of spawn when trying to load data from a file.
void MultiMeshBullets2D::load(const Ref<SaveDataMultiMeshBullets2D> &data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container) {
    set_physics_process(false);
    
    bullets_pool = pool;
    bullet_factory = factory;

    physics_server = PhysicsServer2D::get_singleton();

    size = data->all_cached_instance_transforms.size();

    set_up_rotation(data->all_bullet_rotation_data, data->rotate_only_textures);

    set_up_life_time_timer(data->max_life_time, data->current_life_time);
    set_up_change_texture_timer(data->textures.size(), data->max_change_texture_time, data->current_change_texture_time);

    generate_multimesh();
    set_up_multimesh(size, data->mesh, data->texture_size);

    load_bullet_instances(data);
    custom_additional_load_logic(data);

    finalize_set_up(data->bullets_custom_data, data->textures, data->current_texture_index, data->material);

    bullets_container->add_child(this);
    
    set_physics_process(true);
    set_visible(true);
}

// Activates the multimesh
void MultiMeshBullets2D::activate_multimesh(const Ref<MultiMeshBulletsData2D> &data) {
    set_up_rotation(data->all_bullet_rotation_data, data->rotate_only_textures);

    set_up_life_time_timer(data->max_life_time, data->max_life_time);
    set_up_change_texture_timer(data->textures.size(), data->max_change_texture_time, data->max_change_texture_time);

    set_up_multimesh(size, data->mesh, data->texture_size);

    activate_bullet_instances(
        data->collision_shape_size,
        data->transforms,
        data->texture_rotation_radians,
        data->collision_shape_offset,
        data->is_texture_rotation_permanent,
        data->monitorable,
        bullet_factory->physics_space,
        data->collision_layer,
        data->collision_mask
        );

    custom_additional_activate_logic(data);

    move_to_front(); // Makes sure that the current old multimesh is displayed on top of the newer ones (act as if its the oldest sibling to emulate the behaviour of spawning a brand new multimesh / if I dont do this then the multimesh's instances will be displayed behind the newer ones)

    finalize_set_up(data->bullets_custom_data, data->textures, data->current_texture_index, data->material);
    
    set_physics_process(true);
    set_visible(true);
}

// Called when all bullets have been disabled
void MultiMeshBullets2D::disable_multi_mesh() {
    set_visible(false); // Hide the multimesh node itself
    set_physics_process(false);
    custom_additional_disable_logic();

    bullets_pool->push(this, size);
}

// Safely delete the multimesh
void MultiMeshBullets2D::safe_delete() {
    physics_server->area_set_area_monitor_callback(area, Variant());
    physics_server->area_set_monitor_callback(area, Variant());
    queue_free();
}

void MultiMeshBullets2D::generate_multimesh() {
    multi = memnew(MultiMesh);
    set_multimesh(multi);
}

void MultiMeshBullets2D::set_up_multimesh(int new_instance_count, const Ref<Mesh> &new_mesh, Vector2 new_texture_size) {
    if (new_mesh.is_valid()) {
        multi->set_mesh(new_mesh);
    } else {
        // TODO maybe if the mesh to be generated has the size size as the previous one, then there is no need to generate a brand new one?
        Ref<QuadMesh> mesh = memnew(QuadMesh);
        mesh->set_size(new_texture_size);
        multi->set_mesh(mesh);
        texture_size = new_texture_size;
    }

    multi->set_instance_count(new_instance_count);
}

void MultiMeshBullets2D::spawn_bullet_instances(
    Vector2 new_collision_shape_size,
    const TypedArray<Transform2D> &new_transforms,
    float new_texture_rotation,
    Vector2 new_collision_shape_offset,
    bool new_is_texture_rotation_permanent,
    bool new_monitorable,
    RID new_world_space,
    uint32_t new_collision_layer,
    uint32_t new_collision_mask
    ) {
       
    monitorable = new_monitorable;
    active_bullets_counter = size;
    bullets_enabled_status.resize(size, true);

    area = physics_server->area_create();

    physics_server->area_set_space(area, new_world_space);
    physics_server->area_set_monitorable(area, new_monitorable);
    physics_server->area_set_area_monitor_callback(area, callable_mp(this, &MultiMeshBullets2D::area_entered_func));
    physics_server->area_set_monitor_callback(area, callable_mp(this, &MultiMeshBullets2D::body_entered_func));
    physics_server->area_set_collision_layer(area, new_collision_layer);
    physics_server->area_set_collision_mask(area, new_collision_mask);

    all_cached_instance_transforms.resize(size);
    all_cached_instance_origin.resize(size);
    all_cached_shape_transforms.resize(size);
    all_cached_shape_origin.resize(size);

    for (int i = 0; i < size; i++) {
        RID shape = physics_server->rectangle_shape_create();
        
        physics_server->area_add_shape(area, shape);
            
        // Collision shape offset
        Transform2D shape_transf = new_transforms[i];

        // The rotation of each transform
        float curr_bullet_rotation = shape_transf.get_rotation();

        // Rotate collision_shape_offset based on the direction of the bullets
        Vector2 rotated_offset;
        rotated_offset.x = new_collision_shape_offset.x * Math::cos(curr_bullet_rotation) - new_collision_shape_offset.y * Math::sin(curr_bullet_rotation);
        rotated_offset.y = new_collision_shape_offset.x * Math::sin(curr_bullet_rotation) + new_collision_shape_offset.y * Math::cos(curr_bullet_rotation);

        shape_transf.set_origin(shape_transf.get_origin() + rotated_offset);

        physics_server->area_set_shape_transform(area, i, shape_transf);
        physics_server->shape_set_data(shape, new_collision_shape_size / 2); // SHAPE_RECTANGLE: a Vector2 half_extents  (I'm deviding by 2 to get the actual size the user wants for the rectangle, the function wants half_extents, if it gets the size 32 for the x, that means only the half width is 32 so the other half will also be 32 meaning 64 total width if I give the user's 32 that he said, to avoid that im deviding by 2 so the size gets set correctly to 32)

        // Texture rotation
        Transform2D texture_transf = new_transforms[i];
        // The transform is used for both the collision shape and the texture by default, in case the texture is not rotated correctly, the user can enter an additional texture rotation that will rotate the texture even more, but the collision shape's rotation won't change, so make sure to pass correct transform data
        if (new_is_texture_rotation_permanent) {
            // Same texture rotation no matter the rotation of the bullet's transform
            texture_transf.set_rotation(new_texture_rotation);
        } else {
            // The rotation of the texture will be influenced by the rotation of the bullet transform
            texture_transf.set_rotation(texture_transf.get_rotation() + new_texture_rotation);
        }

        multi->set_instance_transform_2d(i, texture_transf);

        // Cache bullet transforms and origin vectors
        all_cached_instance_transforms[i] = texture_transf;
        all_cached_instance_origin[i] = texture_transf.get_origin();

        all_cached_shape_transforms[i] = shape_transf;
        all_cached_shape_origin[i] = shape_transf.get_origin();
    }
}


void MultiMeshBullets2D::activate_bullet_instances(
    Vector2 new_collision_shape_size,
    const TypedArray<Transform2D> &new_transforms,
    float new_texture_rotation,
    Vector2 new_collision_shape_offset,
    bool new_is_texture_rotation_permanent,
    bool new_monitorable,
    RID new_world_space,
    uint32_t new_collision_layer,
    uint32_t new_collision_mask
    ) {
    monitorable = new_monitorable;
    active_bullets_counter = size;

    for (int i = 0; i < size; i++)
    {
        bullets_enabled_status[i] = true;
    }

    physics_server->area_set_space(area, new_world_space);
    physics_server->area_set_monitorable(area, new_monitorable);
    physics_server->area_set_area_monitor_callback(area, callable_mp(this, &MultiMeshBullets2D::area_entered_func));
    physics_server->area_set_monitor_callback(area, callable_mp(this, &MultiMeshBullets2D::body_entered_func));
    physics_server->area_set_collision_layer(area, new_collision_layer);
    physics_server->area_set_collision_mask(area, new_collision_mask);

    for (int i = 0; i < size; i++) {
        RID shape = physics_server->area_get_shape(area, i);
        
        // Collision shape offset
        Transform2D shape_transf = new_transforms[i];

        // The rotation of each transform
        float curr_bullet_rotation = shape_transf.get_rotation();

        // Rotate collision_shape_offset based on the direction of the bullets
        Vector2 rotated_offset;
        rotated_offset.x = new_collision_shape_offset.x * Math::cos(curr_bullet_rotation) - new_collision_shape_offset.y * Math::sin(curr_bullet_rotation);
        rotated_offset.y = new_collision_shape_offset.x * Math::sin(curr_bullet_rotation) + new_collision_shape_offset.y * Math::cos(curr_bullet_rotation);

        shape_transf.set_origin(shape_transf.get_origin() + rotated_offset);

        physics_server->area_set_shape_transform(area, i, shape_transf);
        physics_server->shape_set_data(shape, new_collision_shape_size / 2); // SHAPE_RECTANGLE: a Vector2 half_extents  (I'm deviding by 2 to get the actual size the user wants for the rectangle, the function wants half_extents, if it gets the size 32 for the x, that means only the half width is 32 so the other half will also be 32 meaning 64 total width if I give the user's 32 that he said, to avoid that im deviding by 2 so the size gets set correctly to 32)
        physics_server->area_set_shape_disabled(area, i, false); // Enable shape again

        // Texture rotation
        Transform2D texture_transf = new_transforms[i];
        // The transform is used for both the collision shape and the texture by default, in case the texture is not rotated correctly, the user can enter an additional texture rotation that will rotate the texture even more, but the collision shape's rotation won't change, so make sure to pass correct transform data
        if (new_is_texture_rotation_permanent) {
            // Same texture rotation no matter the rotation of the bullet's transform
            texture_transf.set_rotation(new_texture_rotation);
        } else {
            // The rotation of the texture will be influenced by the rotation of the bullet transform
            texture_transf.set_rotation(texture_transf.get_rotation() + new_texture_rotation);
        }

        multi->set_instance_transform_2d(i, texture_transf);

        // Cache bullet transforms and origin vectors
        all_cached_instance_transforms[i] = texture_transf;
        all_cached_instance_origin[i] = texture_transf.get_origin();

        all_cached_shape_transforms[i] = shape_transf;
        all_cached_shape_origin[i] = shape_transf.get_origin();
    }
}



void MultiMeshBullets2D::set_up_rotation(TypedArray<BulletRotationData> &new_data, bool new_rotate_only_textures) {
    int amount_of_rotation_data = new_data.size();
    // If the amount of data that was provided is not a single one and also not the same amount as the bullets amount
    // then rotation should be disabled/invalid.
    // This is because I want the user to either provide a single BulletRotationData that will be used for every single bullet
    // or BulletRotation data for each bullet that will allow each bullet to rotate differently. Only these 2 cases are valid.

    if (amount_of_rotation_data != size) {
        if (amount_of_rotation_data != 1) {
            is_rotation_active = false;
            return;
        }

        use_only_first_rotation_data = true; // Important. Means only a single data was provided, so use it for every single bullet. If I don't have this variable then in _process I will be trying to access invalid vector indexes
    }
    rotate_only_textures = new_rotate_only_textures;
    is_rotation_active = true; // Important, because it determines if we have rotation data to use
    all_rotation_speed.resize(amount_of_rotation_data);
    all_max_rotation_speed.resize(amount_of_rotation_data);
    all_rotation_acceleration.resize(amount_of_rotation_data);

    for (int i = 0; i < amount_of_rotation_data; i++) {
        BulletRotationData &curr_bullet_data = *Object::cast_to<BulletRotationData>(new_data[i]); // Found out that if you have a TypedArray<>, trying to access an element with [] will give you Variant, so in order to cast it use Object::cast_to<>(), the normal reinterpret_cast and the C way of casting didn't work hmm

        all_rotation_speed[i] = curr_bullet_data.rotation_speed;
        all_max_rotation_speed[i] = curr_bullet_data.max_rotation_speed;
        all_rotation_acceleration[i] = curr_bullet_data.rotation_acceleration;
    }
}

void MultiMeshBullets2D::set_up_life_time_timer(float new_max_life_time, float new_current_life_time) {
    max_life_time = new_max_life_time;
    current_life_time = new_current_life_time;
}

void MultiMeshBullets2D::set_up_change_texture_timer(int new_amount_textures, float new_max_change_texture_time, float new_current_change_texture_time) {
    if (new_amount_textures > 1) { // the change texture timer will be active only if more than 1 texture was provided
        max_change_texture_time = new_max_change_texture_time;
        current_change_texture_time = new_current_change_texture_time;
    }
}

// Always called last
void MultiMeshBullets2D::finalize_set_up(
    const Ref<Resource> &new_bullets_custom_data,
    const TypedArray<Texture2D> &new_textures,
    int new_current_texture_index,
    const Ref<Material> &new_material) {
    // Bullets custom data
    if (new_bullets_custom_data.is_valid()) {
        bullets_custom_data = new_bullets_custom_data; 
    }

    // Texture logic
    textures.resize(new_textures.size());
    for (int i = 0; i < new_textures.size(); i++) {
        textures[i] = new_textures[i];
    }

    // Make sure the current_texture_index is valid
    if (new_current_texture_index >= textures.size() || new_current_texture_index < 0) {
        new_current_texture_index = 0;
    }
    current_texture_index = new_current_texture_index; // remember the current index of the current texture

    if (textures.size() > 0) {                        // Also make sure that there are textures to set
        set_texture(textures[current_texture_index]); // setting the current texture
    }

    // Material
    if (new_material.is_valid()) {
        set_material(new_material);
    }
}

void MultiMeshBullets2D::load_bullet_instances(const Ref<SaveDataMultiMeshBullets2D> &data) {
    int new_speed_data_size = data->all_cached_velocity.size();

    all_cached_speed.resize(new_speed_data_size);
    all_cached_max_speed.resize(new_speed_data_size);
    all_cached_acceleration.resize(new_speed_data_size);
    all_cached_velocity.resize(new_speed_data_size);
    all_cached_direction.resize(new_speed_data_size);

    all_cached_instance_transforms.resize(size);
    all_cached_instance_origin.resize(size);
    all_cached_shape_transforms.resize(size);
    all_cached_shape_origin.resize(size);

    bullets_enabled_status.resize(size);
    active_bullets_counter = size;

    for (int i = 0; i < new_speed_data_size; i++) {
        all_cached_speed[i] = data->all_cached_speed[i];
        all_cached_max_speed[i] = data->all_cached_max_speed[i];
        all_cached_acceleration[i] = data->all_cached_acceleration[i];
    }

    for (int i = 0; i < data->all_cached_velocity.size(); i++)
    {
        all_cached_velocity[i] = data->all_cached_velocity[i];
    }

    for (int i = 0; i < all_cached_direction.size(); i++)
    {
        all_cached_direction[i] = data->all_cached_direction[i];
    }
    
    area = physics_server->area_create();
    monitorable = data->monitorable;

    physics_server->area_set_space(area, bullet_factory->physics_space);
    physics_server->area_set_monitorable(area, data->monitorable);
    physics_server->area_set_area_monitor_callback(area, callable_mp(this, &MultiMeshBullets2D::area_entered_func));
    physics_server->area_set_monitor_callback(area, callable_mp(this, &MultiMeshBullets2D::body_entered_func));
    physics_server->area_set_collision_layer(area, data->collision_layer);
    physics_server->area_set_collision_mask(area, data->collision_mask);

    for (int i = 0; i < size; i++) {
        
        RID shape = physics_server->rectangle_shape_create();
        
        physics_server->area_add_shape(area, shape);
         
        all_cached_instance_transforms[i] = data->all_cached_instance_transforms[i];
        all_cached_shape_transforms[i] = data->all_cached_shape_transforms[i];
        all_cached_instance_origin[i] = data->all_cached_instance_origin[i];
        all_cached_shape_origin[i] = data->all_cached_shape_origin[i];

        bullets_enabled_status[i] = (bool)data->bullets_enabled_status[i];
        RID shape_rid = physics_server->area_get_shape(area, i);

        if(bullets_enabled_status[i]){
            physics_server->area_set_shape_disabled(area, i, false);
            multi->set_instance_transform_2d(i, all_cached_instance_transforms[i]);
        }else{
            physics_server->area_set_shape_disabled(area, i, true);
            active_bullets_counter--;

            Transform2D zero_transform = Transform2D().scaled(Vector2(0, 0));
            multi->set_instance_transform_2d(i, zero_transform);
        }

        physics_server->area_set_shape_transform(area, i, all_cached_shape_transforms[i]);
        physics_server->shape_set_data(shape_rid, data->collision_shape_size);
    }
}

///

/// COLLISION DETECTION METHODS

void MultiMeshBullets2D::area_entered_func(int status, RID entered_rid, uint64_t entered_instance_id, int entered_shape_index, int bullet_shape_index) {
    if (status == PhysicsServer2D::AREA_BODY_ADDED) {
        Object *obj = ObjectDB::get_instance(entered_instance_id);
        disable_bullet(bullet_shape_index);
        // another option would be to emit a signal here, and then the factory to register a callback for it and then emit its own signal, but I felt that it would be slower and also very messy, I also thought of keeping pointers to outside functions, but again its messier. Best solution I feel like is just keeping a pointer to the factory itself, which also allows me to call pool methods.
        bullet_factory->emit_signal("area_entered", obj, bullets_custom_data, all_cached_instance_transforms[bullet_shape_index].get_origin());
    }
}
void MultiMeshBullets2D::body_entered_func(int status, RID entered_rid, uint64_t entered_instance_id, int entered_shape_index, int bullet_shape_index) {
    if (status == PhysicsServer2D::AREA_BODY_ADDED) {
        Object *obj = ObjectDB::get_instance(entered_instance_id);
        disable_bullet(bullet_shape_index);
        bullet_factory->emit_signal("body_entered", obj, bullets_custom_data, all_cached_instance_transforms[bullet_shape_index].get_origin());
    }
}
}