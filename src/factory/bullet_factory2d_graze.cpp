// BulletFactory2D graze target cache: the graze targets of each group a
// volley asks for, resolved at most once per physics step (ids + positions,
// shared filter in core/graze_targets2d.hpp). Detection itself runs in the
// volley tick (bullet_volley2d_graze.cpp + step_graze in bullet_volley2d_tick.cpp).

#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

int BulletFactory2D::graze_targets_for(const StringName &group, GrazeTarget2D *r_targets) {
	if (group.is_empty() || !is_ready || is_tearing_down || !is_inside_tree()) {
		return 0;
	}
	// Idle groups go first (never the one asked for), then the lookup.
	for (size_t i = 0; i < graze_groups.size();) {
		if (graze_groups[i].group != group && graze_groups[i].last_used_sweep + kGrazeGroupIdleSweeps < sweep_counter) {
			graze_groups[i] = graze_groups.back();
			graze_groups.pop_back();
			continue;
		}
		++i;
	}
	GrazeGroupCache2D *entry = nullptr;
	for (GrazeGroupCache2D &candidate : graze_groups) {
		if (candidate.group == group) {
			entry = &candidate;
			break;
		}
	}
	if (entry == nullptr) {
		graze_groups.emplace_back();
		entry = &graze_groups.back();
		entry->group = group;
	}
	entry->last_used_sweep = sweep_counter;
	if (!entry->refreshed || entry->sweep != sweep_counter) {
		entry->refreshed = true;
		entry->sweep = sweep_counter;
		const Ref<World2D> world = get_world_2d();
		const int total = collect_graze_targets2d(get_tree(), group, world.ptr(), 0, entry->targets, entry->count);
		++stats_graze_refreshes;
		if (total > BulletGrazeZone2D::MAX_TARGETS) {
			WarnOnce2D::warn(get_instance_id(), 24u, (int64_t)String(group).hash(), 0, "BulletGrazeZone2D: group '" + String(group) + "' holds " + String::num_int64(total) + " graze targets; only the first 4 in tree order are tested.");
		}
	}
	for (int i = 0; i < entry->count; ++i) {
		r_targets[i] = entry->targets[i];
	}
	return entry->count;
}

void BulletFactory2D::clear_graze_cache() {
	graze_groups.clear();
}

Array BulletFactory2D::debug_get_graze_targets(const StringName &group) {
	GrazeTarget2D targets[BulletGrazeZone2D::MAX_TARGETS];
	const int count = graze_targets_for(group, targets);
	Array out;
	for (int i = 0; i < count; ++i) {
		Dictionary d;
		d["id"] = (int64_t)targets[i].id;
		d["position"] = targets[i].position;
		out.push_back(d);
	}
	return out;
}

Dictionary BulletFactory2D::debug_get_graze_stats() const {
	Dictionary d;
	d["refreshes"] = (int64_t)stats_graze_refreshes;
	d["events_total"] = (int64_t)stats_graze_events_total;
	d["cached_groups"] = (int64_t)graze_groups.size();
	return d;
}

} // namespace BlastBullets2D
