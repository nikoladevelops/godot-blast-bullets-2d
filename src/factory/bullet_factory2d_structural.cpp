// Structural operations: reset, free_*/clear_* (refused inside physics frames),
// their *_deferred twins (queued to the next idle frame) and the bookkeeping that
// keeps all_volleys, volley_set and volley_pool in sync.

#include "core/emit_on2d.hpp"
#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// ---------------------------------------------------------------------------
// Volley bookkeeping: all_volleys (source of truth), volley_set (indexes of
// the active volleys) and volley_pool (disabled volleys) stay in sync here.
// ---------------------------------------------------------------------------

// Splits out every volley matching a predicate, leaving survivors in `volleys`.
// Returned matches are still alive: the caller unlinks/frees them, then
// calls reindex_volleys().
template <typename TPred>
static std::vector<BulletVolley2D *> extract_matching_volleys(std::vector<BulletVolley2D *> &volleys, TPred matches) {
	std::vector<BulletVolley2D *> removed;
	auto new_end = std::remove_if(volleys.begin(), volleys.end(), [&removed, &matches](BulletVolley2D *volley) {
		if (volley == nullptr || !matches(volley)) {
			return false;
		}
		removed.push_back(volley);
		return true;
	});
	volleys.erase(new_end, volleys.end());
	return removed;
}

// (pointer, id) for volleys that are alive NOW: deleting one runs its
// attachment callbacks, user code that may free any other one, or this
// factory. Each is re-validated through ObjectDB before it is deleted.
static std::vector<BulletFactory2D::VolleyIterationEntry> entries_of(const std::vector<BulletVolley2D *> &volleys) {
	std::vector<BulletFactory2D::VolleyIterationEntry> entries;
	entries.reserve(volleys.size());
	for (BulletVolley2D *volley : volleys) {
		BulletFactory2D::VolleyIterationEntry entry;
		entry.volley = volley;
		entry.id = volley->get_cached_instance_id();
		entries.push_back(entry);
	}
	return entries;
}

static bool entry_alive(const BulletFactory2D::VolleyIterationEntry &entry) {
	return ObjectDB::get_instance(ObjectID(entry.id)) == entry.volley;
}

void BulletFactory2D::unlink_and_delete_volley(BulletVolley2D *volley) {
	volley_pool.try_remove_instance(volley, volley->get_pool_key());
	volley->force_delete();
}

void BulletFactory2D::reindex_volleys() {
	volley_set.clear();
	for (int i = 0; i < (int)all_volleys.size(); ++i) {
		if (all_volleys[i] == nullptr) {
			continue;
		}
		all_volleys[i]->sparse_set_id = i;
		if (all_volleys[i]->is_active) {
			volley_set.activate_data(i);
		}
	}
}

void BulletFactory2D::free_pooled_volleys(const PoolKey *key) {
	// Disabled volleys only, never a PARKED one (auto pooling off: frozen,
	// still the caller's, documented as wakeable; free_active_bullets frees
	// it).
	const std::vector<VolleyIterationEntry> removed = entries_of(extract_matching_volleys(all_volleys, [key](BulletVolley2D *volley) {
		return !volley->is_active && !volley->is_parked() && (key == nullptr || volley->get_pool_key() == *key);
	}));
	reindex_volleys();
	const LifeWatch watch = watch_life();
	for (const VolleyIterationEntry &entry : removed) {
		if (!entry_alive(entry)) {
			continue; // freed by a callback of an earlier delete
		}
		unlink_and_delete_volley(entry.volley);
		if (watch.died()) {
			return;
		}
	}
	// Free leftovers the pool holds but all_volleys never tracked.
	if (key != nullptr) {
		volley_pool.free_specific_bullets(*key);
	} else {
		volley_pool.free_all_bullets();
	}
}

