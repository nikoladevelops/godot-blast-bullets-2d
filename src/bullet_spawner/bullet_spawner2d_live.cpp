// BulletSpawner2D live control: what a spawner changes on the bullets it
// already fired. apply_to_live_volleys re-applies whole groups (homing,
// graze, sound, flocking, splitting) as they stand now; home_live_volleys_to,
// aim_live_volleys_at and the live speed calls steer them directly.
// Every call walks the volleys this spawner owns (live or parked, never
// pooled) through a snapshot of (pointer, id) pairs, re-validated before
// each touch: homing signals run user code that may free anything.

#include "bullet_spawner/pattern_spawner2d_internal.hpp"
#include "core/node_scan2d.hpp"

#include <unordered_set>

using namespace godot;

namespace BlastBullets2D {

namespace {
// The owned volleys with their ids, captured before any user code runs.
struct OwnedVolley2D {
	BulletVolley2D *volley;
	uint64_t id;
};

std::vector<OwnedVolley2D> snapshot_owned2d(BulletFactory2D *factory, uint64_t owner_id) {
	std::vector<BulletVolley2D *> owned;
	factory->collect_volleys_owned_by(owner_id, owned);
	std::vector<OwnedVolley2D> out;
	out.reserve(owned.size());
	for (BulletVolley2D *volley : owned) {
		out.push_back({ volley, volley->get_instance_id() });
	}
	return out;
}

// The snapshot entry, while it is still this spawner's and not pooled.
BulletVolley2D *still_owned2d(const OwnedVolley2D &entry, uint64_t owner_id) {
	if (ObjectDB::get_instance(ObjectID(entry.id)) != entry.volley) {
		return nullptr;
	}
	BulletVolley2D *volley = entry.volley;
	if (volley->owner_spawner_id != owner_id || volley->is_pooled() || volley->is_queued_for_deletion()) {
		return nullptr;
	}
	return volley;
}
} // namespace

int BulletSpawner2D::apply_to_live_volleys(BitField<LiveVolleyGroup> groups) {
	if (get_bullet_factory() == nullptr) {
		return 0;
	}
	std::unordered_set<uint64_t> changed;
	// No user code runs while these arm or disarm.
	if (groups.has_flag(LIVE_GRAZE)) {
		graze_live_volleys(changed);
	}
	if (groups.has_flag(LIVE_SOUND)) {
		sound_live_volleys(changed);
	}
	if (groups.has_flag(LIVE_FLOCK)) {
		flock_live_volleys(changed);
	}
	if (groups.has_flag(LIVE_SPLIT)) {
		split_live_volleys(changed);
	}
	if (groups.has_flag(LIVE_EMITTER)) {
		emitter_live_volleys(changed);
	}
	// Last: arming homing emits homing_targets_resolved and
	// volley_homing_configured (user code).
	if (groups.has_flag(LIVE_HOMING)) {
		homing_live_volleys(changed);
	}
	return (int)changed.size();
}

void BulletSpawner2D::graze_live_volleys(std::unordered_set<uint64_t> &r_changed) {
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		return;
	}
	bool any = false;
	const Array zones = graze_enabled ? spawner_graze_zones(any) : Array();
	if (any) {
		graze_detector_ref();
	}
	std::vector<BulletVolley2D *> owned;
	factory->collect_volleys_owned_by(get_instance_id(), owned);
	for (BulletVolley2D *volley : owned) {
		if (!any) {
			if (volley->graze_zone_slots > 0) {
				volley->graze_release();
				r_changed.insert(volley->get_instance_id());
			}
		} else if (!volley->graze_armed_with(zones, graze_detector)) {
			// A fresh graze state: the rings re-arm.
			volley->graze_arm_from_spawner(zones, graze_detector);
			r_changed.insert(volley->get_instance_id());
		}
	}
}

