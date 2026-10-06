// MultiMesh + render buffer setup for a fresh or reused volley: instance count,
// quad mesh, texture transforms, first buffer upload (set_up_bullet_instances) and
// finalize. Physics area/shape setup lives in bullet_volley2d_collision.cpp.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletVolley2D::set_up_bullet_instances(const BulletVolleyData2D &data) {
	active_bullets_counter = amount_bullets;

	bullet_max_collision_count = data.bullet_max_collision_count;

	if (data.bullets_current_collision_count.size() == 0) {
		bullets_current_collision_count.clear();
		bullets_current_collision_count.resize(amount_bullets, 0);
	} else {
		// Always succeeds (fills + warns); uncovered bullets start at 0.
		set_bullets_current_collision_count(data.bullets_current_collision_count, data.tile_bullets_current_collision_count);
	}

	// Per-bullet custom data (strict indexing; strictly separate from
	// shared_bullets_custom_data - bullets without an entry read null, never
	// the shared value). Empty = all null, size == N = entry i for bullet i,
	// short = tail bullets null, long = extras ignored. With the tile
	// checkbox, short arrays wrap (i % size).
	all_bullets_custom_data.assign(amount_bullets, Ref<Resource>());
	if (data.all_bullets_custom_data.size() > 0) {
		const int custom_size = data.all_bullets_custom_data.size();
		const bool tile_custom = data.tile_all_bullets_custom_data;
		if (custom_size != amount_bullets) {
			WarnOnce2D::warn(warn_data_id, 2u, custom_size, amount_bullets, "BulletVolley2D: all_bullets_custom_data size (" + String::num_int64(custom_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets read null" + String(tile_custom ? " (tiling on: wrapping short array)." : " (check tile_all_bullets_custom_data to wrap, or provide one entry per bullet)."));
		}
		for (int i = 0; i < amount_bullets; ++i) {
			const int src = tile_custom ? (i % custom_size) : i;
			all_bullets_custom_data[i] = (src >= 0 && src < custom_size) ? Ref<Resource>(data.all_bullets_custom_data[src]) : Ref<Resource>();
		}
	}

	is_life_time_over_signal_enabled = data.is_life_time_over_signal_enabled;
	emit_collision_signals = data.emit_collision_signals;

	is_life_time_infinite = data.is_life_time_infinite;

	set_up_area(data.collision_layer, data.collision_mask, data.monitorable, bullet_factory != nullptr ? bullet_factory->physics_space : RID());

	stop_rotation_when_max_reached = data.stop_rotation_when_max_reached;

	cache_collision_shape_offset = data.collision_shape_offset;

	if (all_cached_instance_transforms.size() != 0) {
		// Waking a pooled volley: drop last life's frames. The buffers keep their size since the bullet count never changes on reuse.
		all_cached_instance_transforms.clear();
		all_cached_instance_origin.clear();
		all_cached_shape_transforms.clear();
	} else {
		// First spawn: reserve everything up front for the fixed bullet count.
		all_cached_instance_transforms.reserve(amount_bullets);
		all_cached_instance_origin.reserve(amount_bullets);
		all_cached_shape_transforms.reserve(amount_bullets);
	}

	cache_texture_rotation_radians = data.texture_rotation_radians;

	// One node inverse for the whole setup loop (generate_texture_transform
	// converts every bullet to multimesh-local space).
	NodeInverseScope inverse_scope(this);
	// Shape data once per volley (pooled reuse with the same shape skips it).
	apply_volley_shape_data();

	// Transform source: the factory's native span when present (no Variant),
	// else the data resource's Array. Consumed here (cleared below) so a
	// later re-entrant spawn can never read a stale pointer.
	const Transform2D *span = spawn_transforms_ptr;
	const int span_count = spawn_transforms_count;
	spawn_transforms_ptr = nullptr;
	spawn_transforms_count = 0;
	if (span != nullptr && span_count != amount_bullets) {
		span = nullptr; // defensive: never index past the span
	}
	// One upload for every instance (see generate_texture_transform).
	if ((int)batch_buffer.size() != amount_bullets * 8) {
		batch_buffer.resize(amount_bullets * 8);
	}
	const bool batch_ok = multi.is_valid() && multi->get_instance_count() == amount_bullets;
	float *batch_w = batch_ok ? batch_buffer.ptrw() : nullptr;
	for (int i = 0; i < amount_bullets; ++i) {
		const Transform2D curr_data_transf = span != nullptr ? span[i] : (Transform2D)data.transforms[i];

		// Generates a collision shape transform for a particular bullet and attaches it to the area
		Transform2D shape_transf = generate_collision_shape_transform_for_area(curr_data_transf, data.collision_shape_offset, i);

		// Generates texture transform with correct rotation and sets it to the correct bullet on the multimesh
		const Transform2D &texture_transf = generate_texture_transform(curr_data_transf, data.is_texture_rotation_permanent, cache_texture_rotation_radians);

		if (batch_ok) {
			write_multimesh_transform2d(batch_w + i * 8, to_local_for_multimesh(texture_transf));
		}

		// Cache bullet transforms and origin vectors
		all_cached_instance_transforms.emplace_back(texture_transf);
		all_cached_instance_origin.emplace_back(texture_transf.get_origin());

		all_cached_shape_transforms.emplace_back(shape_transf);
	}
	if (batch_ok) {
		multi->set_buffer(batch_buffer);
	} else if (multi.is_valid()) {
		// Instance count not set up yet (never expected): per-instance
		// fallback so the bullets are never drawn at stale poses.
		const int n = MIN(amount_bullets, (int)multi->get_instance_count());
		for (int i = 0; i < n; ++i) {
			multi->set_instance_transform_2d(i, to_local_for_multimesh(all_cached_instance_transforms[i]));
		}
	}
}

void BulletVolley2D::generate_multimesh() {
	Ref<MultiMesh> new_multi;
	new_multi.instantiate();
	new_multi->set_transform_format(MultiMesh::TRANSFORM_2D);

	multi = new_multi;
	set_multimesh(multi);
}

void BulletVolley2D::set_up_multimesh(int new_instance_count, const Ref<Mesh> &new_mesh, Vector2 new_texture_size) {
	if (new_mesh.is_valid()) {
		if (multi->get_mesh() != new_mesh) {
			multi->set_mesh(new_mesh);
		}
	} else {
		// Pooled reuse keeps the volley's own quad: a fresh QuadMesh per
		// enable was a resource + RS mesh allocation on every spawn.
		if (owned_quad_mesh.is_null()) {
			owned_quad_mesh.instantiate();
		}
		if (owned_quad_mesh->get_size() != new_texture_size) {
			owned_quad_mesh->set_size(new_texture_size);
		}
		if (multi->get_mesh() != owned_quad_mesh) {
			multi->set_mesh(owned_quad_mesh);
		}
	}
	// Always track the resolved size, even with a custom mesh, so pooled reuse
	// with different data cannot inherit a stale quad size.
	texture_size = new_texture_size;

	// One huge bounding box so the camera can never cull the whole volley by mistake (bullets live all over the level). Hidden bullets cost nothing - they're written as zero-scale.
	multi->set_custom_aabb(AABB(Vector3(-100000, -100000, -1000), Vector3(200000, 200000, 2000)));

	// MultiMesh::set_instance_count reallocates the RS buffer even for an
	// unchanged count, and the count never changes on pool reuse (it is
	// the pool key). Every instance is rewritten by set_up_bullet_instances.
	if (multi->get_instance_count() != new_instance_count) {
		multi->set_instance_count(new_instance_count);
	}
	if ((int)batch_buffer.size() != new_instance_count * 8) {
		batch_buffer.resize(new_instance_count * 8);
	}
}

// Always called last (texture comes from rebuild_sprite_animation, called by spawn/enable)
void BulletVolley2D::finalize_set_up(
		const Ref<Resource> &new_shared_bullets_custom_data,
		const Ref<Material> &new_material,
		int new_z_index,
		int new_light_mask,
		int new_visibility_layer,
		const Color &new_self_modulate,
		const Dictionary &new_instance_shader_parameters) {
	// Bullets custom data. Always assigned (null clears) so pool reuse never leaks
	// the previous owner's data into a new spawn.
	shared_bullets_custom_data = new_shared_bullets_custom_data;

	if (new_material.is_valid()) {
		godot::Ref<ShaderMaterial> shader_material = new_material;
		// If a shader material was passed and the user has provided instance shader parameters
		if (shader_material.is_valid() && new_instance_shader_parameters.is_empty() == false) {
			// Keys removed since the last life must be reset on the CanvasItem:
			// clearing only the C++ dict leaves stale GPU overrides behind.
			for (const String &old_key : applied_instance_shader_keys) {
				if (!old_key.is_empty() && !new_instance_shader_parameters.has(old_key)) {
					set_instance_shader_parameter(old_key, Variant());
				}
			}
			applied_instance_shader_keys.clear();
			instance_shader_parameters = new_instance_shader_parameters;

			const Array &keys = new_instance_shader_parameters.keys();
			for (int i = 0; i < keys.size(); ++i) {
				const String key = keys[i];
				if (key.is_empty()) {
					continue;
				}
				const Variant &value = new_instance_shader_parameters[key];

				set_instance_shader_parameter(key, value);
				applied_instance_shader_keys.push_back(key);
			}
		} else {
			// Shader without params (or non-shader material handled below): drop the
			// previous owner's dict AND its live CanvasItem overrides so pool
			// reuse can't leak stale entries into the next volley.
			clear_applied_instance_shader_overrides();
			instance_shader_parameters.clear();
		}

		set_material(new_material);
	} else {
		clear_applied_instance_shader_overrides();
		instance_shader_parameters.clear();
		set_material(nullptr);
	}

	// Z Index
	set_z_index(new_z_index);

	// Light mask
	set_light_mask(new_light_mask);

	// Visibility layer
	set_visibility_layer(new_visibility_layer);

	// Whole-volley tint. Always assigned (white clears) so pool reuse never
	// leaks the previous owner's color into a new spawn.
	set_self_modulate(new_self_modulate);
}

Transform2D BulletVolley2D::generate_texture_transform(Transform2D transf, bool is_texture_rotation_permanent, real_t texture_rotation_radians) {
	if (is_texture_rotation_permanent) {
		// Same texture rotation no matter the rotation of the bullet's transform
		transf.set_rotation(texture_rotation_radians);
	} else {
		// The rotation of the texture will be influenced by the rotation of the bullet transform
		transf.set_rotation(transf.get_rotation() + texture_rotation_radians);
	}

	// No per-instance server write here: set_up_bullet_instances writes every
	// instance into batch_buffer and uploads them with ONE set_buffer call.
	return transf;
}

} // namespace BlastBullets2D