void BulletFactory2D::free_all_volleys(const PoolKey *key) {
	const LifeWatch watch = watch_life();
	if (key == nullptr) {
		// Untrack EVERY volley first, then delete: each deletion runs the
		// attachment callbacks of its bullets (user code), which may read the
		// counters that walk all_volleys or the pool; a volley deleted earlier
		// must no longer be in either.
		const std::vector<VolleyIterationEntry> doomed = snapshot_volley_entries();
		volley_pool.clear();
		all_volleys.clear();
		volley_set.clear();
		for (const VolleyIterationEntry &entry : doomed) {
			if (!entry_alive(entry)) {
				continue; // freed by a callback of an earlier delete
			}
			entry.volley->force_delete();
			if (watch.died()) {
				return;
			}
		}
		return;
	}
	// Unlink matches from the pool first when present (disabled + auto
	// pooling on), then free exactly once. Active volleys are never pooled.
	const std::vector<VolleyIterationEntry> removed = entries_of(extract_matching_volleys(all_volleys, [key](BulletVolley2D *volley) {
		return volley->get_pool_key() == *key;
	}));
	for (const VolleyIterationEntry &entry : removed) {
		if (!entry_alive(entry)) {
			continue;
		}
		unlink_and_delete_volley(entry.volley);
		if (watch.died()) {
			return;
		}
	}
	reindex_volleys();
	// Frees pooled leftovers that were never tracked in all_volleys.
	volley_pool.free_specific_bullets(*key);
}

void BulletFactory2D::free_active_volleys(const PoolKey *key) {
	const std::vector<VolleyIterationEntry> removed = entries_of(extract_matching_volleys(all_volleys, [key](BulletVolley2D *volley) {
		return volley->is_active && (key == nullptr || volley->get_pool_key() == *key);
	}));
	const LifeWatch watch = watch_life();
	for (const VolleyIterationEntry &entry : removed) {
		if (!entry_alive(entry)) {
			continue; // freed by a callback of an earlier delete
		}
		entry.volley->force_delete();
		if (watch.died()) {
			return;
		}
	}
	reindex_volleys();
}

void BulletFactory2D::free_disabled_volleys(const PoolKey *key) {
	// Same rule as free_pooled_volleys: a parked volley is spared.
	const std::vector<VolleyIterationEntry> removed = entries_of(extract_matching_volleys(all_volleys, [key](BulletVolley2D *volley) {
		return !volley->is_active && !volley->is_parked() && (key == nullptr || volley->get_pool_key() == *key);
	}));
	const LifeWatch watch = watch_life();
	for (const VolleyIterationEntry &entry : removed) {
		if (!entry_alive(entry)) {
			continue; // freed by a callback of an earlier delete
		}
		if (key == nullptr) {
			entry.volley->force_delete(); // the pool only needs its pointers dropped (below)
		} else {
			unlink_and_delete_volley(entry.volley);
		}
		if (watch.died()) {
			return;
		}
	}
	if (key == nullptr) {
		volley_pool.clear();
	} else {
		volley_pool.free_specific_bullets(*key);
	}
	reindex_volleys();
}

void BulletFactory2D::remove_volley_from_tracking(BulletVolley2D *target) {
	++volley_free_epoch;
	int id_to_remove = target->sparse_set_id;
	const int last_idx = static_cast<int>(all_volleys.size()) - 1;
	if (id_to_remove < 0 || id_to_remove > last_idx) {
		return;
	}
	// Identity check: a stale id must never delete an innocent element.
	// Fall back to a linear search so the right volley is still removed.
	if (all_volleys[id_to_remove] != target) {
		id_to_remove = -1;
		for (int i = 0; i <= last_idx; ++i) {
			if (all_volleys[i] == target) {
				id_to_remove = i;
				break;
			}
		}
		if (id_to_remove < 0) {
			return;
		}
		target->sparse_set_id = id_to_remove;
	}

	BulletVolley2D *last_volley = all_volleys[last_idx];
	const bool last_was_active = last_volley != nullptr && last_volley->is_active;

	// Remove both ids from the active set (target first).
	volley_set.disable_data(id_to_remove);
	if (last_idx != id_to_remove) {
		volley_set.disable_data(last_idx);
	}
	// Move the last volley into the hole, unless the target IS the last.
	if (id_to_remove < last_idx) {
		all_volleys[id_to_remove] = last_volley;
		if (last_volley != nullptr) {
			last_volley->sparse_set_id = id_to_remove;
		}
	}
	all_volleys.pop_back();
	if (last_was_active && id_to_remove < last_idx) {
		volley_set.activate_data(id_to_remove);
	}
}

