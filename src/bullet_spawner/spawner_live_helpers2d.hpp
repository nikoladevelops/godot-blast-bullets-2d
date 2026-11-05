#pragma once

// Private to the bullet_spawner2d*.cpp translation units: the (pointer, id)
// snapshot every live-control leg walks (bullet_spawner2d_live.cpp,
// bullet_spawner2d_emitter.cpp). Never include from anywhere else.
//
// A leg arms or releases the user Resources of the volleys this spawner owns
// (zones, sound entries, the flock, the split chain, the emitter timing).
// Dropping the last Ref destroys such a resource, and a script resource's
// NOTIFICATION_PREDELETE is user code that may free this spawner mid-leg
// (proven under AddressSanitizer); re-arming a volley also replaces the config
// a script handed it (emitter_set / split_set_data), whose Ref to the resource
// is then the last one. User code is reached from inside those calls, so the
// volley pointer is validated through ObjectDB before every touch and the
// spawner is re-checked after every callback.
//
// Functions are `static inline` (one private copy per TU, no ODR coupling, no
// unused-function warnings), like pattern_spawner2d_internal.hpp.

#include "bullet_volley/bullet_volley2d.hpp"
#include "godot_cpp/core/object.hpp"

#include <cstdint>
#include <vector>

namespace BlastBullets2D {

// One owned volley with its id, captured before any user code runs.
struct OwnedVolley2D {
	BulletVolley2D *volley;
	uint64_t id;
};

// Every volley this spawner owns (live or parked, never pooled) as
// (pointer, id) pairs: read the pointer only through still_owned2d().
static inline std::vector<OwnedVolley2D> snapshot_owned2d(BulletFactory2D *factory, uint64_t owner_id) {
	std::vector<BulletVolley2D *> owned;
	factory->collect_volleys_owned_by(owner_id, owned);
	std::vector<OwnedVolley2D> out;
	out.reserve(owned.size());
	for (BulletVolley2D *volley : owned) {
		out.push_back({ volley, volley->get_instance_id() });
	}
	return out;
}

// The snapshot entry, while it is still this spawner's and not pooled: the
// only way a leg gets a volley pointer back after user code ran inside it.
static inline BulletVolley2D *still_owned2d(const OwnedVolley2D &entry, uint64_t owner_id) {
	if (ObjectDB::get_instance(ObjectID(entry.id)) != entry.volley) {
		return nullptr;
	}
	BulletVolley2D *volley = entry.volley;
	if (volley->owner_spawner_id != owner_id || volley->is_pooled() || volley->is_queued_for_deletion()) {
		return nullptr;
	}
	return volley;
}

} // namespace BlastBullets2D
