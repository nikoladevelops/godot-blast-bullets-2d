#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/transform2d.hpp>

#include <vector>

namespace BlastBullets2D {
using namespace godot;

// One box a laser's contact query tests this tick (laser_query_box2d): its
// pose and half extents, and the laser and beam it belongs to.
struct LaserDebugBox2D {
	Transform2D pose;
	Vector2 extents;
	uint64_t laser_id = 0;
	int beam = 0;
};

// The collision debugger's laser overlay: every box the active lasers query
// this tick (one per beam segment, in a hitting phase), drawn as a
// translucent fill with an outline in the debugger color. Owned by
// BulletFactory2D (internal, owner-less, tagged, top-level so the global
// boxes draw as they are), refreshed after the laser tick while the debugger
// is enabled.
class LaserDebugLayer2D : public Node2D {
	GDCLASS(LaserDebugLayer2D, Node2D)

public:
	std::vector<LaserDebugBox2D> boxes;
	Color color = Color(1, 1, 1, 1);
	// Instrumentation for tests: how many times _draw() ran.
	int debug_draw_count = 0;

	void _draw() override;

protected:
	static void _bind_methods() {}
};

} // namespace BlastBullets2D
