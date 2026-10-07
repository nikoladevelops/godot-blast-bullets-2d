#pragma once

#include "core/graze_targets2d.hpp"
#include "data/bullet_graze_zone2d.hpp"

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

// One ring as a graze preview draws it (plain values, never a node pointer).
struct GrazePreviewCircle2D {
	Vector2 center;
	real_t radius = 0.0;
	Color color;
	int zone_index = 0;
	int ring_index = 0;
	uint64_t target_id = 0;
	bool operator==(const GrazePreviewCircle2D &o) const {
		return center == o.center && radius == o.radius && color == o.color && zone_index == o.zone_index && ring_index == o.ring_index && target_id == o.target_id;
	}
};

// Graze ring preview layer: every ring of the zones it is given, drawn as a
// circle around each resolved target. Used by BulletSpawner2D (its editor
// preview, and at runtime with graze_preview_during_runtime). Top-level
// (canvas coordinates: target global
// positions draw as they are, a moving owner never redraws), owner-less and
// internal (never saved, never a pattern marker). Same snapshot contract as
// PatternPreviewLayer2D: the owner stores plain values, _draw() repaints
// them forever, and a redraw happens only when the snapshot changed.
class GrazePreviewLayer2D : public Node2D {
	GDCLASS(GrazePreviewLayer2D, Node2D)

public:
	std::vector<GrazePreviewCircle2D> circles;
	float line_width = 1.5f;
	// Instrumentation for tests: how many times _draw() ran.
	int debug_draw_count = 0;
	// Stores a new snapshot and queues ONE redraw when it differs.
	void set_circles(const std::vector<GrazePreviewCircle2D> &p_circles, float p_line_width);
	void _draw() override;

protected:
	static void _bind_methods();
};

// Appends the rings of `zone` around each of `targets` (resolved by the
// zone's graze detector, so a preview draws exactly what the runtime
// tests). Inner rings use the zone's preview_color, each larger ring 0.7x
// fainter.
inline void append_graze_zone_circles2d(const BulletGrazeZone2D &zone, int zone_index, const GrazeTarget2D *targets, int count, std::vector<GrazePreviewCircle2D> &r_circles) {
	const int rings = CLAMP(zone.ring_count, 1, BulletGrazeZone2D::MAX_RINGS);
	for (int r = 0; r < rings; ++r) {
		int rank = 0;
		for (int o = 0; o < rings; ++o) {
			if (zone.ring_radii[o] < zone.ring_radii[r] || (zone.ring_radii[o] == zone.ring_radii[r] && o < r)) {
				++rank;
			}
		}
		Color color = zone.preview_color;
		color.a *= (float)std::pow(0.7, (double)rank);
		for (int k = 0; k < count; ++k) {
			GrazePreviewCircle2D circle;
			circle.center = targets[k].position;
			circle.radius = zone.ring_radii[r];
			circle.color = color;
			circle.zone_index = zone_index;
			circle.ring_index = r;
			circle.target_id = targets[k].id;
			r_circles.push_back(circle);
		}
	}
}

} // namespace BlastBullets2D
