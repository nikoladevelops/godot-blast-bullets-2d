#include "block_bullets2d.hpp"

#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/classes/multi_mesh.hpp"
#include "godot_cpp/classes/quad_mesh.hpp"
#include "godot_cpp/classes/physics_server2d.hpp"

#include "../factory/bullet_factory2d.hpp"
#include "godot_cpp/classes/engine.hpp"

#define physics_server PhysicsServer2D::get_singleton()

using namespace godot;
BlockBullets2D::BlockBullets2D(){
}

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

void BlockBullets2D::_ready(){
    set_physics_process(false);
    set_process(false);
    set_visible(false);
}

void BlockBullets2D::_physics_process(float delta){
    Vector2 new_pos = current_position + velocity * delta;
    set_global_position(new_pos);
    
    for (int i = 0; i < size; i++)
    {
        // No point in editing transforms if the bullet has been disabled, that would mean moving disabled bullets and taking away from performance
        if(bullets_enabled_status[i] == false){
            continue;
        }
        Transform2D new_transf = physics_server->area_get_shape_transform(area,i);
        Vector2 new_transf_origin = new_transf.get_origin() + velocity*delta;
        new_transf.set_origin(new_transf_origin);

        physics_server->area_set_shape_transform(area, i, new_transf);
    }
    
    current_position=new_pos;

    // Timer logic

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

    // The Texture change timer is active only if more than 1 texture has been provided (and if that's the case then max_change_texture_time will never be 0)
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

    // Acceleration timer
    if(max_acceleration_time != 0.0f){
        current_acceleration_time-=delta;
        if(current_acceleration_time <= 0.0f){
            speed = std::min<float>(speed+acceleration, max_speed); // make sure speed doesn't go above max_speed
            
            velocity = Vector2(cos(block_rotation_radians), sin(block_rotation_radians)) * speed; // find the new velocity

            // If the speed reaches the max_speed then there is no reason to use an acceleration timer, so disable it by setting it to 0.
            if(speed == max_speed){
                max_acceleration_time=0.0f;
            }else{
                current_acceleration_time = max_acceleration_time; // make sure to begin from max_acceleration_time so you can accelerate once again
            }
        }
    }
}

void BlockBullets2D::spawn(const Ref<BlockBulletsData2D>& spawn_data, BulletFactory2D* new_factory){
    factory = new_factory;
    factory->bullets_container->add_child(this);

    set_global_position(Vector2(0,0));

    // Initial movement logic
    block_rotation_radians=spawn_data->block_rotation_radians;
    speed = spawn_data->speed;

    velocity = Vector2(cos(block_rotation_radians), sin(block_rotation_radians)) * speed;
    current_position = Vector2(0,0);

    size = spawn_data->transforms.size(); // important, because some set_up methods use this
    
    set_up_life_time_timer(spawn_data->max_life_time, spawn_data->max_life_time);
    set_up_change_texture_timer(spawn_data->textures.size(), spawn_data->max_change_texture_time, spawn_data->max_change_texture_time);
    set_up_acceleration_timer(spawn_data->max_speed, spawn_data->acceleration, spawn_data->max_acceleration_time, spawn_data->max_acceleration_time);
    
    generate_multimesh();
    set_up_multimesh(size, spawn_data->mesh, spawn_data->texture_size);

    generate_area();
    set_up_area(factory->physics_space, spawn_data->collision_layer, spawn_data->collision_mask, spawn_data->monitorable);
    
    generate_collision_shapes_for_area();
    set_up_collision_shapes_for_area(spawn_data->collision_shape_size, spawn_data->transforms, spawn_data->texture_rotation_radians, spawn_data->collision_shape_offset);
    
    make_all_bullet_instances_visible();
    finalize_set_up(spawn_data->bullets_custom_data, spawn_data->textures, spawn_data->current_texture_index, spawn_data->material);
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
void BlockBullets2D::set_up_acceleration_timer(float new_max_speed, float new_acceleration, float new_max_acceleration_time, float new_current_acceleration_time){
    if(new_max_speed != -1){ // if its -1, it means disabled, so acceleration won't be used
        max_speed=new_max_speed;
        acceleration=new_acceleration;

        // Acceleration timer
        max_acceleration_time=new_max_acceleration_time;
        current_acceleration_time=new_current_acceleration_time;
    }
}

void BlockBullets2D::generate_area(){
    area = physics_server->area_create();

    physics_server->area_set_area_monitor_callback(area, callable_mp(this, &BlockBullets2D::area_entered_func));
    physics_server->area_set_monitor_callback(area, callable_mp(this, &BlockBullets2D::body_entered_func));
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
    }
}

