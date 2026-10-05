#pragma once

// BulletPatterns2D: the bullet-pattern library. Every shape generator
// (helper_generate_transforms_*), its preview track (helper_sample_outline_*),
// the outline layout engine (On Outline / Layers / Fill Inside), the pattern
// enums and the conformance inspectors (debug_*) live here, in src/patterns/:
//   patterns_internal.hpp          helpers shared by the files below
//   pattern_layout2d.cpp           outline layout engine + shared machinery
//   patterns_shapes.cpp            danmaku generators (grid, fan, spiral, ...)
//   patterns_curves.cpp            closed curves (ring, circle, ellipse, flower, ...)
//   patterns_polygons.cpp          polygons (rectangle, polygon, triangle, ...)
//   patterns_edges.cpp             point lists, image edges, edge normals
//   patterns_polyline.cpp          arc-length layout along a polyline (Path2D mode)
//   pattern_bake_cache2d.cpp       raw-pattern bake cache + motion classifier
//   pattern_pose2d.cpp             spin/scale matrix, skip mask, per-bullet scale
//   pattern_preview_tracks2d.cpp   helper_sample_outline_* preview tracks
//   pattern_debug2d.cpp            outline/volley conformance inspectors
//   bullet_patterns2d_bindings.cpp Godot bindings of all of the above
// Pure static functions of their arguments (no instance state): BulletSpawner2D
// calls them per shot or once per bake, GDScript calls them directly
// (BulletPatterns2D.helper_generate_transforms_ring(...)). The class is
// registered abstract: it is never instantiated.

#include "godot_cpp/classes/image.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/packed_int32_array.hpp"
#include "godot_cpp/variant/packed_vector2_array.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/vector2.hpp"

#include <vector>

namespace BlastBullets2D {
using namespace godot;

// One global Transform2D per bullet: the native result of a pattern
// generator (the bound GDScript helpers return it as a TypedArray).
using PatternSlots2D = std::vector<Transform2D>;

// Parameter structs of the native cores (patterns/pattern_params2d.hpp).
struct OutlineLayout2D;
struct CornerLayout2D;
struct AimedParams2D;
struct CircleParams2D;
struct CorridorParams2D;
struct CounterSpiralParams2D;
struct CrossParams2D;
struct DiamondParams2D;
struct EdgeFromPointsParams2D;
struct EllipseParams2D;
struct FanParams2D;
struct FlowerParams2D;
struct GridParams2D;
struct HeartParams2D;
struct LatticeParams2D;
struct LineParams2D;
struct LissajousParams2D;
struct MultispiralParams2D;
struct PolygonParams2D;
struct PolylineParams2D;
struct RainParams2D;
struct RectangleParams2D;
struct RingParams2D;
struct RoseParams2D;
struct ScatterParams2D;
struct SpiralParams2D;
struct StarParams2D;
struct StarPolygonParams2D;
struct TrapezoidParams2D;
struct TriangleParams2D;
struct WaterfallParams2D;
struct WaveParams2D;

class BulletPatterns2D : public Object {
	GDCLASS(BulletPatterns2D, Object)

public:
	// The shape registry (patterns/pattern_registry2d.hpp) as data, in
	// inspector order: one Dictionary per pattern source with id, name,
	// knob_prefix ("" when it owns no knobs), outline, corners and
	// reads_external_state. Ids are BulletSpawner2D.pattern_source values.
	static TypedArray<Dictionary> get_shapes();

	// Enum class for grid alignment
	enum Alignment {
		TOP_LEFT,
		TOP_CENTER,
		TOP_RIGHT,
		CENTER_LEFT,
		CENTER,
		CENTER_RIGHT,
		BOTTOM_LEFT,
		BOTTOM_CENTER,
		BOTTOM_RIGHT
	};

	// Facing modes for the spiral transform generator. TANGENT faces each
	// bullet along its travel direction on the spiral (analytic derivative);
	// RADIAL_OUTWARD keeps the historical behavior (away from the marker,
	// mirrored for negative radii); TOWARD_CENTER faces the marker;
	// KEEP_MARKER keeps the marker rotation on every transform.
	enum SpiralFacingMode {
		SPIRAL_FACING_TANGENT,
		SPIRAL_FACING_RADIAL_OUTWARD,
		SPIRAL_FACING_TOWARD_CENTER,
		SPIRAL_FACING_KEEP_MARKER
	};

	// Scatter facing: which way burst bullets look. OUTWARD faces away from
	// the marker (explosions), RANDOM faces anywhere (shotgun spray),
	// INWARD faces the marker (implosions).
	enum ScatterFacingMode {
		SCATTER_FACING_OUTWARD,
		SCATTER_FACING_RANDOM,
		SCATTER_FACING_INWARD
	};

	// Anchor for the line transform generator: which end of the row the
	// marker sits at. CENTER (default) preserves the historical behavior.
	enum LineAnchor {
		LINE_ANCHOR_START,
		LINE_ANCHOR_CENTER,
		LINE_ANCHOR_END
	};

	// Ring mode for the ellipse transform generator: FULL draws every slot
	// around the ellipse; ARC draws a spaced arc segment; WALL draws a dense
	// arc with carved dodge gaps (danmaku wall with readable escape routes).
	enum EllipseMode {
		ELLIPSE_FULL,
		ELLIPSE_ARC,
		ELLIPSE_WALL
	};

	// Triangle kinds for helper_generate_transforms_triangle(): equilateral
	// (circumradius), isosceles (base + height, apex up) and right-angled
	// (X/Y legs from the corner, recentered). All corners recentre on the
	// marker and walk counter-clockwise like the rectangle primitive.
	enum TriangleType {
		TRIANGLE_EQUILATERAL = 0,
		TRIANGLE_ISOSCELES = 1,
		TRIANGLE_RIGHT = 2
	};

	// Flower bloom kinds for helper_generate_transforms_flower(): FAN is the
	// legacy petal-major fan (byte-identical default); RHODONEA is a
	// continuous rhodonea sweep; PHYLLOTAXIS is a Vogel golden-angle disc;
	// SPIROGRAPH is a hypotrochoid lacy bloom; SUPERFORMULA is a simplified
	// Gielis curve (lobes m + fullness exponent).
	enum FlowerBloom {
		FLOWER_FAN = 0,
		FLOWER_RHODONEA = 1,
		FLOWER_PHYLLOTAXIS = 2,
		FLOWER_SPIROGRAPH = 3,
		FLOWER_SUPERFORMULA = 4
	};

	// Named pattern presets that fill the spawner's helper_* properties in
	// one call (discoverability over 40 raw knobs; see
	// BulletSpawner2D::apply_pattern_preset).
	enum PatternPreset {
		PATTERN_PRESET_CUSTOM = -1,
		PATTERN_PRESET_RADIAL_DENSE = 0,
		PATTERN_PRESET_RADIAL_SPARSE,
		PATTERN_PRESET_SPIRAL_3ARM,
		PATTERN_PRESET_AIMED_FAN_NARROW,
		PATTERN_PRESET_AIMED_FAN_WIDE,
		PATTERN_PRESET_RING_SLOW,
		PATTERN_PRESET_WALL_GAPS,
		PATTERN_PRESET_RAIN,
		PATTERN_PRESET_FLOWER_6,
		PATTERN_PRESET_SCATTER_BURST,
		PATTERN_PRESET_CROSS_BURST,
		PATTERN_PRESET_STAR_SHELL,
		PATTERN_PRESET_HEART_BLOOM,
		PATTERN_PRESET_SNAKE_WAVE,
		PATTERN_PRESET_WATERFALL_CURTAIN,
		PATTERN_PRESET_PETAL_STORM,
		PATTERN_PRESET_TWIN_SPIRAL_COUNTER,
		PATTERN_PRESET_AIMED_TRAP,
		PATTERN_PRESET_BLOSSOM_FINALE,
		PATTERN_PRESET_TERRAIN_CREST
	};

