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
#define PATTERN_GENERATOR(SHAPE, PARAMS) struct PARAMS;
#include "patterns/pattern_signatures2d.inc"
#undef PATTERN_GENERATOR

class BulletPatterns2D : public Object {
	GDCLASS(BulletPatterns2D, Object)

public:
	// The shape registry (patterns/pattern_registry2d.hpp) as data, in
	// inspector order: one Dictionary per pattern source with id, name,
	// knob_prefix ("" when it owns no knobs), outline, corners and
	// reads_external_state. Ids are BulletSpawner2D.pattern_source values.
	static TypedArray<Dictionary> get_shapes();

	// The per-call bullet limit (default 100000; project setting
	// blastbullets2d/patterns/max_bullets_per_pattern). Rejects values < 1
	// loudly and keeps the old one. Lowering it never touches amounts a
	// spawner already holds: they fail loud at the next generation.
	static void set_max_bullets_per_pattern(int p_limit);
	static int get_max_bullets_per_pattern();
	// Registers the project setting and applies its value (module init).
	static void register_project_settings();

	// Any pattern source by id (BulletSpawner2D.pattern_source, see
	// get_shapes) without a spawner: `params` names knobs exactly like the
	// spawner properties, with or without the helper_ prefix
	// ({"ring_radius": 80, "outline_placement": 1}), and they are checked
	// with the spawner's own rules and wording. Scene inputs come in as
	// params too: aim_position (Aimed, Corridor), path_points (Path2D,
	// marker-local), children (From Children, global transforms). Returns
	// the raw pattern a spawner at marker_transform would fire before its
	// pose (spin, pattern_scale, transforms_scale, muzzle offset), with
	// helper_skip_indices applied. Unknown params fail loud with a
	// did-you-mean and the rest still apply.
	static TypedArray<Transform2D> generate(int shape, int amount, const Transform2D &marker_transform, const Dictionary &params = Dictionary());

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
	// mean_gap, gap_ratio (max/min, 1.0 = perfectly even)}. Gaps are
	// consecutive origin distances in volley order; the last entry is the
	// last->first closure. Works per ring too (pass one ring's transforms).
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

	// ---- Generators: one pair per entry of patterns/pattern_signatures2d.inc ----
	// helper_generate_transforms_<shape> is the bound GDScript API; its
	// native core generate_<shape>2d (C++ only, patterns_<family>.cpp) takes
	// the same arguments as a <Shape>Params2D and returns a PatternSlots2D,
	// so C++ callers (the spawner dispatch, the bake cache) pay no Variant
	// per bullet. Same validation and error texts on both paths.
#define PATTERN_DECL_REQ(TYPE, NAME, FIELD) , TYPE NAME
#define PATTERN_DECL_OPT(TYPE, NAME, DEFAULT, FIELD) , TYPE NAME = DEFAULT
#define PATTERN_GENERATOR(SHAPE, PARAMS)                                                                                                                                             \
	static TypedArray<Transform2D> helper_generate_transforms_##SHAPE(int transforms_amount, Transform2D marker_transform PATTERN_ARGS_##SHAPE(PATTERN_DECL_REQ, PATTERN_DECL_OPT)); \
	static PatternSlots2D generate_##SHAPE##2d(int transforms_amount, Transform2D marker_transform, const PARAMS &params);
#include "patterns/pattern_signatures2d.inc"
#undef PATTERN_GENERATOR
#undef PATTERN_DECL_REQ
#undef PATTERN_DECL_OPT

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
