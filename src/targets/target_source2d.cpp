#include "targets/target_source2d.hpp"

#include "core/graze_targets2d.hpp"
#include "core/node_scan2d.hpp"
#include "core/transform_math2d.hpp"
#include "factory/bullet_factory2d.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace BlastBullets2D {

const std::shared_ptr<GrazeDetector2D> &TargetSource2D::shared_detector(uint64_t owner_id, const GrazeDetector2D::Config &config_now) const {
	if (detector_ptr == nullptr) {
		// Never std::make_shared: its type tag is a GNU unique symbol, which
		// pins the library in memory (dlclose keeps it, its statics outlive
		// Godot's StringName table).
		detector_ptr = std::shared_ptr<GrazeDetector2D>(new GrazeDetector2D(owner_id));
		detector_ptr->set_config(config_now);
	}
	return detector_ptr;
}

GrazeDetector2D &TargetSource2D::detector(uint64_t owner_id, const GrazeDetector2D::Config &config_now) const {
	return *shared_detector(owner_id, config_now);
}

void TargetSource2D::push(const GrazeDetector2D::Config &config_now) const {
	if (detector_ptr != nullptr) {
		detector_ptr->set_config(config_now);
	}
}

void TargetSource2D::collect(const Node2D &owner, BulletFactory2D *factory, const GrazeDetector2D::Config &config_now, std::vector<GrazeTarget2D> &r_targets) const {
	r_targets.clear();
	if (!owner.is_inside_tree()) {
		return;
	}
	GrazeDetector2D &det = detector(owner.get_instance_id(), config_now);
	const uint64_t self_id = owner.get_instance_id();
	const bool editor = Engine::get_singleton()->is_editor_hint();
	if (factory != nullptr && factory->graze_lists_usable() && !editor) {
		// What the volleys test this sweep (shared lists may hold the owner;
		// its own volleys skip it, so does this view). A target freed since
		// the sweep (idle-time user code) is left out too.
		const GrazeTargetList2D *list = det.list(*factory);
		for (int k = 0; list != nullptr && k < list->count(); ++k) {
			const GrazeTarget2D &target = list->targets[k];
			if (graze_is_point_target2d(target.id)) {
				r_targets.push_back(target);
				continue;
			}
			const Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(target.id)));
			if (target.id != self_id && node != nullptr && !node->is_queued_for_deletion()) {
				r_targets.push_back(target);
			}
		}
		return;
	}
	// No running factory (editor, factory not ready): a fresh scan in the
	// same space as the runtime, the factory's world when there is one. The
	// cursor only at runtime (in the editor it sits over the editor's UI).
	const bool factory_in_tree = factory != nullptr && factory->is_inside_tree();
	const Ref<World2D> world = factory_in_tree ? factory->get_world_2d() : owner.get_world_2d();
	const CanvasItem *mouse_space = editor ? nullptr : (factory_in_tree ? (const CanvasItem *)factory : (const CanvasItem *)&owner);
	det.collect_now(owner.get_tree(), world.ptr(), mouse_space, r_targets);
}

void TargetSource2D::mark_due(const Node2D &owner, BulletFactory2D *factory, const GrazeDetector2D::Config &config_now) const {
	GrazeDetector2D &det = detector(owner.get_instance_id(), config_now);
	det.mark_due();
	if (det.shares_factory_lists() && factory != nullptr) {
		// Shared lists rescan every tick anyway; due now so a count right
		// after (and the next tick) see the scene as it is.
		factory->get_graze_default_detector().mark_due();
	}
}

bool TargetSource2D::check_positions(const PackedVector2Array &value, const String &cls, const char *property) {
	for (int i = 0; i < value.size(); ++i) {
		if (!value[i].is_finite()) {
			UtilityFunctions::push_error(cls + String(": ") + property + " must hold finite positions only, keeping the old value.");
			return false;
		}
	}
	return true;
}

bool TargetSource2D::check_update_interval(double value, const String &cls, const char *property) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error(cls + String(": ") + property + " must be finite and >= 0 (0 scans every tick), keeping the old value.");
		return false;
	}
	return true;
}

bool TargetSource2D::check_name_match(int value, const String &cls, const char *property) {
	if (value < NODE_NAME_MATCH_EXACT || value > NODE_NAME_MATCH_ENDS_WITH) {
		UtilityFunctions::push_error(cls + String(": invalid ") + property + ", keeping the old value.");
		return false;
	}
	return true;
}

} // namespace BlastBullets2D
