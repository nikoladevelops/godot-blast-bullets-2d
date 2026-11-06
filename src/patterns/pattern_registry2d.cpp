#include "patterns/pattern_registry2d.hpp"
#include "patterns/bullet_patterns2d.hpp"

#include "godot_cpp/variant/utility_functions.hpp"

using namespace godot;

namespace BlastBullets2D {

static const PatternShapeInfo2D kShapes[] = {
	{ PATTERN_SHAPE_CHILDREN, "From Children", nullptr, false, false, true },
	{ PATTERN_SHAPE_SELF, "From Self", nullptr, false, false, false },
	{ PATTERN_SHAPE_PATH2D, "Path2D", "helper_path2d_", false, false, true },
	{ PATTERN_SHAPE_AIMED, "Aimed", "helper_aimed_", false, false, true },
	{ PATTERN_SHAPE_CUSTOM, "Custom", "helper_custom_", false, false, true },
	{ PATTERN_SHAPE_CIRCLE, "Circle", "helper_circle_", true, false, false },
	{ PATTERN_SHAPE_SQUARE, "Square", "helper_square_", true, true, false },
	{ PATTERN_SHAPE_RECTANGLE, "Rectangle", "helper_rectangle_", true, true, false },
	{ PATTERN_SHAPE_TRIANGLE, "Triangle", "helper_triangle_", true, true, false },
	{ PATTERN_SHAPE_DIAMOND, "Diamond", "helper_diamond_", true, true, false },
	{ PATTERN_SHAPE_TRAPEZOID, "Trapezoid", "helper_trapezoid_", true, true, false },
	{ PATTERN_SHAPE_POLYGON, "Polygon", "helper_polygon_", true, true, false },
	{ PATTERN_SHAPE_ELLIPSE, "Ellipse", "helper_ellipse_", true, false, false },
	{ PATTERN_SHAPE_RING, "Ring", "helper_ring_", true, false, false },
	{ PATTERN_SHAPE_STAR, "Star", "helper_star_", true, true, false },
	{ PATTERN_SHAPE_HEART, "Heart", "helper_heart_", true, false, false },
	{ PATTERN_SHAPE_STAR_POLYGON, "Star Polygon", "helper_star_polygon_", false, false, false },
	{ PATTERN_SHAPE_FLOWER, "Flower", "helper_flower_", true, false, false },
	{ PATTERN_SHAPE_ROSE, "Rose", "helper_rose_", true, false, false },
	{ PATTERN_SHAPE_LISSAJOUS, "Lissajous", "helper_lissajous_", true, false, false },
	{ PATTERN_SHAPE_LINE, "Line", "helper_line_", false, false, false },
	{ PATTERN_SHAPE_GRID, "Grid", "helper_grid_", false, false, false },
	{ PATTERN_SHAPE_LATTICE, "Lattice", "helper_lattice_", false, false, false },
	{ PATTERN_SHAPE_RAIN, "Rain", "helper_rain_", false, false, false },
	{ PATTERN_SHAPE_WATERFALL, "Waterfall", "helper_waterfall_", false, false, false },
	{ PATTERN_SHAPE_WAVE, "Wave", "helper_wave_", false, false, false },
	{ PATTERN_SHAPE_FAN, "Fan", "helper_fan_", false, false, false },
	{ PATTERN_SHAPE_CORRIDOR, "Corridor", "helper_corridor_", false, false, true },
	{ PATTERN_SHAPE_SPIRAL, "Spiral", "helper_spiral_", false, false, false },
	{ PATTERN_SHAPE_MULTISPIRAL, "Multi Spiral", "helper_multispiral_", false, false, false },
	{ PATTERN_SHAPE_COUNTER_SPIRAL, "Counter Spiral", "helper_counter_spiral_", false, false, false },
	{ PATTERN_SHAPE_CROSS, "Cross", "helper_cross_", false, false, false },
	{ PATTERN_SHAPE_SCATTER, "Scatter", "helper_scatter_", false, false, false },
};

static constexpr int kShapeCount = (int)(sizeof(kShapes) / sizeof(kShapes[0]));
static_assert(kShapeCount == PATTERN_SHAPE_COUNT, "every pattern shape id has exactly one registry row");

const PatternShapeInfo2D *pattern_shapes2d(int &r_count) {
	r_count = kShapeCount;
	return kShapes;
}

const PatternShapeInfo2D *pattern_shape_info2d(int id) {
	for (const PatternShapeInfo2D &info : kShapes) {
		if (info.id == id) {
			return &info;
		}
	}
	return nullptr;
}

const char *pattern_shape_name2d(int id) {
	const PatternShapeInfo2D *info = pattern_shape_info2d(id);
	return info != nullptr ? info->name : "unknown";
}

bool pattern_shape_supports_outline2d(int id) {
	const PatternShapeInfo2D *info = pattern_shape_info2d(id);
	return info != nullptr && info->outline;
}

bool pattern_shape_supports_corners2d(int id) {
	const PatternShapeInfo2D *info = pattern_shape_info2d(id);
	return info != nullptr && info->corners;
}

bool pattern_shape_reads_external_state2d(int id) {
	const PatternShapeInfo2D *info = pattern_shape_info2d(id);
	return info != nullptr && info->reads_external_state;
}

String pattern_shape_hint2d() {
	String out;
	for (const PatternShapeInfo2D &info : kShapes) {
		if (!out.is_empty()) {
			out += ",";
		}
		out += String(info.name) + ":" + itos(info.id);
	}
	return out;
}

int pattern_shape_owning_knob2d(const String &property_name) {
	int best = -1;
	int best_len = 0;
	for (const PatternShapeInfo2D &info : kShapes) {
		if (info.knob_prefix == nullptr) {
			continue;
		}
		const String prefix(info.knob_prefix);
		if (prefix.length() > best_len && property_name.begins_with(prefix)) {
			best = info.id;
			best_len = prefix.length();
		}
	}
	return best;
}

TypedArray<Dictionary> BulletPatterns2D::get_shapes() {
	TypedArray<Dictionary> out;
	for (const PatternShapeInfo2D &info : kShapes) {
		Dictionary row;
		row["id"] = info.id;
		row["name"] = String(info.name);
		row["knob_prefix"] = info.knob_prefix != nullptr ? String(info.knob_prefix) : String();
		row["outline"] = info.outline;
		row["corners"] = info.corners;
		row["reads_external_state"] = info.reads_external_state;
		out.push_back(row);
	}
	return out;
}

} //namespace BlastBullets2D
