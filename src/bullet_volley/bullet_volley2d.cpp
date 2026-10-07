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

// ---- Life states --------------------------------------------------------
// ACTIVE : is_active (at least one live bullet, ticked by the factory).
// PARKED : the last bullet went out with is_auto_pooling_enabled OFF. Nothing
//          was reset: the volley is frozen, still owned, and enable_bullet()
//          resumes it exactly where it stopped.
// POOLED : the last bullet went out with is_auto_pooling_enabled ON.
//          release_life() dropped every external reference; only a new life
//          (spawn / enable_volley -> begin_life) revives it, and waking a
//          pooled volley with enable_bullet() is refused (stale handle).

bool BulletVolley2D::is_pooled() const {
	return is_pooled_in_pool;
}

bool BulletVolley2D::is_parked() const {
	return !is_active && !is_pooled_in_pool && multi.is_valid() && amount_bullets > 0 && !is_queued_for_deletion();
}

String BulletVolley2D::debug_get_life_state() const {
	if (is_queued_for_deletion()) {
		return "dying";
	}
	if (is_active) {
		return "active";
	}
	if (is_pooled_in_pool) {
		return "pooled";
	}
	if (multi.is_valid() && amount_bullets > 0) {
		return "parked";
	}
	return "fresh";
}

void BulletVolley2D::set_is_auto_pooling_enabled(bool value) {
	is_auto_pooling_enabled = value;
	// A parked volley pools as soon as its owner lets it go.
	if (value && is_parked() && !is_being_ticked && bullets_pool != nullptr) {
		const uint64_t self_id = get_instance_id();
		release_life();
		if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
			return;
		}
		if (!is_active && !is_queued_for_deletion() && is_auto_pooling_enabled && !is_pooled_in_pool) {
			bullets_pool->push(this, get_pool_key());
		}
	}
}

// Holds the factory busy flag across a sweep that runs user callbacks
// (attachment on_bullet_disable): reset()/free_*/spawns from inside reject
// with the standard busy error, and wakes are refused, until the sweep ends.
// The factory is re-resolved by id on exit (a callback could free it).
struct FactoryBusyScope2D {
	BulletFactory2D *factory = nullptr;
	uint64_t factory_id = 0;
	bool saved = false;
	explicit FactoryBusyScope2D(BulletFactory2D *p_factory) :
			factory(p_factory) {
		if (factory != nullptr) {
			factory_id = factory->get_instance_id();
			saved = factory->get_is_factory_busy();
			factory->_set_internal_operation_busy(true);
		}
	}
	~FactoryBusyScope2D() {
		if (factory != nullptr && ObjectDB::get_instance(ObjectID(factory_id)) == factory) {
			factory->_set_internal_operation_busy(saved);
		}
	}
	FactoryBusyScope2D(const FactoryBusyScope2D &) = delete;
	FactoryBusyScope2D &operator=(const FactoryBusyScope2D &) = delete;
};

void BulletVolley2D::on_volley_drained() {
	// disable_bullet() just took out the last live bullet.
	active_bullets_counter = 0;
	is_active = false;
	on_volley_deactivated();
	// Every shape was disabled bullet by bullet on the way here; hiding the
	// node is all that is left for the frozen (parked) state.
	set_visible(false);
	if (!is_auto_pooling_enabled || bullets_pool == nullptr) {
		return; // PARKED: frozen, still owned, enable_bullet() resumes it
	}
	const uint64_t self_id = get_instance_id();
	release_life();
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return; // a release callback freed this volley
	}
	// A release callback may have queued the volley for deletion or turned
	// pooling off (then it stays parked, released).
	if (is_active || active_bullets_counter > 0 || is_queued_for_deletion() || !is_auto_pooling_enabled || is_pooled_in_pool) {
		return;
	}
	bullets_pool->push(this, get_pool_key());
}