	// Universal side placement for closed-outline patterns (Ring, Ellipse,
	// Star, Polygon, Flower, Rose, Lissajous, Custom, and the shape
	// generators below): where bullets sit relative to the path they were
	// generated on. ON_PATH is identity (default: every existing scene is
	// untouched). OUTSIDE/INSIDE push along each slot's facing by
	// spread * pow(rand, exponent); BOTH picks a random side per bullet.
	// Positions move; facings never change.
	enum SideMode {
		SIDE_ON_PATH = 0,
		SIDE_OUTSIDE = 1,
		SIDE_INSIDE = 2,
		SIDE_BOTH = 3
	};

	// Outline placement: where loop-shape bullets live. ON_OUTLINE keeps the
	// generated slot loop exactly (single layer, the default); LAYERS spreads
	// the same slot count over scaled repeats of the loop about its center
	// (same figure at every layer; layer 0 sits exactly on the outline,
	// extras grow inward/outward per OutlineLayerSide); FILL_INSIDE
	// replaces the loop with a grid masked to the shape interior: exactly
	// transforms_amount cells, picked evenly over the shape; fill_spacing is
	// honored when they fit and shrinks just enough when they do not.
	enum OutlinePlacement {
		OUTLINE_ON_OUTLINE = 0,
		OUTLINE_LAYERS = 1,
		OUTLINE_FILL_INSIDE = 2
	};

	// Layer side: which side of the base outline extra layers grow toward.
	// OUTWARD scales each ring up, INWARD scales down toward the center,
	// BOTH alternates (up, down, further up, ...).
	enum OutlineLayerSide {
		OUTLINE_LAYER_OUTWARD = 0,
		OUTLINE_LAYER_INWARD = 1,
		OUTLINE_LAYER_BOTH = 2
	};

	// Layer fill: how bullets are dealt across layers. INTERLEAVED sends
	// bullet i to layer (i % layer_count) so the winding order survives;
	// SEQUENTIAL fills layer 0 first, then layer 1, and so on, so small
	// volleys still read as the base shape (layer = i * layer_count / n);
	// OUTER_FIRST fills from the outermost ring inward (implosion readings);
	// PINGPONG waves 0,1,..,last,..,1 (pairs with spin).
	enum OutlineLayerFill {
		OUTLINE_LAYER_INTERLEAVED = 0,
		OUTLINE_LAYER_SEQUENTIAL = 1,
		OUTLINE_LAYER_OUTER_FIRST = 2,
		OUTLINE_LAYER_PINGPONG = 3
	};

	// Layer scale curve: how ring sizes progress. LINEAR steps evenly
	// (1+step per layer); EXPONENTIAL compounds ((1+step)^L, mirrored
	// reciprocally inward). A non-empty custom scale list overrides both.
	enum OutlineLayerScaleCurve {
		OUTLINE_LAYER_CURVE_LINEAR = 0,
		OUTLINE_LAYER_CURVE_EXPONENTIAL = 1
	};

	// Outline facing: rotates each generated default facing. NORMAL keeps it,
	// ALONG_P90 / ALONG_M90 turn it toward the loop tangent (+-90 deg).
	enum OutlineFacing {
		OUTLINE_FACING_NORMAL = 0,
		OUTLINE_FACING_ALONG_P90 = 1,
		OUTLINE_FACING_ALONG_M90 = 2
	};

	// Outline distribution: how corner-anchored polygon loops (rectangle,
	// square, polygon, triangle, trapezoid, diamond, star) spread interior
	// slots across edges. LEGACY is largest-remainder in winding order (first
	// edges win ties, so equal sides can differ); SYMMETRIC pairs opposite
	// edges so opposite sides stay equal (a single leftover dot breaks one
	// pair by exactly one, which is unavoidable for odd remainders).
	enum OutlineDistribution {
		OUTLINE_DISTRIBUTION_LEGACY = 0,
		OUTLINE_DISTRIBUTION_SYMMETRIC = 1
	};

	// Outline layer layout: how LAYERS placement assigns bullets to rings.
	// SHARED_LOOP decimates one n-point loop (bullet i scales its own slot,
	// so outer rings miss corners and open arcs miss endpoints on some
	// layers); EVEN_PER_LAYER rebuilds an even symmetric loop per ring, so
	// every ring has corners/endpoints and even gaps. SEQUENTIAL-family
	// deals only choose which bullets ride which ring; positions stay even.
	enum OutlineLayerLayout {
		OUTLINE_LAYER_LAYOUT_SHARED_LOOP = 0,
		OUTLINE_LAYER_LAYOUT_EVEN_PER_LAYER = 1
	};

	// Outline corner priority: which adjoining side owns a shared corner dot
	// on corner-anchored polygon loops (rectangle, square, polygon, triangle,
	// trapezoid, diamond, star). Classification runs in shape-local space
	// (before rotation), so it stays stable when the shape spins.
	// HORIZONTAL_SIDES (default): top/bottom edges own corners (a top-right
	// corner dot faces UP with the top side); VERTICAL_SIDES: left/right own
	// them (faces +X with the right side); BALANCED: keeps the outgoing-edge
	// owner (previous behavior) with symmetric opposite-pair leftovers.
	enum OutlineCornerPriority {
		OUTLINE_CORNER_PRIORITY_HORIZONTAL = 0,
		OUTLINE_CORNER_PRIORITY_VERTICAL = 1,
		OUTLINE_CORNER_PRIORITY_BALANCED = 2
	};

	// Outline corner mode: PIN_CORNERS (default) always seats a dot exactly
	// on every corner; EVEN_ARC spreads dots purely evenly by arc length
	// (corners only coincide when the count aligns). Pair with
	// outline_edge_margin to un-cram dense sides.
	enum OutlineCornerMode {
		OUTLINE_CORNER_MODE_PIN_CORNERS = 0,
		OUTLINE_CORNER_MODE_EVEN_ARC = 1
	};

	// Outline corner facing: which normal a corner-seated dot uses. SIDE
	// (default) keeps the owning side's edge normal (stable: rectangles and
	// squares never change); MITER faces the angle bisector of the adjoining
	// edges (triangle apexes and diamond tops face UP, star tips radial);
	// SMOOTH uses averaged loop normals everywhere (coherent on
	// high-frequency outlines like many-pointed stars, where even mid-edge
	// normals swing wildly). Interiors always keep their edge normal except
	// under SMOOTH, which interpolates the averaged normals along the edge.
	enum OutlineCornerFacing {
		OUTLINE_CORNER_FACING_SIDE = 0,
		OUTLINE_CORNER_FACING_MITER = 1,
		OUTLINE_CORNER_FACING_SMOOTH = 2
	};

