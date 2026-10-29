// BulletFactory2D lifecycle and frame loop: tree entry (_ready / lazy init), the
// volley container and debugger, physics interpolation, processing on/off, the
// per-frame volley sweep (_physics_process -> tick_volleys) and teleporting.
// Spawning: bullet_factory2d_spawn.cpp. Freeing/resetting: bullet_factory2d_structural.cpp.

#include "bullet_spawner/bullet_spawner2d.hpp"
#include "core/physics_flush2d.hpp"
#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletFactory2D::snapshot_active_volleys() {
	// Snapshot (id, pointer) per active volley; see VolleyIterationEntry.
	const std::vector<int> &dense = volley_set.get_active_indexes();
	iteration_scratch.clear();
	iteration_scratch.reserve(dense.size());
	for (int index : dense) {
		if (index < 0 || index >= (int)all_volleys.size() || all_volleys[index] == nullptr) {
			continue;
		}
		VolleyIterationEntry entry;
		entry.volley = all_volleys[index];
		entry.id = entry.volley->get_cached_instance_id();
		iteration_scratch.push_back(entry);
	}
}

std::vector<BulletFactory2D::VolleyIterationEntry> BulletFactory2D::snapshot_volley_entries() const {
	std::vector<VolleyIterationEntry> entries;
	entries.reserve(all_volleys.size());
	for (BulletVolley2D *volley : all_volleys) {
		if (volley != nullptr) {
			VolleyIterationEntry entry;
			entry.volley = volley;
			entry.id = volley->get_cached_instance_id();
			entries.push_back(entry);
		}
	}
	return entries;
}

// The snapshot entry's volley if it still exists (a handler may have freed
// it since), else nullptr. One ObjectDB lookup, no dereference before it.
static _ALWAYS_INLINE_ BulletVolley2D *resolve_iteration_entry(uint64_t id, BulletVolley2D *volley) {
	return ObjectDB::get_instance(ObjectID(id)) == (Object *)volley ? volley : nullptr;
}

// Shot timing (see the header): what a timed shot fires starts on time.
double BulletFactory2D::first_tick_lead(double overshoot_sec) const {
	const Engine *engine = Engine::get_singleton();
	// The delta a sweep runs on (Engine.time_scale included, like the
	// spawner clock the overshoot was measured on).
	const double tick = get_physics_process_delta_time();
	const double step = Math::is_finite(tick) && tick > 0.0 ? tick : 0.0;
	const double overshoot = Math::is_finite(overshoot_sec) && overshoot_sec > 0.0 ? overshoot_sec : 0.0;
	if (engine->is_in_physics_frame()) {
		return sweep_physics_frame == engine->get_physics_frames() ? overshoot : overshoot - step;
	}
	const double fraction = engine->get_physics_interpolation_fraction();
	return overshoot - (Math::is_finite(fraction) ? Math::clamp(fraction, 0.0, 1.0) : 0.0) * step;
}

// The delta one volley runs on: the bullet-world delta times the time scale of
// its owner spawner while that lives (and opted in). Orphans, factory-spawned
// and pooled volleys (owner 0) run on the factory scale alone.
double BulletFactory2D::volley_time_factor(const BulletVolley2D &volley) const {
	double factor = time_scale * volley.own_time_scale;
	const uint64_t scale_owner = volley.owner_spawner_id != 0 ? volley.owner_spawner_id : volley.pickup_time_scale_owner_id;
	if (scale_owner != 0) {
		const PatternSpawner2D *spawner = Object::cast_to<PatternSpawner2D>(ObjectDB::get_instance(ObjectID(scale_owner)));
		if (spawner != nullptr) {
			factor *= spawner->get_fired_time_factor();
		}
	}
	return Math::is_finite(factor) && factor > 0.0 ? factor : 0.0;
}

