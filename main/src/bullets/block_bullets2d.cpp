#include "block_bullets2d.hpp"

#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/classes/multi_mesh.hpp"
#include "godot_cpp/classes/quad_mesh.hpp"
#include "godot_cpp/classes/physics_server2d.hpp"

#include "../factory/bullet_factory2d.hpp"
#include "godot_cpp/classes/engine.hpp"

#define physics_server PhysicsServer2D::get_singleton()

using namespace godot;

BlockBullets2D::~BlockBullets2D(){
    // For some reason the destructor runs on project start up by default, so avoid doing that
    if(Engine::get_singleton()->is_editor_hint()){
        return;
    }
    
    // Avoid memory leaks if you've used the PhysicsServer2D to generate area and shapes
    for (int i = 0; i < size; i++)
    {
        RID shape = physics_server->area_get_shape(area, 0);
        physics_server->free_rid(shape);
    }  
    physics_server->free_rid(area);
}

void BlockBullets2D::move_bullets(float delta){
    if(use_block_rotation_radians){
        Vector2 new_pos = current_position + all_cached_velocity[0] * delta;
        set_global_position(new_pos);
    
        for (int i = 0; i < size; i++)
        {
            // No point in editing transforms if the bullet has been disabled, that would mean moving disabled bullets and taking away from performance
            if(bullets_enabled_status[i] == false){
                continue;
            }

            Transform2D& new_transf = all_cached_shape_transforms[i];
            Vector2& new_shape_origin = all_cached_shape_origin[i];
            new_shape_origin = new_shape_origin + all_cached_velocity[0]*delta;

            new_transf.set_origin(new_shape_origin);

            physics_server->area_set_shape_transform(area, i, new_transf);

        }
        // Accelerate the shared singular bullet speed
        accelerate_bullet_speed(0,delta);
        
        current_position=new_pos;
    }
    else{
        for (int i = 0; i < size; i++)
        {
            if(bullets_enabled_status[i] == false){
                continue;
            }

            Transform2D& curr_instance_transf = all_cached_instance_transforms[i];
            Transform2D& curr_shape_transf = all_cached_shape_transforms[i];

            Vector2& curr_instance_origin = all_cached_instance_origin[i];
            Vector2& curr_shape_origin = all_cached_shape_origin[i];

            curr_instance_origin += all_cached_velocity[i] * delta; 
            curr_shape_origin += all_cached_velocity[i] *delta;

            curr_instance_transf.set_origin(curr_instance_origin);
            curr_shape_transf.set_origin(curr_shape_origin);

            multi->set_instance_transform_2d(i, curr_instance_transf);
            physics_server->area_set_shape_transform(area, i, curr_shape_transf);

            // Accelerate each bullet's speed
            accelerate_bullet_speed(i, delta);
        }

    }

    // Life time timer logic
    current_life_time-=delta;
    if(current_life_time <= 0){
        // Disable still active bullets (I'm not checking for bullet status,because disable_bullet already has that logic so I would be doing double checks for no reason)
        for (int i = 0; i < size; i++)
        {
            disable_bullet(i);
        }
        return;
    }

    // The texture change timer is active only if more than 1 texture has been provided (and if that's the case then max_change_texture_time will never be 0)
    if(max_change_texture_time != 0.0f){
        current_change_texture_time-=delta;
        if(current_change_texture_time <= 0.0f){
            if(current_texture_index + 1 < textures.size()){
                current_texture_index++;
            }
            else{
                current_texture_index=0;
            }

            set_texture(textures[current_texture_index]); // set new texture
            current_change_texture_time=max_change_texture_time; // reset timer
        }
    }

    // Handle rotation
    if(is_rotation_active){
        if(use_only_first_rotation_data){
            accelerate_bullet_rotation_speed(0, delta); // acceleration should be applied once every frame for the SINGULAR rotation speed that all bullets share
            float cache_speed = all_rotation_speed[0] * delta;
            for (int i = 0; i < size; i++)
            {
                // No point in rotating if the bullet has been disabled, performance will just be lost for deactivated bullets..
                if(bullets_enabled_status[i] == false){
                    continue;
                }

                rotate_bullet(i, cache_speed);
            }
        }else{
            for (int i = 0; i < size; i++)
            {
                // No point in rotating if the bullet has been disabled, performance will just be lost for deactivated bullets..
                if(bullets_enabled_status[i] == false){
                    continue;
                }

                rotate_bullet(i, all_rotation_speed[i] * delta);
                accelerate_bullet_rotation_speed(i, delta); // each bullet has its own BulletRotationData (meaning INDIVIDUAL rotation_speed that has to be accelerated every frame)
            }
        }
    }
}

