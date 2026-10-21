// Emitter bullets on the factory side: the queue volleys record their due
// shots into (emit_enqueue) and the flush that fires them through their
// template spawners once per sweep (flush_emits, from _physics_process right
// after the splits). Same shape as the split flush: a swapped-out batch (shots
// recorded meanwhile wait for the next sweep), a drop epoch a reset from a
// handler bumps, an optional per-sweep cap that carries the rest over in
// order. Every shot runs user code (the template's signals and callbacks):
// this factory and the template are re-checked after each one.

#include "bullet_spawner/pattern_spawner2d.hpp"
#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletFactory2D::emit_enqueue(EmitRequest2D &&request) {
	if (is_tearing_down) {
		return;
	}
	pending_emits.push_back(std::move(request));
	++emit_recorded_total;
}

void BulletFactory2D::set_emitter_max_shots_per_sweep(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletFactory2D: emitter_max_shots_per_sweep must be >= 0 (0 = no cap), keeping the old value.");
		return;
	}
	emitter_max_shots_per_sweep = value;
}

void BulletFactory2D::set_emitter_max_pending(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletFactory2D: emitter_max_pending must be >= 0 (0 = no cap), keeping the old value.");
		return;
	}
	emitter_max_pending = value;
}

bool BulletFactory2D::emit_has_room() {
	if (emitter_max_pending <= 0 || pending_emits.size() < (size_t)emitter_max_pending) {
		emit_full_warned = false;
		return true;
	}
	++emit_dropped_total;
	if (!emit_full_warned) {
		emit_full_warned = true;
		UtilityFunctions::push_warning(vformat("BulletFactory2D: emitter_max_pending (%d) reached, new emitter shots are dropped until the queue drains (raise it, or 0 = no cap).", emitter_max_pending));
	}
	return false;
}

void BulletFactory2D::drop_pending_emits() {
	pending_emits.clear();
	++emit_drop_epoch; // a running flush stops firing its batch
}

void BulletFactory2D::drop_pending_emits_for(const PoolKey *key) {
	if (key == nullptr) {
		drop_pending_emits();
		return;
	}
	pending_emits.erase(std::remove_if(pending_emits.begin(), pending_emits.end(), [&](const EmitRequest2D &r) {
		return r.source_key == *key;
	}),
			pending_emits.end());
	if (emit_flush_running) {
		for (size_t i = emit_flush_next + 1; i < emit_flush_scratch.size(); ++i) {
			if (emit_flush_scratch[i].source_key == *key) {
				emit_flush_scratch[i].dropped = true;
			}
		}
	}
}

void BulletFactory2D::orphan_pending_emits(uint64_t owner_spawner_id, int policy) {
	// Clear / Remove: the spawner asked for its bullets to go, their queued
	// shots go with them. Keep Flying / Hand To Factory: they still fire (the
	// template, not the freed spawner, fires them).
	if (owner_spawner_id == 0 || (policy != 2 && policy != 3)) {
		return;
	}
	pending_emits.erase(std::remove_if(pending_emits.begin(), pending_emits.end(), [&](const EmitRequest2D &r) {
		return r.owner_id == owner_spawner_id;
	}),
			pending_emits.end());
	if (emit_flush_running) {
		for (size_t i = emit_flush_next + 1; i < emit_flush_scratch.size(); ++i) {
			if (emit_flush_scratch[i].owner_id == owner_spawner_id) {
				emit_flush_scratch[i].dropped = true;
			}
		}
	}
}

Dictionary BulletFactory2D::debug_get_emitter_stats() const {
	Dictionary out;
	out["pending"] = (int64_t)pending_emits.size();
	out["fired_last"] = emit_stats_fired_last;
	out["carried_over_last"] = emit_stats_carried_last;
	out["fired_total"] = (int64_t)emit_fired_total;
	out["recorded_total"] = (int64_t)emit_recorded_total;
	out["dropped_total"] = (int64_t)emit_dropped_total;
	return out;
}

bool BulletFactory2D::flush_emits(const bool &alive) {
	emit_stats_fired_last = 0;
	emit_stats_carried_last = 0;
	if (pending_emits.empty() || emit_flush_running) {
		return true;
	}
	if (is_tearing_down || !is_ready) {
		drop_pending_emits();
		return true;
	}
	std::vector<EmitRequest2D> &batch = emit_flush_scratch;
	batch.clear();
	batch.swap(pending_emits);
	emit_flush_running = true;
	const uint64_t drop_epoch = emit_drop_epoch;
	const int cap = emitter_max_shots_per_sweep;
	size_t next = 0;
	for (; next < batch.size(); ++next) {
		if (emit_drop_epoch != drop_epoch) {
			break; // a handler reset the factory: the rest never fires
		}
		if (cap > 0 && emit_stats_fired_last >= cap) {
			break; // over the cap: this and the rest wait for the next sweep
		}
		emit_flush_next = next;
		const EmitRequest2D &r = batch[next];
		if (r.dropped) {
			continue;
		}
		// The template by id: a handler (or the game) may have freed it.
		PatternSpawner2D *tpl = Object::cast_to<PatternSpawner2D>(ObjectDB::get_instance(ObjectID(r.template_id)));
		if (tpl == nullptr || tpl->is_queued_for_deletion() || !tpl->is_inside_tree()) {
			continue;
		}
		Transform2D pose = r.pose;
		if (r.aim_template) {
			pose = Transform2D((real_t)tpl->get_global_rotation(), pose.columns[2]);
		}
		++emit_stats_fired_last;
		++emit_fired_total;
		note_user_code();
		tpl->emit_shot_from(pose, r.extra_spin_deg, r.velocity); // user code: its shot signals
		if (!sweep_may_continue(alive)) {
			if (alive) {
				// Queued for deletion: the flush closes, the rest never fires.
				batch.clear();
				pending_emits.clear();
				emit_flush_running = false;
			}
			return false;
		}
	}
	if (next < batch.size() && emit_drop_epoch == drop_epoch) {
		// Carried over: ahead of anything recorded during this flush.
		std::vector<EmitRequest2D> carried;
		carried.reserve(batch.size() - next + pending_emits.size());
		for (size_t i = next; i < batch.size(); ++i) {
			if (!batch[i].dropped) {
				carried.push_back(batch[i]);
			}
		}
		emit_stats_carried_last = (int)carried.size();
		for (EmitRequest2D &late : pending_emits) {
			carried.push_back(late);
		}
		pending_emits.swap(carried);
	}
	batch.clear();
	emit_flush_running = false;
	return true;
}

} // namespace BlastBullets2D
