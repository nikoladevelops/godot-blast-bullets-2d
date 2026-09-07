#include "./multimesh_bullets2d.hpp"
#include "../factory/bullet_factory2d.hpp"
#include "../shared/multimesh_object_pool2d.hpp"

#include "godot_cpp/classes/curve.hpp"
#include "godot_cpp/classes/curve2d.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/print_string.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "multimesh_bullets2d.hpp"
#include "shared/bullet_curves_data2d.hpp"
#include "shared/bullet_movement_pattern_data2d.hpp"
#include "shared/collision_shape_helper2d.hpp"
#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

MultiMeshBullets2D::~MultiMeshBullets2D() {
	// This runs AFTER NOTIFICATION_PREDELETE.
	// only for raw memory cleanup that doesn't
	// need to talk to Godot's servers.

	// Also never forget this
	// if (Engine::get_singleton()->is_editor_hint()) {
	//		return;
	// }
}

void MultiMeshBullets2D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PREDELETE: {
			// The destructor also runs for editor-time instances, which have no runtime state.
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}

			if (!marked_for_internal_deletion && bullet_factory) {
				bullet_factory->handle_manual_user_deletion_of_multimesh_bullets(*this);
			}

			clear_homing_state_for_teardown();

			if (physics_server && area.is_valid()) {
				// Disable the area's shapes (ALL OF THEM no matter their bullets_enabled_status).
				// Bounds-checked: never let a desynced attachments vector take down PREDELETE.
				// When the factory itself is tearing down, only run the script callback and
				// drop the slot: re-pooling into (or queue_freeing from) a dying factory is
				// pointless, the engine destroys the whole subtree anyway.
				const bool factory_is_dying = bullet_factory == nullptr || bullet_factory->get_is_tearing_down();
				for (int i = 0; i < amount_bullets && i < (int)physics_shapes.size(); ++i) {
					physics_server->area_set_shape_disabled(area, i, true);

					if (i >= 0 && i < (int)attachments.size() && attachments[i] != nullptr) {
						if (factory_is_dying) {
							attachments[i]->call_on_bullet_disable();
							attachments[i] = nullptr;
						} else {
							bullet_disable_attachment(i);
						}
					}
				}

				physics_server->area_set_area_monitor_callback(area, Variant());
				physics_server->area_set_monitor_callback(area, Variant());

				// Avoid memory leaks if you've used the PhysicsServer2D to generate area and shapes
				for (auto &shape : physics_shapes) {
					if (shape.is_valid()) {
						physics_server->free_rid(shape);
					}
				}
				physics_shapes.clear();

				if (area.is_valid()) {
					physics_server->free_rid(area);
				}
				area = RID();
			}
		} break;
	}
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
void MultiMeshBullets2D::spawn(const MultiMeshBulletsData2D &data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container, const Vector2 &new_inherited_velocity_offset, int new_sparse_set_id, bool spawn_in_pool) {
	this->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF); // We have custom physics interpolation logic, so disable the Godot one that comes from Godot 4.5

	sparse_set_id = new_sparse_set_id;
	inherited_velocity_offset = new_inherited_velocity_offset;

	bullets_pool = pool;
	bullet_factory = factory;
	physics_server = PhysicsServer2D::get_singleton();

	amount_bullets = data.transforms.size(); // important, because some set_up methods use this
	cache_collision_shape_typed(data.collision_shape);

	++multimesh_generation;

	all_bullets_enabled_set.resize(amount_bullets);
	all_bullet_curves_data.assign(amount_bullets, Ref<BulletCurvesData2D>());
	all_movement_pattern_data.assign(amount_bullets, BulletMovementPatternData2D());
	batch_buffer.resize(amount_bullets * 8);

	set_up_life_time_timer(data.max_life_time, data.max_life_time);

	generate_multimesh();
	set_up_multimesh(amount_bullets, data.mesh, resolve_quad_size(data.sprite_frames, data.animation, data.texture_size));

	area = physics_server->area_create();
	generate_physics_shapes_for_area(amount_bullets);

	set_up_bullet_instances(data);

	// Set up bullet attachments so that for every bullet you will be able to have an attachment if needed

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
			data.material,
			data.z_index,
			data.light_mask,
			data.visibility_layer,
			data.instance_shader_parameters);

	// Single-error policy: rebuild_sprite_animation already reported the cause;
	// no wrapper error here. Failure leaves previous texture/cache untouched.
	rebuild_sprite_animation(data.sprite_frames, data.animation);

	custom_additional_spawn_logic(data);

	set_process(false);
	set_physics_process(false);

	if (spawn_in_pool) {
		set_visible(false);
		is_active = false;
		// Pooled instances hold zero enabled bullets: reset the counter that
		// set_up_bullet_instances set to amount_bullets so counter==0 matches
		// the empty enabled set (enable_bullet wake counts up from here).
		active_bullets_counter = 0;
		set_all_physics_shapes_enabled_for_area(false);
		bullets_container->add_child(this);
		bullets_pool->push(this, get_pool_key());
	} else {
		all_bullets_enabled_set.activate_all_data();
		is_active = true;
		bullets_container->add_child(this);
	}
}

// Activates the multimesh
void MultiMeshBullets2D::enable_multimesh(const MultiMeshBulletsData2D &data, const Vector2 &new_inherited_velocity_offset) {
	inherited_velocity_offset = new_inherited_velocity_offset;
	cache_collision_shape_typed(data.collision_shape);

	++multimesh_generation;

	set_up_life_time_timer(data.max_life_time, data.max_life_time);

	set_up_multimesh(amount_bullets, data.mesh, resolve_quad_size(data.sprite_frames, data.animation, data.texture_size));

	set_up_bullet_instances(data);
	set_all_physics_shapes_enabled_for_area(true);

	set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures);

	move_to_front(); // Pooled instances render behind newer ones without this; moving to front emulates fresh spawn order.

	update_all_previous_transforms_for_interpolation();

	finalize_set_up(
			data.bullets_custom_data,
			data.material,
			data.z_index,
			data.light_mask,
			data.visibility_layer,
			data.instance_shader_parameters);

	// Single-error policy: rebuild already reported; previous texture kept on failure.
	rebuild_sprite_animation(data.sprite_frames, data.animation);

	// Pooled instances may carry user connections from a previous owner; they must not
	// fire for the new spawn. Mirrors the homing-signal cleanup in directional enable logic.
	for (const Dictionary &connection : get_signal_connection_list("sprite_animation_finished")) {
		const Callable callable = connection["callable"];
		disconnect("sprite_animation_finished", callable);
	}

	custom_additional_enable_logic(data);

	set_visible(true);

	// Mark all bullets as enabled in the sparse set (amount_bullets never changes)
	all_bullets_enabled_set.activate_all_data();
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

	set_up_area(data.collision_layer, data.collision_mask, data.monitorable, bullet_factory->physics_space);

	stop_rotation_when_max_reached = data.stop_rotation_when_max_reached;

	cache_collision_shape_offset = data.collision_shape_offset;

	if (all_cached_instance_transforms.size() != 0) {
		// Enabling a pooled multimesh: drop old frame data. Capacity stays put and the
		// pool always reuses the original amount_bullets, so no reallocation happens here.
		all_cached_instance_transforms.clear();
		all_cached_instance_origin.clear();
		all_cached_shape_transforms.clear();
		all_cached_shape_origin.clear();
	} else {
		// First spawn: reserve everything up front for the fixed bullet count.
		all_cached_instance_transforms.reserve(amount_bullets);
		all_cached_instance_origin.reserve(amount_bullets);
		all_cached_shape_transforms.reserve(amount_bullets);
		all_cached_shape_origin.reserve(amount_bullets);
	}

	cache_texture_rotation_radians = data.texture_rotation_radians;
	cache_texture_transforms.resize(amount_bullets);

	for (int i = 0; i < amount_bullets; ++i) {
		RID shape = physics_shapes[i];

		const Transform2D &curr_data_transf = data.transforms[i];

		// Generates a collision shape transform for a particular bullet and attaches it to the area
		Transform2D shape_transf = generate_collision_shape_transform_for_area(curr_data_transf, shape, data.collision_shape_offset, i);

		// Generates texture transform with correct rotation and sets it to the correct bullet on the multimesh
		const Transform2D &texture_transf = generate_texture_transform(curr_data_transf, data.is_texture_rotation_permanent, cache_texture_rotation_radians, i);

		cache_texture_transforms[i] = texture_transf;

		// Cache bullet transforms and origin vectors
		all_cached_instance_transforms.emplace_back(texture_transf);
		all_cached_instance_origin.emplace_back(texture_transf.get_origin());

		all_cached_shape_transforms.emplace_back(shape_transf);
		all_cached_shape_origin.emplace_back(shape_transf.get_origin());
	}
}

