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
							// Null the slot BEFORE the callback (same ordering as
							// bullet_disable_attachment): re-entrant API calls from the
							// script must see an empty slot.
							BulletAttachment2D *detaching = attachments[i];
							attachments[i] = nullptr;
							// Owner tracking cleared like bullet_disable_attachment:
							// attachments outlive this multimesh (siblings in the
							// container), and a stale owner id would make their
							// PREDELETE resolve a dead multimesh.
							detaching->owner_multimesh_id = 0;
							detaching->owner_bullet_index = -1;
							detaching->call_on_bullet_disable();
						} else {
							bullet_disable_attachment(i);
						}
					}
				}

				physics_server->area_set_area_monitor_callback(area, Variant());
				physics_server->area_set_monitor_callback(area, Variant());

				// Detach the shapes from the area BEFORE freeing their RIDs (same
				// order as enable_multimesh()/set_collision_shape_runtime()): freeing
				// still-attached shape RIDs warns/leaks on the physics server.
				physics_server->area_clear_shapes(area);

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

	// min(): the vector is sized to amount_bullets by spawn(), but this can be
	// called on a not-yet-spawned instance through debug helpers.
	const int count = Math::min((int)attachments.size(), amount_bullets);
	for (int i = 0; i < count; ++i) {
		if (attachments[i] != nullptr) {
			++amount_active_attachments;
		}
	}

	return amount_active_attachments;
}

void MultiMeshBullets2D::reset_attachment_state_for_reuse() {
	// Force-disable any surviving slot first. Deferred attachment disables can be
	// dropped by a generation bump (e.g. lifetime expiry pooled this instance and a
	// spawn re-enabled it before the deferred flush ran), so a new owner must never
	// be able to observe, disable or re-pool a previous owner's attachment.
	for (int i = 0; i < (int)attachments.size(); ++i) {
		if (attachments[i] != nullptr) {
			bullet_disable_attachment(i);
		}
	}

	const int count = amount_bullets;
	attachment_pooling_ids.assign(count, 0);
	attachments.assign(count, nullptr);
	attachment_transforms.assign(count, Transform2D());
	attachment_offsets.assign(count, Vector2());
	attachment_local_transforms.assign(count, Transform2D());
	attachment_stick_relative_to_bullet.assign(count, 1);
	all_previous_attachment_transf.assign(count, Transform2D());
}

// Used to spawn brand new bullets.
void MultiMeshBullets2D::spawn(const MultiMeshBulletsData2D &data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container, const Vector2 &new_inherited_velocity_offset, int new_sparse_set_id, bool spawn_in_pool, uint64_t spawner_id) {
	this->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF); // We have custom physics interpolation logic, so disable the Godot one that comes from Godot 4.5

	sparse_set_id = new_sparse_set_id;
	inherited_velocity_offset = new_inherited_velocity_offset;

	bullets_pool = pool;
	bullet_factory = factory;
	// Ownership is stamped FIRST, before the area gets a physics space, shapes
	// are enabled, or the node enters the tree below: a spawner volley is never
	// observable as factory-owned. Pooled pre-population passes 0, so reused
	// instances can never inherit a previous owner's spawner.
	owner_spawner_id = spawner_id;
	physics_server = PhysicsServer2D::get_singleton();

	amount_bullets = data.transforms.size(); // important, because some set_up methods use this
	cache_collision_shape_typed(data.collision_shape);

	++multimesh_generation;

	all_bullets_enabled_set.resize(amount_bullets);
	all_bullet_curves_data.assign(amount_bullets, Ref<BulletCurvesData2D>());
	all_movement_pattern_data.assign(amount_bullets, BulletMovementPatternData2D());
	bullet_collision_epochs.assign(amount_bullets, 0);
	batch_buffer.resize(amount_bullets * 8);

	set_up_life_time_timer(data.max_life_time, data.max_life_time);

	generate_multimesh();
	set_up_multimesh(amount_bullets, data.mesh, resolve_quad_size(data.sprite_frames, data.animation, data.texture_size));

	area = physics_server->area_create();
	generate_physics_shapes_for_area(amount_bullets);

	set_up_bullet_instances(data);

	// Set up bullet attachments so that for every bullet you will be able to have an attachment if needed.
	// Blanks all slots and arrays (fresh instances get zeroed vectors; the loop in
	// reset_attachment_state_for_reuse is a no-op here but keeps the invariant in one place).
	reset_attachment_state_for_reuse();

	set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures);

	all_previous_instance_transf.resize(amount_bullets);
	all_previous_attachment_transf.resize(amount_bullets);

	update_all_previous_transforms_for_interpolation();

	finalize_set_up(
			data.shared_bullets_custom_data,
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

	// Shared spawn-data attachments (both bullet types). Skipped for pooled
	// pre-population: the slots were just blanked above, and enable_multimesh()
	// applies the data when the instance is popped instead.
	if (!spawn_in_pool) {
		apply_shared_bullet_attachment_from_data(data);
	}
}

void MultiMeshBullets2D::reset_transient_subclass_state(bool drop_stale_work) {
	// Base holds no subclass state; overrides neutralize their own ballistics
	// below. Shared curves/patterns live in the reset body itself (not here)
	// so no override can skip them by forgetting a base call.
	(void)drop_stale_work;
}

void MultiMeshBullets2D::reset_transient_volley_state(uint64_t new_owner_spawner_id, bool drop_stale_work) {
	// Ownership is stamped first so every step below already belongs to the
	// new life (or to nobody, when dying).
	owner_spawner_id = new_owner_spawner_id;
	// Shared curves/patterns are cleared HERE, not in the virtual below: an
	// override that forgets its base call would otherwise leak the previous
	// owner's per-bullet curves and movement patterns into the next life
	// (the reseed functions return early on empty arrays and skip null
	// entries, so stale slots would never be blanked).
	shared_bullet_curves_data.unref();
	for (auto &r : all_bullet_curves_data) {
		r.unref();
	}
	for (auto &p : all_movement_pattern_data) {
		p = BulletMovementPatternData2D();
	}
	reset_transient_subclass_state(drop_stale_work);
	// Blank attachment state: a reused instance must never carry the previous
	// owner's attachment slots into the next life.
	reset_attachment_state_for_reuse();
	if (drop_stale_work) {
		// New life: stale deferred emits/disables (scheduled before a pool
		// reuse) carry the old generation and no-op at flush time, and the
		// previous owner's volley-wide connections must not fire again.
		// Disabled (but not pooled) volleys keep their connections here: a
		// same-owner enable_bullet() wake must not silence the volley.
		++multimesh_generation;
		disconnect_sprite_animation_connections();
	}
	// Fresh lifetime must not inherit stale hits or timers from the previous
	// owner. disable_multimesh() clears these when pooling is on, but an
	// instance woken with pooling off (or a direct enable) would leak them.
	all_collided_bullets.clear();
	_do_detach_all_time_based_functions(multimesh_timers_generation);
	// Same reason: a woken instance that never went through disable_multimesh()
	// would keep counting curve time from the previous owner.
	curves_elapsed_time = 0.0;
	// Animation cursor restarts; baked frames are kept so a same-owner wake
	// resumes them. New-life paths blank the frames before rebuilding.
	anim_frame_index = 0;
	anim_paused = false;
	anim_finished = false;
	if (!anim_frame_secs.empty()) {
		anim_frame_time_left = anim_frame_secs[0];
	}
}

void MultiMeshBullets2D::deactivate_volley() {
	all_bullets_enabled_set.clear();
	active_bullets_counter = 0;
	is_active = false;
	set_visible(false);
	set_all_physics_shapes_enabled_for_area(false);
}