void BlockBullets2D::spawn(const Ref<BlockBulletsData2D>& spawn_data, BulletFactory2D* new_factory){
    factory = new_factory;
    size = spawn_data->transforms.size(); // important, because some set_up methods use this
    
    set_up_rotation(spawn_data->all_bullet_rotation_data, spawn_data->rotate_only_textures);

    set_up_life_time_timer(spawn_data->max_life_time, spawn_data->max_life_time);
    set_up_change_texture_timer(spawn_data->textures.size(), spawn_data->max_change_texture_time, spawn_data->max_change_texture_time);
   
    generate_multimesh();
    set_up_multimesh(size, spawn_data->mesh, spawn_data->texture_size);

    generate_area();
    set_up_area(factory->physics_space, spawn_data->collision_layer, spawn_data->collision_mask, spawn_data->monitorable);
    
    make_all_bullet_instances_visible();
    generate_collision_shapes_for_area(); 
    
    if(set_up_bullets_state(
        spawn_data->collision_shape_size,
        spawn_data->transforms,
        spawn_data->texture_rotation_radians,
        spawn_data->collision_shape_offset,
        spawn_data->is_texture_rotation_permanent,
        spawn_data->all_bullet_speed_data,
        spawn_data->use_block_rotation_radians,
        spawn_data->block_rotation_radians
        ) == false){
            UtilityFunctions::print("ERROR, NO TRANSFORMS PROVIDED OR NO BULLET SPEED DATA PROVIDED");
            return;
        }
    enable_bullets_based_on_status();

    finalize_set_up(spawn_data->bullets_custom_data, spawn_data->textures, spawn_data->current_texture_index, spawn_data->material);

    factory->bullets_container->add_child(this);
    
    factory->all_bullets.push_back(this);
    emit_signal("spawned");
}