void MultiMeshBullets2D::generate_multimesh() {
	Ref<MultiMesh> new_multi;
	new_multi.instantiate();
	new_multi->set_transform_format(MultiMesh::TRANSFORM_2D);

	multi = new_multi;
	set_multimesh(multi);
}

void MultiMeshBullets2D::set_up_multimesh(int new_instance_count, const Ref<Mesh> &new_mesh, Vector2 new_texture_size) {
	if (new_mesh.is_valid()) {
		multi->set_mesh(new_mesh);
	} else {
		Ref<QuadMesh> mesh = memnew(QuadMesh);
		mesh->set_size(new_texture_size);
		multi->set_mesh(mesh);
	}
	// Always track the resolved size, even with a custom mesh, so pooled reuse
	// with different data cannot inherit a stale quad size.
	texture_size = new_texture_size;

	multi->set_instance_count(new_instance_count);
	batch_buffer.resize(new_instance_count * 8);
}

void MultiMeshBullets2D::set_up_life_time_timer(double new_max_life_time, double new_current_life_time) {
	max_life_time = new_max_life_time;
	current_life_time = new_current_life_time;
}

static bool resolve_sprite_animation_impl(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_requested, StringName &out_anim, bool silent) {
	if (p_sprite_frames.is_null()) {
		if (!silent) {
			UtilityFunctions::push_error("MultiMeshBullets2D: sprite_frames is null. Assign a SpriteFrames resource.");
		}
		return false;
	}
	const PackedStringArray names = p_sprite_frames->get_animation_names();
	if (names.is_empty()) {
		if (!silent) {
			UtilityFunctions::push_error("MultiMeshBullets2D: sprite_frames has no animations.");
		}
		return false;
	}
	const String requested_str = String(p_requested);
	const bool is_auto = requested_str.is_empty() || p_requested == StringName("default");
	// Picks the first animation that actually has frames. An empty "default" (fresh
	// SpriteFrames resources always contain one) must not shadow a populated animation.
	auto first_with_frames = [&]() -> StringName {
		for (int i = 0; i < names.size(); ++i) {
			if (p_sprite_frames->get_frame_count(names[i]) > 0) {
				return names[i];
			}
		}
		return StringName();
	};
	if (is_auto) {
		// Unselected animation: play "default" silently when usable, else first animation
		// with frames, silently.
		if (p_sprite_frames->has_animation(StringName("default")) && p_sprite_frames->get_frame_count(StringName("default")) > 0) {
			out_anim = StringName("default");
			return true;
		}
		const StringName fallback = first_with_frames();
		if (String(fallback).is_empty()) {
			if (!silent) {
				UtilityFunctions::push_error("MultiMeshBullets2D: sprite_frames has no animation with frames.");
			}
			return false;
		}
		out_anim = fallback;
		return true;
	}
	if (p_sprite_frames->has_animation(p_requested) && p_sprite_frames->get_frame_count(p_requested) > 0) {
		out_anim = p_requested;
		return true;
	}
	const StringName fallback = first_with_frames();
	if (String(fallback).is_empty()) {
		if (!silent) {
			UtilityFunctions::push_error("MultiMeshBullets2D: sprite_frames has no animation with frames.");
		}
		return false;
	}
	if (!silent) {
		UtilityFunctions::push_error("MultiMeshBullets2D: missing animation '" + requested_str + "', falling back to '" + String(fallback) + "'.");
	}
	out_anim = fallback;
	return true;
}

bool MultiMeshBullets2D::resolve_sprite_animation(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_requested, StringName &out_anim) {
	return resolve_sprite_animation_impl(p_sprite_frames, p_requested, out_anim, false);
}

bool MultiMeshBullets2D::resolve_sprite_animation_quiet(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_requested, StringName &out_anim) {
	return resolve_sprite_animation_impl(p_sprite_frames, p_requested, out_anim, true);
}

bool MultiMeshBullets2D::rebuild_sprite_animation(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_animation) {
	StringName anim;
	if (!resolve_sprite_animation(p_sprite_frames, p_animation, anim)) {
		return false; // error already reported, previous animation untouched
	}
	const int count = p_sprite_frames->get_frame_count(anim);
	if (count <= 0) {
		UtilityFunctions::push_error("MultiMeshBullets2D: animation '" + String(anim) + "' has no frames.");
		return false;
	}
	double fps = p_sprite_frames->get_animation_speed(anim);
	if (fps <= 0.0) {
		UtilityFunctions::push_error("MultiMeshBullets2D: animation '" + String(anim) + "' has invalid speed, using 1 fps.");
		fps = 1.0;
	}
	std::vector<Ref<Texture2D>> frames;
	std::vector<double> secs;
	frames.reserve(count);
	secs.reserve(count);
	for (int i = 0; i < count; ++i) {
		Ref<Texture2D> tex = p_sprite_frames->get_frame_texture(anim, i);
		if (tex.is_null()) {
			UtilityFunctions::push_error("MultiMeshBullets2D: animation '" + String(anim) + "' frame " + String::num_int64(i) + " has null texture.");
			return false; // previous cache untouched (swap only on success below)
		}
		const float dur = p_sprite_frames->get_frame_duration(anim, i);
		frames.push_back(tex);
		secs.push_back((dur <= 0.0f ? 0.0 : (double)dur / fps));
	}
	anim_source = p_sprite_frames;
	anim_name = anim;
	anim_frames.swap(frames);
	anim_frame_secs.swap(secs);
	anim_loop = p_sprite_frames->get_animation_loop(anim);
	anim_paused = false;
	anim_finished = false;
	anim_frame_index = 0;
	anim_frame_time_left = anim_frame_secs[0];
	set_texture(anim_frames[0]);
	return true;
}

bool MultiMeshBullets2D::play_sprite_animation(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_animation) {
	if (p_sprite_frames.is_null()) {
		UtilityFunctions::push_error("MultiMeshBullets2D play_sprite_animation: sprite_frames is null.");
		return false;
	}
	if (p_animation == StringName() || String(p_animation).is_empty()) {
		UtilityFunctions::push_error("MultiMeshBullets2D play_sprite_animation: animation is empty.");
		return false;
	}
	return rebuild_sprite_animation(p_sprite_frames, p_animation);
}

bool MultiMeshBullets2D::play_sprite_animation_name(const StringName &p_animation) {
	if (anim_source.is_null()) {
		UtilityFunctions::push_error("MultiMeshBullets2D play_sprite_animation_name: no SpriteFrames cached yet, call play_sprite_animation first.");
		return false;
	}
	if (p_animation == StringName() || String(p_animation).is_empty()) {
		UtilityFunctions::push_error("MultiMeshBullets2D play_sprite_animation_name: animation is empty.");
		return false;
	}
	return rebuild_sprite_animation(anim_source, p_animation);
}

