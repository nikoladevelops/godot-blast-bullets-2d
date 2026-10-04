// Lifecycle of a volley: creation (spawn), pool reuse (enable_volley), per-bullet
// enable/disable/clear, the clean-disabled reset and teardown. Start here to follow
// a volley's life; the per-frame motion lives in bullet_volley2d_tick.cpp.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

bool BulletVolley2D::enable_volley_for_script(const Ref<BulletVolleyData2D> &data, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id) {
	if (data.is_null()) {
		UtilityFunctions::push_error("enable_volley: spawn data is null.");
		return false;
	}
	return enable_volley(*data.ptr(), new_inherited_velocity_offset, spawner_id);
}

void BulletVolley2D::force_delete() {
	marked_for_internal_deletion = true;
	Node *parent = get_parent();
	if (parent) {
		parent->remove_child(this);
	}
	memdelete(this); // Immediate deletion after removal from tree
}

TypedArray<bool> BulletVolley2D::get_all_bullets_status() {
	TypedArray<bool> status_array;
	status_array.resize(amount_bullets);

	for (int i = 0; i < amount_bullets; i++) {
		bool status = all_bullets_enabled_set.contains(i);
		status_array[i] = status;
	}

	return status_array;
}

bool BulletVolley2D::is_bullet_status_enabled(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "is_bullet_status_enabled")) {
		return false;
	}

	return all_bullets_enabled_set.contains(bullet_index);
}

void BulletVolley2D::reset_pooling_flags_to_default() {
	is_auto_pooling_enabled = true;
	is_attachments_auto_pooling_enabled = true;
}