bool BulletFactory2D::tick_volleys(double delta, const bool &alive) {
	if (!Math::is_finite(delta) || delta < 0.0) {
		return true;
	}
	snapshot_active_volleys();
	// Flocks steer by where every member stood before this sweep moved any
	// of them (no user code runs between the snapshot and the pass).
	if (flock_armed_volleys > 0) {
		run_flock_pass(delta);
	} else if (flock_solver.has_results()) {
		// The last armed volley left: drop the old results once.
		flock_solver.begin(sweep_counter);
		stats_last_flock_usec = 0;
	}
	const uint64_t snapshot_epoch = volley_free_epoch;
	for (const VolleyIterationEntry &entry : iteration_scratch) {
		BulletVolley2D *volley = volley_free_epoch == snapshot_epoch ? entry.volley : resolve_iteration_entry(entry.id, entry.volley);
		// Freed, parked/pooled by an earlier handler, or already ticked (or
		// begun a new life) this sweep: nothing to do.
		if (volley == nullptr || !volley->is_active || volley->sweep_tick_stamp == sweep_counter) {
			continue;
		}
		volley->sweep_tick_stamp = sweep_counter;
		const double time_factor = volley_time_factor(*volley);
		double first_delta = delta;
		if (volley->first_tick_lead != 0.0) {
			// Its first tick: from when its shot was due (first_tick_lead).
			first_delta = Math::max(0.0, delta + volley->first_tick_lead);
			volley->first_tick_lead = 0.0;
		}
		const double volley_delta = capped_volley_delta(first_delta, time_factor);
		volley->set_real_tick_delta(delta);
		// Flag the volley for its whole tick: handlers fired from inside
		// (collision drain, attachment callbacks) may spawn, and the pool must
		// never hand out the volley whose drain is still running. Liveness is
		// re-checked between steps: a handler that free()s the volley
		// (against the contract) must not crash the sweep.
		++stats_tick_volleys;
		stats_tick_bullets += volley->active_bullets_counter;
		volley->is_being_ticked = true;
		const uint64_t epoch_before_tick = volley_free_epoch;
		// Attachments hear the scale change first (user code: it may free the
		// volley, hence the same liveness check as after the tick).
		volley->apply_attachment_time_scale(time_factor);
		if (!alive) {
			return false; // user code freed this factory
		}
		if (volley_free_epoch != epoch_before_tick && ObjectDB::get_instance(ObjectID(entry.id)) != (Object *)volley) {
			continue;
		}
		volley->tick(volley_delta);
		if (!alive) {
			return false;
		}
		if (volley_free_epoch != epoch_before_tick && ObjectDB::get_instance(ObjectID(entry.id)) != (Object *)volley) {
			continue; // a handler freed this volley
		}
		volley->is_being_ticked = false;
		// A handler paused the factory: every volley not ticked yet this
		// frame stays exactly where it is (hit-stop freezes the frame).
		if (!is_factory_processing_bullets) {
			break;
		}
	}
	return true;
}

void BulletFactory2D::interpolate_volleys() {
	// Interpolation only reads, but a re-entrant free/reset mid-loop mutates
	// all_volleys under iteration; resolving by id keeps a swap-remove from
	// skipping or touching a freed volley (same reasoning as the tick).
	snapshot_active_volleys();
	const uint64_t snapshot_epoch = volley_free_epoch;
	for (const VolleyIterationEntry &entry : iteration_scratch) {
		BulletVolley2D *volley = volley_free_epoch == snapshot_epoch ? entry.volley : resolve_iteration_entry(entry.id, entry.volley);
		if (volley == nullptr || !volley->is_active) {
			continue;
		}
		volley->interpolate_bullet_visuals();
	}
}

FactoryOperationGuard::FactoryOperationGuard(BulletFactory2D *p_factory, bool p_manage_debuggers, bool p_defer_debugger_restore) :
		factory(p_factory), manage_debuggers(p_manage_debuggers), defer_debugger_restore(p_defer_debugger_restore) {
	life = factory->life_token();
	started_alive = *life;
	saved_busy = factory->is_factory_busy;
	resume_processing = factory->is_factory_processing_bullets;
	if (manage_debuggers) {
		saved_debuggers = factory->get_is_debugger_enabled();
		if (saved_debuggers) {
			factory->set_is_debugger_enabled(false);
		}
	}
	factory->is_factory_busy = true;
	factory->set_is_factory_processing_bullets(false);
}