bool MultiMeshBullets2D::restart_sprite_animation() {
	if (anim_frames.empty()) {
		UtilityFunctions::push_error("MultiMeshBullets2D restart_sprite_animation: no baked animation to restart.");
		return false;
	}
	anim_frame_index = 0;
	anim_frame_time_left = anim_frame_secs[0];
	anim_paused = false;
	anim_finished = false;
	set_texture(anim_frames[0]);
	return true;
}

Vector2 MultiMeshBullets2D::resolve_quad_size(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_animation, Vector2 override_size) {
	if (override_size.x > 0.0f && override_size.y > 0.0f) {
		return override_size;
	}
	// Silent fallback: rebuild_sprite_animation owns all error reporting (spawn calls
	// both, so resolving loudly here would print every failure twice).
	StringName anim;
	if (!resolve_sprite_animation_quiet(p_sprite_frames, p_animation, anim)) {
		return Vector2(32, 32);
	}	if (p_sprite_frames->get_frame_count(anim) > 0) {
		if (const Ref<Texture2D> tex = p_sprite_frames->get_frame_texture(anim, 0); tex.is_valid()) {
			if (const Ref<AtlasTexture> atlas = tex; atlas.is_valid()) {
				const Vector2 region = atlas->get_region().size;
				if (region.x > 0.0f && region.y > 0.0f) {
					return region;
				}
			}
			const Vector2 size = tex->get_size();
			if (size.x > 0.0f && size.y > 0.0f) {
				return size;
			}
		}
	}
	return Vector2(32, 32);
}

// Always called last (texture comes from rebuild_sprite_animation, called by spawn/enable)
void MultiMeshBullets2D::finalize_set_up(
		const Ref<Resource> &new_bullets_custom_data,
		const Ref<Material> &new_material,
		int new_z_index,
		int new_light_mask,
		int new_visibility_layer,
		const Dictionary &new_instance_shader_parameters) {
	// Bullets custom data. Always assigned (null clears) so pool reuse never leaks
	// the previous owner's data into a new spawn.
	bullets_custom_data = new_bullets_custom_data;

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
		} else {
			// Shader without params (or non-shader material handled below): drop the
			// previous owner's dict so pool reuse can't leak stale entries into it.
			instance_shader_parameters.clear();
		}

		set_material(new_material);
	} else {
		instance_shader_parameters.clear();
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
		is_rotation_data_active = false;
		return;
	}

	is_rotation_data_active = true;

	if (amount_rotation_data == amount_bullets) {
		use_only_first_rotation_data = false;
	} else {
		use_only_first_rotation_data = true;
	}

	// Validate every element we are about to read. A null or wrong-typed entry would
	// crash on dereference below, so fail open to no-rotation instead.
	// Non-finite values would poison the tick path (INF rotation never heals and
	// NaNs the transform), so they fail open the same way.
	const int validate_count = use_only_first_rotation_data ? 1 : amount_rotation_data;
	for (int i = 0; i < validate_count; ++i) {
		BulletRotationData2D *entry = Object::cast_to<BulletRotationData2D>(rotation_data[i]);
		if (entry == nullptr) {
			UtilityFunctions::push_error("Invalid rotation data at index " + String::num_int64(i) + ": expected BulletRotationData2D. Ignoring all rotation data.");
			is_rotation_data_active = false;
			return;
		}
		if (!Math::is_finite(entry->rotation_speed) || !Math::is_finite(entry->max_rotation_speed) || !Math::is_finite(entry->rotation_acceleration)) {
			UtilityFunctions::push_error("Non-finite rotation data at index " + String::num_int64(i) + ": rotation values must be finite. Ignoring all rotation data.");
			is_rotation_data_active = false;
			return;
		}
	}

	rotate_only_textures = new_rotate_only_textures;

	// Clear existing data (avoids freeing the actual memory, instead only the .amount_bullets is changed which allows me to push brand new elements as if the vector is empty/ overwrite existing but not accessible ones)
	all_rotation_speed.clear();
	all_max_rotation_speed.clear();
	all_rotation_acceleration.clear();

	if (use_only_first_rotation_data) {
		// Single rotation data provided but amount_bullets is N -> expand to N identical entries to avoid OOB when consumers index by bullet_index
		BulletRotationData2D &single_data = *Object::cast_to<BulletRotationData2D>(rotation_data[0]);
		if (amount_bullets > (int)all_rotation_speed.capacity()) {
			all_rotation_speed.reserve(amount_bullets);
			all_max_rotation_speed.reserve(amount_bullets);
			all_rotation_acceleration.reserve(amount_bullets);
		}
		for (int i = 0; i < amount_bullets; ++i) {
			all_rotation_speed.emplace_back(single_data.rotation_speed);
			all_max_rotation_speed.emplace_back(single_data.max_rotation_speed);
			all_rotation_acceleration.emplace_back(single_data.rotation_acceleration);
		}
	} else {
		// Per-bullet data: size must equal amount_bullets
		if (amount_rotation_data > (int)all_rotation_speed.capacity()) {
			all_rotation_speed.reserve(amount_rotation_data);
			all_max_rotation_speed.reserve(amount_rotation_data);
			all_rotation_acceleration.reserve(amount_rotation_data);
		}
		for (int i = 0; i < amount_rotation_data; ++i) {
			BulletRotationData2D &curr_bullet_data = *Object::cast_to<BulletRotationData2D>(rotation_data[i]);

			all_rotation_speed.emplace_back(curr_bullet_data.rotation_speed);
			all_max_rotation_speed.emplace_back(curr_bullet_data.max_rotation_speed);
			all_rotation_acceleration.emplace_back(curr_bullet_data.rotation_acceleration);
		}
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

Transform2D MultiMeshBullets2D::generate_collision_shape_transform_for_area(Transform2D transf, const RID &shape, const Vector2 &collision_shape_offset, int bullet_index) {
	// The rotation of each transform
	real_t curr_bullet_rotation = transf.get_rotation();

	// Rotate collision_shape_offset based on the direction of the bullets (single cos/sin) - early out if zero (common case)
	Vector2 rotated_offset = Vector2(0, 0);
	if (collision_shape_offset != Vector2(0, 0)) {
		rotated_offset = collision_shape_offset.rotated(curr_bullet_rotation);
	}

	transf.set_origin(transf.get_origin() + rotated_offset);

	physics_server->area_set_shape_transform(area, bullet_index, transf);

	switch (cached_effective_shape_type) {
		case PhysicsServer2D::SHAPE_CIRCLE:
			physics_server->shape_set_data(shape, cached_circle_radius);
			break;
		case PhysicsServer2D::SHAPE_CAPSULE:
			physics_server->shape_set_data(shape, Vector2(cached_capsule_radius, cached_capsule_height));
			break;
		case PhysicsServer2D::SHAPE_RECTANGLE:
		default:
			physics_server->shape_set_data(shape, cached_rect_size / 2);
			break;
	}

	return transf;
}

void MultiMeshBullets2D::generate_physics_shapes_for_area(int amount) {
	physics_shapes.reserve(amount);
	// Type already resolved + error printed once in cache_collision_shape_typed(). No per-RID error.
	for (int i = 0; i < amount; ++i) {
		RID shape = CollisionShapeHelper2D::create_server_shape(physics_server, cached_effective_shape_type);
		physics_server->area_add_shape(area, shape);
		physics_shapes.emplace_back(shape);
	}
}

void MultiMeshBullets2D::set_all_physics_shapes_enabled_for_area(bool enable) {
	for (int i = 0; i < amount_bullets; ++i) {
		physics_server->area_set_shape_disabled(area, i, !enable);
	}
}

Ref<BulletSpeedData2D> MultiMeshBullets2D::get_bullet_speed_data(int bullet_index) const {
	Ref<BulletSpeedData2D> speed_data = memnew(BulletSpeedData2D);

	if (!validate_bullet_index(bullet_index, "get_bullet_speed_data")) {
		return speed_data;
	}

	// BlockBullets keeps single entry for speed - map any index to 0
	int eff = (all_cached_speed.size() == 1) ? 0 : bullet_index;
	speed_data->speed = all_cached_speed[eff];
	speed_data->max_speed = all_cached_max_speed[eff];
	speed_data->acceleration = all_cached_acceleration[eff];

	return speed_data;
}

void MultiMeshBullets2D::set_bullet_speed_data(int bullet_index, const Ref<BulletSpeedData2D> &new_bullet_speed_data) {
	if (!validate_bullet_index(bullet_index, "set_bullet_speed_data")) {
		return;
	}

	if (new_bullet_speed_data.is_null()) {
		UtilityFunctions::push_error("set_bullet_speed_data: new_bullet_speed_data is null.");
		return;
	}

	if (shared_bullet_curves_data.is_valid() && shared_bullet_curves_data->movement_speed_curve.is_valid()) {
		UtilityFunctions::push_warning("You are trying to set bullet speed data directly while having a movement speed curve assigned to the shared curves data. The curve will override any direct speed data changes. Set the curve to null first if you want to set speed data directly.");
		return;
	}

	BulletCurvesData2D *curves_data = find_bullet_curves_data_ptr(bullet_index);

	if (curves_data != nullptr && curves_data->movement_speed_curve.is_valid()) {
		UtilityFunctions::push_warning("You are trying to set bullet speed data directly while having a movement speed curve assigned as an individual bullet curves data. The curve will override any direct speed data changes. Set the curve to null first if you want to set speed data directly.");
		return;
	}

	// Block keeps single entry
	if (all_cached_speed.size() == 1) {
		bullet_index = 0;
	}

	if (!Math::is_finite(new_bullet_speed_data->speed) || !Math::is_finite(new_bullet_speed_data->max_speed) || !Math::is_finite(new_bullet_speed_data->acceleration)) {
		UtilityFunctions::push_error("set_bullet_speed_data: speed values must be finite.");
		return;
	}

	all_cached_speed[bullet_index] = new_bullet_speed_data->speed;
	all_cached_max_speed[bullet_index] = new_bullet_speed_data->max_speed;
	all_cached_acceleration[bullet_index] = new_bullet_speed_data->acceleration;
}

TypedArray<BulletSpeedData2D> MultiMeshBullets2D::all_bullets_get_speed_data(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_speed_data");

	TypedArray<BulletSpeedData2D> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_speed_data(i));
	}

	return arr;
}

