#pragma once

// PatternKnobs2D: every pattern knob (the helper_* properties) as plain
// storage, owned by the patterns module. BulletSpawner2D inherits it (its
// bound helper_* accessors read and write these fields), and every pattern
// stage reads the knobs from here: generation (pattern_dispatch2d.cpp),
// inspector gating (pattern_gating2d.cpp), presets (pattern_presets2d.cpp).
// Field names ARE the serialized property names: never rename one.

#include "godot_cpp/variant/node_path.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/packed_int32_array.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "patterns/bullet_patterns2d.hpp"
#include "patterns/pattern_inputs2d.hpp"
#include "patterns/pattern_slots2d.hpp"
#include <cstdint>

namespace godot {
class Node;
class Node2D;
} //namespace godot

namespace BlastBullets2D {
using namespace godot;

struct PatternKnobs2D {
	// Bullet count for the helper modes (children/self modes derive the
	// count from the collected transforms instead).
	int helper_bullets_amount = 10;

	// GRID
	int helper_grid_rows_per_column = 10;
	int helper_grid_alignment = 3; // BulletPatterns2D::Alignment, center-left
	double helper_grid_column_offset = 150.0;
	double helper_grid_row_offset = 150.0;
	bool helper_grid_rotate_with_marker = true;
	bool helper_grid_random_local_rotation = false;
	double helper_grid_jitter = 0.0;
	// Seed for grid jitter + random rotation. 0 = non-deterministic, >0 = reproducible.
	int helper_grid_seed = 0;

	// RING
	double helper_ring_radius = 150.0;
	double helper_ring_start_angle = 0.0;
	double helper_ring_arc = 6.283185307179586; // Math::TAU
	bool helper_ring_rotate_with_marker = true;
	bool helper_ring_random_rotation = false;
	bool helper_ring_face_outward = true;
	double helper_ring_y_scale = 1.0;
	double helper_ring_facing_offset_deg = 0.0;
	// Seed for ring random rotation. 0 = non-deterministic, >0 = reproducible.
	int helper_ring_seed = 0;

	// FAN
	double helper_fan_spread = 0.5;
	double helper_fan_direction_angle = 0.0;
	double helper_fan_step_offset = 0.0;
	bool helper_fan_centered = true;
	// Per-slot random angle variance in radians (shotgun spread, 0 = exact).
	double helper_fan_angle_jitter = 0.0;
	// Seed for fan angle jitter. 0 = non-deterministic, >0 = reproducible.
	int helper_fan_seed = 0;

	// SPIRAL
	double helper_spiral_start_radius = 50.0;
	double helper_spiral_radius_step = 15.0;
	double helper_spiral_angle_step = 0.6;
	bool helper_spiral_rotate_with_marker = true;
	int helper_spiral_facing = 0; // BulletPatterns2D::SpiralFacingMode, tangent
	double helper_spiral_facing_offset_deg = 0.0;

	// LINE
	Vector2 helper_line_direction = Vector2(1, 0);
	double helper_line_spacing = 32.0;
	bool helper_line_face_direction = true;
	int helper_line_anchor = 1; // BulletPatterns2D::LineAnchor, center
	// 0 = along the line (as generated), 1 = +90 deg, 2 = -90 deg.
	int helper_line_facing = 0;
	bool helper_line_reverse = false;
	int helper_line_slot_offset = 0;
	double helper_line_start_offset = 0.0;

	// AIMED
	// Scene-tree reference to the target node the aimed cone centers on.
	// Same NodePath pattern as the other scene references on this node.
	NodePath helper_aimed_target_path;
	// Runtime cache of the resolved target. Not a bound property.
	// helper_aimed_target_id pairs with it (same dangling guard).
	mutable Node2D *helper_aimed_target = nullptr;
	mutable uint64_t helper_aimed_target_id = 0;
	double helper_aimed_spread = 0.3;
	double helper_aimed_step_offset = 0.0;
	bool helper_aimed_centered = true;
	// Blend toward the predicted target position: 0 aims at where the
	// target is now, 1 aims where it will be after helper_aimed_prediction
	// seconds at its current velocity (needs a target that exposes
	// get_velocity(), e.g. CharacterBody2D; otherwise falls back to now).
	double helper_aimed_prediction = 0.0;
	double helper_aimed_prediction_time = 0.5;

