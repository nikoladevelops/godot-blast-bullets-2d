#include "debugger/despawn_bounds_preview_layer2d.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

namespace BlastBullets2D {

void DespawnBoundsPreviewLayer2D::_bind_methods() {
}

void DespawnBoundsPreviewLayer2D::set_snapshot(const Rect2 &p_rect, const Color &p_color) {
	if (rect == p_rect && color == p_color) {
		return;
	}
	rect = p_rect;
	color = p_color;
	queue_redraw();
}

void DespawnBoundsPreviewLayer2D::_draw() {
	++debug_draw_count;
	if (rect.size.x <= 0.0 || rect.size.y <= 0.0) {
		return;
	}
	draw_rect(rect, color, true);
	// The outline reads on any background: clearly stronger than the fill.
	Color outline = color;
	outline.a = CLAMP(color.a * 3.2f, 0.6f, 1.0f);
	draw_rect(rect, outline, false, 2.0f);
}

} // namespace BlastBullets2D
