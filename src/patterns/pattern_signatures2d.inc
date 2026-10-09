// Pattern generator signature table: ONE entry per bound
// BulletPatterns2D.helper_generate_transforms_<shape>, arguments in GDScript
// order (after transforms_amount and marker_transform). Expanded by X-macros
// into the wrapper declarations (bullet_patterns2d.hpp), the wrapper bodies
// (copy every argument into the native core's <Shape>Params2D, then call
// generate_<shape>2d) and the ClassDB binds with their defaults
// (bullet_patterns2d_bindings.cpp). The native cores and the Params structs
// stay hand-written (pattern_params2d.hpp; defaults must match this table).
//
// REQ(C++ type, argument name, params field)              required argument
// OPT(C++ type, argument name, default literal, params field) optional one
//   The C++ type is exact (enum types name the doc enum; real_t vs double
//   is visible in the API). Defaults stay literals: never derive them from a
//   real_t struct field (float rounding would leak into the bound default).
// PATTERN_GENERATOR(shape, Params struct) closes an entry; its argument list
//   is PATTERN_ARGS_<shape>(REQ, OPT).
//
// API COMPAT: argument names, order and defaults are public (positional
// GDScript calls, docs). Append new arguments at the END of an entry.

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
#define PATTERN_OUTLINE_HEAD(REQ, OPT) \
	OPT(int, outline_placement, 0, outline.outline_placement) \
	OPT(int, outline_facing, 0, outline.outline_facing) \
	OPT(bool, outline_reverse, false, outline.outline_reverse) \
	OPT(int, outline_slot_offset, 0, outline.outline_slot_offset) \
	OPT(double, fill_spacing, 32.0, outline.fill_spacing) \
	OPT(bool, fill_stagger, false, outline.fill_stagger) \
	OPT(double, fill_margin, 0.0, outline.fill_margin) \
	OPT(int, layer_count, 1, outline.layer_count) \
	OPT(double, layer_scale, 0.2, outline.layer_scale) \
	OPT(int, layer_side, 0, outline.layer_side) \
	OPT(int, layer_fill, 0, outline.layer_fill) \
	OPT(int, layer_start_offset, 0, outline.layer_start_offset) \
	OPT(int, layer_scale_curve, 0, outline.layer_scale_curve) \
	OPT(const PackedFloat32Array &, layer_custom_scales, PackedFloat32Array(), outline.layer_custom_scales) \
	OPT(int, layer_twist, 0, outline.layer_twist) \
	OPT(int, layer_max_dots, 0, outline.layer_max_dots)

// Appended at the END of every outline shape's entry (after its own
// arguments) so positional GDScript calls keep working: the finest grid the
// Fill Inside spacing search may use (see fill_spacing_that_fits).
#define PATTERN_OUTLINE_FILL_TAIL(REQ, OPT) \
	OPT(double, fill_min_spacing, 0.5, outline.fill_min_spacing)

// Corner anchoring of the polygon loops (CornerLayout2D); layer_layout
// rides here for the corner shapes.
#define PATTERN_CORNER_TAIL(REQ, OPT) \
	OPT(int, outline_distribution, 1, corner.outline_distribution) \
	OPT(int, layer_layout, 1, outline.layer_layout) \
	OPT(int, outline_corner_priority, 0, corner.outline_corner_priority) \
	OPT(int, outline_corner_mode, 0, corner.outline_corner_mode) \
	OPT(double, outline_edge_margin, 0.0, corner.outline_edge_margin) \
	OPT(int, outline_corner_facing, 0, corner.outline_corner_facing)

// Generates a grid of 2D transforms positioned relative to marker_transform.
// seed drives jitter + random rotation (0 = non-deterministic).
#define PATTERN_ARGS_grid(REQ, OPT) \
	OPT(int, rows_per_column, 10, rows_per_column) \
	OPT(Alignment, alignment, Alignment::CENTER_LEFT, alignment) \
	OPT(real_t, column_offset, 150.0, column_offset) \
	OPT(real_t, row_offset, 150.0, row_offset) \
	OPT(bool, rotate_grid_with_marker, true, rotate_grid_with_marker) \
	OPT(bool, random_local_rotation, false, random_local_rotation) \
	OPT(real_t, jitter, 0.0, jitter) \
	OPT(uint64_t, seed, 0, seed)