void BulletSpawner2D::sound_live_volleys(std::unordered_set<uint64_t> &r_changed) {
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		return;
	}
	bool any = false;
	const Array entries = sound_enabled ? spawner_sound_entries(any) : Array();
	const std::shared_ptr<GrazeDetector2D> detector = any ? sound_detector_ref() : nullptr;
	std::vector<BulletVolley2D *> owned;
	factory->collect_volleys_owned_by(get_instance_id(), owned);
	for (BulletVolley2D *volley : owned) {
		if (!any) {
			if (!volley->sound_effects.empty()) {
				volley->sound_release();
				r_changed.insert(volley->get_instance_id());
			}
		} else if (!volley->sound_armed_with(entries, detector, (float)sound_volume_db)) {
			volley->sound_arm_from_spawner(entries, detector, (float)sound_volume_db);
			r_changed.insert(volley->get_instance_id());
		}
	}
}

void BulletSpawner2D::flock_live_volleys(std::unordered_set<uint64_t> &r_changed) {
	BulletFactory2D *factory = get_bullet_factory();
	// Off reaches flying bullets through the shared config already.
	if (factory == nullptr || !flock_enabled) {
		return;
	}
	std::vector<BulletVolley2D *> owned;
	factory->collect_volleys_owned_by(get_instance_id(), owned);
	for (BulletVolley2D *volley : owned) {
		if (volley->flock_config == nullptr || volley->flock_config != flock_shared) {
			apply_volley_flock(volley);
			r_changed.insert(volley->get_instance_id());
		}
	}
}

void BulletSpawner2D::split_live_volleys(std::unordered_set<uint64_t> &r_changed) {
	BulletFactory2D *factory = get_bullet_factory();
	// Off reaches flying bullets through the shared config already.
	if (factory == nullptr || !split_enabled || split_data.is_null() || spawn_data.is_null()) {
		return;
	}
	// Children copy the spawn data like a fresh shot's (the spawner-owned copy).
	const Ref<BulletVolleyData2D> child_source = cached_volley_template.is_valid() && cached_spawn_data_id == spawn_data->get_instance_id() ? cached_volley_template : spawn_data;
	std::vector<BulletVolley2D *> owned;
	factory->collect_volleys_owned_by(get_instance_id(), owned);
	for (BulletVolley2D *volley : owned) {
		if (split_shared == nullptr || volley->split_config != split_shared) {
			apply_volley_split(volley, child_source);
			r_changed.insert(volley->get_instance_id());
		}
	}
}

void BulletSpawner2D::homing_live_volleys(std::unordered_set<uint64_t> &r_changed) {
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		return;
	}
	const uint64_t self_id = get_instance_id();
	const std::vector<OwnedVolley2D> owned = snapshot_owned2d(factory, self_id);
	for (const OwnedVolley2D &entry : owned) {
		BulletVolley2D *volley = still_owned2d(entry, self_id);
		if (volley == nullptr) {
			continue;
		}
		if (!homing_enabled) {
			// Off: the volleys stop chasing (queues and orbits cleared), parked
			// ones too (a wake would resume the chase), and leave the
			// retarget list: turned on again, they are armed again.
			const bool waiting = volley->homing_wake_owner_id != 0;
			volley->homing_wake_owner_id = 0;
			volley_tracker.untrack((int64_t)entry.id);
			if (waiting || volley->shared_homing_deque_check_has_homing_targets() || volley_has_own_homing_targets(volley)) {
				clear_volley_homing(volley);
				r_changed.insert(entry.id);
			}
			continue;
		}
		if (volley_tracker.contains((int64_t)entry.id) || volley->homing_wake_owner_id == self_id) {
			continue; // armed alike: a homing shot (or adopted) already tracked
		}
		if (!volley->is_active) {
			// Parked: frozen bullets take no targets. Armed when it wakes
			// (arm_homing_after_wake), like a fresh shot.
			volley->homing_wake_owner_id = self_id;
			r_changed.insert(entry.id);
			continue;
		}
		apply_volley_homing_and_orbiting(volley);
		r_changed.insert(entry.id);
		if (ObjectDB::get_instance(ObjectID(self_id)) != this || shot_owner_dying()) {
			return; // a homing handler freed this spawner
		}
	}
}