Ref<SaveDataBlockBullets2D> BlockBullets2D::save(){
    set_physics_process(false);
    set_process(false);

    Ref<SaveDataBlockBullets2D> data = memnew(SaveDataBlockBullets2D);

    // TEXTURE RELATED
    
    int amount_textures = textures.size();
    data->textures.resize(amount_textures);
    for (int i = 0; i < amount_textures; i++)
    {
        data->textures[i] = textures[i];
    }
    data->texture_size =  texture_size;
    data->max_change_texture_time = max_change_texture_time;
    data->current_change_texture_time = current_change_texture_time;
    data->current_texture_index = current_texture_index;

    // BULLET MOVEMENT RELATED
    
    data->use_block_rotation_radians=use_block_rotation_radians;
    if(use_block_rotation_radians){
        data->block_rotation_radians = block_rotation_radians;
        data->multimesh_position = current_position;
    }

    data->all_cached_instance_transforms.resize(size);
    for (int i = 0; i < size; i++)
    {
        data->all_cached_instance_transforms[i] = all_cached_instance_transforms[i];
    }

    data->all_cached_shape_transforms.resize(size);
    for (int i = 0; i < size; i++)
    {
        data->all_cached_shape_transforms[i] = all_cached_shape_transforms[i];
    }

    data->all_cached_instance_origin.resize(size);
    for (int i = 0; i < size; i++)
    {
        data->all_cached_instance_origin[i] = all_cached_instance_origin[i];
    }

    data->all_cached_shape_origin.resize(size);
    for (int i = 0; i < size; i++)
    {
        data->all_cached_shape_origin[i] = all_cached_shape_origin[i];
    }

    int speed_data_size = all_cached_velocity.size();

    data->all_cached_velocity.resize(speed_data_size);
    for (int i = 0; i < speed_data_size; i++)
    {
        data->all_cached_velocity[i] = all_cached_velocity[i];
    }

    data->all_cached_direction.resize(speed_data_size);
    for (int i = 0; i < speed_data_size; i++)
    {
        data->all_cached_direction[i] = all_cached_direction[i];
    }
    
    // BULLET SPEED RELATED

    data->all_cached_speed.resize(speed_data_size);
    for (int i = 0; i < speed_data_size; i++)
    {
        data->all_cached_speed[i] = all_cached_speed[i];
    }

    data->all_cached_max_speed.resize(speed_data_size);
    for (int i = 0; i < speed_data_size; i++)
    {
        data->all_cached_max_speed[i] = all_cached_max_speed[i];
    }

    data->all_cached_acceleration.resize(speed_data_size);
    for (int i = 0; i < speed_data_size; i++)
    {
        data->all_cached_acceleration[i] = all_cached_acceleration[i];
    }

    // BULLET ROTATION RELATED
    if(is_rotation_active){
        data->all_bullet_rotation_data.resize(all_rotation_speed.size());
        for (int i = 0; i < all_rotation_speed.size(); i++)
        {
            Ref<BulletRotationData> bullet_data = memnew(BulletRotationData);
            bullet_data->rotation_speed = all_rotation_speed[i];
            bullet_data->max_rotation_speed = all_max_rotation_speed[i];
            bullet_data->rotation_acceleration = all_rotation_acceleration[i];

            data->all_bullet_rotation_data[i] = bullet_data;
        }
        data->rotate_only_textures=rotate_only_textures;
    }

    // COLLISION RELATED

    data->collision_layer = physics_server->area_get_collision_layer(area);
    data->collision_mask = physics_server->area_get_collision_mask(area);
    data->monitorable = monitorable;

    RID shape = physics_server->area_get_shape(area,0);
    data->collision_shape_size = (Vector2)(physics_server->shape_get_data(shape));

    // OTHER
    data->max_life_time = max_life_time;
    data->current_life_time = current_life_time;
    data->size = size;

    data->material = get_material();
    data->mesh=multi->get_mesh();
    data->bullets_custom_data=bullets_custom_data;

    // Save the enabled status so you can determine which bullets were active/disabled
    data->bullets_enabled_status.resize(size);
    for (int i = 0; i < size; i++)
    {
        data->bullets_enabled_status[i] = (bool)(bullets_enabled_status[i]);
    }

    
    set_physics_process(true);
    set_process(true);
    return data;
}

void BlockBullets2D::load(const Ref<SaveDataBlockBullets2D>& data, BulletFactory2D* new_factory){
    factory = new_factory;
    size = data->all_cached_instance_transforms.size();
    
    set_up_rotation(data->all_bullet_rotation_data, data->rotate_only_textures);

    set_up_life_time_timer(data->max_life_time, data->current_life_time);
    set_up_change_texture_timer(data->textures.size(), data->max_change_texture_time, data->current_change_texture_time);
    
    generate_multimesh();
    set_up_multimesh(size, data->mesh, data->texture_size);

    generate_area();
    set_up_area(factory->physics_space, data->collision_layer, data->collision_mask, data->monitorable);
    
    make_bullet_instances_visible(data->bullets_enabled_status);
    generate_collision_shapes_for_area();
    
    load_bullets_state(data);

    enable_bullets_based_on_status();
 
    finalize_set_up(data->bullets_custom_data, data->textures, data->current_texture_index, data->material);

    factory->bullets_container->add_child(this);
    factory->all_bullets.push_back(this);
    emit_signal("spawned");
}

void BlockBullets2D::enable_bullets_based_on_status(){
    for (int i = 0; i < size; i++)
    {
        physics_server->call_deferred("area_set_shape_disabled", area, i, ((bool)bullets_enabled_status[i]) == false);
    }

    physics_server->area_set_area_monitor_callback(area, callable_mp(this, &BlockBullets2D::area_entered_func));
    physics_server->area_set_monitor_callback(area, callable_mp(this, &BlockBullets2D::body_entered_func));
}