_ALWAYS_INLINE_ void BulletVolley2D::apply_shared_bullet_attachment_from_data(const BulletVolleyData2D &data) {
	if (data.shared_bullet_attachment.is_null()) {
		return;
	}
	for (int i = 0; i < amount_bullets; ++i) {
		if (!attach_bullet_attachment_internal(i, data.shared_bullet_attachment, data.shared_bullet_attachment_offset, data.shared_bullet_attachment_stick_relative_to_bullet)) {
			break;
		}
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::disable_volley() {
	// Re-entrancy guard: the sweep below fires user script callbacks
	// (attachment on_bullet_disable), and a handler calling factory.reset() /
	// free_* there would force_delete this multimesh mid-sweep (use-after-free).
	// Holding the factory's busy flag makes those paths reject with the standard
	// busy error until the sweep and the pool push are done.
	const bool saved_factory_busy = bullet_factory != nullptr ? bullet_factory->get_is_factory_busy() : false;
	if (bullet_factory != nullptr) {
		bullet_factory->_set_internal_operation_busy(true);
	}

	_disable_volley_internal();

	if (bullet_factory != nullptr) {
		bullet_factory->_set_internal_operation_busy(saved_factory_busy);
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::_disable_volley_internal() {
	// Pre-sweep drain markers: every drain goes through disable_bullet()
	// first, so no live bits should remain; clear anyway so a future direct
	// call can't pool an instance whose sparse set claims live bullets at
	// counter 0. Cleared BEFORE the sweep so a wake during the sweep
	// (is_active/counter set by enable_bullet) aborts below instead of
	// being buried.
	active_bullets_counter = 0;
	is_active = false;
	all_bullets_enabled_set.clear();
	// Both clocks rewind together (see disable_volley): unit curves
	// sample elapsed/max, so a split rewind pins curves at 1.0 while
	// lifetime still ticks.
	curves_elapsed_time = 0.0;
	if (!is_life_time_infinite) {
		current_life_time = 0.0;
	}
	// Deferred attachment disables can be dropped by a generation bump, so the
	// pool push below must never inherit live slots: sweep every survivor now
	// (auto-pool returns them to the pool, otherwise they are queue_freed).
	// The sweep runs user script callbacks (on_bullet_disable), and a handler
	// may wake a bullet again via enable_bullet(). The wake sets is_active and
	// bumps the counter/generations, so the trailing steps below (which belong
	// to the dying life) must not run over the fresh life - abort instead.
	for (int i = 0; i < (int)attachments.size(); ++i) {
		if (attachments[i] != nullptr) {
			bullet_disable_attachment(i);
		}
		if (is_active || active_bullets_counter > 0) {
			return;
		}
	}
	// One reset owns the rest of the clean-disabled state (owner 0
	// for pool neutrality, curves/patterns/motion state, attachment blanks,
	// collided hits, timers, clocks, animation cursor). No generation bump
	// and no connection scrub: the dying life's deferred emits must still
	// flush, and same-owner wakes keep their connections.
	// Pooling flags are PRESERVED here (not reset): reset_transient_volley_state
	// defaults them for new lives, but this is a dying life — the owner's
	// explicit "don't pool" must survive to the gate below, otherwise a
	// pooling-off volley would resurrect pooling mid-sweep and pool itself.
	const bool saved_auto_pool = is_auto_pooling_enabled;
	const bool saved_auto_pool_attachments = is_attachments_auto_pooling_enabled;
	reset_transient_volley_state(0, false);
	is_auto_pooling_enabled = saved_auto_pool;
	is_attachments_auto_pooling_enabled = saved_auto_pool_attachments;

	on_volley_deactivated();

	deactivate_volley();

	if (!is_auto_pooling_enabled) {
		return;
	}

	// A script callback inside the sweep can legally wake a bullet again
	// (enable_bullet). Pooling an instance with live bullets would hand an ACTIVE
	// multimesh to the next pop() as if it were disabled - only pool when the
	// instance is truly drained.
	if (bullets_pool != nullptr && active_bullets_counter == 0) {
		bullets_pool->push(this, get_pool_key());
	}
}

void BulletVolley2D::on_bullet_disabled(int bullet_index) {
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return;
	}
	// Bounce cleanup first and independent: a disabled bullet owns no
	// bounce state (counts, cooldown, per-tick guard, smooth-visual
	// pursuit all restart on wake), and this must run even if the
	// homing vectors below are ever sized differently.
	if (bullet_index < (int)all_bounce_count.size()) {
		all_bounce_count[bullet_index] = 0;
	}
	if (bullet_index < (int)all_bounce_cooldown.size()) {
		all_bounce_cooldown[bullet_index] = 0.0;
	}
	if (bullet_index < (int)all_bounce_last_tick.size()) {
		all_bounce_last_tick[bullet_index] = 0;
	}
	// A wake forgets who it bounced off last, same as counts above.
	if (bullet_index < (int)all_bounce_last_target.size()) {
		all_bounce_last_target[bullet_index] = 0;
	}
	if (bullet_index < (int)all_bounce_last_time.size()) {
		all_bounce_last_time[bullet_index] = 0.0;
	}
	if (bullet_index < (int)all_bounce_last_normal.size()) {
		all_bounce_last_normal[bullet_index] = Vector2(0, 0);
	}
	if (bullet_index < (int)all_bounce_last_target_velocity.size()) {
		all_bounce_last_target_velocity[bullet_index] = Vector2(0, 0);
	}
	if (bullet_index < (int)bounce_visual_pending.size()) {
		bounce_visual_pending[bullet_index] = 0;
	}
	if (bullet_index < (int)bounce_visual_target.size()) {
		bounce_visual_target[bullet_index] = Vector2(1, 0);
	}
	// A wake starts fresh like every other bounce ledger entry: without
	// this a re-enabled bullet would keep scaling curve/accel ticks by
	// a stale boost from its previous life.
	if (bullet_index < (int)all_bounce_speed_multiplier.size()) {
		all_bounce_speed_multiplier[bullet_index] = 1.0;
	}
	if (bullet_index >= (int)all_bullet_homing_targets.size()) {
		return;
	}
	auto &queue = all_bullet_homing_targets[bullet_index];
	queue.clear_homing_targets(cached_mouse_global_position);
	if (bullet_index >= 0 && bullet_index < (int)all_homing_count.size()) {
		active_homing_count -= all_homing_count[bullet_index];
		if (active_homing_count < 0) {
			active_homing_count = 0;
		}
		all_homing_count[bullet_index] = 0;
	}
	// Drop this bullet's shared-deque reached state so a re-enabled bullet can
	// emit again for the current front target.
	if (bullet_index >= 0 && bullet_index < (int)all_shared_homing_reached.size()) {
		all_shared_homing_reached[bullet_index] = SharedHomingReachedState();
	}
	if (bullet_index >= 0 && bullet_index < (int)all_orbiting_status.size() && all_orbiting_status[bullet_index]) {
		all_orbiting_status[bullet_index] = 0;
		--active_orbiting_count;
		if (active_orbiting_count < 0) {
			active_orbiting_count = 0;
		}
	}
	if (bullet_index >= 0 && bullet_index < (int)all_orbiting_data.size()) {
		all_orbiting_data[bullet_index] = OrbitingData();
	}
}

void BulletVolley2D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PREDELETE: {
			// The destructor also runs for editor-time instances, which have no runtime state.
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}

			if (!marked_for_internal_deletion && bullet_factory) {
				bullet_factory->handle_manual_volley_deletion(*this);
			}

			clear_homing_state_for_teardown();

			// Sprite effect teardown first: factory one-shot bakes keyed by
			// this volley die here (in-flight visuals stop), trail shard
			// children are freed with the node below. Safe during factory
			// teardown too (its vectors were already cleared, unregister
			// no-ops, and our own children are still alive).
			if (bullet_factory != nullptr) {
				bullet_factory->fx_unregister_volley(get_instance_id());
			}
			fx_clear_trail_layers();

			if (physics_server && area.is_valid()) {
				// Disable the area's shapes (ALL OF THEM no matter their bullets_enabled_status).
				// Bounds-checked: never let a desynced attachments vector take down PREDELETE.
				// When the factory itself is tearing down, only run the script callback and
				// drop the slot: re-pooling into (or queue_freeing from) a dying factory is
				// pointless, the engine destroys the whole subtree anyway.
				const bool factory_is_dying = bullet_factory == nullptr || bullet_factory->get_is_tearing_down();
				for (int i = 0; i < amount_bullets && i < area_shape_count; ++i) {
					physics_server->area_set_shape_disabled(area, i, true);

					if (i >= 0 && i < (int)attachments.size() && attachments[i] != nullptr) {
						if (factory_is_dying) {
							// Null the slot BEFORE the callback (same ordering as
							// bullet_disable_attachment): re-entrant API calls from the
							// script must see an empty slot.
							BulletAttachment2D *detaching = attachments[i];
							attachments[i] = nullptr;
							// Every ownership change bumps the slot epoch, including
							// this teardown path, so the "bumped on every change"
							// invariant holds without exceptions.
							bump_attachment_epoch(i);
							// Owner tracking cleared like bullet_disable_attachment:
							// attachments outlive this multimesh (siblings in the
							// container), and a stale owner id would make their
							// PREDELETE resolve a dead multimesh.
							detaching->owner_volley_id = 0;
							detaching->owner_bullet_index = -1;
							detaching->call_on_bullet_disable();
						} else {
							bullet_disable_attachment(i);
						}
					}
				}

				physics_server->area_set_area_monitor_callback(area, Variant());
				physics_server->area_set_monitor_callback(area, Variant());

				// Detach the shapes from the area BEFORE freeing the RID (freeing a
				// still-attached shape RID warns/leaks on the physics server).
				release_volley_shape();

				if (area.is_valid()) {
					physics_server->free_rid(area);
				}
				area = RID();
			}
		} break;
	}
}

