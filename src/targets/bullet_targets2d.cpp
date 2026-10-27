#include "targets/bullet_targets2d.hpp"

#include "core/live_object2d.hpp"
#include "factory/bullet_factory2d.hpp"
#include "targets/target_source2d.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

static_assert((int)BulletTargets2D::SOURCE_NODE_GROUP == (int)GrazeDetector2D::SOURCE_NODE_GROUP, "detector source ids");
static_assert((int)BulletTargets2D::SOURCE_NODE == (int)GrazeDetector2D::SOURCE_NODE_PATH, "detector source ids");
static_assert((int)BulletTargets2D::SOURCE_NODE_NAME == (int)GrazeDetector2D::SOURCE_NODE_NAME, "detector source ids");
static_assert((int)BulletTargets2D::SOURCE_NODE_CHILDREN == (int)GrazeDetector2D::SOURCE_NODE_CHILDREN, "detector source ids");
static_assert((int)BulletTargets2D::SOURCE_MOUSE == (int)GrazeDetector2D::SOURCE_MOUSE, "detector source ids");
static_assert((int)BulletTargets2D::SOURCE_POSITIONS == (int)GrazeDetector2D::SOURCE_GLOBAL_POSITIONS, "detector source ids");

namespace {

Ref<BulletTargets2D> refuse(const char *fn, const String &text) {
	UtilityFunctions::push_error(String("BulletTargets2D.") + fn + ": " + text + ", nothing created.");
	return Ref<BulletTargets2D>();
}

} // namespace

BulletTargets2D::BulletTargets2D() :
		detector(new GrazeDetector2D(0)) {
}

Ref<BulletTargets2D> BulletTargets2D::from_group(const StringName &group, const StringName &filter_group) {
	if (group.is_empty()) {
		return refuse("from_group", "group is empty (name the group whose nodes are the targets)");
	}
	Ref<BulletTargets2D> t;
	t.instantiate();
	t->config.source = SOURCE_NODE_GROUP;
	t->config.node_group = group;
	t->config.filter_group = filter_group;
	t->push();
	return t;
}

Ref<BulletTargets2D> BulletTargets2D::from_node(const Variant &node) {
	const Node2D *target = live_object2d<Node2D>(node);
	if (target == nullptr) {
		return refuse("from_node", "node must be a live Node2D");
	}
	Ref<BulletTargets2D> t;
	t.instantiate();
	t->config.source = SOURCE_NODE;
	t->push();
	t->detector->set_fixed_nodes(target->get_instance_id(), 0);
	return t;
}

Ref<BulletTargets2D> BulletTargets2D::from_children(const Variant &parent, bool recursive) {
	const Node *node = live_object2d<Node>(parent);
	if (node == nullptr) {
		return refuse("from_children", "parent must be a live Node");
	}
	Ref<BulletTargets2D> t;
	t.instantiate();
	t->config.source = SOURCE_NODE_CHILDREN;
	t->config.children_recursive = recursive;
	t->push();
	t->detector->set_fixed_nodes(0, node->get_instance_id());
	return t;
}

Ref<BulletTargets2D> BulletTargets2D::from_name(const String &name, int match_mode, bool case_sensitive) {
	if (name.is_empty()) {
		return refuse("from_name", "name is empty");
	}
	if (match_mode < NODE_NAME_MATCH_EXACT || match_mode > NODE_NAME_MATCH_ENDS_WITH) {
		return refuse("from_name", "match_mode must be 0 (Exact), 1 (Contains), 2 (Starts With) or 3 (Ends With)");
	}
	Ref<BulletTargets2D> t;
	t.instantiate();
	t->config.source = SOURCE_NODE_NAME;
	t->config.node_name = name;
	t->config.node_name_match = match_mode;
	t->config.node_name_case_sensitive = case_sensitive;
	t->push();
	return t;
}

Ref<BulletTargets2D> BulletTargets2D::from_positions(const PackedVector2Array &positions) {
	for (int64_t i = 0; i < positions.size(); ++i) {
		if (!positions[i].is_finite()) {
			return refuse("from_positions", "positions must hold finite positions only");
		}
	}
	Ref<BulletTargets2D> t;
	t.instantiate();
	t->config.source = SOURCE_POSITIONS;
	t->config.global_positions = positions;
	t->push();
	return t;
}

Ref<BulletTargets2D> BulletTargets2D::from_mouse() {
	Ref<BulletTargets2D> t;
	t.instantiate();
	t->config.source = SOURCE_MOUSE;
	t->push();
	return t;
}

void BulletTargets2D::set_filter_group(const StringName &value) {
	if (config.filter_group == value) {
		return;
	}
	config.filter_group = value;
	push();
}

void BulletTargets2D::set_update_interval(double value) {
	if (!TargetSource2D::check_update_interval(value, "BulletTargets2D", "update_interval")) {
		return;
	}
	if (config.update_interval == value) {
		return;
	}
	config.update_interval = value;
	push();
}