void BlockBullets2D::set_up_collision_shapes_for_area(
    Vector2 new_collision_shape_size,
    const TypedArray<Transform2D>& new_original_collision_shape_transforms, // make sure you are giving transforms that don't have collision offset applied, otherwise it will apply it twice
    float new_texture_rotation,
    Vector2 new_collision_shape_offset
){
    for (int i = 0; i < size; i++)
    {
        RID shape = physics_server->area_get_shape(area, i);
        physics_server->shape_set_data(shape, new_collision_shape_size/2); // SHAPE_RECTANGLE: a Vector2 half_extents  (I'm deviding by 2 to get the actual size the user wants for the rectangle, the function wants half_extents, if it gets the size 32 for the x, that means only the half width is 32 so the other half will also be 32 meaning 64 total width if I give the user's 32 that he said, to avoid that im deviding by 2 so the size gets set correctly to 32)
        // Collision shape offset
        Transform2D shape_transf = new_original_collision_shape_transforms[i];

        // The offset also needs to be rotated
        Vector2 rotated_offset;
        rotated_offset.x = new_collision_shape_offset.x * Math::cos(block_rotation_radians) - new_collision_shape_offset.y * Math::sin(block_rotation_radians);
        rotated_offset.y = new_collision_shape_offset.x * Math::sin(block_rotation_radians) + new_collision_shape_offset.y * Math::cos(block_rotation_radians);

        shape_transf.set_origin(shape_transf.get_origin() + rotated_offset); //Vector2(cos(block_rotation_radians), sin(block_rotation_radians))

        physics_server->area_set_shape_transform(area, i, shape_transf);

        // Texture rotation
        Transform2D texture_transf = new_original_collision_shape_transforms[i];
        // The transform is used for both the collision shape and the texture by default, in case the texture is not rotated correctly, the user can enter an additional texture rotation that will rotate the texture even more, but the collision shape's rotation won't change, so make sure to pass correct transform data.
        texture_transf.set_rotation(texture_transf.get_rotation()+new_texture_rotation);

        multi->set_instance_transform_2d(i, texture_transf);
        
        texture_rotation_radians = new_texture_rotation;
        collision_shape_offset = new_collision_shape_offset;
    }
}

