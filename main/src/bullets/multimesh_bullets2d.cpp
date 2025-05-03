#include "./multimesh_bullets2d.hpp"
#include "../factory/bullet_factory2d.hpp"
#include "../shared/multimesh_object_pool2d.hpp"

#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include "multimesh_bullets2d.hpp"

using namespace godot;

namespace BlastBullets2D {

MultiMeshBullets2D::~MultiMeshBullets2D() {
    // For some reason the destructor runs on project start up by default, so avoid doing that
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }

    // Avoid memory leaks if you've used the PhysicsServer2D to generate area and shapes
    for (auto& shape : physics_shapes)
    {
        physics_server->free_rid(shape);
    }

    physics_server->free_rid(area);
}

size_t MultiMeshBullets2D::get_amount_active_attachments() const
{
    size_t amount_active_attachments = 0;

    if (is_bullet_attachment_provided)
    {
        for (size_t i = 0; i < amount_bullets; i++)
        {
            if (bullet_attachments[i] != nullptr)
            {
                ++amount_active_attachments;
            }
        }
    }

    return amount_active_attachments;
}

// Used to spawn brand new bullets.
void MultiMeshBullets2D::spawn(const MultiMeshBulletsData2D &data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container) {
    bullets_pool = pool;
    bullet_factory = factory;
    physics_server = PhysicsServer2D::get_singleton();

    amount_bullets = data.transforms.size(); // important, because some set_up methods use this

    set_up_life_time_timer(data.max_life_time, data.max_life_time);
    set_up_change_texture_timer(
        data.textures.size(),
        data.default_change_texture_time,
        data.change_texture_times
    );

    generate_multimesh();
    set_up_multimesh(amount_bullets, data.mesh, data.texture_size);

    area = physics_server->area_create();
    generate_physics_shapes_for_area(amount_bullets);

    set_up_bullet_instances(data);

    set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures);

    finalize_set_up(
        data.bullets_custom_data,
        data.textures,
        data.current_texture_index,
        data.material,
        data.z_index,
        data.light_mask,
        data.visibility_layer,
        data.instance_shader_parameters
        );

    custom_additional_spawn_logic(data);
    
    is_active=true;
    bullets_container->add_child(this);
}

// Activates the multimesh
void MultiMeshBullets2D::activate_multimesh(const MultiMeshBulletsData2D &data) {
    set_up_life_time_timer(data.max_life_time, data.max_life_time);
    set_up_change_texture_timer(
        data.textures.size(),
        data.default_change_texture_time,
        data.change_texture_times
    );

    set_up_multimesh(amount_bullets, data.mesh, data.texture_size);

    set_up_bullet_instances(data);
    set_all_physics_shapes_enabled_for_area(true);

    set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures);

    move_to_front(); // Makes sure that the current old multimesh is displayed on top of the newer ones (act as if its the oldest sibling to emulate the behaviour of spawning a brand new multimesh / if I dont do this then the multimesh's instances will be displayed behind the newer ones)

    finalize_set_up(
        data.bullets_custom_data,
        data.textures,
        data.current_texture_index,
        data.material,
        data.z_index,
        data.light_mask,
        data.visibility_layer,
        data.instance_shader_parameters
    );
    
    custom_additional_activate_logic(data);

    set_visible(true);
    is_active=true;
}