	// FLOWER (spell-card blossoms: petals symmetric lobes).
	int helper_flower_petals = 6;
	double helper_flower_radius = 150.0;
	double helper_flower_petal_spread = 0.5;
	double helper_flower_petal_sharpness = 1.0;
	double helper_flower_base_rotation = 0.0;
	bool helper_flower_face_outward = true;
	double helper_flower_facing_offset_deg = 0.0;
	// Bloom kind selector (FlowerBloom): 0=FAN (legacy default), 1=RHODONEA,
	// 2=PHYLLOTAXIS, 3=SPIROGRAPH, 4=SUPERFORMULA. Per-type knobs below are
	// used only by the matching bloom kind (see bullet_factory2d.hpp doc).
	int helper_flower_type = 0;
	double helper_flower_inner_radius_scale = 0.0; // core-hole lift (0 = full bloom)
	double helper_flower_spiro_roller = 45.0; // hypotrochoid roller r (> 0)
	double helper_flower_spiro_pen = 80.0; // hypotrochoid pen d (>= 0)
	double helper_flower_super_lobes = 6.0; // superformula lobe count m
	double helper_flower_super_fullness = 1.0; // superformula fullness exponent

	// ELLIPSE (true ellipse ring / arc / wall-with-gaps).
	double helper_ellipse_radius_x = 150.0;
	double helper_ellipse_radius_y = 100.0;
	double helper_ellipse_rotation = 0.0;
	double helper_ellipse_start_angle = 0.0;
	double helper_ellipse_arc = 6.283185307179586;
	int helper_ellipse_mode = 0; // BulletPatterns2D::EllipseMode, full
	int helper_ellipse_gap_count = 2;
	double helper_ellipse_gap_width = 0.3;
	bool helper_ellipse_face_outward = true;
	double helper_ellipse_facing_offset_deg = 0.0;

	// RAIN (curtain band of descending bullets).
	double helper_rain_band_width = 600.0;
	Vector2 helper_rain_direction = Vector2(0, 1);
	double helper_rain_drop_spacing = 48.0;
	double helper_rain_jitter = 12.0;
	// Seed for rain jitter. 0 = non-deterministic, >0 = reproducible.
	int helper_rain_seed = 0;

	// SCATTER (biased-random burst disc: explosions, deaths, pops).
	double helper_scatter_burst_radius = 120.0;
	double helper_scatter_facing_jitter = 0.4;
	// 0 = non-deterministic, otherwise reproducible (replays, bosses).
	int helper_scatter_seed = 0;
	// Inner radius of the burst annulus. 0 = full disc; above 0 the
	// center stays empty (explosion rings). Clamped to burst_radius
	// at generation, so set order never matters.
	double helper_scatter_inner_radius = 0.0;
	// Center of the scatter sector (only when arc < TAU).
	Vector2 helper_scatter_direction = Vector2(1, 0);
	// Sector width in radians. TAU or more = full circle.
	double helper_scatter_arc = 6.283185307179586; // Math::TAU
	// Bullet facing: 0 = radial outward, 1 = fully random, 2 = inward.
	int helper_scatter_facing = 0;

	// STAR POLYGON (vertex emphasis: density pulled toward N vertices of
	// a star frame; unlike Polygon it is a scatter, not a perimeter).
	int helper_star_polygon_vertices = 5;
	double helper_star_polygon_radius = 150.0;
	double helper_star_polygon_vertex_bias = 2.0;
	double helper_star_polygon_base_rotation = 0.0;
	bool helper_star_polygon_face_outward = true;
	double helper_star_polygon_facing_offset_deg = 0.0;

