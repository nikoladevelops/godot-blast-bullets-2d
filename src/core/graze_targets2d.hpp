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
#include <vector>

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

// Appends `node` to r_targets when it is a usable target (above).
inline bool graze_add_target2d(Node2D *node, const World2D *world, std::vector<GrazeTarget2D> &r_targets) {
	Vector2 position;
	if (!graze_target_usable2d(node, world, position)) {
		return false;
	}
	GrazeTarget2D target;
	target.id = node->get_instance_id();
	target.position = position;
	r_targets.push_back(target);
	return true;
}

// Appends the usable Node2D members of `group` (also in filter_group when it
// is not empty) except `exclude_id`, in tree order (SceneTree sorts group
// members by tree order). No cap: every member counts.
inline void collect_graze_targets2d(SceneTree *tree, const StringName &group, const StringName &filter_group, const World2D *world, uint64_t exclude_id, std::vector<GrazeTarget2D> &r_targets) {
	if (tree == nullptr || group.is_empty()) {
		return;
	}
	const TypedArray<Node> members = tree->get_nodes_in_group(group);
	for (int i = 0; i < members.size(); ++i) {
		Node2D *node = Object::cast_to<Node2D>(members[i]);
		if (node == nullptr || node->get_instance_id() == exclude_id) {
			continue;
		}
		if (!filter_group.is_empty() && !node->is_in_group(filter_group)) {
			continue;
		}
		graze_add_target2d(node, world, r_targets);
	}
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
