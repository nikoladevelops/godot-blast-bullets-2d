// GrazeDetector2D: graze target lists with stable slots, rescanned on the
// detector's update interval (factory time), positions refreshed once per
// factory sweep, re-validated when user code ran inside the sweep. See
// graze_detector2d.hpp.

#include "factory/graze_detector2d.hpp"

#include "factory/bullet_factory2d.hpp"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>

#include <algorithm>

using namespace godot;

namespace BlastBullets2D {

int64_t GrazeDetector2D::live_count = 0;
int GrazeTargetList2D::slab_min_targets = GrazeTargetList2D::DEFAULT_SLAB_MIN_TARGETS;
uint64_t GrazeDetector2D::next_list_uid = 0;

GrazeDetector2D::GrazeDetector2D(uint64_t p_spawner_id) :
		spawner_id(p_spawner_id) {
	++live_count;
}

GrazeDetector2D::~GrazeDetector2D() {
	--live_count;
}

void GrazeDetector2D::set_config(const Config &p_config) {
	if (p_config.source != config.source || p_config.node_group != config.node_group) {
		// Other targets altogether: volleys meet new list uids and treat
		// every slot as changed.
		lists.clear();
	}
	if (p_config.target_path != config.target_path) {
		path_target_id = 0;
	}
	if (p_config.children_parent_path != config.children_parent_path) {
		children_parent_id = 0;
	}
	config = p_config;
	mark_due();
}

void GrazeDetector2D::mark_due() {
	for (std::unique_ptr<GrazeTargetList2D> &list : lists) {
		list->scan_due = true;
	}
}

void GrazeDetector2D::clear_lists() {
	lists.clear();
}

bool GrazeDetector2D::shares_factory_lists() const {
	return config.source == SOURCE_NODE_GROUP && config.filter_group.is_empty() && config.update_interval == 0.0;
}

GrazeTargetList2D *GrazeDetector2D::list(BulletFactory2D &factory) {
	return list_for_group(config.node_group, factory);
}

GrazeTargetList2D *GrazeDetector2D::list_for_group(const StringName &group, BulletFactory2D &factory) {
	if (!factory.graze_lists_usable()) {
		return nullptr;
	}
	GrazeDetector2D &shared = factory.get_graze_default_detector();
	if (this != &shared && shares_factory_lists()) {
		return shared.list_for_group(group, factory);
	}
	const bool by_group = config.source == SOURCE_NODE_GROUP;
	if (by_group && group.is_empty()) {
		return nullptr;
	}
	const uint64_t sweep = factory.get_sweep_counter();
	GrazeTargetList2D *list = nullptr;
	if (by_group) {
		// Idle groups go first (never the one asked for), then the lookup.
		for (size_t i = 0; i < lists.size();) {
			if (lists[i]->group != group && lists[i]->last_used_sweep + kIdleSweeps < sweep) {
				lists[i] = std::move(lists.back());
				lists.pop_back();
				continue;
			}
			if (lists[i]->group == group) {
				list = lists[i].get();
			}
			++i;
		}
	} else if (!lists.empty()) {
		list = lists[0].get();
	}
	if (list == nullptr) {
		lists.push_back(std::make_unique<GrazeTargetList2D>());
		list = lists.back().get();
		list->uid = ++next_list_uid;
		list->group = by_group ? group : StringName();
	}
	list->last_used_sweep = sweep;
	update(*list, factory);
	return list;
}

void GrazeDetector2D::collect_now(SceneTree *tree, const World2D *world, const CanvasItem *mouse_space, std::vector<GrazeTarget2D> &r_targets) {
	r_targets.clear();
	scan(config.node_group, tree, world, mouse_space, r_targets);
}

void GrazeDetector2D::scan(const StringName &group, SceneTree *tree, const World2D *world, const CanvasItem *mouse_space, std::vector<GrazeTarget2D> &r_targets) {
	if (tree == nullptr) {
		return;
	}
	++scans;
	if (config.source == SOURCE_MOUSE) {
		if (mouse_space != nullptr && mouse_space->is_inside_tree()) {
			GrazeTarget2D cursor;
			cursor.id = GRAZE_POINT_TARGET_BIT;
			cursor.position = mouse_space->get_global_mouse_position();
			if (cursor.position.is_finite()) {
				r_targets.push_back(cursor);
			}
		}
		return;
	}
	if (config.source == SOURCE_GLOBAL_POSITIONS) {
		// Finite by the spawner's setter.
		const int count = config.global_positions.size();
		r_targets.reserve(r_targets.size() + (size_t)count);
		for (int i = 0; i < count; ++i) {
			GrazeTarget2D point;
			point.id = GRAZE_POINT_TARGET_BIT | (uint64_t)i;
			point.position = config.global_positions[i];
			r_targets.push_back(point);
		}
		return;
	}
	Node *spawner = spawner_id != 0 ? Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(spawner_id))) : nullptr;
	// Paths resolve from the spawner while it is in the tree; out of it (or
	// freed) the nodes they pointed to last keep serving its volleys.
	const bool resolve_paths = spawner != nullptr && spawner->is_inside_tree();
	auto add = [&](Node2D *node) {
		graze_add_target2d(node, world, r_targets);
	};
	// The scans never visit nor enter the spawner (its markers, its preview
	// layers) or a bullet factory (thousands of bullet nodes, none a target).
	auto skip_scan = [spawner](Node *node) { return node == spawner || Object::cast_to<BulletFactory2D>(node) != nullptr; };
	switch (config.source) {
		case SOURCE_NODE_PATH: {
			Node *node = nullptr;
			if (resolve_paths) {
				node = config.target_path.is_empty() ? nullptr : spawner->get_node_or_null(config.target_path);
				path_target_id = node != nullptr ? node->get_instance_id() : 0;
			} else if (path_target_id != 0) {
				node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(path_target_id)));
			}
			// Explicit: the spawner's own children may graze, never the
			// spawner itself (it never grazes its own bullets).
			Node2D *target = Object::cast_to<Node2D>(node);
			if (target != nullptr && target != spawner && (config.filter_group.is_empty() || target->is_in_group(config.filter_group))) {
				add(target);
			}
			break;
		}
		case SOURCE_NODE_CHILDREN: {
			Node *parent = nullptr;
			if (resolve_paths) {
				parent = config.children_parent_path.is_empty() ? nullptr : spawner->get_node_or_null(config.children_parent_path);
				children_parent_id = parent != nullptr ? parent->get_instance_id() : 0;
			} else if (children_parent_id != 0) {
				parent = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(children_parent_id)));
			}
			// A parent inside the spawner or a bullet factory has nothing a
			// scan may find.
			if (parent == nullptr || parent == spawner || (spawner != nullptr && spawner->is_ancestor_of(parent)) || node_in_bullet_factory2d(parent)) {
				break;
			}
			scan_node2d_children(parent, config.children_recursive, config.filter_group, scan_stack, skip_scan, add);
			break;
		}
		case SOURCE_NODE_NAME: {
			if (config.node_name.is_empty()) {
				break;
			}
			// Same root as the homing name scan: current_scene may be null
			// (autoload-only setups), the viewport root always exists.
			Node *root = tree->get_current_scene();
			if (root == nullptr) {
				root = tree->get_root();
			}
			scan_node2ds_by_name(root, config.node_name, config.node_name_match, config.node_name_case_sensitive, config.filter_group, scan_stack, skip_scan, add);
			break;
		}
		case SOURCE_NODE_GROUP:
		default:
			collect_graze_targets2d(tree, group, config.filter_group, world, spawner_id, r_targets);
			break;
	}
}