void BulletVolley2D::release_life() {
	// Pool time: drop everything that reaches OUTSIDE the volley (nodes,
	// callables, connections, user resources, the owner), so a pooled
	// instance retains nothing and fires nothing. Plain values (ballistics,
	// ledgers, seeds) are NOT reset here: begin_life() reseeds every one of
	// them once, from the next owner's data.
	FactoryBusyScope2D busy(bullet_factory);
	const uint64_t self_id = get_instance_id();
	for (int i = 0; i < (int)attachments.size(); ++i) {
		if (attachments[i] != nullptr) {
			bullet_disable_attachment(i); // user callback (on_bullet_disable)
			if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
				return;
			}
		}
	}
	reset_attachment_state_for_reuse();
	clear_homing_state_for_teardown();
	detach_all_time_based_functions();
	all_collided_bullets.clear();
	clear_collision_dedup_keys();
	paused_overlaps.clear();
	homing_reached_events.clear();
	shared_pop_requested = false;
	anim_finished_event_pending = false;
	graze_release(); // user resources (zones) and queued events
	sound_release(); // user resources (sounds) and the listener ref
	graze_detector.reset(); // the arming spawner's target settings
	// Volley-level listeners belong to the life that connected them.
	disconnect_sprite_animation_connections();
	for (const Dictionary &connection : get_signal_connection_list(CachedStringNames2D::get().bullet_homing_target_reached)) {
		const Callable callable = connection["callable"];
		disconnect(CachedStringNames2D::get().bullet_homing_target_reached, callable);
	}
	owner_spawner_id = 0;
	orphaned_spawner_path = String();
	// User resources: a pooled volley must not keep them alive.
	shared_bullets_custom_data.unref();
	for (Ref<Resource> &r : all_bullets_custom_data) {
		r.unref();
	}
	shared_bullet_curves_data.unref();
	for (Ref<BulletCurvesData2D> &r : all_bullet_curves_data) {
		r.unref();
	}
	for (BulletMovementPatternData2D &p : all_movement_pattern_data) {
		p = BulletMovementPatternData2D();
	}
	shared_movement_pattern_curve.unref();
	shared_bullet_speed_data.unref();
	shared_bullet_rotation_data.unref();
	shared_bullet_wobble_data.unref();
	for (Ref<BulletWobbleData2D> &r : all_bullet_wobble_data) {
		r.unref();
	}
	anim_source.unref();
	anim_frames.clear();
	anim_frame_secs.clear();
	set_texture(Ref<Texture2D>());
	// Node-level decorations a previous owner may have added: user groups
	// (engine-internal groups start with '_'), metadata and the CanvasItem
	// modulate. The next owner starts from a neutral node.
	const TypedArray<StringName> groups = get_groups();
	for (int g = 0; g < groups.size(); ++g) {
		const StringName group = groups[g];
		if (!String(group).begins_with("_")) {
			remove_from_group(group);
		}
	}
	const TypedArray<StringName> meta_keys = get_meta_list();
	for (int m = 0; m < meta_keys.size(); ++m) {
		remove_meta(meta_keys[m]);
	}
	restore_node_baseline();
	life_released = true;
}