void MultiMeshBullets2D::all_bullets_set_speed_data(const Ref<BulletSpeedData2D> &new_bullet_speed_data, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_speed_data");

	if (new_bullet_speed_data.is_null()) {
		UtilityFunctions::push_error("all_bullets_set_speed_data: new_bullet_speed_data is null.");
		return;
	}

	if (shared_bullet_curves_data.is_valid() && shared_bullet_curves_data->movement_speed_curve.is_valid()) {
		UtilityFunctions::push_warning("You are trying to set bullet speed data directly while having a movement speed curve assigned. The curve will override any direct speed data changes. Set the curve to null first if you want to set speed data directly.");
		return;
	}

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_speed_data(i, new_bullet_speed_data);
	}
}

Vector2 MultiMeshBullets2D::get_bullet_direction(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_direction")) {
		return Vector2();
	}

	int eff = (all_cached_direction.size() == 1) ? 0 : bullet_index;
	return all_cached_direction[eff];
}

void MultiMeshBullets2D::set_bullet_direction(int bullet_index, const Vector2 &new_direction) {
	if (!validate_bullet_index(bullet_index, "set_bullet_direction")) {
		return;
	}

	if (shared_bullet_curves_data.is_valid() && (shared_bullet_curves_data->x_direction_curve.is_valid() || shared_bullet_curves_data->y_direction_curve.is_valid())) {
		UtilityFunctions::push_warning("You are trying to set bullet direction directly while having a direction curve assigned to the shared curves data. The curve will override any direct speed data changes. Set the curve to null first if you want to set speed data directly.");
		return;
	}

	BulletCurvesData2D *curves_data = find_bullet_curves_data_ptr(bullet_index);

	if (curves_data != nullptr && (curves_data->x_direction_curve.is_valid() || curves_data->y_direction_curve.is_valid())) {
		UtilityFunctions::push_warning("You are trying to set bullet direction directly while having a direction curve assigned to the individual curves data. The curve will override any direct speed data changes. Set the curve to null first if you want to set speed data directly.");
		return;
	}

	if (all_cached_direction.size() == 1) {
		bullet_index = 0;
	}
	all_cached_direction[bullet_index] = new_direction.normalized();
	// For Block with single dir, keep velocity in sync
	if (all_cached_velocity.size() == 1) {
		all_cached_velocity[0] = all_cached_direction[0] * all_cached_speed[0] + inherited_velocity_offset;
	}
}

TypedArray<Vector2> MultiMeshBullets2D::all_bullets_get_direction(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_direction");

	TypedArray<Vector2> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_direction(i));
	}

	return arr;
}

void MultiMeshBullets2D::all_bullets_set_direction(const Vector2 &new_direction, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_direction");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_direction(i, new_direction);
	}
}

real_t MultiMeshBullets2D::get_bullet_texture_rotation_radians(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_texture_rotation_radians")) {
		return 0.0;
	}

	return all_cached_instance_transforms[bullet_index].get_rotation();
}

void MultiMeshBullets2D::set_bullet_texture_rotation_radians(int bullet_index, real_t new_rotation_radians) {
	if (!validate_bullet_index(bullet_index, "set_bullet_texture_rotation_radians")) {
		return;
	}

	auto &curr_transf = all_cached_instance_transforms[bullet_index];
	curr_transf.set_rotation(new_rotation_radians);

	// Instantly apply the updated transform so paused factories don't render stale visuals.
	if (all_bullets_enabled_set.contains(bullet_index)) {
		multi->set_instance_transform_2d(bullet_index, curr_transf);
	}

	update_bullet_previous_transform_for_interpolation(bullet_index);
}

TypedArray<real_t> MultiMeshBullets2D::all_bullets_get_texture_rotation_radians(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_texture_rotation_radians");

	TypedArray<real_t> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_texture_rotation_radians(i));
	}

	return arr;
}

void MultiMeshBullets2D::all_bullets_set_texture_rotation_radians(real_t new_rotation_radians, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_texture_rotation_radians");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_texture_rotation_radians(i, new_rotation_radians);
	}
}

real_t MultiMeshBullets2D::get_bullet_texture_rotation_degrees(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_texture_rotation_degrees")) {
		return 0.0;
	}

	return Math::rad_to_deg(all_cached_instance_transforms[bullet_index].get_rotation());
}

void MultiMeshBullets2D::set_bullet_texture_rotation_degrees(int bullet_index, real_t new_rotation_degrees) {
	if (!validate_bullet_index(bullet_index, "set_bullet_texture_rotation_degrees")) {
		return;
	}

	auto &curr_transf = all_cached_instance_transforms[bullet_index];
	curr_transf.set_rotation(Math::deg_to_rad(new_rotation_degrees));

	// Instantly apply the updated transform so paused factories don't render stale visuals.
	if (all_bullets_enabled_set.contains(bullet_index)) {
		multi->set_instance_transform_2d(bullet_index, curr_transf);
	}

	update_bullet_previous_transform_for_interpolation(bullet_index);
}