FactoryOperationGuard::~FactoryOperationGuard() {
	if (started_alive && !*life) {
		return; // user code inside the operation freed the factory
	}
	// Drop the busy flag before resuming anything - resuming while busy errors out, and doing it in this order keeps a nested busy state intact.
	factory->is_factory_busy = false;
	if (resume_processing) {
		factory->set_is_factory_processing_bullets(true);
	}
	if (manage_debuggers && saved_debuggers) {
		// Immediate outside physics processing (no frame of missing
		// debugger); deferred within it, where tree mutation could race
		// the debugger tick. Always deferred when the caller runs inside a
		// PREDELETE notification (immediate rebuild crashes there).
		if (defer_debugger_restore || Engine::get_singleton()->is_in_physics_frame()) {
			factory->call_deferred("set_is_debugger_enabled", true);
		} else {
			factory->set_is_debugger_enabled(true);
		}
	}
	factory->is_factory_busy = saved_busy;
}

std::vector<BulletFactory2D *> BulletFactory2D::factories_in_tree;

bool node_in_bullet_factory2d(Node *node) {
	for (BulletFactory2D *factory : BulletFactory2D::factories_in_tree) {
		if (factory == node || factory->is_ancestor_of(node)) {
			return true;
		}
	}
	return false;
}

void BulletFactory2D::queue_physics_sync(BulletVolley2D *volley) {
	VolleyIterationEntry entry;
	entry.id = volley->get_instance_id();
	entry.volley = volley;
	physics_sync_queue.push_back(entry);
	if (!physics_sync_call_queued) {
		physics_sync_call_queued = true;
		callable_mp(this, &BulletFactory2D::flush_physics_sync).call_deferred();
	}
}

void BulletFactory2D::flush_physics_sync() {
	physics_sync_call_queued = false;
	if (physics_sync_queue.empty()) {
		return;
	}
	std::vector<VolleyIterationEntry> batch;
	batch.swap(physics_sync_queue);
	for (const VolleyIterationEntry &entry : batch) {
		if (ObjectDB::get_instance(ObjectID(entry.id)) == entry.volley) {
			entry.volley->apply_physics_sync(); // no user code
		}
	}
}

void BulletFactory2D::on_scene_physics_frame() {
	PhysicsFlush2D::note_scene_physics_frame();
}