void BulletVolley2D::restore_node_baseline() {
	// Node/CanvasItem/Node2D settings spawn data never seeds: a previous
	// owner's change (moved node, top_level, filtering, sorting, process
	// mode...) must not leak into the next life. Each setter runs only when
	// the value actually changed (the common pooled release costs reads).
	if (get_modulate() != Color(1, 1, 1, 1)) {
		set_modulate(Color(1, 1, 1, 1));
	}
	if (get_process_mode() != Node::PROCESS_MODE_INHERIT) {
		set_process_mode(Node::PROCESS_MODE_INHERIT);
	}
	if (get_process_priority() != 0) {
		set_process_priority(0);
	}
	if (get_physics_process_priority() != 0) {
		set_physics_process_priority(0);
	}
	if (is_draw_behind_parent_enabled()) {
		set_draw_behind_parent(false);
	}
	if (is_set_as_top_level()) {
		set_as_top_level(false);
	}
	if (get_clip_children_mode() != CanvasItem::CLIP_CHILDREN_DISABLED) {
		set_clip_children_mode(CanvasItem::CLIP_CHILDREN_DISABLED);
	}
	if (!is_z_relative()) {
		set_z_as_relative(true);
	}
	if (is_y_sort_enabled()) {
		set_y_sort_enabled(false);
	}
	if (get_texture_filter() != CanvasItem::TEXTURE_FILTER_PARENT_NODE) {
		set_texture_filter(CanvasItem::TEXTURE_FILTER_PARENT_NODE);
	}
	if (get_texture_repeat() != CanvasItem::TEXTURE_REPEAT_PARENT_NODE) {
		set_texture_repeat(CanvasItem::TEXTURE_REPEAT_PARENT_NODE);
	}
	if (get_use_parent_material()) {
		set_use_parent_material(false);
	}
	if (get_transform() != Transform2D()) {
		set_transform(Transform2D());
	}
	// Newer engines add CanvasItem.oversampling_with_scale: restore it when
	// the running engine has it (absent properties read as nil).
	const StringName &oversampling_name = CachedStringNames2D::get().oversampling_with_scale;
	const Variant oversampling = get(oversampling_name);
	if (oversampling.get_type() == Variant::INT && (int64_t)oversampling != 0) {
		set(oversampling_name, 0);
	}
}