void BulletFactory2D::reset_factory_state(const PoolKey *key) {
	// Pure state function: the caller (reset(), under FactoryOperationGuard)
	// owns busy/processing/debugger state. Debuggers stay powered while the
	// vectors are rebuilt; the guard restores them afterwards.

	// Free all BulletVolley2D, their attachments and the object pool
	const LifeWatch watch = watch_life();
	free_all_volleys(key);
	if (watch.died()) {
		return; // an attachment callback freed this factory
	}

	// Freed volleys unregister their bakes through PREDELETE; only a full
	// reset wipes the rest (manual hatch included). A scoped reset must
	// preserve living volleys' bakes, or their triggers would go silent.
	if (key == nullptr) {
		fx_unregister_all_bakes();
		// Full reset: also drop whitened frames so edited art re-bakes.
		clear_whitened_frame_cache();
	}

	// Attachments of freed multis are already handled per-multi via force_delete ->
	// bullet_disable_attachment (pushed to the pool or queue_freed per auto-pool flag).
	// Only a full (null-key) reset wipes the global attachment pool; a scoped reset must
	// preserve unrelated pooled attachments.
	if (key == nullptr) {
		bullet_attachments_pool.free_all_bullet_attachments();
	}
}

void BulletFactory2D::reset(const Ref<VolleyPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to call reset(). BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	if (reject_when_iterating("reset")) {
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("reset: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return;
	}

	const LifeWatch watch = watch_life();
	{
		FactoryOperationGuard op(this);
		PoolKey resolved;
		const PoolKey *key_ptr = resolve_pool_key(key, resolved);
		reset_factory_state(key_ptr);
		if (!watch.died()) {
			drop_pending_splits_for(key_ptr); // reset bullets never split
		}
	}
	if (watch.died()) {
		return; // an attachment callback freed this factory
	}
	clear_graze_cache();
	sound_mixer.clear(); // full or scoped: voices stop, channels forgotten
	// Finished means finished: the guard has restored processing and the
	// debuggers, so a handler can spawn the next wave right away (emitting
	// inside the guard made every spawn from the handler fail as busy).
	emit_on2d(this, "reset_finished");
}

void BulletFactory2D::free_active_bullets(const Ref<VolleyPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to free active bullets. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	if (reject_when_iterating("free_active_bullets")) {
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_active_bullets: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return;
	}

	FactoryOperationGuard op(this);

	// Free all ACTIVE BulletVolley2D
	PoolKey resolved;
	const PoolKey *key_ptr = resolve_pool_key(key, resolved);
	// Before the free: its attachment callbacks may free this factory (a
	// free never records splits, so nothing new can arrive meanwhile).
	drop_pending_splits_for(key_ptr); // freed bullets never split
	free_active_volleys(key_ptr);
}

int BulletFactory2D::clear_active_bullets(const Ref<VolleyPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to clear active bullets. BulletFactory2D is currently busy. Ignoring the request");
		return 0;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("clear_active_bullets: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return 0;
	}

	// NOT structural: clearing only disables bullets (their volleys park in
	// the pool, no RID is freed), so it is allowed from hit/lifetime
	// handlers and timer callbacks, i.e. inside the physics frame ("the
	// player got hit: clear the screen"). A cleared volley that is being
	// ticked right now just skips its remaining records (epoch moved) and
	// the pool never hands it out before its tick ends. The busy latch makes
	// spawns and structural calls from on_bullet_disable callbacks reject
	// until the sweep is done (same as the disable sweep itself).
	const bool saved_busy = is_factory_busy;
	is_factory_busy = true;

	// Snapshot first: each clear mutates live sets below (the last cleared
	// bullet funnels its volley into the pool). The callbacks may free any
	// volley or this factory (life).
	const std::vector<VolleyIterationEntry> snapshot = snapshot_volley_entries();
	const std::shared_ptr<bool> life = alive_flag;

	PoolKey resolved;
	const PoolKey *key_ptr = resolve_pool_key(key, resolved);

	int cleared = 0;
	// bullets_cleared payloads, captured only while somebody listens.
	struct Announcement {
		uint64_t volley_id = 0;
		BulletVolley2D *volley = nullptr;
		int life_id = 0; // a volley reused by an earlier handler is a different life
		PackedVector2Array positions;
		Array indexes;
	};
	std::vector<Announcement> announcements;
	const bool announce = has_connections("bullets_cleared");
	for (const VolleyIterationEntry &entry : snapshot) {
		BulletVolley2D *volley = entry.volley;
		// An earlier clear ran user callbacks (on_bullet_disable) that may
		// have freed a volley later in this snapshot: validate via ObjectDB
		// with the id taken before (no dereference of the raw pointer).
		const uint64_t volley_id = entry.id;
		if (ObjectDB::get_instance(ObjectID(volley_id)) != volley) {
			continue;
		}
		if (!volley->is_active) {
			continue;
		}
		if (key_ptr != nullptr && !(volley->get_pool_key() == *key_ptr)) {
			continue;
		}
		Announcement note;
		if (announce) {
			// Poses before the clear: the last cleared bullet releases the life.
			const std::vector<int> live = volley->all_bullets_enabled_set.get_active_indexes();
			note.positions.resize((int64_t)live.size());
			note.indexes.resize((int64_t)live.size());
			for (size_t k = 0; k < live.size(); ++k) {
				note.positions[(int64_t)k] = volley->all_cached_instance_transforms[live[k]].columns[2];
				note.indexes[(int64_t)k] = live[k];
			}
		}
		const int done = volley->clear_all_bullets();
		cleared += done;
		if (!*life) {
			return cleared; // a callback freed this factory
		}
		if (announce && done > 0 && ObjectDB::get_instance(ObjectID(volley_id)) == volley) {
			note.volley_id = volley_id;
			note.volley = volley;
			note.life_id = volley->get_life_id();
			announcements.push_back(std::move(note));
		}
	}
	is_factory_busy = saved_busy;
	// The latch is down: handlers may spawn (drop items, replace bullets).
	for (const Announcement &note : announcements) {
		if (is_tearing_down) {
			break;
		}
		if (ObjectDB::get_instance(ObjectID(note.volley_id)) != note.volley || note.volley->get_life_id() != note.life_id) {
			continue;
		}
		note_user_code();
		emit_on2d(this, "bullets_cleared", note.positions, note.volley, note.indexes);
		if (!*life) {
			return cleared;
		}
	}
	return cleared;
}

int BulletFactory2D::clear_bullets_owned_by(uint64_t owner_spawner_id, bool fire_clear_effects, int split_cause) {
	if (owner_spawner_id == 0 || !is_ready || is_tearing_down) {
		return 0;
	}
	if (is_factory_busy) {
		UtilityFunctions::push_error("clear_active_bullets: BulletFactory2D is busy (a sweep is running). Clear after it, e.g. from the next frame.");
		return 0;
	}
	// Same rules as clear_active_bullets(): not structural, safe from
	// handlers, busy latch held across the user callbacks of the sweep.
	const bool saved_busy = is_factory_busy;
	is_factory_busy = true;
	const std::vector<VolleyIterationEntry> snapshot = snapshot_volley_entries();
	const std::shared_ptr<bool> life = alive_flag;
	int cleared = 0;
	for (const VolleyIterationEntry &entry : snapshot) {
		BulletVolley2D *volley = entry.volley;
		if (ObjectDB::get_instance(ObjectID(entry.id)) != volley || volley->owner_spawner_id != owner_spawner_id || volley->is_queued_for_deletion()) {
			continue;
		}
		if (volley->is_active) {
			const std::vector<int> live = volley->all_bullets_enabled_set.get_active_indexes();
			cleared += volley->disable_bullets_bulk(live, fire_clear_effects ? (int)EFFECT_ON_CLEAR : -1, nullptr, split_cause);
			if (!*life) {
				return cleared; // a callback freed this factory
			}
		}
	}
	is_factory_busy = saved_busy;
	// Parked volleys of this owner (drained with pooling off) go back to the
	// pool: the owner asked for its bullets to be gone.
	for (const VolleyIterationEntry &entry : snapshot) {
		BulletVolley2D *volley = entry.volley;
		if (ObjectDB::get_instance(ObjectID(entry.id)) != volley || volley->owner_spawner_id != owner_spawner_id) {
			continue;
		}
		if (volley->is_parked()) {
			volley->set_is_auto_pooling_enabled(true);
		}
	}
	return cleared;
}

void BulletFactory2D::apply_orphan_policy(uint64_t owner_spawner_id, int policy, const String &spawner_path) {
	if (owner_spawner_id == 0 || !is_ready || is_tearing_down) {
		return;
	}
	// Policy ids mirror BulletSpawner2D::OrphanedVolleys. The queued splits
	// follow first (dropping or re-owning a request runs no user code).
	orphan_pending_splits(owner_spawner_id, policy);
	if (policy == 2 || policy == 3) {
		if (!is_factory_busy) {
			// The cleanup never splits: the spawner asked for its bullets to go.
			clear_bullets_owned_by(owner_spawner_id, policy == 2, SPLIT_CAUSE_NONE);
			return;
		}
		// Busy (freed from inside a sweep callback): fall back to keeping
		// them flying, so the orphan warning still explains what happened.
		policy = 0;
	}
	for (BulletVolley2D *volley : all_volleys) {
		if (volley == nullptr || volley->owner_spawner_id != owner_spawner_id) {
			continue;
		}
		if (policy == 1) {
			volley->owner_spawner_id = 0; // factory-owned from now on
		} else {
			volley->orphaned_spawner_path = spawner_path;
		}
	}
}

void BulletFactory2D::free_disabled_bullets(const Ref<VolleyPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring free_disabled_bullets request.");
		return;
	}

	if (reject_when_iterating("free_disabled_bullets")) {
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_disabled_bullets: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return;
	}

	FactoryOperationGuard op(this);

	PoolKey resolved;
	const PoolKey *key_ptr = resolve_pool_key(key, resolved);

	free_disabled_volleys(key_ptr);
}

