#pragma once

#include "core/cached_string_names2d.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace BlastBullets2D {
using namespace godot;

// Node scans and THE target filter shared by the spawner's target sources
// (homing and graze): one walk, one set of exclusions, so both features find
// the same nodes.

// Metadata tag of the spawner's preview nodes (pattern preview holder,
// graze ring layer). Children-mode pattern markers and every target scan
// skip anything carrying it, so a preview never becomes a spawn marker or
// a target.
static constexpr const char *PREVIEW_META_KEY = "blastbullets_pattern_preview";

// `node` is a BulletFactory2D inside the tree or sits under one (its
// containers, bullets, attachments, effect shards). Implemented in
// bullet_factory2d.cpp: one is_ancestor_of per factory in the tree, no
// ancestor walk.
bool node_in_bullet_factory2d(Node *node);

// A target that is still there: inside the tree, not queued for deletion,
// in `world` when given (coordinates of different worlds never mix) and at
// a finite global position (returned in r_position). Cheap enough for
// every tick (graze reads its targets' positions with it between scans).
inline bool target_node_present2d(Node2D *node, const World2D *world, Vector2 &r_position) {
	if (node == nullptr || !node->is_inside_tree() || node->is_queued_for_deletion()) {
		return false;
	}
	if (world != nullptr) {
		const Ref<World2D> node_world = node->get_world_2d();
		if (node_world.ptr() != world) {
			return false;
		}
	}
	r_position = node->get_global_position();
	return r_position.is_finite();
}

// THE target filter of every node source of graze and homing: present
// (above), never a preview layer (PREVIEW_META_KEY) and never a
// BulletFactory2D or anything inside one. Who else is left out (the
// spawner itself, its subtree) is each source's rule on top of this.
inline bool target_node_usable2d(Node2D *node, const World2D *world, Vector2 &r_position) {
	return target_node_present2d(node, world, r_position) && !node->has_meta(CachedStringNames2D::get().preview_meta) && !node_in_bullet_factory2d(node);
}

// How a node name is compared with a pattern. The ids are
// BulletSpawner2D::HomingNodeNameMatch's (serialized by
// homing_node_name_match_mode and graze_node_name_match_mode).
enum NodeNameMatch2D {
	NODE_NAME_MATCH_EXACT = 0, // "Player" matches only "Player"
	NODE_NAME_MATCH_CONTAINS = 1, // "Player" matches "Player2", "EnemyPlayer", ...
	NODE_NAME_MATCH_STARTS_WITH = 2, // "Player" matches "Player2" but not "EnemyPlayer"
	NODE_NAME_MATCH_ENDS_WITH = 3, // "Player" matches "EnemyPlayer" but not "Player2"
};

// `name` and `pattern` already folded to the same case.
inline bool node_name_matches2d(const String &name, const String &pattern, int mode) {
	switch (mode) {
		case NODE_NAME_MATCH_CONTAINS:
			return name.contains(pattern);
		case NODE_NAME_MATCH_STARTS_WITH:
			return name.begins_with(pattern);
		case NODE_NAME_MATCH_ENDS_WITH:
			return name.ends_with(pattern);
		case NODE_NAME_MATCH_EXACT:
		default:
			return name == pattern;
	}
}

// Node-name scan: walks the tree under `root` depth-first in tree order and
// calls visit(Node2D *) for every Node2D whose name matches `pattern` per
// `mode` (both lowercased unless case_sensitive) and that is in
// filter_group (when not empty). Never visits nor descends into a node
// skip(node) refuses (bullet factories: thousands of bullet nodes, none a
// target; the scanning spawner and its markers). The visitor applies the
// target filter (target_node_usable2d). Explicit stack (`stack`: caller
// scratch, cleared here) instead of recursion: trees can be arbitrarily
// deep. Children are pushed in reverse so they pop in tree order. Internal
// children are never visited.
template <typename SkipFn, typename VisitFn>
inline void scan_node2ds_by_name(Node *root, const String &name_pattern, int mode, bool case_sensitive, const StringName &filter_group, Array &stack, SkipFn skip, VisitFn visit) {
	if (root == nullptr) {
		return;
	}
	stack.clear();
	stack.push_back(root);
	// Pattern folded once per scan, not once per visited node.
	const String pattern = case_sensitive ? name_pattern : name_pattern.to_lower();
	while (!stack.is_empty()) {
		Node *node = Object::cast_to<Node>(stack.pop_back());
		if (node == nullptr || skip(node)) {
			continue;
		}
		Node2D *as_2d = Object::cast_to<Node2D>(node);
		if (as_2d != nullptr) {
			String node_name = String(as_2d->get_name());
			if (!case_sensitive) {
				node_name = node_name.to_lower();
			}
			if (node_name_matches2d(node_name, pattern, mode) && (filter_group.is_empty() || as_2d->is_in_group(filter_group))) {
				visit(as_2d);
			}
		}
		for (int i = node->get_child_count() - 1; i >= 0; --i) {
			stack.push_back(node->get_child(i));
		}
	}
}

// Children scan: calls visit(Node2D *) for every Node2D child of `parent`
// (and their descendants when `recursive`) in filter_group (when not
// empty), depth-first pre-order. A node skip(node) refuses (bullet
// factories, the scanning spawner) is neither visited nor descended into.
// The visitor applies the target filter. Same explicit stack as the name
// scan.
template <typename SkipFn, typename VisitFn>
inline void scan_node2d_children(Node *parent, bool recursive, const StringName &filter_group, Array &stack, SkipFn skip, VisitFn visit) {
	if (parent == nullptr) {
		return;
	}
	stack.clear();
	for (int i = parent->get_child_count() - 1; i >= 0; --i) {
		stack.push_back(parent->get_child(i));
	}
	while (!stack.is_empty()) {
		Node *child = Object::cast_to<Node>(stack.pop_back());
		if (child == nullptr || skip(child)) {
			continue;
		}
		Node2D *as_2d = Object::cast_to<Node2D>(child);
		if (as_2d != nullptr && (filter_group.is_empty() || as_2d->is_in_group(filter_group))) {
			visit(as_2d);
		}
		if (recursive) {
			for (int i = child->get_child_count() - 1; i >= 0; --i) {
				stack.push_back(child->get_child(i));
			}
		}
	}
}

} // namespace BlastBullets2D