	// Edge spray side: which side of the polyline the normal-direction
	// falloff extends toward. ALONG offsets along +normal, BEHIND along
	// -normal, BOTH picks a random side per bullet. (Custom mode now drives
	// these from its single helper_edge_side knob; callers may still use
	// them directly.)
	enum EdgeSpreadSide {
		EDGE_SPREAD_ALONG_NORMAL = 0,
		EDGE_SPREAD_BEHIND_NORMAL = 1,
		EDGE_SPREAD_BOTH = 2
	};

	// Polyline layout (helper_generate_transforms_polyline and the spawner's
	// Path2D mode; ids mirror BulletSpawner2D's PATH2D_* enums).
	// FIXED_SPACING lays a run `spacing` apart; EVEN covers the whole line.
	enum PolylineDistribution {
		POLYLINE_DISTRIBUTION_FIXED_SPACING = 0,
		POLYLINE_DISTRIBUTION_EVEN = 1
	};
	// A fixed run that does not fit: CLAMP piles at the end, WRAP continues
	// from the start, SHRINK_TO_FIT shrinks the spacing until it fits.
	enum PolylineOverflow {
		POLYLINE_OVERFLOW_CLAMP = 0,
		POLYLINE_OVERFLOW_WRAP = 1,
		POLYLINE_OVERFLOW_SHRINK_TO_FIT = 2
	};
	// Where a fixed run shorter than the line sits.
	enum PolylineAnchor {
		POLYLINE_ANCHOR_START = 0,
		POLYLINE_ANCHOR_CENTER = 1,
		POLYLINE_ANCHOR_END = 2
	};
	// ALONG_PATH aims +X with travel, the NORMAL modes aim across (+/-90 deg).
	enum PolylineFacing {
		POLYLINE_FACING_ALONG_PATH = 0,
		POLYLINE_FACING_NORMAL_P90 = 1,
		POLYLINE_FACING_NORMAL_M90 = 2
	};

	// ---- Layer-ring shared math (single source of truth) ----
	// Each extra concentric outline layer re-spawns the selected shape
	// scaled about the loop center (mean of the base slot loop): same
	// figure at every layer, like a second spawner with a bigger shape.
	// The volley layout (layout_outline_slots) and the spawner preview
	// share the two helpers below, so dots and rings can never drift apart.
	// helper_layer_scale_factor maps a layer index to its scale (1.0 for
	// layer 0 = the outline itself); helper_bullet_layer_index maps a
	// bullet index to its layer under the active fill deal, so volley,
	// preview and debug all deal identically.
	static int helper_bullet_layer_index(
			int bullet_index,
			int slot_count,
			int layer_count,
			int layer_fill,
			int layer_start_offset);

	// Generates a grid of 2D transforms positioned relative to marker_transform.
	// seed drives jitter + random rotation (0 = non-deterministic).
	static TypedArray<Transform2D> helper_generate_transforms_grid(
			int transforms_amount,
			Transform2D marker_transform,
			int rows_per_column = 10,
			Alignment alignment = Alignment::CENTER_LEFT,
			real_t column_offset = 150.0,
			real_t row_offset = 150.0,
			bool rotate_grid_with_marker = true,
			bool random_local_rotation = false,
			real_t jitter = 0.0,
			uint64_t seed = 0);