void MultiMeshBullets2D::set_up_bullet_instances(const MultiMeshBulletsData2D &data) {
    active_bullets_counter = amount_bullets;

    bullets_enabled_status.assign(amount_bullets, true);
    set_up_area(data.collision_layer, data.collision_mask, data.monitorable, bullet_factory->physics_space);
     
    if(all_cached_instance_transforms.size() != 0){
        // If there was old data then we are currently trying to activate a bullets multimesh, so clear everything that is old
        // Note: We never really resize any of these vectors, so capacity always stays the same and the object pooling logic also ensures of this, so no need to reserve different amount of space since it's always going to be the original capacity value/ no memory reallocations
        // Note: This logic relies on the fact that we always activate multimesh bullets based on their original amount_bullets - that's how the object pooling logic works in order to re-use everything
        all_cached_instance_transforms.clear();
        all_cached_instance_origin.clear();
        all_cached_shape_transforms.clear();
        all_cached_shape_origin.clear();
    }else{ 
        // If there wasn't any old data, that means we are spawning a bullets multimesh, so we need to ensure that all data structures reserve needed memory at once
        all_cached_instance_transforms.reserve(amount_bullets);
        all_cached_instance_origin.reserve(amount_bullets);
        all_cached_shape_transforms.reserve(amount_bullets);
        all_cached_shape_origin.reserve(amount_bullets);
    }

    cache_texture_rotation_radians = data.texture_rotation_radians;
    
    // An attachment_id will always be set but that does not mean it's valid. Always rely on is_bullet_attachment_provided
    cache_attachment_id = set_attachment_related_data(data.bullet_attachment_scene, data.bullet_attachment_offset);
    
    BulletAttachmentObjectPool2D& attachment_pool = bullet_factory->bullet_attachments_pool;

    for (size_t i = 0; i < amount_bullets; i++) {
        RID shape = physics_shapes[i];

        const Transform2D &curr_data_transf = data.transforms[i];
        
        // Generates a collision shape transform for a particular bullet and attaches it to the area
        Transform2D shape_transf = generate_collision_shape_transform_for_area(curr_data_transf, shape, data.collision_shape_size, data.collision_shape_offset, i);
        
        // Generates texture transform with correct rotation and sets it to the correct bullet on the multimesh
        const Transform2D &texture_transf = generate_texture_transform(curr_data_transf, data.is_texture_rotation_permanent, cache_texture_rotation_radians, i);
        
        // Cache bullet transforms and origin vectors
        all_cached_instance_transforms.emplace_back(texture_transf);
        all_cached_instance_origin.emplace_back(texture_transf.get_origin());

        all_cached_shape_transforms.emplace_back(shape_transf);
        all_cached_shape_origin.emplace_back(shape_transf.get_origin());

        if (is_bullet_attachment_provided) {
            const Transform2D& attachment_transf = calculate_attachment_global_transf(texture_transf);

            bool is_successful = reuse_attachment_from_object_pool(attachment_pool, attachment_transf, cache_attachment_id);

            if (!is_successful)
            {
                create_new_bullet_attachment(attachment_transf);
            }
        }
    }

}