void BulletSpawner2D::arm_homing_after_wake(BulletVolley2D *volley) {
	// The volley just woke (BulletVolley2D::enable_bullet): still this
	// spawner's, homing still on, not armed meanwhile.
	if (volley == nullptr || !homing_enabled || volley->owner_spawner_id != get_instance_id() || !volley->is_active || volley_tracker.contains((int64_t)volley->get_instance_id())) {
		return;
	}
	apply_volley_homing_and_orbiting(volley);
}

bool BulletSpawner2D::volley_has_own_homing_targets(BulletVolley2D *volley) {
	const int count = volley->get_amount_bullets();
	for (int b = 0; b < count; ++b) {
		if (volley->bullet_check_has_homing_targets(b)) {
			return true;
		}
	}
	return false;
}

void BulletSpawner2D::clear_volley_homing(BulletVolley2D *volley) {
	// Engine clears keep every counter exact; orbit is stopped with the same
	// per-bullet guard the retarget path uses (disable warns when already off).
	volley->shared_homing_deque_clear_homing_targets();
	volley->all_bullets_clear_homing_targets();
	const int bullet_count = volley->get_amount_bullets();
	for (int b = 0; b < bullet_count; ++b) {
		if (volley->bullet_is_orbiting_enabled(b)) {
			volley->bullet_disable_orbiting(b);
		}
	}
}

int BulletSpawner2D::home_live_volleys_to(const Variant &target) {
	// What to chase: a node, a point, the nearest member of a group per
	// bullet, or the mouse (null).
	Node2D *node = nullptr;
	Vector2 point;
	StringName group;
	const Variant::Type type = target.get_type();
	bool ok = true;
	if (type == Variant::NIL) {
		// The mouse.
	} else if (type == Variant::OBJECT) {
		node = Object::cast_to<Node2D>((Object *)target);
		ok = node != nullptr && node->is_inside_tree() && !node->is_queued_for_deletion() && node->get_global_position().is_finite();
	} else if (type == Variant::VECTOR2) {
		point = target;
		ok = point.is_finite();
	} else if (type == Variant::STRING_NAME || type == Variant::STRING) {
		group = target;
		ok = !group.is_empty();
	} else {
		ok = false;
	}
	if (!ok) {
		UtilityFunctions::push_error(String(get_class()) + "::home_live_volleys_to: target must be a Node2D in the tree, a finite Vector2, a group name or null (the mouse), nothing changed.");
		return 0;
	}
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr || !is_inside_tree()) {
		return 0;
	}
	// Group members, filtered like every node target (never the spawner).
	std::vector<std::pair<Node2D *, Vector2>> members;
	if (!group.is_empty()) {
		const World2D *world = homing_target_world();
		const TypedArray<Node> nodes = get_tree()->get_nodes_in_group(group);
		for (int i = 0; i < nodes.size(); ++i) {
			Node2D *member = Object::cast_to<Node2D>((Object *)nodes[i]);
			Vector2 position;
			if (member != nullptr && member != this && target_node_usable2d(member, world, position)) {
				members.push_back({ member, position });
			}
		}
	}
	const uint64_t self_id = get_instance_id();
	const uint64_t node_id = node != nullptr ? node->get_instance_id() : 0;
	const std::vector<OwnedVolley2D> owned = snapshot_owned2d(factory, self_id);
	int done = 0;
	for (const OwnedVolley2D &entry : owned) {
		BulletVolley2D *volley = still_owned2d(entry, self_id);
		if (volley == nullptr || (node != nullptr && ObjectDB::get_instance(ObjectID(node_id)) != node)) {
			continue;
		}
		// The steering of the Homing group (bullets fired without homing turn too).
		apply_steering_to_volley(volley);
		volley->shared_homing_deque_clear_homing_targets();
		volley->all_bullets_clear_homing_targets();
		if (node != nullptr) {
			volley->shared_homing_deque_replace_homing_targets_with_new_target(node);
		} else if (type == Variant::VECTOR2) {
			volley->shared_homing_deque_push_back_global_position_target(point);
		} else if (!group.is_empty()) {
			const int count = volley->get_amount_bullets();
			for (int b = 0; b < count && !members.empty(); ++b) {
				if (!volley->is_bullet_status_enabled(b)) {
					continue;
				}
				const Vector2 at = volley->get_bullet_transform(b).get_origin();
				size_t best = 0;
				real_t best_d = members[0].second.distance_squared_to(at);
				for (size_t m = 1; m < members.size(); ++m) {
					const real_t d = members[m].second.distance_squared_to(at);
					if (d < best_d) {
						best_d = d;
						best = m;
					}
				}
				volley->bullet_replace_homing_targets_with_new_target(b, members[best].first);
			}
		} else {
			volley->shared_homing_deque_push_back_mouse_position_target();
		}
		track_live_volley(volley);
		++done;
	}
	return done;
}

