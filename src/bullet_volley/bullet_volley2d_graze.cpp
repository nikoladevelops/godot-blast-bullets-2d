// Graze: arming (graze_set_zones for factory volleys, graze_arm_from_spawner
// for spawner volleys), the per-tick snapshot (prepare_graze_tick: the
// volley's target list, rings sorted, bullet size folded in), live event
// dispatch and the state readouts. The per-bullet test (step_graze) lives in
// bullet_volley2d_tick.cpp with the other move stages.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

bool BulletVolley2D::graze_collect_zones(const Array &zones, Ref<BulletGrazeZone2D> *r_armed, int &r_slots) {
	r_slots = 0;
	if (zones.size() > MAX_GRAZE_ZONES) {
		UtilityFunctions::push_error("graze_set_zones: at most 4 zones (got " + String::num_int64(zones.size()) + "), nothing changed.");
		return false;
	}
	for (int i = 0; i < zones.size(); ++i) {
		const Variant &entry = zones[i];
		if (entry.get_type() == Variant::NIL) {
			continue;
		}
		BulletGrazeZone2D *zone = Object::cast_to<BulletGrazeZone2D>(entry.get_type() == Variant::OBJECT ? (Object *)entry : nullptr);
		if (zone == nullptr) {
			UtilityFunctions::push_error("graze_set_zones: entry " + String::num_int64(i) + " is not a BulletGrazeZone2D, nothing changed.");
			return false;
		}
		r_armed[i] = Ref<BulletGrazeZone2D>(zone);
		r_slots = i + 1;
	}
	return true;
}

bool BulletVolley2D::graze_set_zones(const Array &zones, const StringName &target_group) {
	if (reject_pooled_handle("graze_set_zones")) {
		return false;
	}
	Ref<BulletGrazeZone2D> armed[MAX_GRAZE_ZONES];
	int slots = 0;
	if (!graze_collect_zones(zones, armed, slots)) {
		return false;
	}
	if (target_group.is_empty()) {
		UtilityFunctions::push_error("graze_set_zones: target_group is empty (name the group whose nodes graze these bullets), nothing changed.");
		return false;
	}
	graze_arm(armed, slots, nullptr, target_group);
	return true;
}

bool BulletVolley2D::graze_arm_from_spawner(const Array &zones, const std::shared_ptr<GrazeDetector2D> &detector) {
	Ref<BulletGrazeZone2D> armed[MAX_GRAZE_ZONES];
	int slots = 0;
	if (detector == nullptr || !graze_collect_zones(zones, armed, slots)) {
		return false;
	}
	graze_arm(armed, slots, detector, StringName());
	return true;
}

void BulletVolley2D::graze_arm(const Ref<BulletGrazeZone2D> *armed, int slots, const std::shared_ptr<GrazeDetector2D> &detector, const StringName &target_group) {
	graze_release();
	if (slots == 0) {
		return;
	}
	for (int i = 0; i < slots; ++i) {
		graze_zones[i] = armed[i];
	}
	graze_zone_slots = slots;
	// Another detector or group hands out other lists: the next tick meets
	// a new list uid and moves or ends open visits like any target change.
	graze_detector = detector;
	graze_target_group = target_group;
	graze_state.assign((size_t)amount_bullets * (size_t)slots, 0);
	graze_anchor.assign((size_t)amount_bullets * (size_t)slots, 0);
	// Bullet size scaling, measured once: the largest basis scale of the
	// volley's bullets (scaled or mirrored patterns keep their real size).
	real_t scale = 0.0;
	for (int i = 0; i < amount_bullets && i < (int)all_cached_instance_transforms.size(); ++i) {
		const Transform2D &tr = all_cached_instance_transforms[i];
		scale = MAX(scale, MAX(tr.columns[0].length(), tr.columns[1].length()));
	}
	graze_bullet_scale = (Math::is_finite(scale) && scale > 0.0) ? scale : (real_t)1.0;
}