// Used to retrieve a resource representing the bullets' data, so that it can be saved to a file.
Ref<SaveDataMultiMeshBullets2D> MultiMeshBullets2D::save(const Ref<SaveDataMultiMeshBullets2D>& empty_data) {
    SaveDataMultiMeshBullets2D &data_to_populate = *empty_data.ptr();

    // TEXTURE RELATED

    int64_t amount_textures = textures.size();
    for (int64_t i = 0; i < amount_textures; i++) {
        data_to_populate.textures.push_back(textures[i]);
    }
    data_to_populate.texture_size = texture_size;
    data_to_populate.current_change_texture_time = current_change_texture_time;
    data_to_populate.current_texture_index = current_texture_index;
    data_to_populate.cache_texture_rotation_radians = cache_texture_rotation_radians;
    data_to_populate.change_texture_times = change_texture_times;

    data_to_populate.z_index = get_z_index();
    data_to_populate.light_mask = get_light_mask();
    data_to_populate.visibility_layer = get_visibility_layer();

    // BULLET MOVEMENT RELATED

    for (size_t i = 0; i < amount_bullets; i++) {
        data_to_populate.all_cached_instance_transforms.push_back(all_cached_instance_transforms[i]);
    }

    for (size_t i = 0; i < amount_bullets; i++) {
        data_to_populate.all_cached_shape_transforms.push_back(all_cached_shape_transforms[i]);
    }

    for (size_t i = 0; i < amount_bullets; i++) {
        data_to_populate.all_cached_instance_origin.push_back(all_cached_instance_origin[i]);
    }

    for (size_t i = 0; i < amount_bullets; i++) {
        data_to_populate.all_cached_shape_origin.push_back(all_cached_shape_origin[i]);
    }

    size_t speed_data_size = all_cached_velocity.size();
    for (size_t i = 0; i < speed_data_size; i++) {
        data_to_populate.all_cached_velocity.push_back(all_cached_velocity[i]);
    }

    for (size_t i = 0; i < speed_data_size; i++) {
        data_to_populate.all_cached_direction.push_back(all_cached_direction[i]);
    }

    // BULLET SPEED RELATED

    for (size_t i = 0; i < speed_data_size; i++) {
        data_to_populate.all_cached_speed.push_back(all_cached_speed[i]);
    }

    for (size_t i = 0; i < speed_data_size; i++) {
        data_to_populate.all_cached_max_speed.push_back(all_cached_max_speed[i]);
    }

    for (size_t i = 0; i < speed_data_size; i++) {
        data_to_populate.all_cached_acceleration.push_back(all_cached_acceleration[i]);
    }

    // BULLET ROTATION RELATED
    if (is_rotation_active) {
        for (size_t i = 0; i < all_rotation_speed.size(); i++) {
            Ref<BulletRotationData2D> bullet_data = memnew(BulletRotationData2D);
            bullet_data->rotation_speed = all_rotation_speed[i];
            bullet_data->max_rotation_speed = all_max_rotation_speed[i];
            bullet_data->rotation_acceleration = all_rotation_acceleration[i];

            data_to_populate.all_bullet_rotation_data.push_back(bullet_data);
        }
        data_to_populate.rotate_only_textures = rotate_only_textures;
    }

    // COLLISION RELATED

    data_to_populate.collision_layer = physics_server->area_get_collision_layer(area);
    data_to_populate.collision_mask = physics_server->area_get_collision_mask(area);
    data_to_populate.monitorable = monitorable;

    RID shape = physics_shapes[0];
    data_to_populate.collision_shape_size = (Vector2)(physics_server->shape_get_data(shape));

    // BULLET ATTACHMENTS RELATED
    data_to_populate.attachment_id = cache_attachment_id;
    data_to_populate.bullet_attachment_scene = bullet_attachment_scene;
    data_to_populate.is_bullet_attachment_provided = is_bullet_attachment_provided;

    
    if(is_bullet_attachment_provided){
        data_to_populate.cache_stick_relative_to_bullet = cache_stick_relative_to_bullet;
        data_to_populate.bullet_attachment_local_transform = bullet_attachment_local_transform;
        
        TypedArray<Resource> &custom_data_to_save = data_to_populate.bullet_attachments_custom_data;
        TypedArray<Transform2D> &transf_data_to_save = data_to_populate.attachment_transforms;

        for (size_t i = 0; i < amount_bullets; i++) {
            BulletAttachment2D* attachment = bullet_attachments[i];

            // Note that only data for active bullet attachments is being saved, so you should account for that in the load method. Basically there is no useless data being saved, but this requires you to pay attention to valid indexes
            if(attachment != nullptr){
                Ref<Resource> attachment_data = attachment->call_on_bullet_save();
                const Transform2D &transf = attachment_transforms[i];

                custom_data_to_save.push_back(attachment_data);
                transf_data_to_save.push_back(transf);
            }
                
        }
    }
        
    // OTHER
    data_to_populate.max_life_time = max_life_time;
    data_to_populate.current_life_time = current_life_time;
    data_to_populate.amount_bullets = amount_bullets;

    data_to_populate.material = get_material();

    const Array &keys = instance_shader_parameters.keys();
    for (int i = 0; i < keys.size(); i++) {
        const String& key = keys[i];
        const Variant& value = get_instance_shader_parameter(key);

        // Save the current updated value of the instance shader parameter (the shader might've edited the value depending on the effect)
        data_to_populate.instance_shader_parameters[key] = value;
    }

    data_to_populate.mesh = multi->get_mesh();
    data_to_populate.bullets_custom_data = bullets_custom_data;

    // Save the enabled status so you can determine which bullets were active/disabled
    for (size_t i = 0; i < amount_bullets; i++) {
        data_to_populate.bullets_enabled_status.push_back(static_cast<bool>(bullets_enabled_status[i]));
    }


    custom_additional_save_logic(data_to_populate);

    return empty_data;
}