TypedArray<real_t> MultiMeshBullets2D::all_bullets_get_texture_rotation_degrees(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_texture_rotation_degrees");

	TypedArray<real_t> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_texture_rotation_degrees(i));
	}

	return arr;
}

void MultiMeshBullets2D::all_bullets_set_texture_rotation_degrees(real_t new_rotation_degrees, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_texture_rotation_degrees");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_texture_rotation_degrees(i, new_rotation_degrees);
	}
}

Transform2D MultiMeshBullets2D::get_bullet_transform(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_transform")) {
		return Transform2D();
	}

	return all_cached_instance_transforms[bullet_index];
}

Transform2D MultiMeshBullets2D::get_bullet_global_transform(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_global_transform")) {
		return Transform2D();
	}

	return get_global_transform() * all_cached_instance_transforms[bullet_index];
}

Vector2 MultiMeshBullets2D::get_bullet_velocity(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_velocity")) {
		return Vector2();
	}

	int eff = (all_cached_velocity.size() == 1) ? 0 : bullet_index;
	if (eff < 0 || eff >= (int)all_cached_velocity.size()) {
		return Vector2();
	}
	return all_cached_velocity[eff];
}
void MultiMeshBullets2D::set_bullet_transform(int bullet_index, const Transform2D &new_transform, bool set_direction_based_on_transform) {
	if (!validate_bullet_index(bullet_index, "set_bullet_transform")) {
		return;
	}
	auto &curr_bullet_transf = all_cached_instance_transforms[bullet_index];
	auto &curr_bullet_origin = all_cached_instance_origin[bullet_index];

	const Vector2 origin_delta = new_transform.get_origin() - curr_bullet_origin;

	curr_bullet_transf = new_transform;
	curr_bullet_origin = new_transform.get_origin();

	sync_shape_transform_from_instance(bullet_index, curr_bullet_transf);

	// Instantly apply the updated transforms
	if (all_bullets_enabled_set.contains(bullet_index)) {
		multi->set_instance_transform_2d(bullet_index, curr_bullet_transf);
	}

	// Carry the attachment along so it doesn't stay behind at the old position.
	// Stick-relative attachments recompute from the new transform (same as the next
	// tick would); non-stick ones translate by the jump delta (they never heal otherwise).
	if (bullet_factory != nullptr && bullet_index >= 0 && bullet_index < (int)attachments.size() && attachments[bullet_index] != nullptr) {
		if (attachment_stick_relative_to_bullet[bullet_index]) {
			attachment_transforms[bullet_index] = calculate_attachment_global_transf(bullet_index, curr_bullet_transf);
		} else {
			attachment_transforms[bullet_index] = attachment_transforms[bullet_index].translated(origin_delta);
		}
		if (!bullet_factory->use_physics_interpolation) {
			attachments[bullet_index]->set_global_transform(attachment_transforms[bullet_index]);
		}
	}

	// Update direction if requested
	if (set_direction_based_on_transform) {
		Vector2 new_direction = Vector2(1, 0).rotated(curr_bullet_transf.get_rotation());
		int eff = (all_cached_direction.size() == 1) ? 0 : bullet_index;
		all_cached_direction[eff] = new_direction.normalized();
		if (all_cached_velocity.size() == 1) {
			all_cached_velocity[0] = all_cached_direction[0] * all_cached_speed[0] + inherited_velocity_offset;
		}
	}

	update_bullet_previous_transform_for_interpolation(bullet_index);
}

TypedArray<Transform2D> MultiMeshBullets2D::all_bullets_get_transforms(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_transforms");

	TypedArray<Transform2D> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(get_bullet_transform(i));
	}

	return arr;
}

void MultiMeshBullets2D::all_bullets_set_transforms(const Transform2D &new_transform, bool set_direction_based_on_transform, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_transforms");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_transform(i, new_transform, set_direction_based_on_transform);
	}
}

void MultiMeshBullets2D::set_bullet_direction_towards_position(int bullet_index, const Vector2 &target_position) {
	if (!validate_bullet_index(bullet_index, "set_bullet_direction_towards_position")) {
		return;
	}

	int eff = (all_cached_direction.size() == 1) ? 0 : bullet_index;
	all_cached_direction[eff] = (target_position - all_cached_instance_origin[bullet_index]).normalized();
	if (all_cached_velocity.size() == 1) {
		all_cached_velocity[0] = all_cached_direction[0] * all_cached_speed[0] + inherited_velocity_offset;
	}
}

void MultiMeshBullets2D::all_bullets_set_direction_towards_position(const Vector2 &target_position, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_direction_towards_position");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_direction_towards_position(i, target_position);
	}
}

void MultiMeshBullets2D::set_bullet_direction_towards_node2d(int bullet_index, const Node2D *target_node) {
	if (!validate_bullet_index(bullet_index, "set_bullet_direction_towards_node2d")) {
		return;
	}

	if (target_node == nullptr) {
		UtilityFunctions::push_error("The target_node provided to set_bullet_direction_towards_node2d is null. Cannot set direction towards a null node.");
		return;
	}

	const Vector2 target_position = target_node->get_global_position();
	int eff = (all_cached_direction.size() == 1) ? 0 : bullet_index;
	all_cached_direction[eff] = (target_position - all_cached_instance_origin[bullet_index]).normalized();
	if (all_cached_velocity.size() == 1) {
		all_cached_velocity[0] = all_cached_direction[0] * all_cached_speed[0] + inherited_velocity_offset;
	}
}

void MultiMeshBullets2D::all_bullets_set_direction_towards_node2d(const Node2D *target_node, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_direction_towards_node2d");

	if (target_node == nullptr) {
		UtilityFunctions::push_error("The target_node provided to all_bullets_set_direction_towards_node2d is null. Cannot set direction towards a null node.");
		return;
	}

	const Vector2 target_position = target_node->get_global_position();

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_direction_towards_position(i, target_position);
	}
}

void MultiMeshBullets2D::set_bullet_texture_rotation_towards_position(int bullet_index, const Vector2 &target_position) {
	if (!validate_bullet_index(bullet_index, "set_bullet_texture_rotation_towards_position"))
		return;

	Transform2D &transf = all_cached_instance_transforms[bullet_index];

	Vector2 pos = all_cached_instance_origin[bullet_index];
	Vector2 dir = (target_position - pos).normalized();
	real_t angle = Math::atan2(dir.y, dir.x);

	Vector2 scale = transf.get_scale();
	transf.set_rotation_and_scale(angle, scale);
	transf.set_origin(pos);

	multi->set_instance_transform_2d(bullet_index, transf);

	update_bullet_previous_transform_for_interpolation(bullet_index);
}

void MultiMeshBullets2D::all_bullets_set_texture_rotation_towards_position(const Vector2 &target_position, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_texture_rotation_towards_position");

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_texture_rotation_towards_position(i, target_position);
	}
}

void MultiMeshBullets2D::set_bullet_texture_rotation_towards_node2d(int bullet_index, const Node2D *target_node) {
	if (!validate_bullet_index(bullet_index, "set_bullet_texture_rotation_towards_node2d"))
		return;

	if (target_node == nullptr) {
		UtilityFunctions::push_error("The target_node provided to set_bullet_texture_rotation_towards_node2d is null. Cannot set texture rotation towards a null node.");
		return;
	}

	const Vector2 target_position = target_node->get_global_position();
	set_bullet_texture_rotation_towards_position(bullet_index, target_position);
}

void MultiMeshBullets2D::all_bullets_set_texture_rotation_towards_node2d(const Node2D *target_node, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_texture_rotation_towards_node2d");

	if (target_node == nullptr) {
		UtilityFunctions::push_error("The target_node provided to all_bullets_set_texture_rotation_towards_node2d is null. Cannot set texture rotation towards a null node.");
		return;
	}

	const Vector2 target_position = target_node->get_global_position();

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_texture_rotation_towards_position(i, target_position);
	}
}