void BulletFactory2D::_notification(int p_what) {
	// The callbacks (core/native_callbacks2d.hpp): gated by the plugin's own
	// switches, a script may keep the engine processing for its callbacks.
	if (p_what == NOTIFICATION_PHYSICS_PROCESS) {
		if (callbacks.physics_on) {
			native_physics_process(get_physics_process_delta_time());
		}
		return;
	}
	if (p_what == NOTIFICATION_PROCESS) {
		if (callbacks.idle_on) {
			native_process(get_process_delta_time());
		}
		return;
	}
	if (p_what == NOTIFICATION_READY) {
		native_ready();
		return;
	}
	if (p_what == NOTIFICATION_ENTER_TREE) {
		factories_in_tree.push_back(this);
		// The physics flush runs before the SceneTree's physics_frame
		// (core/physics_flush2d.hpp): runtime only, the editor has no flush.
		if (!Engine::get_singleton()->is_editor_hint()) {
			const Callable on_frame = callable_mp(this, &BulletFactory2D::on_scene_physics_frame);
			if (!get_tree()->is_connected("physics_frame", on_frame)) {
				get_tree()->connect("physics_frame", on_frame);
			}
		}
		// Not _ready: the factory skips it in the editor, and the preview is
		// the one thing it draws there.
		refresh_despawn_bounds_preview();
		// Re-entry (_ready runs once): take the monitors when nobody has them.
		if (is_ready) {
			register_monitors();
		}
		return;
	}
	if (p_what == NOTIFICATION_EXIT_TREE) {
		factories_in_tree.erase(std::remove(factories_in_tree.begin(), factories_in_tree.end(), this), factories_in_tree.end());
		const Callable on_frame = callable_mp(this, &BulletFactory2D::on_scene_physics_frame);
		if (get_tree()->is_connected("physics_frame", on_frame)) {
			get_tree()->disconnect("physics_frame", on_frame);
		}
		if (owns_performance_monitors) {
			unregister_monitors();
			hand_monitors_to_another_factory();
		}
		return;
	}
	if (p_what == NOTIFICATION_PREDELETE) {
		if (emit_guard.depth > 0 && predelete_state == PREDELETE_NONE) {
			// Freed from a handler of one of its own signals (a release
			// build: debug builds refuse first). The emit queues it.
			cancel_free();
			emit_guard.refused_free = true;
			return;
		}
		if (predelete_state == PREDELETE_RUNNING) {
			// Freed again from user code inside its own teardown (an
			// attachment callback of a dying volley): a second deletion would
			// free this memory under the running one.
			cancel_free();
			refused_nested_free = true;
			return;
		}
		if (predelete_state == PREDELETE_DONE) {
			return; // the deferred deletion below: everything is torn down
		}
		predelete_state = PREDELETE_RUNNING;
		*alive_flag = false;
		if (owns_performance_monitors) {
			unregister_monitors();
			hand_monitors_to_another_factory();
		}
		// Parent is notified before children are destroyed. From here on no child
		// pointer (debuggers, containers) may be touched by teardown paths.
		is_tearing_down = true;
		drop_pending_splits(); // destroyed bullets never split; their Refs go now
		drop_pending_emits(); // nor fire
		detach_lasers_for_teardown(); // the children die with the tree; never call back
		// Pooled attachments outlive this call as engine children; drop their pool
		// tracking now so their later PREDELETEs never touch this pool object again.
		bullet_attachments_pool.detach_all();
		// Effect shards are our children and die with the tree; drop the bake
		// records (with their raw node pointers) before any volley PREDELETE
		// below tries to unregister through them.
		fx_unregister_all_bakes();
		// A volley emitting its own signal whose handler freed this factory
		// must not be deleted under Godot's emit (the engine reports "freed
		// while a signal is being emitted from it"): it leaves the tree and
		// the tracking now and dies at the end of the frame. Its own
		// liveness checks (factory_life) stop it touching this factory.
		for (const VolleyIterationEntry &entry : snapshot_volley_entries()) {
			BulletVolley2D *volley = entry.volley;
			if (ObjectDB::get_instance(ObjectID(entry.id)) != volley || volley->self_emit_depth <= 0) {
				continue;
			}
			remove_volley_from_tracking(volley);
			if (Node *parent = volley->get_parent()) {
				parent->remove_child(volley);
			}
			volley->queue_free();
		}
		// A volley a script moved out of this subtree (local-space bullets
		// under a moving ship) would outlive the factory holding a dangling
		// factory pointer, and its own teardown would call into freed
		// memory. Free every tracked volley now, while this factory is valid.
		for (BulletVolley2D *volley : all_volleys) {
			if (volley != nullptr && !is_ancestor_of(volley)) {
				free_all_volleys(nullptr);
				break;
			}
		}
		// The children die HERE, not in Node's own PREDELETE that runs after
		// this handler: their teardown runs user code (attachment callbacks of
		// the dying volleys) that may free this factory again, which is
		// refused only while this handler runs.
		for (int64_t count = get_child_count(true); count > 0;) {
			memdelete(get_child(count - 1, true));
			const int64_t left = get_child_count(true);
			if (left >= count) {
				break; // a child refused to die: Node's own teardown handles the rest
			}
			count = left;
		}
		predelete_state = PREDELETE_DONE;
		if (refused_nested_free) {
			// Godot keeps ONE "free ok" flag per object: the refused free
			// cancelled this deletion too. Finish it at the end of the frame.
			queue_free();
		}
	}
}

void BulletFactory2D::native_ready() {
	// Ensure the code that is next will not be ran in the editor
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	// Once: a spawn before the READY notification (from a script _ready,
	// which runs first) already started it through ensure_factory_initialized.
	if (is_ready) {
		return;
	}

	// Use default physics space if physics_space is invalid
	if (physics_space.is_valid() == false) {
		Ref<World2D> world = get_world_2d();
		if (world.is_null()) {
			UtilityFunctions::push_error("BulletFactory2D needs to be inside a World2D (a Node2D scene tree) to work.");
			return;
		}
		physics_space = world->get_space();
	}

	all_volleys.reserve(2048);
	volley_set.resize(2048);

	add_bullet_containers();
	add_bullet_attachment_container();
	add_debuggers();
	apply_debugger_settings(*volley_debugger.ptr);

	update_process_state();

	is_ready = true;
	register_monitors();
	resolve_physics_interpolation();

	// On while Godot's own interpolation is off: bullets are smooth but every
	// other node (the player, the camera) steps, which reads as bullet jitter.
	if (physics_interpolation == INTERPOLATION_ON && !ProjectSettings::get_singleton()->get_setting("physics/common/physics_interpolation", false)) {
		UtilityFunctions::push_warning("BulletFactory2D: physics_interpolation is On but ProjectSettings physics/common/physics_interpolation is OFF, so the rest of the scene is not interpolated. Enable it in Project Settings (or use Follow Project). See debug_check_interpolation_status().");
	}
}