	// Generates transforms on a ring (or arc) around marker_transform.
	// Set face_outward to false for implosion patterns that fly toward the center.
	// y_scale stretches the ring into an ellipse (1.0 = circle); the facing
	// stays radial, so it is approximate on stretched rings.
	// seed drives random rotation (0 = non-deterministic).
	static TypedArray<Transform2D> helper_generate_transforms_ring(
			int transforms_amount,
			Transform2D marker_transform,
			real_t radius = 150.0,
			real_t start_angle = 0.0,
			real_t arc = Math::TAU,
			bool rotate_with_marker = true,
			bool random_rotation = false,
			bool face_outward = true,
			real_t y_scale = 1.0,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (exactly transforms_amount grid cells
			// inside the shape; spacing shrinks only when they do not fit) or Layers
			// (concentric rings sharing the slot count, layer 0 on outline);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// layer_count/layer_scale tune the concentric layers (LAYERS
			// placement); layer_side picks the growth side, layer_fill how
			// bullets are dealt across layers, layer_start_offset which
			// layer sequential filling starts from (wraps, sequential only).
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			uint64_t seed = 0,
			int layer_layout = 1);

	// Generates transforms in an aimed cone (shotgun spread): direction_angle is the cone center,
	// spread is the full cone width, origins stagger along the direction so pellets
	// do not stack on top of each other. When centered is false the cone is
	// one-sided, from the center direction out to +spread.
	// angle_jitter adds per-slot random variance for shotgun spread.
	// seed drives the jitter (0 = non-deterministic).
	static TypedArray<Transform2D> helper_generate_transforms_fan(
			int transforms_amount,
			Transform2D marker_transform,
			real_t spread = 0.5,
			real_t direction_angle = 0.0,
			real_t step_offset = 0.0,
			bool centered = true,
			real_t angle_jitter = 0.0,
			uint64_t seed = 0);

	// Generates transforms along an expanding spiral around marker_transform.
	// facing_mode picks the bullet facing (tangent = travel direction);
	// facing_offset_degrees twists every facing by a fixed amount.
	static TypedArray<Transform2D> helper_generate_transforms_spiral(
			int transforms_amount,
			Transform2D marker_transform,
			real_t start_radius = 50.0,
			real_t radius_step = 15.0,
			real_t angle_step = 0.6,
			bool rotate_with_marker = true,
			SpiralFacingMode facing_mode = SPIRAL_FACING_TANGENT,
			real_t facing_offset_degrees = 0.0);

	// Generates transforms in a straight wall/curtain/row centered on the marker.
	// direction is the line axis (need not be normalized); origins spread evenly
	// with the given spacing. Bullets face along the line when face_direction is
	// true, otherwise they keep the marker rotation. anchor moves the marker to
	// the start/end of the row; perpendicular faces them 90 degrees off the axis.
	static TypedArray<Transform2D> helper_generate_transforms_line(
			int transforms_amount,
			Transform2D marker_transform,
			const Vector2 &direction,
			real_t spacing = 32.0,
			bool face_direction = true,
			LineAnchor anchor = LINE_ANCHOR_CENTER,
			bool perpendicular = false);

	// Aimed fan: same as helper_generate_transforms_fan with the cone centered on
	// the marker-to-target direction.
	static TypedArray<Transform2D> helper_generate_transforms_aimed(
			int transforms_amount,
			Transform2D marker_transform,
			const Vector2 &target_position,
			real_t spread = 0.3,
			real_t step_offset = 0.0,
			bool centered = true);

	// Floral spell-card pattern with selectable bloom kinds (see FlowerBloom).
	// FAN (default): petals symmetric lobes around the marker; the
	// transforms_amount bullets are split evenly over the petals (remainder
	// spread symmetrically) and fan across petal_spread, so exactly
	// transforms_amount distinct slots are emitted at any amount.
	// petal_sharpness 0 = round lobes, higher = tighter flowers.
	// RHODONEA: continuous rhodonea sweep r = R*|cos(k*theta/2)|^p; k comes
	// from petals, p from petal_sharpness, inner_radius_scale carves a core
	// hole (0 = full bloom, < 1 = ring). petal_spread is unused.
	// PHYLLOTAXIS: Vogel golden-angle sunflower disc r = R*sqrt(i/n);
	// inner_radius_scale sets the disc inner edge (0 = center bloom).
	// petal-family knobs are unused.
	// SPIROGRAPH: hypotrochoid lacy bloom from outer radius R, roller
	// spiro_roller (r > 0) and pen spiro_pen (d >= 0). Petal-family knobs
	// are unused; the lobe count emerges from the R/r ratio.
	// SUPERFORMULA: simplified Gielis curve with lobe count super_lobes (m)
	// and fullness super_fullness. Petal-family knobs are unused.
	static TypedArray<Transform2D> helper_generate_transforms_flower(
			int transforms_amount,
			Transform2D marker_transform,
			int petals = 6,
			real_t radius = 150.0,
			real_t petal_spread = 0.5,
			real_t petal_sharpness = 1.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (exactly transforms_amount grid cells
			// inside the shape; spacing shrinks only when they do not fit) or Layers
			// (concentric rings sharing the slot count, layer 0 on outline);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// layer_count/layer_scale tune the concentric layers (LAYERS
			// placement); layer_side picks the growth side, layer_fill how
			// bullets are dealt across layers, layer_start_offset which
			// layer sequential filling starts from (wraps, sequential only).
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			// Bloom-kind selector + per-kind knobs (append-only; old calls are
			// unaffected). Out-of-range flower_type is rejected loudly.
			int flower_type = 0,
			double inner_radius_scale = 0.0,
			double spiro_roller = 45.0,
			double spiro_pen = 80.0,
			double super_lobes = 6.0,
			double super_fullness = 1.0,
			int layer_layout = 1);

	// True ellipse ring with independent radii and rotation (the ring
	// helper's y_scale is only an approximation): rx/ry semi-axes rotated by
	// ellipse_rotation. mode picks FULL ring, ARC segment, or WALL (arc with
	// gap_count dodge gaps of gap_width radians each; exactly
	// transforms_amount slots shared by the solid stretches). An ARC/WALL
	// spanning a full turn closes like FULL (no doubled seam bullet).
	static TypedArray<Transform2D> helper_generate_transforms_ellipse(
			int transforms_amount,
			Transform2D marker_transform,
			real_t radius_x = 150.0,
			real_t radius_y = 100.0,
			real_t ellipse_rotation = 0.0,
			real_t start_angle = 0.0,
			real_t arc = Math::TAU,
			EllipseMode mode = ELLIPSE_FULL,
			int gap_count = 2,
			real_t gap_width = 0.3,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (exactly transforms_amount grid cells
			// inside the shape; spacing shrinks only when they do not fit) or Layers
			// (concentric rings sharing the slot count, layer 0 on outline);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// layer_count/layer_scale tune the concentric layers (LAYERS
			// placement); layer_side picks the growth side, layer_fill how
			// bullets are dealt across layers, layer_start_offset which
			// layer sequential filling starts from (wraps, sequential only).
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			int layer_layout = 1);

	// Rain curtain: slots spread along a horizontal band of band_width above
	// (or around) the marker, facing rain_direction. drop_spacing staggers
	// rows so the curtain reads as layered sheets instead of one flat row.
	// seed drives the jitter (0 = non-deterministic).
	static TypedArray<Transform2D> helper_generate_transforms_rain(
			int transforms_amount,
			Transform2D marker_transform,
			real_t band_width = 600.0,
			Vector2 rain_direction = Vector2(0, 1),
			real_t drop_spacing = 48.0,
			real_t jitter = 12.0,
			uint64_t seed = 0);

	// Scatter burst: biased-random disc for explosions, boss deaths, petal
	// pops. Offsets fill the disc of burst_radius (sqrt distribution, so
	// density is even, not center-clumped); facings are radial-outward plus
	// facing_jitter. seed = 0 means non-deterministic, otherwise reproducible.
	static TypedArray<Transform2D> helper_generate_transforms_scatter(
			int transforms_amount,
			Transform2D marker_transform,
			real_t burst_radius = 120.0,
			real_t facing_jitter = 0.4,
			uint64_t seed = 0,
			real_t inner_radius = 0.0,
			Vector2 sector_direction = Vector2(1, 0),
			real_t sector_arc = Math::TAU,
			ScatterFacingMode facing_mode = SCATTER_FACING_OUTWARD);

	// Star/polygon emphasis: vertices symmetric directions around the marker
	// with extra density pulled toward each vertex (vertex_bias 0 = even
	// ring, higher = sharper star). edges bullets per edge fill the spans.
	static TypedArray<Transform2D> helper_generate_transforms_star_polygon(
			int transforms_amount,
			Transform2D marker_transform,
			int vertices = 5,
			real_t radius = 150.0,
			real_t vertex_bias = 2.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0);

	// Multi-arm spiral: arms interleaved arms around the marker (galaxy,
	// windmill, rose looks at low bullet counts). arm_index_stride lets
	// callers interleave (1) or group (arms) consecutive slots per arm.
	static TypedArray<Transform2D> helper_generate_transforms_multispiral(
			int transforms_amount,
			Transform2D marker_transform,
			int arms = 3,
			real_t start_radius = 50.0,
			real_t radius_step = 15.0,
			real_t angle_step = 0.6,
			bool rotate_with_marker = true,
			SpiralFacingMode facing_mode = SPIRAL_FACING_TANGENT,
			real_t facing_offset_degrees = 0.0,
			int arm_index_stride = 1);

	// Cross/plus barrage: arm_count rays of evenly spaced slots from the
	// marker out to arm_length. Crossfire and plus-shaped
	// danmaku bursts.
	static TypedArray<Transform2D> helper_generate_transforms_cross(
			int transforms_amount,
			Transform2D marker_transform,
			int arm_count = 4,
			real_t arm_length = 150.0,
			real_t spacing = 32.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0);

	// True star shell: points alternating outer/inner vertices around the
	// marker (distinct from the polygon helper's density bias). Boss star
	// bursts and celebratory shells.
	static TypedArray<Transform2D> helper_generate_transforms_star(
			int transforms_amount,
			Transform2D marker_transform,
			int points = 5,
			real_t outer_radius = 150.0,
			real_t inner_radius = 65.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (exactly transforms_amount grid cells
			// inside the shape; spacing shrinks only when they do not fit) or Layers
			// (concentric rings sharing the slot count, layer 0 on outline);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// layer_count/layer_scale tune the concentric layers (LAYERS
			// placement); layer_side picks the growth side, layer_fill how
			// bullets are dealt across layers, layer_start_offset which
			// layer sequential filling starts from (wraps, sequential only).
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			int outline_distribution = 1,
			int layer_layout = 1,
			int outline_corner_priority = 0,
			int outline_corner_mode = 0,
			double outline_edge_margin = 0.0,
			int outline_corner_facing = 0);

	// Heart bloom: parametric heart outline (boss love attacks, endings).
	// size scales the classic 16sin^3 / 13cos-5cos2t curve. Full
	// outline-layout support (on outline, concentric layers, interior fill);
	// layer offsets run along center-radial normals, facings stay radial
	// like the legacy loop.
	static TypedArray<Transform2D> helper_generate_transforms_heart(
			int transforms_amount,
			Transform2D marker_transform,
			real_t size = 150.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			int layer_layout = 1);

	// Snake row: slots along a sine wave of width, amplitude and wave count
	// around the marker axis (direction need not be normalized). Pairs with
	// wobble flight for slithering curtains.
	static TypedArray<Transform2D> helper_generate_transforms_wave(
			int transforms_amount,
			Transform2D marker_transform,
			real_t width = 600.0,
			real_t amplitude = 48.0,
			real_t waves = 2.0,
			Vector2 direction = Vector2(1, 0),
			bool face_direction = true,
			real_t facing_offset_degrees = 0.0);

	// Waterfall curtain: staggered rows x columns grid with per-row stagger
	// offsets and jitter (danmaku curtains with readable doors when combined
	// with skip_indices). Fires along rain_direction.
	// seed drives the jitter (0 = non-deterministic).
	static TypedArray<Transform2D> helper_generate_transforms_waterfall(
			int transforms_amount,
			Transform2D marker_transform,
			int columns = 12,
			real_t column_spacing = 48.0,
			int rows = 3,
			real_t row_spacing = 64.0,
			real_t stagger = 0.5,
			Vector2 rain_direction = Vector2(0, 1),
			real_t jitter = 6.0,
			real_t facing_offset_degrees = 0.0,
			uint64_t seed = 0);

	// Lattice honeycomb: staggered hex-style rows for honeycomb walls.
	static TypedArray<Transform2D> helper_generate_transforms_lattice(
			int transforms_amount,
			Transform2D marker_transform,
			int columns = 8,
			int rows = 5,
			real_t spacing_x = 48.0,
			real_t spacing_y = 42.0,
			bool stagger_rows = true,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0);

	// Mathematical rose for petal-storm/blossom-finales: exact rhodonea rose
	// r = R * cos(k * theta); k = petals; dense slot sweep theta = i / n * TAU.
	static TypedArray<Transform2D> helper_generate_transforms_rose(
			int transforms_amount,
			Transform2D marker_transform,
			int petals = 6,
			real_t radius = 150.0,
			real_t lobe_sharpness = 1.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (exactly transforms_amount grid cells
			// inside the shape; spacing shrinks only when they do not fit) or Layers
			// (concentric rings sharing the slot count, layer 0 on outline);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// layer_count/layer_scale tune the concentric layers (LAYERS
			// placement); layer_side picks the growth side, layer_fill how
			// bullets are dealt across layers, layer_start_offset which
			// layer sequential filling starts from (wraps, sequential only).
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			int layer_layout = 1);

	// Twin counter-rotating galaxy: odd arms wind -angle_step, even arms
	// +angle_step when mirrored (same facing switch as multispiral).
	static TypedArray<Transform2D> helper_generate_transforms_counter_spiral(
			int transforms_amount,
			Transform2D marker_transform,
			int arms = 2,
			real_t start_radius = 50.0,
			real_t radius_step = 15.0,
			real_t angle_step = 0.6,
			bool rotate_with_marker = true,
			SpiralFacingMode facing_mode = SPIRAL_FACING_TANGENT,
			real_t facing_offset_degrees = 0.0,
			int arm_index_stride = 1,
			bool mirror_alternate_arms = true);

	// Dense wall perpendicular to aim with carved center dodge door.
	// Aimed-trap usage: slots spread across width on the axis across from
	// aim_direction; the center gap of gap_width stays empty as the door.
	// Exactly transforms_amount slots: they are split over the two wall
	// segments (outer edge to door edge). spacing is reserved (unused).
	static TypedArray<Transform2D> helper_generate_transforms_corridor(
			int transforms_amount,
			Transform2D marker_transform,
			const Vector2 &aim_direction,
			real_t width = 400.0,
			real_t spacing = 32.0,
			real_t gap_width = 96.0,
			bool face_aim = true,
			real_t facing_offset_degrees = 0.0);

	// Figure-8/weave openings: slots sweep t = i / n * TAU over the
	// lissajous curve for weaving curtains with readable doors.
	static TypedArray<Transform2D> helper_generate_transforms_lissajous(
			int transforms_amount,
			Transform2D marker_transform,
			real_t size_x = 200.0,
			real_t size_y = 120.0,
			real_t freq_x = 3.0,
			real_t freq_y = 2.0,
			real_t phase = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (exactly transforms_amount grid cells
			// inside the shape; spacing shrinks only when they do not fit) or Layers
			// (concentric rings sharing the slot count, layer 0 on outline);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// layer_count/layer_scale tune the concentric layers (LAYERS
			// placement); layer_side picks the growth side, layer_fill how
			// bullets are dealt across layers, layer_start_offset which
			// layer sequential filling starts from (wraps, sequential only).
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			int layer_layout = 1);

	// Clean circle outline: transforms_amount slots evenly on a radius
	// circle around the marker, facing outward (or inward). The Ring helper
	// covers arcs; this is the exact full-loop shape primitive.
	static TypedArray<Transform2D> helper_generate_transforms_circle(
			int transforms_amount,
			Transform2D marker_transform,
			real_t radius = 150.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (exactly transforms_amount grid cells
			// inside the shape; spacing shrinks only when they do not fit) or Layers
			// (concentric rings sharing the slot count, layer 0 on outline);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// layer_count/layer_scale tune the concentric layers (LAYERS
			// placement); layer_side picks the growth side, layer_fill how
			// bullets are dealt across layers, layer_start_offset which
			// layer sequential filling starts from (wraps, sequential only).
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			int layer_layout = 1);

	// Rectangle perimeter: slots walk the outline of a size-sized box
	// centered on the marker (counter-clockwise from top-left), facing
	// outward (or inward). Square = size with equal sides.
	static TypedArray<Transform2D> helper_generate_transforms_rectangle(
			int transforms_amount,
			Transform2D marker_transform,
			const Vector2 &size = Vector2(300, 200),
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (exactly transforms_amount grid cells
			// inside the shape; spacing shrinks only when they do not fit) or Layers
			// (concentric rings sharing the slot count, layer 0 on outline);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// layer_count/layer_scale tune the concentric layers (LAYERS
			// placement); layer_side picks the growth side, layer_fill how
			// bullets are dealt across layers, layer_start_offset which
			// layer sequential filling starts from (wraps, sequential only).
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			int outline_distribution = 1,
			int layer_layout = 1,
			int outline_corner_priority = 0,
			int outline_corner_mode = 0,
			double outline_edge_margin = 0.0,
			int outline_corner_facing = 0);

	// Polygon perimeter: vertices corners on a radius circle from
	// base_rotation, slots spread evenly by arc length along the outline,
	// facing outward (or inward).
	static TypedArray<Transform2D> helper_generate_transforms_polygon(
			int transforms_amount,
			Transform2D marker_transform,
			int vertices = 6,
			real_t radius = 150.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (exactly transforms_amount grid cells
			// inside the shape; spacing shrinks only when they do not fit) or Layers
			// (concentric rings sharing the slot count, layer 0 on outline);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// layer_count/layer_scale tune the concentric layers (LAYERS
			// placement); layer_side picks the growth side, layer_fill how
			// bullets are dealt across layers, layer_start_offset which
			// layer sequential filling starts from (wraps, sequential only).
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			int outline_distribution = 1,
			int layer_layout = 1,
			int outline_corner_priority = 0,
			int outline_corner_mode = 0,
			double outline_edge_margin = 0.0,
			int outline_corner_facing = 0);

	// Triangle perimeter: equilateral (circumradius size_a), isosceles
	// (base size_a + height size_b, apex up) or right-angled (legs size_a
	// along X and size_b along Y, recentered), all rotated by rotation and
	// centered on the marker. Slots walk the outline evenly by arc length,
	// facing outward (or inward). Degenerate (zero-area) triangles stack at
	// the marker like the rectangle primitive.
	static TypedArray<Transform2D> helper_generate_transforms_triangle(
			int transforms_amount,
			Transform2D marker_transform,
			TriangleType triangle_type = TRIANGLE_EQUILATERAL,
			real_t size_a = 150.0,
			real_t size_b = 150.0,
			real_t rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (exactly transforms_amount grid cells
			// inside the shape; spacing shrinks only when they do not fit) or Layers
			// (concentric rings sharing the slot count, layer 0 on outline);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// layer_count/layer_scale tune the concentric layers (LAYERS
			// placement); layer_side picks the growth side, layer_fill how
			// bullets are dealt across layers, layer_start_offset which
			// layer sequential filling starts from (wraps, sequential only).
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			int outline_distribution = 1,
			int layer_layout = 1,
			int outline_corner_priority = 0,
			int outline_corner_mode = 0,
			double outline_edge_margin = 0.0,
			int outline_corner_facing = 0);

	// Isosceles trapezoid perimeter: bases base_top/base_bottom with height,
	// centered on the marker and rotated by rotation. Slots walk the outline
	// evenly by arc length, facing outward (or inward). A zero top base
	// degrades gracefully to a triangle; zero-area boxes stack at the marker.
	static TypedArray<Transform2D> helper_generate_transforms_trapezoid(
			int transforms_amount,
			Transform2D marker_transform,
			real_t base_top = 200.0,
			real_t base_bottom = 300.0,
			real_t height = 200.0,
			real_t rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (exactly transforms_amount grid cells
			// inside the shape; spacing shrinks only when they do not fit) or Layers
			// (concentric rings sharing the slot count, layer 0 on outline);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// layer_count/layer_scale tune the concentric layers (LAYERS
			// placement); layer_side picks the growth side, layer_fill how
			// bullets are dealt across layers, layer_start_offset which
			// layer sequential filling starts from (wraps, sequential only).
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			int outline_distribution = 1,
			int layer_layout = 1,
			int outline_corner_priority = 0,
			int outline_corner_mode = 0,
			double outline_edge_margin = 0.0,
			int outline_corner_facing = 0);

	// Diamond (rhombus) perimeter: diagonals diagonal_x/diagonal_y, centered
	// on the marker and rotated by rotation. Slots walk the outline evenly
	// by arc length, facing outward (or inward). Zero-area diamonds stack at
	// the marker.
	static TypedArray<Transform2D> helper_generate_transforms_diamond(
			int transforms_amount,
			Transform2D marker_transform,
			real_t diagonal_x = 200.0,
			real_t diagonal_y = 300.0,
			real_t rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (exactly transforms_amount grid cells
			// inside the shape; spacing shrinks only when they do not fit) or Layers
			// (concentric rings sharing the slot count, layer 0 on outline);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// layer_count/layer_scale tune the concentric layers (LAYERS
			// placement); layer_side picks the growth side, layer_fill how
			// bullets are dealt across layers, layer_start_offset which
			// layer sequential filling starts from (wraps, sequential only).
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int layer_count = 1,
			double layer_scale = 0.2,
			int layer_side = 0,
			int layer_fill = 0,
			int layer_start_offset = 0,
			int layer_scale_curve = 0,
			const PackedFloat32Array &layer_custom_scales = PackedFloat32Array(),
			int layer_twist = 0,
			int layer_max_dots = 0,
			int outline_distribution = 1,
			int layer_layout = 1,
			int outline_corner_priority = 0,
			int outline_corner_mode = 0,
			double outline_edge_margin = 0.0,
			int outline_corner_facing = 0);

	// Universal side pass for closed-outline patterns: returns a copy of
	// transforms with each origin pushed along its own facing by
	// spread * pow(rand, spread_exponent). side: 0 = identity copy,
	// 1 = outward (+facing), 2 = inward (-facing), 3 = random side per
	// bullet. seed = 0 means non-deterministic. Facings never change.
	static TypedArray<Transform2D> helper_apply_side_spread(
			const TypedArray<Transform2D> &transforms,
			int side_mode = 0,
			real_t spread = 0.0,
			real_t spread_exponent = 2.0,
			uint64_t seed = 0);

	// Scale factor for one outline layer: how much bigger (or smaller) ring
	// layer_index is than the base outline (1.0 for layer 0). A non-empty
	// custom_scales list wins outright (entry layer_index % size); otherwise
	// Outward grows (linearly or compounded per scale_curve), Inward crowds
	// toward the center reciprocally, Both alternates. Single source of
	// truth shared by the generators and the preview, so rings can never
	// disagree with bullets. Bad inputs warn and yield 1.0.
	static double helper_layer_scale_factor(
			int layer_index,
			double scale_step,
			int side = 0,
			int scale_curve = 0,
			const PackedFloat32Array &custom_scales = PackedFloat32Array());

	// Closed outline shapes addressable by the debug inspectors below. Values
	// mirror BulletSpawner2D::PatternSource for these shapes only.
	enum DebugOutlineShape {
		DEBUG_SHAPE_CIRCLE = 0,
		DEBUG_SHAPE_RING = 1,
		DEBUG_SHAPE_ELLIPSE = 2,
		DEBUG_SHAPE_RECTANGLE = 3,
		DEBUG_SHAPE_SQUARE = 4,
		DEBUG_SHAPE_POLYGON = 5,
		DEBUG_SHAPE_TRIANGLE = 6,
		DEBUG_SHAPE_TRAPEZOID = 7,
		DEBUG_SHAPE_DIAMOND = 8,
		DEBUG_SHAPE_STAR = 9,
		DEBUG_SHAPE_HEART = 10,
		DEBUG_SHAPE_FLOWER = 11,
		DEBUG_SHAPE_ROSE = 12,
		DEBUG_SHAPE_LISSAJOUS = 13
	};

	// Marker-local outline report for one closed shape: rebuilds the exact
	// slot loop the volley generators use (same builders, same knobs) and
	// returns {ok, error, points, facings, edge_ids, corner_flags, gaps,
	// settings}. points/facings are marker-local (marker at origin, identity
	// rotation); gaps holds consecutive origin distances including the seam
	// closure; corner_flags marks corner-seated slots; settings echoes the
	// resolved distribution/priority/mode/margin/layout values. params
	// carries shape knobs as documented per shape (missing keys fall back to
	// the helper defaults); unrecognized shapes return ok=false. Pure math:
	// no tree, no RNG beyond the caller's seed entries.
	static Dictionary debug_describe_outline(int shape, int count, const Dictionary &params = Dictionary());

	// Shape-agnostic volley metrics over an emitted transform array (e.g. a
	// helper_generate_transforms_* result): {count, gaps, min_gap, max_gap,
	// mean_gap, gap_ratio (max/min, 1.0 = perfectly even), seam_gap}. Gaps
	// are consecutive origin distances in volley order plus the last->first
	// closure. Works per ring too (pass one ring's transforms).
	static Dictionary debug_volley_gaps(const TypedArray<Transform2D> &volley);

	// Mathematical conformance check: regenerates the expected marker-local
	// loop via debug_describe_outline and compares every volley slot
	// (origins within tolerance_px after the marker transform, facings
	// within tolerance_rad). Covers ON_OUTLINE placement (single ring).
	// Returns {ok, checked, worst_pos_px, worst_face_rad, bad_index}.
	static Dictionary debug_verify_volley(const TypedArray<Transform2D> &volley, int shape, const Transform2D &marker, int count, const Dictionary &params = Dictionary(), double tolerance_px = 1.0, double tolerance_rad = 0.02);

	// Per-edge dot quotas plus an optimality verdict against the
	// length-proportional largest-remainder optimum: every edge must sit
	// within < 1 slot of its exact share (both LEGACY and SYMMETRIC satisfy
	// this; they differ only in tie-breaks). Returns {ok, edge_counts,
	// interiors, exact_shares, optimal, worst_pair_spread (opposite-edge
	// equality on even corner counts), settings}. Corner-anchored polygonal
	// shapes only; anything else reports ok=false.
	static Dictionary debug_outline_quotas(int shape, int count, const Dictionary &params = Dictionary());

	// Edge normals for a polyline: per-point outward normal from the local
	// tangent (segment perpendicular, averaged at joints). tangent (1,0)
	// yields normal (0,-1) (up in Godot 2D). flip negates every normal.
	// Returns an empty array with an error when fewer than 1 point is given.
	static PackedVector2Array helper_compute_edge_normals(
			const PackedVector2Array &edge_points,
			bool closed = false,
			bool flip = false);

	// Terrain-edge emitter: slots sampled along a polyline edge (local to
	// the marker), each facing along the edge normal. Even sampling spreads
	// uniformly by arc length (open polylines include both endpoints);
	// random sampling picks uniform arc positions (seed = 0 means
	// non-deterministic). jitter scatters origins in a disc of that radius.
	// Normal convention: the normal is the tangent rotated by orthogonal()
	// (for a left-to-right polyline the normals point UP, -Y); flip swaps
	// the side. spread adds a one-sided normal-direction falloff cloud (the
	// terrain crest look): offset = spread * pow(rand, spread_exponent)
	// along the normal. spread_side: 0 = along +normal, 1 = behind
	// (-normal), 2 = random side per bullet. tangent_jitter scatters along
	// the local tangent (softens the crest core, works with or without
	// spread). Spread defaults (0) preserve the crest-only behavior exactly.
	static TypedArray<Transform2D> helper_generate_transforms_edge_from_points(
			int transforms_amount,
			Transform2D marker_transform,
			const PackedVector2Array &edge_points,
			bool closed = false,
			bool flip_normals = false,
			bool random_sample = false,
			real_t jitter = 0.0,
			real_t facing_offset_degrees = 0.0,
			uint64_t seed = 0,
			real_t spread = 0.0,
			real_t spread_exponent = 2.0,
			int spread_side = 0,
			real_t tangent_jitter = 0.0);

	// Bitmap edge extraction: opaque pixels (alpha >= threshold) with a
	// transparent/out-of-bounds 4-neighbor, sampled every step pixels.
	// Points are centered (texture center = local origin); normals point
	// outward (toward transparency). Returns {"points", "normals"}.
	// quiet = true suppresses errors (editor preview / cache probes).
	// Images larger than 2048x2048 are rejected to avoid editor stalls.
	static Dictionary helper_extract_edge_from_image(
			const Ref<Image> &image,
			real_t threshold = 0.5,
			int step = 4,
			bool quiet = false);

	// Closed-loop track samplers for the editor preview: marker-local loop
	// points plus a closed flag, bundled as {"points","closed"}. Densities
	// are fixed and adaptive (never bullet-count dependent); polygons return
	// exact corners. Invalid input yields an empty track.
	static Dictionary helper_sample_outline_rose(int petals = 6, real_t radius = 150.0, real_t lobe_sharpness = 1.0, real_t base_rotation = 0.0);
	// Flower bloom track sampler mirroring helper_generate_transforms_flower
	// per-type math at fixed density (closed loop). Marker-relative offsets.
	static Dictionary helper_sample_outline_flower(int flower_type = 0, int petals = 6, real_t radius = 150.0, real_t petal_spread = 0.5, real_t petal_sharpness = 1.0, double inner_radius_scale = 0.0, double spiro_roller = 45.0, double spiro_pen = 80.0, double super_lobes = 6.0, double super_fullness = 1.0, real_t base_rotation = 0.0, int transforms_amount = -1);
	static Dictionary helper_sample_outline_lissajous(real_t size_x = 200.0, real_t size_y = 120.0, real_t freq_x = 3.0, real_t freq_y = 2.0, real_t phase = 0.0);
	static Dictionary helper_sample_outline_circle(real_t radius = 150.0);
	static Dictionary helper_sample_outline_rectangle(const Vector2 &size = Vector2(300, 200));
	static Dictionary helper_sample_outline_triangle(int triangle_type = 0, real_t size_a = 150.0, real_t size_b = 150.0, real_t rotation = 0.0);
	static Dictionary helper_sample_outline_trapezoid(real_t base_top = 200.0, real_t base_bottom = 300.0, real_t height = 200.0, real_t rotation = 0.0);
	static Dictionary helper_sample_outline_diamond(real_t diagonal_x = 200.0, real_t diagonal_y = 300.0, real_t rotation = 0.0);
	static Dictionary helper_sample_outline_polygon(int vertices = 6, real_t radius = 150.0, real_t base_rotation = 0.0);
	static Dictionary helper_sample_outline_ellipse(real_t radius_x = 150.0, real_t radius_y = 100.0, real_t ellipse_rotation = 0.0, real_t start_angle = 0.0, real_t arc = Math::TAU, int mode = 0);
	static Dictionary helper_sample_outline_ring(real_t radius = 150.0, real_t arc = Math::TAU, real_t y_scale = 1.0, real_t start_angle_abs = 0.0);
	static Dictionary helper_sample_outline_star(int points = 5, real_t outer_radius = 150.0, real_t inner_radius = 65.0, real_t base_rotation = 0.0);
	// Grid-family row-strip track samplers: INF-separated multi-row strips
	// mirroring each generator's row loop. Fixed/adaptive density, never
	// bullet-count dependent. Invalid input yields an empty track.
	static Dictionary helper_sample_outline_grid(int transforms_amount = 0, int rows_per_column = 10, int alignment = 3, real_t column_offset = 150.0, real_t row_offset = 150.0, real_t base_rotation_abs = 0.0, bool rotate_with_marker = true);
	static Dictionary helper_sample_outline_lattice(int transforms_amount = 0, int columns = 4, int rows = 4, real_t spacing_x = 64.0, real_t spacing_y = 64.0, bool stagger_rows = true);
	static Dictionary helper_sample_outline_waterfall(int transforms_amount = 0, int columns = 4, real_t column_spacing = 64.0, int rows = 4, real_t row_spacing = 64.0, real_t stagger = 0.0, const Vector2 &rain_direction = Vector2(0, 1));
	static Dictionary helper_sample_outline_rain(int transforms_amount = 0, real_t band_width = 600.0, const Vector2 &rain_direction = Vector2(0, 1), real_t drop_spacing = 48.0);
	static Dictionary helper_sample_outline_wave(real_t width = 300.0, real_t amplitude = 50.0, real_t waves = 2.0, const Vector2 &direction = Vector2(1, 0));
	static Dictionary helper_sample_outline_heart(real_t size = 100.0, real_t base_rotation = 0.0);
	// Spiral arm-sweep track samplers: single or multi-arm strips (INF-
	// separated) mirroring each generator's (r, angle) formula.
	static Dictionary helper_sample_outline_spiral(int transforms_amount = 0, real_t start_radius = 50.0, real_t radius_step = 15.0, real_t angle_step = 0.6, real_t base_rotation_abs = 0.0);
	static Dictionary helper_sample_outline_multispiral(int transforms_amount = 0, int arms = 3, real_t start_radius = 50.0, real_t radius_step = 15.0, real_t angle_step = 0.6, real_t base_rotation_abs = 0.0, int arm_index_stride = 1);
	static Dictionary helper_sample_outline_counter_spiral(int transforms_amount = 0, int arms = 4, real_t start_radius = 50.0, real_t radius_step = 15.0, real_t angle_step = 0.6, real_t base_rotation_abs = 0.0, int arm_index_stride = 1, bool mirror_alternate_arms = true);

	// Negative space: drops slot indexes from a generated array (carve dodge
	// doors, write bullet text). Out-of-range entries are ignored with a
	// single warning; the output shrinks like ELLIPSE_WALL.
	static TypedArray<Transform2D> helper_apply_skip_indices(
			const TypedArray<Transform2D> &transforms,
			const PackedInt32Array &skip_indices);

	// Arc-length layout along a marker-local polyline (closed adds the
	// last-to-first stretch when it has length), tangent-first facing.
	static TypedArray<Transform2D> helper_generate_transforms_polyline(
			int transforms_amount,
			Transform2D marker_transform,
			const PackedVector2Array &points,
			bool closed = false,
			PolylineDistribution distribution = POLYLINE_DISTRIBUTION_EVEN,
			real_t spacing = 32.0,
			PolylineOverflow overflow = POLYLINE_OVERFLOW_CLAMP,
			PolylineAnchor anchor = POLYLINE_ANCHOR_START,
			real_t start_offset = 0.0,
			bool reverse = false,
			PolylineFacing facing = POLYLINE_FACING_ALONG_PATH,
			real_t facing_offset_deg = 0.0);

	// ---- Native cores (C++ only, not bound) ----
	// The implementation behind every helper_generate_transforms_* binding:
	// same arguments, validation and error texts, but a PatternSlots2D
	// result, so C++ callers (the spawner dispatch, the bake cache) pay no
	// Variant per bullet. The bindings wrap them (pattern_slots_to_array).
	static PatternSlots2D generate_aimed2d(int transforms_amount, Transform2D marker_transform, const AimedParams2D &params);
	static PatternSlots2D generate_circle2d(int transforms_amount, Transform2D marker_transform, const CircleParams2D &params);
	static PatternSlots2D generate_corridor2d(int transforms_amount, Transform2D marker_transform, const CorridorParams2D &params);
	static PatternSlots2D generate_counter_spiral2d(int transforms_amount, Transform2D marker_transform, const CounterSpiralParams2D &params);
	static PatternSlots2D generate_cross2d(int transforms_amount, Transform2D marker_transform, const CrossParams2D &params);
	static PatternSlots2D generate_diamond2d(int transforms_amount, Transform2D marker_transform, const DiamondParams2D &params);
	static PatternSlots2D generate_edge_from_points2d(int transforms_amount, Transform2D marker_transform, const EdgeFromPointsParams2D &params);
	static PatternSlots2D generate_ellipse2d(int transforms_amount, Transform2D marker_transform, const EllipseParams2D &params);
	static PatternSlots2D generate_fan2d(int transforms_amount, Transform2D marker_transform, const FanParams2D &params);
	static PatternSlots2D generate_flower2d(int transforms_amount, Transform2D marker_transform, const FlowerParams2D &params);
	static PatternSlots2D generate_grid2d(int transforms_amount, Transform2D marker_transform, const GridParams2D &params);
	static PatternSlots2D generate_heart2d(int transforms_amount, Transform2D marker_transform, const HeartParams2D &params);
	static PatternSlots2D generate_lattice2d(int transforms_amount, Transform2D marker_transform, const LatticeParams2D &params);
	static PatternSlots2D generate_line2d(int transforms_amount, Transform2D marker_transform, const LineParams2D &params);
	static PatternSlots2D generate_lissajous2d(int transforms_amount, Transform2D marker_transform, const LissajousParams2D &params);
	static PatternSlots2D generate_multispiral2d(int transforms_amount, Transform2D marker_transform, const MultispiralParams2D &params);
	static PatternSlots2D generate_polygon2d(int transforms_amount, Transform2D marker_transform, const PolygonParams2D &params);
	static PatternSlots2D generate_polyline2d(int transforms_amount, Transform2D marker_transform, const PolylineParams2D &params);
	static PatternSlots2D generate_rain2d(int transforms_amount, Transform2D marker_transform, const RainParams2D &params);
	static PatternSlots2D generate_rectangle2d(int transforms_amount, Transform2D marker_transform, const RectangleParams2D &params);
	static PatternSlots2D generate_ring2d(int transforms_amount, Transform2D marker_transform, const RingParams2D &params);
	static PatternSlots2D generate_rose2d(int transforms_amount, Transform2D marker_transform, const RoseParams2D &params);
	static PatternSlots2D generate_scatter2d(int transforms_amount, Transform2D marker_transform, const ScatterParams2D &params);
	static PatternSlots2D generate_spiral2d(int transforms_amount, Transform2D marker_transform, const SpiralParams2D &params);
	static PatternSlots2D generate_star2d(int transforms_amount, Transform2D marker_transform, const StarParams2D &params);
	static PatternSlots2D generate_star_polygon2d(int transforms_amount, Transform2D marker_transform, const StarPolygonParams2D &params);
	static PatternSlots2D generate_trapezoid2d(int transforms_amount, Transform2D marker_transform, const TrapezoidParams2D &params);
	static PatternSlots2D generate_triangle2d(int transforms_amount, Transform2D marker_transform, const TriangleParams2D &params);
	static PatternSlots2D generate_waterfall2d(int transforms_amount, Transform2D marker_transform, const WaterfallParams2D &params);
	static PatternSlots2D generate_wave2d(int transforms_amount, Transform2D marker_transform, const WaveParams2D &params);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::Alignment);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::SpiralFacingMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::ScatterFacingMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::LineAnchor);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::EllipseMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::TriangleType);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::PatternPreset);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::EdgeSpreadSide);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::SideMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::OutlinePlacement);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::OutlineDistribution);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::OutlineLayerLayout);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::OutlineCornerPriority);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::OutlineCornerMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::OutlineCornerFacing);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::DebugOutlineShape);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::OutlineLayerSide);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::OutlineLayerFill);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::OutlineLayerScaleCurve);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::OutlineFacing);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::FlowerBloom);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::PolylineDistribution);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::PolylineOverflow);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::PolylineAnchor);
VARIANT_ENUM_CAST(BlastBullets2D::BulletPatterns2D::PolylineFacing);