void BulletTargets2D::set_positions(const PackedVector2Array &value) {
	if (!TargetSource2D::check_positions(value, "BulletTargets2D", "positions")) {
		return;
	}
	config.global_positions = value;
	push();
}

Array BulletTargets2D::resolve(const Variant &factory) const {
	Array out;
	BulletFactory2D *f = live_object2d<BulletFactory2D>(factory);
	if (f == nullptr) {
		UtilityFunctions::push_error("BulletTargets2D.resolve: factory must be a live BulletFactory2D, nothing resolved.");
		return out;
	}
	std::vector<GrazeTarget2D> found;
	if (f->graze_lists_usable() && !Engine::get_singleton()->is_editor_hint()) {
		// What the factory's bullets test this sweep.
		const GrazeTargetList2D *list = detector->list(*f);
		for (int k = 0; list != nullptr && k < list->count(); ++k) {
			found.push_back(list->targets[k]);
		}
	} else if (f->is_inside_tree()) {
		detector->collect_now(f->get_tree(), f->get_world_2d().ptr(), Engine::get_singleton()->is_editor_hint() ? nullptr : f, found);
	}
	for (const GrazeTarget2D &target : found) {
		if (graze_is_point_target2d(target.id)) {
			out.push_back(target.position);
			continue;
		}
		Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(target.id)));
		if (node != nullptr && !node->is_queued_for_deletion()) {
			out.push_back(node);
		}
	}
	return out;
}

void BulletTargets2D::refresh() {
	detector->mark_due();
}

bool read_targets_argument2d(const Variant &value, const char *fn, const char *param, StringName &r_group, std::shared_ptr<GrazeDetector2D> &r_detector) {
	r_group = StringName();
	r_detector.reset();
	const Variant::Type type = value.get_type();
	if (type == Variant::NIL) {
		return true;
	}
	if (type == Variant::STRING || type == Variant::STRING_NAME) {
		r_group = value;
		return true;
	}
	const BulletTargets2D *targets = live_object2d<BulletTargets2D>(value);
	if (targets == nullptr) {
		UtilityFunctions::push_error(String(fn) + ": " + param + " must be a group name or a BulletTargets2D, nothing changed.");
		return false;
	}
	r_detector = targets->get_detector();
	return true;
}

void BulletTargets2D::_bind_methods() {
	ClassDB::bind_static_method("BulletTargets2D", D_METHOD("from_group", "group", "filter_group"), &BulletTargets2D::from_group, DEFVAL(StringName()));
	ClassDB::bind_static_method("BulletTargets2D", D_METHOD("from_node", "node"), &BulletTargets2D::from_node);
	ClassDB::bind_static_method("BulletTargets2D", D_METHOD("from_children", "parent", "recursive"), &BulletTargets2D::from_children, DEFVAL(false));
	ClassDB::bind_static_method("BulletTargets2D", D_METHOD("from_name", "name", "match_mode", "case_sensitive"), &BulletTargets2D::from_name, DEFVAL((int)NODE_NAME_MATCH_CONTAINS), DEFVAL(false));
	ClassDB::bind_static_method("BulletTargets2D", D_METHOD("from_positions", "positions"), &BulletTargets2D::from_positions);
	ClassDB::bind_static_method("BulletTargets2D", D_METHOD("from_mouse"), &BulletTargets2D::from_mouse);
	ClassDB::bind_method(D_METHOD("get_source"), &BulletTargets2D::get_source);
	ClassDB::bind_method(D_METHOD("get_filter_group"), &BulletTargets2D::get_filter_group);
	ClassDB::bind_method(D_METHOD("set_filter_group", "value"), &BulletTargets2D::set_filter_group);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "filter_group"), "set_filter_group", "get_filter_group");
	ClassDB::bind_method(D_METHOD("get_update_interval"), &BulletTargets2D::get_update_interval);
	ClassDB::bind_method(D_METHOD("set_update_interval", "value"), &BulletTargets2D::set_update_interval);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "update_interval", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_update_interval", "get_update_interval");
	ClassDB::bind_method(D_METHOD("get_positions"), &BulletTargets2D::get_positions);
	ClassDB::bind_method(D_METHOD("set_positions", "value"), &BulletTargets2D::set_positions);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "positions"), "set_positions", "get_positions");
	ClassDB::bind_method(D_METHOD("resolve", "factory"), &BulletTargets2D::resolve);
	ClassDB::bind_method(D_METHOD("refresh"), &BulletTargets2D::refresh);
	BIND_ENUM_CONSTANT(SOURCE_NODE_GROUP);
	BIND_ENUM_CONSTANT(SOURCE_NODE);
	BIND_ENUM_CONSTANT(SOURCE_NODE_NAME);
	BIND_ENUM_CONSTANT(SOURCE_NODE_CHILDREN);
	BIND_ENUM_CONSTANT(SOURCE_MOUSE);
	BIND_ENUM_CONSTANT(SOURCE_POSITIONS);
}

} // namespace BlastBullets2D
