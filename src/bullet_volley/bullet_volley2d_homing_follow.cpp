// BulletVolley2D (same class): homing by a BulletTargets2D (homing_follow).
// The handle's detector is shared (graze_detector2d.hpp); the volley picks
// from its target list on its own clock and writes the picks through the
// ordinary queue API, so steering, reach, auto pop and orbit locks work as
// for any pushed target.

#include "bullet_volley/bullet_volley2d_internal.hpp"

#include "core/graze_targets2d.hpp"
#include "core/live_object2d.hpp"
#include "targets/bullet_targets2d.hpp"

#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

bool BulletVolley2D::homing_follow(const Variant &targets, double retarget_interval, HomingFollowMode mode) {
	if (reject_pooled_handle("homing_follow")) {
		return false;
	}
	if (!Math::is_finite(retarget_interval) || retarget_interval < 0.0) {
		UtilityFunctions::push_error("homing_follow: retarget_interval must be finite and >= 0 (0 picks every tick), nothing changed.");
		return false;
	}
	if (mode != HOMING_FOLLOW_SHARED_NEAREST && mode != HOMING_FOLLOW_EACH_NEAREST) {
		UtilityFunctions::push_error("homing_follow: mode must be 0 (Shared Nearest) or 1 (Each Nearest), nothing changed.");
		return false;
	}
	if (targets.get_type() == Variant::NIL) {
		follow_detector.reset();
		return true;
	}
	const BulletTargets2D *handle = live_object2d<BulletTargets2D>(targets);
	if (handle == nullptr) {
		UtilityFunctions::push_error("homing_follow: targets must be a BulletTargets2D or null, nothing changed.");
		return false;
	}
	follow_detector = handle->get_detector();
	follow_interval = retarget_interval;
	follow_time_left = 0.0; // the next tick picks
	follow_mode = (int)mode;
	return true;
}

// The target at slot k of the list, as a homing target (a point: its
// position; a node: the node, live by the list's per-sweep validation).
static Variant follow_target_of2d(const GrazeTargetList2D &list, int k) {
	const GrazeTarget2D &found = list.targets[k];
	if (graze_is_point_target2d(found.id)) {
		return Variant(found.position);
	}
	return Variant(ObjectDB::get_instance(ObjectID(found.id)));
}

static int follow_nearest2d(const GrazeTargetList2D *list, const Vector2 &from) {
	int best = -1;
	real_t best_d = Math::INF;
	for (int k = 0; list != nullptr && k < list->count(); ++k) {
		const Vector2 &p = list->targets[k].position;
		const real_t dx = p.x - from.x;
		const real_t dy = p.y - from.y;
		const real_t d = dx * dx + dy * dy;
		// The first candidate always beats none (an overflowed INF distance too).
		if (best < 0 || d < best_d) {
			best = k;
			best_d = d;
		}
	}
	return best;
}

void BulletVolley2D::homing_follow_bullet(int bullet_index, const Variant &target, bool mouse) {
	if (target.get_type() == Variant::NIL) {
		if (bullet_check_has_homing_targets(bullet_index)) {
			bullet_clear_homing_targets(bullet_index);
		}
		return;
	}
	if (mouse) {
		if (bullet_homing_check_current_target_type(bullet_index) != MousePositionTarget) {
			all_bullets_replace_homing_targets_with_mouse(bullet_index, bullet_index);
		}
		return;
	}
	if (bullet_get_current_homing_target(bullet_index) != target) {
		bullet_replace_homing_targets_with_new_target(bullet_index, target);
	}
}

void BulletVolley2D::homing_follow_pick(double delta) {
	follow_time_left -= delta;
	if (follow_time_left > 0.0 || bullet_factory == nullptr || !bullet_factory->graze_lists_usable() || active_bullets_counter <= 0) {
		return;
	}
	follow_time_left = follow_interval;
	// No user code here: the list is re-validated plain data, and the queue
	// writes below read nodes through their ids.
	const GrazeTargetList2D *list = follow_detector->list(*bullet_factory);
	const bool mouse = follow_detector->get_config().source == GrazeDetector2D::SOURCE_MOUSE;
	const int n = MIN(amount_bullets, (int)all_cached_instance_transforms.size());
	if (follow_mode == HOMING_FOLLOW_EACH_NEAREST) {
		for (int i = 0; i < n; ++i) {
			if (!is_bullet_status_enabled(i)) {
				continue; // a frozen slot keeps its queue (pushes would inflate its counters)
			}
			const int best = follow_nearest2d(list, all_cached_instance_transforms[i].get_origin());
			homing_follow_bullet(i, best >= 0 ? follow_target_of2d(*list, best) : Variant(), mouse);
		}
		return;
	}
	// Shared: the target nearest the live bullets' center.
	Vector2 sum;
	int live = 0;
	for (int i = 0; i < n; ++i) {
		if (is_bullet_status_enabled(i)) {
			const Vector2 p = all_cached_instance_transforms[i].get_origin();
			sum.x += p.x;
			sum.y += p.y;
			++live;
		}
	}
	const Vector2 center = live > 0 ? sum / (real_t)live : Vector2();
	const int best = center.is_finite() ? follow_nearest2d(list, center) : (list != nullptr && list->count() > 0 ? 0 : -1);
	if (best < 0) {
		if (shared_homing_deque_check_has_homing_targets()) {
			shared_homing_deque_clear_homing_targets();
		}
		return;
	}
	if (mouse) {
		if (shared_homing_deque_check_current_target_type() != MousePositionTarget) {
			shared_homing_deque_clear_homing_targets();
			shared_homing_deque_push_back_mouse_position_target();
		}
		return;
	}
	const Variant target = follow_target_of2d(*list, best);
	if (shared_homing_deque_get_current_homing_target() != target) {
		shared_homing_deque_replace_homing_targets_with_new_target(target);
	}
}

} // namespace BlastBullets2D