PATTERN_GENERATOR(grid, GridParams2D)

// Generates transforms on a ring (or arc) around marker_transform.
// Set face_outward to false for implosion patterns that fly toward the center.
// y_scale stretches the ring into an ellipse (1.0 = circle); the facing
// stays radial, so it is approximate on stretched rings.
// seed drives random rotation (0 = non-deterministic).
#define PATTERN_ARGS_ring(REQ, OPT) \
	OPT(real_t, radius, 150.0, radius) \
	OPT(real_t, start_angle, 0.0, start_angle) \
	OPT(real_t, arc, Math::TAU, arc) \
	OPT(bool, rotate_with_marker, true, rotate_with_marker) \
	OPT(bool, random_rotation, false, random_rotation) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, y_scale, 1.0, y_scale) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	OPT(uint64_t, seed, 0, seed) \
	OPT(int, layer_layout, 1, outline.layer_layout) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(ring, RingParams2D)

// Generates transforms in an aimed cone (shotgun spread): direction_angle is the cone center,
// spread is the full cone width, origins stagger along the direction so pellets
// do not stack on top of each other. When centered is false the cone is
// one-sided, from the center direction out to +spread.
// angle_jitter adds per-slot random variance for shotgun spread.
// seed drives the jitter (0 = non-deterministic).
#define PATTERN_ARGS_fan(REQ, OPT) \
	OPT(real_t, spread, 0.5, spread) \
	OPT(real_t, direction_angle, 0.0, direction_angle) \
	OPT(real_t, step_offset, 0.0, step_offset) \
	OPT(bool, centered, true, centered) \
	OPT(real_t, angle_jitter, 0.0, angle_jitter) \
	OPT(uint64_t, seed, 0, seed)

PATTERN_GENERATOR(fan, FanParams2D)

// Generates transforms along an expanding spiral around marker_transform.
// facing_mode picks the bullet facing (tangent = travel direction);
// facing_offset_degrees twists every facing by a fixed amount.
#define PATTERN_ARGS_spiral(REQ, OPT) \
	OPT(real_t, start_radius, 50.0, start_radius) \
	OPT(real_t, radius_step, 15.0, radius_step) \
	OPT(real_t, angle_step, 0.6, angle_step) \
	OPT(bool, rotate_with_marker, true, rotate_with_marker) \
	OPT(SpiralFacingMode, facing_mode, SPIRAL_FACING_TANGENT, facing_mode) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees)

PATTERN_GENERATOR(spiral, SpiralParams2D)

// Generates transforms in a straight wall/curtain/row centered on the marker.
// direction is the line axis (need not be normalized); origins spread evenly
// with the given spacing. Bullets face along the line when face_direction is
// true, otherwise they keep the marker rotation. anchor moves the marker to
// the start/end of the row; perpendicular faces them 90 degrees off the axis.
#define PATTERN_ARGS_line(REQ, OPT) \
	REQ(const Vector2 &, direction, direction) \
	OPT(real_t, spacing, 32.0, spacing) \
	OPT(bool, face_direction, true, face_direction) \
	OPT(LineAnchor, anchor, LINE_ANCHOR_CENTER, anchor) \
	OPT(bool, perpendicular, false, perpendicular)

PATTERN_GENERATOR(line, LineParams2D)

// Aimed fan: same as helper_generate_transforms_fan with the cone centered on
// the marker-to-target direction.
#define PATTERN_ARGS_aimed(REQ, OPT) \
	REQ(const Vector2 &, target_position, target_position) \
	OPT(real_t, spread, 0.3, spread) \
	OPT(real_t, step_offset, 0.0, step_offset) \
	OPT(bool, centered, true, centered)

