#include "./multimesh_bullets2d.hpp"
#include "../factory/bullet_factory2d.hpp"
#include "../shared/multimesh_object_pool2d.hpp"

#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/print_string.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "multimesh_bullets2d.hpp"
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

MultiMeshBullets2D::~MultiMeshBullets2D() {
	// For some reason the destructor runs on project start up by default, so avoid doing that
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	// Avoid memory leaks if you've used the PhysicsServer2D to generate area and shapes
	for (auto &shape : physics_shapes) {
		physics_server->free_rid(shape);
	}

	physics_server->free_rid(area);
}

int MultiMeshBullets2D::get_amount_active_attachments() const {
	int amount_active_attachments = 0;

	for (int i = 0; i < amount_bullets; ++i) {
		if (attachments[i] != nullptr) {
			++amount_active_attachments;
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
			data.change_texture_times);

	generate_multimesh();
	set_up_multimesh(amount_bullets, data.mesh, data.texture_size);

	area = physics_server->area_create();
	generate_physics_shapes_for_area(amount_bullets);

	set_up_bullet_instances(data);

	// Set up bullet attachments so that for every bullet you will be able to have an attachment if needed

	// TODO might be best to have a property that says whether attachments are used or not so that we dont allocate memory for no reason..

	attachment_scenes.resize(amount_bullets, nullptr);

	attachment_pooling_ids.resize(amount_bullets, 0);

	attachments.resize(amount_bullets, nullptr);

	attachment_transforms.resize(amount_bullets, Transform2D());

	attachment_offsets.resize(amount_bullets, Vector2());

	attachment_local_transforms.resize(amount_bullets, Transform2D());

	attachment_stick_relative_to_bullet.resize(amount_bullets, 1);

	set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures);

	all_previous_instance_transf.resize(amount_bullets);
	all_previous_attachment_transf.resize(amount_bullets);

	update_all_previous_transforms_for_interpolation();

	finalize_set_up(
			data.bullets_custom_data,
			data.textures,
			data.default_texture,
			data.current_texture_index,
			data.material,
			data.z_index,
			data.light_mask,
			data.visibility_layer,
			data.instance_shader_parameters);

	custom_additional_spawn_logic(data);

	is_active = true;
	bullets_container->add_child(this);
}

// Activates the multimesh
void MultiMeshBullets2D::enable_multimesh(const MultiMeshBulletsData2D &data) {
	set_up_life_time_timer(data.max_life_time, data.max_life_time);
	set_up_change_texture_timer(
			data.textures.size(),
			data.default_change_texture_time,
			data.change_texture_times);

	set_up_multimesh(amount_bullets, data.mesh, data.texture_size);

	set_up_bullet_instances(data);
	set_all_physics_shapes_enabled_for_area(true);

	set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures);

	move_to_front(); // Makes sure that the current old multimesh is displayed on top of the newer ones (act as if its the oldest sibling to emulate the behaviour of spawning a brand new multimesh / if I dont do this then the multimesh's instances will be displayed behind the newer ones)

	update_all_previous_transforms_for_interpolation();

	finalize_set_up(
			data.bullets_custom_data,
			data.textures,
			data.default_texture,
			data.current_texture_index,
			data.material,
			data.z_index,
			data.light_mask,
			data.visibility_layer,
			data.instance_shader_parameters);

	custom_additional_enable_logic(data);

	set_visible(true);
	is_active = true;
}

