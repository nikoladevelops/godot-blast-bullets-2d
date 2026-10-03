// Structural operations: reset, free_*/clear_* (refused inside physics frames),
// their *_deferred twins (queued to the next idle frame) and the bookkeeping that
// keeps all_volleys, volley_set and volley_pool in sync.

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
	// Disabled volleys only. Correct whether auto pooling is on (pooled),
	// off (never pooled) or was toggled mid-game (mixed).
	std::vector<BulletVolley2D *> removed = extract_matching_volleys(all_volleys, [key](BulletVolley2D *volley) {
		return !volley->is_active && (key == nullptr || volley->get_pool_key() == *key);
	});
	reindex_volleys();
	for (BulletVolley2D *volley : removed) {
		unlink_and_delete_volley(volley);
	}
	// Free leftovers the pool holds but all_volleys never tracked.
	if (key != nullptr) {
		volley_pool.free_specific_bullets(*key);
	} else {
		volley_pool.free_all_bullets();
	}
}

void BulletFactory2D::free_all_volleys(const PoolKey *key) {
	if (key == nullptr) {
		for (BulletVolley2D *volley : all_volleys) {
			if (volley != nullptr) {
				volley->force_delete();
			}
		}
		// Every tracked volley (pooled ones included) is deleted above, so
		// only drop the pool's dangling pointers. free_all_bullets() here
		// would delete the same objects twice.
		volley_pool.clear();
		all_volleys.clear();
		volley_set.clear();
		return;
	}
	// Unlink matches from the pool first when present (disabled + auto
	// pooling on), then free exactly once. Active volleys are never pooled.
	std::vector<BulletVolley2D *> removed = extract_matching_volleys(all_volleys, [key](BulletVolley2D *volley) {
		return volley->get_pool_key() == *key;
	});
	for (BulletVolley2D *volley : removed) {
		unlink_and_delete_volley(volley);
	}
	reindex_volleys();
	// Frees pooled leftovers that were never tracked in all_volleys.
	volley_pool.free_specific_bullets(*key);
}

void BulletFactory2D::free_active_volleys(const PoolKey *key) {
	std::vector<BulletVolley2D *> removed = extract_matching_volleys(all_volleys, [key](BulletVolley2D *volley) {
		return volley->is_active && (key == nullptr || volley->get_pool_key() == *key);
	});
	for (BulletVolley2D *volley : removed) {
		volley->force_delete();
	}
	reindex_volleys();
}

void BulletFactory2D::free_disabled_volleys(const PoolKey *key) {
	std::vector<BulletVolley2D *> removed = extract_matching_volleys(all_volleys, [key](BulletVolley2D *volley) {
		return !volley->is_active && (key == nullptr || volley->get_pool_key() == *key);
	});
	if (key == nullptr) {
		// force_delete memdeletes every disabled volley, so the pool only
		// needs its pointers dropped.
		for (BulletVolley2D *volley : removed) {
			volley->force_delete();
		}
		volley_pool.clear();
	} else {
		for (BulletVolley2D *volley : removed) {
			unlink_and_delete_volley(volley);
		}
		volley_pool.free_specific_bullets(*key);
	}
	reindex_volleys();
}

void BulletFactory2D::remove_volley_from_tracking(BulletVolley2D *target) {
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
	free_all_volleys(key);

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

	FactoryOperationGuard op(this);

	PoolKey resolved;
	reset_factory_state(resolve_pool_key(key, resolved));

	// Notify the user that all bullets have been freed/deleted (before the
	// guard restores processing state).
	emit_signal("reset_finished");
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
	free_active_volleys(key_ptr);
}

int BulletFactory2D::clear_active_bullets(const Ref<VolleyPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to clear active bullets. BulletFactory2D is currently busy. Ignoring the request");
		return 0;
	}

	if (reject_when_iterating("clear_active_bullets")) {
		return 0;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("clear_active_bullets: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return 0;
	}

	FactoryOperationGuard op(this);

	// Snapshot first: each clear mutates live sets below (the last cleared
	// bullet funnels its volley into the pool).
	std::vector<BulletVolley2D *> snapshot = all_volleys;

	PoolKey resolved;
	const PoolKey *key_ptr = resolve_pool_key(key, resolved);

	int cleared = 0;
	for (BulletVolley2D *volley : snapshot) {
		if (volley == nullptr) {
			continue;
		}
		// An earlier clear ran user callbacks (on_bullet_disable) that may
		// have freed a volley later in this snapshot: validate via ObjectDB
		// (no dereference) instead of trusting the raw pointer.
		const uint64_t volley_id = volley->get_instance_id();
		if (ObjectDB::get_instance(ObjectID(volley_id)) != volley) {
			continue;
		}
		if (!volley->is_active) {
			continue;
		}
		if (key_ptr != nullptr && !(volley->get_pool_key() == *key_ptr)) {
			continue;
		}
		cleared += volley->clear_all_bullets();
	}
	return cleared;
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
	// a label (note_key_label) would permanently mark even an invalid scene as
	// recognized and change later error branches.
	bullet_attachments_pool.free_specific_bullet_attachments(BulletAttachmentObjectPool2D::make_pooling_key_for_scene(attachment_scene));
}

// ---- Deferred structural wrappers ----
// Structural ops must run on an IDLE frame: Godot flushes call_deferred()
// queued during the physics step while still inside that physics frame, so a
// plain call_deferred from a collision handler hit reject_when_iterating() and
// was silently refused. These wrappers queue onto the next SceneTree
// process_frame instead (idle, after the physics step), in call order.

void BulletFactory2D::queue_structural_call(const Callable &call) {
	pending_structural_calls.push_back(call);
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
	std::vector<Callable> calls;
	calls.swap(pending_structural_calls);
	for (const Callable &call : calls) {
		if (is_tearing_down) {
			return;
		}
		if (call.is_valid()) {
			call.call();
		}
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
	queue_structural_call(Callable(this, "reset").bind(key));
}

void BulletFactory2D::free_active_bullets_deferred(const Ref<VolleyPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_active_bullets_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call(Callable(this, "free_active_bullets").bind(key));
}

void BulletFactory2D::clear_active_bullets_deferred(const Ref<VolleyPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("clear_active_bullets_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call(Callable(this, "clear_active_bullets").bind(key));
}

void BulletFactory2D::free_disabled_bullets_deferred(const Ref<VolleyPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_disabled_bullets_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call(Callable(this, "free_disabled_bullets").bind(key));
}

void BulletFactory2D::free_bullets_pool_deferred(const Ref<VolleyPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_bullets_pool_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call(Callable(this, "free_bullets_pool").bind(key));
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
	queue_structural_call(Callable(this, "populate_bullets_pool").bind(key, spawn_data, instance_count));
}

void BulletFactory2D::free_attachments_pool_deferred() {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_attachments_pool_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call(Callable(this, "free_attachments_pool"));
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
	queue_structural_call(Callable(this, "free_attachments_pool_for_scene").bind(attachment_scene));
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