void BulletVolley2D::begin_life(const BulletVolleyData2D &data, uint64_t new_owner_spawner_id, const Vector2 &new_inherited_velocity_offset) {
	// The single new-life path (spawn and enable_volley both end here). A
	// volley that was never released (fresh, or parked and re-enabled)
	// releases first, so nothing of the previous life survives.
	if (!life_released) {
		const uint64_t self_id = get_instance_id();
		release_life();
		if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
			return;
		}
	}
	// A life begun mid-sweep (cold spawn or pool reuse from a handler) starts
	// on the NEXT factory step, never inside the one already running.
	if (bullet_factory != nullptr) {
		sweep_tick_stamp = bullet_factory->get_sweep_counter();
		sweep_timer_stamp = sweep_tick_stamp;
	}
	// One generation (life id) per life: stale handles compare against it.
	++volley_generation;
	// Ownership first: a spawner volley is never observable as factory-owned.
	owner_spawner_id = new_owner_spawner_id;
	orphaned_spawner_path = String();
	inherited_velocity_offset = new_inherited_velocity_offset;
	// Runtime-only knobs spawn data never seeds start from their defaults.
	reset_pooling_flags_to_default();
	collision_dedup_by_object = true;
	curves_elapsed_time = 0.0;
	anim_frame_index = 0;
	anim_paused = false;
	anim_finished = false;
	anim_finished_event_pending = false;

	set_up_life_time_timer(data.max_life_time, data.max_life_time);
	set_up_multimesh(amount_bullets, data.mesh, resolve_quad_size(data.sprite_frames, data.animation, data.texture_size));
	set_up_bullet_instances(data);
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
	// Single-error policy: rebuild_sprite_animation already reported the
	// cause. Appearance snapshot first: the rebuild whitens frames when the
	// data asks for it, and the fade half starts transparent when fading in.
	snapshot_appearance_from_data(data);
	rebuild_sprite_animation(data.sprite_frames, data.animation);
	seed_motion_features(data);
	life_released = false;
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
				// Disable every shape of the area (enabled or not).
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
	bullets_pool = pool;
	bullet_factory = factory;
	physics_server = PhysicsServer2D::get_singleton();

	warn_data_id = data.get_instance_id();
	amount_bullets = spawn_transform_count(data); // important, because some set_up methods use this
	cache_collision_shape_typed(data.collision_shape);

	// Allocation (once per instance; pool reuse keeps all of it).
	all_bullets_enabled_set.resize(amount_bullets);
	bullet_collision_epochs.assign(amount_bullets, 0);
	batch_buffer.resize(amount_bullets * 8);
	generate_multimesh();
	area = physics_server->area_create();
	generate_physics_shapes_for_area(amount_bullets);
	// A fresh instance has nothing to release.
	life_released = true;

	begin_life(data, spawn_in_pool ? 0 : spawner_id, new_inherited_velocity_offset);

	set_process(false);
	set_physics_process(false);
	if (spawn_in_pool) {
		// Pre-population: a released, inactive instance waiting in the pool
		// (enable_volley seeds it from the real data when popped).
		set_visible(false);
		is_active = false;
		active_bullets_counter = 0;
		set_all_physics_shapes_enabled_for_area(false);
		bullets_container->add_child(this);
		life_released = true;
		bullets_pool->push(this, get_pool_key());
		return;
	}
	all_bullets_enabled_set.activate_all_data();
	is_active = true;
	bullets_container->add_child(this);
	// Shared spawn-data attachments and effect layers (trail shards rebuilt,
	// factory one-shot bakes registered, spawn flashes at every bullet).
	apply_shared_bullet_attachment_from_data(data);
	fx_reseed_from_data(data.effect_layers, true);
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

	// Same-shape reuse only rewrites transforms and reseeds vectors (no RID
	// alloc/free), so it is safe from physics callbacks (shooting from
	// _physics_process, spawning from hit handlers). A shape-TYPE change
	// recreates RIDs and must wait for a safe point.
	const PhysicsServer2D::ShapeType incoming_effective = CollisionShapeHelper2D::get_effective_type(data.collision_shape, false);
	const bool shape_type_changes = (incoming_effective != cached_effective_shape_type);
	// Only THIS volley's own tick is off limits (its drain/lifetime pass is
	// still walking its vectors). Other pooled volleys reuse fine mid-sweep.
	if (is_being_ticked) {
		UtilityFunctions::push_error("enable_volley cannot run on a volley while its own tick is running (e.g. from its collision handler or attachment callback). Spawn a new volley instead.");
		return false;
	}
	if (shape_type_changes && bullet_factory != nullptr && bullet_factory->is_structural_mutation_unsafe()) {
		UtilityFunctions::push_error("enable_volley with a different collision shape type cannot run inside a physics frame (server flush locks apply to the shape RIDs it must recreate). Spawn through BulletFactory2D (a shape change pops a matching pool bucket) or call enable_volley from an idle frame.");
		return false;
	}
	// Re-enabling a live volley would wipe its attachments, timers, curves and
	// patterns mid-flight.
	if (is_active) {
		UtilityFunctions::push_error("enable_volley: instance is already active. Disable it first or spawn a new volley instead.");
		return false;
	}
	if (is_queued_for_deletion()) {
		UtilityFunctions::push_error("enable_volley: multimesh is queued for deletion.");
		return false;
	}
	// Never-spawned instances hold no bullet storage (spawn() allocates it).
	if (multi.is_null() || !multi.is_valid()) {
		UtilityFunctions::push_error("enable_volley: multimesh was never spawned through BulletFactory2D (no bullet storage). Spawn it first.");
		return false;
	}
	// The factory spawn paths validate finiteness; a direct call must too.
	if (!new_inherited_velocity_offset.is_finite()) {
		UtilityFunctions::push_error("enable_volley: inherited velocity offset must be finite, keeping the old value.");
		return false;
	}
	// A direct enable on a pooled instance takes it out of its bucket (the
	// factory pop already did when it got here through spawn_volley).
	if (is_pooled_in_pool && bullets_pool != nullptr) {
		bullets_pool->try_remove_instance(this, get_pool_key());
	}

	// Reused RIDs keep their original type: a shape-type change recreates
	// them exactly like set_collision_shape_runtime() does.
	cache_collision_shape_typed(data.collision_shape);
	if (shape_type_changes && physics_server != nullptr && area.is_valid()) {
		release_volley_shape();
		generate_physics_shapes_for_area(amount_bullets);
	}

	const uint64_t self_id = get_instance_id();
	begin_life(data, spawner_id, new_inherited_velocity_offset);
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return false;
	}

	set_all_physics_shapes_enabled_for_area(true);
	// Shared spawn-data attachments (slots were blanked by begin_life).
	apply_shared_bullet_attachment_from_data(data);
	move_to_front(); // Pooled instances render behind newer ones without this; moving to front emulates fresh spawn order.
	// Previous life's bakes die here, trail shards rebuild, spawn flashes fire.
	fx_reseed_from_data(data.effect_layers, true);
	set_visible(true);
	all_bullets_enabled_set.activate_all_data();
	is_active = true;
	// Join the factory tick (the spawn path does this too; the sparse set
	// dedups). Without it a direct enable_volley() call left an active
	// volley that never moved.
	if (bullet_factory != nullptr) {
		bullet_factory->track_volley_active(*this);
	}
	return true;
}