void MultiMeshBullets2D::set_up_bullet_instances(const MultiMeshBulletsData2D &data) {
	active_bullets_counter = amount_bullets;

	bullet_max_collision_count = data.bullet_max_collision_count;

	if (data.bullets_current_collision_count.size() == 0) {
		bullets_current_collision_count.clear();
		bullets_current_collision_count.resize(amount_bullets, 0);
	} else {
		bool success = set_bullets_current_collision_count(data.bullets_current_collision_count);
		if (!success) {
			return;
		}
	}

	is_life_time_over_signal_enabled = data.is_life_time_over_signal_enabled;

	is_life_time_infinite = data.is_life_time_infinite;

	bullets_enabled_status.assign(amount_bullets, true);
	set_up_area(data.collision_layer, data.collision_mask, data.monitorable, bullet_factory->physics_space);

	stop_rotation_when_max_reached = data.stop_rotation_when_max_reached;

	cache_collision_shape_offset = data.collision_shape_offset;

	if (all_cached_instance_transforms.size() != 0) {
		// If there was old data then we are currently trying to enable a bullets multimesh, so clear everything that is old
		// Note: We never really resize any of these vectors, so capacity always stays the same and the object pooling logic also ensures of this, so no need to reserve different amount of space since it's always going to be the original capacity value/ no memory reallocations
		// Note: This logic relies on the fact that we always enable multimesh bullets based on their original amount_bullets - that's how the object pooling logic works in order to re-use everything
		all_cached_instance_transforms.clear();
		all_cached_instance_origin.clear();
		all_cached_shape_transforms.clear();
		all_cached_shape_origin.clear();
	} else {
		// If there wasn't any old data, that means we are spawning a bullets multimesh, so we need to ensure that all data structures reserve needed memory at once
		all_cached_instance_transforms.reserve(amount_bullets);
		all_cached_instance_origin.reserve(amount_bullets);
		all_cached_shape_transforms.reserve(amount_bullets);
		all_cached_shape_origin.reserve(amount_bullets);
	}

	cache_texture_rotation_radians = data.texture_rotation_radians;
	cache_texture_transforms.resize(amount_bullets);

	// // An attachment_id will always be set but that does not mean it's valid.
	// cache_attachment_id = set_attachment_related_data(data.attachment_scenes, // TODO this needs to be removed data.bullet_attachment_offset);

	// BulletAttachmentObjectPool2D &attachment_pool = bullet_factory->bullet_attachments_pool;

	for (int i = 0; i < amount_bullets; ++i) {
		RID shape = physics_shapes[i];

		const Transform2D &curr_data_transf = data.transforms[i];

		// Generates a collision shape transform for a particular bullet and attaches it to the area
		Transform2D shape_transf = generate_collision_shape_transform_for_area(curr_data_transf, shape, data.collision_shape_size, data.collision_shape_offset, i);

		// Generates texture transform with correct rotation and sets it to the correct bullet on the multimesh
		const Transform2D &texture_transf = generate_texture_transform(curr_data_transf, data.is_texture_rotation_permanent, cache_texture_rotation_radians, i);

		cache_texture_transforms[i] = texture_transf;

		// Cache bullet transforms and origin vectors
		all_cached_instance_transforms.emplace_back(texture_transf);
		all_cached_instance_origin.emplace_back(texture_transf.get_origin());

		all_cached_shape_transforms.emplace_back(shape_transf);
		all_cached_shape_origin.emplace_back(shape_transf.get_origin());

		// if (attachment_scenes.is_valid()) {
		// 	const Transform2D &attachment_transf = calculate_attachment_global_transf(texture_transf);

		// 	bool is_successful = reuse_attachment_from_object_pool(i, attachment_pool, attachment_transf, cache_attachment_id);

		// 	if (!is_successful) {
		// 		create_new_bullet_attachment(i, attachment_transf);
		// 	}
	}
}