void BlockBullets2D::disable_bullet(int bullet_index){
    // I am doing this, because there is a chance that the bullet collides with more than 1 thing at the same exact time (if I didn't have this check then the active_bullets_counter would be set wrong).
    if(bullets_enabled_status[bullet_index] == false){
        return;
    }

    active_bullets_counter--;
    multi->set_instance_color(bullet_index, Color(0,0,0,0));
    
    bullets_enabled_status[bullet_index] = false;
    physics_server->call_deferred("area_set_shape_disabled", area, bullet_index, true);
    

    if(active_bullets_counter == 0){
        disable_multi_mesh();
    }
}

void BlockBullets2D::disable_multi_mesh(){
    set_visible(false); // very important to hide it, otherwise it will be rendering the transparent bullets when there is no need, which will tank performance
    set_physics_process(false);
    set_process(false);
    current_life_time = 0.0f; //Important. This way I determine which bullets should not be saved/ are not yet set up correctly
    
    factory->add_bullets_to_pool(this);
}

void BlockBullets2D::activate_multimesh(const Ref<BlockBulletsData2D>& data){
    set_up_rotation(data->all_bullet_rotation_data, data->rotate_only_textures);

    set_up_life_time_timer(data->max_life_time, data->max_life_time);
    set_up_change_texture_timer(data->textures.size(), data->max_change_texture_time, data->max_change_texture_time);
    
    set_up_multimesh(size, data->mesh, data->texture_size);

    set_up_area(factory->physics_space, data->collision_layer, data->collision_mask, data->monitorable);

    if(set_up_bullets_state(
        data->collision_shape_size,
        data->transforms,
        data->texture_rotation_radians,
        data->collision_shape_offset,
        data->is_texture_rotation_permanent,
        data->all_bullet_speed_data,
        data->use_block_rotation_radians,
        data->block_rotation_radians
        ) == false){
            UtilityFunctions::print("ERROR, NO TRANSFORMS PROVIDED OR NO BULLET SPEED DATA PROVIDED");
            return;
        }

    make_all_bullet_instances_visible();
    enable_bullets_based_on_status();

    finalize_set_up(data->bullets_custom_data, data->textures, data->current_texture_index, data->material);
}

_ALWAYS_INLINE_ void BlockBullets2D::accelerate_bullet_speed(int speed_data_id, float delta){
    float curr_bullet_speed = all_cached_speed[speed_data_id];
    float curr_max_bullet_speed = all_cached_max_speed[speed_data_id];

    if(curr_bullet_speed == curr_max_bullet_speed){
        return;
    }

    all_cached_speed[speed_data_id] = std::min<float>(curr_bullet_speed + all_cached_acceleration[speed_data_id] * delta, curr_max_bullet_speed);
    all_cached_velocity[speed_data_id] = all_cached_direction[speed_data_id] * all_cached_speed[speed_data_id];
}

_ALWAYS_INLINE_ void BlockBullets2D::rotate_bullet(int multi_instance_id, float rotated_angle){
    Transform2D& rotated_transf = all_cached_instance_transforms[multi_instance_id];
    rotated_transf = rotated_transf.rotated_local(rotated_angle);

    multi->set_instance_transform_2d(multi_instance_id, rotated_transf);

    if(rotate_only_textures == false){
        Transform2D& rotated_shape_transf = all_cached_shape_transforms[multi_instance_id];
        rotated_shape_transf = rotated_shape_transf.rotated_local(rotated_angle);

        physics_server->area_set_shape_transform(area, multi_instance_id, rotated_shape_transf);
    }
}

_ALWAYS_INLINE_ void BlockBullets2D::accelerate_bullet_rotation_speed(int multi_instance_id, float delta){
    if(all_rotation_speed[multi_instance_id] == all_max_rotation_speed[multi_instance_id]){
        return;
    }
    
    all_rotation_speed[multi_instance_id] = std::min<float>(all_rotation_speed[multi_instance_id] + (all_rotation_acceleration[multi_instance_id]*delta), all_max_rotation_speed[multi_instance_id]);
}