void BulletVolley2D::graze_clear() {
	if (reject_pooled_handle("graze_clear")) {
		return;
	}
	graze_release();
}

void BulletVolley2D::graze_release() {
	for (Ref<BulletGrazeZone2D> &zone : graze_zones) {
		zone.unref();
	}
	graze_zone_slots = 0;
	graze_detector.reset();
	graze_target_group = StringName();
	++graze_generation;
	graze_state.clear();
	graze_anchor.clear();
	graze_tick_targets = GrazeTickTargets2D();
	for (GrazeTickZone2D &tz : graze_tick_zones) {
		tz = GrazeTickZone2D();
	}
	graze_events.clear();
	graze_tick_active = false;
	graze_zones_with_targets = 0;
	graze_bullet_scale = 1.0;
	graze_events_dispatched = 0;
}

Array BulletVolley2D::get_graze_zones() const {
	Array out;
	for (int i = 0; i < graze_zone_slots; ++i) {
		// A plain null for an empty slot (not a null object).
		out.push_back(graze_zones[i].is_valid() ? Variant(graze_zones[i]) : Variant());
	}
	return out;
}

bool BulletVolley2D::is_graze_armed() const {
	return graze_zone_slots > 0;
}

// Shared entry check of the per-bullet readouts.
static bool graze_check_zone_index(int zone_index, int slots, const char *function_name) {
	if (zone_index < 0 || zone_index >= slots) {
		UtilityFunctions::push_error(String(function_name) + ": zone_index " + String::num_int64(zone_index) + " is out of range (the volley has " + String::num_int64(slots) + " graze zone slots).");
		return false;
	}
	return true;
}

int BulletVolley2D::get_bullet_grazed_rings(int bullet_index, int zone_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_grazed_rings") || !graze_check_zone_index(zone_index, graze_zone_slots, "get_bullet_grazed_rings")) {
		return 0;
	}
	const size_t at = (size_t)bullet_index * graze_zone_slots + zone_index;
	return at < graze_state.size() ? (int)(graze_state[at] & GRAZE_RINGS_MASK) : 0;
}

bool BulletVolley2D::is_bullet_inside_graze(int bullet_index, int zone_index) const {
	if (!validate_bullet_index(bullet_index, "is_bullet_inside_graze") || !graze_check_zone_index(zone_index, graze_zone_slots, "is_bullet_inside_graze")) {
		return false;
	}
	const size_t at = (size_t)bullet_index * graze_zone_slots + zone_index;
	return at < graze_state.size() && (graze_state[at] & GRAZE_INSIDE) != 0;
}

void BulletVolley2D::bullet_reset_graze(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_reset_graze")) {
		return;
	}
	const size_t from = (size_t)bullet_index * graze_zone_slots;
	for (int z = 0; z < graze_zone_slots && from + z < graze_state.size(); ++z) {
		graze_state[from + z] = 0;
	}
}

void BulletVolley2D::graze_end_visits_of_bullet(int bullet_index) {
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return;
	}
	const size_t from = (size_t)bullet_index * graze_zone_slots;
	for (int z = 0; z < graze_zone_slots && from + z < graze_state.size(); ++z) {
		uint16_t &st = graze_state[from + z];
		st &= (uint16_t)~(GRAZE_INSIDE | GRAZE_DEEPEST_MASK | GRAZE_VISIT_FIRED);
		if (graze_zones[z].is_valid() && graze_zones[z]->regraze == BulletGrazeZone2D::REGRAZE_AFTER_EXIT) {
			st &= (uint16_t)~GRAZE_RINGS_MASK;
		}
	}
}