void BulletFactory2D::handle_manual_volley_deletion(BulletVolley2D &bullet_multi) {
	++volley_free_epoch; // running sweeps re-resolve their snapshot by id
	// During factory teardown the whole subtree dies with it; vectors die too, so
	// there is nothing to fix up and child pointers must not be touched.
	if (is_tearing_down) {
		return;
	}
	// NOTE 1: deliberately NOT rejected while is_iterating_bullets. This runs from the
	// dying multimesh's PREDELETE - the node is already gone, so the vec fixup below
	// MUST run now or the factory keeps a dangling pointer that crashes the next
	// tick. The swap-remove is safe mid-iteration: tick_volleys copies the
	// dense list up front and re-checks bounds/identity per element.
	// NOTE 2: also NOT rejected while is_factory_busy. During reset()/free_* loops
	// every deletion is marked_for_internal_deletion so this callback never fires from
	// them; the only busy-region entry is a user freeing a multimesh from a script
	// callback nested in an internal busy region (e.g. a disable sweep). Rejecting
	// there would strand a dangling pointer in the vec, so the fixup must always run.
	// The guard preserves a possibly-nested busy flag and restores processing
	// afterwards. Debugger restore is always deferred here (it will cause a
	// crash if re-enabled immediately from a PREDELETE notification).
	FactoryOperationGuard op(this, true, true);

	volley_pool.try_remove_instance(&bullet_multi, bullet_multi.get_pool_key());
	remove_volley_from_tracking(&bullet_multi);
}