// Used to spawn brand new bullets.
void BulletVolley2D::spawn(const BulletVolleyData2D &data, VolleyPool *pool, BulletFactory2D *factory, Node *bullets_container, const Vector2 &new_inherited_velocity_offset, int new_sparse_set_id, bool spawn_in_pool, uint64_t spawner_id) {
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

	warn_data_id = data.get_instance_id();
	amount_bullets = spawn_transform_count(data); // important, because some set_up methods use this
	cache_collision_shape_typed(data.collision_shape);

	++volley_generation;

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

	set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures, data.tile_all_bullet_rotation_data);

	all_previous_instance_transf.resize(amount_bullets);
	all_previous_attachment_transf.resize(amount_bullets);

	update_all_previous_transforms_for_interpolation();

	finalize_set_up(
			data.shared_bullets_custom_data,
			data.material,
			data.z_index,
			data.light_mask,
			data.visibility_layer,
			data.self_modulate,
			data.instance_shader_parameters);

	// Single-error policy: rebuild_sprite_animation already reported the cause;
	// no wrapper error here. Failure leaves previous texture/cache untouched.
	// Appearance snapshot first: the rebuild below whitens frames when the
	// data asks for it, and the fade half starts transparent when fading in.
	snapshot_appearance_from_data(data);
	rebuild_sprite_animation(data.sprite_frames, data.animation);

	seed_motion_features_on_spawn(data);

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
	// pre-population: the slots were just blanked above, and enable_volley()
	// applies the data when the instance is popped instead.
	if (!spawn_in_pool) {
		apply_shared_bullet_attachment_from_data(data);
		// Sprite effect layers reseed here too (trail shards rebuilt, factory
		// one-shot bakes registered, spawn flashes fired at every bullet).
		fx_reseed_from_data(data.effect_layers, true);
	}
}

void BulletVolley2D::reset_transient_volley_state(uint64_t new_owner_spawner_id, bool drop_stale_work) {
	// A new life / a dead life never replays the previous life's parked overlaps.
	paused_overlaps.clear();
	// Ownership is stamped first so every step below already belongs to the
	// new life (or to nobody, when dying).
	owner_spawner_id = new_owner_spawner_id;
	// Shared curves/patterns are cleared HERE, before the motion reset: the
	// reseed functions return early on empty arrays and skip null entries, so
	// a stale slot left here would leak the previous owner's per-bullet
	// curves and movement patterns into the next life.
	shared_bullet_curves_data.unref();
	for (auto &r : all_bullet_curves_data) {
		r.unref();
	}
	for (auto &p : all_movement_pattern_data) {
		p = BulletMovementPatternData2D();
	}
	reset_motion_feature_state(drop_stale_work);
	// Blank attachment state: a reused instance must never carry the previous
	// owner's attachment slots into the next life. A held expiry keeps them
	// for its deferred handler (released after the flush); a new life always
	// ends any hold first.
	reset_attachment_state_for_reuse();
	// Pooling flags reset every life - if you turned pooling off to hold a volley manually, the next pooled reuse still pools normally unless you turn it off again.
	reset_pooling_flags_to_default();
	if (drop_stale_work) {
		// Runtime-only collision knob (spawn data never seeds it): a new
		// life always starts with the default object-level dedup.
		collision_dedup_by_object = true;
		// New life: stale deferred emits/disables (scheduled before a pool
		// reuse) carry the old generation and no-op at flush time, and the
		// previous owner's volley-wide connections must not fire again.
		// Disabled (but not pooled) volleys keep their connections here: a
		// same-owner enable_bullet() wake must not silence the volley.
		++volley_generation;
		disconnect_sprite_animation_connections();
	}
	// A fresh volley starts with zero hits and no timers, no matter how the last one died.
	all_collided_bullets.clear();
	// The dedup keys mirror all_collided_bullets, so they reset with it.
	clear_collision_dedup_keys();
	detach_all_time_based_functions();
	// Same for the volley clock - waking an old instance must not resume the previous owner's curve time.
	curves_elapsed_time = 0.0;
	// Animation cursor restarts; baked frames are kept so a same-owner wake
	// resumes them. New-life paths blank the frames before rebuilding.
	anim_frame_index = 0;
	anim_paused = false;
	anim_finished = false;
	anim_finished_event_pending = false;
	if (!anim_frame_secs.empty()) {
		anim_frame_time_left = anim_frame_secs[0];
	}
}