// Used to load a resource. Should be used instead of spawn when trying to load data from a file.
void MultiMeshBullets2D::load(const Ref<SaveDataMultiMeshBullets2D> &data_to_load, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container) {
    const SaveDataMultiMeshBullets2D &data = *data_to_load.ptr();

    bullets_pool = pool;
    bullet_factory = factory;

    physics_server = PhysicsServer2D::get_singleton();

    amount_bullets = data.all_cached_instance_transforms.size();

    set_up_life_time_timer(data.max_life_time, data.current_life_time);
    
    change_texture_times = data.change_texture_times.duplicate();
    current_change_texture_time = data.current_change_texture_time;
    
    generate_multimesh();
    set_up_multimesh(amount_bullets, data.mesh, data.texture_size);

    load_bullet_instances(data);

    finalize_set_up(
        data.bullets_custom_data,
        data.textures,
        data.current_texture_index,
        data.material,
        data.z_index,
        data.light_mask,
        data.visibility_layer,
        data.instance_shader_parameters
    );

    custom_additional_load_logic(data);
    
    bullets_container->add_child(this);

    if(active_bullets_counter > 0){
        is_active = true;
    }
}


void MultiMeshBullets2D::load_bullet_instances(const SaveDataMultiMeshBullets2D &data) {
    size_t new_speed_data_size = data.all_cached_velocity.size();

    active_bullets_counter = amount_bullets;
    cache_texture_rotation_radians = data.cache_texture_rotation_radians;
    
    all_cached_speed.reserve(new_speed_data_size);
    all_cached_max_speed.reserve(new_speed_data_size);
    all_cached_acceleration.reserve(new_speed_data_size);
    all_cached_velocity.reserve(new_speed_data_size);
    all_cached_direction.reserve(new_speed_data_size);

    all_cached_instance_transforms.reserve(amount_bullets);
    all_cached_instance_origin.reserve(amount_bullets);
    all_cached_shape_transforms.reserve(amount_bullets);
    all_cached_shape_origin.reserve(amount_bullets);

    bullets_enabled_status.reserve(amount_bullets);

    for (size_t i = 0; i < new_speed_data_size; i++) {
        all_cached_speed.push_back(data.all_cached_speed[i]);
        all_cached_max_speed.push_back(data.all_cached_max_speed[i]);
        all_cached_acceleration.push_back(data.all_cached_acceleration[i]);
    }

    for (size_t i = 0; i < new_speed_data_size; i++)
    {
        all_cached_velocity.push_back(data.all_cached_velocity[i]);
    }

    for (size_t i = 0; i < new_speed_data_size; i++)
    {
        all_cached_direction.push_back(data.all_cached_direction[i]);
    }
    
    area = physics_server->area_create();
    
    set_up_area(data.collision_layer, data.collision_mask, data.monitorable, bullet_factory->physics_space);
    
    generate_physics_shapes_for_area(amount_bullets);

    set_bullet_attachment(data.bullet_attachment_scene);
    
    if (is_bullet_attachment_provided)
    {
        cache_attachment_id = data.attachment_id;
        cache_stick_relative_to_bullet = data.cache_stick_relative_to_bullet;
        bullet_attachment_local_transform = data.bullet_attachment_local_transform;
        bullet_attachments.reserve(amount_bullets);
        attachment_transforms.reserve(amount_bullets);
    }

    size_t count_attachments = 0;
    for (size_t i = 0; i < amount_bullets; i++) {
        all_cached_instance_transforms.push_back(data.all_cached_instance_transforms[i]);
        all_cached_instance_origin.push_back(data.all_cached_instance_origin[i]);
        all_cached_shape_transforms.push_back(data.all_cached_shape_transforms[i]);
        all_cached_shape_origin.push_back(data.all_cached_shape_origin[i]);

        bool is_bullet_enabled = static_cast<uint8_t>(data.bullets_enabled_status[i]);
        bullets_enabled_status.push_back(is_bullet_enabled);

        RID shape_rid = physics_shapes[i];

        if(is_bullet_enabled){
            physics_server->area_set_shape_disabled(area, i, false);
            multi->set_instance_transform_2d(i, all_cached_instance_transforms[i]);
        }else{
            physics_server->area_set_shape_disabled(area, i, true);
            active_bullets_counter--;

            multi->set_instance_transform_2d(i, zero_transform);
        }

        physics_server->area_set_shape_transform(area, i, all_cached_shape_transforms[i]);
        physics_server->shape_set_data(shape_rid, data.collision_shape_size);

        // LOAD BULLET ATTACHMENT RELATED DATA
        if (is_bullet_attachment_provided)
        {
            // If the bullet is disabled then that means it didnt have an attachment before being saved, so skip it and mark it as nullptr
            if (!is_bullet_enabled)
            {
                // Doing these for 2 reasons - 1. attachments that don't exist should always be nullptr to ensure correct state  2. I should always push transforms even if they are default constructed because I want the indexes to match (I am using the .reserve method before this after all)
                bullet_attachments.emplace_back(nullptr);
                attachment_transforms.emplace_back(Transform2D());
                continue;
            }


            BulletAttachment2D* attachment = static_cast<BulletAttachment2D*>(bullet_attachment_scene->instantiate());
            attachment->attachment_id = cache_attachment_id;
            attachment->stick_relative_to_bullet = cache_stick_relative_to_bullet;

            const Transform2D& attachment_transf = data.attachment_transforms[count_attachments];

            attachment->set_global_transform(attachment_transf);
            attachment_transforms.emplace_back(attachment_transf);

            const Ref<Resource> attachment_data = data.bullet_attachments_custom_data[count_attachments];

            if (attachment_data.is_valid()) // If it's not nullptr, load it
            {
                attachment->call_on_bullet_load(attachment_data);
            }
            

            bullet_factory->bullet_attachments_container->add_child(attachment);
            bullet_attachments.emplace_back(attachment);

            count_attachments++;
        }
    }


    // LOAD ROTATION DATA
    set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures);
}


