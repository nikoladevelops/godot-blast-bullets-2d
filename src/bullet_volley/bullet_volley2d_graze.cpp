// Graze: arming (graze_set_zones), the per-tick zone snapshot
// (prepare_graze_tick: targets from the factory cache, rings sorted, bullet
// size folded in), live event dispatch and the state readouts. The
// per-bullet test (step_graze) lives in bullet_volley2d_tick.cpp with the
// other move stages.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

bool BulletVolley2D::graze_set_zones(const Array &zones) {
	if (reject_pooled_handle("graze_set_zones")) {
		return false;
	}
	if (zones.size() > MAX_GRAZE_ZONES) {
		UtilityFunctions::push_error("graze_set_zones: at most 4 zones (got " + String::num_int64(zones.size()) + "), nothing changed.");
		return false;
	}
	Ref<BulletGrazeZone2D> armed[MAX_GRAZE_ZONES];
	int slots = 0;
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
		armed[i] = Ref<BulletGrazeZone2D>(zone);
		slots = i + 1;
	}
	graze_release();
	for (int i = 0; i < slots; ++i) {
		graze_zones[i] = armed[i];
	}
	graze_zone_slots = slots;
	if (slots == 0) {
		return true;
	}
	graze_state.assign((size_t)amount_bullets * (size_t)slots, 0);
	// Bullet size scaling, measured once: the largest basis scale of the
	// volley's bullets (scaled or mirrored patterns keep their real size).
	real_t scale = 0.0;
	for (int i = 0; i < amount_bullets && i < (int)all_cached_instance_transforms.size(); ++i) {
		const Transform2D &tr = all_cached_instance_transforms[i];
		scale = MAX(scale, MAX(tr.columns[0].length(), tr.columns[1].length()));
	}
	graze_bullet_scale = (Math::is_finite(scale) && scale > 0.0) ? scale : (real_t)1.0;
	return true;
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
	++graze_generation;
	graze_state.clear();
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
		st &= (uint16_t)~(GRAZE_INSIDE | GRAZE_DEEPEST_MASK | GRAZE_VISIT_FIRED | GRAZE_ANCHOR_MASK);
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
	const uint16_t keep = rearm ? (uint16_t)0 : GRAZE_RINGS_MASK; // visit bits and anchor always go
	for (size_t at = (size_t)zone_index; at < graze_state.size(); at += (size_t)graze_zone_slots) {
		graze_state[at] &= keep;
	}
}