void BulletVolley2D::deactivate_volley() {
	all_bullets_enabled_set.clear();
	active_bullets_counter = 0;
	is_active = false;
	set_visible(false);
	set_all_physics_shapes_enabled_for_area(false);
}

// Activates the multimesh
bool BulletVolley2D::enable_volley(const BulletVolleyData2D &data, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id) {
	warn_data_id = data.get_instance_id();
	// The pool sorts volleys by bullet count, so a wrong-size array here would read past the end - bail before touching anything.
	if (spawn_transform_count(data) != amount_bullets) {
		spawn_transforms_ptr = nullptr;
		UtilityFunctions::push_error("enable_volley: transforms size (" + String::num_int64(spawn_transform_count(data)) + ") must match amount_bullets (" + String::num_int64(amount_bullets) + ").");
		return false;
	}

	// Fast path: same shape and count means we just rewrite transforms instead of rebuilding physics.
	// shapes and reseeds SoA vectors (no RID alloc/free), so it is safe from
	// ordinary physics callbacks such as _physics_process shooting. Only a
	// shape-TYPE change performs structural RID work (area_clear_shapes /
	// free_rid / re-bucket) and must wait for a safe point. Iterating sweeps
	// (factory busy) always reject: vectors below are being walked.
	const PhysicsServer2D::ShapeType incoming_effective = CollisionShapeHelper2D::get_effective_type(data.collision_shape, false);
	const bool shape_type_changes = (incoming_effective != cached_effective_shape_type);
	// Only THIS volley's own tick is off limits (its drain/lifetime pass is
	// still walking its vectors). Other pooled volleys reuse fine mid-sweep:
	// the factory iterates a snapshot, so a re-activated volley simply joins
	// the next tick.
	if (is_being_ticked) {
		UtilityFunctions::push_error("enable_volley cannot run on a volley while its own tick is running (e.g. from its collision handler or attachment callback). Use call_deferred() to enable it after the sweep.");
		return false;
	}
	if (shape_type_changes && bullet_factory != nullptr && bullet_factory->is_structural_mutation_unsafe()) {
		UtilityFunctions::push_error("enable_volley with a different collision shape type cannot run inside a physics frame (server flush locks apply to the shape RIDs it must recreate). Spawn through BulletFactory2D (a shape change pops a matching pool bucket) or call enable_volley from an idle frame.");
		return false;
	}

	// Re-enabling a live volley would wipe its attachments, timers, curves and
	// patterns mid-flight. Pool pops only hand out disabled instances, so a
	// live instance here is always a direct (mis)call: reject, don't reseed.
	if (is_active) {
		UtilityFunctions::push_error("enable_volley: instance is already active. Disable it first or spawn a new volley instead.");
		return false;
	}

	// Reseeding a dying instance would configure a volley that never lives a
	// tick (pool pops already filter these; this covers direct GDScript
	// calls). queue_free() is terminal.
	if (is_queued_for_deletion()) {
		UtilityFunctions::push_error("enable_volley: multimesh is queued for deletion.");
		return false;
	}

	// Validate everything before touching state; reset + reseed only runs on
	// first mutation, so a reject below leaves zero state behind and no
	// valid input, so a rejected enable never needs a rollback.
	// Never-spawned instances hold no multimesh handle (generate_multimesh
	// runs in spawn() only): reseeding one would null-deref below. Pool
	// pops always carry theirs, so this rejects misuse only.
	if (multi.is_null() || !multi.is_valid()) {
		UtilityFunctions::push_error("enable_volley: multimesh was never spawned through BulletFactory2D (no bullet storage). Spawn it first.");
		return false;
	}

	// The factory spawn_* paths validate finiteness, but a direct
	// enable_volley() call bypasses them: a NaN/Inf offset here would
	// poison every bullet's velocity for the whole volley. Checked before any
	// mutation (previously after the owner/curve clears, leaking on reject).
	if (!new_inherited_velocity_offset.is_finite()) {
		UtilityFunctions::push_error("enable_volley: inherited velocity offset must be finite, keeping the old value.");
		return false;
	}

	// One reset owns the whole clean-disabled state (owner stamp,
	// curves/patterns/motion ballistics, attachment slots, collided hits,
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
	// No snapshot/rollback needed. Wrong-type input was rejected above
	// before any mutation, and every reseed below fully overwrites the blank
	// state reset_transient_volley_state() left behind.
	inherited_velocity_offset = new_inherited_velocity_offset;
	const PhysicsServer2D::ShapeType old_effective_shape_type = cached_effective_shape_type;
	cache_collision_shape_typed(data.collision_shape);

	// Reused RIDs keep their original type. If the new spawn switches shape type,
	// the old RIDs would receive mismatched data below, so recreate them exactly
	// like set_collision_shape_runtime() does.
	if (cached_effective_shape_type != old_effective_shape_type && physics_server != nullptr && area.is_valid()) {
		release_volley_shape();
		generate_physics_shapes_for_area(amount_bullets);
	}

	// Blank attachment state before anything else: a reused instance must never
	// carry the previous owner's attachment slots into this enable.
	reset_attachment_state_for_reuse();

	++volley_generation;

	// A fresh volley starts with zero hits and no timers, no matter how the last one died.
	all_collided_bullets.clear();
	// The dedup keys mirror all_collided_bullets, so they reset with it.
	clear_collision_dedup_keys();
	detach_all_time_based_functions();

	// Same for the volley clock - waking an old instance must not resume the previous owner's curve time.
	curves_elapsed_time = 0.0;

	set_up_life_time_timer(data.max_life_time, data.max_life_time);

	set_up_multimesh(amount_bullets, data.mesh, resolve_quad_size(data.sprite_frames, data.animation, data.texture_size));

	set_up_bullet_instances(data);
	set_all_physics_shapes_enabled_for_area(true);

	// Shared spawn-data attachments (both bullet types). Slot vectors were
	// reset above and caches sized by set_up_bullet_instances, so this is safe
	// for pooled reuse with new data.
	apply_shared_bullet_attachment_from_data(data);

	set_rotation_data(data.all_bullet_rotation_data, data.rotate_only_textures, data.tile_all_bullet_rotation_data);

	move_to_front(); // Pooled instances render behind newer ones without this; moving to front emulates fresh spawn order.

	update_all_previous_transforms_for_interpolation();

	finalize_set_up(
			data.shared_bullets_custom_data,
			data.material,
			data.z_index,
			data.light_mask,
			data.visibility_layer,
			data.self_modulate,
			data.instance_shader_parameters);

	// Single-error policy: rebuild already reported; blank animation kept on failure.
	// (Frames were blanked in the prologue; connections scrubbed by the reset.)
	// Appearance snapshot first (same ordering as spawn above).
	snapshot_appearance_from_data(data);
	rebuild_sprite_animation(data.sprite_frames, data.animation);

	// Motion reseed. A refusal leaves a clean disabled instance for the pool
	// instead of half-seeded state: reset + deactivate fully blank what the
	// reseed above wrote, so the next correct enable starts from neutral.
	if (!reseed_motion_features_on_enable(data)) {
		reset_transient_volley_state(spawner_id, true);
		deactivate_volley();
		return false;
	}

	// Sprite effect layers reseed from the new data (previous life's bakes
	// die here, trail shards rebuild, spawn flashes fire at every bullet).
	fx_reseed_from_data(data.effect_layers, true);

	set_visible(true);

	// Mark all bullets as enabled in the sparse set (amount_bullets never changes)
	all_bullets_enabled_set.activate_all_data();
	is_active = true;
	return true;
}