void BulletVolley2D::enable_bullet(int bullet_index, int collision_amount, bool should_enable_attachment) {
	// Wake = RESUME (freeze contract): the bullet continues exactly where
	// disable_bullet() froze it (ballistics, hit count, bounce ledger, homing
	// queue, orbit, wobble, gravity, pattern progress, custom data). Waking
	// the LAST disabled bullet of a parked volley resumes the whole volley,
	// owner and timers included; an expired parked volley gets a fresh
	// lifetime (both clocks restart together). A POOLED volley is refused:
	// it was released, so this handle is stale.
	if (!validate_bullet_index(bullet_index, "enable_bullet")) {
		return;
	}
	if (is_queued_for_deletion()) {
		UtilityFunctions::push_error("enable_bullet: multimesh is queued for deletion.");
		return;
	}
	if (is_pooled_in_pool) {
		UtilityFunctions::push_error("enable_bullet: this volley went back to the pool when its last bullet died (is_auto_pooling_enabled was on), so this handle is stale. Spawn a new volley, or set is_auto_pooling_enabled = false before the last bullet dies to keep (park) the volley.");
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
	if (all_bullets_enabled_set.contains(bullet_index)) {
		return;
	}
	// A wake from inside a sweep (on_bullet_disable handler during a pool
	// release or a clear) would resurrect a volley that is being torn down.
	if (bullet_factory != nullptr && bullet_factory->get_is_factory_busy()) {
		UtilityFunctions::push_error("enable_bullet: cannot wake a bullet while the factory is busy (e.g. inside an on_bullet_disable handler during a sweep). Wake it after the sweep (e.g. from the next frame).");
		return;
	}
	// The attachment callback below runs user code that may re-enter
	// enable_bullet()/disable_bullet(): claim the slot (set + counter) first
	// and reject nested calls, so the counter never drifts vs the sparse set.
	if (_bullet_enable_depth > 0) {
		UtilityFunctions::push_error("enable_bullet: re-entrant call from inside on_bullet_enable is not allowed. Use call_deferred to change bullet state from that callback.");
		return;
	}
	ReentrancyGuard bullet_enable_guard(_bullet_enable_depth);

	const bool waking_volley = !is_active;
	all_bullets_enabled_set.activate_data(bullet_index);
	++active_bullets_counter;
	if (active_bullets_counter > amount_bullets) {
		active_bullets_counter = amount_bullets;
	}
	// A wake is a new slot life: records queued before the disable must not
	// fire now (same-overlap double count).
	bump_collision_epoch_for_bullet(bullet_index);
	// The freeze ended any graze visit: a fresh one starts (no exit fires
	// for a visit that ended frozen).
	graze_end_visits_of_bullet(bullet_index);

	multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(all_cached_instance_transforms[bullet_index])); // Start rendering the instance
	write_trail_instances(bullet_index); // Wake resumes the trail with the bullet
	physics_server->area_set_shape_disabled(area, bullet_index, false);
	// Woken bullets must not lerp from a stale pre-disable position.
	update_bullet_previous_transform_for_interpolation(bullet_index);

	// collision_amount -1 (default) keeps the frozen hit count. 0+ sets how
	// many hits the bullet has already taken, clamped like
	// set_bullet_collision_count (at/above max leaves exactly one hit).
	if (collision_amount >= 0) {
		int &count = bullets_current_collision_count[bullet_index];
		if (bullet_max_collision_count > 0 && collision_amount >= bullet_max_collision_count) {
			count = bullet_max_collision_count - 1;
		} else {
			count = collision_amount;
		}
	}

	if (waking_volley) {
		// Expired parked volley: a fresh lifetime, both clocks restart
		// together (unit curves sample elapsed / max_life_time). A manual
		// drain keeps its remaining time and clock.
		if (!is_life_time_infinite && current_life_time <= 0.0) {
			current_life_time = max_life_time;
			curves_elapsed_time = 0.0;
		}
		is_active = true;
		set_visible(true);
		if (bullet_factory != nullptr) {
			bullet_factory->track_volley_active(*this);
		}
		// Disabled bullets are not rendered, but their prev cache is stale:
		// sync all so a later wake never lerps from a pre-disable pose.
		update_all_previous_transforms_for_interpolation();
	}

	if (should_enable_attachment) {
		bullet_enable_attachment(bullet_index); // resumes a suspended attachment (on_bullet_enable)
	}
}