void BlockBullets2D::generate_multimesh(){
    multi = memnew(MultiMesh);
    multi->set_use_colors(true);
    set_multimesh(multi);
}

void BlockBullets2D::set_up_multimesh(int new_instance_count, const Ref<Mesh>& new_mesh, Vector2 new_texture_size){
    if(new_mesh.is_valid()){
        multi->set_mesh(new_mesh);
    }
    else{
        // TODO maybe if the mesh to be generated has the size size as the previous one, then there is no need to generate a brand new one?
        Ref<QuadMesh> mesh = memnew(QuadMesh);
        mesh->set_size(new_texture_size);
        multi->set_mesh(mesh);
        texture_size = new_texture_size;
    }
    
    multi->set_instance_count(new_instance_count);
}

void BlockBullets2D::set_up_rotation(TypedArray<BulletRotationData>& new_data, bool new_rotate_only_textures){
        int amount_of_rotation_data = new_data.size();
        // If the amount of data that was provided is not a single one and also not the same amount as the bullets amount
        // then rotation should be disabled/invalid.
        // This is because I want the user to either provide a single BulletRotationData that will be used for every single bullet
        // or BulletRotation data for each bullet that will allow each bullet to rotate differently. Only these 2 cases are valid.
        
        if(amount_of_rotation_data != size){
            if(amount_of_rotation_data != 1){
                is_rotation_active = false;
                return;
            }

            use_only_first_rotation_data=true; // Important. Means only a single data was provided, so use it for every single bullet. If I don't have this variable then in _process I will be trying to access invalid vector indexes
        }
        rotate_only_textures=new_rotate_only_textures;
        is_rotation_active = true;  // Important, because it determines if we have rotation data to use
        all_rotation_speed.resize(amount_of_rotation_data);
        all_max_rotation_speed.resize(amount_of_rotation_data);
        all_rotation_acceleration.resize(amount_of_rotation_data);

        for (int i = 0; i < amount_of_rotation_data; i++)
        {
            BulletRotationData& curr_bullet_data = *Object::cast_to<BulletRotationData>(new_data[i]); // Found out that if you have a TypedArray<>, trying to access an element with [] will give you Variant, so in order to cast it use Object::cast_to<>(), the normal reinterpret_cast and the C way of casting didn't work hmm

            all_rotation_speed[i] = curr_bullet_data.rotation_speed;
            all_max_rotation_speed[i] = curr_bullet_data.max_rotation_speed;
            all_rotation_acceleration[i] = curr_bullet_data.rotation_acceleration;
        }
}

void BlockBullets2D::set_up_life_time_timer(float new_max_life_time, float new_current_life_time){
    max_life_time = new_max_life_time;
    current_life_time = new_current_life_time;
}
void BlockBullets2D::set_up_change_texture_timer(int new_amount_textures, float new_max_change_texture_time, float new_current_change_texture_time){
    if(new_amount_textures > 1){ // the change texture timer will be active only if more than 1 texture was provided
        max_change_texture_time = new_max_change_texture_time;
        current_change_texture_time = new_current_change_texture_time;
    }
}

void BlockBullets2D::generate_area(){
    area = physics_server->area_create();
}

void BlockBullets2D::set_up_area(
    RID new_world_space,
    uint32_t new_collision_layer,
    uint32_t new_collision_mask,
    bool new_monitorable
){
    physics_server->area_set_space(area, new_world_space);
    physics_server->area_set_collision_layer(area, new_collision_layer);
    physics_server->area_set_collision_mask(area, new_collision_mask);
    physics_server->area_set_monitorable(area, new_monitorable);

    monitorable = new_monitorable;
}

void BlockBullets2D::generate_collision_shapes_for_area(){
    for (int i = 0; i < size; i++)
    {
        RID shape = physics_server->rectangle_shape_create();
        physics_server->area_add_shape(area, shape);
        physics_server->area_set_shape_disabled(area,i,true); // By default make it disabled
    }
}