void BulletVolley2D::enable_bullet(int bullet_index, int collision_amount, bool should_enable_attachment) {
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
		// Cross-owner reuse must go through spawn_*()/enable_volley(),
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
		// resurrect the volley while _disable_volley_internal() is tearing it
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
			++volley_generation;
			custom_timers.clear();
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
		write_trail_instances(bullet_index); // Wake resumes the trail with the bullet

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
				bullet_factory->track_volley_active(*this);
			}
		// A pooled instance carries the previous owner's signal connections; they
		// must not fire for this wake (same cleanup the pool-pop enable does).
		// Scoped to foreign lives only (see was_pooled above): same-owner
		// revives skip the disconnects so sibling notifications survive.
		if (was_pooled) {
			disconnect_sprite_animation_connections();
			// Same for the previous owner's homing forward: without this, the old
			// spawner would keep retargeting a volley someone else woke manually.
			for (const Dictionary &connection : get_signal_connection_list("bullet_homing_target_reached")) {
				const Callable callable = connection["callable"];
				disconnect("bullet_homing_target_reached", callable);
			}
			// Fail-safe neutral subset: the queue_free-vs-pool decision and the
			// rotation drive must not follow a dead owner into the new life.
			// Pooling flags reset to default (a foreign wake must never inherit
			// "don't pool" and strand itself, nor "pool" against the new owner's
			// wishes — set them explicitly after the wake if needed). Rotation
			// speeds are cleared (stale spin would steer the new life with no
			// data behind it). Ballistics, appearance, custom data, collision
			// counts/max, lifetime and shape state resume by design (warned
			// below): reseed via spawn_*/enable_volley for a clean slate.
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
		// through spawn_*/enable_volley or adopt_live_volley + manual
		// re-push, so warn once per wake instead of driving silently stale.
		if (was_pooled) {
			UtilityFunctions::push_warning("enable_bullet: woke a pooled volley from the pool outside spawn_*/enable_volley. Ballistics, appearance, custom data, collision counts, lifetime and shape state still hold the previous owner's values: reseed them (or adopt_live_volley + re-push homing/orbit) before relying on this volley.");
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

void BulletVolley2D::disable_bullet(int bullet_index, bool should_disable_attachment) {
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
		// otherwise decrement the counter twice for one claimed slot, and a
		// nested disable inside the sweep below would pool the instance twice.
		if (_bullet_enable_depth > 0) {
			UtilityFunctions::push_error("disable_bullet: re-entrant call from inside on_bullet_enable/on_bullet_disable is not allowed. Use call_deferred to change bullet state from that callback.");
			return;
		}
		ReentrancyGuard bullet_disable_guard(_bullet_enable_depth);

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

		// Drop per-bullet homing/orbit state now: the tick
		// only trims active bullets, so without this a disabled bullet's invalid
		// targets leak counters until the whole multimesh dies. Pattern and curve
		// state is deliberately kept: a wake resumes the bullet's own movement
		// (documented wake contract), while homing queues and orbit locks are
		// re-pushed/re-armed after the wake.
		on_bullet_disabled(bullet_index);

		multi->set_instance_transform_2d(bullet_index, zero_transform); // Stops rendering the instance
		hide_trail_instances(bullet_index); // Trail dies with its bullet, same frame

		physics_server->area_set_shape_disabled(area, bullet_index, true);

		if (should_disable_attachment) {
			bullet_disable_attachment(bullet_index);
		}

		if (active_bullets_counter <= 0) {
			disable_volley();
		}
	}

bool BulletVolley2D::clear_bullet(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "clear_bullet")) {
			return false;
		}
		// Already-dead slots stay silent: without this a double clear would
		// fire a second visual for a bullet that is already gone.
		if (!all_bullets_enabled_set.contains(bullet_index)) {
			return false;
		}
		// Pose captured before the disable below (disable never moves the
		// bullet, but the last-bullet disable funnels into disable_volley
		// which pools the instance; the DESTROY path fires the same way).
		Transform2D fx_clear_transf;
		const bool fx_have_pose = bullet_index >= 0 && bullet_index < (int)all_cached_instance_transforms.size();
		if (fx_have_pose) {
			fx_clear_transf = all_cached_instance_transforms[bullet_index];
		}
		disable_bullet(bullet_index, true);
		if (fx_have_pose) {
			fx_fire_oneshot(EFFECT_ON_CLEAR, bullet_index, fx_clear_transf);
		}
		return true;
	}