	// MULTISPIRAL (interleaved galaxy/windmill/rose arms).
	int helper_multispiral_arms = 3;
	double helper_multispiral_start_radius = 50.0;
	double helper_multispiral_radius_step = 15.0;
	double helper_multispiral_angle_step = 0.6;
	bool helper_multispiral_rotate_with_marker = true;
	int helper_multispiral_facing = 0; // BulletPatterns2D::SpiralFacingMode, tangent
	double helper_multispiral_facing_offset_deg = 0.0;
	int helper_multispiral_arm_stride = 1;

	// CROSS (plus/X barrage: rays from the marker).
	int helper_cross_arm_count = 4;
	double helper_cross_arm_length = 150.0;
	double helper_cross_spacing = 32.0;
	double helper_cross_base_rotation = 0.0;
	bool helper_cross_face_outward = true;
	double helper_cross_facing_offset_deg = 0.0;

	// STAR (true star shell: alternating outer/inner vertices).
	int helper_star_points = 5;
	double helper_star_outer_radius = 150.0;
	double helper_star_inner_radius = 65.0;
	double helper_star_base_rotation = 0.0;
	bool helper_star_face_outward = true;
	double helper_star_facing_offset_deg = 0.0;

	// HEART (parametric heart bloom for boss love attacks).
	double helper_heart_size = 150.0;
	double helper_heart_base_rotation = 0.0;
	bool helper_heart_face_outward = true;
	double helper_heart_facing_offset_deg = 0.0;

	// WAVE (snake row along a sine wave).
	double helper_wave_width = 600.0;
	double helper_wave_amplitude = 48.0;
	double helper_wave_waves = 2.0;
	Vector2 helper_wave_direction = Vector2(1, 0);
	bool helper_wave_face_direction = true;
	double helper_wave_facing_offset_deg = 0.0;

	// WATERFALL (staggered curtain grid).
	int helper_waterfall_columns = 12;
	double helper_waterfall_column_spacing = 48.0;
	int helper_waterfall_rows = 3;
	double helper_waterfall_row_spacing = 64.0;
	double helper_waterfall_stagger = 0.5;
	Vector2 helper_waterfall_rain_direction = Vector2(0, 1);
	double helper_waterfall_jitter = 6.0;
	double helper_waterfall_facing_offset_deg = 0.0;
	// Seed for waterfall jitter. 0 = non-deterministic, >0 = reproducible.
	int helper_waterfall_seed = 0;

	// LATTICE (staggered honeycomb wall).
	int helper_lattice_columns = 8;
	int helper_lattice_rows = 5;
	double helper_lattice_spacing_x = 48.0;
	double helper_lattice_spacing_y = 42.0;
	bool helper_lattice_stagger_rows = true;
	bool helper_lattice_face_outward = true;
	double helper_lattice_facing_offset_deg = 0.0;

	// ROSE (exact rhodonea rose for petal-storm / blossom-finale blooms).
	int helper_rose_petals = 6;
	double helper_rose_radius = 150.0;
	double helper_rose_lobe_sharpness = 1.0;
	double helper_rose_base_rotation = 0.0;
	bool helper_rose_face_outward = true;
	double helper_rose_facing_offset_deg = 0.0;

	// COUNTER SPIRAL (twin counter-rotating galaxy / windmill arms).
	int helper_counter_spiral_arms = 2;
	double helper_counter_spiral_start_radius = 50.0;
	double helper_counter_spiral_radius_step = 15.0;
	double helper_counter_spiral_angle_step = 0.6;
	bool helper_counter_spiral_rotate_with_marker = true;
	int helper_counter_spiral_facing = 0; // BulletPatterns2D::SpiralFacingMode, tangent
	double helper_counter_spiral_facing_offset_deg = 0.0;
	int helper_counter_spiral_arm_stride = 1;
	bool helper_counter_spiral_mirror_alternate_arms = true;