void BulletVolley2D::graze_end_visits_of_zone(int zone_index) {
	if (zone_index < 0 || zone_index >= graze_zone_slots) {
		return;
	}
	const bool rearm = graze_zones[zone_index].is_valid() && graze_zones[zone_index]->regraze == BulletGrazeZone2D::REGRAZE_AFTER_EXIT;
	const uint16_t keep = rearm ? (uint16_t)0 : GRAZE_RINGS_MASK; // the visit bits always go
	for (size_t at = (size_t)zone_index; at < graze_state.size(); at += (size_t)graze_zone_slots) {
		graze_state[at] &= keep;
	}
}

GrazeTargetList2D *BulletVolley2D::graze_target_list() {
	if (bullet_factory == nullptr) {
		return nullptr;
	}
	if (graze_detector != nullptr) {
		return graze_detector->list(*bullet_factory);
	}
	return graze_target_group.is_empty() ? nullptr : bullet_factory->get_graze_default_detector().list_for_group(graze_target_group, *bullet_factory);
}

void BulletVolley2D::prepare_graze_tick() {
	graze_tick_active = false;
	if (graze_zone_slots <= 0 || bullet_factory == nullptr || graze_state.size() != (size_t)amount_bullets * (size_t)graze_zone_slots) {
		return;
	}
	// Bullet size: the shape's bounding radius (read every tick: a runtime
	// shape change counts at once) times the scale measured at arming.
	const real_t bullet_radius = graze_shape_bound_radius2d(cached_effective_shape_type, cached_circle_radius, cached_rect_size, cached_capsule_height) * graze_bullet_scale;
	// The targets: one list for every zone, updated at most once per sweep
	// and shared by every volley of its detector. Read in place; copied only
	// when few (inline) or when this volley's owner spawner is one of them.
	GrazeTickTargets2D &tg = graze_tick_targets;
	const GrazeTargetList2D *list = graze_target_list();
	tg.count = 0;
	tg.inline_count = 0;
	tg.sorted = false;
	tg.order = nullptr;
	if (list == nullptr) {
		tg.seen_list_uid = 0;
		tg.seen_list_serial = 0;
		tg.switched = false;
		tg.slot_ids = nullptr;
		tg.slot_changed = nullptr;
		tg.slot_count = 0;
	} else {
		// Another list than last tick (new source or group): every visit's
		// target changed. The first list a volley meets changes nothing.
		tg.switched = tg.seen_list_uid != 0 && tg.seen_list_uid != list->uid;
		tg.prev_serial = tg.seen_list_uid == list->uid ? tg.seen_list_serial : list->serial;
		tg.seen_list_uid = list->uid;
		tg.seen_list_serial = list->serial;
		tg.slot_ids = list->slot_ids.data();
		tg.slot_changed = list->slot_changed.data();
		tg.slot_count = (uint32_t)list->slot_ids.size();
		const int n = list->count();
		const bool owner_is_target = owner_spawner_id != 0 && list->contains(owner_spawner_id);
		tg.sorted = list->by_x_valid;
		if (!tg.sorted && n <= GrazeTickTargets2D::INLINE_TARGETS) {
			for (int k = 0; k < n; ++k) {
				const uint32_t slot = list->view_slots[k];
				// The owner spawner never grazes its own bullets.
				if (owner_is_target && tg.slot_ids[slot] == owner_spawner_id) {
					continue;
				}
				tg.inline_centers[tg.inline_count] = list->view_centers[k];
				tg.inline_slots[tg.inline_count] = slot;
				++tg.inline_count;
			}
			tg.count = tg.inline_count;
			tg.centers = tg.inline_centers;
			tg.slots = tg.inline_slots;
		} else if (owner_is_target) {
			graze_tick_centers.resize(n);
			graze_tick_slots.resize(n);
			graze_tick_order.resize(n);
			for (int k = 0; k < n; ++k) {
				const uint32_t slot = list->view_slots[k];
				if (tg.slot_ids[slot] == owner_spawner_id) {
					continue;
				}
				graze_tick_centers[tg.count] = list->view_centers[k];
				graze_tick_slots[tg.count] = slot;
				graze_tick_order[tg.count] = list->view_order[k];
				++tg.count;
			}
			tg.centers = graze_tick_centers.data();
			tg.slots = graze_tick_slots.data();
			tg.order = graze_tick_order.data();
		} else {
			tg.count = n;
			tg.centers = list->view_centers.data();
			tg.slots = list->view_slots.data();
			tg.order = list->view_order.data();
		}
	}
	real_t reach = 0.0;
	for (int z = 0; z < graze_zone_slots; ++z) {
		GrazeTickZone2D &tz = graze_tick_zones[z];
		const BulletGrazeZone2D *zone = graze_zones[z].ptr();
		tz.active = zone != nullptr && zone->enabled && tg.count > 0;
		if (tz.active) {
			// Rings by radius, largest first (insertion sort: equal radii
			// keep ring order, so their events fire by ring index).
			const real_t inflate = zone->count_bullet_size ? bullet_radius : (real_t)0.0;
			tz.ring_count = CLAMP(zone->ring_count, 1, BulletGrazeZone2D::MAX_RINGS);
			real_t radius[BulletGrazeZone2D::MAX_RINGS];
			for (int r = 0; r < tz.ring_count; ++r) {
				const real_t value = zone->ring_radii[r] + inflate;
				int at = r;
				while (at > 0 && radius[at - 1] < value) {
					radius[at] = radius[at - 1];
					tz.ring_index[at] = tz.ring_index[at - 1];
					--at;
				}
				radius[at] = value;
				tz.ring_index[at] = (uint8_t)r;
			}
			for (int s = 0; s < tz.ring_count; ++s) {
				tz.ring_r2[s] = radius[s] * radius[s];
				tz.ring_slot[tz.ring_index[s]] = (uint8_t)s;
			}
			tz.regraze_after_exit = zone->regraze == BulletGrazeZone2D::REGRAZE_AFTER_EXIT;
			// A target farther than this in x can never reach the outermost
			// ring; the margin keeps every boundary case on the exact test.
			tz.reach = radius[0] * (real_t)1.001 + (real_t)0.001;
			reach = MAX(reach, tz.reach);
		}
		const uint8_t bit = (uint8_t)(1u << z);
		if (tz.active) {
			graze_tick_active = true;
			graze_zones_with_targets |= bit;
		} else if ((graze_zones_with_targets & bit) != 0) {
			// The zone lost every target (freed, left the group, zone
			// disabled): open visits end silently, once.
			graze_zones_with_targets &= (uint8_t)~bit;
			graze_end_visits_of_zone(z);
		}
	}
	tg.slab_reach = tg.sorted ? reach : (real_t)Math::INF;
}