void GrazeDetector2D::update(GrazeTargetList2D &list, BulletFactory2D &factory) {
	const uint64_t sweep = factory.get_sweep_counter();
	const uint64_t factory_id = factory.get_instance_id();
	const uint64_t epoch = factory.get_user_code_epoch();
	const bool fresh = list.fresh_sweep == sweep && list.fresh_factory_id == factory_id;
	std::vector<GrazeTarget2D> &live = live_scratch;
	live.clear();
	if (fresh && !list.scan_due) {
		if (list.validated_epoch == epoch) {
			return;
		}
		// User code ran since this sweep's update (an earlier volley's
		// handler): a target it freed or queued for deletion drops out now,
		// so later volleys of the sweep never test it. Positions stay this
		// sweep's.
		list.validated_epoch = epoch;
		for (const GrazeTarget2D &target : list.targets) {
			if (graze_is_point_target2d(target.id)) {
				live.push_back(target); // points never die (an edit marks the list due)
				continue;
			}
			const Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(target.id)));
			if (node != nullptr && !node->is_queued_for_deletion()) {
				live.push_back(target);
			}
		}
		if (live.size() != list.targets.size()) {
			apply(list, live);
		}
		return;
	}
	const Ref<World2D> world = factory.get_world_2d();
	const double now = factory.get_graze_clock();
	// Points are read every sweep (the cursor moves, an array edit is live).
	if (uses_points() || list.scan_due || list.fresh_factory_id != factory_id || now >= list.next_scan_time) {
		list.scan_due = false;
		list.next_scan_time = now + config.update_interval;
		scan(list.group, factory.get_tree(), world.ptr(), &factory, live);
		list.members.resize(live.size());
		for (size_t k = 0; k < live.size(); ++k) {
			list.members[k] = live[k].id;
		}
	} else {
		// Between rescans: what the last scan found, positions read now.
		size_t kept = 0;
		for (size_t k = 0; k < list.members.size(); ++k) {
			const uint64_t id = list.members[k];
			Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(id)));
			if (node == nullptr || node->is_queued_for_deletion()) {
				continue; // gone for good: the next rescan may find others
			}
			list.members[kept++] = id;
			GrazeTarget2D target;
			if (target_node_present2d(node, world.ptr(), target.position)) {
				target.id = id;
				live.push_back(target);
			}
		}
		list.members.resize(kept);
	}
	apply(list, live);
	list.fresh_sweep = sweep;
	list.fresh_factory_id = factory_id;
	list.validated_epoch = epoch;
}