void BulletFactory2D::track_volley_active(BulletVolley2D &bullet_multi) {
	if (is_tearing_down) {
		return;
	}
	if (is_factory_busy) {
		UtilityFunctions::push_error("track_volley_active: BulletFactory2D is busy, so the multimesh was left out of the active set. It will not move until the factory processes it again.");
		return;
	}
	// Identity-check the id first: activating a stale id would drive the wrong volley.
	const int id = bullet_multi.sparse_set_id;
	if (id >= 0 && id < (int)all_volleys.size() && all_volleys[id] == &bullet_multi) {
		volley_set.activate_data(id);
	}
}

void BulletFactory2D::track_volley_inactive(BulletVolley2D &bullet_multi) {
	const int id = bullet_multi.sparse_set_id;
	if (id >= 0 && id < (int)all_volleys.size() && all_volleys[id] == &bullet_multi) {
		volley_set.disable_data(id);
	}
}

void BulletFactory2D::free_bullets_pool(const Ref<VolleyPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring free_bullets_pool request.");
		return;
	}

	if (reject_when_iterating("free_bullets_pool")) {
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_bullets_pool: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return;
	}

	FactoryOperationGuard op(this);

	PoolKey resolved;
	free_pooled_volleys(resolve_pool_key(key, resolved));
	// Debuggers rebuild from the new indices when the guard restores them.
}

