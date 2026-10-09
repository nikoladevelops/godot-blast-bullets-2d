#pragma once

// Helpers shared by the BulletPatterns2D implementation files (file map in
// patterns/bullet_patterns2d.hpp): tiny helpers inline, the rest declared.
// Only src/patterns/*.cpp include this; everything else includes
// patterns/bullet_patterns2d.hpp.

#include "core/transform_math2d.hpp"
#include "core/warn_once2d.hpp"
#include "godot_cpp/classes/image.hpp"
#include "godot_cpp/classes/random_number_generator.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/math_defs.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "patterns/bullet_patterns2d.hpp"
#include "patterns/pattern_inputs2d.hpp"
#include "patterns/pattern_params2d.hpp"
#include "patterns/pattern_slots2d.hpp"
#include <algorithm>
#include <cstdint>
#include <godot_cpp/variant/utility_functions.hpp>

namespace BlastBullets2D {
using namespace godot;

// Knobs of the polyline layout (ids = BulletPatterns2D::Polyline* enums).
struct PolylineLayout2D {
	bool closed = false;
	int distribution = 1;
	double spacing = 32.0;
	int overflow = 0;
	int anchor = 0;
	double start_offset = 0.0;
	bool reverse = false;
	int facing = 0;
	double facing_offset_deg = 0.0;
};
// Shared by helper_generate_transforms_polyline and the spawner's Path2D
// mode. caller prefixes every error ("<caller>: ...").
// Follow Generator (Path2D mode): the curve SHAPE is resettled with its
// bounding-box center at the origin, so it blooms around the generator
// wherever it was drawn. Non-finite centers leave the points unchanged.
PackedVector2Array recenter_polyline2d(const PackedVector2Array &pts);
PatternSlots2D polyline_layout2d(const Transform2D &marker, const PackedVector2Array &path_pts, int count, const PolylineLayout2D &p, bool quiet, const char *caller);

// Layout formulas shared by a generator and its preview track, so the track
// always draws where the bullets sit (invariant 4, pinned for every source
// by spawner/test_spawner_preview_track_coincidence.gd).

// Rain: drops per row (band_width / drop_spacing gaps + 1, capped by the
// amount; one row when the spacing is 0).
static inline int rain_columns2d(int amount, real_t band_width, real_t drop_spacing) {
	if (amount <= 0) {
		return 1;
	}
	return (drop_spacing > 0.0)
			? Math::clamp((int)Math::floor(band_width / Math::max((real_t)drop_spacing, (real_t)1.0)) + 1, 1, amount)
			: amount;
}
// Rain: drop i's offset across the band. Each row (the last, partial one
// included) spreads its own drops over the full band width.
static inline real_t rain_along2d(int i, int amount, int cols, real_t band_width) {
	const int row = i / cols;
	const int col = i % cols;
	const int in_row = Math::min(cols, amount - row * cols);
	return (in_row > 1) ? (band_width * (real_t)col / (real_t)(in_row - 1) - band_width * 0.5) : (real_t)0.0;
}

// Cross: bullets go round-robin over the arms (i % arm_count), ceil(amount /
// arms) deep; the step compresses so the deepest bullet sits at arm_length
// when the spacing would overshoot it.
static inline real_t cross_step2d(int amount, int arm_count, real_t arm_length, real_t spacing) {
	const int per_arm = Math::max(1, (int)Math::ceil((double)amount / (double)Math::max(arm_count, 1)));
	return ((real_t)per_arm * spacing > arm_length) ? arm_length / (real_t)per_arm : spacing;
}
// Cross: how many bullets arm `arm` holds.
static inline int cross_arm_bullets2d(int amount, int arm_count, int arm) {
	return (arm < amount) ? (amount - arm + arm_count - 1) / arm_count : 0;
}

// Every helper_generate_transforms_* call allocates O(n) slots, so an
// unbounded count (a typo'd 1000000000, let alone INT_MAX) would freeze or
// OOM the game: they all share the developer-set pattern_max_bullets() with
// the spawner's helper_bullets_amount (the spawner fans its amount straight
// into these generators).

// Clamp finished slot arrays into a sane world box: huge-but-finite inputs
// (1e30 spacing on an 8k volley) overflow slot math to Inf/NaN, which would
// poison the whole volley downstream. Slots that blew out land at the clamped
// edge, valid slots pass through untouched. Warns once when it fires.
void danmaku_clamp_slots_finite(const char *caller_name, PatternSlots2D &slots); // pattern_layout2d.cpp

double outline_point_seg_dist(const Vector2 &p, const Vector2 &a, const Vector2 &b); // pattern_layout2d.cpp

// Angular-sorted silhouette of a slot cloud around its average: recovers a
// clean simple polygon for star-shaped outlines (star/flower/rose loops),
// approximates one for self-intersecting weaves. Consecutive near-dupes
// (repeated star vertices, closed-curve seams) collapse. False when no
// usable interior exists.
bool outline_build_boundary(const PackedVector2Array &points, PackedVector2Array &r_boundary, Vector2 &r_center); // pattern_layout2d.cpp

// Even arc-length resample of a slot-space polyline into m points (plus
// interpolated parallel normals/override facings). Closed loops wrap around
// (no seam duplicate, so decimated subsets can never strand a 1-step seam
// gap next to bullet 0); open polylines pin both endpoints. Override angles
// interpolate along the shortest arc. Degenerate input yields copies of the
// first point so callers always get exactly m outputs.
void resample_loop_even(const PackedVector2Array &pts, const PackedVector2Array &nrms, const PackedFloat32Array &ovr, int m, bool closed, PackedVector2Array &r_pts, PackedVector2Array &r_nrms, PackedFloat32Array &r_ovr, double phase = 0.0); // pattern_layout2d.cpp

// `avoid_radius` > 0 also keeps every slot that far from `avoid_point`
// (layer rings scale about it: a slot there would stack on every ring).
void resample_loop_even_distinct(const PackedVector2Array &pts, const PackedVector2Array &nrms, const PackedFloat32Array &ovr, int m, bool closed, PackedVector2Array &r_pts, PackedVector2Array &r_nrms, PackedFloat32Array &r_ovr, const Vector2 &avoid_point = Vector2(), real_t avoid_radius = 0.0); // pattern_layout2d.cpp

// Dense outline for Fill Inside / Layers: marker-relative sweep points
// shifted by `origin`, or nothing for On Outline (unused there).
PackedVector2Array fill_outline_from(int outline_placement, const PackedVector2Array &local_pts, const Vector2 &origin); // pattern_layout2d.cpp

PatternSlots2D layout_outline_slots(
		const char *caller_name,
		const Transform2D &marker_transform,
		const PackedVector2Array &points,
		const PackedVector2Array &normals,
		bool points_are_local,
		real_t rot_add,
		bool face_outward,
		real_t facing_offset_degrees,
		const PackedFloat32Array &facing_override,
		// The user's outline knobs (placement, slot order, fill, layers).
		const OutlineLayout2D &outline,
		// Corner anchoring: the polygon loops pass their knobs, curves
		// pass CornerLayout2D::smooth().
		const CornerLayout2D &corner,
		// Corner points of a polygon loop (empty for curves).
		const PackedVector2Array &polygon_corners,
		bool loop_closed,
		bool allow_resample,
		// Dense ideal curve (Fill / Layers): Fill builds the interior from
		// it and Layers resample each ring from it, so both follow the true
		// curve at any bullet count. Empty = slot loop only.
		const PackedVector2Array &dense_outline = PackedVector2Array(),
		const PackedVector2Array &dense_normals = PackedVector2Array(),
		const PackedFloat32Array &dense_overrides = PackedFloat32Array()); // pattern_layout2d.cpp

// Generator heads (pattern_layout2d.cpp), each fails loud once with
// "<caller>: ..." and returns false. danmaku_validate_head = amount, then the
// marker (finite + invertible: the outline layout inverts it).
bool pattern_check_amount(const char *caller_name, int transforms_amount);
bool pattern_check_marker(const char *caller_name, const Transform2D &marker_transform, bool require_invertible);
bool danmaku_validate_head(const char *caller_name, int transforms_amount, const Transform2D &marker_transform);

// Generator rejection: pushes "<caller>: <tail>" once and returns an empty
// result. Each generator names itself once (`const char *caller = "helper_
// generate_transforms_<shape>";`); the message is only built on failure.
void pattern_reject(const char *caller_name, const char *tail);
// The six corner-layout knob checks of the polygon loops, in their pinned
// order: outline_distribution, layer_layout, corner priority, mode, edge
// margin, corner facing.
bool pattern_check_corner_layout(const char *caller_name, const CornerLayout2D &corner, int layer_layout);
// Returns an empty result when a check (which already failed loud) is false.
#define PATTERN_REQUIRE(CHECK)   \
	if (!(CHECK)) {              \
		return PatternSlots2D(); \
	}
#define PATTERN_REJECT_IF(COND, TAIL) \
	if (COND) {                       \
		pattern_reject(caller, TAIL); \
		return PatternSlots2D();      \
	}

inline PatternSlots2D danmaku_make_slots(int transforms_amount) {
	return PatternSlots2D((size_t)(transforms_amount > 0 ? transforms_amount : 0));
}

inline void danmaku_apply_marker_scale(Transform2D &slot, const Transform2D &marker_transform) {
	slot.set_scale(marker_transform.get_scale());
}

// Revolutions a hypotrochoid (spirograph) needs to close: the smallest positive
// integer m such that k*m is (approximately) an integer, where k = (R - r)/r.
// The outer term repeats every 2pi; the inner term repeats every 2pi/k; both
// align only after m full revolutions. Caps at max_m for irrational ratios,
// which never truly close (the preview then shows an open approximation).
// Dense sample count for a spirograph sweep of `revolutions` turns. Shared
// by the generator and the preview track so dots sit on the drawn curve.
inline int spirograph_dense_samples(int revolutions) {
	return Math::clamp(720 * revolutions, 720, 4096);
}

int spirograph_revolutions(double k, int max_m = 64); // patterns_curves.cpp

// FAN flower: how many of `amount` bullets petal `petal` holds. Even split
// with the remainder spread symmetrically around the bloom (never all on
// the first petals); amounts below the petal count light evenly spaced
// petals. Shared by the generator and the preview track.
inline int flower_fan_petal_count(int petal, int petals, int amount) {
	if (petals <= 0 || amount <= 0) {
		return 0;
	}
	return (int)(((int64_t)(petal + 1) * amount) / petals - ((int64_t)petal * amount) / petals);
}

// Arm and step of spiral slot i. Interleave (stride < arms) deals slots
// round robin; grouping (stride >= arms) gives each arm runs of
// g = stride / arms + 1 consecutive slots. Always one-to-one: no two slots
// share an (arm, step) pair, so no bullet hides under another.
void spiral_arm_step(int i, int arms, int stride, int &r_arm, int &r_step); // patterns_shapes.cpp

// Outward edge normal for the directed edge a -> b, oriented against ref
// (a corner-averaged normal): falls back to a zero vector when degenerate,
// letting callers substitute their own fallback.
Vector2 oriented_edge_normal(const Vector2 &a, const Vector2 &b, const Vector2 &ref); // patterns_polygons.cpp

// True when the directed edge a -> b runs more along X than Y (horizontal-ish
// in shape-local space). Used by corner priority: HORIZONTAL lets the
// horizontal adjoining edge own a shared corner, VERTICAL the vertical one.
// Near-diagonal edges (|dx| ~= |dy|) count as horizontal so the default stays
// deterministic.
inline bool edge_is_horizontal(const Vector2 &a, const Vector2 &b) {
	const Vector2 seg = b - a;
	return Math::abs(seg.x) >= Math::abs(seg.y);
}

// Bisector (miter) normal of corner `c`: normalized sum of the two adjoining
// outward edge normals. Points along the corner's angle bisector (triangle
// apexes face UP, star tips read radial). Falls back to the averaged corner
// normal when the edges oppose (straight continuation) or degenerate.
Vector2 miter_normal(const PackedVector2Array &corners, int c, const Vector2 &ref); // patterns_polygons.cpp

// Shared corner-anchored polygon loop builder: every corner carries a slot
// and interiors spread per edge via apportion_polygon_slots, each slot riding
// its edge-constant outward normal (built from corners + averaged corner
// normals). count <= corners seats evenly spaced corners. corner_priority
// picks which adjoining edge owns a shared corner dot (HORIZONTAL default:
// top/bottom own it; VERTICAL: left/right; BALANCED: outgoing edge, i.e. the
// previous behavior). corner_mode EVEN_ARC skips corner pinning and spreads
// purely evenly by arc length from corner 0. edge_margin keeps interior dots
// at least that many px away from corners along their edge (clamped per
// edge). Returns false when degenerate (caller stacks at the marker).
bool build_symmetric_polygon_loop(const PackedVector2Array &corners, const PackedVector2Array &corner_normals, int count, const CornerLayout2D &corner, PackedVector2Array &r_points, PackedVector2Array &r_normals); // patterns_polygons.cpp

// Marker-local corner builders shared by the polygon primitives below and
// their preview samplers (single source of truth: the track can never drift
// from the volley). All windings come out rectangle-positive (outward edge
// normals); rotation spins the finished corners.
PackedVector2Array build_triangle_corners(int triangle_type, real_t size_a, real_t size_b, real_t rotation); // patterns_polygons.cpp

PackedVector2Array build_trapezoid_corners(real_t base_top, real_t base_bottom, real_t height, real_t rotation); // patterns_polygons.cpp

PackedVector2Array build_diamond_corners(real_t diagonal_x, real_t diagonal_y, real_t rotation); // patterns_polygons.cpp

// Quiet worker shared by the bound normal computer and the edge sampler:
// fills r_normals, returns false (no error spam) when unusable.
bool compute_edge_normals_quiet(const PackedVector2Array &edge_points, bool closed, bool flip, PackedVector2Array &r_normals); // patterns_edges.cpp

} // namespace BlastBullets2D