void GrazeDetector2D::apply(GrazeTargetList2D &list, const std::vector<GrazeTarget2D> &live) {
	// Same targets in the same order as last time (the common case): only
	// the positions moved.
	bool same = live.size() == list.targets.size();
	for (size_t k = 0; same && k < live.size(); ++k) {
		same = list.targets[k].id == live[k].id;
	}
	if (same) {
		for (size_t k = 0; k < live.size(); ++k) {
			list.targets[k].position = live[k].position;
		}
		build_view(list, true);
		return;
	}
	// A remaining target keeps its slot, a new one takes a free slot (or a
	// new one), a target that is no longer there frees its slot.
	std::vector<uint32_t> &changed = changed_scratch;
	changed.clear();
	++list.stamp;
	list.slots.resize(live.size());
	for (size_t k = 0; k < live.size(); ++k) {
		const uint64_t id = live[k].id;
		uint32_t slot;
		const auto found = list.slot_of.find(id);
		if (found != list.slot_of.end()) {
			slot = found->second;
		} else {
			if (!list.free_slots.empty()) {
				slot = list.free_slots.back();
				list.free_slots.pop_back();
			} else {
				slot = (uint32_t)list.slot_ids.size();
				list.slot_ids.push_back(0);
				list.slot_changed.push_back(0);
				list.slot_stamp.push_back(0);
			}
			list.slot_ids[slot] = id;
			list.slot_of[id] = slot;
			changed.push_back(slot);
		}
		list.slot_stamp[slot] = list.stamp;
		list.slots[k] = slot;
	}
	for (uint32_t s = 0; s < (uint32_t)list.slot_ids.size(); ++s) {
		if (list.slot_ids[s] != 0 && list.slot_stamp[s] != list.stamp) {
			list.slot_of.erase(list.slot_ids[s]);
			list.slot_ids[s] = 0;
			list.free_slots.push_back(s);
			changed.push_back(s);
		}
	}
	list.targets = live;
	if (!changed.empty()) {
		++list.serial;
		for (const uint32_t s : changed) {
			list.slot_changed[s] = list.serial;
		}
	}
	build_view(list, false);
}

void GrazeDetector2D::build_view(GrazeTargetList2D &list, bool same_targets) {
	const size_t n = list.targets.size();
	list.view_centers.resize(n);
	list.view_slots.resize(n);
	list.view_order.resize(n);
	if ((int)n <= GrazeTargetList2D::slab_min_targets) {
		list.by_x_valid = false;
		for (size_t k = 0; k < n; ++k) {
			list.view_centers[k] = list.targets[k].position;
			list.view_slots[k] = list.slots[k];
			list.view_order[k] = (uint32_t)k;
		}
		return;
	}
	// Sorted by x, equal x by tree index (ties stay with tree order).
	const GrazeTarget2D *targets = list.targets.data();
	auto before = [targets](uint32_t a, uint32_t b) {
		const real_t ax = targets[a].position.x;
		const real_t bx = targets[b].position.x;
		return ax < bx || (ax == bx && a < b);
	};
	if (!same_targets || !list.by_x_valid || list.by_x.size() != n) {
		list.by_x.resize(n);
		for (size_t k = 0; k < n; ++k) {
			list.by_x[k] = (uint32_t)k;
		}
		std::sort(list.by_x.begin(), list.by_x.end(), before);
	} else {
		// Same targets, moved a little since last tick: the last order is
		// nearly sorted, so insertion sort is about linear.
		for (size_t i = 1; i < n; ++i) {
			const uint32_t index = list.by_x[i];
			size_t at = i;
			while (at > 0 && before(index, list.by_x[at - 1])) {
				list.by_x[at] = list.by_x[at - 1];
				--at;
			}
			list.by_x[at] = index;
		}
	}
	list.by_x_valid = true;
	for (size_t i = 0; i < n; ++i) {
		const uint32_t k = list.by_x[i];
		list.view_centers[i] = list.targets[k].position;
		list.view_slots[i] = list.slots[k];
		list.view_order[i] = k;
	}
}

} // namespace BlastBullets2D
