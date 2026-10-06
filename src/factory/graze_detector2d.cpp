// GrazeDetector2D: graze target lists with stable slots, rescanned on the
// detector's update interval (factory time), positions refreshed once per
// factory sweep. See graze_detector2d.hpp.

#include "factory/graze_detector2d.hpp"

#include "core/warn_once2d.hpp"
#include "factory/bullet_factory2d.hpp"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>

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
	if (p_config.source != config.source) {
		// Group lists and the single list do not map onto each other:
		// volleys meet new list uids and treat every slot as changed.
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
	return config.source == SOURCE_ZONE_GROUPS && config.filter_group.is_empty() && config.update_interval == 0.0;
}

GrazeTargetList2D *GrazeDetector2D::list_for(const BulletGrazeZone2D &zone, BulletFactory2D &factory) {
	if (!zone.enabled) {
		return nullptr;
	}
	return list_for_group(zone.target_group, factory);
}

GrazeTargetList2D *GrazeDetector2D::list_for_group(const StringName &group, BulletFactory2D &factory) {
	if (!factory.graze_lists_usable()) {
		return nullptr;
	}
	GrazeDetector2D &shared = factory.get_graze_default_detector();
	if (this != &shared && shares_factory_lists()) {
		return shared.list_for_group(group, factory);
	}
	const bool by_group = config.source == SOURCE_ZONE_GROUPS;
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

int GrazeDetector2D::collect_now(const BulletGrazeZone2D &zone, SceneTree *tree, const World2D *world, GrazeTarget2D *r_targets, int &r_count) {
	r_count = 0;
	if (!zone.enabled) {
		return 0;
	}
	return scan(zone.target_group, tree, world, r_targets, r_count);
}

int GrazeDetector2D::scan(const StringName &group, SceneTree *tree, const World2D *world, GrazeTarget2D *r_targets, int &r_count) {
	r_count = 0;
	if (tree == nullptr) {
		return 0;
	}
	++scans;
	Node *spawner = spawner_id != 0 ? Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(spawner_id))) : nullptr;
	// Paths resolve from the spawner while it is in the tree; out of it (or
	// freed) the nodes they pointed to last keep serving its volleys.
	const bool resolve_paths = spawner != nullptr && spawner->is_inside_tree();
	int total = 0;
	auto add = [&](Node2D *node) {
		graze_add_target2d(node, world, r_targets, r_count, total);
	};
	switch (config.source) {
		case SOURCE_NODE_PATH: {
			Node *node = nullptr;
			if (resolve_paths) {
				node = config.target_path.is_empty() ? nullptr : spawner->get_node_or_null(config.target_path);
				path_target_id = node != nullptr ? node->get_instance_id() : 0;
			} else if (path_target_id != 0) {
				node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(path_target_id)));
			}
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
			scan_node2d_children(
					parent, config.children_recursive, spawner, config.filter_group, PREVIEW_META_KEY, scan_stack,
					[](Node *node) { return Object::cast_to<BulletFactory2D>(node) != nullptr; },
					add);
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
			scan_node2ds_by_name(
					root, config.node_name, config.node_name_match, config.node_name_case_sensitive, config.filter_group, PREVIEW_META_KEY, scan_stack,
					[spawner](Node *node) { return node == spawner || Object::cast_to<BulletFactory2D>(node) != nullptr; },
					add);
			break;
		}
		case SOURCE_ZONE_GROUPS:
		default:
			total = collect_graze_targets2d(tree, group, config.filter_group, world, spawner_id, r_targets, r_count);
			break;
	}
	return total;
}

void GrazeDetector2D::update(GrazeTargetList2D &list, BulletFactory2D &factory) {
	const uint64_t sweep = factory.get_sweep_counter();
	const uint64_t factory_id = factory.get_instance_id();
	if (!list.scan_due && list.fresh_sweep == sweep && list.fresh_factory_id == factory_id) {
		return;
	}
	const Ref<World2D> world = factory.get_world_2d();
	const double now = factory.get_graze_clock();
	GrazeTarget2D live[GrazeTargetList2D::CAP];
	int live_count = 0;
	if (list.scan_due || list.fresh_factory_id != factory_id || now >= list.next_scan_time) {
		list.scan_due = false;
		list.next_scan_time = now + config.update_interval;
		const int total = scan(list.group, factory.get_tree(), world.ptr(), live, live_count);
		warn_overflow(list, total, factory_id);
		list.member_count = live_count;
		for (int k = 0; k < live_count; ++k) {
			list.members[k] = live[k].id;
		}
	} else {
		// Between rescans: what the last scan found, positions read now.
		int kept = 0;
		for (int k = 0; k < list.member_count; ++k) {
			const uint64_t id = list.members[k];
			Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(id)));
			if (node == nullptr || node->is_queued_for_deletion()) {
				continue; // gone for good: the next rescan may find others
			}
			list.members[kept++] = id;
			Vector2 position;
			if (graze_target_usable2d(node, world.ptr(), position)) {
				live[live_count].id = id;
				live[live_count].position = position;
				++live_count;
			}
		}
		list.member_count = kept;
	}
	const bool same_targets = live_count == list.count && [&]() {
		for (int k = 0; k < live_count; ++k) {
			if (list.targets[k].id != live[k].id) {
				return false;
			}
		}
		return true;
	}();
	assign_slots(list, live, live_count);
	sort_by_x(list, same_targets);
	list.fresh_sweep = sweep;
	list.fresh_factory_id = factory_id;
}