real_t MultiMeshBullets2D::get_curves_elapsed_time() const {
	return curves_elapsed_time;
}
void MultiMeshBullets2D::set_curves_elapsed_time(real_t new_time) {
	curves_elapsed_time = new_time;
}

Ref<Curve2D> MultiMeshBullets2D::get_bullet_movement_pattern_curve(int bullet_index) const {
	if (check_exists_bullet_movement_pattern_data(bullet_index)) {
		return find_bullet_movement_pattern_data(bullet_index).path_curve;
	}

	return nullptr;
}

void MultiMeshBullets2D::set_bullet_movement_pattern_from_path(int bullet_index, Path2D *path_holding_pattern, bool face_movement_direction, bool repeat_pattern) {
	if (path_holding_pattern == nullptr) {
		remove_bullet_movement_pattern(bullet_index);
		return;
	}

	const Ref<Curve2D> &curve = path_holding_pattern->get_curve();

	set_bullet_movement_pattern_from_curve(bullet_index, curve, face_movement_direction, repeat_pattern);
}

void MultiMeshBullets2D::all_bullets_set_movement_pattern_from_path(Path2D *path_holding_pattern, bool face_movement_direction, bool repeat_pattern, int start_index, int end_index_inclusive) {
	ensure_indexes_match_amount_bullets_range(start_index, end_index_inclusive, "all_bullets_set_movement_pattern_from_path");

	if (path_holding_pattern == nullptr) {
		all_bullets_remove_movement_pattern(start_index, end_index_inclusive);
		return;
	}

	const Ref<Curve2D> &curve = path_holding_pattern->get_curve();

	if (curve.is_null()) {
		all_bullets_remove_movement_pattern(start_index, end_index_inclusive);
		return;
	}

	for (int i = start_index; i <= end_index_inclusive; ++i) {
		set_bullet_movement_pattern_from_curve(i, curve, face_movement_direction, repeat_pattern);
	}
}

void MultiMeshBullets2D::set_bullet_movement_pattern_from_curve(int bullet_index, const Ref<Curve2D> &curve_pattern, bool face_movement_direction, bool repeat_pattern) {
	if (!validate_bullet_index(bullet_index, "set_bullet_movement_pattern_from_curve")) {
		return;
	}
	if (curve_pattern.is_null()) {
		remove_bullet_movement_pattern(bullet_index);
		return;
	}
	// Block bullets are spawned without an instance handle by design (spawn_block_bullets
	// returns void), so movement patterns stay on DirectionalBullets2D.
	if (is_class("BlockBullets2D")) {
		UtilityFunctions::push_error("BlockBullets2D does not support movement patterns - use DirectionalBullets2D for patterned movement.");
		return;
	}

	all_movement_pattern_data[bullet_index] = BulletMovementPatternData2D{ curve_pattern, face_movement_direction, repeat_pattern };
}

void MultiMeshBullets2D::all_bullets_set_movement_pattern_from_curve(const Ref<Curve2D> &curve_pattern, bool face_movement_direction, bool repeat_pattern, int start_index, int end_index_inclusive) {
	ensure_indexes_match_amount_bullets_range(start_index, end_index_inclusive, "all_bullets_set_movement_pattern_from_curve");

	if (curve_pattern.is_null()) {
		all_bullets_remove_movement_pattern(start_index, end_index_inclusive);
		return;
	}

	// Single error for the whole range instead of one per bullet below.
	if (is_class("BlockBullets2D")) {
		UtilityFunctions::push_error("BlockBullets2D does not support movement patterns - use DirectionalBullets2D for patterned movement.");
		return;
	}

	for (int i = start_index; i <= end_index_inclusive; ++i) {
		set_bullet_movement_pattern_from_curve(i, curve_pattern, face_movement_direction, repeat_pattern);
	}
}

void MultiMeshBullets2D::remove_bullet_movement_pattern(int bullet_index) {
	if (check_exists_bullet_movement_pattern_data(bullet_index)) {
		all_movement_pattern_data[bullet_index] = BulletMovementPatternData2D();
	}
}

void MultiMeshBullets2D::all_bullets_remove_movement_pattern(int start_index, int end_index_inclusive) {
	ensure_indexes_match_amount_bullets_range(start_index, end_index_inclusive, "all_bullets_remove_movement_pattern");

	for (int i = start_index; i <= end_index_inclusive; ++i) {
		remove_bullet_movement_pattern(i);
	}
}

int MultiMeshBullets2D::get_collision_layer() const {
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("get_collision_layer: multimesh was never spawned through BulletFactory2D.");
		return 0;
	}
	return physics_server->area_get_collision_layer(area);
}

void MultiMeshBullets2D::set_collision_layer(int new_collision_layer) {
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_collision_layer: multimesh was never spawned through BulletFactory2D.");
		return;
	}
	physics_server->area_set_collision_layer(area, new_collision_layer);
}

void MultiMeshBullets2D::set_collision_layer_from_array(const TypedArray<int> &numbers) {
	int bitmask = MultiMeshBulletsData2D::calculate_bitmask(numbers);
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_collision_layer_from_array: multimesh was never spawned through BulletFactory2D.");
		return;
	}
	physics_server->area_set_collision_layer(area, bitmask);
}

int MultiMeshBullets2D::get_collision_mask() const {
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("get_collision_mask: multimesh was never spawned through BulletFactory2D.");
		return 0;
	}
	return physics_server->area_get_collision_mask(area);
}

void MultiMeshBullets2D::set_collision_mask(int new_collision_mask) {
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_collision_mask: multimesh was never spawned through BulletFactory2D.");
		return;
	}
	physics_server->area_set_collision_mask(area, new_collision_mask);
}

void MultiMeshBullets2D::set_collision_mask_from_array(const TypedArray<int> &numbers) {
	int bitmask = MultiMeshBulletsData2D::calculate_bitmask(numbers);
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_collision_mask_from_array: multimesh was never spawned through BulletFactory2D.");
		return;
	}
	physics_server->area_set_collision_mask(area, bitmask);
}

bool MultiMeshBullets2D::get_monitorable() const {
	return monitorable;
}

void MultiMeshBullets2D::set_monitorable(bool value) {
	monitorable = value;
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_monitorable: multimesh was never spawned through BulletFactory2D.");
		return;
	}
	physics_server->area_set_monitorable(area, monitorable);
}