// Activates the multimesh
bool MultiMeshBullets2D::enable_multimesh(const MultiMeshBulletsData2D &data, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id) {
	// Pool buckets are keyed by amount, but a direct GDScript call can hand a
	// mismatched array. set_up_bullet_instances indexes data.transforms by
	// amount_bullets, so reject early without touching state.
	if (data.transforms.size() != amount_bullets) {
		UtilityFunctions::push_error("enable_multimesh: transforms size (" + String::num_int64(data.transforms.size()) + ") must match amount_bullets (" + String::num_int64(amount_bullets) + ").");
		return false;
	}

	// Narrow guard: same-shape reuse only rewrites transforms, re-enables
	// shapes and reseeds SoA vectors (no RID alloc/free), so it is safe from
	// ordinary physics callbacks such as _physics_process shooting. Only a
	// shape-TYPE change performs structural RID work (area_clear_shapes /
	// free_rid / re-bucket) and must wait for a safe point. Iterating sweeps
	// (factory busy) always reject: vectors below are being walked.
	const PhysicsServer2D::ShapeType incoming_effective = CollisionShapeHelper2D::get_effective_type(data.collision_shape, false);
	const bool shape_type_changes = (incoming_effective != cached_effective_shape_type);
	if (bullet_factory != nullptr && bullet_factory->is_bullets_iterating()) {
		UtilityFunctions::push_error("enable_multimesh cannot run while bullets are being processed (factory sweep in progress, e.g. inside a collision or lifetime signal handler). Use call_deferred() to enable outside the sweep.");
		return false;
	}
	if (shape_type_changes && bullet_factory != nullptr && bullet_factory->is_structural_mutation_unsafe()) {
		UtilityFunctions::push_error("enable_multimesh with a different collision shape type cannot run inside a physics frame (server flush locks apply to the shape RIDs it must recreate). Use call_deferred() to enable outside the physics step.");
		return false;
	}

	// Re-enabling a live volley would wipe its attachments, timers, curves and
	// patterns mid-flight. Pool pops only hand out disabled instances, so a
	// live instance here is always a direct (mis)call: reject, don't reseed.
	if (is_active) {
		UtilityFunctions::push_error("enable_multimesh: instance is already active. Disable it first or spawn a new volley instead.");
		return false;
	}

	// Reseeding a dying instance would configure a volley that never lives a
	// tick (pool pops already filter these; this covers direct GDScript
	// calls). queue_free() is terminal.
	if (is_queued_for_deletion()) {
		UtilityFunctions::push_error("enable_multimesh: multimesh is queued for deletion.");
		return false;
	}

	// WP-A: two-phase enable. Phase 1 (validate everything) runs before the
	// first mutation, so a reject below leaves zero state behind and no
	// rollback is needed. Phase 2 (reset + reseed) only runs on valid input.
	// Wrong-type data is refused here: custom_additional_enable_logic() is
	// seeding-only now and its own check is unreachable defense-in-depth.
	if (!is_data_type_compatible(data)) {
		UtilityFunctions::push_error("enable_multimesh: wrong spawn data type for this multimesh, ignoring the enable.");
		return false;
	}

	// The factory spawn_* paths validate finiteness, but a direct
	// enable_multimesh() call bypasses them: a NaN/Inf offset here would
	// poison every bullet's velocity for the whole volley. Checked before any
	// mutation (previously after the owner/curve clears, leaking on reject).
	if (!new_inherited_velocity_offset.is_finite()) {
		UtilityFunctions::push_error("enable_multimesh: inherited velocity offset must be finite, keeping the old value.");
		return false;
	}

	// WP-B: one reset owns the whole clean-disabled invariant (owner stamp,
	// curves/patterns/subclass ballistics, attachment slots, collided hits,
	// timers, clocks, animation cursor, generation bump, connection scrub).
	// Spawner ownership is stamped here (before any re-activation below), so
	// a spawner reuse is never observable as factory-owned. Whoever enables
	// next stamps anew; 0 keeps the factory-owned default.
	reset_transient_volley_state(spawner_id, true);

	// A new life never inherits the previous owner's baked animation
	// (reset kept the frames for same-owner wakes; a reseed always rebuilds,
	// so blank them here). rebuild only overwrites on success: a failed
	// rebuild leaves blank state, never the old animation playing.
	anim_source.unref();
	anim_frames.clear();
	anim_frame_secs.clear();
	set_texture(Ref<Texture2D>());
	// WP-A: no snapshot/rollback anymore. Wrong-type input was rejected above
	// before any mutation, and every reseed below fully overwrites the blank
	// state reset_transient_volley_state() left behind.
	inherited_velocity_offset = new_inherited_velocity_offset;
	const PhysicsServer2D::ShapeType old_effective_shape_type = cached_effective_shape_type;
	cache_collision_shape_typed(data.collision_shape);

	// Reused RIDs keep their original type. If the new spawn switches shape type,
	// the old RIDs would receive mismatched data below, so recreate them exactly
	// like set_collision_shape_runtime() does.
	if (cached_effective_shape_type != old_effective_shape_type && physics_server != nullptr && area.is_valid()) {
		physics_server->area_clear_shapes(area);
		for (RID &s : physics_shapes) {
			if (s.is_valid()) {
				physics_server->free_rid(s);
			}
		}
		physics_shapes.clear();
		generate_physics_shapes_for_area(amount_bullets);
	}

	// Blank attachment state before anything else: a reused instance must never
	// carry the previous owner's attachment slots into this enable.
	reset_attachment_state_for_reuse();

	++multimesh_generation;

	// Fresh lifetime must not inherit stale hits or timers from the previous
	// owner. disable_multimesh() clears these when pooling is on, but an
	// instance woken with pooling off (or a direct enable) would leak them.
	all_collided_bullets.clear();
	_do_detach_all_time_based_functions(multimesh_timers_generation);

	// Same reason: a woken instance that never went through disable_multimesh()
	// would keep counting curve time from the previous owner.
	curves_elapsed_time = 0.0;

	set_up_life_time_timer(data.max_life_time, data.max_life_time);

	set_up_multimesh(amount_bullets, data.mesh, resolve_quad_size(data.sprite_frames, data.animation, data.texture_size));

	set_up_bullet_instances(data);
	set_all_physics_shapes_enabled_for_area(true);

	// Shared spawn-data attachments (both bullet types). Slot vectors were
	// reset above and caches sized by set_up_bullet_instances, so this is safe
	// for pooled reuse with new data.
	apply_shared_bullet_attachment_from_data(data);

	set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures);

	move_to_front(); // Pooled instances render behind newer ones without this; moving to front emulates fresh spawn order.

	update_all_previous_transforms_for_interpolation();

	finalize_set_up(
			data.shared_bullets_custom_data,
			data.material,
			data.z_index,
			data.light_mask,
			data.visibility_layer,
			data.instance_shader_parameters);

	// Single-error policy: rebuild already reported; blank animation kept on failure.
	// (Frames were blanked in the prologue; connections scrubbed by the reset.)
	rebuild_sprite_animation(data.sprite_frames, data.animation);

	// Seeding-only subclass step. Unreachable in practice: wrong-type data was
	// rejected before any mutation (is_data_type_compatible). Defensive
	// fallback leaves a clean disabled instance for the pool instead of
	// half-seeded state: reset + deactivate fully blank what the reseed above
	// wrote, so the next correct enable starts from neutral.
	if (!custom_additional_enable_logic(data)) {
		reset_transient_volley_state(spawner_id, true);
		deactivate_volley();
		return false;
	}

	set_visible(true);

	// Mark all bullets as enabled in the sparse set (amount_bullets never changes)
	all_bullets_enabled_set.activate_all_data();
	is_active = true;
	return true;
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
			// set_* already reported the mismatch; fall back to zeros explicitly so
			// the vector can't be left half-cleared below by the size check inside.
			bullets_current_collision_count.clear();
			bullets_current_collision_count.resize(amount_bullets, 0);
		}
	}

	// Per-bullet custom data (same fallback rule as speed data; null entries
	// fall back to the shared value when read).
	all_bullets_custom_data.assign(amount_bullets, Ref<Resource>());
	if (data.all_bullets_custom_data.size() == amount_bullets) {
		for (int i = 0; i < amount_bullets; ++i) {
			all_bullets_custom_data[i] = data.all_bullets_custom_data[i];
		}
	} else if (data.all_bullets_custom_data.size() > 0) {
		Ref<Resource> first_custom_data = data.all_bullets_custom_data[0];
		for (int i = 0; i < amount_bullets; ++i) {
			all_bullets_custom_data[i] = first_custom_data;
		}
	}

	is_life_time_over_signal_enabled = data.is_life_time_over_signal_enabled;

	is_life_time_infinite = data.is_life_time_infinite;

	set_up_area(data.collision_layer, data.collision_mask, data.monitorable, bullet_factory != nullptr ? bullet_factory->physics_space : RID());

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

	// Bullets scatter across the whole level, but frustum culling is tested
	// once per MultiMeshInstance2D node against the MultiMesh AABB. Without a
	// custom AABB the box is single-quad-sized at the node origin, so zooming
	// a Camera2D in culls the ENTIRE volley the moment that tiny box leaves
	// the frustum. A huge box effectively disables culling for this node
	// (always drawn); overdraw stays cheap because disabled bullets write
	// scale-0 transforms. Must live here (not spawn()/ctor): generate_multimesh()
	// recreates the resource on every fresh spawn.
	// AABB is Vector3-based even for TRANSFORM_2D, so z must be non-degenerate.
	multi->set_custom_aabb(AABB(Vector3(-100000, -100000, -1000), Vector3(200000, 200000, 2000)));

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
	// Empty follows the same auto-resolve rules as spawn ("default" if present,
	// else the first animation with frames) instead of erroring.
	return rebuild_sprite_animation(p_sprite_frames, p_animation);
}