void MultiMeshBullets2D::spawn_as_disabled_multimesh(size_t new_amount_bullets, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container){
    set_visible(false);

    bullets_pool = pool;
    bullet_factory = factory;
    physics_server = PhysicsServer2D::get_singleton();

    amount_bullets = new_amount_bullets;

    generate_multimesh();

    multi->set_instance_count(amount_bullets);

    // Create the area that will hold collision shapes
    area = physics_server->area_create();
    generate_physics_shapes_for_area(amount_bullets);

    // Only reason this here exists is so that the debugger can continue to work properly without throwing a bunch of errors in the console if the shape has not been configured/ doesn't have a data
    RID shape = physics_shapes[0];
    physics_server->shape_set_data(shape, Vector2(5,5));
    //

    // Just add it to the bullets container
    bullets_container->add_child(this);
}


void MultiMeshBullets2D::set_bullet_attachment(const Ref<PackedScene>& attachment_scene){
    // If a bullet attachment scene was provided and that scene is valid then set is_bullet_attachment_provided to true
    if (attachment_scene.is_valid())
    {
        is_bullet_attachment_provided = true;
        bullet_attachment_scene = attachment_scene;
    }
    else {
        is_bullet_attachment_provided = false;
        bullet_attachment_scene = Ref<PackedScene>(); // wont let me do "= nullptr", so doing this instead
    }
}


// Called when all bullets have been disabled
void MultiMeshBullets2D::disable_multimesh() {
    is_active=false;
    set_visible(false); // Hide the multimesh node itself

    is_bullet_attachment_provided = false; // Doing this for performance reasons, so the force_delete skips logic bullet_attachments, since if the multimesh is disabled, then all attachments are already pooled and there is no need to go trough each element of the vector checking for nullptr
    custom_additional_disable_logic();

    bullets_pool->push(this, amount_bullets);
}

void MultiMeshBullets2D::generate_multimesh() {
    multi = memnew(MultiMesh);
    set_multimesh(multi);
}

void MultiMeshBullets2D::set_up_multimesh(size_t new_instance_count, const Ref<Mesh> &new_mesh, Vector2 new_texture_size) {
    if (new_mesh.is_valid()) {
        multi->set_mesh(new_mesh);
    } else {
        Ref<QuadMesh> mesh = memnew(QuadMesh);
        mesh->set_size(new_texture_size);
        multi->set_mesh(mesh);
        texture_size = new_texture_size;
    }

    multi->set_instance_count(new_instance_count);
}