PATTERN_GENERATOR(aimed, AimedParams2D)

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
// (flower_type...) Bloom-kind selector + per-kind knobs (append-only; old calls are unaffected). Out-of-range flower_type is rejected loudly.
#define PATTERN_ARGS_flower(REQ, OPT) \
	OPT(int, petals, 6, petals) \
	OPT(real_t, radius, 150.0, radius) \
	OPT(real_t, petal_spread, 0.5, petal_spread) \
	OPT(real_t, petal_sharpness, 1.0, petal_sharpness) \
	OPT(real_t, base_rotation, 0.0, base_rotation) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	OPT(int, flower_type, 0, flower_type) \
	OPT(double, inner_radius_scale, 0.0, inner_radius_scale) \
	OPT(double, spiro_roller, 45.0, spiro_roller) \
	OPT(double, spiro_pen, 80.0, spiro_pen) \
	OPT(double, super_lobes, 6.0, super_lobes) \
	OPT(double, super_fullness, 1.0, super_fullness) \
	OPT(int, layer_layout, 1, outline.layer_layout) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(flower, FlowerParams2D)

// True ellipse ring with independent radii and rotation (the ring
// helper's y_scale is only an approximation): rx/ry semi-axes rotated by
// ellipse_rotation. mode picks FULL ring, ARC segment, or WALL (arc with
// gap_count dodge gaps of gap_width radians each; exactly
// transforms_amount slots shared by the solid stretches). An ARC/WALL
// spanning a full turn closes like FULL (no doubled seam bullet).
#define PATTERN_ARGS_ellipse(REQ, OPT) \
	OPT(real_t, radius_x, 150.0, radius_x) \
	OPT(real_t, radius_y, 100.0, radius_y) \
	OPT(real_t, ellipse_rotation, 0.0, ellipse_rotation) \
	OPT(real_t, start_angle, 0.0, start_angle) \
	OPT(real_t, arc, Math::TAU, arc) \
	OPT(EllipseMode, mode, ELLIPSE_FULL, mode) \
	OPT(int, gap_count, 2, gap_count) \
	OPT(real_t, gap_width, 0.3, gap_width) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	OPT(int, layer_layout, 1, outline.layer_layout) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(ellipse, EllipseParams2D)

// Rain curtain: slots spread along a horizontal band of band_width above
// (or around) the marker, facing rain_direction. drop_spacing staggers
// rows so the curtain reads as layered sheets instead of one flat row.
// seed drives the jitter (0 = non-deterministic).
#define PATTERN_ARGS_rain(REQ, OPT) \
	OPT(real_t, band_width, 600.0, band_width) \
	OPT(Vector2, rain_direction, Vector2(0, 1), rain_direction) \
	OPT(real_t, drop_spacing, 48.0, drop_spacing) \
	OPT(real_t, jitter, 12.0, jitter) \
	OPT(uint64_t, seed, 0, seed)

PATTERN_GENERATOR(rain, RainParams2D)

// Scatter burst: biased-random disc for explosions, boss deaths, petal
// pops. Offsets fill the disc of burst_radius (sqrt distribution, so
// density is even, not center-clumped); facings are radial-outward plus
// facing_jitter. seed = 0 means non-deterministic, otherwise reproducible.
#define PATTERN_ARGS_scatter(REQ, OPT) \
	OPT(real_t, burst_radius, 120.0, burst_radius) \
	OPT(real_t, facing_jitter, 0.4, facing_jitter) \
	OPT(uint64_t, seed, 0, seed) \
	OPT(real_t, inner_radius, 0.0, inner_radius) \
	OPT(Vector2, sector_direction, Vector2(1, 0), sector_direction) \
	OPT(real_t, sector_arc, Math::TAU, sector_arc) \
	OPT(ScatterFacingMode, facing_mode, SCATTER_FACING_OUTWARD, facing_mode)

PATTERN_GENERATOR(scatter, ScatterParams2D)

// Star/polygon emphasis: vertices symmetric directions around the marker
// with extra density pulled toward each vertex (vertex_bias 0 = even
// ring, higher = sharper star). edges bullets per edge fill the spans.
#define PATTERN_ARGS_star_polygon(REQ, OPT) \
	OPT(int, vertices, 5, vertices) \
	OPT(real_t, radius, 150.0, radius) \
	OPT(real_t, vertex_bias, 2.0, vertex_bias) \
	OPT(real_t, base_rotation, 0.0, base_rotation) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees)

PATTERN_GENERATOR(star_polygon, StarPolygonParams2D)

