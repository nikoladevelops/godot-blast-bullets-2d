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

// One graze target as the runtime keeps it: an instance id and the global
// position read when the snapshot was taken. Never a pointer: user code runs
// between the snapshot and its use (handlers of earlier volleys in the same
// factory sweep), so every consumer re-resolves the id through ObjectDB.
struct GrazeTarget2D {
	uint64_t id = 0;
	Vector2 position;
};

// THE graze target filter, shared by the factory's per-tick cache (runtime
// detection) and the spawner's ring preview, so the preview draws exactly
// what the runtime tests. A target is a Node2D member of `group` that is
// inside the tree, not queued for deletion, has a finite global position,
// lives in `world` (when given: coordinates of different worlds never mix)
// and is not `exclude_id`. Keeps the first MAX_TARGETS in tree order
// (SceneTree sorts group members by tree order) and returns how many
// qualified in total, so callers can warn about the overflow.
inline int collect_graze_targets2d(SceneTree *tree, const StringName &group, const World2D *world, uint64_t exclude_id, GrazeTarget2D *r_targets, int &r_count) {
	r_count = 0;
	if (tree == nullptr || group.is_empty()) {
		return 0;
	}
	const TypedArray<Node> members = tree->get_nodes_in_group(group);
	int total = 0;
	for (int i = 0; i < members.size(); ++i) {
		Node2D *node = Object::cast_to<Node2D>(members[i]);
		if (node == nullptr || !node->is_inside_tree() || node->is_queued_for_deletion()) {
			continue;
		}
		const uint64_t id = node->get_instance_id();
		if (id == exclude_id) {
			continue;
		}
		if (world != nullptr) {
			const Ref<World2D> node_world = node->get_world_2d();
			if (node_world.ptr() != world) {
				continue;
			}
		}
		const Vector2 position = node->get_global_position();
		if (!position.is_finite()) {
			continue;
		}
		++total;
		if (r_count < BulletGrazeZone2D::MAX_TARGETS) {
			r_targets[r_count].id = id;
			r_targets[r_count].position = position;
			++r_count;
		}
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