void MultiMeshBullets2D::set_up_life_time_timer(float new_max_life_time, float new_current_life_time) {
    max_life_time = new_max_life_time;
    current_life_time = new_current_life_time;
}

void MultiMeshBullets2D::set_up_change_texture_timer(int64_t new_amount_textures, float new_default_change_texture_time, const TypedArray<float> &new_change_texture_times) {
    if (new_amount_textures > 1) { // the change texture timer will be active only if more than 1 texture was provided
        change_texture_times.clear();
        
        int64_t amount_change_texture_times = new_change_texture_times.size();

        // The change texture times will only be used if their amount is the same as the amount of textures currently present. Each time value corresponds to a texture
        if(amount_change_texture_times > 0 && amount_change_texture_times == new_amount_textures){
            change_texture_times = new_change_texture_times.duplicate();

            current_change_texture_time = change_texture_times[0];
        }else{
            change_texture_times.append(new_default_change_texture_time);

            current_change_texture_time = new_default_change_texture_time;
        }
    }

}

// Always called last
void MultiMeshBullets2D::finalize_set_up(
    const Ref<Resource> &new_bullets_custom_data,
    const TypedArray<Texture2D> &new_textures,
    size_t new_current_texture_index,
    const Ref<Material> &new_material,
    int new_z_index, 
    uint32_t new_light_mask,
    uint32_t new_visibility_layer,
    const Dictionary& new_instance_shader_parameters
) {
    // Bullets custom data
    if (new_bullets_custom_data.is_valid()) {
        bullets_custom_data = new_bullets_custom_data; 
    }

    // Texture logic
    textures = new_textures.duplicate();

    // Make sure the current_texture_index is valid
    if (new_current_texture_index >= textures.size() || new_current_texture_index < 0) {
        new_current_texture_index = 0;
    }
    current_texture_index = new_current_texture_index; // remember the current index of the current texture

    if (textures.size() > 0) {                        // Also make sure that there are textures to set
        set_texture(textures[current_texture_index]); // setting the current texture
    }

    if (new_material.is_valid()) {
        godot::Ref<ShaderMaterial> shader_material = new_material;
        // If a shader material was passed and the user has provided instance shader parameters
        if (shader_material.is_valid() && new_instance_shader_parameters.is_empty() == false) {
            
            instance_shader_parameters = new_instance_shader_parameters;

            const Array &keys = new_instance_shader_parameters.keys();
            for (int i = 0; i < keys.size(); i++) {
                const String &key = keys[i];
                const Variant &value = new_instance_shader_parameters[key];

                set_instance_shader_parameter(key, value);
            }
        }

        set_material(new_material);
    }
    else {
        set_material(nullptr);
    }

    // Z Index
    set_z_index(new_z_index);

    // Light mask
    set_light_mask(new_light_mask);

    // Visibility layer
    set_visibility_layer(new_visibility_layer);
}


// OTHER 


void MultiMeshBullets2D::set_rotation_data(const TypedArray<BulletRotationData2D> &rotation_data, bool new_rotate_only_textures){
    size_t amount_rotation_data = rotation_data.size();
    
    // If the amount of rotation data is:
    // 0 -> rotation is disabled
    // Same as the amount of bullets -> rotation is enabled and each provided rotation data will be used for the corresponding bullet
    // Otherwise -> rotation is enabled, but only the first data is used
    if(amount_rotation_data == 0){
        is_rotation_active = false;
        return;
    }
    
    is_rotation_active = true;

    if(amount_rotation_data == amount_bullets){
        use_only_first_rotation_data=false;
    }else{
        use_only_first_rotation_data=true;
    }
    
    rotate_only_textures = new_rotate_only_textures;
    
    // Clear existing data (avoids freeing the actual memory, instead only the .amount_bullets is changed which allows me to push brand new elements as if the vector is empty/ overwrite existing but not accessible ones)
    all_rotation_speed.clear();
    all_max_rotation_speed.clear();
    all_rotation_acceleration.clear();

    // Reserve more space if needed
    if(amount_rotation_data > all_rotation_speed.capacity()) {
      all_rotation_speed.reserve(amount_rotation_data);
      all_max_rotation_speed.reserve(amount_rotation_data);
      all_rotation_acceleration.reserve(amount_rotation_data);
    }

    // Add newest data
    for (size_t i = 0; i < amount_rotation_data; i++) {
        BulletRotationData2D &curr_bullet_data = *Object::cast_to<BulletRotationData2D>(rotation_data[i]);

        all_rotation_speed.emplace_back(curr_bullet_data.rotation_speed);
        all_max_rotation_speed.emplace_back(curr_bullet_data.max_rotation_speed);
        all_rotation_acceleration.emplace_back(curr_bullet_data.rotation_acceleration);
    }
}

