#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/rect2.hpp>

namespace BlastBullets2D {
using namespace godot;

// Draws the factory's despawn rectangle: a translucent fill with a stronger
// outline. Owned by BulletFactory2D (internal, owner-less, tagged, top-level
// so the global rect draws as it is). Same snapshot contract as
// GrazePreviewLayer2D: the factory stores plain values, _draw() repaints
// them, and a redraw is queued only when the snapshot changed.
class DespawnBoundsPreviewLayer2D : public Node2D {
	GDCLASS(DespawnBoundsPreviewLayer2D, Node2D)

public:
	Rect2 rect;
	Color color = Color(1, 0, 0, 0.25f);
	// Instrumentation for tests: how many times _draw() ran.
	int debug_draw_count = 0;
	// Stores a new snapshot and queues ONE redraw when it differs.
	void set_snapshot(const Rect2 &p_rect, const Color &p_color);
	void _draw() override;

protected:
	static void _bind_methods();
};

} // namespace BlastBullets2D