bool MultiMeshBullets2D::play_sprite_animation_name(const StringName &p_animation) {
	if (anim_source.is_null()) {
		UtilityFunctions::push_error("MultiMeshBullets2D play_sprite_animation_name: no SpriteFrames cached yet, call play_sprite_animation first.");
		return false;
	}
	// Empty follows the same auto-resolve rules as spawn.
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
		const Ref<Resource> &new_shared_bullets_custom_data,
		const Ref<Material> &new_material,
		int new_z_index,
		int new_light_mask,
		int new_visibility_layer,
		const Dictionary &new_instance_shader_parameters) {
	// Bullets custom data. Always assigned (null clears) so pool reuse never leaks
	// the previous owner's data into a new spawn.
	shared_bullets_custom_data = new_shared_bullets_custom_data;

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
		use_only_first_rotation_data = false;
		all_rotation_speed.clear();
		all_max_rotation_speed.clear();
		all_rotation_acceleration.clear();
		// The flag must follow the new data even when rotation is disabled:
		// otherwise a dead owner's texture/shape-follow mode leaks into the
		// next life (e.g. set_shared_bullet_rotation_data reuses this flag).
		rotate_only_textures = new_rotate_only_textures;
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
			use_only_first_rotation_data = false;
			all_rotation_speed.clear();
			all_max_rotation_speed.clear();
			all_rotation_acceleration.clear();
			rotate_only_textures = new_rotate_only_textures;
			return;
		}
		if (!Math::is_finite(entry->rotation_speed) || !Math::is_finite(entry->max_rotation_speed) || !Math::is_finite(entry->rotation_acceleration)) {
			UtilityFunctions::push_error("Non-finite rotation data at index " + String::num_int64(i) + ": rotation values must be finite. Ignoring all rotation data.");
			is_rotation_data_active = false;
			use_only_first_rotation_data = false;
			all_rotation_speed.clear();
			all_max_rotation_speed.clear();
			all_rotation_acceleration.clear();
			rotate_only_textures = new_rotate_only_textures;
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

	multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(transf));

	return transf;
}

void MultiMeshBullets2D::set_up_area(const int collision_layer, const int collision_mask, bool new_monitorable, const RID &physics_space) {
	monitorable = new_monitorable;
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_up_area: physics server or area is not ready, bullets will not collide.");
		return;
	}
	RID space_to_use = physics_space;
	if (!space_to_use.is_valid() && bullet_factory != nullptr) {
		space_to_use = bullet_factory->physics_space;
	}
	if (!space_to_use.is_valid()) {
		UtilityFunctions::push_error("set_up_area: no valid physics space, bullets will not collide. Set BulletFactory2D physics_space first.");
		return;
	}
	physics_server->area_set_space(area, space_to_use);
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
	if (eff < 0 || eff >= (int)all_cached_speed.size() || eff >= (int)all_cached_max_speed.size() || eff >= (int)all_cached_acceleration.size()) {
		return speed_data;
	}
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

	if (bullet_index < 0 || bullet_index >= (int)all_cached_speed.size() || bullet_index >= (int)all_cached_max_speed.size() || bullet_index >= (int)all_cached_acceleration.size() || bullet_index >= (int)all_cached_velocity.size() || bullet_index >= (int)all_cached_direction.size()) {
		return;
	}

	all_cached_speed[bullet_index] = new_bullet_speed_data->speed;
	all_cached_max_speed[bullet_index] = new_bullet_speed_data->max_speed;
	all_cached_acceleration[bullet_index] = new_bullet_speed_data->acceleration;
	all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * new_bullet_speed_data->speed + inherited_velocity_offset;
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
	if (eff < 0 || eff >= (int)all_cached_direction.size()) {
		return Vector2();
	}
	return all_cached_direction[eff];
}

void MultiMeshBullets2D::set_bullet_direction(int bullet_index, const Vector2 &new_direction) {
	if (!validate_bullet_index(bullet_index, "set_bullet_direction")) {
		return;
	}

	if (!new_direction.is_finite()) {
		UtilityFunctions::push_error("set_bullet_direction: new_direction must be finite, keeping the old direction.");
		return;
	}

	if (new_direction.length_squared() < 0.000001) {
		UtilityFunctions::push_error("set_bullet_direction: new_direction is zero, keeping the old direction.");
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
	if (bullet_index < 0 || bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_velocity.size() || bullet_index >= (int)all_cached_speed.size()) {
		return;
	}
	all_cached_direction[bullet_index] = new_direction.normalized();
	all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * all_cached_speed[bullet_index] + inherited_velocity_offset;
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
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return 0.0;
	}

	return all_cached_instance_transforms[bullet_index].get_rotation();
}

void MultiMeshBullets2D::set_bullet_texture_rotation_radians(int bullet_index, real_t new_rotation_radians) {
	if (!validate_bullet_index(bullet_index, "set_bullet_texture_rotation_radians")) {
		return;
	}
	if (!Math::is_finite(new_rotation_radians)) {
		UtilityFunctions::push_error("set_bullet_texture_rotation_radians: rotation must be finite, keeping the old rotation.");
		return;
	}
	if (bullet_index >= (int)all_cached_instance_transforms.size()) {
		return;
	}

	auto &curr_transf = all_cached_instance_transforms[bullet_index];
	// Absolute visual rotation: the volley-wide texture offset is part of
	// the instance basis (spawn path bakes it in), so writing absolute
	// would double-count it in adjust_direction_based_on_rotation (which
	// strips exactly one offset). Compose like the towards_position setter.
	curr_transf.set_rotation(new_rotation_radians + cache_texture_rotation_radians);
	sync_shape_transform_from_instance(bullet_index, curr_transf);

	// Instantly apply the updated transform so paused factories don't render stale visuals.
	if (all_bullets_enabled_set.contains(bullet_index)) {
		multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(curr_transf));
	}

	// Stick-relative attachments follow the rotation like set_bullet_transform
	// does: without this they sit at the old angle until the next tick (and
	// forever while paused).
	carry_attachment_with_transform(bullet_index, curr_transf, Vector2(0, 0));

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
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return 0.0;
	}

	return Math::rad_to_deg(all_cached_instance_transforms[bullet_index].get_rotation());
}

void MultiMeshBullets2D::set_bullet_texture_rotation_degrees(int bullet_index, real_t new_rotation_degrees) {
	if (!validate_bullet_index(bullet_index, "set_bullet_texture_rotation_degrees")) {
		return;
	}
	if (!Math::is_finite(new_rotation_degrees)) {
		UtilityFunctions::push_error("set_bullet_texture_rotation_degrees: rotation must be finite, keeping the old rotation.");
		return;
	}
	if (bullet_index >= (int)all_cached_instance_transforms.size()) {
		return;
	}

	auto &curr_transf = all_cached_instance_transforms[bullet_index];
	curr_transf.set_rotation(Math::deg_to_rad(new_rotation_degrees) + cache_texture_rotation_radians);
	sync_shape_transform_from_instance(bullet_index, curr_transf);

	// Instantly apply the updated transform so paused factories don't render stale visuals.
	if (all_bullets_enabled_set.contains(bullet_index)) {
		multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(curr_transf));
	}

	carry_attachment_with_transform(bullet_index, curr_transf, Vector2(0, 0));

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
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return Transform2D();
	}

	// Caches are global; return the cache directly so get()/set() round-trip in
	// the same space. Use get_bullet_global_transform() for an explicit world read.
	return all_cached_instance_transforms[bullet_index];
}