bool BlockBullets2D::set_up_bullets_state(
    Vector2 new_collision_shape_size,
    const TypedArray<Transform2D>& new_transforms, // make sure you are giving transforms that don't have collision offset applied, otherwise it will apply it twice
    float new_texture_rotation,
    Vector2 new_collision_shape_offset,
    bool new_is_texture_rotation_permanent,
    const TypedArray<BulletSpeedData>& new_speed_data,
    bool new_use_block_rotation_radians,
    float new_block_rotation_radians
){
    int speed_data_size = size; // the size of the all_cached vectors
    int all_speed_data_size = new_speed_data.size(); // the amount of speed data provided

    if(all_speed_data_size == 0 || size == 0){
        return false;
    }

    // Case 1 - when the user forces for the bullets to be treated as a block that has the same BulletSpeedData - done by setting the use_block_rotation_radians to true. Only the first BulletSpeedData will be used
    // Case 2 - when only a single BulletSpeedData was provided (this is literally like Case 1, but the user has forgotten to set the use_block_rotation_radians to true) - no point in saving copies of the same exact data in each cached vector index and doing the same exact calculations when they are the same... pointless and will reduce performance
    // Case 3 - when the user has provided more than 1 BulletSpeedData, but the BulletSpeedData amount is not enough for all bullets (meaning some will have it others won't), in this case all bullets will again use ONLY the first BulletSpeedData and ignore the rest.. the user is REQUIRED to provide BulletSpeedData for each individual bullet if he wants them to have different speed/max_speed/acceleration
    // Note: the && size !=1 part is to ensure that when the user is trying to spawn only a single bullet, the bullet will use the transform rotation by default and NOT the block_rotation_radians. Basically an edge case.
    if(new_use_block_rotation_radians || (all_speed_data_size == 1 && size != 1) || (all_speed_data_size != size && size !=1)){
        block_rotation_radians = new_block_rotation_radians;
        speed_data_size = 1; // if use_block_rotation_radians it means we are going to store only a single cached data inside each cached array, so the size should be equal to 1, instead of being equal to the amount of bullets
        use_block_rotation_radians=true;
    }else{
        use_block_rotation_radians=false; // important because this set_up method gets reused by the activate_multimesh method, that means the default value of the boolean may not be FALSE by default when activating deactivated bullets (some of them may have been spawned with use_block_rotation_radians = true), so If I dont do this im risking of spawning bullets that should have their own direction BUT instead ARE FORCED to use the old use_block_rotation_radians of the deactivated multimesh
    }
    current_position = Vector2(0,0);
    set_global_position(Vector2(0,0));

    // Make space at once for all the data that will come in (reserving memory at once is more performant rather than using .push and letting the container resize itself/ constantly copy old items and place them inside new found memory that would fit all, this operation will happen when pushing an element exceeds the internal size.. no point in doing it when I know what size the vectors will be)
    all_cached_speed.resize(speed_data_size);
    all_cached_max_speed.resize(speed_data_size);
    all_cached_acceleration.resize(speed_data_size);

    // Always caching these for every single bullet no matter the use_block_rotation_radians value
    all_cached_instance_transforms.resize(size);
    all_cached_shape_transforms.resize(size);
    all_cached_shape_origin.resize(size);
    all_cached_instance_origin.resize(size);

    all_cached_direction.resize(speed_data_size);
    all_cached_velocity.resize(speed_data_size); // this is an exception, because if we have only a single direction and a single speed then there is no point of storing the SAME velocity calculation over and over again for each bullet

    for (int i = 0; i < size; i++)
    {
        RID shape = physics_server->area_get_shape(area, i);
        physics_server->shape_set_data(shape, new_collision_shape_size/2); // SHAPE_RECTANGLE: a Vector2 half_extents  (I'm deviding by 2 to get the actual size the user wants for the rectangle, the function wants half_extents, if it gets the size 32 for the x, that means only the half width is 32 so the other half will also be 32 meaning 64 total width if I give the user's 32 that he said, to avoid that im deviding by 2 so the size gets set correctly to 32)
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

        // Texture rotation
        Transform2D texture_transf = new_transforms[i];
        // The transform is used for both the collision shape and the texture by default, in case the texture is not rotated correctly, the user can enter an additional texture rotation that will rotate the texture even more, but the collision shape's rotation won't change, so make sure to pass correct transform data
        if(new_is_texture_rotation_permanent){
            // Same texture rotation no matter the rotation of the bullet's transform
            texture_transf.set_rotation(new_texture_rotation);
        }else{
            // The rotation of the texture will be influenced by the rotation of the bullet transform
            texture_transf.set_rotation(texture_transf.get_rotation()+new_texture_rotation);
        }

        multi->set_instance_transform_2d(i, texture_transf);

        // Cache movement related logic
        all_cached_instance_transforms[i] = texture_transf;
        all_cached_shape_transforms[i] = shape_transf;

        all_cached_instance_origin[i] = texture_transf.get_origin();
        all_cached_shape_origin[i] = shape_transf.get_origin();

        // If use_block_rotation_radians is true, that means we have only a single speed data to work with that is shared with all bullets, so there isn't really space in the cached arrays for other data (their size is set to 1), so skip the rest of the for loop
        if(use_block_rotation_radians && i>0){
            continue;
        }

        Ref<BulletSpeedData> curr_speed_data = new_speed_data[i];
        all_cached_speed[i] = curr_speed_data->speed;
        all_cached_max_speed[i] = curr_speed_data->max_speed;
        all_cached_acceleration[i] = curr_speed_data->acceleration;

        // If use_block_rotation_radians that means all bullets should move as a block in the same direction, and the direction is determined by block_rotation_radians NOT the first transform rotation
        if(use_block_rotation_radians){
            curr_bullet_rotation = block_rotation_radians;
        }

        // Calculate the direction
        all_cached_direction[i] = Vector2(Math::cos(curr_bullet_rotation), Math::sin(curr_bullet_rotation));
        
        // Calculate the velocity
        all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i];
    }

    return true;
}

 void BlockBullets2D::load_bullets_state(
    const Ref<SaveDataBlockBullets2D>& data
 ){
    int new_speed_data_size = data->all_cached_velocity.size();

    all_cached_speed.resize(new_speed_data_size);
    all_cached_max_speed.resize(new_speed_data_size);
    all_cached_acceleration.resize(new_speed_data_size);

    all_cached_instance_transforms.resize(size);
    all_cached_shape_transforms.resize(size);
    all_cached_instance_origin.resize(size);
    all_cached_shape_origin.resize(size);

    all_cached_velocity.resize(new_speed_data_size);
    all_cached_direction.resize(new_speed_data_size);
    
    for (int i = 0; i < new_speed_data_size; i++)
    {
        all_cached_speed[i] = data->all_cached_speed[i];
        all_cached_max_speed[i] = data->all_cached_max_speed[i];
        all_cached_acceleration[i] = data->all_cached_acceleration[i];

        all_cached_velocity[i] = data->all_cached_velocity[i];
        all_cached_direction[i] = data->all_cached_direction[i];
    }
    
    for (int i = 0; i < size; i++)
    {
        all_cached_instance_transforms[i] = data->all_cached_instance_transforms[i];
        all_cached_shape_transforms[i] = data->all_cached_shape_transforms[i];
        all_cached_instance_origin[i] = data->all_cached_instance_origin[i];
        all_cached_shape_origin[i] = data->all_cached_shape_origin[i];

        RID shape_rid = physics_server->area_get_shape(area,i);
        physics_server->shape_set_data(shape_rid, data->collision_shape_size);
        physics_server->area_set_shape_transform(area, i, all_cached_shape_transforms[i]);

        multi->set_instance_transform_2d(i,all_cached_instance_transforms[i]);
    }

    use_block_rotation_radians=data->use_block_rotation_radians;
    if(use_block_rotation_radians){
        block_rotation_radians=data->block_rotation_radians;
        set_global_position(data->multimesh_position);
        current_position=data->multimesh_position;
    }else{
        current_position = Vector2(0,0);
        set_global_position(Vector2(0,0));
    }
 }

