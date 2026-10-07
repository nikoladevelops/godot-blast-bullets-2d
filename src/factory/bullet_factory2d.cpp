// BulletFactory2D lifecycle and frame loop: tree entry (_ready / lazy init), the
// volley container and debugger, physics interpolation, processing on/off, the
// per-frame volley sweep (_physics_process -> tick_volleys) and teleporting.
// Spawning: bullet_factory2d_spawn.cpp. Freeing/resetting: bullet_factory2d_structural.cpp.

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

// The snapshot entry's volley if it still exists (a handler may have freed
// it since), else nullptr. One ObjectDB lookup, no dereference before it.
static _ALWAYS_INLINE_ BulletVolley2D *resolve_iteration_entry(uint64_t id, BulletVolley2D *volley) {
	return ObjectDB::get_instance(ObjectID(id)) == (Object *)volley ? volley : nullptr;
}

void BulletFactory2D::tick_volleys(double delta) {
	if (!Math::is_finite(delta) || delta < 0.0) {
		return;
	}
	snapshot_active_volleys();
	const uint64_t snapshot_epoch = volley_free_epoch;
	for (const VolleyIterationEntry &entry : iteration_scratch) {
		BulletVolley2D *volley = volley_free_epoch == snapshot_epoch ? entry.volley : resolve_iteration_entry(entry.id, entry.volley);
		// Freed, parked/pooled by an earlier handler, or already ticked (or
		// begun a new life) this sweep: nothing to do.
		if (volley == nullptr || !volley->is_active || volley->sweep_tick_stamp == sweep_counter) {
			continue;
		}
		volley->sweep_tick_stamp = sweep_counter;
		// Flag the volley for its whole tick: handlers fired from inside
		// (collision drain, attachment callbacks) may spawn, and the pool must
		// never hand out the volley whose drain is still running. Liveness is
		// re-checked between steps: a handler that free()s the volley
		// (against the contract) must not crash the sweep.
		++stats_tick_volleys;
		stats_tick_bullets += volley->active_bullets_counter;
		volley->is_being_ticked = true;
		const uint64_t epoch_before_tick = volley_free_epoch;
		volley->tick(delta);
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

void BulletFactory2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		factories_in_tree.push_back(this);
		return;
	}
	if (p_what == NOTIFICATION_EXIT_TREE) {
		factories_in_tree.erase(std::remove(factories_in_tree.begin(), factories_in_tree.end(), this), factories_in_tree.end());
		unregister_monitors();
		return;
	}
	if (p_what == NOTIFICATION_PREDELETE) {
		unregister_monitors();
		// Parent is notified before children are destroyed. From here on no child
		// pointer (debuggers, containers) may be touched by teardown paths.
		is_tearing_down = true;
		// Pooled attachments outlive this call as engine children; drop their pool
		// tracking now so their later PREDELETEs never touch this pool object again.
		bullet_attachments_pool.detach_all();
		// Effect shards are our children and die with the tree; drop the bake
		// records (with their raw node pointers) before any volley PREDELETE
		// below tries to unregister through them.
		fx_unregister_all_bakes();
	}
}

void BulletFactory2D::_ready() {
	// Ensure the code that is next will not be ran in the editor
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	// Guard against double-init via ensure_factory_initialized() (lazy init
	// path may have already built containers when a script _ready() ran first
	// without super and a spawn happened before the native _ready).
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

	volley_debugger->set_debugger_color(debugger_color_cached_before_ready);
	volley_debugger->set_is_debugger_enabled(is_debugger_enabled_cached_before_ready);
	volley_debugger->set_max_debug_providers(debugger_max_providers_cached_before_ready);
	volley_debugger->set_draw_inactive_shapes(debugger_draw_inactive_cached_before_ready);

	use_physics_interpolation = use_physics_interpolation_cached_before_ready;
	update_process_state();

	is_ready = true;
	register_monitors();

	// Interpolation mismatch warning: warn when the factory flag disagrees with the project setting.
	// Bullets look steppy on >60Hz displays when project interpolation is off
	// but the factory flag is on, and vice versa wastes previous-frame memory.
	if (use_physics_interpolation) {
		bool project_interp = ProjectSettings::get_singleton()->get_setting("physics/common/physics_interpolation", false);
		if (!project_interp) {
			UtilityFunctions::push_warning("BulletFactory2D: use_physics_interpolation is enabled on the factory but ProjectSettings physics/common/physics_interpolation is OFF. Enable it in Project Settings for smooth bullets on high-refresh displays. See debug_check_interpolation_status().");
		}
	}
}