void BulletVolley2D::disable_bullet(int bullet_index, bool release_attachment, bool reset_state) {
	// Disable = FREEZE: the bullet stops rendering, colliding and moving, and
	// keeps every runtime value for enable_bullet() to resume. reset_state
	// clears its runtime ledgers (see bullet_reset_state). The attachment is
	// either returned to the attachment pool (default) or suspended in its
	// slot (on_bullet_disable now, on_bullet_enable on wake).
	if (!validate_bullet_index(bullet_index, "disable_bullet")) {
		return;
	}
	// Teardown-adjacent path (PREDELETE frees the area): never null-deref.
	if (multi.is_null() || !multi.is_valid()) {
		return;
	}
	if (physics_server == nullptr || !area.is_valid()) {
		return;
	}
	// Same re-entrancy latch as enable_bullet(): the attachment callback
	// below runs user code that may call enable_bullet()/disable_bullet().
	if (_bullet_enable_depth > 0) {
		UtilityFunctions::push_error("disable_bullet: re-entrant call from inside on_bullet_enable/on_bullet_disable is not allowed. Use call_deferred to change bullet state from that callback.");
		return;
	}
	if (!all_bullets_enabled_set.contains(bullet_index)) {
		// Already frozen: a reset request still applies to its ledgers.
		if (reset_state) {
			bullet_reset_state(bullet_index);
		}
		return;
	}
	ReentrancyGuard bullet_disable_guard(_bullet_enable_depth);

	all_bullets_enabled_set.disable_data(bullet_index);
	--active_bullets_counter;
	if (active_bullets_counter < 0) {
		active_bullets_counter = 0;
	}
	// Records queued before this disable must not fire after a re-enable.
	bump_collision_epoch_for_bullet(bullet_index);
	if (reset_state) {
		reset_bullet_runtime_state(bullet_index);
	}

	multi->set_instance_transform_2d(bullet_index, zero_transform); // Stops rendering the instance
	hide_trail_instances(bullet_index); // Trail dies with its bullet, same frame
	physics_server->area_set_shape_disabled(area, bullet_index, true);

	const uint64_t self_id = get_instance_id();
	if (release_attachment) {
		bullet_disable_attachment(bullet_index);
	} else {
		suspend_bullet_attachment(bullet_index);
	}
	// The attachment callback ran user code: stop if it freed this volley.
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return;
	}
	if (active_bullets_counter <= 0 && is_active) {
		on_volley_drained();
	}
}

