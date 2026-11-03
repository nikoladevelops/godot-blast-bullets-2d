#include "debugger/laser_debug_layer2d.hpp"

#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/transform2d.hpp>

using namespace godot;
namespace BlastBullets2D {

void LaserDebugLayer2D::_draw() {
	++debug_draw_count;
	if (boxes.empty()) {
		return;
	}
	Color fill = color;
	fill.a = color.a * 0.35f;
	// Each box is a rect in its own pose: a rect never triangulates, so a
	// hairline box (a slow head's tiny segment) draws where a polygon of its
	// corners failed in float (Godot printed an error per box per frame).
	for (const LaserDebugBox2D &box : boxes) {
		const Vector2 e = box.extents;
		const Rect2 rect(-e, e * (real_t)2.0);
		draw_set_transform_matrix(box.pose);
		draw_rect(rect, fill, true);
		draw_rect(rect, color, false, 1.0f);
	}
	draw_set_transform_matrix(Transform2D());
}

} // namespace BlastBullets2D
