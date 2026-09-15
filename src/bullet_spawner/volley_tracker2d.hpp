#pragma once

#include "bullets/directional_bullets2d.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/variant/packed_int64_array.hpp"

namespace BlastBullets2D {
using namespace godot;

// Owns one spawner's tracked-volley id list with the prune-before-touch
// invariant made structural (item 6).
//
// The list only ever holds plain instance ids — no ownership, no node
// pointers — so a freed, pooled, or re-homed volley can never be touched
// through it: every reader prunes first, and every resolved volley is
// re-validated (type + owner_spawner_id + is_active) before use. That pair
// is what makes adopt_live_volley handovers and pool reuse safe even though
// the previous owner's list still holds the id until its next prune.
//
// Bounded (oldest dropped first) so infinite-lifetime volleys can't grow
// retarget cost without bound. Header-only: all operations are O(tracked).
class VolleyTracker2D {
public:
	// Hard cap on tracked volleys. Retarget passes are O(volleys * bullets),
	// so an unbounded list would let infinite-lifetime auto-shoot sessions
	// grow per-tick cost forever. Oldest dropped first (newest volleys matter
	// most for retargeting). Use clear_live_volleys() to reset manually.
	static constexpr int MAX_TRACKED_VOLLEYS = 256;

	void track(DirectionalBullets2D *volley, uint64_t owner_spawner_id) {
		if (volley == nullptr) {
			return;
		}
		const int64_t id = (int64_t)volley->get_instance_id();
		if (!live_ids.has(id)) {
			live_ids.push_back(id);
		}
		prune(owner_spawner_id);
		while (live_ids.size() > MAX_TRACKED_VOLLEYS) {
			live_ids.remove_at(0);
		}
	}

	void clear() {
		live_ids.clear();
	}

	// Drops freed instances (teardown), volleys re-owned by another spawner
	// (owner tag changed, e.g. adopt_live_volley), and fully-disabled
	// instances (a new enable wipes homing anyway). Never touches survivors.
	// The is_active gate matters: disable keeps owner_spawner_id, so without
	// it retargets would arm dead queues and inflate homing counters.
	void prune(uint64_t owner_spawner_id) const {
		PackedInt64Array kept;
		for (int i = 0; i < live_ids.size(); ++i) {
			if (resolve_live(live_ids[i], owner_spawner_id) != nullptr) {
				kept.push_back(live_ids[i]);
			}
		}
		live_ids = kept;
	}

	int count(uint64_t owner_spawner_id) const {
		prune(owner_spawner_id);
		return live_ids.size();
	}

	bool is_empty(uint64_t owner_spawner_id) const {
		prune(owner_spawner_id);
		return live_ids.is_empty();
	}

	// Pruned id snapshot for index-based passes (e.g. newest-only retarget).
	// Resolve each id with resolve_live() before touching the volley.
	PackedInt64Array snapshot(uint64_t owner_spawner_id) const {
		prune(owner_spawner_id);
		return live_ids;
	}

	// Resolves one tracked id to a live volley owned by the given spawner.
	// Null for freed instances, foreign-owned volleys, inactive volleys, and
	// volleys queued for deletion (dying at flush: handing them out for
	// direct engine calls would let scripts drive a corpse for a frame).
	// Every touch of a tracked volley must go through here (or prune + an
	// equivalent check): never dereference a stored id blindly.
	static DirectionalBullets2D *resolve_live(int64_t id, uint64_t owner_spawner_id) {
		Object *obj = ObjectDB::get_instance(ObjectID((uint64_t)id));
		DirectionalBullets2D *volley = Object::cast_to<DirectionalBullets2D>(obj);
		if (volley == nullptr || volley->owner_spawner_id != owner_spawner_id || !volley->is_active || volley->is_queued_for_deletion()) {
			return nullptr;
		}
		return volley;
	}

private:
	// Mutable: even const readers (counts, volley lists) prune dead entries,
	// so queries never report corpses.
	mutable PackedInt64Array live_ids;
};

} //namespace BlastBullets2D
