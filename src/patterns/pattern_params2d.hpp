#pragma once

// Parameter structs of the native pattern generators (BulletPatterns2D::
// generate_<shape>2d). Field names and defaults are the ones of the bound
// helper_generate_transforms_<shape> GDScript functions, so a default
// struct generates exactly what the default call does. OutlineLayout2D and
// CornerLayout2D carry the shared outline-layout knobs of the loop shapes.

#include "patterns/bullet_patterns2d.hpp"

namespace BlastBullets2D {
using namespace godot;

// Placement of a closed loop: on outline, layers, fill inside (layout_outline_slots).
struct OutlineLayout2D {
	int outline_placement = 0;
	int outline_facing = 0;
	bool outline_reverse = false;
	int outline_slot_offset = 0;
	double fill_spacing = 32.0;
	bool fill_stagger = false;
	double fill_margin = 0.0;
	int layer_count = 1;
	double layer_scale = 0.2;
	int layer_side = 0;
	int layer_fill = 0;
	int layer_start_offset = 0;
	int layer_scale_curve = 0;
	PackedFloat32Array layer_custom_scales = PackedFloat32Array();
	int layer_twist = 0;
	int layer_max_dots = 0;
	int layer_layout = 1;
};

// Corner anchoring of the polygon loops (rectangle, square, polygon, triangle,
// trapezoid, diamond, star). smooth() is what the curved loops use.
struct CornerLayout2D {
	int outline_distribution = 1;
	int outline_corner_priority = 0;
	int outline_corner_mode = 0;
	double outline_edge_margin = 0.0;
	int outline_corner_facing = 0;
	static CornerLayout2D smooth() {
		CornerLayout2D c;
		c.outline_distribution = 1; // curves resample evenly, no corners
		c.outline_corner_priority = 0;
		c.outline_corner_mode = 0;
		c.outline_edge_margin = 0.0;
		c.outline_corner_facing = 0;
		return c;
	}
};

struct AimedParams2D {
	Vector2 target_position;
	real_t spread = 0.3;
	real_t step_offset = 0.0;
	bool centered = true;
};

struct CircleParams2D {
	real_t radius = 150.0;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
	OutlineLayout2D outline;
};

struct CorridorParams2D {
	Vector2 aim_direction;
	real_t width = 400.0;
	real_t spacing = 32.0;
	real_t gap_width = 96.0;
	bool face_aim = true;
	real_t facing_offset_degrees = 0.0;
};

struct CounterSpiralParams2D {
	int arms = 2;
	real_t start_radius = 50.0;
	real_t radius_step = 15.0;
	real_t angle_step = 0.6;
	bool rotate_with_marker = true;
	BulletPatterns2D::SpiralFacingMode facing_mode = BulletPatterns2D::SPIRAL_FACING_TANGENT;
	real_t facing_offset_degrees = 0.0;
	int arm_index_stride = 1;
	bool mirror_alternate_arms = true;
};

struct CrossParams2D {
	int arm_count = 4;
	real_t arm_length = 150.0;
	real_t spacing = 32.0;
	real_t base_rotation = 0.0;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
};

struct DiamondParams2D {
	real_t diagonal_x = 200.0;
	real_t diagonal_y = 300.0;
	real_t rotation = 0.0;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
	OutlineLayout2D outline;
	CornerLayout2D corner;
};

struct EdgeFromPointsParams2D {
	PackedVector2Array edge_points;
	bool closed = false;
	bool flip_normals = false;
	bool random_sample = false;
	real_t jitter = 0.0;
	real_t facing_offset_degrees = 0.0;
	uint64_t seed = 0;
	real_t spread = 0.0;
	real_t spread_exponent = 2.0;
	int spread_side = 0;
	real_t tangent_jitter = 0.0;
};

struct EllipseParams2D {
	real_t radius_x = 150.0;
	real_t radius_y = 100.0;
	real_t ellipse_rotation = 0.0;
	real_t start_angle = 0.0;
	real_t arc = Math::TAU;
	BulletPatterns2D::EllipseMode mode = BulletPatterns2D::ELLIPSE_FULL;
	int gap_count = 2;
	real_t gap_width = 0.3;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
	OutlineLayout2D outline;
};

struct FanParams2D {
	real_t spread = 0.5;
	real_t direction_angle = 0.0;
	real_t step_offset = 0.0;
	bool centered = true;
	real_t angle_jitter = 0.0;
	uint64_t seed = 0;
};

struct FlowerParams2D {
	int petals = 6;
	real_t radius = 150.0;
	real_t petal_spread = 0.5;
	real_t petal_sharpness = 1.0;
	real_t base_rotation = 0.0;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
	int flower_type = 0;
	double inner_radius_scale = 0.0;
	double spiro_roller = 45.0;
	double spiro_pen = 80.0;
	double super_lobes = 6.0;
	double super_fullness = 1.0;
	OutlineLayout2D outline;
};

struct GridParams2D {
	int rows_per_column = 10;
	BulletPatterns2D::Alignment alignment = BulletPatterns2D::Alignment::CENTER_LEFT;
	real_t column_offset = 150.0;
	real_t row_offset = 150.0;
	bool rotate_grid_with_marker = true;
	bool random_local_rotation = false;
	real_t jitter = 0.0;
	uint64_t seed = 0;
};

struct HeartParams2D {
	real_t size = 150.0;
	real_t base_rotation = 0.0;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
	OutlineLayout2D outline;
};

struct LatticeParams2D {
	int columns = 8;
	int rows = 5;
	real_t spacing_x = 48.0;
	real_t spacing_y = 42.0;
	bool stagger_rows = true;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
};

struct LineParams2D {
	Vector2 direction;
	real_t spacing = 32.0;
	bool face_direction = true;
	BulletPatterns2D::LineAnchor anchor = BulletPatterns2D::LINE_ANCHOR_CENTER;
	bool perpendicular = false;
};

struct LissajousParams2D {
	real_t size_x = 200.0;
	real_t size_y = 120.0;
	real_t freq_x = 3.0;
	real_t freq_y = 2.0;
	real_t phase = 0.0;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
	OutlineLayout2D outline;
};

struct MultispiralParams2D {
	int arms = 3;
	real_t start_radius = 50.0;
	real_t radius_step = 15.0;
	real_t angle_step = 0.6;
	bool rotate_with_marker = true;
	BulletPatterns2D::SpiralFacingMode facing_mode = BulletPatterns2D::SPIRAL_FACING_TANGENT;
	real_t facing_offset_degrees = 0.0;
	int arm_index_stride = 1;
};

struct PolygonParams2D {
	int vertices = 6;
	real_t radius = 150.0;
	real_t base_rotation = 0.0;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
	OutlineLayout2D outline;
	CornerLayout2D corner;
};

struct PolylineParams2D {
	PackedVector2Array points;
	bool closed = false;
	BulletPatterns2D::PolylineDistribution distribution = BulletPatterns2D::POLYLINE_DISTRIBUTION_EVEN;
	real_t spacing = 32.0;
	BulletPatterns2D::PolylineOverflow overflow = BulletPatterns2D::POLYLINE_OVERFLOW_CLAMP;
	BulletPatterns2D::PolylineAnchor anchor = BulletPatterns2D::POLYLINE_ANCHOR_START;
	real_t start_offset = 0.0;
	bool reverse = false;
	BulletPatterns2D::PolylineFacing facing = BulletPatterns2D::POLYLINE_FACING_ALONG_PATH;
	real_t facing_offset_deg = 0.0;
};

struct RainParams2D {
	real_t band_width = 600.0;
	Vector2 rain_direction = Vector2(0, 1);
	real_t drop_spacing = 48.0;
	real_t jitter = 12.0;
	uint64_t seed = 0;
};

struct RectangleParams2D {
	Vector2 size = Vector2(300, 200);
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
	OutlineLayout2D outline;
	CornerLayout2D corner;
};

struct RingParams2D {
	real_t radius = 150.0;
	real_t start_angle = 0.0;
	real_t arc = Math::TAU;
	bool rotate_with_marker = true;
	bool random_rotation = false;
	bool face_outward = true;
	real_t y_scale = 1.0;
	real_t facing_offset_degrees = 0.0;
	uint64_t seed = 0;
	OutlineLayout2D outline;
};

struct RoseParams2D {
	int petals = 6;
	real_t radius = 150.0;
	real_t lobe_sharpness = 1.0;
	real_t base_rotation = 0.0;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
	OutlineLayout2D outline;
};

struct ScatterParams2D {
	real_t burst_radius = 120.0;
	real_t facing_jitter = 0.4;
	uint64_t seed = 0;
	real_t inner_radius = 0.0;
	Vector2 sector_direction = Vector2(1, 0);
	real_t sector_arc = Math::TAU;
	BulletPatterns2D::ScatterFacingMode facing_mode = BulletPatterns2D::SCATTER_FACING_OUTWARD;
};

struct SpiralParams2D {
	real_t start_radius = 50.0;
	real_t radius_step = 15.0;
	real_t angle_step = 0.6;
	bool rotate_with_marker = true;
	BulletPatterns2D::SpiralFacingMode facing_mode = BulletPatterns2D::SPIRAL_FACING_TANGENT;
	real_t facing_offset_degrees = 0.0;
};

struct StarParams2D {
	int points = 5;
	real_t outer_radius = 150.0;
	real_t inner_radius = 65.0;
	real_t base_rotation = 0.0;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
	OutlineLayout2D outline;
	CornerLayout2D corner;
};

struct StarPolygonParams2D {
	int vertices = 5;
	real_t radius = 150.0;
	real_t vertex_bias = 2.0;
	real_t base_rotation = 0.0;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
};

struct TrapezoidParams2D {
	real_t base_top = 200.0;
	real_t base_bottom = 300.0;
	real_t height = 200.0;
	real_t rotation = 0.0;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
	OutlineLayout2D outline;
	CornerLayout2D corner;
};

struct TriangleParams2D {
	BulletPatterns2D::TriangleType triangle_type = BulletPatterns2D::TRIANGLE_EQUILATERAL;
	real_t size_a = 150.0;
	real_t size_b = 150.0;
	real_t rotation = 0.0;
	bool face_outward = true;
	real_t facing_offset_degrees = 0.0;
	OutlineLayout2D outline;
	CornerLayout2D corner;
};

struct WaterfallParams2D {
	int columns = 12;
	real_t column_spacing = 48.0;
	int rows = 3;
	real_t row_spacing = 64.0;
	real_t stagger = 0.5;
	Vector2 rain_direction = Vector2(0, 1);
	real_t jitter = 6.0;
	real_t facing_offset_degrees = 0.0;
	uint64_t seed = 0;
};

struct WaveParams2D {
	real_t width = 600.0;
	real_t amplitude = 48.0;
	real_t waves = 2.0;
	Vector2 direction = Vector2(1, 0);
	bool face_direction = true;
	real_t facing_offset_degrees = 0.0;
};

} //namespace BlastBullets2D
