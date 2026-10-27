#include "debugger/laser_debug_layer2d.hpp"

#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

using namespace godot;
namespace BlastBullets2D {

void LaserDebugLayer2D::_draw() {
	++debug_draw_count;
	if (boxes.empty()) {
		return;
	}
	Color fill = color;
	fill.a = color.a * 0.35f;
	PackedVector2Array quad;
	quad.resize(4);
	PackedVector2Array outline;
	outline.resize(5);
	for (const LaserDebugBox2D &box : boxes) {
		const Vector2 e = box.extents;
		const Vector2 corners[4] = { box.pose.xform(Vector2(-e.x, -e.y)), box.pose.xform(Vector2(e.x, -e.y)), box.pose.xform(Vector2(e.x, e.y)), box.pose.xform(Vector2(-e.x, e.y)) };
		for (int k = 0; k < 4; ++k) {
			quad.set(k, corners[k]);
			outline.set(k, corners[k]);
		}
		outline.set(4, corners[0]);
		draw_colored_polygon(quad, fill);
		draw_polyline(outline, color, 1.0f);
	}
}

} // namespace BlastBullets2D