Transform2D MultiMeshBullets2D::get_bullet_global_transform(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_global_transform")) {
		return Transform2D();
	}
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return Transform2D();
	}

	return all_cached_instance_transforms[bullet_index];
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
	if (!new_transform.get_origin().is_finite() || !Math::is_finite(new_transform.get_rotation()) || !new_transform.get_scale().is_finite()) {
		UtilityFunctions::push_error("set_bullet_transform: new_transform must be finite, keeping the old transform.");
		return;
	}
	// A degenerate (near-zero) scale collapses columns[0] to zero, which silently
	// zeroes the movement direction wherever it is derived from the transform
	// (adjust_direction_based_on_rotation tick path -> velocity falls back to
	// the inherited offset only). Reject instead of storing a poisoned basis.
	if (new_transform.get_scale().length_squared() < 0.00000001) {
		UtilityFunctions::push_error("set_bullet_transform: scale must be non-zero, keeping the old transform.");
		return;
	}
	if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
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
		multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(curr_bullet_transf));
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

	// Update direction if requested. A direction curve owns the direction, so
	// skip just this part (the transform itself is still applied above).
	if (set_direction_based_on_transform) {
		bool direction_owned_by_curve = shared_bullet_curves_data.is_valid() && (shared_bullet_curves_data->x_direction_curve.is_valid() || shared_bullet_curves_data->y_direction_curve.is_valid());
		BulletCurvesData2D *transform_curves_data = direction_owned_by_curve ? nullptr : find_bullet_curves_data_ptr(bullet_index);
		if (transform_curves_data != nullptr && (transform_curves_data->x_direction_curve.is_valid() || transform_curves_data->y_direction_curve.is_valid())) {
			direction_owned_by_curve = true;
		}
		if (direction_owned_by_curve) {
			UtilityFunctions::push_warning("set_bullet_transform was asked to derive the direction while a direction curve is assigned. The curve owns the direction, so it was left alone. Set the curve to null first if you want the transform to steer.");
		} else {
			// Strip the volley-wide texture offset like the tick's adjust
			// path does: the instance basis carries it, the logical
			// direction must not.
			Vector2 new_direction = Vector2(1, 0).rotated(curr_bullet_transf.get_rotation() - cache_texture_rotation_radians);
			int eff = (all_cached_direction.size() == 1) ? 0 : bullet_index;
			if (eff >= 0 && eff < (int)all_cached_direction.size() && eff < (int)all_cached_velocity.size() && eff < (int)all_cached_speed.size()) {
				all_cached_direction[eff] = new_direction.normalized();
				all_cached_velocity[eff] = all_cached_direction[eff] * all_cached_speed[eff] + inherited_velocity_offset;
			}
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
	if (!target_position.is_finite()) {
		UtilityFunctions::push_error("set_bullet_direction_towards_position: target_position must be finite.");
		return;
	}

	if (shared_bullet_curves_data.is_valid() && (shared_bullet_curves_data->x_direction_curve.is_valid() || shared_bullet_curves_data->y_direction_curve.is_valid())) {
		UtilityFunctions::push_warning("You are trying to set bullet direction directly while having a direction curve assigned to the shared curves data. The curve will override any direct direction changes. Set the curve to null first if you want to set direction directly.");
		return;
	}

	BulletCurvesData2D *towards_curves_data = find_bullet_curves_data_ptr(bullet_index);

	if (towards_curves_data != nullptr && (towards_curves_data->x_direction_curve.is_valid() || towards_curves_data->y_direction_curve.is_valid())) {
		UtilityFunctions::push_warning("You are trying to set bullet direction directly while having a direction curve assigned to the individual curves data. The curve will override any direct direction changes. Set the curve to null first if you want to set direction directly.");
		return;
	}

	int eff = (all_cached_direction.size() == 1) ? 0 : bullet_index;
	if (eff < 0 || eff >= (int)all_cached_direction.size() || eff >= (int)all_cached_velocity.size() || eff >= (int)all_cached_speed.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
		return;
	}
	const Vector2 to_target = target_position - all_cached_instance_origin[bullet_index];
	if (to_target.length_squared() < 0.000001) {
		UtilityFunctions::push_error("set_bullet_direction_towards_position: bullet is already at the target position, keeping the old direction.");
		return;
	}
	all_cached_direction[eff] = to_target.normalized();
	all_cached_velocity[eff] = all_cached_direction[eff] * all_cached_speed[eff] + inherited_velocity_offset;
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
	if (!target_position.is_finite()) {
		UtilityFunctions::push_error("set_bullet_direction_towards_node2d: target position must be finite.");
		return;
	}
	if (shared_bullet_curves_data.is_valid() && (shared_bullet_curves_data->x_direction_curve.is_valid() || shared_bullet_curves_data->y_direction_curve.is_valid())) {
		UtilityFunctions::push_warning("You are trying to set bullet direction directly while having a direction curve assigned to the shared curves data. The curve will override any direct direction changes. Set the curve to null first if you want to set direction directly.");
		return;
	}

	BulletCurvesData2D *towards_node_curves_data = find_bullet_curves_data_ptr(bullet_index);

	if (towards_node_curves_data != nullptr && (towards_node_curves_data->x_direction_curve.is_valid() || towards_node_curves_data->y_direction_curve.is_valid())) {
		UtilityFunctions::push_warning("You are trying to set bullet direction directly while having a direction curve assigned to the individual curves data. The curve will override any direct direction changes. Set the curve to null first if you want to set direction directly.");
		return;
	}
	int eff = (all_cached_direction.size() == 1) ? 0 : bullet_index;
	if (eff < 0 || eff >= (int)all_cached_direction.size() || eff >= (int)all_cached_velocity.size() || eff >= (int)all_cached_speed.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
		return;
	}
	const Vector2 to_node = target_position - all_cached_instance_origin[bullet_index];
	if (to_node.length_squared() < 0.000001) {
		UtilityFunctions::push_error("set_bullet_direction_towards_node2d: bullet is already at the target position, keeping the old direction.");
		return;
	}
	all_cached_direction[eff] = to_node.normalized();
	all_cached_velocity[eff] = all_cached_direction[eff] * all_cached_speed[eff] + inherited_velocity_offset;
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
	if (!target_position.is_finite()) {
		UtilityFunctions::push_error("set_bullet_texture_rotation_towards_position: target_position must be finite.");
		return;
	}
	if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
		return;
	}

	Transform2D &transf = all_cached_instance_transforms[bullet_index];

	Vector2 pos = all_cached_instance_origin[bullet_index];
	Vector2 dir = (target_position - pos).normalized();
	real_t angle = Math::atan2(dir.y, dir.x);

	// Compose with the volley-wide texture offset like the spawn path does
	// (generate_texture_transform adds cache_texture_rotation_radians):
	// without it the visual faces the target while the physics shape
	// (synced with -cache stripped) sits off by exactly the offset, and
	// adjust_direction_based_on_rotation re-derives a wrong direction.
	Vector2 scale = transf.get_scale();
	transf.set_rotation_and_scale(angle + cache_texture_rotation_radians, scale);
	transf.set_origin(pos);
	sync_shape_transform_from_instance(bullet_index, transf);

	// Only write the multimesh slot for ENABLED bullets: a disabled slot holds the
	// zero transform that hides it, and writing a real transform here would
	// resurrect the visual for a frame (or permanently on a paused factory).
	if (all_bullets_enabled_set.contains(bullet_index)) {
		multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(transf));
	}

	carry_attachment_with_transform(bullet_index, transf, Vector2(0, 0));

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
	// NaN/Inf here would poison every curve sample (speed/rotation/direction) with no
	// recovery, so reject non-finite time like the other movement setters do.
	if (!Math::is_finite(new_time) || new_time < 0.0) {
		UtilityFunctions::push_error("set_curves_elapsed_time: new_time must be a finite value >= 0.");
		return;
	}
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

	// Single error for the whole range instead of one per bullet below.
	if (is_class("BlockBullets2D")) {
		UtilityFunctions::push_error("BlockBullets2D does not support movement patterns - use DirectionalBullets2D for patterned movement.");
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
	// Frees/recreates server RIDs and re-buckets the pool: unsafe while the
	// factory iterates bullet state or inside any physics frame (server flush
	// locks apply). Same contract as the factory structural methods.
	if (bullet_factory != nullptr && bullet_factory->is_structural_mutation_unsafe()) {
		UtilityFunctions::push_error("set_collision_shape_runtime cannot run while bullets are being processed or inside a physics frame (e.g. inside directional_area_entered/block_body_entered handlers). Use call_deferred() to run this after the physics step.");
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
		// Push the new size/type data to the server shape. The returned transform
		// is intentionally discarded: the cached shape transform below is derived
		// through the sync helper so rotate_only_textures and the texture-rotation
		// strip stay consistent with the tick and teleport paths.
		(void)generate_collision_shape_transform_for_area(all_cached_instance_transforms[i], physics_shapes[i], cache_collision_shape_offset, i);
		sync_shape_transform_from_instance(i, all_cached_instance_transforms[i]);
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
	// An unpooled instance with pooling off keeps its new key cached but stays
	// out of every bucket on purpose: it is only reusable through a direct
	// enable_multimesh() (which reads the live key) or free_disabled_bullets().
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
	// Same clamp as enable_bullet()'s wake top-up: values at/above max leave
	// exactly one hit remaining (max - 1). Storing max itself would pin the
	// bullet at the kill threshold so the next handle_bullet_collision() hit
	// disables it immediately - inconsistent with a wake with the same amount.
	if (value < 0) {
		bullets_current_collision_count[bullet_index] = 0;
	} else if (bullet_max_collision_count > 0 && value >= bullet_max_collision_count) {
		bullets_current_collision_count[bullet_index] = bullet_max_collision_count - 1;
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
	ClassDB::bind_method(D_METHOD("_do_deferred_bullet_disable_attachment", "bullet_index", "expected_generation", "expected_attachment_id", "expected_attachment"), &MultiMeshBullets2D::_do_deferred_bullet_disable_attachment);
	ClassDB::bind_method(D_METHOD("_do_emit_life_time_over", "expected_generation", "emitter_instance_id", "signal_name", "bullet_indexes"), &MultiMeshBullets2D::_do_emit_life_time_over);
	ClassDB::bind_method(D_METHOD("_do_emit_sprite_animation_finished", "expected_generation"), &MultiMeshBullets2D::_do_emit_sprite_animation_finished);

	ClassDB::bind_method(D_METHOD("get_amount_bullets"), &MultiMeshBullets2D::get_amount_bullets);

	// Base-class methods only (no ADD_PROPERTY here): DirectionalBullets2D binds its
	// own versions and exposes the "inherited_velocity_offset" property; these binds
	// make the getters/setters reachable on MultiMeshBullets2D/BlockBullets2D too.
	ClassDB::bind_method(D_METHOD("get_inherited_velocity_offset"), &MultiMeshBullets2D::get_inherited_velocity_offset);
	ClassDB::bind_method(D_METHOD("set_inherited_velocity_offset", "new_offset"), &MultiMeshBullets2D::set_inherited_velocity_offset);

	ClassDB::bind_method(D_METHOD("get_all_bullets_status"), &MultiMeshBullets2D::get_all_bullets_status);
	ClassDB::bind_method(D_METHOD("is_bullet_status_enabled", "bullet_index"), &MultiMeshBullets2D::is_bullet_status_enabled);

	ClassDB::bind_method(D_METHOD("get_shared_bullets_custom_data"), &MultiMeshBullets2D::get_shared_bullets_custom_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullets_custom_data", "new_shared_bullets_custom_data"), &MultiMeshBullets2D::set_shared_bullets_custom_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullets_custom_data"), "set_shared_bullets_custom_data", "get_shared_bullets_custom_data");

	ClassDB::bind_method(D_METHOD("bullet_get_custom_data", "bullet_index"), &MultiMeshBullets2D::bullet_get_custom_data);
	ClassDB::bind_method(D_METHOD("bullet_set_custom_data", "bullet_index", "new_custom_data"), &MultiMeshBullets2D::bullet_set_custom_data);
	ClassDB::bind_method(D_METHOD("all_bullets_get_custom_data", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_get_custom_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_custom_data", "new_custom_data", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_custom_data, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_is_life_time_infinite"), &MultiMeshBullets2D::get_is_life_time_infinite);
	ClassDB::bind_method(D_METHOD("set_is_life_time_infinite", "value"), &MultiMeshBullets2D::set_is_life_time_infinite);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_life_time_infinite"), "set_is_life_time_infinite", "get_is_life_time_infinite");

	// Time based functions
	ClassDB::bind_method(D_METHOD("multimesh_attach_time_based_function", "time", "callable", "repeat", "execute_only_if_multimesh_is_active"), &MultiMeshBullets2D::multimesh_attach_time_based_function, DEFVAL(false), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("_do_attach_time_based_function", "time", "callable", "repeat", "execute_only_if_multimesh_is_active", "expected_timers_generation"), &MultiMeshBullets2D::_do_attach_time_based_function);

	ClassDB::bind_method(D_METHOD("multimesh_detach_time_based_function", "callable"), &MultiMeshBullets2D::multimesh_detach_time_based_function);
	ClassDB::bind_method(D_METHOD("_do_detach_time_based_function", "callable", "expected_timers_generation"), &MultiMeshBullets2D::_do_detach_time_based_function);

	ClassDB::bind_method(D_METHOD("multimesh_detach_all_time_based_functions"), &MultiMeshBullets2D::multimesh_detach_all_time_based_functions);
	ClassDB::bind_method(D_METHOD("_do_detach_all_time_based_functions", "expected_timers_generation"), &MultiMeshBullets2D::_do_detach_all_time_based_functions);

	ClassDB::bind_method(D_METHOD("_do_execute_stored_callable_safely", "_callback", "_execute_only_if_multimesh_is_active", "expected_timers_generation"), &MultiMeshBullets2D::_do_execute_stored_callable_safely);

	ClassDB::bind_method(D_METHOD("get_is_multimesh_auto_pooling_enabled"), &MultiMeshBullets2D::get_is_multimesh_auto_pooling_enabled);
	ClassDB::bind_method(D_METHOD("set_is_multimesh_auto_pooling_enabled", "value"), &MultiMeshBullets2D::set_is_multimesh_auto_pooling_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_multimesh_auto_pooling_enabled"), "set_is_multimesh_auto_pooling_enabled", "get_is_multimesh_auto_pooling_enabled");

	ClassDB::bind_method(D_METHOD("get_is_attachments_auto_pooling_enabled"), &MultiMeshBullets2D::get_is_attachments_auto_pooling_enabled);
	ClassDB::bind_method(D_METHOD("set_is_attachments_auto_pooling_enabled", "value"), &MultiMeshBullets2D::set_is_attachments_auto_pooling_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_attachments_auto_pooling_enabled"), "set_is_attachments_auto_pooling_enabled", "get_is_attachments_auto_pooling_enabled");

	ClassDB::bind_method(D_METHOD("reset_pooling_flags_to_default"), &MultiMeshBullets2D::reset_pooling_flags_to_default);

	// Collision
	ClassDB::bind_method(D_METHOD("get_bullet_max_collision_count"), &MultiMeshBullets2D::get_bullet_max_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullet_max_collision_count", "value"), &MultiMeshBullets2D::set_bullet_max_collision_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bullet_max_collision_count"), "set_bullet_max_collision_count", "get_bullet_max_collision_count");

	ClassDB::bind_method(D_METHOD("get_bullet_collision_count", "bullet_index"), &MultiMeshBullets2D::get_bullet_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullet_collision_count", "bullet_index", "value"), &MultiMeshBullets2D::set_bullet_collision_count);
	ClassDB::bind_method(D_METHOD("get_bullets_current_collision_count"), &MultiMeshBullets2D::get_bullets_current_collision_count);
	ClassDB::bind_method(D_METHOD("set_bullets_current_collision_count_no_return", "arr"), &MultiMeshBullets2D::set_bullets_current_collision_count_no_return);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bullets_current_collision_count", PROPERTY_HINT_ARRAY_TYPE, "int"), "set_bullets_current_collision_count_no_return", "get_bullets_current_collision_count");

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
	ClassDB::bind_method(D_METHOD("all_bullets_set_attachment", "attachment_scene", "bullet_attachment_offset", "stick_relative_to_bullet", "bullet_index_start", "bullet_index_end_inclusive"), &MultiMeshBullets2D::all_bullets_set_attachment, DEFVAL(true), DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("bullet_set_attachment", "bullet_index", "attachment_scene", "bullet_attachment_offset", "stick_relative_to_bullet"), &MultiMeshBullets2D::bullet_set_attachment, DEFVAL(Vector2(0, 0)), DEFVAL(true));

	ClassDB::bind_method(D_METHOD("set_shared_bullet_curves_data", "data"), &MultiMeshBullets2D::set_shared_bullet_curves_data);
	ClassDB::bind_method(D_METHOD("get_shared_bullet_curves_data"), &MultiMeshBullets2D::get_shared_bullet_curves_data);
	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "shared_bullet_curves_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletCurvesData2D"),
			"set_shared_bullet_curves_data", "get_shared_bullet_curves_data");

	ClassDB::bind_method(D_METHOD("has_shared_bullet_curves_data"), &MultiMeshBullets2D::has_shared_bullet_curves_data);
	ClassDB::bind_method(D_METHOD("remove_shared_bullet_curves_data"), &MultiMeshBullets2D::remove_shared_bullet_curves_data);

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

	// NOTE: signal args use PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) so the
	// class name reaches ClassDB and --doctool; see the note on the factory
	// signals.
	ADD_SIGNAL(MethodInfo("sprite_animation_finished",
			PropertyInfo(Variant::OBJECT, "multimesh_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "MultiMeshBullets2D")));
}

// WP-F: cold paths moved out of the header (per-tick hot paths stay inline).
// Bodies are unchanged; only the location moved.

void MultiMeshBullets2D::_do_deferred_bullet_disable_attachment(int bullet_index, int expected_generation, uint64_t expected_attachment_id, BulletAttachment2D *expected_attachment) {
	if (expected_generation != multimesh_generation) {
		return;
	}
	if (!slot_still_holds_attachment(bullet_index, expected_attachment, expected_attachment_id)) {
		return;
	}
	bullet_disable_attachment(bullet_index);
}

void MultiMeshBullets2D::_do_emit_life_time_over(int expected_generation, uint64_t emitter_instance_id, const StringName &signal_name, const TypedArray<int> &bullet_indexes) {
	if (expected_generation != multimesh_generation) {
		return;
	}
	if (bullet_indexes.is_empty()) {
		return;
	}
	Object *emitter = ObjectDB::get_instance(ObjectID(emitter_instance_id));
	if (emitter == nullptr) {
		return;
	}
	// Re-ownership guard: the volley may have been handed to a different
	// owner without a generation bump (adopt_live_volley re-stamps only).
	// Never deliver the old life's expiry to the new owner, and never
	// deliver a spawner life's expiry to the factory. A cleared tag
	// (owner == 0 after the expiry pooled the instance) still belongs to
	// the captured emitter, so only a *different live owner* drops here.
	if (owner_spawner_id != 0 && owner_spawner_id != emitter_instance_id) {
		return;
	}
	emitter->emit_signal(signal_name, this, bullet_indexes);
}

void MultiMeshBullets2D::_do_emit_sprite_animation_finished(int expected_generation) {
	if (expected_generation != multimesh_generation) {
		return;
	}
	if (!anim_finished) {
		return;
	}
	emit_signal("sprite_animation_finished", this);
}


// WP-F: cold paths moved out of the header (per-tick hot paths stay inline).
// Bodies are unchanged; only the location moved.

void MultiMeshBullets2D::reduce_lifetime(double delta) {
		if (!Math::is_finite(delta) || delta < 0.0) {
			return;
		}
		curves_elapsed_time += delta;

		// If the lifetime is infinite there is no lifetime timer
		if (is_life_time_infinite) {
			return;
		}

		// Life time timer logic
		current_life_time -= delta;

		// The bullets still have life time left, so don't do anything yet
		if (current_life_time > 0) {
			return;
		}

		std::vector<int> active_copy = all_bullets_enabled_set.get_active_indexes();

		// If the life_time_over signal is not enabled, we can just disable all bullets right away and skip the additional logic
		if (!is_life_time_over_signal_enabled) {
			for (int i : active_copy) {
				if (!all_bullets_enabled_set.contains(i)) {
					continue;
				}
				disable_bullet(i, true);
			}

			return;
		}

		// If the life_time_over signal is enabled - collect indexes, disable bullets immediately (consistent with collision path),
		// but keep attachment disable and signal deferred so handler can still access attachment.
		// Full-volley expiry additionally detaches the survivors from the disable
		// sweep: the last disable_bullet() funnels into disable_multimesh(),
		// whose sweep would otherwise pool every attachment BEFORE the deferred
		// signal fires (handler would see nullptr). Detaching first keeps the
		// slots alive for the signal; the deferred disables re-pool them after.
		TypedArray<int> bullet_indexes;

		// Snapshot the signal owner BEFORE the disable loop below: a full-volley
		// expiry funnels into disable_multimesh(), which clears owner_spawner_id
		// and pools the instance. Resolving after would schedule the factory's
		// life_time_over signal for a spawner-owned volley.
		Object *lifetime_emitter = resolve_signal_emitter();

		// Caches already hold global-space transforms (spawn data is global and all
		// movement/homing math is global), so no get_global_transform() compose is
		// needed anywhere transforms are read (handlers use get_bullet_global_transform()).
		for (int i : active_copy) {
			if (!all_bullets_enabled_set.contains(i)) {
				continue;
			}
			bullet_indexes.push_back(i);
			disable_bullet(i, false); // immediate shape disable, keep attachment for signal
		}

		// The sweep above pooled every attachment when the last bullet went out
		// (full-volley expiry always ends there). Reclaim them into the slots so
		// the deferred signal below observes live attachments again; the
		// deferred per-bullet disables queued below return them afterwards.
		// Partial expiry never reaches the sweep, so this is a no-op there
		// (slots still hold their attachments). Popping is LIFO-safe here:
		// the sweep just pushed these exact instances, so the bucket top is
		// ours unless a re-entrant handler stole it (then the slot stays
		// null and the handler owns that attachment - no double-claim).
		// Reclaimed slots re-enter the enabled state: without on_bullet_enable
		// + a transform sync the handler would observe a disabled-state node,
		// and the deferred disable below would fire on_bullet_disable a
		// second time with no enable in between.
		if (!is_active && bullet_indexes.size() > 0 && bullet_factory != nullptr) {
			// Self-liveness token (same pattern as handle_bullet_collision):
			// call_on_bullet_enable() below runs user code that may free this
			// volley; every later member access would then be use-after-free.
			const uint64_t reclaim_self_id = get_instance_id();
			for (int k = 0; k < bullet_indexes.size(); ++k) {
				// A previous iteration's callback may have freed this volley:
				// bail before touching members (ObjectDB validates without
				// touching the object). Remaining slots stay pooled; the
				// deferred signal below is skipped with them.
				if (ObjectDB::get_instance(ObjectID(reclaim_self_id)) != this) {
					bullet_indexes.clear();
					break;
				}
				const int idx = (int)bullet_indexes[k];
				if (idx < 0 || idx >= amount_bullets || idx >= (int)attachments.size() || idx >= (int)attachment_pooling_ids.size()) {
					continue;
				}
				if (attachments[idx] != nullptr) {
					continue;
				}
				const uint32_t pooling_id = attachment_pooling_ids[idx];
				if (pooling_id == 0) {
					continue;
				}
			BulletAttachment2D *candidate = bullet_factory->bullet_attachments_pool.pop(pooling_id);
			if (candidate != nullptr) {
				// Identity guard shared with the attach path (null expected
				// scene here: verified as genuine bucket membership instead
				// of an exact scene match - see the helper's note).
				if (!is_popped_attachment_from_scene(candidate, Ref<PackedScene>(), pooling_id)) {
					bullet_factory->bullet_attachments_pool.push(candidate, pooling_id);
				} else {
						attachments[idx] = candidate;
						candidate->owner_multimesh_id = get_instance_id();
						candidate->owner_bullet_index = idx;
						if (idx >= 0 && idx < (int)all_cached_instance_transforms.size()) {
							attachment_transforms[idx] = calculate_attachment_global_transf(idx, all_cached_instance_transforms[idx]);
							candidate->set_global_transform(attachment_transforms[idx]);
							candidate->reset_physics_interpolation();
							if (idx >= 0 && idx < (int)all_previous_attachment_transf.size()) {
								all_previous_attachment_transf[idx] = attachment_transforms[idx];
							}
						}
						candidate->call_on_bullet_enable();
					}
				}
			}
		}

	if (bullet_indexes.size() > 0) {
		// Emit deferred so user code runs outside physics step. Guarded by
		// spawn generation (not just emitter validity): expiry queues the
		// emit, then a same-frame pool reuse hands this instance to a new
		// owner before the flush. The bare call_deferred("emit_signal")
		// would then deliver the OLD life's indexes to the NEW life.
		// Uses the pre-disable snapshot above, never a post-disable resolve.
		Object *emitter = lifetime_emitter;
		if (emitter != nullptr) {
			const uint64_t emitter_id = emitter->get_instance_id();
			if (emitter == bullet_factory) {
				const char *signal_name = is_class("BlockBullets2D") ? "block_life_time_over" : "directional_life_time_over";
				call_deferred("_do_emit_life_time_over", multimesh_generation, emitter_id, StringName(signal_name), bullet_indexes);
			} else {
				call_deferred("_do_emit_life_time_over", multimesh_generation, emitter_id, StringName("life_time_over"), bullet_indexes);
			}
		}

		// Disable attachments after signal (deferred keeps order)
		for (int i = 0; i < bullet_indexes.size(); ++i) {
			int idx = bullet_indexes[i];
			BulletAttachment2D *queued_attachment = attachments[idx];
			const uint64_t queued_attachment_id = queued_attachment != nullptr ? queued_attachment->get_instance_id() : 0;
			call_deferred("_do_deferred_bullet_disable_attachment", idx, multimesh_generation, queued_attachment_id, queued_attachment);
		}
	}
	}

void MultiMeshBullets2D::enable_bullet(int bullet_index, int collision_amount, bool should_enable_attachment) {
		// Wake semantics (contract, keep in sync with the doc XML):
		// "resume, not respawn" for ballistics (speed/direction/velocity/
		// position), appearance, custom data and per-bullet movement state -
		// there is no spawn data to reseed from. Two deliberate exceptions:
		// (1) an expiry-pooled wake tops up the whole volley's lifetime AND
		// rewinds the curve clock together (curves sample
		// curves_elapsed_time/max_life_time, so one without the other would
		// pin curves at their end sample); (2) per-bullet homing queues and
		// orbit state are NOT resumed - disable_bullet() clears them, so
		// re-push targets and re-enable orbit after the wake.
		// Cross-owner reuse must go through spawn_*()/enable_multimesh(),
		// which reseed everything from fresh data.
		if (!validate_bullet_index(bullet_index, "enable_bullet")) {
			return;
		}
		// Waking a volley that is queued for deletion would reactivate,
		// re-pool-unlink, and re-register an instance that dies at the end
		// of the frame. Refuse: queue_free() is terminal.
		if (is_queued_for_deletion()) {
			UtilityFunctions::push_error("enable_bullet: multimesh is queued for deletion.");
			return;
		}
		if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)bullets_current_collision_count.size()) {
			return;
		}
		if (multi.is_null() || !multi.is_valid()) {
			return;
		}
		if (physics_server == nullptr || !area.is_valid()) {
			return;
		}

		const bool curr_bullet_status = all_bullets_enabled_set.contains(bullet_index);

		// If the bullet is already enabled, just return
		if (curr_bullet_status) {
			return;
		}

		// A wake from inside the disable sweep (on_bullet_disable handler) would
		// resurrect the volley while _disable_multimesh_internal() is tearing it
		// down; the sweep aborts on wake now, but the factory also drops the
		// re-registration (reactivate fails under the busy flag), leaving a live
		// volley the factory never ticks. Reject here so the wake is explicit
		// (call_deferred) instead of silently frozen.
		if (bullet_factory != nullptr && bullet_factory->get_is_factory_busy()) {
			UtilityFunctions::push_error("enable_bullet: cannot wake a bullet while the factory is busy (e.g. inside an on_bullet_disable handler during the disable sweep). Use call_deferred to wake after the sweep.");
			return;
		}

		// The attachment callback below runs user code that may re-enter
		// enable_bullet()/disable_bullet() on this volley. Claim the slot (set
		// + counter) BEFORE it runs, and reject nested enable/disable calls
		// while the latch is held, so the counter can never drift vs the
		// sparse set (activate_data dedups, the counter does not).
		if (_bullet_enable_depth > 0) {
			UtilityFunctions::push_error("enable_bullet: re-entrant call from inside on_bullet_enable is not allowed. Use call_deferred to change bullet state from that callback.");
			return;
		}
		ReentrancyGuard bullet_enable_guard(_bullet_enable_depth);

		// Full-volley wake coming: the generations must bump BEFORE the
		// attachment callback below (user on_bullet_enable code) runs, so work
		// it queues is stamped with the new life instead of being invalidated
		// right after. Deferred work from the dead life is correctly dropped.
		// Timers are dropped outright here (not just bumped): disable only
		// detaches on the full-kill path, so a disable→attach→wake sequence
		// would otherwise hand the new life timers armed while pooled.
		const bool will_reactivate_volley = !is_active;
		if (will_reactivate_volley) {
			++multimesh_generation;
			++multimesh_timers_generation;
			multimesh_custom_timers.clear();
		}

		all_bullets_enabled_set.activate_data(bullet_index);
		++active_bullets_counter;
		if (active_bullets_counter > amount_bullets) {
			active_bullets_counter = amount_bullets;
		}

		// A wake is a new life for this slot: stale queued hits from before
		// the disable must not fire now (same-overlap double count).
		bump_collision_epoch_for_bullet(bullet_index);

		multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(all_cached_instance_transforms[bullet_index])); // Start rendering the instance

		physics_server->area_set_shape_disabled(area, bullet_index, false);

		// Woken bullets must not lerp from a stale pre-disable position.
		// Every other teleport-sentenced path syncs prev==curr; do the same.
		update_bullet_previous_transform_for_interpolation(bullet_index);

		auto &current_bullet_collision_amount = bullets_current_collision_count[bullet_index];

		// collision_amount is how many hits the bullet has already taken: 0 means fresh
		// (full hits remaining). Clamp into range so re-enabling can't grant extra hits
		// or kill the bullet one hit early: values at/above max leave exactly one
		// hit remaining (max - 1), same as set_bullet_collision_count().
		if (collision_amount < 0) {
			current_bullet_collision_amount = 0;
		} else if (bullet_max_collision_count > 0 && collision_amount >= bullet_max_collision_count) {
			current_bullet_collision_amount = bullet_max_collision_count - 1;
		} else {
			current_bullet_collision_amount = collision_amount;
		}

		if (should_enable_attachment) {
			bullet_enable_attachment(bullet_index);
		}

		if (!is_active) {
			// Waking a fully pooled multimesh outside the factory pop path: drop it from
			// the pool first, otherwise the next pop() would hand out this live instance
			// to a second owner while the first still drives it. The factory also resumes
			// processing it so woken bullets actually move.
			// Generations were already bumped above (before user callbacks), so
			// deferred work from the dead life is gone and anything queued from
			// here on belongs to this new life.
			// Only a foreign life (actually pooled) carries the previous
			// Only a foreign life (actually pooled) carries the previous
			// owner's volley-wide signal connections. A same-owner revive of
			// a drained-but-unpooled volley must keep them: disconnecting here
			// would silence the whole volley because of one bullet's wake.
			// try_remove_instance returns false when pooling is off or the
			// instance was never pooled - both mean same-owner revive.
			const bool was_pooled = (bullets_pool != nullptr) && bullets_pool->try_remove_instance(this, get_pool_key());
			if (bullet_factory != nullptr) {
				bullet_factory->reactivate_multimesh_instance(*this);
			}
		// A pooled instance carries the previous owner's signal connections; they
		// must not fire for this wake (same cleanup the pool-pop enable does).
		// Scoped to foreign lives only (see was_pooled above): same-owner
		// revives skip the disconnects so sibling notifications survive.
		if (was_pooled) {
			disconnect_sprite_animation_connections();
			// Same for the previous owner's homing forward: without this, the old
			// spawner would keep retargeting a volley someone else woke manually.
			// Guarded by has_signal so BlockBullets2D (no homing signal) no-ops.
			if (has_signal("bullet_homing_target_reached")) {
				for (const Dictionary &connection : get_signal_connection_list("bullet_homing_target_reached")) {
					const Callable callable = connection["callable"];
					disconnect("bullet_homing_target_reached", callable);
				}
			}
			// Fail-safe neutral subset: the queue_free-vs-pool decision and the
			// rotation drive must not follow a dead owner into the new life.
			// Pooling flags reset to default (a foreign wake must never inherit
			// "don't pool" and strand itself, nor "pool" against the new owner's
			// wishes — set them explicitly after the wake if needed). Rotation
			// speeds are cleared (stale spin would steer the new life with no
			// data behind it). Ballistics, appearance, custom data, collision
			// counts/max, lifetime and shape state resume by design (warned
			// below): reseed via spawn_*/enable_multimesh for a clean slate.
			reset_pooling_flags_to_default();
			set_rotation_data(TypedArray<BulletRotationData2D>(), rotate_only_textures);
		}
		// Ownership restarts from scratch: whoever woke this re-stamps if it
		// is a spawner (see BulletSpawner2D::adopt_live_volley). Keeping the
		// old id would let a foreign spawner steer manual wakes.
		owner_spawner_id = 0;
		// A foreign pooled wake is a new owner with partially stale state:
		// ballistics, appearance, custom data, collision counts/max, lifetime
		// and shape state still hold the previous owner's values (only the
		// woken slot's collision count was reseeded above). Pooling flags,
		// rotation, signals and ownership were neutralized above. Same-owner
		// revives resume everything by design; foreign wakes must reseed
		// through spawn_*/enable_multimesh or adopt_live_volley + manual
		// re-push, so warn once per wake instead of driving silently stale.
		if (was_pooled) {
			UtilityFunctions::push_warning("enable_bullet: woke a pooled volley from the pool outside spawn_*/enable_multimesh. Ballistics, appearance, custom data, collision counts, lifetime and shape state still hold the previous owner's values: reseed them (or adopt_live_volley + re-push homing/orbit) before relying on this volley.");
		}
			// An expiry-pooled wake would otherwise die again on the next tick with an
			// exhausted timer. Only top it up when expired; manual-disable wakes keep
			// their remaining lifetime untouched. Both clocks restart together:
			// curves sample curves_elapsed_time/max_life_time, so topping up one
			// without the other would pin curves at their end sample (1.0).
			if (!is_life_time_infinite && current_life_time <= 0.0) {
				current_life_time = max_life_time;
				curves_elapsed_time = 0.0;
			}
			is_active = true;
			set_visible(true);
			// Keep the interpolator consistent for the whole volley: disabled
			// bullets are not rendered, but their prev cache is stale. Sync all
			// so a later enable_bullet() never lerps from a pre-disable pose.
			// (The woken bullet itself was already synced above.)
			update_all_previous_transforms_for_interpolation();
		}
	}

void MultiMeshBullets2D::disable_bullet(int bullet_index, bool should_disable_attachment) {
		if (!validate_bullet_index(bullet_index, "disable_bullet")) {
			return;
		}
		// disable_bullet() is a teardown-adjacent path (debug helpers call it on
		// unspawned instances; PREDELETE frees the area). enable_bullet() guards
		// these; mirror the guards so a dead multimesh can't null-deref below.
		if (multi.is_null() || !multi.is_valid()) {
			return;
		}
		if (physics_server == nullptr || !area.is_valid()) {
			return;
		}

		// Same re-entrancy latch as enable_bullet(): the attachment callback
		// below runs user code that may call enable_bullet()/disable_bullet().
		// A nested enable-then-disable pair inside one outer disable would
		// otherwise decrement the counter twice for one claimed slot.
		if (_bullet_enable_depth > 0) {
			UtilityFunctions::push_error("disable_bullet: re-entrant call from inside on_bullet_enable is not allowed. Use call_deferred to change bullet state from that callback.");
			return;
		}

		const bool curr_bullet_status = all_bullets_enabled_set.contains(bullet_index);

		// If the bullet is already disabled, just return
		if (!curr_bullet_status) {
			return;
		}

		all_bullets_enabled_set.disable_data(bullet_index);

		--active_bullets_counter;
		if (active_bullets_counter < 0) {
			active_bullets_counter = 0;
		}

		// Stale queued hits for this slot must not fire after a re-enable:
		// bump the drain epoch so records queued before this disable
		// mismatch at emit time.
		bump_collision_epoch_for_bullet(bullet_index);

		// Drop per-bullet homing/orbit state now (directional override): the tick
		// only trims active bullets, so without this a disabled bullet's invalid
		// targets leak counters until the whole multimesh dies. Pattern and curve
		// state is deliberately kept: a wake resumes the bullet's own movement
		// (documented wake contract), while homing queues and orbit locks are
		// re-pushed/re-armed after the wake.
		on_bullet_disabled(bullet_index);

		multi->set_instance_transform_2d(bullet_index, zero_transform); // Stops rendering the instance

		physics_server->area_set_shape_disabled(area, bullet_index, true);

		if (should_disable_attachment) {
			bullet_disable_attachment(bullet_index);
		}

		if (active_bullets_counter <= 0) {
			disable_multimesh();
		}
	}

void MultiMeshBullets2D::handle_bullet_collision(CollisionType collision_type, int bullet_index, int64_t entered_instance_id, uint64_t queued_bullet_epoch) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return;
		}
		if (bullet_index >= (int)bullets_current_collision_count.size() || bullet_index >= (int)attachments.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
			return;
		}
		// Stale record check: a handler earlier in this same drain may have
		// disabled then re-enabled this slot (epoch bump on both). The queued
		// stamp no longer matches, so this record describes a dead overlap,
		// not a new hit - skip it instead of double-counting.
		if (queued_bullet_epoch != collision_epoch_for_bullet(bullet_index)) {
			return;
		}
		const bool curr_bullet_status = all_bullets_enabled_set.contains(bullet_index);

		// If the bullet is already disabled, just return
		if (!curr_bullet_status) {
			return;
		}

		int &current_bullet_collision_amount = bullets_current_collision_count[bullet_index];

		// Always keep track of how many collisions this bullet had (yes even if the user set bullet_max_collision_count to 0, I just want consistent behavior)
		++current_bullet_collision_amount;

		const bool bullet_reached_max_collisions = bullet_max_collision_count > 0 && current_bullet_collision_amount >= bullet_max_collision_count;

		// Snapshot the signal owner BEFORE any disable below: the killing blow
		// funnels into disable_multimesh(), which clears owner_spawner_id and
		// pools the instance. Resolving after would route a spawner volley's
		// hit to the factory (or drop it) instead of the owning spawner.
		Object *emitter = resolve_signal_emitter();

		// Only disable the bullet if the max collision count is greater than 0, otherwise the bullet should never be disabled due to collisions
		if (bullet_reached_max_collisions) {
			disable_bullet(bullet_index, false); // Don't disable the attachment yet, first emit the signal for collision so user has access to the attachment and CAN detach it himself inside GDScript
		}

		// Capture the slot before the signal: the handler runs user code that may
		// detach, replace, or - through a re-entrant spawn that pops this instance
		// from the pool - hand the whole multimesh to a new owner.
		BulletAttachment2D *attachment_at_signal_time = attachments[bullet_index];

		Object *hit_target = ObjectDB::get_instance(entered_instance_id);

		// Typed per-kind signals emit synchronously (Godot-style): the instance
		// is alive and the slot state valid by construction here, so handlers
		// run with live data, need no casts, and need no call_deferred for
		// game logic. HANDLER CONTRACT: to destroy this volley from inside
		// the handler use queue_free() (or call_deferred factory free/reset
		// calls) — never immediate Object.free()/memdelete. The post-emit
		// code below touches this instance, so an immediately-freed volley
		// would be use-after-free. Slim payload - custom data and transforms
		// are one
		// instance call away (bullet_get_custom_data(),
		// get_bullet_global_transform()).
		// Possessed by the tagged spawner when there is one, else the factory.
		// A null emitter (teardown, spawner gone, or both gone) only skips the
		// notification - cleanup below still runs.
		// Self-liveness token, captured before user code runs: a handler that
		// immediately frees this volley (against the contract below) leaves
		// every member access after the emit as use-after-free - including the
		// is_queued_for_deletion() check itself. ObjectDB validates the id
		// without touching the object, and comparing the result against this
		// performs no dereference, so a freed volley bails safely instead of
		// crashing (misuse is still prohibited: state after the emit is lost).
		const uint64_t self_id = get_instance_id();
		if (emitter != nullptr) {
			if (emitter == bullet_factory) {
				if (is_class("BlockBullets2D")) {
					if (collision_type == CollisionType::AREA) {
						emitter->emit_signal("block_area_entered", hit_target, this, bullet_index);
					} else if (collision_type == CollisionType::BODY) {
						emitter->emit_signal("block_body_entered", hit_target, this, bullet_index);
					}
				} else {
					if (collision_type == CollisionType::AREA) {
						emitter->emit_signal("directional_area_entered", hit_target, this, bullet_index);
					} else if (collision_type == CollisionType::BODY) {
						emitter->emit_signal("directional_body_entered", hit_target, this, bullet_index);
					}
				}
			} else {
				if (collision_type == CollisionType::AREA) {
					emitter->emit_signal("area_entered", hit_target, this, bullet_index);
				} else if (collision_type == CollisionType::BODY) {
					emitter->emit_signal("body_entered", hit_target, this, bullet_index);
				}
			}
		}

		// Disable the bullet attachment if the bullet reached its max collision count and the attachment is still enabled
		if (bullet_reached_max_collisions) {
			// The signal above runs user code that may have detached this slot
			// already (bullet_set_attachment_to_null / bullet_free_attachment /
			// bullet_set_attachment), or re-assigned it. Only disable the slot if
			// it still holds what we captured - anything else belongs to whoever
			// changed it (possibly a new pool owner), and disable_multimesh()'s
			// sweep catches any survivor that would otherwise leak.
			// The handler may also have freed THIS multimesh (queue_free during
			// the sync emit): attachments[]/bullets_current_collision_count[] are
			// member vectors, so bail before touching them.
			// The handler may also have freed the captured attachment itself:
			// compare by instance id (never a raw dangling pointer), and only
			// after confirming this multimesh is still alive.
			// Liveness FIRST (see self_id token above): no member touch - not
			// even is_queued_for_deletion() - when the handler freed us.
		if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
			return;
		}
		if (is_queued_for_deletion()) {
			return;
		}
		const uint64_t captured_id = attachment_at_signal_time != nullptr ? attachment_at_signal_time->get_instance_id() : 0;
		if (slot_still_holds_attachment(bullet_index, attachment_at_signal_time, captured_id)) {
			bullet_disable_attachment(bullet_index);
		}
		}
	}
} //namespace BlastBullets2D