int BulletVolley2D::clear_all_bullets() {
		// Snapshot first: each clear mutates the live set below.
		std::vector<int> live = all_bullets_enabled_set.get_active_indexes();
		int cleared = 0;
		for (int i : live) {
			if (clear_bullet(i)) {
				++cleared;
			}
		}
		return cleared;
	}

BulletVolley2D::~BulletVolley2D() {
	for (auto &queue : all_bullet_homing_targets) {
		queue.clear_homing_targets(cached_mouse_global_position);
	}
	shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
}

void BulletVolley2D::seed_motion_features_on_spawn(const BulletVolleyData2D &data) {
	const BulletVolleyData2D *volley_data = &data;
	// Size movement/homing/orbit SoA up front: the tick path indexes them
	// unconditionally, so even a wrong-type early-return must leave them sized.
	// Gravity vectors/velocities sized here too (same invariant as movement).
	set_up_movement_data(TypedArray<BulletSpeedData2D>());
	adjust_direction_based_on_rotation = false;
	homing_update_timer = 0.0;
	all_bullet_wobble.assign(amount_bullets, WobbleSeed());
	all_bullet_wobble_data.assign(amount_bullets, Ref<BulletWobbleData2D>());
	wobble_distance_traveled.assign(amount_bullets, 0.0);
	is_wobble_feature_enabled = false;
	shared_bullet_wobble_data.unref();
	gravity = Vector2(0, 0);
	all_gravity.assign(amount_bullets, Vector2(0, 0));
	all_gravity_velocity.assign(amount_bullets, Vector2(0, 0));
	has_per_bullet_gravity.assign(amount_bullets, 0);
	gravity_delay_sec = 0.0;
	gravity_duration_sec = 0.0;
	linear_drag = 0.0;
	homing_delay_sec = 0.0;
	homing_duration_sec = 0.0;
	homing_lose_range_px = 0.0;
	all_bullet_homing_targets.resize(amount_bullets);
	all_homing_count.assign(amount_bullets, 0);
	all_bullet_homing_smoothing.assign(amount_bullets, 0.0);
	use_per_bullet_homing_smoothing = false;
	all_orbiting_data.resize(amount_bullets);
	all_orbiting_status.assign(amount_bullets, 0);
	all_shared_homing_reached.resize(amount_bullets);
	homing_inert_warning_issued = false;
	// Like enable: a fresh volley starts with zeroed homing/orbit counters and no cached mouse position.
	active_homing_count = 0;
	active_orbiting_count = 0;
	cached_mouse_global_position = Vector2(0, 0);
	homing_reached_events.clear();
	shared_pop_requested = false;

	set_up_movement_data(volley_data->all_bullet_speed_data, volley_data->tile_all_bullet_speed_data);

	adjust_direction_based_on_rotation = volley_data->adjust_direction_based_on_rotation;

	// Unified precedence: per-bullet wins over shared. Seed per-bullet
	// first, then fill only the gaps left by invalid entries (null or
	// non-finite) from shared. Shared is the fallback default, never an
	// override. Members mirror the data so runtime getters stay truthful.
	shared_bullet_speed_data = volley_data->shared_bullet_speed_data;
	if (shared_bullet_speed_data.is_valid()) {
		apply_shared_speed_fallback(shared_bullet_speed_data);
	}
	shared_bullet_rotation_data = volley_data->shared_bullet_rotation_data;
	if (shared_bullet_rotation_data.is_valid()) {
		apply_shared_rotation_fallback(shared_bullet_rotation_data, data.rotate_only_textures);
	}

	// Per-bullet spawn-data curves/patterns first, then the shared features
	// below (separate storages, so the tick resolves precedence live).
	apply_per_bullet_curves_from_data(*volley_data);
	apply_per_bullet_movement_patterns_from_data(*volley_data);

	// Shared spawn-data features. Curves always applied (a null data unrefs
	// any stale member via populate_shared); the pattern resolver clears its
	// own slot on empty/unresolvable paths. Per-bullet runtime state set after
	// spawn still overrides afterwards.
	populate_shared_curves_related_data(volley_data->shared_bullet_curves_data);
	apply_shared_movement_pattern_from_data(*volley_data);
	apply_wobble_from_data(*volley_data);
	apply_gravity_from_data(*volley_data);

	// Homing steering seeds from the same spawn data (direct factory users
	// keep it on fresh spawns too, matching the enable path).
	homing_smoothing = (real_t)volley_data->homing_smoothing;
	homing_update_interval = (real_t)volley_data->homing_update_interval;
	homing_update_timer = 0.0;
	homing_take_control_of_texture_rotation = volley_data->homing_take_control_of_texture_rotation;
	homing_distance_before_reached = (real_t)volley_data->homing_distance_before_reached;
	bullet_homing_auto_pop_after_target_reached = volley_data->bullet_homing_auto_pop_after_target_reached;
	shared_homing_deque_auto_pop_after_target_reached = volley_data->shared_homing_deque_auto_pop_after_target_reached;
	linear_drag = (real_t)volley_data->linear_drag;
	homing_delay_sec = (real_t)volley_data->homing_delay_sec;
	homing_duration_sec = (real_t)volley_data->homing_duration_sec;
	homing_lose_range_px = (real_t)volley_data->homing_lose_range_px;
	apply_bounce_from_data(*volley_data, data.collision_mask);
}