void MultiMeshBullets2D::set_collision_shape_runtime(const Ref<Shape2D> &new_shape) {
	if (!physics_server || !area.is_valid()) {
		UtilityFunctions::push_error("set_collision_shape_runtime: physics not ready, cannot change shape at runtime.");
		return;
	}
	PhysicsServer2D::ShapeType old_effective = cached_effective_shape_type;
	const PoolKey old_key{ amount_bullets, old_effective };
	cache_collision_shape_typed(new_shape);
	// Typed cache already printed error once + fallback if needed.
	if (cached_effective_shape_type != old_effective) {
		// RID type mismatch: clear area, free old RIDs and recreate correct type to avoid setting Vector2 data on circle RID etc.
		physics_server->area_clear_shapes(area);
		for (RID &s : physics_shapes) {
			if (s.is_valid()) {
				physics_server->free_rid(s);
			}
		}
		physics_shapes.clear();
		generate_physics_shapes_for_area(amount_bullets);
	}
	// Refresh data + transforms for all bullets so physics + debugger pick up new size immediately.
	// generate sets area transform + shape data from typed cache; then sync cached vectors (no second area_set) + interp cache to avoid lerp pop.
	if ((int)physics_shapes.size() != amount_bullets) {
		UtilityFunctions::push_error("set_collision_shape_runtime: shape RID count mismatch, cannot refresh.");
		return;
	}
	for (int i = 0; i < amount_bullets; ++i) {
		(void)generate_collision_shape_transform_for_area(all_cached_instance_transforms[i], physics_shapes[i], cache_collision_shape_offset, i);
		all_cached_shape_transforms[i] = all_cached_instance_transforms[i];
		Vector2 off = Vector2(0, 0);
		if (cache_collision_shape_offset != Vector2(0, 0)) {
			off = cache_collision_shape_offset.rotated(all_cached_instance_transforms[i].get_rotation());
		}
		all_cached_shape_origin[i] = all_cached_instance_origin[i] + off;
		all_cached_shape_transforms[i].set_origin(all_cached_shape_origin[i]);
		// Fresh RIDs from a type change come enabled; restore per-bullet disabled state
		// so individually disabled bullets don't become collidable again.
		if (!all_bullets_enabled_set.contains(i)) {
			physics_server->area_set_shape_disabled(area, i, true);
		}
		update_bullet_previous_transform_for_interpolation(i);
	}
	// Pooled instances live inside a bucket keyed by get_pool_key(). A runtime type change
	// while disabled would otherwise leave this instance in the stale bucket. Re-bucket it,
	// unless the user opted out of auto pooling (then it must never enter the pool).
	if (!is_active && is_multimesh_auto_pooling_enabled && bullets_pool != nullptr) {
		const PoolKey new_key = get_pool_key();
		if (!(new_key == old_key)) {
			bullets_pool->try_remove_instance(this, old_key);
			bullets_pool->push(this, new_key);
		}
	}
}

int MultiMeshBullets2D::get_bullet_collision_count(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_collision_count")) {
		return 0;
	}
	if (bullet_index < 0 || bullet_index >= (int)bullets_current_collision_count.size()) {
		return 0;
	}
	return bullets_current_collision_count[bullet_index];
}

void MultiMeshBullets2D::set_bullet_collision_count(int bullet_index, int value) {
	if (!validate_bullet_index(bullet_index, "set_bullet_collision_count")) {
		return;
	}
	if (bullet_index < 0 || bullet_index >= (int)bullets_current_collision_count.size()) {
		UtilityFunctions::push_error("set_bullet_collision_count: collision data not initialized for this multimesh.");
		return;
	}
	if (value < 0) {
		bullets_current_collision_count[bullet_index] = 0;
	} else if (bullet_max_collision_count > 0 && value > bullet_max_collision_count) {
		bullets_current_collision_count[bullet_index] = bullet_max_collision_count;
	} else {
		bullets_current_collision_count[bullet_index] = value;
	}
}