// Multi-arm spiral: arms interleaved arms around the marker (galaxy,
// windmill, rose looks at low bullet counts). arm_index_stride lets
// callers interleave (1) or group (arms) consecutive slots per arm.
#define PATTERN_ARGS_multispiral(REQ, OPT) \
	OPT(int, arms, 3, arms) \
	OPT(real_t, start_radius, 50.0, start_radius) \
	OPT(real_t, radius_step, 15.0, radius_step) \
	OPT(real_t, angle_step, 0.6, angle_step) \
	OPT(bool, rotate_with_marker, true, rotate_with_marker) \
	OPT(SpiralFacingMode, facing_mode, SPIRAL_FACING_TANGENT, facing_mode) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	OPT(int, arm_index_stride, 1, arm_index_stride)

PATTERN_GENERATOR(multispiral, MultispiralParams2D)

// Cross/plus barrage: arm_count rays of evenly spaced slots from the
// marker out to arm_length. Crossfire and plus-shaped
// danmaku bursts.
#define PATTERN_ARGS_cross(REQ, OPT) \
	OPT(int, arm_count, 4, arm_count) \
	OPT(real_t, arm_length, 150.0, arm_length) \
	OPT(real_t, spacing, 32.0, spacing) \
	OPT(real_t, base_rotation, 0.0, base_rotation) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees)

PATTERN_GENERATOR(cross, CrossParams2D)

// True star shell: points alternating outer/inner vertices around the
// marker (distinct from the polygon helper's density bias). Boss star
// bursts and celebratory shells.
#define PATTERN_ARGS_star(REQ, OPT) \
	OPT(int, points, 5, points) \
	OPT(real_t, outer_radius, 150.0, outer_radius) \
	OPT(real_t, inner_radius, 65.0, inner_radius) \
	OPT(real_t, base_rotation, 0.0, base_rotation) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	PATTERN_CORNER_TAIL(REQ, OPT) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(star, StarParams2D)

// Heart bloom: parametric heart outline (boss love attacks, endings).
// size scales the classic 16sin^3 / 13cos-5cos2t curve. Full
// outline-layout support (on outline, concentric layers, interior fill);
// layer offsets run along center-radial normals, facings stay radial
// like the legacy loop.
#define PATTERN_ARGS_heart(REQ, OPT) \
	OPT(real_t, size, 150.0, size) \
	OPT(real_t, base_rotation, 0.0, base_rotation) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	OPT(int, layer_layout, 1, outline.layer_layout) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(heart, HeartParams2D)

// Snake row: slots along a sine wave of width, amplitude and wave count
// around the marker axis (direction need not be normalized). Pairs with
// wobble flight for slithering curtains.
#define PATTERN_ARGS_wave(REQ, OPT) \
	OPT(real_t, width, 600.0, width) \
	OPT(real_t, amplitude, 48.0, amplitude) \
	OPT(real_t, waves, 2.0, waves) \
	OPT(Vector2, direction, Vector2(1, 0), direction) \
	OPT(bool, face_direction, true, face_direction) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees)

PATTERN_GENERATOR(wave, WaveParams2D)

// Waterfall curtain: staggered rows x columns grid with per-row stagger
// offsets and jitter (danmaku curtains with readable doors when combined
// with skip_indices). Fires along rain_direction.
// seed drives the jitter (0 = non-deterministic).
#define PATTERN_ARGS_waterfall(REQ, OPT) \
	OPT(int, columns, 12, columns) \
	OPT(real_t, column_spacing, 48.0, column_spacing) \
	OPT(int, rows, 3, rows) \
	OPT(real_t, row_spacing, 64.0, row_spacing) \
	OPT(real_t, stagger, 0.5, stagger) \
	OPT(Vector2, rain_direction, Vector2(0, 1), rain_direction) \
	OPT(real_t, jitter, 6.0, jitter) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	OPT(uint64_t, seed, 0, seed)

PATTERN_GENERATOR(waterfall, WaterfallParams2D)

// Lattice honeycomb: staggered hex-style rows for honeycomb walls.
#define PATTERN_ARGS_lattice(REQ, OPT) \
	OPT(int, columns, 8, columns) \
	OPT(int, rows, 5, rows) \
	OPT(real_t, spacing_x, 48.0, spacing_x) \
	OPT(real_t, spacing_y, 42.0, spacing_y) \
	OPT(bool, stagger_rows, true, stagger_rows) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees)