void BulletVolley2D::reset_motion_feature_state(bool drop_stale_work) {
	// Neutralize ballistics/shared/homing so a new life never inherits the
	// previous owner's values (called by reset_transient_volley_state).
	// set_up_movement_data re-seeds has_per_bullet_speed_data; the rotation
	// presence is handled below (kept across plain disables, reset only for
	// new pooled lives).
	// Linear ballistics (speed/max/accel/direction/velocity) survive a plain
	// full drain so a same-owner wake resumes them, exactly like rotation
	// speeds below (contract fix: a full drain used to zero them, so waking
	// any bullet after the last one died revived a frozen bullet). A new life
	// (drop_stale_work) neutralizes them; every spawn/enable reseeds anyway.
	if (drop_stale_work) {
		set_up_movement_data(TypedArray<BulletSpeedData2D>());
	}
	// Rotation presence follows the VALUES: rotation speeds survive a plain
	// disable (for same-owner wakes), so their presence decisions must too —
	// otherwise a later shared write would clobber authored entries the wake
	// meant to resume. Only a new pooled life (drop_stale_work) resets the
	// decisions (the seed below re-derives them anyway).
	if (drop_stale_work) {
		reset_per_bullet_rotation_presence();
	}
	shared_bullet_speed_data.unref();
	shared_bullet_rotation_data.unref();
	adjust_direction_based_on_rotation = false;
	homing_update_timer = 0.0;
	shared_movement_pattern_curve.unref();
	shared_movement_pattern_face_movement_direction = false;
	shared_movement_pattern_repeat = true;
	shared_movement_pattern_distances.assign(amount_bullets, 0.0);
	all_bullet_wobble.assign(amount_bullets, WobbleSeed());
	all_bullet_wobble_data.assign(amount_bullets, Ref<BulletWobbleData2D>());
	wobble_distance_traveled.assign(amount_bullets, 0.0);
	is_wobble_feature_enabled = false;
	shared_bullet_wobble_data.unref();
	gravity = Vector2(0, 0);
	all_gravity.assign(amount_bullets, Vector2(0, 0));
	all_gravity_velocity.assign(amount_bullets, Vector2(0, 0));
	has_per_bullet_gravity.assign(amount_bullets, 0);
	refresh_gravity_active();
	gravity_delay_sec = 0.0;
	gravity_duration_sec = 0.0;
	linear_drag = 0.0;
	homing_delay_sec = 0.0;
	homing_duration_sec = 0.0;
	homing_lose_range_px = 0.0;
	bounce_mask = 0;
	bounce_strength = 1.0;
	bounce_tilemap_layers = false;
	bounce_push_assist = true;
	bounce_charge_amplify = true;
	bounce_hit_consumed = false;
	bounce_max_count = 0;
	bounce_mode = 0;
	bounce_rotate_texture = true;
	bounce_rotation_smooth = 0.0;
	bounce_randomness_deg = 0.0;
	bounce_cooldown_sec = 0.05;
	bounce_debounce_sec = 0.15;
	all_bounce_count.clear();
	all_bounce_cooldown.clear();
	all_bounce_last_tick.clear();
	all_bounce_last_target.clear();
	all_bounce_last_time.clear();
	all_bounce_last_normal.clear();
	all_bounce_last_target_velocity.clear();
	bounce_visual_pending.clear();
	bounce_visual_target.clear();
	all_bounce_speed_multiplier.clear();
	bounce_speed_scaled = false;
	bounce_mask_warning_issued = false;
	clear_homing_state_for_teardown();
	if (drop_stale_work) {
		// New pooled life only: drop the last owner's signal connections. A plain wake keeps them - same owner, same listeners.
		for (const Dictionary &connection : get_signal_connection_list("bullet_homing_target_reached")) {
			const Callable callable = connection["callable"];
			disconnect("bullet_homing_target_reached", callable);
		}
		homing_reached_events.clear();
		shared_pop_requested = false;
	}
}