size_t MultiMeshBullets2D::set_attachment_related_data(const Ref<PackedScene> &new_bullet_attachment_scene, const Vector2 &bullet_attachment_offset){
    set_bullet_attachment(new_bullet_attachment_scene);

    // If an attachment was not provided, then there is no point to continue
    if (!is_bullet_attachment_provided)
    {
        return -1;
    }

    // Clear old data if any
    bullet_attachments.clear();
    attachment_transforms.clear();

    // Reserve space for new data
    bullet_attachments.reserve(amount_bullets);
    attachment_transforms.reserve(amount_bullets);

    // Get data from the bullet attachment scene
    BulletAttachment2D *first_ever_attachment = static_cast<BulletAttachment2D *>(bullet_attachment_scene->instantiate());
    first_ever_attachment->call_on_bullet_spawn(); // This is mandatory since this is the function that sets up custom values to the properties inside the scene (example attachment_id)

    cache_stick_relative_to_bullet = first_ever_attachment->stick_relative_to_bullet;
    
    bullet_attachment_local_transform = Transform2D();
    bullet_attachment_local_transform.set_origin(bullet_attachment_offset);
    bullet_attachment_local_transform.set_rotation(first_ever_attachment->get_rotation());
    

    size_t attachment_id = first_ever_attachment->attachment_id;
    memdelete(first_ever_attachment); // I don't really need it anymore, so delete it

    return attachment_id;
}

void MultiMeshBullets2D::create_new_bullet_attachment(const Transform2D &attachment_global_transf){
    BulletAttachment2D* attachment = static_cast<BulletAttachment2D*>(bullet_attachment_scene->instantiate());
    attachment->set_global_transform(attachment_global_transf);
    attachment->call_on_bullet_spawn(); // call GDScript custom virtual method to ensure the proper state before adding to the scene tree
    
    bullet_factory->bullet_attachments_container->add_child(attachment); // add it to the scene tree
    
    attachment_transforms.emplace_back(attachment_global_transf);
    bullet_attachments.emplace_back(attachment);
}

bool MultiMeshBullets2D::reuse_attachment_from_object_pool(BulletAttachmentObjectPool2D& pool, const Transform2D& attachment_global_transf, size_t attachment_id){
    // First check the bullet attachment object pool, if you can re-activate an already existing bullet attachment
    BulletAttachment2D* attachment = pool.pop(attachment_id);

    if (attachment != nullptr) {
        attachment->set_global_transform(attachment_global_transf);

        attachment_transforms.emplace_back(attachment_global_transf);
        bullet_attachments.emplace_back(attachment);

        attachment->call_on_bullet_activate(); // call GDScript custom virtual method to ensure proper activation/state reset of the bullet attachment you are about to re-use
        return true;
    }
    return false;

}

Transform2D MultiMeshBullets2D::generate_texture_transform(Transform2D transf, bool is_texture_rotation_permanent, float texture_rotation_radians, size_t bullet_index){
    if (is_texture_rotation_permanent) {
        // Same texture rotation no matter the rotation of the bullet's transform
        transf.set_rotation(texture_rotation_radians);
    } else {
        // The rotation of the texture will be influenced by the rotation of the bullet transform
        transf.set_rotation(transf.get_rotation() + texture_rotation_radians);
    }
    
    multi->set_instance_transform_2d(bullet_index, transf);

    return transf;
}


