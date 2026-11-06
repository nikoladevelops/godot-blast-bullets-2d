// BulletFactory2D graze targets: the factory's own graze detector (zone
// groups, rescanned every physics step; graze_detector2d.*) and its debug
// readouts. Detection itself runs in the volley tick
// (bullet_volley2d_graze.cpp + step_graze in bullet_volley2d_tick.cpp).

#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletFactory2D::clear_graze_cache() {
	graze_default_detector.clear_lists();
}

Array BulletFactory2D::debug_get_graze_targets(const StringName &group) {
	Array out;
	const GrazeTargetList2D *list = graze_default_detector.list_for_group(group, *this);
	for (int i = 0; list != nullptr && i < list->count(); ++i) {
		Dictionary d;
		d["id"] = (int64_t)list->targets[i].id;
		d["position"] = list->targets[i].position;
		out.push_back(d);
	}
	return out;
}

Dictionary BulletFactory2D::debug_get_graze_stats() const {
	Dictionary d;
	d["refreshes"] = (int64_t)graze_default_detector.scans;
	d["events_total"] = (int64_t)stats_graze_events_total;
	d["cached_groups"] = (int64_t)graze_default_detector.get_list_count();
	d["live_detectors"] = (int64_t)GrazeDetector2D::live_count;
	return d;
}

int BulletFactory2D::debug_set_graze_slab_min_targets(int value) {
	const int previous = GrazeTargetList2D::slab_min_targets;
	if (value < 0) {
		UtilityFunctions::push_error("BulletFactory2D::debug_set_graze_slab_min_targets: value must be >= 0, nothing changed.");
		return previous;
	}
	GrazeTargetList2D::slab_min_targets = value;
	return previous;
}

} // namespace BlastBullets2D