// Used to retrieve a resource representing the bullets' data, so that it can be saved to a file.
Ref<SaveDataMultiMeshBullets2D> MultiMeshBullets2D::save(const Ref<SaveDataMultiMeshBullets2D> &empty_data) {
	SaveDataMultiMeshBullets2D &data_to_populate = *empty_data.ptr();

	// TEXTURE RELATED

	int64_t amount_textures = textures.size();
	for (int64_t i = 0; i < amount_textures; ++i) {
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

	for (int i = 0; i < amount_bullets; ++i) {
		data_to_populate.all_cached_instance_transforms.push_back(all_cached_instance_transforms[i]);
	}

	for (int i = 0; i < amount_bullets; ++i) {
		data_to_populate.all_cached_shape_transforms.push_back(all_cached_shape_transforms[i]);
	}

	for (int i = 0; i < amount_bullets; ++i) {
		data_to_populate.all_cached_instance_origin.push_back(all_cached_instance_origin[i]);
	}

	for (int i = 0; i < amount_bullets; ++i) {
		data_to_populate.all_cached_shape_origin.push_back(all_cached_shape_origin[i]);
	}

	int speed_data_size = all_cached_velocity.size();
	for (int i = 0; i < speed_data_size; ++i) {
		data_to_populate.all_cached_velocity.push_back(all_cached_velocity[i]);
	}

	for (int i = 0; i < speed_data_size; ++i) {
		data_to_populate.all_cached_direction.push_back(all_cached_direction[i]);
	}

	// BULLET SPEED RELATED

	for (int i = 0; i < speed_data_size; ++i) {
		data_to_populate.all_cached_speed.push_back(all_cached_speed[i]);
	}

	for (int i = 0; i < speed_data_size; ++i) {
		data_to_populate.all_cached_max_speed.push_back(all_cached_max_speed[i]);
	}

	for (int i = 0; i < speed_data_size; ++i) {
		data_to_populate.all_cached_acceleration.push_back(all_cached_acceleration[i]);
	}

	// BULLET ROTATION RELATED
	if (is_rotation_active) {
		for (int i = 0; i < all_rotation_speed.size(); ++i) {
			Ref<BulletRotationData2D> bullet_data = memnew(BulletRotationData2D);
			bullet_data->rotation_speed = all_rotation_speed[i];
			bullet_data->max_rotation_speed = all_max_rotation_speed[i];
			bullet_data->rotation_acceleration = all_rotation_acceleration[i];

			data_to_populate.all_bullet_rotation_data.push_back(bullet_data);
		}
		data_to_populate.rotate_only_textures = rotate_only_textures;
		data_to_populate.stop_rotation_when_max_reached = stop_rotation_when_max_reached;
	}

	// COLLISION RELATED

	data_to_populate.collision_layer = physics_server->area_get_collision_layer(area);
	data_to_populate.collision_mask = physics_server->area_get_collision_mask(area);
	data_to_populate.monitorable = monitorable;

	RID shape = physics_shapes[0];
	data_to_populate.collision_shape_size = (Vector2)(physics_server->shape_get_data(shape));

	data_to_populate.bullet_max_collision_count = bullet_max_collision_count;

	for (auto curr_collision_count : bullets_current_collision_count) {
		data_to_populate.bullets_current_collision_count.push_back(curr_collision_count);
	}

	// BULLET ATTACHMENTS RELATED
	// TODO saving bullet attachments needs refactor

	// data_to_populate.attachment_id = cache_attachment_id;
	// data_to_populate.attachment_scenes = attachment_scenes;
	// //data_to_populate.is_bullet_attachment_provided = is_bullet_attachment_provided; TODO remove this

	// data_to_populate.cache_stick_relative_to_bullet = cache_stick_relative_to_bullet;
	// data_to_populate.bullet_attachment_local_transform = bullet_attachment_local_transform;

	// TypedArray<Resource> &custom_data_to_save = data_to_populate.bullet_attachments_custom_data;
	// TypedArray<Transform2D> &transf_data_to_save = data_to_populate.attachment_transforms;

	// // Resize them since for every bullet we store attachment data
	// custom_data_to_save.resize(amount_bullets);
	// transf_data_to_save.resize(amount_bullets);

	// // Go through each bullet and in case it DOES have an attachment, set the correct data
	// for (int i = 0; i < amount_bullets; ++i) {
	// 	BulletAttachment2D *attachment = attachments[i];

	// 	if (attachment != nullptr) {
	// 		Ref<Resource> attachment_data = attachment->call_on_bullet_save();
	// 		const Transform2D &transf = attachment_transforms[i];

	// 		custom_data_to_save[i] = attachment_data;
	// 		transf_data_to_save[i] = transf;
	// 	}
	// }

	// OTHER

	data_to_populate.is_life_time_over_signal_enabled = is_life_time_over_signal_enabled;

	data_to_populate.is_life_time_infinite = is_life_time_infinite;

	data_to_populate.max_life_time = max_life_time;
	data_to_populate.current_life_time = current_life_time;
	data_to_populate.amount_bullets = amount_bullets;

	data_to_populate.material = get_material();

	const Array &keys = instance_shader_parameters.keys();
	for (int i = 0; i < keys.size(); ++i) {
		const String &key = keys[i];
		const Variant &value = get_instance_shader_parameter(key);

		// Save the current updated value of the instance shader parameter (the shader might've edited the value depending on the effect)
		data_to_populate.instance_shader_parameters[key] = value;
	}

	data_to_populate.mesh = multi->get_mesh();
	data_to_populate.bullets_custom_data = bullets_custom_data;

	// Save the enabled status so you can determine which bullets were active/disabled
	for (int i = 0; i < amount_bullets; ++i) {
		data_to_populate.bullets_enabled_status.push_back(static_cast<bool>(bullets_enabled_status[i]));
	}

	custom_additional_save_logic(data_to_populate);

	// TODO save cache_collision_shape_offset = data.collision_shape_offset;

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
			nullptr, // I am always saving an array of textures anyways/ no default texture to retrieve, so its fine passing a nullptr here in order to use the method logic
			data.current_texture_index,
			data.material,
			data.z_index,
			data.light_mask,
			data.visibility_layer,
			data.instance_shader_parameters);

	custom_additional_load_logic(data);

	bullets_container->add_child(this);

	if (active_bullets_counter > 0) {
		is_active = true;
	}

	// TODO load cache_collision_shape_offset = data.collision_shape_offset;
}