int BulletVolley2D::disable_bullets_bulk(const std::vector<int> &indexes, int fx_trigger, const std::vector<uint64_t> *expected_epochs) {
	// Same per-bullet contract as disable_bullet(i, true) (freeze, epoch
	// bump, trail hidden, shape off, attachment released), but the render
	// side is batched: the per-instance zero writes are collected into ONE
	// buffer upload when the batch drains the volley (expiry / clear of a
	// whole volley: N render-server calls -> 1). fx_trigger >= 0 fires that
	// one-shot layer at each disabled bullet's pose. expected_epochs (same
	// length as indexes) skips bullets a handler took over in between.
	if (multi.is_null() || !multi.is_valid() || physics_server == nullptr || !area.is_valid()) {
		return 0;
	}
	if (_bullet_enable_depth > 0) {
		UtilityFunctions::push_error("disable_bullet: re-entrant call from inside on_bullet_enable/on_bullet_disable is not allowed. Use call_deferred to change bullet state from that callback.");
		return 0;
	}
	const uint64_t self_id = get_instance_id();
	int disabled_count = 0;
	{
		ReentrancyGuard bullet_disable_guard(_bullet_enable_depth);
		for (size_t k = 0; k < indexes.size(); ++k) {
			const int i = indexes[k];
			if (i < 0 || i >= amount_bullets || !all_bullets_enabled_set.contains(i)) {
				continue;
			}
			if (expected_epochs != nullptr && k < expected_epochs->size() && (*expected_epochs)[k] != collision_epoch_for_bullet(i)) {
				continue;
			}
			const bool fx_have_pose = fx_trigger >= 0 && i < (int)all_cached_instance_transforms.size();
			const Transform2D fx_pose = fx_have_pose ? all_cached_instance_transforms[i] : Transform2D();
			all_bullets_enabled_set.disable_data(i);
			--active_bullets_counter;
			if (active_bullets_counter < 0) {
				active_bullets_counter = 0;
			}
			bump_collision_epoch_for_bullet(i);
			++disabled_count;
			hide_trail_instances(i);
			physics_server->area_set_shape_disabled(area, i, true);
			if (i < (int)attachments.size() && attachments[i] != nullptr) {
				bullet_disable_attachment(i); // user callback (on_bullet_disable)
				if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
					return disabled_count; // freed by the callback: touch nothing more
				}
			}
			if (fx_have_pose) {
				fx_fire_oneshot(fx_trigger, i, fx_pose);
			}
		}
	}
	if (disabled_count == 0) {
		return 0;
	}
	// Render: zero every dead instance. A drained volley uploads one zero
	// buffer; a partial batch writes only the bullets it disabled.
	if (active_bullets_counter <= 0 && (int)batch_buffer.size() == amount_bullets * 8 && multi->get_instance_count() == amount_bullets) {
		batch_buffer.fill(0.0f);
		multi->set_buffer(batch_buffer);
	} else {
		for (int i : indexes) {
			if (i >= 0 && i < amount_bullets && !all_bullets_enabled_set.contains(i)) {
				multi->set_instance_transform_2d(i, zero_transform);
			}
		}
	}
	if (active_bullets_counter <= 0 && is_active) {
		on_volley_drained();
	}
	return disabled_count;
}

void BulletVolley2D::bullet_reset_state(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_reset_state")) {
		return;
	}
	reset_bullet_runtime_state(bullet_index);
}

void BulletVolley2D::all_bullets_reset_state(int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_reset_state", [&](int i) { reset_bullet_runtime_state(i); });
}