void BlockBullets2D::make_all_bullet_instances_visible(){
    bullets_enabled_status.resize(size,true);
    active_bullets_counter=size;

    for (int i = 0; i < size; i++)
    {
        multi->set_instance_color(i, Color(1,1,1,1));
    }
    
}
void BlockBullets2D::make_bullet_instances_visible(const TypedArray<bool>& new_bullets_enabled_status){
    bullets_enabled_status.resize(size);
    for (int i = 0; i < size; i++)
    {
        if(new_bullets_enabled_status[i]){
            multi->set_instance_color(i, Color(1,1,1,1));
            bullets_enabled_status[i] = true;
            active_bullets_counter++;
        }else{
            multi->set_instance_color(i, Color(0,0,0,0));
            bullets_enabled_status[i] = false;
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
    for (int i = 0; i < new_textures.size(); i++)
    {
        textures.push_back(new_textures[i]);
    }

    current_texture_index = new_current_texture_index; // remember the current index of the current texture
    
    set_texture(textures[current_texture_index]); // setting the current texture


    // Material
    if(new_material.is_valid()){
        set_material(new_material);
    }

    set_physics_process(true);
    set_process(true);
    set_visible(true);
    emit_signal("spawned");
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
    current_life_time = 0; // this is how I differentiate between bullets that are in the pool and bullets that are still active
    factory->add_bullets_to_pool(this);
}

Ref<SaveDataBlockBullets2D> BlockBullets2D::save(){
    Ref<SaveDataBlockBullets2D> data = memnew(SaveDataBlockBullets2D);

    data->transforms.resize(size);
    for (int i = 0; i < size; i++)
    {
        // I am saving the original shape transform without the collision shape offset applied to it, this is so I can reuse my methods fully when loading and avoid complicating my code further
        Transform2D transform_without_shape_offset = physics_server->area_get_shape_transform(area,i);

        Vector2 rotated_offset;
        rotated_offset.x = collision_shape_offset.x * Math::cos(block_rotation_radians) - collision_shape_offset.y * Math::sin(block_rotation_radians);
        rotated_offset.y = collision_shape_offset.x * Math::sin(block_rotation_radians) + collision_shape_offset.y * Math::cos(block_rotation_radians);

        transform_without_shape_offset.set_origin(transform_without_shape_offset.get_origin() - rotated_offset);

        data->transforms[i] = transform_without_shape_offset;
    }
    
    data->velocity = velocity;
    data->current_position=current_position; // TODO potentially useless?
    data->max_life_time = max_life_time;
    data->current_life_time = current_life_time; // todo check the timers if they are actually valid or not.
    data->size = size;

    data->bullets_enabled_status.resize(size);
    
    for (int i = 0; i < size; i++)
    {
        data->bullets_enabled_status[i] = bullets_enabled_status[i] == true; // [] doesn't give me a boolean so I improvise
    }
    
    data->bullets_custom_data=bullets_custom_data;

    int amount_textures = textures.size();
    data->textures.resize(amount_textures);
    for (int i = 0; i < amount_textures; i++)
    {
        data->textures[i] = textures[i];
    }
    data->texture_rotation_radians = texture_rotation_radians;
    data->texture_size =  texture_size;
    data->max_change_texture_time = max_change_texture_time;
    data->current_change_texture_time = current_change_texture_time;

    data->current_texture_index = current_texture_index;

    data->block_rotation_radians = block_rotation_radians;
    data->max_speed = max_speed;
    data->speed = speed;
    data->acceleration = acceleration;

    data->max_acceleration_time = max_acceleration_time;
    data->current_acceleration_time = current_acceleration_time;
   
    data->collision_layer = physics_server->area_get_collision_layer(area);
    data->collision_mask = physics_server->area_get_collision_mask(area);
    data->monitorable = monitorable;

    RID shape = physics_server->area_get_shape(area,0);
    data->collision_shape_size = (Vector2)(physics_server->shape_get_data(shape))*2; // again, because im using a rectangle, it will give me the half extents, meaning half the width and half the height, so the actual size that the user gave is actually the Vector2 the functon returns multiplied by 2. I am trying to recover the original data that the user entered, so that I can reuse the same exact functions, that's why im doing this.
    data->collision_shape_offset = collision_shape_offset;
    data->material = get_material();
    data->mesh=multi->get_mesh();

    return data;
}

void BlockBullets2D::load(const Ref<SaveDataBlockBullets2D>& data, BulletFactory2D* new_factory){
    factory = new_factory;
    factory->bullets_container->add_child(this);

    block_rotation_radians = data->block_rotation_radians;
    speed = data->speed;
    velocity = data->velocity;

    size = data->transforms.size();
    
    set_up_life_time_timer(data->max_life_time, data->current_life_time);
    set_up_change_texture_timer(data->textures.size(), data->max_change_texture_time, data->current_change_texture_time);
    set_up_acceleration_timer(data->max_speed, data->acceleration, data->max_acceleration_time, data->current_acceleration_time);
    
    generate_multimesh();
    set_up_multimesh(size, data->mesh, data->texture_size);

    generate_area();
    set_up_area(factory->physics_space, data->collision_layer, data->collision_mask, data->monitorable);
    
    generate_collision_shapes_for_area();
    set_up_collision_shapes_for_area(data->collision_shape_size, data->transforms, data->texture_rotation_radians, data->collision_shape_offset);
    
    make_bullet_instances_visible(data->bullets_enabled_status);
    finalize_set_up(data->bullets_custom_data, data->textures, data->current_texture_index, data->material);
}

void BlockBullets2D::activate_multimesh(const Ref<BlockBulletsData2D>& data){
    set_global_position(Vector2(0,0));
    
    // Set all bullet collision shapes to be active
    for (int i = 0; i < size; i++)
    {
        bullets_enabled_status[i]=true;
        physics_server->area_set_shape_disabled(area, i, false);
    }

    
    block_rotation_radians=data->block_rotation_radians;
    speed = data->speed;

    velocity = Vector2(cos(block_rotation_radians), sin(block_rotation_radians)) * speed;
    current_position = Vector2(0,0);


    set_up_life_time_timer(data->max_life_time, data->max_life_time);
    set_up_change_texture_timer(data->textures.size(), data->max_change_texture_time, data->max_change_texture_time);
    set_up_acceleration_timer(data->max_speed, data->acceleration, data->max_acceleration_time, data->max_acceleration_time);
    
    set_up_multimesh(size, data->mesh, data->texture_size);

    set_up_area(factory->physics_space, data->collision_layer, data->collision_mask, data->monitorable);
    set_up_collision_shapes_for_area(data->collision_shape_size, data->transforms, data->texture_rotation_radians, data->collision_shape_offset);
    make_all_bullet_instances_visible();
    finalize_set_up(data->bullets_custom_data, data->textures, data->current_texture_index, data->material);
}

void BlockBullets2D::_bind_methods(){
    ClassDB::bind_method(D_METHOD("spawn", "spawn_data", "world_space"),&BlockBullets2D::spawn);
    ClassDB::bind_method(D_METHOD("save"),&BlockBullets2D::save);

    ADD_SIGNAL(MethodInfo("spawned"));
}