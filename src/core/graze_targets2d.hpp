#pragma once

#include "data/bullet_graze_zone2d.hpp"

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstdint>

namespace BlastBullets2D {
using namespace godot;

// Internal (non-persistent) group of every running BulletSpawner2D with
// graze on: BulletFactory2D scans it for zones flagged
// preview_during_runtime. The tree keeps it exact (leaving the tree or being
// freed removes a spawner); never a user-facing group.
static constexpr const char *GRAZE_SPAWNER_GROUP = "_blast_bullets_graze_spawners";

// One graze target as the runtime keeps it: an instance id and the global
// position read when the snapshot was taken. Never a pointer: user code runs
// between the snapshot and its use (handlers of earlier volleys in the same
// factory sweep), so every consumer re-resolves the id through ObjectDB.
struct GrazeTarget2D {
	uint64_t id = 0;
	Vector2 position;
};

// THE graze target filter, shared by every graze target source (the
// factory's zone-group lists, the spawner's name/path/children sources)
// and the ring previews, so a preview draws exactly what the runtime
// tests. A usable target is inside the tree, not queued for deletion,
// lives in `world` (when given: coordinates of different worlds never mix)
// and has a finite global position (returned in r_position).
inline bool graze_target_usable2d(Node2D *node, const World2D *world, Vector2 &r_position) {
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

// Counts `node` in r_total when it is a usable target (above) and appends
// it to r_targets while there is room (MAX_TARGETS); r_total past
// MAX_TARGETS lets callers warn about the overflow. Returns whether it was
// usable.
inline bool graze_add_target2d(Node2D *node, const World2D *world, GrazeTarget2D *r_targets, int &r_count, int &r_total) {
	Vector2 position;
	if (!graze_target_usable2d(node, world, position)) {
		return false;
	}
	++r_total;
	if (r_count < BulletGrazeZone2D::MAX_TARGETS) {
		r_targets[r_count].id = node->get_instance_id();
		r_targets[r_count].position = position;
		++r_count;
	}
	return true;
}

// The usable Node2D members of `group` (also in filter_group when it is not
// empty) except `exclude_id`, the first MAX_TARGETS in tree order (SceneTree
// sorts group members by tree order). Returns how many qualified in total.
inline int collect_graze_targets2d(SceneTree *tree, const StringName &group, const StringName &filter_group, const World2D *world, uint64_t exclude_id, GrazeTarget2D *r_targets, int &r_count) {
	r_count = 0;
	if (tree == nullptr || group.is_empty()) {
		return 0;
	}
	const TypedArray<Node> members = tree->get_nodes_in_group(group);
	int total = 0;
	for (int i = 0; i < members.size(); ++i) {
		Node2D *node = Object::cast_to<Node2D>(members[i]);
		if (node == nullptr || node->get_instance_id() == exclude_id) {
			continue;
		}
		if (!filter_group.is_empty() && !node->is_in_group(filter_group)) {
			continue;
		}
		graze_add_target2d(node, world, r_targets, r_count, total);
	}
	return total;
}

// Collision bounding radius of a bullet shape, the same rule the spawner's
// collision-ring preview and the bounce separation use: circle radius,
// rectangle half its shorter side, capsule half its height.
inline real_t graze_shape_bound_radius2d(PhysicsServer2D::ShapeType shape_type, real_t circle_radius, const Vector2 &rect_size, real_t capsule_height) {
	real_t r = circle_radius;
	if (shape_type == PhysicsServer2D::SHAPE_RECTANGLE) {
		r = MIN(rect_size.x, rect_size.y) * (real_t)0.5;
	} else if (shape_type == PhysicsServer2D::SHAPE_CAPSULE) {
		r = capsule_height * (real_t)0.5;
	}
	if (!Math::is_finite(r) || r < 0.0) {
		return 0.0;
	}
	return r;
}

} // namespace BlastBullets2D