bool BulletVolley2D::dispatch_graze_events() {
	graze_dispatch_scratch.clear();
	graze_dispatch_scratch.swap(graze_events);
	const uint64_t self_id = get_instance_id();
	const CachedStringNames2D &names = CachedStringNames2D::get();
	for (size_t k = 0; k < graze_dispatch_scratch.size(); ++k) {
		const GrazeEvent2D ev = graze_dispatch_scratch[k];
		// An earlier handler re-armed or cleared the zones, or disabled /
		// woke this bullet (epoch moved): the event belongs to a past state.
		if (ev.generation != graze_generation || ev.zone >= graze_zone_slots || graze_zones[ev.zone].is_null()) {
			continue;
		}
		if (ev.bullet_index < 0 || ev.bullet_index >= amount_bullets || !all_bullets_enabled_set.contains(ev.bullet_index) || collision_epoch_for_bullet(ev.bullet_index) != ev.epoch) {
			continue;
		}
		// A target freed (or queued for deletion) since the move is no
		// longer something to report. A point (Mouse, Global Positions) is
		// no node: the signals carry a null target and it never dies.
		const bool point = graze_is_point_target2d(ev.target_id);
		Node2D *target = point ? nullptr : Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(ev.target_id)));
		if (!point && (target == nullptr || target->is_queued_for_deletion())) {
			continue;
		}
		const Ref<BulletGrazeZone2D> zone = graze_zones[ev.zone];
		// The graze is delivered (drop checks above passed): the sound
		// offers whether or not anything is connected.
		if (ev.bullet_index >= 0 && ev.bullet_index < (int)all_cached_instance_transforms.size()) {
			sound_fire(ev.exit ? BulletSoundData2D::SOUND_ON_GRAZE_EXIT : BulletSoundData2D::SOUND_ON_GRAZE, ev.bullet_index, all_cached_instance_transforms[ev.bullet_index].get_origin(), (int)ev.ring, (int)ev.zone);
		}
		const StringName &signal_name = ev.exit ? names.bullet_graze_exited : names.bullet_grazed;
		bool handled = false;
		// Bubbling: the owner spawner first (while it lives), then the
		// factory, which receives every graze whoever owns the volley.
		Object *spawner = owner_spawner_id != 0 ? ObjectDB::get_instance(ObjectID(owner_spawner_id)) : nullptr;
		if (spawner != nullptr && spawner->has_connections(signal_name)) {
			handled = true;
			note_user_code();
			spawner->emit_signal(signal_name, target, this, ev.bullet_index, zone, (int)ev.ring);
			if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
				return false;
			}
			if (is_queued_for_deletion()) {
				graze_dispatch_scratch.clear();
				return true;
			}
			// The handler may have freed the target: the factory's
			// handler gets a live one or nothing.
			if (!point) {
				target = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(ev.target_id)));
				if (target == nullptr || target->is_queued_for_deletion()) {
					continue;
				}
			}
		}
		if (bullet_factory != nullptr && bullet_factory->has_connections(signal_name)) {
			handled = true;
			note_user_code();
			bullet_factory->emit_signal(signal_name, target, this, ev.bullet_index, zone, (int)ev.ring);
			if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
				return false;
			}
			if (is_queued_for_deletion()) {
				graze_dispatch_scratch.clear();
				return true;
			}
		}
		if (bullet_factory != nullptr) {
			++bullet_factory->stats_graze_events_total;
		}
		++graze_events_dispatched;
		if (!handled && !ev.exit) {
			Object *emitter = spawner != nullptr ? spawner : (Object *)bullet_factory;
			if (emitter != nullptr) {
				WarnOnce2D::warn(emitter->get_instance_id(), 25u, 0, 0, describe_emitter2d(emitter) + ": its bullets were grazed, but nothing is connected to bullet_grazed (on it or on BulletFactory2D), so the graze is not handled. Connect BulletFactory2D.bullet_grazed once (it receives every graze), or remove the graze zones.");
			}
		}
	}
	graze_dispatch_scratch.clear();
	return true;
}

Dictionary BulletVolley2D::debug_get_graze_info() const {
	Dictionary d;
	d["armed"] = graze_zone_slots > 0;
	d["zone_slots"] = graze_zone_slots;
	d["state_bytes"] = (int64_t)(graze_state.size() * sizeof(uint16_t) + graze_anchor.size() * sizeof(uint32_t));
	d["active_this_tick"] = graze_tick_active;
	d["pending_events"] = (int64_t)graze_events.size();
	d["dispatched_events"] = (int64_t)graze_events_dispatched;
	d["bullet_radius"] = graze_shape_bound_radius2d(cached_effective_shape_type, cached_circle_radius, cached_rect_size, cached_capsule_height) * graze_bullet_scale;
	d["generation"] = (int64_t)graze_generation;
	// Who grazes: the group of a factory volley, or its spawner's settings.
	d["target_group"] = graze_target_group;
	d["spawner_targets"] = graze_detector != nullptr;
	d["targets"] = graze_tick_targets.count; // as of the last tick
	return d;
}

} // namespace BlastBullets2D