void BlockBullets2D::make_all_bullet_instances_visible(){
    bullets_enabled_status.resize(size);
    active_bullets_counter=size;

    for (int i = 0; i < size; i++)
    {
        multi->set_instance_color(i, Color(1,1,1,1));
        bullets_enabled_status[i] = true;
    }
    
}
void BlockBullets2D::make_bullet_instances_visible(const TypedArray<bool>& new_bullets_enabled_status){
    bullets_enabled_status.resize(size);
    for (int i = 0; i < size; i++)
    {
        if((bool)new_bullets_enabled_status[i] == true){
            multi->set_instance_color(i, Color(1,1,1,1));
            active_bullets_counter++;
            bullets_enabled_status[i]=true;

        }else{
            multi->set_instance_color(i, Color(0,0,0,0));
            bullets_enabled_status[i]=false;
        }
    } 
}

void BlockBullets2D::finalize_set_up(
    const Ref<Resource>& new_bullets_custom_data,
    const TypedArray<Texture2D>& new_textures,
    int new_current_texture_index,
    const Ref<Material>& new_material
    ){
    // Bullets custom data
    if (new_bullets_custom_data.is_valid()){ // make sure its valid before trying to duplicate it..
        bullets_custom_data = new_bullets_custom_data->duplicate(true); // the reason im duplicating it, is for the user to not worry about editing the resource he already has in GDScript
    }

    // Texture logic
    textures.resize(new_textures.size());
    for (int i = 0; i < new_textures.size(); i++)
    {
        textures[i] = new_textures[i];
    }

    // Make sure the current_texture_index is valid
    if(new_current_texture_index >= textures.size() || new_current_texture_index < 0 ){
        new_current_texture_index=0;
    }
    current_texture_index = new_current_texture_index; // remember the current index of the current texture
    
    if(textures.size()>0){ // Also make sure that there are textures to set
        set_texture(textures[current_texture_index]); // setting the current texture
    }

    // Material
    if(new_material.is_valid()){
        set_material(new_material);
    }

    set_physics_process(true);
    set_process(true);
    set_visible(true);
}