void BulletFactory2D::free_attachments_pool() {
	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_attachments_pool: BulletFactory2D is not in the scene tree yet (or is being destroyed). Add it first, then manage attachment pools.");
		return;
	}

	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring free_attachments_pool request.");
		return;
	}

	if (reject_when_iterating("free_attachments_pool")) {
		return;
	}

	FactoryOperationGuard op(this);

	bullet_attachments_pool.free_all_bullet_attachments();
}

void BulletFactory2D::free_attachments_pool_for_scene(const Ref<PackedScene> &attachment_scene) {
	if (attachment_scene.is_null()) {
		UtilityFunctions::push_error("free_attachments_pool_for_scene: attachment_scene is null, nothing to free.");
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_attachments_pool_for_scene: BulletFactory2D is not in the scene tree yet (or is being destroyed). Add it first, then manage attachment pools.");
		return;
	}

	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring free_attachments_pool_for_scene request.");
		return;
	}

	if (reject_when_iterating("free_attachments_pool_for_scene")) {
		return;
	}

	FactoryOperationGuard op(this);

	// Freed via the non-recording key (make_pooling_key_for_scene): recording
	// a label (note_scene) would permanently mark even an invalid scene as
	// recognized and change later error branches.
	bullet_attachments_pool.free_specific_bullet_attachments(BulletAttachmentObjectPool2D::make_pooling_key_for_scene(attachment_scene));
}

// ---- Deferred structural wrappers ----
// Structural ops must run on an IDLE frame: Godot flushes call_deferred()
// queued during the physics step while still inside that physics frame, so a
// plain call_deferred from a collision handler hit reject_when_iterating() and
// was silently refused. These wrappers queue onto the next SceneTree
// process_frame instead (idle, after the physics step), in call order.

void BulletFactory2D::queue_structural_call(std::function<void()> call) {
	pending_structural_calls.push_back(std::move(call));
	if (structural_flush_connected) {
		return;
	}
	SceneTree *tree = is_inside_tree() ? get_tree() : nullptr;
	if (tree != nullptr) {
		tree->connect("process_frame", callable_mp(this, &BulletFactory2D::_flush_structural_calls), CONNECT_ONE_SHOT);
	} else {
		call_deferred("_flush_structural_calls");
	}
	structural_flush_connected = true;
}

void BulletFactory2D::_flush_structural_calls() {
	structural_flush_connected = false;
	std::vector<std::function<void()>> calls;
	calls.swap(pending_structural_calls);
	// Each call runs user code (attachment callbacks inside a reset or a
	// free, reset_finished): it may free this factory, so nothing of `this`
	// is read after a call before the liveness check.
	const LifeWatch watch = watch_life();
	for (const std::function<void()> &call : calls) {
		if (watch.died() || is_tearing_down) {
			return;
		}
		call();
	}
}