void MultiMeshBullets2D::set_up_area(const uint32_t collision_layer, const uint32_t collision_mask, bool new_monitorable, const RID &physics_space){
    monitorable = new_monitorable;
    physics_server->area_set_space(area, bullet_factory->physics_space);
    physics_server->area_set_monitorable(area, monitorable);
    physics_server->area_set_area_monitor_callback(area, callable_mp(this, &MultiMeshBullets2D::area_entered_func));
    physics_server->area_set_monitor_callback(area, callable_mp(this, &MultiMeshBullets2D::body_entered_func));
    physics_server->area_set_collision_layer(area, collision_layer);
    physics_server->area_set_collision_mask(area, collision_mask);

}

Transform2D MultiMeshBullets2D::generate_collision_shape_transform_for_area(Transform2D transf, const RID &shape, const Vector2 &collision_shape_size, const Vector2 &collision_shape_offset, size_t bullet_index){
    // The rotation of each transform
    float curr_bullet_rotation = transf.get_rotation();

    // Rotate collision_shape_offset based on the direction of the bullets
    Vector2 rotated_offset(
        collision_shape_offset.x * Math::cos(curr_bullet_rotation) - collision_shape_offset.y * Math::sin(curr_bullet_rotation),
        collision_shape_offset.x * Math::sin(curr_bullet_rotation) + collision_shape_offset.y * Math::cos(curr_bullet_rotation)
    );

    transf.set_origin(transf.get_origin() + rotated_offset);

    physics_server->area_set_shape_transform(area, bullet_index, transf);
    physics_server->shape_set_data(shape, collision_shape_size / 2); // SHAPE_RECTANGLE: a Vector2 half_extents  (I'm dividing by 2 to get the actual size the user wants for the rectangle, the function wants half_extents, if it gets the size 32 for the x, that means only the half width is 32 so the other half will also be 32 meaning 64 total width if I give the user's 32 that he said, to avoid that im deviding by 2 so the size gets set correctly to 32)
    
    return transf;
}

void MultiMeshBullets2D::generate_physics_shapes_for_area(size_t amount){
    physics_shapes.reserve(amount);
    for (size_t i = 0; i < amount; i++)
    {
        RID shape = physics_server->rectangle_shape_create();
        physics_server->area_add_shape(area, shape);
        physics_shapes.emplace_back(shape);
    }
}

void MultiMeshBullets2D::set_all_physics_shapes_enabled_for_area(bool enable){
    for (size_t i = 0; i < amount_bullets; i++)
    {
        physics_server->area_set_shape_disabled(area, i, !enable);
    }
}

/// COLLISION DETECTION METHODS

void MultiMeshBullets2D::area_entered_func(int status, RID entered_rid, uint64_t entered_instance_id, size_t entered_shape_index, size_t bullet_shape_index) {
    if (status == PhysicsServer2D::AREA_BODY_ADDED) {
        Object *obj = ObjectDB::get_instance(entered_instance_id);
        call_deferred("disable_bullet", bullet_shape_index);
        // another option would be to emit a signal here, and then the factory to register a callback for it and then emit its own signal, but I felt that it would be slower and also very messy, I also thought of keeping pointers to outside functions, but again its messier. Best solution I feel like is just keeping a pointer to the factory itself, which also allows me to call pool methods.
        bullet_factory->emit_signal("area_entered", obj, bullets_custom_data, all_cached_instance_transforms[bullet_shape_index].get_origin());
    }
}
void MultiMeshBullets2D::body_entered_func(int status, RID entered_rid, uint64_t entered_instance_id, size_t entered_shape_index, size_t bullet_shape_index) {
    if (status == PhysicsServer2D::AREA_BODY_ADDED) {
        Object *obj = ObjectDB::get_instance(entered_instance_id);
        call_deferred("disable_bullet", bullet_shape_index);
        bullet_factory->emit_signal("body_entered", obj, bullets_custom_data, all_cached_instance_transforms[bullet_shape_index].get_origin());
    }
}
}