void BlockBullets2D::area_entered_func(int status, RID entered_rid, uint64_t entered_instance_id, int entered_shape_index, int bullet_shape_index){
    if(status == PhysicsServer2D::AREA_BODY_ADDED ){
        Object* obj = ObjectDB::get_instance(entered_instance_id);
        disable_bullet(bullet_shape_index);
        // another option would be to emit a signal here, and then the factory to register a callback for it and then emit its own signal, but I felt that it would be slower and also very messy, I also thought of keeping pointers to outside functions, but again its messier. Best solution I feel like is just keeping a pointer to the factory itself, which also allows me to call pool methods.
        factory->emit_signal("area_entered", obj, bullets_custom_data, multi->get_instance_transform_2d(bullet_shape_index));
    }   
}

void BlockBullets2D::body_entered_func(int status, RID entered_rid, uint64_t entered_instance_id, int entered_shape_index, int bullet_shape_index){
    if(status == PhysicsServer2D::AREA_BODY_ADDED ){
        Object* obj = ObjectDB::get_instance(entered_instance_id);
        disable_bullet(bullet_shape_index);
        factory->emit_signal("body_entered", obj, bullets_custom_data, multi->get_instance_transform_2d(bullet_shape_index));
    }
}

void BlockBullets2D::_bind_methods(){
    ClassDB::bind_method(D_METHOD("spawn", "spawn_data", "world_space"),&BlockBullets2D::spawn);
    ClassDB::bind_method(D_METHOD("save"), &BlockBullets2D::save);
    
    ClassDB::bind_method(D_METHOD("activate_multimesh", "spawn_data"),&BlockBullets2D::activate_multimesh);
    ClassDB::bind_method(D_METHOD("load", "data","world_space"),&BlockBullets2D::load);
    ADD_SIGNAL(MethodInfo("spawned"));
}