// Each validates cheaply now (teardown/null only) and defers the real op so
// physics-frame / sweep callers never hit reject_when_iterating(). The
// deferred call re-runs full validation at flush time.

void BulletFactory2D::reset_deferred(const Ref<VolleyPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("reset_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call([this, key]() { reset(key); });
}

void BulletFactory2D::free_active_bullets_deferred(const Ref<VolleyPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_active_bullets_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call([this, key]() { free_active_bullets(key); });
}

void BulletFactory2D::clear_active_bullets_deferred(const Ref<VolleyPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("clear_active_bullets_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call([this, key]() { clear_active_bullets(key); });
}

void BulletFactory2D::free_disabled_bullets_deferred(const Ref<VolleyPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_disabled_bullets_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call([this, key]() { free_disabled_bullets(key); });
}

void BulletFactory2D::free_bullets_pool_deferred(const Ref<VolleyPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_bullets_pool_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call([this, key]() { free_bullets_pool(key); });
}

void BulletFactory2D::populate_bullets_pool_deferred(const Ref<VolleyPoolKey2D> &key, const Ref<BulletVolleyData2D> &spawn_data, int instance_count) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("populate_bullets_pool_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	if (key.is_null()) {
		UtilityFunctions::push_error("populate_bullets_pool_deferred requires an explicit VolleyPoolKey2D. Nothing was queued.");
		return;
	}
	if (spawn_data.is_null()) {
		UtilityFunctions::push_error("populate_bullets_pool_deferred: spawn_data is null. Nothing was queued.");
		return;
	}
	if (instance_count <= 0) {
		UtilityFunctions::push_error("populate_bullets_pool_deferred: instance_count must be > 0. Nothing was queued.");
		return;
	}
	queue_structural_call([this, key, spawn_data, instance_count]() { populate_bullets_pool(key, spawn_data, instance_count); });
}

void BulletFactory2D::free_attachments_pool_deferred() {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_attachments_pool_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call([this]() { free_attachments_pool(); });
}

void BulletFactory2D::populate_attachments_pool_deferred(const Ref<PackedScene> &attachment_scene, int amount_instances) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("populate_attachments_pool_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	if (attachment_scene.is_null() || amount_instances <= 0) {
		UtilityFunctions::push_error("populate_attachments_pool_deferred: needs a scene and amount_attachments > 0. Nothing was queued.");
		return;
	}
	queue_structural_call([this, attachment_scene, amount_instances]() { populate_attachments_pool(attachment_scene, amount_instances); });
}

void BulletFactory2D::free_attachments_pool_for_scene_deferred(const Ref<PackedScene> &attachment_scene) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_attachments_pool_for_scene_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	if (attachment_scene.is_null()) {
		UtilityFunctions::push_error("free_attachments_pool_for_scene_deferred: attachment_scene is null. Nothing was queued.");
		return;
	}
	queue_structural_call([this, attachment_scene]() { free_attachments_pool_for_scene(attachment_scene); });
}

void BulletFactory2D::free_volley_deferred(Node *volley) {
	if (volley == nullptr) {
		UtilityFunctions::push_error("free_volley_deferred: volley is null. Nothing was queued.");
		return;
	}
	// Volleys only: this is a bullet API, and queue_freeing an arbitrary
	// node passed by mistake (a target, the factory itself) is never right.
	if (Object::cast_to<BulletVolley2D>(volley) == nullptr) {
		UtilityFunctions::push_error("free_volley_deferred: node is not a bullet volley (BulletVolley2D). Nothing was queued.");
		return;
	}
	if (!volley->is_inside_tree()) {
		// Already outside the tree: queue_free is still safe, just do it now.
		volley->queue_free();
		return;
	}
	// queue_free() is always safe mid-sweep (deletes at frame end), unlike
	// free()/force_delete(). Never call free() on a volley in a handler.
	volley->queue_free();
}

} // namespace BlastBullets2D