int BulletSpawner2D::aim_live_volleys_at(const Vector2 &position) {
	if (!position.is_finite()) {
		UtilityFunctions::push_error(String(get_class()) + "::aim_live_volleys_at: position must be finite, nothing changed.");
		return 0;
	}
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		return 0;
	}
	const uint64_t self_id = get_instance_id();
	const std::vector<OwnedVolley2D> owned = snapshot_owned2d(factory, self_id);
	int done = 0;
	for (const OwnedVolley2D &entry : owned) {
		BulletVolley2D *volley = still_owned2d(entry, self_id);
		if (volley == nullptr) {
			continue;
		}
		volley->all_bullets_set_direction_towards_position(position);
		++done;
	}
	return done;
}

int BulletSpawner2D::set_live_volleys_speed(double speed) {
	if (!is_finite_real2d(speed)) {
		UtilityFunctions::push_error(String(get_class()) + "::set_live_volleys_speed: speed must be finite, nothing changed.");
		return 0;
	}
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		return 0;
	}
	const uint64_t self_id = get_instance_id();
	const std::vector<OwnedVolley2D> owned = snapshot_owned2d(factory, self_id);
	int done = 0;
	for (const OwnedVolley2D &entry : owned) {
		BulletVolley2D *volley = still_owned2d(entry, self_id);
		if (volley == nullptr) {
			continue;
		}
		volley->all_bullets_set_speed((real_t)speed);
		++done;
	}
	return done;
}

int BulletSpawner2D::scale_live_volleys_speed(double factor) {
	if (!is_finite_real2d(factor) || factor < 0.0) {
		UtilityFunctions::push_error(String(get_class()) + "::scale_live_volleys_speed: factor must be finite and >= 0, nothing changed.");
		return 0;
	}
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		return 0;
	}
	const uint64_t self_id = get_instance_id();
	const std::vector<OwnedVolley2D> owned = snapshot_owned2d(factory, self_id);
	std::vector<real_t> factors;
	int done = 0;
	for (const OwnedVolley2D &entry : owned) {
		BulletVolley2D *volley = still_owned2d(entry, self_id);
		if (volley == nullptr) {
			continue;
		}
		// Speed, top speed and acceleration together (the shot's speed roll).
		factors.assign((size_t)volley->get_amount_bullets(), (real_t)factor);
		volley->apply_speed_factors(factors.data(), (int)factors.size());
		++done;
	}
	return done;
}

int BulletSpawner2D::override_live_volleys_velocity(const Vector2 &new_velocity) {
	if (!new_velocity.is_finite()) {
		UtilityFunctions::push_error("BulletSpawner2D::override_live_volleys_velocity: velocity must be finite.");
		return 0;
	}
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		return 0;
	}
	// Every volley this spawner owns (homing or not). Engine-owned semantics:
	// a speed curve overrides direct velocity (the engine warns and no-ops),
	// so this visibly works only for curve-free volleys.
	const uint64_t self_id = get_instance_id();
	const std::vector<OwnedVolley2D> owned = snapshot_owned2d(factory, self_id);
	int done = 0;
	for (const OwnedVolley2D &entry : owned) {
		BulletVolley2D *volley = still_owned2d(entry, self_id);
		if (volley == nullptr) {
			continue;
		}
		volley->all_bullets_set_velocity(new_velocity);
		++done;
	}
	return done;
}

} // namespace BlastBullets2D
