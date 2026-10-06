#include "debugger/graze_preview_layer2d.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

namespace BlastBullets2D {

void GrazePreviewLayer2D::_bind_methods() {
}

void GrazePreviewLayer2D::set_circles(const std::vector<GrazePreviewCircle2D> &p_circles, float p_line_width) {
	if (circles == p_circles && line_width == p_line_width) {
		return;
	}
	circles = p_circles;
	line_width = p_line_width;
	queue_redraw();
}

void GrazePreviewLayer2D::_draw() {
	++debug_draw_count;
	for (const GrazePreviewCircle2D &c : circles) {
		if (!c.center.is_finite() || !Math::is_finite(c.radius) || c.radius <= 0.0) {
			continue;
		}
		// Enough segments for a smooth ring at any preview size.
		const int segments = CLAMP((int)(c.radius / 4.0), 24, 128);
		draw_arc(c.center, c.radius, 0.0, Math::TAU, segments, c.color, line_width, false);
	}
}

} // namespace BlastBullets2D