bool BulletFactory2D::ensure_factory_initialized() {
	if (is_ready) {
		return true;
	}
	if (Engine::get_singleton()->is_editor_hint() || !is_inside_tree() || is_tearing_down) {
		return false;
	}
	// Used before its READY notification (a spawn from a script _ready that
	// runs first): the native start now, once.
	native_ready();
	return is_ready;
}

void BulletFactory2D::set_physics_interpolation(PhysicsInterpolation value) {
	if (value < INTERPOLATION_OFF || value > INTERPOLATION_FOLLOW_PROJECT) {
		UtilityFunctions::push_error("BulletFactory2D: physics_interpolation must be 0 (Off), 1 (On) or 2 (Follow Project), keeping the old value.");
		return;
	}
	physics_interpolation = value;
	resolve_physics_interpolation();
}

void BulletFactory2D::resolve_physics_interpolation() {
	bool want = physics_interpolation == INTERPOLATION_ON;
	if (physics_interpolation == INTERPOLATION_FOLLOW_PROJECT) {
		const SceneTree *tree = is_inside_tree() ? get_tree() : nullptr;
		want = tree != nullptr && tree->is_physics_interpolation_enabled();
	}
	if (want == use_physics_interpolation || !is_ready || is_tearing_down || is_factory_busy || is_iterating_bullets) {
		return; // nothing to do, or the next physics frame applies it
	}
	// A flag flip and a copy of poses: no user code runs and no vector is
	// rebuilt, so no operation guard (pausing the factory for it would pause
	// its sound voices too).
	use_physics_interpolation = want;
	// Turning it on mid-game needs previous-frame data to exist, so seed it here.
	// Without this the first interpolated frames would lerp from stale transforms.
	if (use_physics_interpolation) {
		for (BulletVolley2D *volley : all_volleys) {
			if (volley != nullptr) {
				volley->update_all_previous_transforms_for_interpolation();
			}
		}
	}
	update_process_state();
}

void BulletFactory2D::add_bullet_containers() {
	// Create BulletVolleysContainer Node and add it as a child to factory
	Node *container = memnew(Node);
	volley_container.set(container);
	container->set_name("BulletVolleysContainer");
	add_child(container);
}

void BulletFactory2D::add_bullet_attachment_container() {
	// Create BulletAttachmentContainer Node and add it as a child to factory
	Node *container = memnew(Node);
	bullet_attachments_container.set(container);
	container->set_name("BulletAttachmentsContainer");
	add_child(container);
}

void BulletFactory2D::add_debuggers() {
	// Configure BulletVolley2D debugger and add it as a child to factory
	BulletVolleyDebugger2D *debugger = memnew(BulletVolleyDebugger2D);
	volley_debugger.set(debugger);
	debugger->configure(volley_container.get(), "BulletVolleysDebugger", debugger_color_cached_before_ready);
	debugger->set_debugger_z_index(debugger_z_index);
	add_child(debugger);
}

void BulletFactory2D::apply_debugger_settings(BulletVolleyDebugger2D &debugger) const {
	debugger.set_debugger_color(debugger_color_cached_before_ready);
	debugger.set_is_debugger_enabled(is_debugger_enabled_cached_before_ready);
	debugger.set_max_debug_providers(debugger_max_providers_cached_before_ready);
	debugger.set_draw_inactive_shapes(debugger_draw_inactive_cached_before_ready);
}

void BulletFactory2D::heal_owned_children() {
	if (!is_ready || is_tearing_down) {
		return;
	}
	const bool volleys_lost = volley_container.get() == nullptr;
	if (volleys_lost) {
		add_bullet_containers(); // its volleys died with it (their PREDELETE untracked them)
	}
	if (bullet_attachments_container.get() == nullptr) {
		add_bullet_attachment_container();
	}
	BulletVolleyDebugger2D *debugger = volley_debugger.get();
	if (debugger == nullptr) {
		add_debuggers();
		apply_debugger_settings(*volley_debugger.ptr);
	} else if (volleys_lost) {
		// configure() switches it off; on again watches the new container.
		debugger->configure(volley_container.get(), "BulletVolleysDebugger", debugger->get_debugger_color());
		debugger->set_debugger_z_index(debugger_z_index);
		debugger->set_is_debugger_enabled(is_debugger_enabled_cached_before_ready);
	}
}