void MultiMeshBullets2D::load_bullet_instances(const SaveDataMultiMeshBullets2D &data) {
	int new_speed_data_size = data.all_cached_velocity.size();

	active_bullets_counter = amount_bullets;
	cache_texture_rotation_radians = data.cache_texture_rotation_radians;

	is_life_time_over_signal_enabled = data.is_life_time_over_signal_enabled;

	is_life_time_infinite = data.is_life_time_infinite;

	stop_rotation_when_max_reached = data.stop_rotation_when_max_reached;

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

	for (int i = 0; i < new_speed_data_size; ++i) {
		all_cached_speed.push_back(data.all_cached_speed[i]);
		all_cached_max_speed.push_back(data.all_cached_max_speed[i]);
		all_cached_acceleration.push_back(data.all_cached_acceleration[i]);
	}

	for (int i = 0; i < new_speed_data_size; ++i) {
		all_cached_velocity.push_back(data.all_cached_velocity[i]);
	}

	for (int i = 0; i < new_speed_data_size; ++i) {
		all_cached_direction.push_back(data.all_cached_direction[i]);
	}

	bullet_max_collision_count = data.bullet_max_collision_count;
	set_bullets_current_collision_count(data.bullets_current_collision_count); // this should always be valid, since im responsible for saving it after all..

	area = physics_server->area_create();

	set_up_area(data.collision_layer, data.collision_mask, data.monitorable, bullet_factory->physics_space);

	generate_physics_shapes_for_area(amount_bullets);

	// TODO ATTACHMENTS LOADING NEEDS REFACTOR

	//attachment_scenes = data.attachment_scenes;

	// Resize them since for every bullet we store attachment data
	attachments.resize(amount_bullets, nullptr);
	attachment_transforms.resize(amount_bullets);

	// if (attachment_scenes.is_valid()) {
	// 	cache_attachment_id = data.attachment_id;
	// 	cache_stick_relative_to_bullet = data.cache_stick_relative_to_bullet;
	// 	bullet_attachment_local_transform = data.bullet_attachment_local_transform;
	// }

	cache_texture_transforms.resize(amount_bullets); // TODO handle this properly -- add data to it

	for (int i = 0; i < amount_bullets; ++i) {
		all_cached_instance_transforms.push_back(data.all_cached_instance_transforms[i]);
		all_cached_instance_origin.push_back(data.all_cached_instance_origin[i]);
		all_cached_shape_transforms.push_back(data.all_cached_shape_transforms[i]);
		all_cached_shape_origin.push_back(data.all_cached_shape_origin[i]);

		bool is_bullet_enabled = static_cast<int8_t>(data.bullets_enabled_status[i]);
		bullets_enabled_status.push_back(is_bullet_enabled);

		RID shape_rid = physics_shapes[i];

		if (is_bullet_enabled) {
			physics_server->area_set_shape_disabled(area, i, false);
			multi->set_instance_transform_2d(i, all_cached_instance_transforms[i]);
		} else {
			physics_server->area_set_shape_disabled(area, i, true);
			active_bullets_counter--;

			multi->set_instance_transform_2d(i, zero_transform);
		}

		physics_server->area_set_shape_transform(area, i, all_cached_shape_transforms[i]);
		physics_server->shape_set_data(shape_rid, data.collision_shape_size);

		// // In case the bullet atatchment scene is valid provide attachment data
		// if (attachment_scenes.is_valid()) {
		// 	// If the bullet is disabled then that means it didnt have an attachment before being saved, so skip it and mark it as nullptr
		// 	// TODO this logic should be updated
		// 	if (!is_bullet_enabled) {
		// 		continue;
		// 	}

		// 	BulletAttachment2D *attachment = static_cast<BulletAttachment2D *>(attachment_scenes->instantiate());
		// 	attachment->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);

		// 	attachment->attachment_id = cache_attachment_id;
		// 	attachment->stick_relative_to_bullet = cache_stick_relative_to_bullet;

		// 	const Transform2D &attachment_transf = data.attachment_transforms[count_attachments];

		// 	attachment->set_global_transform(attachment_transf);
		// 	attachment_transforms[i] = attachment_transf;

		// 	const Ref<Resource> attachment_data = data.bullet_attachments_custom_data[count_attachments];

		// 	if (attachment_data.is_valid()) // If it's not nullptr, load it
		// 	{
		// 		attachment->call_on_bullet_load(attachment_data);
		// 	}

		// 	bullet_factory->bullet_attachments_container->add_child(attachment);
		// 	attachments[i] = attachment;

		// 	count_attachments++;
		// }
	}

	// LOAD ROTATION DATA
	set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures);

	update_all_previous_transforms_for_interpolation();
}