bool BulletVolley2D::reseed_motion_features_on_enable(const BulletVolleyData2D &data) {
	const BulletVolleyData2D *volley_data = &data;

	// Seeding-only from here: reset_motion_feature_state left blank
	// ballistics/homing/orbit.
	set_up_movement_data(volley_data->all_bullet_speed_data, volley_data->tile_all_bullet_speed_data);

	adjust_direction_based_on_rotation = volley_data->adjust_direction_based_on_rotation;

	// Unified precedence (same as spawn; enable runs on every pool reuse):
	// per-bullet wins, shared fills only invalid-entry gaps.
	shared_bullet_speed_data = volley_data->shared_bullet_speed_data;
	if (shared_bullet_speed_data.is_valid()) {
		apply_shared_speed_fallback(shared_bullet_speed_data);
	}
	shared_bullet_rotation_data = volley_data->shared_bullet_rotation_data;
	if (shared_bullet_rotation_data.is_valid()) {
		apply_shared_rotation_fallback(shared_bullet_rotation_data, data.rotate_only_textures);
	}

	// Per-bullet spawn-data curves/patterns first, then the shared features
	// below (separate storages, so the tick resolves precedence live).
	apply_per_bullet_curves_from_data(*volley_data);
	apply_per_bullet_movement_patterns_from_data(*volley_data);

	// Shared spawn-data features (same as spawn; enable runs on every pool
	// reuse, and stale state was cleared above by enable_volley).
	// Always applied: null data removes previously set features.
	populate_shared_curves_related_data(volley_data->shared_bullet_curves_data);
	apply_shared_movement_pattern_from_data(*volley_data);
	apply_wobble_from_data(*volley_data);
	apply_gravity_from_data(*volley_data);

	// Vectors are sized in spawn; resize defensively anyway (the reset
	// already blanked them, so no clears are needed here). Epochs re-assigned (not resized) so
	// a shrunken-then-regrown volley never keeps stale per-bullet epochs.
	all_bullet_wobble.resize(amount_bullets);
	all_bullet_wobble_data.resize(amount_bullets);
	wobble_distance_traveled.resize(amount_bullets, 0.0);
	all_bullet_homing_targets.resize(amount_bullets);
	all_homing_count.resize(amount_bullets, 0);
	all_bullet_homing_smoothing.resize(amount_bullets, 0.0);
	all_orbiting_data.resize(amount_bullets);
	all_orbiting_status.resize(amount_bullets, 0);
	all_shared_homing_reached.resize(amount_bullets);
	homing_reached_events.clear();
	shared_pop_requested = false;

	// Homing steering seeds from spawn data (direct factory users keep it
	// across pool reuse now) and is always overwritten by the spawner
	// afterwards, so spawner users keep their exact tuning either way.
	homing_smoothing = (real_t)volley_data->homing_smoothing;
	homing_update_interval = (real_t)volley_data->homing_update_interval;
	homing_update_timer = 0.0;
	homing_take_control_of_texture_rotation = volley_data->homing_take_control_of_texture_rotation;
	homing_inert_warning_issued = false;

	homing_distance_before_reached = (real_t)volley_data->homing_distance_before_reached;
	bullet_homing_auto_pop_after_target_reached = volley_data->bullet_homing_auto_pop_after_target_reached;
	shared_homing_deque_auto_pop_after_target_reached = volley_data->shared_homing_deque_auto_pop_after_target_reached;
	// Gravity fully seeded by apply_gravity_from_data above (vectors +
	// windows); the shared member mirrors it for the runtime getter.
	linear_drag = (real_t)volley_data->linear_drag;
	homing_delay_sec = (real_t)volley_data->homing_delay_sec;
	homing_duration_sec = (real_t)volley_data->homing_duration_sec;
	homing_lose_range_px = (real_t)volley_data->homing_lose_range_px;
	apply_bounce_from_data(*volley_data, data.collision_mask);
	return true;
}

void BulletVolley2D::on_volley_deactivated() {
	// Dying life: reached events collected this tick die with it.
	homing_reached_events.clear();
	shared_pop_requested = false;
	if (bullet_factory != nullptr) {
		bullet_factory->track_volley_inactive(*this);
	}
}

} // namespace BlastBullets2D