bool BulletFactory2D::get_is_factory_busy() const {
	return is_factory_busy;
}

bool BulletFactory2D::get_is_factory_processing_bullets() const {
	return is_factory_processing_bullets;
}

void BulletFactory2D::set_is_factory_processing_bullets(bool is_processing_enabled) {
	// When trying to set processing to enabled but the factory is currently busy, then something went wrong
	// The only time you can call this method is if all tasks were completed and the factory is free to do its work
	if (is_processing_enabled && is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to call set_is_factory_processing_bullets. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	const bool resuming = is_processing_enabled && !is_factory_processing_bullets;
	is_factory_processing_bullets = is_processing_enabled;
	sound_mixer.set_paused(!is_processing_enabled);

	// Overlaps that started during the pause were parked by the volleys;
	// queue them now so the next tick drains them (exactly once).
	// The replay reads target properties for the bounce math (a script _get
	// is user code: it may free volleys or this factory), so walk a snapshot.
	if (resuming) {
		const LifeWatch watch = watch_life();
		for (const VolleyIterationEntry &entry : snapshot_volley_entries()) {
			BulletVolley2D *volley = entry.volley;
			if (ObjectDB::get_instance(ObjectID(entry.id)) != volley || !volley->is_active) {
				continue;
			}
			volley->replay_paused_overlaps();
			if (watch.died()) {
				return;
			}
		}
	}

	update_process_state();
}

void BulletFactory2D::update_process_state() {
	// The sweep runs while bullets process (a pause stops it); _process
	// drives the interpolation pass (only while bullets process and
	// interpolation is on): idle otherwise instead of paying an empty call
	// every rendered frame.
	callbacks.apply(*this, is_factory_processing_bullets && use_physics_interpolation, is_factory_processing_bullets);
}

void BulletFactory2D::native_physics_process(double delta) {
	if (is_queued_for_deletion()) {
		return; // dying (a refused free queued it, or a script did): no more sweeps
	}
	// After the physics flush: shape writes deferred from it apply now, and
	// none of the sweep's own writes is ever deferred (core/physics_flush2d.hpp).
	PhysicsFlush2D::note_scene_physics_frame();
	flush_physics_sync();
	resolve_physics_interpolation();
	struct SweepScope {
		SweepScope() { ++PhysicsFlush2D::sweep_depth; }
		~SweepScope() { --PhysicsFlush2D::sweep_depth; }
	} sweep_scope;
	const uint64_t stats_t0 = Time::get_singleton()->get_ticks_usec();
	stats_tick_volleys = 0;
	stats_tick_bullets = 0;
	is_iterating_bullets = true;
	++sweep_counter;
	sweep_physics_frame = Engine::get_singleton()->get_physics_frames();
	graze_clock += delta;
	// The bullet world runs on its own clock (time_scale 1 is bit-identical
	// to the engine delta); target scans (graze_clock) stay in real time.
	const double bullet_delta = delta * time_scale;
	// User code in the sweep (attachment callbacks, timers) may free this
	// factory: the copy outlives it and every step after user code tests it.
	const std::shared_ptr<bool> life = alive_flag;
	const bool &alive = *life;
	// Every way out of the sweep (a step stopped by user code included)
	// leaves the iteration flag down while this factory lives.
	struct IterationScope {
		BulletFactory2D *factory;
		std::shared_ptr<bool> life;
		~IterationScope() {
			if (life != nullptr && *life) {
				factory->is_iterating_bullets = false;
			}
		}
	} iteration_scope{ this, life };
	if (!tick_volleys(delta, alive) || !sweep_may_continue(alive) || sweep_paused()) {
		return;
	}
	// One-shot sprite effects age on the same clock as bullets (pausing the
	// factory freezes both). Volley trails tick inside move_bullets instead.
	age_fx_effects(bullet_delta);

	// Timers run for every volley that holds some (parked ones included:
	// their timers may be wake-up timers; pooled ones released theirs).
	// Same id-resolved snapshot as the tick: callbacks run user code that
	// may spawn (new volleys wait for the next step), free (swap-remove) or
	// respawn, and each volley runs its timers at most once per step.
	timer_iteration_scratch.clear();
	for (BulletVolley2D *volley : all_volleys) {
		if (volley != nullptr && !volley->custom_timers.empty()) {
			VolleyIterationEntry entry;
			entry.volley = volley;
			entry.id = volley->get_cached_instance_id();
			timer_iteration_scratch.push_back(entry);
		}
	}
	const uint64_t timer_snapshot_epoch = volley_free_epoch;
	for (size_t i = 0; i < timer_iteration_scratch.size(); ++i) {
		const VolleyIterationEntry entry = timer_iteration_scratch[i];
		BulletVolley2D *volley = volley_free_epoch == timer_snapshot_epoch ? entry.volley : resolve_iteration_entry(entry.id, entry.volley);
		if (volley == nullptr || volley->sweep_timer_stamp == sweep_counter || volley->custom_timers.empty()) {
			continue;
		}
		volley->sweep_timer_stamp = sweep_counter;
		volley->run_custom_timers(capped_volley_delta(delta, volley_time_factor(*volley)));
		if (!sweep_may_continue(alive) || sweep_paused()) {
			return;
		}
	}
	// Lasers after the volleys (their signals are live: user code inside).
	if (!tick_lasers(delta, alive) || sweep_paused()) { // each laser scales it by its own time factor
		return;
	}
	refresh_laser_debug_layer();
	is_iterating_bullets = false;

	// Splits recorded this sweep (and before it) spawn now, at the poses
	// their parents died at; the children tick from the next sweep. User
	// code runs inside (spawn callbacks, homing and bullet_split signals).
	if (!flush_splits(alive) || !sweep_may_continue(alive) || sweep_paused()) {
		return;
	}
	// Emitter bullets fire their due shots through their templates (user
	// code: the templates' shot signals); the new volleys tick next sweep.
	if (!flush_emits(alive) || !sweep_may_continue(alive) || sweep_paused()) {
		return;
	}

	// Sound winners play after every volley, effect and timer of the sweep
	// ran (timer-spawned offers join this sweep), never inside the iteration.
	sound_mixer.flush(*this);

	stats_last_physics_tick_usec = Time::get_singleton()->get_ticks_usec() - stats_t0;
	if (stats_last_physics_tick_usec > stats_peak_physics_tick_usec) {
		stats_peak_physics_tick_usec = stats_last_physics_tick_usec;
	}
	stats_last_tick_volleys = stats_tick_volleys;
	stats_last_tick_bullets = stats_tick_bullets;
	++stats_physics_ticks;
}

void BulletFactory2D::native_process(double delta) {
	if (is_factory_processing_bullets && use_physics_interpolation) {
		// Same guard as _physics_process: reset/free_* during the render
		// sweep would mutate the vec under iteration. The interpolation pass
		// only reads, but its inputs (vec, sparse set) are shared with the
		// writers.
		const uint64_t stats_t0 = Time::get_singleton()->get_ticks_usec();
		is_iterating_bullets = true;
		interpolate_volleys();
		redraw_lasers();
		is_iterating_bullets = false;
		stats_last_render_usec = Time::get_singleton()->get_ticks_usec() - stats_t0;
	}
}

RID BulletFactory2D::get_physics_space() const {
	return physics_space;
}

void BulletFactory2D::set_physics_space(RID new_space_rid) {
	if (!new_space_rid.is_valid()) {
		UtilityFunctions::push_error("set_physics_space: the provided RID is invalid. Set a valid physics space before spawning (it applies to subsequently spawned bullets; already spawned areas stay in their space).");
		return;
	}
	physics_space = new_space_rid;
}

void BulletFactory2D::teleport_shift_all_bullets(const Vector2 &shift_amount) {
	if (!shift_amount.is_finite()) {
		UtilityFunctions::push_error("teleport_shift_all_bullets: shift_amount must be finite, nothing moved.");
		return;
	}
	int volley_amount = static_cast<int>(all_volleys.size());

	for (int i = 0; i < volley_amount; ++i) {
		BulletVolley2D *bullets = all_volleys[i];
		if (bullets != nullptr && !bullets->is_queued_for_deletion()) {
			bullets->teleport_shift_all_bullets(shift_amount);
		}
	}
}

} // namespace BlastBullets2D