	// CORRIDOR (aimed trap: dense wall with a carved center dodge door).
	// Exactly helper_bullets_amount slots, split over the two wall
	// segments; helper_corridor_gap_width stays empty as the door.
	Vector2 helper_corridor_aim_direction = Vector2(0, 1);
	double helper_corridor_width = 400.0;
	double helper_corridor_gap_width = 96.0;
	bool helper_corridor_face_aim = true;
	double helper_corridor_facing_offset_deg = 0.0;

	// LISSAJOUS (figure-8 / weave openings for crossing curtains).
	double helper_lissajous_size_x = 200.0;
	double helper_lissajous_size_y = 120.0;
	double helper_lissajous_freq_x = 3.0;
	double helper_lissajous_freq_y = 2.0;
	double helper_lissajous_phase = 0.0;
	bool helper_lissajous_face_outward = true;
	double helper_lissajous_facing_offset_deg = 0.0;

	// CUSTOM (hand-placed transforms). Store any Transform2D array in
	// helper_custom_transforms (generator-local: composed as
	// marker * local, so spin and scales keep working): each bullet
	// spawns exactly there, facing per helper_custom_facing. Reverse and
	// slot offset reorder the array; the count comes from the array
	// itself (helper_bullets_amount is ignored).
	TypedArray<Transform2D> helper_custom_transforms;
	int helper_custom_facing = 0; // 0 = as stored, 1 = face outward, 2 = face inward, 3 = +90 deg, 4 = -90 deg
	double helper_custom_facing_offset_deg = 0.0;
	bool helper_custom_reverse = false;
	int helper_custom_slot_offset = 0;
	// TRIANGLE (equilateral / isosceles / right perimeter).
	int helper_triangle_type = 0; // BulletPatterns2D::TriangleType
	double helper_triangle_size_a = 150.0;
	double helper_triangle_size_b = 150.0;
	double helper_triangle_rotation = 0.0;
	bool helper_triangle_face_outward = true;
	double helper_triangle_facing_offset_deg = 0.0;
	// TRAPEZOID (isosceles trapezoid perimeter).
	double helper_trapezoid_base_top = 200.0;
	double helper_trapezoid_base_bottom = 300.0;
	double helper_trapezoid_height = 200.0;
	double helper_trapezoid_rotation = 0.0;
	bool helper_trapezoid_face_outward = true;
	double helper_trapezoid_facing_offset_deg = 0.0;
	// DIAMOND (rhombus perimeter from its diagonals).
	double helper_diamond_diagonal_x = 200.0;
	double helper_diamond_diagonal_y = 300.0;
	double helper_diamond_rotation = 0.0;
	bool helper_diamond_face_outward = true;
	double helper_diamond_facing_offset_deg = 0.0;
	// CIRCLE (exact loop outline).
	double helper_circle_radius = 150.0;
	bool helper_circle_face_outward = true;
	double helper_circle_facing_offset_deg = 0.0;
	// RECTANGLE (perimeter walk, centered on the generator).
	Vector2 helper_rectangle_size = Vector2(300, 200);
	bool helper_rectangle_face_outward = true;
	double helper_rectangle_facing_offset_deg = 0.0;
	// SQUARE (perimeter walk, centered; single side length).
	double helper_square_size = 300.0;
	bool helper_square_face_outward = true;
	double helper_square_facing_offset_deg = 0.0;
	// POLYGON (true perimeter walk, centered; corner count and radius).
	int helper_polygon_vertices = 6;
	double helper_polygon_radius = 150.0;
	double helper_polygon_rotation = 0.0;
	bool helper_polygon_face_outward = true;
	double helper_polygon_facing_offset_deg = 0.0;
	// PATH2D (live curve layout; standalone, deterministic).
	// Bullets sit ON the baked curve: FIXED_SPACING places them
	// helper_path2d_spacing apart (helper_path2d_anchor/overflow decide
	// placement when the run is shorter/longer than the curve),
	// EVEN_DISTRIBUTION spreads them over the whole length.
	// Facing is tangent-first: ALONG_PATH aims +X with travel, the two
	// NORMAL modes aim across it. helper_path2d_reverse walks end->start
	// (ALONG_PATH flips with it); helper_path2d_closed includes the
	// last->first segment for loop paths.
	NodePath helper_path2d_path;
	int helper_path2d_space = 0; // BulletSpawner2D::Path2DSpace (FOLLOW_GENERATOR)
	int helper_path2d_distribution = 0; // BulletSpawner2D::Path2DDistribution (FIXED_SPACING)
	double helper_path2d_spacing = 32.0;
	int helper_path2d_overflow = 2; // BulletSpawner2D::Path2DOverflow (SHRINK_TO_FIT)
	int helper_path2d_anchor = 0; // BulletSpawner2D::Path2DAnchor (START)
	double helper_path2d_start_offset = 0.0;
	bool helper_path2d_reverse = false;
	bool helper_path2d_closed = false;
	int helper_path2d_facing = 0; // BulletSpawner2D::Path2DFacing (ALONG_PATH)
	double helper_path2d_facing_offset_deg = 0.0;
	// Runtime cache of the resolved Path2D. Not a bound property. The id
	// pairs with it: validate BEFORE dereferencing (a raw pointer
	// outlives freed nodes — see crash history).
	mutable Node *helper_path2d_cache = nullptr;
	mutable uint64_t helper_path2d_id = 0;
	// OUTLINE LAYOUT (closed-loop shapes: Ring, Ellipse, Star, Flower,
	// Rose, Lissajous, Circle, Rectangle, Square, Regular Polygon,
	// Triangle, Trapezoid, Diamond, Heart).
	// One placement model instead of the old scatter pass: On Outline
	// keeps the generated slot loop; Fill Inside swaps it for a row-major
	// grid masked to the loop interior (capped at helper_bullets_amount,
	// may return fewer on small shapes); Layers re-spawns the same slot
	// loop scaled about the loop center (layer 0 exactly on the outline:
	// the identical figure at a different size per layer).
	// Facing rotates each default facing (Normal = as generated,
	// Along ±90 = toward the loop tangent); reverse mirrors the slot
	// order; slot_offset rotates which slot becomes bullet 0.
	// Fill/layer dims only show in their mode.
	int helper_outline_placement = 0; // BulletPatterns2D::OutlinePlacement
	// Concentric layers (LAYERS placement): layer 0 sits exactly on the
	// outline. 1 = single exact layer (default, current behavior).
	// Capped at 64.
	int helper_outline_layer_count = 1;
	// Fractional growth per layer (0.2 = each ring 20% bigger than the
	// previous). Must stay finite in (0, 8].
	double helper_outline_layer_scale = 0.2;
	// Growth side (OutlineLayerSide): 0 outward, 1 inward, 2 both.
	int helper_outline_layer_side = 0;
	// Deal order (OutlineLayerFill): 0 interleaved, 1 sequential,
	// 2 outer-first, 3 ping-pong.
	int helper_outline_layer_fill = 0;
	// Ring-size progression (OutlineLayerScaleCurve): 0 linear steps,
	// 1 compounding. A non-empty custom scale list overrides both.
	int helper_outline_layer_scale_curve = 0;
	// Explicit per-layer scales (entry L % size); empty = formula.
	PackedFloat32Array helper_outline_layer_scales;
	// Per-layer slot rotation for rings above the outline (0 = off):
	// stacked rings interleave angularly instead of sitting in spokes.
	int helper_outline_layer_twist = 0;
	// Max bullets kept per extra layer (0 = unlimited): overflow is
	// dropped, first-kept in bullet order. Layer 0 is never capped.
	int helper_outline_layer_max_dots = 0;
	// Sequential start layer: which layer sequential filling begins from
	// (wraps around). 0 = start on the outline. Sequential mode only.
	int helper_outline_layer_start_offset = 0;
	// Corner distribution (OutlineDistribution) for the corner-anchored
	// polygon loops (rectangle, square, polygon, triangle, trapezoid,
	// diamond, star): 0 legacy winding-order leftovers (first edges win),
	// 1 symmetric opposite-pair leftovers (default: opposite sides equal).
	int helper_outline_distribution = 1;
	// Ring layout (OutlineLayerLayout): 0 decimates one shared loop
	// (outer rings miss corners), 1 rebuilds an even symmetric loop per
	// ring (default: corners on every ring, even gaps).
	int helper_outline_layer_layout = 1;
	// Corner priority (OutlineCornerPriority): which adjoining side owns
	// a shared corner dot. 0 horizontal (default: top/bottom own corners,
	// so a top-right dot faces UP with the top side), 1 vertical
	// (left/right own them), 2 balanced (outgoing edge owns, previous
	// behavior). Classification runs in shape-local space.
	int helper_outline_corner_priority = 0;
	// Corner mode (OutlineCornerMode): 0 pins a dot on every corner
	// (default), 1 spreads purely evenly by arc length (corners coincide
	// only when the count aligns; best for dense volleys).
	int helper_outline_corner_mode = 0;
	// Edge margin in px: interior dots keep at least this clearance from
	// corners along their edge (clamped per edge, corners stay pinned).
	// 0 disables (default: even split of the full edge).
	double helper_outline_edge_margin = 0.0;
	// Corner facing (OutlineCornerFacing): which normal a corner-seated
	// dot uses. 0 side (default: owning side's edge normal, stable),
	// 1 miter (angle bisector: triangle apexes face UP, star tips read
	// radial), 2 smooth (averaged loop normals everywhere: coherent on
	// high-frequency outlines where even mid-edge normals swing wildly).
	int helper_outline_corner_facing = 0;
	int helper_outline_facing = 0; // BulletPatterns2D::OutlineFacing
	bool helper_outline_reverse = false;
	int helper_outline_slot_offset = 0;
	double helper_outline_fill_spacing = 32.0;
	bool helper_outline_fill_stagger = false;
	double helper_outline_fill_margin = 0.0;
	double helper_outline_fill_min_spacing = 0.5;