void BulletVolley2D::prepare_graze_tick() {
	graze_tick_active = false;
	if (graze_zone_slots <= 0 || bullet_factory == nullptr || graze_state.size() != (size_t)amount_bullets * (size_t)graze_zone_slots) {
		return;
	}
	// Bullet size: the shape's bounding radius (read every tick: a runtime
	// shape change counts at once) times the scale measured at arming.
	const real_t bullet_radius = graze_shape_bound_radius2d(cached_effective_shape_type, cached_circle_radius, cached_rect_size, cached_capsule_height) * graze_bullet_scale;
	for (int z = 0; z < graze_zone_slots; ++z) {
		GrazeTickZone2D &tz = graze_tick_zones[z];
		tz.target_count = 0;
		GrazeTarget2D live[BulletGrazeZone2D::MAX_TARGETS];
		int live_count = 0;
		const BulletGrazeZone2D *zone = graze_zones[z].ptr();
		if (zone != nullptr && zone->enabled && zone->preview_during_runtime) {
			// Keeps the factory's runtime ring preview drawing this zone
			// (cheap: a flag check once awake).
			bullet_factory->wake_graze_runtime_preview();
		}
		if (zone != nullptr && zone->enabled && !zone->target_group.is_empty()) {
			GrazeTarget2D found[BulletGrazeZone2D::MAX_TARGETS];
			const int count = bullet_factory->graze_targets_for(zone->target_group, found);
			for (int k = 0; k < count; ++k) {
				// The snapshot may predate a handler of an earlier volley of
				// this sweep: re-resolve, never trust a stored id. The owner
				// spawner never grazes its own bullets.
				if (found[k].id == owner_spawner_id) {
					continue;
				}
				Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(found[k].id)));
				if (node == nullptr || node->is_queued_for_deletion()) {
					continue;
				}
				live[live_count++] = found[k];
			}
		}
		// Stable slots: a target keeps its slot while it lives; a vanished
		// one frees it (flagged for this tick), a new one takes a free slot.
		tz.vanished_slots = 0;
		for (int slot = 0; slot < BulletGrazeZone2D::MAX_TARGETS; ++slot) {
			if (tz.slot_ids[slot] == 0) {
				continue;
			}
			bool alive = false;
			for (int k = 0; k < live_count && !alive; ++k) {
				alive = live[k].id == tz.slot_ids[slot];
			}
			if (!alive) {
				tz.slot_ids[slot] = 0;
				tz.vanished_slots |= (uint8_t)(1u << slot);
			}
		}
		for (int k = 0; k < live_count; ++k) {
			int slot = -1;
			for (int s = 0; s < BulletGrazeZone2D::MAX_TARGETS && slot < 0; ++s) {
				if (tz.slot_ids[s] == live[k].id) {
					slot = s;
				}
			}
			for (int s = 0; s < BulletGrazeZone2D::MAX_TARGETS && slot < 0; ++s) {
				if (tz.slot_ids[s] == 0) {
					slot = s;
					tz.slot_ids[s] = live[k].id;
				}
			}
			if (slot < 0) {
				continue; // unreachable: at most MAX_TARGETS live targets, as many slots
			}
			tz.centers[tz.target_count] = live[k].position;
			tz.slots[tz.target_count] = (uint8_t)slot;
			++tz.target_count;
		}
		if (zone != nullptr && tz.target_count > 0) {
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
		}
		const uint8_t bit = (uint8_t)(1u << z);
		if (tz.target_count > 0) {
			graze_tick_active = true;
			graze_zones_with_targets |= bit;
		} else if ((graze_zones_with_targets & bit) != 0) {
			// Every target of the zone vanished (freed, left the group,
			// zone disabled): open visits end silently, once.
			graze_zones_with_targets &= (uint8_t)~bit;
			graze_end_visits_of_zone(z);
		}
	}
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
		// longer something to report.
		Node2D *target = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(ev.target_id)));
		if (target == nullptr || target->is_queued_for_deletion()) {
			continue;
		}
		const Ref<BulletGrazeZone2D> zone = graze_zones[ev.zone];
		const StringName &signal_name = ev.exit ? names.bullet_graze_exited : names.bullet_grazed;
		bool handled = false;
		// Bubbling: the owner spawner first (while it lives), then the
		// factory, which receives every graze whoever owns the volley.
		Object *spawner = owner_spawner_id != 0 ? ObjectDB::get_instance(ObjectID(owner_spawner_id)) : nullptr;
		if (spawner != nullptr && spawner->has_connections(signal_name)) {
			handled = true;
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
			target = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(ev.target_id)));
			if (target == nullptr || target->is_queued_for_deletion()) {
				continue;
			}
		}
		if (bullet_factory != nullptr && bullet_factory->has_connections(signal_name)) {
			handled = true;
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
	d["state_bytes"] = (int64_t)(graze_state.size() * sizeof(uint16_t));
	d["active_this_tick"] = graze_tick_active;
	d["pending_events"] = (int64_t)graze_events.size();
	d["dispatched_events"] = (int64_t)graze_events_dispatched;
	d["bullet_radius"] = graze_shape_bound_radius2d(cached_effective_shape_type, cached_circle_radius, cached_rect_size, cached_capsule_height) * graze_bullet_scale;
	d["generation"] = (int64_t)graze_generation;
	return d;
}

} // namespace BlastBullets2D