void MultiMeshBullets2D::spawn_as_disabled_multimesh(int new_amount_bullets, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container) {
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
	physics_server->shape_set_data(shape, Vector2(5, 5));
	//

	// Just add it to the bullets container
	bullets_container->add_child(this);
}

void MultiMeshBullets2D::generate_multimesh() {
	multi = memnew(MultiMesh);
	set_multimesh(multi);
}

void MultiMeshBullets2D::set_up_multimesh(int new_instance_count, const Ref<Mesh> &new_mesh, Vector2 new_texture_size) {
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

void MultiMeshBullets2D::set_up_life_time_timer(double new_max_life_time, double new_current_life_time) {
	max_life_time = new_max_life_time;
	current_life_time = new_current_life_time;
}

void MultiMeshBullets2D::set_up_change_texture_timer(int64_t new_amount_textures, double new_default_change_texture_time, const TypedArray<double> &new_change_texture_times) {
	if (new_amount_textures > 1) { // the change texture timer will be active only if more than 1 texture was provided
		change_texture_times.clear();

		int64_t amount_change_texture_times = new_change_texture_times.size();

		// The change texture times will only be used if their amount is the same as the amount of textures currently present. Each time value corresponds to a texture
		if (amount_change_texture_times > 0 && amount_change_texture_times == new_amount_textures) {
			change_texture_times = new_change_texture_times.duplicate();

			current_change_texture_time = change_texture_times[0];
		} else {
			change_texture_times.append(new_default_change_texture_time);

			current_change_texture_time = new_default_change_texture_time;
		}
	}
}

// Always called last
void MultiMeshBullets2D::finalize_set_up(
		const Ref<Resource> &new_bullets_custom_data,
		const TypedArray<Texture2D> &new_textures,
		const Ref<Texture2D> &new_default_texture,
		int new_current_texture_index,
		const Ref<Material> &new_material,
		int new_z_index,
		int new_light_mask,
		int new_visibility_layer,
		const Dictionary &new_instance_shader_parameters) {
	// Bullets custom data
	if (new_bullets_custom_data.is_valid()) {
		bullets_custom_data = new_bullets_custom_data;
	}

	textures.clear(); // Clear old textures data if any
	// Texture logic
	if (new_textures.size() > 0) {
		textures = new_textures.duplicate();

		// Make sure the current_texture_index is valid
		if (new_current_texture_index >= textures.size() || new_current_texture_index < 0) {
			new_current_texture_index = 0;
		}
		current_texture_index = new_current_texture_index;

		set_texture(textures[current_texture_index]);
	} else if (new_default_texture != nullptr) {
		textures.append(new_default_texture);
		current_texture_index = 0;

		set_texture(textures[current_texture_index]);
	} else {
		UtilityFunctions::print("Error. You have not provided any textures as data. Ensure that you either provide an array of textures or a default texture that can be used");
	}

	if (new_material.is_valid()) {
		godot::Ref<ShaderMaterial> shader_material = new_material;
		// If a shader material was passed and the user has provided instance shader parameters
		if (shader_material.is_valid() && new_instance_shader_parameters.is_empty() == false) {
			instance_shader_parameters = new_instance_shader_parameters;

			const Array &keys = new_instance_shader_parameters.keys();
			for (int i = 0; i < keys.size(); ++i) {
				const String &key = keys[i];
				const Variant &value = new_instance_shader_parameters[key];

				set_instance_shader_parameter(key, value);
			}
		}

		set_material(new_material);
	} else {
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

void MultiMeshBullets2D::set_rotation_data(const TypedArray<BulletRotationData2D> &rotation_data, bool new_rotate_only_textures) {
	int amount_rotation_data = rotation_data.size();

	// If the amount of rotation data is:
	// 0 -> rotation is disabled
	// Same as the amount of bullets -> rotation is enabled and each provided rotation data will be used for the corresponding bullet
	// Otherwise -> rotation is enabled, but only the first data is used
	if (amount_rotation_data == 0) {
		is_rotation_active = false;
		return;
	}

	is_rotation_active = true;

	if (amount_rotation_data == amount_bullets) {
		use_only_first_rotation_data = false;
	} else {
		use_only_first_rotation_data = true;
	}

	rotate_only_textures = new_rotate_only_textures;

	// Clear existing data (avoids freeing the actual memory, instead only the .amount_bullets is changed which allows me to push brand new elements as if the vector is empty/ overwrite existing but not accessible ones)
	all_rotation_speed.clear();
	all_max_rotation_speed.clear();
	all_rotation_acceleration.clear();

	// Reserve more space if needed
	if (amount_rotation_data > all_rotation_speed.capacity()) {
		all_rotation_speed.reserve(amount_rotation_data);
		all_max_rotation_speed.reserve(amount_rotation_data);
		all_rotation_acceleration.reserve(amount_rotation_data);
	}

	// Add newest data
	for (int i = 0; i < amount_rotation_data; ++i) {
		BulletRotationData2D &curr_bullet_data = *Object::cast_to<BulletRotationData2D>(rotation_data[i]);

		all_rotation_speed.emplace_back(curr_bullet_data.rotation_speed);
		all_max_rotation_speed.emplace_back(curr_bullet_data.max_rotation_speed);
		all_rotation_acceleration.emplace_back(curr_bullet_data.rotation_acceleration);
	}
}

Transform2D MultiMeshBullets2D::generate_texture_transform(Transform2D transf, bool is_texture_rotation_permanent, real_t texture_rotation_radians, int bullet_index) {
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

void MultiMeshBullets2D::set_up_area(const int collision_layer, const int collision_mask, bool new_monitorable, const RID &physics_space) {
	monitorable = new_monitorable;
	physics_server->area_set_space(area, bullet_factory->physics_space);
	physics_server->area_set_monitorable(area, monitorable);
	physics_server->area_set_area_monitor_callback(area, callable_mp(this, &MultiMeshBullets2D::area_entered_func));
	physics_server->area_set_monitor_callback(area, callable_mp(this, &MultiMeshBullets2D::body_entered_func));
	physics_server->area_set_collision_layer(area, collision_layer);
	physics_server->area_set_collision_mask(area, collision_mask);
}

Transform2D MultiMeshBullets2D::generate_collision_shape_transform_for_area(Transform2D transf, const RID &shape, const Vector2 &collision_shape_size, const Vector2 &collision_shape_offset, int bullet_index) {
	// The rotation of each transform
	real_t curr_bullet_rotation = transf.get_rotation();

	// Rotate collision_shape_offset based on the direction of the bullets
	Vector2 rotated_offset(
			collision_shape_offset.x * Math::cos(curr_bullet_rotation) - collision_shape_offset.y * Math::sin(curr_bullet_rotation),
			collision_shape_offset.x * Math::sin(curr_bullet_rotation) + collision_shape_offset.y * Math::cos(curr_bullet_rotation));

	transf.set_origin(transf.get_origin() + rotated_offset);

	physics_server->area_set_shape_transform(area, bullet_index, transf);
	physics_server->shape_set_data(shape, collision_shape_size / 2); // SHAPE_RECTANGLE: a Vector2 half_extents  (I'm dividing by 2 to get the actual size the user wants for the rectangle, the function wants half_extents, if it gets the size 32 for the x, that means only the half width is 32 so the other half will also be 32 meaning 64 total width if I give the user's 32 that he said, to avoid that im deviding by 2 so the size gets set correctly to 32)

	return transf;
}

void MultiMeshBullets2D::generate_physics_shapes_for_area(int amount) {
	physics_shapes.reserve(amount);
	for (int i = 0; i < amount; ++i) {
		RID shape = physics_server->rectangle_shape_create();
		physics_server->area_add_shape(area, shape);
		physics_shapes.emplace_back(shape);
	}
}

void MultiMeshBullets2D::set_all_physics_shapes_enabled_for_area(bool enable) {
	for (int i = 0; i < amount_bullets; ++i) {
		physics_server->area_set_shape_disabled(area, i, !enable);
	}
}

void MultiMeshBullets2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_textures"), &MultiMeshBullets2D::get_textures);
	ClassDB::bind_method(D_METHOD("set_textures", "new_textures", "new_change_texture_times", "selected_texture_index"), &MultiMeshBullets2D::set_textures, DEFVAL(0));

	ClassDB::bind_method(D_METHOD("disable_bullet", "bullet_index", "disable_bullet_attachment"), &MultiMeshBullets2D::disable_bullet, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("enable_bullet", "bullet_index", "collision_amount", "enable_attachment"), &MultiMeshBullets2D::enable_bullet, DEFVAL(0), DEFVAL(true));

	ClassDB::bind_method(D_METHOD("bullet_free_attachment", "bullet_index"), &MultiMeshBullets2D::bullet_free_attachment);
	ClassDB::bind_method(D_METHOD("bullet_disable_attachment", "bullet_index"), &MultiMeshBullets2D::bullet_disable_attachment);
	ClassDB::bind_method(D_METHOD("bullet_enable_attachment", "bullet_index"), &MultiMeshBullets2D::bullet_enable_attachment);

	ClassDB::bind_method(D_METHOD("get_amount_bullets"), &MultiMeshBullets2D::get_amount_bullets);

	ClassDB::bind_method(D_METHOD("get_all_bullets_status"), &MultiMeshBullets2D::get_all_bullets_status);
	ClassDB::bind_method(D_METHOD("is_bullet_status_enabled", "bullet_index"), &MultiMeshBullets2D::is_bullet_status_enabled);

	ClassDB::bind_method(D_METHOD("get_bullets_custom_data"), &MultiMeshBullets2D::get_bullets_custom_data);
	ClassDB::bind_method(D_METHOD("set_bullets_custom_data", "new_custom_data"), &MultiMeshBullets2D::set_bullets_custom_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bullets_custom_data"), "set_bullets_custom_data", "get_bullets_custom_data");

	ClassDB::bind_method(D_METHOD("get_is_life_time_infinite"), &MultiMeshBullets2D::get_is_life_time_infinite);
	ClassDB::bind_method(D_METHOD("set_is_life_time_infinite", "value"), &MultiMeshBullets2D::set_is_life_time_infinite);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_life_time_infinite"), "set_is_life_time_infinite", "get_is_life_time_infinite");

	// Time based functions
	ClassDB::bind_method(D_METHOD("multimesh_attach_time_based_function", "time", "callable", "repeat", "execute_only_if_multimesh_is_active"), &MultiMeshBullets2D::multimesh_attach_time_based_function);
	ClassDB::bind_method(D_METHOD("_do_attach_time_based_function", "time", "callable", "repeat", "execute_only_if_multimesh_is_active"), &MultiMeshBullets2D::_do_attach_time_based_function);

	ClassDB::bind_method(D_METHOD("multimesh_detach_time_based_function", "callable"), &MultiMeshBullets2D::multimesh_detach_time_based_function);
	ClassDB::bind_method(D_METHOD("_do_detach_time_based_function", "callable"), &MultiMeshBullets2D::_do_detach_time_based_function);

	ClassDB::bind_method(D_METHOD("multimesh_detach_all_time_based_functions"), &MultiMeshBullets2D::multimesh_detach_all_time_based_functions);
	ClassDB::bind_method(D_METHOD("_do_detach_all_time_based_functions"), &MultiMeshBullets2D::_do_detach_all_time_based_functions);

	ClassDB::bind_method(D_METHOD("_do_execute_stored_callable_safely", "_callback", "_execute_only_if_multimesh_is_active"), &MultiMeshBullets2D::_do_execute_stored_callable_safely);

	ClassDB::bind_method(D_METHOD("get_is_multimesh_auto_pooling_enabled"), &MultiMeshBullets2D::get_is_multimesh_auto_pooling_enabled);
	ClassDB::bind_method(D_METHOD("set_is_multimesh_auto_pooling_enabled", "value"), &MultiMeshBullets2D::set_is_multimesh_auto_pooling_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_multimesh_auto_pooling_enabled"), "set_is_multimesh_auto_pooling_enabled", "get_is_multimesh_auto_pooling_enabled");

	ClassDB::bind_method(D_METHOD("get_is_attachments_auto_pooling_enabled"), &MultiMeshBullets2D::get_is_attachments_auto_pooling_enabled);
	ClassDB::bind_method(D_METHOD("set_is_attachments_auto_pooling_enabled", "value"), &MultiMeshBullets2D::set_is_attachments_auto_pooling_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_attachments_auto_pooling_enabled"), "set_is_attachments_auto_pooling_enabled", "get_is_attachments_auto_pooling_enabled");

	// Collision
	ClassDB::bind_method(D_METHOD("get_bullet_max_collision_count"), &MultiMeshBullets2D::get_bullet_max_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullet_max_collision_count", "value"), &MultiMeshBullets2D::set_bullet_max_collision_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bullet_max_collision_count"), "set_bullet_max_collision_count", "get_bullet_max_collision_count");

	ClassDB::bind_method(D_METHOD("get_bullets_current_collision_count"), &MultiMeshBullets2D::get_bullets_current_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullets_current_collision_count", "arr"), &MultiMeshBullets2D::set_bullets_current_collision_count);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bullets_current_collision_count"), "set_bullets_current_collision_count", "get_bullets_current_collision_count");

	ClassDB::bind_method(D_METHOD("_handle_bullet_collision", "factory_signal_name_to_emit", "bullet_index", "entered_instance_id"), &MultiMeshBullets2D::_handle_bullet_collision);

	ClassDB::bind_method(D_METHOD("bullet_get_attachment", "bullet_index"), &MultiMeshBullets2D::bullet_get_attachment);
	ClassDB::bind_method(D_METHOD("bullet_set_attachment_to_null", "bullet_index"), &MultiMeshBullets2D::bullet_set_attachment_to_null);

	ClassDB::bind_method(D_METHOD("all_bullets_get_attachments", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_get_attachments, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_attachment_to_null", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_attachment_to_null, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_attachment", "attachment_scene", "attachment_pooling_id", "bullet_attachment_offset", "stick_relative_to_bullet", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_attachment, DEFVAL(true), DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("bullet_set_attachment", "bullet_index", "attachment_scene", "attachment_pooling_id", "bullet_attachment_offset", "stick_relative_to_bullet"), &MultiMeshBullets2D::bullet_set_attachment, DEFVAL(true));

	ClassDB::bind_method(D_METHOD("set_bullet_curves_data", "data"), &MultiMeshBullets2D::set_bullet_curves_data);
	ClassDB::bind_method(D_METHOD("get_bullet_curves_data"), &MultiMeshBullets2D::get_bullet_curves_data);
	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "bullet_curves_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletCurvesData2D"),
			"set_bullet_curves_data", "get_bullet_curves_data");
}
} //namespace BlastBullets2D