void GrazeDetector2D::sort_by_x(GrazeTargetList2D &list, bool same_targets) {
	if (list.count <= GrazeTargetList2D::slab_min_targets) {
		list.by_x_valid = false;
		return;
	}
	if (!same_targets || !list.by_x_valid) {
		for (int k = 0; k < list.count; ++k) {
			list.by_x[k] = (uint8_t)k;
		}
	}
	// Insertion sort: targets move little between ticks, so the last
	// order is nearly sorted (linear in the common case).
	for (int i = 1; i < list.count; ++i) {
		const uint8_t index = list.by_x[i];
		const real_t x = list.targets[index].position.x;
		int at = i;
		while (at > 0) {
			const uint8_t before = list.by_x[at - 1];
			const real_t bx = list.targets[before].position.x;
			if (bx < x || (bx == x && before < index)) {
				break;
			}
			list.by_x[at] = before;
			--at;
		}
		list.by_x[at] = index;
	}
	list.by_x_valid = true;
}

void GrazeDetector2D::warn_overflow(const GrazeTargetList2D &list, int total, uint64_t factory_id) const {
	if (total <= GrazeTargetList2D::CAP) {
		return;
	}
	const uint64_t owner = spawner_id != 0 ? spawner_id : factory_id;
	const String tail = " graze targets; only the first " + String::num_int64(GrazeTargetList2D::CAP) + " in tree order are tested.";
	switch (config.source) {
		case SOURCE_NODE_NAME:
			WarnOnce2D::warn(owner, 105u, (int64_t)config.node_name.hash(), 0, "BulletSpawner2D: graze_node_name '" + config.node_name + "' matches " + String::num_int64(total) + tail);
			break;
		case SOURCE_NODE_CHILDREN:
			WarnOnce2D::warn(owner, 106u, 0, 0, "BulletSpawner2D: graze_children_parent_path holds " + String::num_int64(total) + tail);
			break;
		default:
			WarnOnce2D::warn(owner, 24u, (int64_t)String(list.group).hash(), 0, "BulletGrazeZone2D: group '" + String(list.group) + "' holds " + String::num_int64(total) + tail);
			break;
	}
}

void GrazeDetector2D::assign_slots(GrazeTargetList2D &list, const GrazeTarget2D *live, int live_count) {
	// Same targets in the same order as last time (the common case): only
	// the positions moved.
	bool same = live_count == list.count;
	for (int k = 0; same && k < live_count; ++k) {
		same = list.targets[k].id == live[k].id;
	}
	if (same) {
		for (int k = 0; k < live_count; ++k) {
			list.targets[k].position = live[k].position;
		}
		return;
	}
	uint64_t changed = 0;
	// A target that is no longer there frees its slot...
	for (int s = 0; s < GrazeTargetList2D::CAP; ++s) {
		if (list.slot_ids[s] == 0) {
			continue;
		}
		bool kept = false;
		for (int k = 0; k < live_count && !kept; ++k) {
			kept = live[k].id == list.slot_ids[s];
		}
		if (!kept) {
			list.slot_ids[s] = 0;
			changed |= 1ull << s;
		}
	}
	// ...a remaining one keeps its slot, a new one takes the lowest free.
	for (int k = 0; k < live_count; ++k) {
		int slot = -1;
		for (int s = 0; s < GrazeTargetList2D::CAP && slot < 0; ++s) {
			if (list.slot_ids[s] == live[k].id) {
				slot = s;
			}
		}
		for (int s = 0; s < GrazeTargetList2D::CAP && slot < 0; ++s) {
			if (list.slot_ids[s] == 0) {
				slot = s;
				list.slot_ids[s] = live[k].id;
				changed |= 1ull << s;
			}
		}
		// Always found: at most CAP live targets, as many slots.
		list.targets[k] = live[k];
		list.slots[k] = (uint8_t)slot;
	}
	list.count = live_count;
	if (changed != 0) {
		++list.serial;
		for (int s = 0; s < GrazeTargetList2D::CAP; ++s) {
			if ((changed & (1ull << s)) != 0) {
				list.slot_changed[s] = list.serial;
			}
		}
	}
}

} // namespace BlastBullets2D