PATTERN_GENERATOR(lattice, LatticeParams2D)

// Mathematical rose for petal-storm/blossom-finales: exact rhodonea rose
// r = R * cos(k * theta); k = petals; dense slot sweep theta = i / n * TAU.
#define PATTERN_ARGS_rose(REQ, OPT) \
	OPT(int, petals, 6, petals) \
	OPT(real_t, radius, 150.0, radius) \
	OPT(real_t, lobe_sharpness, 1.0, lobe_sharpness) \
	OPT(real_t, base_rotation, 0.0, base_rotation) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	OPT(int, layer_layout, 1, outline.layer_layout) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(rose, RoseParams2D)

// Twin counter-rotating galaxy: odd arms wind -angle_step, even arms
// +angle_step when mirrored (same facing switch as multispiral).
#define PATTERN_ARGS_counter_spiral(REQ, OPT) \
	OPT(int, arms, 2, arms) \
	OPT(real_t, start_radius, 50.0, start_radius) \
	OPT(real_t, radius_step, 15.0, radius_step) \
	OPT(real_t, angle_step, 0.6, angle_step) \
	OPT(bool, rotate_with_marker, true, rotate_with_marker) \
	OPT(SpiralFacingMode, facing_mode, SPIRAL_FACING_TANGENT, facing_mode) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	OPT(int, arm_index_stride, 1, arm_index_stride) \
	OPT(bool, mirror_alternate_arms, true, mirror_alternate_arms)

PATTERN_GENERATOR(counter_spiral, CounterSpiralParams2D)

// Dense wall perpendicular to aim with carved center dodge door.
// Aimed-trap usage: slots spread across width on the axis across from
// aim_direction; the center gap of gap_width stays empty as the door.
// Exactly transforms_amount slots: they are split over the two wall
// segments (outer edge to door edge). spacing is reserved (unused).
#define PATTERN_ARGS_corridor(REQ, OPT) \
	REQ(const Vector2 &, aim_direction, aim_direction) \
	OPT(real_t, width, 400.0, width) \
	OPT(real_t, spacing, 32.0, spacing) \
	OPT(real_t, gap_width, 96.0, gap_width) \
	OPT(bool, face_aim, true, face_aim) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees)

PATTERN_GENERATOR(corridor, CorridorParams2D)

// Figure-8/weave openings: slots sweep t = i / n * TAU over the
// lissajous curve for weaving curtains with readable doors.
#define PATTERN_ARGS_lissajous(REQ, OPT) \
	OPT(real_t, size_x, 200.0, size_x) \
	OPT(real_t, size_y, 120.0, size_y) \
	OPT(real_t, freq_x, 3.0, freq_x) \
	OPT(real_t, freq_y, 2.0, freq_y) \
	OPT(real_t, phase, 0.0, phase) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	OPT(int, layer_layout, 1, outline.layer_layout) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(lissajous, LissajousParams2D)

// Clean circle outline: transforms_amount slots evenly on a radius
// circle around the marker, facing outward (or inward). The Ring helper
// covers arcs; this is the exact full-loop shape primitive.
#define PATTERN_ARGS_circle(REQ, OPT) \
	OPT(real_t, radius, 150.0, radius) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	OPT(int, layer_layout, 1, outline.layer_layout) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(circle, CircleParams2D)

// Rectangle perimeter: slots walk the outline of a size-sized box
// centered on the marker (counter-clockwise from top-left), facing
// outward (or inward). Square = size with equal sides.
#define PATTERN_ARGS_rectangle(REQ, OPT) \
	OPT(const Vector2 &, size, Vector2(300, 200), size) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	PATTERN_CORNER_TAIL(REQ, OPT) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(rectangle, RectangleParams2D)

// Polygon perimeter: vertices corners on a radius circle from
// base_rotation, slots spread evenly by arc length along the outline,
// facing outward (or inward).
#define PATTERN_ARGS_polygon(REQ, OPT) \
	OPT(int, vertices, 6, vertices) \
	OPT(real_t, radius, 150.0, radius) \
	OPT(real_t, base_rotation, 0.0, base_rotation) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	PATTERN_CORNER_TAIL(REQ, OPT) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(polygon, PolygonParams2D)