void BulletVolley2D::reset_bullet_runtime_state(int bullet_index) {
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return;
	}
	// Runtime LEDGERS only (what the bullet accumulated while flying).
	// Kinematics (pose, direction, speed, rotation), per-bullet configuration
	// (curves, wobble/gravity seeds, custom data) and the attachment stay.
	if (bullet_index < (int)bullets_current_collision_count.size()) {
		bullets_current_collision_count[bullet_index] = 0;
	}
	for (int z = 0; z < graze_zone_slots; ++z) {
		const size_t at = (size_t)bullet_index * graze_zone_slots + z;
		if (at < graze_state.size()) {
			graze_state[at] = 0;
		}
	}
	visit_bounce_ledger([&](auto &v, auto empty) {
		if (bullet_index < (int)v.size()) {
			v[bullet_index] = empty;
		}
	});
	if (bullet_index < (int)all_gravity_velocity.size()) {
		all_gravity_velocity[bullet_index] = Vector2(0, 0);
	}
	if (bullet_index < (int)all_bullet_wobble.size()) {
		all_bullet_wobble[bullet_index].has_last = false;
	}
	if (bullet_index < (int)wobble_distance_traveled.size()) {
		wobble_distance_traveled[bullet_index] = 0.0;
	}
	if (bullet_index < (int)all_movement_pattern_data.size()) {
		all_movement_pattern_data[bullet_index].distance_traveled = 0.0;
	}
	if (bullet_index < (int)shared_movement_pattern_distances.size()) {
		shared_movement_pattern_distances[bullet_index] = 0.0;
	}
	if (bullet_index < (int)all_bullet_homing_targets.size() && bullet_index < (int)all_homing_count.size()) {
		homing_drop_own_targets(bullet_index);
	}
	if (bullet_index < (int)all_shared_homing_reached.size()) {
		all_shared_homing_reached[bullet_index] = SharedHomingReachedState();
	}
	if (bullet_index < (int)all_orbiting_status.size() && all_orbiting_status[bullet_index]) {
		all_orbiting_status[bullet_index] = 0;
		--active_orbiting_count;
		if (active_orbiting_count < 0) {
			active_orbiting_count = 0;
		}
	}
	if (bullet_index < (int)all_orbiting_data.size()) {
		all_orbiting_data[bullet_index] = OrbitingData();
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
	// bullet, but the last-bullet disable releases the life and pools the
	// instance; the DESTROY path fires the same way).
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
	// Snapshot first: the batch mutates the live set. One On Clear per
	// cleared bullet; a drained volley pays one render upload.
	// A local copy, never a member: an attachment callback may re-enter
	// clear_all_bullets() (rejected by the latch inside, but it must not
	// rewrite the list this batch is walking).
	const std::vector<int> live = all_bullets_enabled_set.get_active_indexes();
	return disable_bullets_bulk(live, EFFECT_ON_CLEAR);
}

BulletVolley2D::~BulletVolley2D() {
	for (auto &queue : all_bullet_homing_targets) {
		queue.clear_homing_targets(cached_mouse_global_position);
	}
	shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
}

void BulletVolley2D::seed_motion_features(const BulletVolleyData2D &data) {
	const BulletVolleyData2D *volley_data = &data;
	// Reset every motion feature to its blank state first (the apply_* calls
	// below then seed from the data); the speed/direction/velocity arrays are
	// sized and fully written by set_up_movement_data below.
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
	shared_movement_pattern_curve.unref();
	shared_movement_pattern_face_movement_direction = false;
	shared_movement_pattern_repeat = true;
	shared_movement_pattern_distances.assign(amount_bullets, 0.0);
	all_bullet_curves_data.assign(amount_bullets, Ref<BulletCurvesData2D>());
	all_movement_pattern_data.assign(amount_bullets, BulletMovementPatternData2D());
	shared_bullet_curves_data.unref();
	// Rotation presence is (re)derived by set_rotation_data, which begin_life
	// runs before this seed; resetting it here would erase authored entries.

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

void BulletVolley2D::on_volley_deactivated() {
	// Dying life: reached events collected this tick die with it.
	homing_reached_events.clear();
	shared_pop_requested = false;
	if (bullet_factory != nullptr) {
		bullet_factory->track_volley_inactive(*this);
	}
}

} // namespace BlastBullets2D