void MultiMeshBullets2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_bullet_speed_data", "bullet_index"), &MultiMeshBullets2D::get_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("set_bullet_speed_data", "bullet_index", "new_bullet_speed_data"), &MultiMeshBullets2D::set_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("all_bullets_get_speed_data", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_get_speed_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_speed_data", "new_bullet_speed_data", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_speed_data, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_bullet_direction", "bullet_index"), &MultiMeshBullets2D::get_bullet_direction);
	ClassDB::bind_method(D_METHOD("set_bullet_direction", "bullet_index", "new_direction"), &MultiMeshBullets2D::set_bullet_direction);
	ClassDB::bind_method(D_METHOD("all_bullets_get_direction", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_get_direction, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_direction", "new_direction", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_direction, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("set_bullet_direction_towards_position", "bullet_index", "target_position"), &MultiMeshBullets2D::set_bullet_direction_towards_position);
	ClassDB::bind_method(D_METHOD("all_bullets_set_direction_towards_position", "target_position", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_direction_towards_position, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("set_bullet_direction_towards_node2d", "bullet_index", "target_node"), &MultiMeshBullets2D::set_bullet_direction_towards_node2d);
	ClassDB::bind_method(D_METHOD("all_bullets_set_direction_towards_node2d", "target_node", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_direction_towards_node2d, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_bullet_texture_rotation_radians", "bullet_index"), &MultiMeshBullets2D::get_bullet_texture_rotation_radians);
	ClassDB::bind_method(D_METHOD("set_bullet_texture_rotation_radians", "bullet_index", "new_rotation_radians"), &MultiMeshBullets2D::set_bullet_texture_rotation_radians);
	ClassDB::bind_method(D_METHOD("all_bullets_get_texture_rotation_radians", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_get_texture_rotation_radians, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_texture_rotation_radians", "new_rotation_radians", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_texture_rotation_radians, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_bullet_texture_rotation_degrees", "bullet_index"), &MultiMeshBullets2D::get_bullet_texture_rotation_degrees);
	ClassDB::bind_method(D_METHOD("set_bullet_texture_rotation_degrees", "bullet_index", "new_rotation_degrees"), &MultiMeshBullets2D::set_bullet_texture_rotation_degrees);
	ClassDB::bind_method(D_METHOD("all_bullets_get_texture_rotation_degrees", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_get_texture_rotation_degrees, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_texture_rotation_degrees", "new_rotation_degrees", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_texture_rotation_degrees, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("set_bullet_texture_rotation_towards_position", "bullet_index", "target_position"), &MultiMeshBullets2D::set_bullet_texture_rotation_towards_position);
	ClassDB::bind_method(D_METHOD("all_bullets_set_texture_rotation_towards_position", "target_position", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_texture_rotation_towards_position, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("set_bullet_texture_rotation_towards_node2d", "bullet_index", "target_node"), &MultiMeshBullets2D::set_bullet_texture_rotation_towards_node2d);
	ClassDB::bind_method(D_METHOD("all_bullets_set_texture_rotation_towards_node2d", "target_node", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_texture_rotation_towards_node2d, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_bullet_transform", "bullet_index"), &MultiMeshBullets2D::get_bullet_transform);
	ClassDB::bind_method(D_METHOD("get_bullet_global_transform", "bullet_index"), &MultiMeshBullets2D::get_bullet_global_transform);
	ClassDB::bind_method(D_METHOD("get_bullet_velocity", "bullet_index"), &MultiMeshBullets2D::get_bullet_velocity);
	ClassDB::bind_method(D_METHOD("set_bullet_transform", "bullet_index", "new_transform", "set_direction_based_on_transform"), &MultiMeshBullets2D::set_bullet_transform, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("all_bullets_get_transforms", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_get_transforms, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_transforms", "new_transform", "set_direction_based_on_transform", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_transforms, DEFVAL(false), DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("play_sprite_animation", "sprite_frames", "animation"), &MultiMeshBullets2D::play_sprite_animation, DEFVAL(StringName("default")));
	ClassDB::bind_method(D_METHOD("play_sprite_animation_name", "animation"), &MultiMeshBullets2D::play_sprite_animation_name);
	ClassDB::bind_method(D_METHOD("restart_sprite_animation"), &MultiMeshBullets2D::restart_sprite_animation);
	ClassDB::bind_method(D_METHOD("stop_sprite_animation"), &MultiMeshBullets2D::stop_sprite_animation);
	ClassDB::bind_method(D_METHOD("resume_sprite_animation"), &MultiMeshBullets2D::resume_sprite_animation);
	ClassDB::bind_method(D_METHOD("is_sprite_animation_playing"), &MultiMeshBullets2D::is_sprite_animation_playing);
	ClassDB::bind_method(D_METHOD("is_sprite_animation_finished"), &MultiMeshBullets2D::is_sprite_animation_finished);
	ClassDB::bind_method(D_METHOD("get_sprite_animation"), &MultiMeshBullets2D::get_sprite_animation);
	ClassDB::bind_method(D_METHOD("get_sprite_frames"), &MultiMeshBullets2D::get_sprite_frames);
	ClassDB::bind_method(D_METHOD("get_sprite_frame"), &MultiMeshBullets2D::get_sprite_frame);
	ClassDB::bind_method(D_METHOD("get_sprite_frame_count"), &MultiMeshBullets2D::get_sprite_frame_count);

	ClassDB::bind_method(D_METHOD("disable_bullet", "bullet_index", "disable_bullet_attachment"), &MultiMeshBullets2D::disable_bullet, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("enable_bullet", "bullet_index", "collision_amount", "enable_attachment"), &MultiMeshBullets2D::enable_bullet, DEFVAL(0), DEFVAL(true));

	ClassDB::bind_method(D_METHOD("bullet_free_attachment", "bullet_index"), &MultiMeshBullets2D::bullet_free_attachment);
	ClassDB::bind_method(D_METHOD("bullet_disable_attachment", "bullet_index"), &MultiMeshBullets2D::bullet_disable_attachment);
	ClassDB::bind_method(D_METHOD("bullet_enable_attachment", "bullet_index"), &MultiMeshBullets2D::bullet_enable_attachment);
	ClassDB::bind_method(D_METHOD("get_amount_active_attachments"), &MultiMeshBullets2D::get_amount_active_attachments);
	ClassDB::bind_method(D_METHOD("_do_deferred_bullet_disable_attachment", "bullet_index", "expected_generation"), &MultiMeshBullets2D::_do_deferred_bullet_disable_attachment);

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
	ClassDB::bind_method(D_METHOD("multimesh_attach_time_based_function", "time", "callable", "repeat", "execute_only_if_multimesh_is_active"), &MultiMeshBullets2D::multimesh_attach_time_based_function, DEFVAL(false), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("_do_attach_time_based_function", "time", "callable", "repeat", "execute_only_if_multimesh_is_active", "expected_timers_generation"), &MultiMeshBullets2D::_do_attach_time_based_function);

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

	ClassDB::bind_method(D_METHOD("get_bullet_collision_count", "bullet_index"), &MultiMeshBullets2D::get_bullet_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullet_collision_count", "bullet_index", "value"), &MultiMeshBullets2D::set_bullet_collision_count);
	ClassDB::bind_method(D_METHOD("get_bullets_current_collision_count"), &MultiMeshBullets2D::get_bullets_current_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullets_current_collision_count", "arr"), &MultiMeshBullets2D::set_bullets_current_collision_count);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bullets_current_collision_count"), "set_bullets_current_collision_count", "get_bullets_current_collision_count");

	ClassDB::bind_method(D_METHOD("get_collision_layer"), &MultiMeshBullets2D::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "new_collision_layer"), &MultiMeshBullets2D::set_collision_layer);

	ClassDB::bind_method(D_METHOD("get_collision_mask"), &MultiMeshBullets2D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "new_collision_mask"), &MultiMeshBullets2D::set_collision_mask);

	ClassDB::bind_method(D_METHOD("set_collision_layer_from_array", "array_of_layers"), &MultiMeshBullets2D::set_collision_layer_from_array);
	ClassDB::bind_method(D_METHOD("set_collision_mask_from_array", "array_of_masks"), &MultiMeshBullets2D::set_collision_mask_from_array);

	ClassDB::bind_method(D_METHOD("get_monitorable"), &MultiMeshBullets2D::get_monitorable);
	ClassDB::bind_method(D_METHOD("set_monitorable", "value"), &MultiMeshBullets2D::set_monitorable);

	ClassDB::bind_method(D_METHOD("get_collision_shape"), &MultiMeshBullets2D::get_collision_shape);
	ClassDB::bind_method(D_METHOD("set_collision_shape_runtime", "new_shape"), &MultiMeshBullets2D::set_collision_shape_runtime);

	//

	ClassDB::bind_method(D_METHOD("bullet_get_attachment", "bullet_index"), &MultiMeshBullets2D::bullet_get_attachment);
	ClassDB::bind_method(D_METHOD("bullet_set_attachment_to_null", "bullet_index"), &MultiMeshBullets2D::bullet_set_attachment_to_null);

	ClassDB::bind_method(D_METHOD("all_bullets_get_attachments", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_get_attachments, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_attachment_to_null", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_attachment_to_null, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_attachment", "attachment_scene", "attachment_pooling_id", "bullet_attachment_offset", "stick_relative_to_bullet", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_attachment, DEFVAL(true), DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("bullet_set_attachment", "bullet_index", "attachment_scene", "attachment_pooling_id", "bullet_attachment_offset", "stick_relative_to_bullet"), &MultiMeshBullets2D::bullet_set_attachment, DEFVAL(Vector2(0, 0)), DEFVAL(true));

	ClassDB::bind_method(D_METHOD("set_shared_bullet_curves_data", "data"), &MultiMeshBullets2D::set_shared_bullet_curves_data);
	ClassDB::bind_method(D_METHOD("get_shared_bullet_curves_data"), &MultiMeshBullets2D::get_shared_bullet_curves_data);
	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "shared_bullet_curves_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletCurvesData2D"),
			"set_shared_bullet_curves_data", "get_shared_bullet_curves_data");

	ClassDB::bind_method(D_METHOD("bullet_set_curves_data", "bullet_index", "data"), &MultiMeshBullets2D::bullet_set_curves_data);
	ClassDB::bind_method(D_METHOD("bullet_get_curves_data", "bullet_index"), &MultiMeshBullets2D::bullet_get_curves_data);
	ClassDB::bind_method(D_METHOD("all_bullets_get_curves_data", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_get_curves_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_curves_data", "curves_data", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_curves_data, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_curves_elapsed_time"), &MultiMeshBullets2D::get_curves_elapsed_time);
	ClassDB::bind_method(D_METHOD("set_curves_elapsed_time", "new_time"), &MultiMeshBullets2D::set_curves_elapsed_time);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "curves_elapsed_time"), "set_curves_elapsed_time", "get_curves_elapsed_time");

	ClassDB::bind_method(D_METHOD("get_bullet_movement_pattern_curve", "bullet_index"), &MultiMeshBullets2D::get_bullet_movement_pattern_curve);

	ClassDB::bind_method(D_METHOD("set_bullet_movement_pattern_from_path", "bullet_index", "path_holding_pattern", "face_movement_direction", "repeat_pattern"), &MultiMeshBullets2D::set_bullet_movement_pattern_from_path, DEFVAL(false), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("all_bullets_set_movement_pattern_from_path", "path_holding_pattern", "face_movement_direction", "repeat_pattern", "start_index", "end_index_inclusive"), &MultiMeshBullets2D::all_bullets_set_movement_pattern_from_path, DEFVAL(false), DEFVAL(true), DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("set_bullet_movement_pattern_from_curve", "bullet_index", "curve_pattern", "face_movement_direction", "repeat_pattern"), &MultiMeshBullets2D::set_bullet_movement_pattern_from_curve, DEFVAL(false), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("all_bullets_set_movement_pattern_from_curve", "curve_pattern", "face_movement_direction", "repeat_pattern", "start_index", "end_index_inclusive"), &MultiMeshBullets2D::all_bullets_set_movement_pattern_from_curve, DEFVAL(false), DEFVAL(true), DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("remove_bullet_movement_pattern", "bullet_index"), &MultiMeshBullets2D::remove_bullet_movement_pattern);
	ClassDB::bind_method(D_METHOD("all_bullets_remove_movement_pattern", "start_index", "end_index_inclusive"), &MultiMeshBullets2D::all_bullets_remove_movement_pattern, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("has_bullet_movement_pattern", "bullet_index"), &MultiMeshBullets2D::check_exists_bullet_movement_pattern_data);

	ADD_SIGNAL(MethodInfo("sprite_animation_finished",
			PropertyInfo(Variant::OBJECT, "multimesh_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "MultiMeshBullets2D")));
}
} //namespace BlastBullets2D