	// NEGATIVE SPACE (skip slots by index: dodge doors, bullet text).
	PackedInt32Array helper_skip_indices;

	// ---- Pattern stages over these knobs (patterns module) ----

	// The shared outline-layout / corner-layout knobs as the generators'
	// structs (pattern_dispatch2d.cpp).
	OutlineLayout2D outline_layout() const;
	CornerLayout2D corner_layout() const;

	// Raw transforms for in.source (pre spin/scale/skip): exactly
	// helper_bullets_amount for the helper shapes. pattern_dispatch2d.cpp.
	PatternSlots2D generate_raw(const PatternInputs2D &in) const;
	// Path2D layout over an already sampled curve (helper_path2d_* knobs).
	PatternSlots2D layout_path2d(const Transform2D &marker, const PackedVector2Array &path_pts, int count, bool quiet) const;
	// The preview track the bullets of in.source sit on, written to the
	// sink. pattern_track_dispatch2d.cpp.
	void build_preview_track(const PatternTrackInputs2D &in, PatternTrackSink2D &sink) const;
	// Quiet mirror of the layout validation: true when the outline-layer
	// rings for this source would be accepted by the generator.
	bool outline_layer_rings_drawable(int source) const;
	// Inspector gating for one helper_* property under the given source:
	// the knob's owning shape (registry prefix) must be the source, plus the
	// per-mode sub-rules (flower type, ellipse wall, fixed-spacing path...).
	// pattern_gating2d.cpp.
	bool is_knob_relevant(int source, const String &property_name) const;
	// Writes a BulletPatterns2D::PatternPreset's knobs (on top of defaults
	// the caller restored) and reports its source/spin. False (nothing
	// written) for CUSTOM and unknown ids. pattern_presets2d.cpp.
	bool write_preset_knobs(int preset, PatternPresetResult2D &r_preset);
};

} //namespace BlastBullets2D