bool BulletFactory2D::ensure_factory_initialized() {
	if (is_ready) {
		return true;
	}
	if (Engine::get_singleton()->is_editor_hint()) {
		return false;
	}
	if (!is_inside_tree()) {
		return false;
	}
	if (is_tearing_down) {
		return false;
	}
	// Containers already exist (native _ready ran): just mark ready. This
	// keeps the normal path allocation-free.
	if (volley_container != nullptr && bullet_attachments_container != nullptr && volley_debugger != nullptr) {
		is_ready = true;
		return true;
	}
	// Recovery path: a GDScript _ready() without super._ready() skipped native
	// init. Build what is missing so the game does not ship a dead factory.
	if (physics_space.is_valid() == false) {
		Ref<World2D> world = get_world_2d();
		if (world.is_null()) {
			return false;
		}
		physics_space = world->get_space();
	}
	if (all_volleys.capacity() == 0) {
		all_volleys.reserve(2048);
		volley_set.resize(2048);
	}
	if (volley_container == nullptr) {
		add_bullet_containers();
	}
	if (bullet_attachments_container == nullptr) {
		add_bullet_attachment_container();
	}
	fx_ensure_effects_container();
	if (volley_debugger == nullptr) {
		add_debuggers();
	}
	if (volley_debugger != nullptr) {
		volley_debugger->set_debugger_color(debugger_color_cached_before_ready);
		volley_debugger->set_is_debugger_enabled(is_debugger_enabled_cached_before_ready);
		volley_debugger->set_max_debug_providers(debugger_max_providers_cached_before_ready);
		volley_debugger->set_draw_inactive_shapes(debugger_draw_inactive_cached_before_ready);
	}
	use_physics_interpolation = use_physics_interpolation_cached_before_ready;
	update_process_state();
	is_ready = true;
	if (!ready_missing_super_warned) {
		ready_missing_super_warned = true;
		UtilityFunctions::push_warning("BulletFactory2D: factory was used before native _ready() ran (likely a GDScript _ready() without super._ready()). Containers were recovered automatically, but call super._ready() in your script to avoid this. See WARNING in README.");
	}
	return true;
}

bool BulletFactory2D::get_use_physics_interpolation() const {
	if (!is_ready) {
		return use_physics_interpolation_cached_before_ready;
	}

	return use_physics_interpolation;
}

void BulletFactory2D::set_use_physics_interpolation_runtime(bool new_use_physics_interpolation) {
	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("set_use_physics_interpolation_runtime: BulletFactory2D is not in the scene tree yet (or is being destroyed). Set the use_physics_interpolation property instead.");
		return;
	}
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to set physics interpolation. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	// No debuggers here: this op only flips interpolation state, it never
	// rebuilds vectors.
	FactoryOperationGuard op(this, false);

	use_physics_interpolation = new_use_physics_interpolation;

	// Turning it on mid-game needs previous-frame data to exist, so seed it here.
	// Without this the first interpolated frames would lerp from stale transforms.
	if (use_physics_interpolation) {
		int amount_multimesh_instances = static_cast<int>(all_volleys.size());
		for (int i = 0; i < amount_multimesh_instances; ++i) {
			BulletVolley2D *bullets_multi = all_volleys[i];
			if (bullets_multi != nullptr) {
				bullets_multi->update_all_previous_transforms_for_interpolation();
			}
		}
	}
}

void BulletFactory2D::set_use_physics_interpolation_editor(bool new_use_physics_interpolation) {
	use_physics_interpolation_cached_before_ready = new_use_physics_interpolation;
	if (is_ready && !is_factory_busy) {
		set_use_physics_interpolation_runtime(new_use_physics_interpolation);
	}
}

void BulletFactory2D::add_bullet_containers() {
	// Create BulletVolleysContainer Node and add it as a child to factory
	volley_container = memnew(Node);
	volley_container->set_name("BulletVolleysContainer");
	add_child(volley_container);
}

void BulletFactory2D::add_bullet_attachment_container() {
	// Create BulletAttachmentContainer Node and add it as a child to factory
	bullet_attachments_container = memnew(Node);
	bullet_attachments_container->set_name("BulletAttachmentsContainer");
	add_child(bullet_attachments_container);
}

void BulletFactory2D::add_debuggers() {
	// Configure BulletVolley2D debugger and add it as a child to factory
	volley_debugger = memnew(BulletVolleyDebugger2D);
	volley_debugger->configure(volley_container, "BulletVolleysDebugger", debugger_color_cached_before_ready);
	add_child(volley_debugger);
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

	// Overlaps that started during the pause were parked by the volleys;
	// queue them now so the next tick drains them (exactly once).
	if (resuming) {
		for (BulletVolley2D *volley : all_volleys) {
			if (volley != nullptr && volley->is_active) {
				volley->replay_paused_overlaps();
			}
		}
	}

	set_physics_process(is_processing_enabled);
	update_process_state();
}

void BulletFactory2D::update_process_state() {
	// _process drives the interpolation pass (only while bullets process
	// and interpolation is on): idle otherwise instead of paying an empty
	// virtual call every rendered frame.
	set_process(is_factory_processing_bullets && use_physics_interpolation);
}

void BulletFactory2D::_physics_process(double delta) {
	const uint64_t stats_t0 = Time::get_singleton()->get_ticks_usec();
	stats_tick_volleys = 0;
	stats_tick_bullets = 0;
	is_iterating_bullets = true;
	++sweep_counter;
	graze_clock += delta;
	tick_volleys(delta);
	// One-shot sprite effects age on the same clock as bullets (pausing the
	// factory freezes both). Volley trails tick inside move_bullets instead.
	age_fx_effects(delta);

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
		volley->run_custom_timers(delta);
	}
	is_iterating_bullets = false;

	stats_last_physics_tick_usec = Time::get_singleton()->get_ticks_usec() - stats_t0;
	if (stats_last_physics_tick_usec > stats_peak_physics_tick_usec) {
		stats_peak_physics_tick_usec = stats_last_physics_tick_usec;
	}
	stats_last_tick_volleys = stats_tick_volleys;
	stats_last_tick_bullets = stats_tick_bullets;
	++stats_physics_ticks;
}

void BulletFactory2D::_process(double delta) {
	if (is_factory_processing_bullets && use_physics_interpolation) {
		// Same guard as _physics_process: reset/free_* during the render
		// sweep would mutate the vec under iteration. The interpolation pass
		// only reads, but its inputs (vec, sparse set) are shared with the
		// writers.
		const uint64_t stats_t0 = Time::get_singleton()->get_ticks_usec();
		is_iterating_bullets = true;
		interpolate_volleys();
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