// Triangle perimeter: equilateral (circumradius size_a), isosceles
// (base size_a + height size_b, apex up) or right-angled (legs size_a
// along X and size_b along Y, recentered), all rotated by rotation and
// centered on the marker. Slots walk the outline evenly by arc length,
// facing outward (or inward). Degenerate (zero-area) triangles stack at
// the marker like the rectangle primitive.
#define PATTERN_ARGS_triangle(REQ, OPT) \
	OPT(TriangleType, triangle_type, TRIANGLE_EQUILATERAL, triangle_type) \
	OPT(real_t, size_a, 150.0, size_a) \
	OPT(real_t, size_b, 150.0, size_b) \
	OPT(real_t, rotation, 0.0, rotation) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	PATTERN_CORNER_TAIL(REQ, OPT) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(triangle, TriangleParams2D)

// Isosceles trapezoid perimeter: bases base_top/base_bottom with height,
// centered on the marker and rotated by rotation. Slots walk the outline
// evenly by arc length, facing outward (or inward). A zero top base
// degrades gracefully to a triangle; zero-area boxes stack at the marker.
#define PATTERN_ARGS_trapezoid(REQ, OPT) \
	OPT(real_t, base_top, 200.0, base_top) \
	OPT(real_t, base_bottom, 300.0, base_bottom) \
	OPT(real_t, height, 200.0, height) \
	OPT(real_t, rotation, 0.0, rotation) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	PATTERN_CORNER_TAIL(REQ, OPT) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(trapezoid, TrapezoidParams2D)

// Diamond (rhombus) perimeter: diagonals diagonal_x/diagonal_y, centered
// on the marker and rotated by rotation. Slots walk the outline evenly
// by arc length, facing outward (or inward). Zero-area diamonds stack at
// the marker.
#define PATTERN_ARGS_diamond(REQ, OPT) \
	OPT(real_t, diagonal_x, 200.0, diagonal_x) \
	OPT(real_t, diagonal_y, 300.0, diagonal_y) \
	OPT(real_t, rotation, 0.0, rotation) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	PATTERN_CORNER_TAIL(REQ, OPT) \
	PATTERN_OUTLINE_FILL_TAIL(REQ, OPT)

PATTERN_GENERATOR(diamond, DiamondParams2D)

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
#define PATTERN_ARGS_edge_from_points(REQ, OPT) \
	REQ(const PackedVector2Array &, edge_points, edge_points) \
	OPT(bool, closed, false, closed) \
	OPT(bool, flip_normals, false, flip_normals) \
	OPT(bool, random_sample, false, random_sample) \
	OPT(real_t, jitter, 0.0, jitter) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	OPT(uint64_t, seed, 0, seed) \
	OPT(real_t, spread, 0.0, spread) \
	OPT(real_t, spread_exponent, 2.0, spread_exponent) \
	OPT(int, spread_side, 0, spread_side) \
	OPT(real_t, tangent_jitter, 0.0, tangent_jitter)

PATTERN_GENERATOR(edge_from_points, EdgeFromPointsParams2D)

// Arc-length layout along a marker-local polyline (closed adds the
// last-to-first stretch when it has length), tangent-first facing.
#define PATTERN_ARGS_polyline(REQ, OPT) \
	REQ(const PackedVector2Array &, points, points) \
	OPT(bool, closed, false, closed) \
	OPT(PolylineDistribution, distribution, POLYLINE_DISTRIBUTION_EVEN, distribution) \
	OPT(real_t, spacing, 32.0, spacing) \
	OPT(PolylineOverflow, overflow, POLYLINE_OVERFLOW_CLAMP, overflow) \
	OPT(PolylineAnchor, anchor, POLYLINE_ANCHOR_START, anchor) \
	OPT(real_t, start_offset, 0.0, start_offset) \
	OPT(bool, reverse, false, reverse) \
	OPT(PolylineFacing, facing, POLYLINE_FACING_ALONG_PATH, facing) \
	OPT(real_t, facing_offset_deg, 0.0, facing_offset_deg)

PATTERN_GENERATOR(polyline, PolylineParams2D)
