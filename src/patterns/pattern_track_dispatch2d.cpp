// Preview-track dispatch: per shape, the curve/strips the bullets of that
// shape sit on, written to a PatternTrackSink2D (the spawner's holder-space
// buffers). Must draw the same geometry the generators place bullets on
// (pinned by spawner/test_spawner_preview_coincidence.gd).

#include "patterns/pattern_knobs2d.hpp"
#include "patterns/pattern_registry2d.hpp"
#include "patterns/patterns_internal.hpp"

#include "godot_cpp/core/math.hpp"

using namespace godot;

namespace BlastBullets2D {

// Cone strip shared by the fan/aimed preview tracks: two boundary rays from
// the origin plus the tip arc connecting them, closed back at the origin.
// Appends marker-GLOBAL points in draw order; the caller feeds each through
// the spin/scale pipeline. Preconditions (checked by callers): finite spread
// > 0 and tip_radius > 0.
static void build_cone_strip(const Vector2 &origin, real_t dir_angle, real_t half_spread, real_t tip_radius, real_t spread, PackedVector2Array &r_out) {
	const real_t left_ang = dir_angle + half_spread;
	const real_t right_ang = dir_angle - half_spread;
	const Vector2 left_dir = Vector2(Math::cos(left_ang), Math::sin(left_ang));
	const Vector2 right_dir = Vector2(Math::cos(right_ang), Math::sin(right_ang));
	const Vector2 left_tip = origin + left_dir * tip_radius;
	const Vector2 right_tip = origin + right_dir * tip_radius;
	// Left ray: origin -> tip.
	r_out.push_back(origin);
	r_out.push_back(left_tip);
	// Arc from left tip to right tip (across the spread).
	const int arc_n = Math::clamp((int)(tip_radius * spread / 8.0), 8, 64);
	for (int i = 1; i < arc_n; i++) {
		const real_t t = (real_t)i / (real_t)arc_n;
		const real_t ang = left_ang + (right_ang - left_ang) * t;
		r_out.push_back(origin + Vector2(Math::cos(ang), Math::sin(ang)) * tip_radius);
	}
	// Right ray: tip -> origin (closes the cone visually).
	r_out.push_back(right_tip);
	r_out.push_back(origin);
}

void PatternKnobs2D::build_preview_track(const PatternTrackInputs2D &in, PatternTrackSink2D &sink) const {
	const Transform2D &track_marker = in.marker;
	const Vector2 track_origin = track_marker.get_origin();
	const real_t preview_mirror_sign = in.mirror_sign;
	switch (in.source) {
		case PATTERN_SHAPE_RING: {
			const real_t start_abs = (helper_ring_rotate_with_marker ? track_marker.get_rotation() : 0.0) + (real_t)helper_ring_start_angle;
			sink.push_dict(BulletPatterns2D::helper_sample_outline_ring(helper_ring_radius, helper_ring_arc, helper_ring_y_scale, start_abs));
			break;
		}
		case PATTERN_SHAPE_ELLIPSE:
			sink.push_dict(BulletPatterns2D::helper_sample_outline_ellipse(helper_ellipse_radius_x, helper_ellipse_radius_y, helper_ellipse_rotation, helper_ellipse_start_angle, helper_ellipse_arc, helper_ellipse_mode));
			break;
		case PATTERN_SHAPE_STAR:
			sink.push_dict(BulletPatterns2D::helper_sample_outline_star(helper_star_points, helper_star_outer_radius, helper_star_inner_radius, helper_star_base_rotation), true);
			break;
		case PATTERN_SHAPE_ROSE:
			sink.push_dict(BulletPatterns2D::helper_sample_outline_rose(helper_rose_petals, helper_rose_radius, helper_rose_lobe_sharpness, helper_rose_base_rotation));
			break;
		case PATTERN_SHAPE_LISSAJOUS:
			sink.push_dict(BulletPatterns2D::helper_sample_outline_lissajous(helper_lissajous_size_x, helper_lissajous_size_y, helper_lissajous_freq_x, helper_lissajous_freq_y, helper_lissajous_phase));
			break;
		case PATTERN_SHAPE_CIRCLE:
			sink.push_dict(BulletPatterns2D::helper_sample_outline_circle(helper_circle_radius));
			break;
		case PATTERN_SHAPE_RECTANGLE:
			sink.push_dict(BulletPatterns2D::helper_sample_outline_rectangle(helper_rectangle_size), true);
			break;
		case PATTERN_SHAPE_SQUARE:
			sink.push_dict(BulletPatterns2D::helper_sample_outline_rectangle(Vector2((real_t)helper_square_size, (real_t)helper_square_size)), true);
			break;
		case PATTERN_SHAPE_TRIANGLE:
			sink.push_dict(BulletPatterns2D::helper_sample_outline_triangle(helper_triangle_type, helper_triangle_size_a, helper_triangle_size_b, helper_triangle_rotation), true);
			break;
		case PATTERN_SHAPE_TRAPEZOID:
			sink.push_dict(BulletPatterns2D::helper_sample_outline_trapezoid(helper_trapezoid_base_top, helper_trapezoid_base_bottom, helper_trapezoid_height, helper_trapezoid_rotation), true);
			break;
		case PATTERN_SHAPE_DIAMOND:
			sink.push_dict(BulletPatterns2D::helper_sample_outline_diamond(helper_diamond_diagonal_x, helper_diamond_diagonal_y, helper_diamond_rotation), true);
			break;
		case PATTERN_SHAPE_POLYGON:
			sink.push_dict(BulletPatterns2D::helper_sample_outline_polygon(helper_polygon_vertices, helper_polygon_radius, helper_polygon_rotation), true);
			break;
		case PATTERN_SHAPE_PATH2D: {
			// Generator-local shape points: the collector composes them
			// as marker.xform, so the track xforms them identically
			// (then spin/scales like every other track point).
			const PackedVector2Array curve = in.path_points != nullptr ? *in.path_points : PackedVector2Array();
			sink.set_closed(helper_path2d_closed);
			const int step = curve.size() > kPatternMaxTrackPoints ? (int)((curve.size() + kPatternMaxTrackPoints - 1) / kPatternMaxTrackPoints) : 1;
			for (int i = 0; i < curve.size(); i += step) {
				sink.push_local(curve[i]);
			}
			break;
		}
		case PATTERN_SHAPE_LINE: {
			// Row segment from the same formula the generator uses
			// (anchor picks the marker seat, start_offset shifts on
			// top). Count-independent: a lone bullet still shows its
			// rail. Reverse/offset preserve the position set.
			const int count = helper_bullets_amount;
			Vector2 axis = Vector2(1, 0);
			if (helper_line_direction.is_finite() && helper_line_direction.length_squared() > 1e-12) {
				axis = helper_line_direction.normalized();
			} else {
				break;
			}
			if (count <= 0 || !Math::is_finite(helper_line_spacing)) {
				break;
			}
			double anchor_seat = (double)(count - 1) * 0.5;
			if (helper_line_anchor == (int)BulletPatterns2D::LINE_ANCHOR_START) {
				anchor_seat = 0.0;
			} else if (helper_line_anchor == (int)BulletPatterns2D::LINE_ANCHOR_END) {
				anchor_seat = (double)(count - 1);
			}
			const double shift = Math::is_finite(helper_line_start_offset) ? MAX(helper_line_start_offset, 0.0) : 0.0;
			// Same span the generator fills (first/last slot, shifted):
			// no half-spacing overhang past the end bullets. A lone
			// bullet is a point (no rail to draw), matching the dots.
			const Vector2 first = track_origin + axis * (real_t)(helper_line_spacing * (0.0 - anchor_seat) + shift);
			const Vector2 last = track_origin + axis * (real_t)(helper_line_spacing * ((double)(count - 1) - anchor_seat) + shift);
			if (count > 1 && (last - first).length_squared() > 1e-12) {
				sink.push_global(first);
				sink.push_global(last);
			}
			break;
		}
		case PATTERN_SHAPE_CUSTOM: {
			// Explicit order strip through the stored slots (finite
			// only): visualizes what reverse/offset do. Strided so a
			// huge array cannot spam the canvas.
			if (in.slots == nullptr) {
				break;
			}
			const TypedArray<Transform2D> &transforms = *in.slots;
			const int step = transforms.size() > 512 ? (int)((transforms.size() + 511) / 512) : 1;
			for (int i = 0; i < transforms.size(); i += step) {
				const Variant tv = transforms[i];
				if (tv.get_type() != Variant::TRANSFORM2D) {
					continue;
				}
				const Transform2D tt = tv;
				if (!tt.is_finite()) {
					continue;
				}
				sink.push_slot_origin(tt.get_origin());
			}
			break;
		}
		case PATTERN_SHAPE_GRID: {
			// Row strips via the factory sampler (INF-separated). The
			// sampler mirrors the generator's rows/columns/alignment;
			// rotation carries the marker spin when asked.
			const real_t base_rot_abs = (helper_grid_rotate_with_marker ? track_marker.get_rotation() : 0.0);
			sink.push_dict(BulletPatterns2D::helper_sample_outline_grid(helper_bullets_amount, helper_grid_rows_per_column, helper_grid_alignment, (real_t)helper_grid_column_offset, (real_t)helper_grid_row_offset, base_rot_abs, true));
			break;
		}
		case PATTERN_SHAPE_LATTICE: {
			// Same row-strip sampler family as Grid: columns × rows on
			// a staggered lattice. No per-row jitter (volley noise).
			sink.push_dict(BulletPatterns2D::helper_sample_outline_lattice(helper_bullets_amount, helper_lattice_columns, helper_lattice_rows, (real_t)helper_lattice_spacing_x, (real_t)helper_lattice_spacing_y, helper_lattice_stagger_rows));
			break;
		}
		case PATTERN_SHAPE_WATERFALL: {
			// Explicit row strips, staggered along the rain direction.
			sink.push_dict(BulletPatterns2D::helper_sample_outline_waterfall(helper_bullets_amount, helper_waterfall_columns, (real_t)helper_waterfall_column_spacing, helper_waterfall_rows, (real_t)helper_waterfall_row_spacing, (real_t)helper_waterfall_stagger, helper_waterfall_rain_direction));
			break;
		}
		case PATTERN_SHAPE_RAIN: {
			// Row strips running along the band; the sampler builds
			// the same row grouping as the generator.
			sink.push_dict(BulletPatterns2D::helper_sample_outline_rain(helper_bullets_amount, (real_t)helper_rain_band_width, helper_rain_direction, (real_t)helper_rain_drop_spacing));
			break;
		}
		case PATTERN_SHAPE_WAVE: {
			// The sine sweep itself, at fixed density.
			sink.push_dict(BulletPatterns2D::helper_sample_outline_wave((real_t)helper_wave_width, (real_t)helper_wave_amplitude, (real_t)helper_wave_waves, helper_wave_direction));
			break;
		}
		case PATTERN_SHAPE_SPIRAL: {
			// Arm sweep replicating the generator's (r, angle) formula.
			const real_t base_rot_abs = (helper_spiral_rotate_with_marker ? track_marker.get_rotation() : 0.0);
			sink.push_dict(BulletPatterns2D::helper_sample_outline_spiral(helper_bullets_amount, (real_t)helper_spiral_start_radius, (real_t)helper_spiral_radius_step, (real_t)helper_spiral_angle_step * preview_mirror_sign, base_rot_abs));
			break;
		}
		case PATTERN_SHAPE_MULTISPIRAL: {
			// One strip per arm (INF-separated), each replicating the
			// generator's spiral formula.
			const real_t base_rot_abs = (helper_multispiral_rotate_with_marker ? track_marker.get_rotation() : 0.0);
			sink.push_dict(BulletPatterns2D::helper_sample_outline_multispiral(helper_bullets_amount, helper_multispiral_arms, (real_t)helper_multispiral_start_radius, (real_t)helper_multispiral_radius_step, (real_t)helper_multispiral_angle_step * preview_mirror_sign, base_rot_abs, helper_multispiral_arm_stride));
			break;
		}
		case PATTERN_SHAPE_COUNTER_SPIRAL: {
			// Mirrored-arm variant of multispiral: same strip-per-arm
			// structure, alternate arms wound the other way.
			const real_t base_rot_abs = (helper_counter_spiral_rotate_with_marker ? track_marker.get_rotation() : 0.0);
			sink.push_dict(BulletPatterns2D::helper_sample_outline_counter_spiral(helper_bullets_amount, helper_counter_spiral_arms, (real_t)helper_counter_spiral_start_radius, (real_t)helper_counter_spiral_radius_step, (real_t)helper_counter_spiral_angle_step * preview_mirror_sign, base_rot_abs, helper_counter_spiral_arm_stride, helper_counter_spiral_mirror_alternate_arms));
			break;
		}
		case PATTERN_SHAPE_HEART: {
			// Parametric sweep of the heart curve.
			sink.push_dict(BulletPatterns2D::helper_sample_outline_heart((real_t)helper_heart_size, (real_t)helper_heart_base_rotation));
			break;
		}
		case PATTERN_SHAPE_FLOWER: {
			// Per-type bloom track at fixed density: FAN traces the
			// petal-tip ring, RHODONEA/SPIROGRAPH/SUPERFORMULA trace the
			// actual curve, PHYLLOTAXIS bounds the Vogel disc rim.
			sink.push_dict(BulletPatterns2D::helper_sample_outline_flower(helper_flower_type, helper_flower_petals, (real_t)helper_flower_radius, (real_t)helper_flower_petal_spread, (real_t)helper_flower_petal_sharpness, helper_flower_inner_radius_scale, helper_flower_spiro_roller, helper_flower_spiro_pen, helper_flower_super_lobes, helper_flower_super_fullness, (real_t)helper_flower_base_rotation,
					// FAN on outline: skip petals that hold no bullet.
					helper_outline_placement == BulletPatterns2D::OUTLINE_ON_OUTLINE ? helper_bullets_amount : -1));
			break;
		}
		case PATTERN_SHAPE_STAR_POLYGON: {
			// The bullets sit on the circle of helper_star_polygon_radius
			// (the vertex bias only clusters their angles toward the
			// vertices), so the track is that circle, not the vertex
			// polygon (whose edges cut up to r * (1 - cos(pi / n)) inside).
			sink.push_dict(BulletPatterns2D::helper_sample_outline_circle((real_t)helper_star_polygon_radius));
			break;
		}
		case PATTERN_SHAPE_CROSS: {
			// One ray per lit arm, origin -> its deepest bullet, from the
			// generator's own layout (cross_step2d): the spacing compresses
			// when the arm would overshoot arm_length, so the ray always ends
			// on the outermost bullet. Arms without bullets draw nothing.
			if (helper_cross_arm_count < 1 || !Math::is_finite(helper_cross_arm_length) || helper_cross_arm_length < 0.0 || !Math::is_finite(helper_cross_spacing) || helper_cross_spacing <= 0.0 || helper_bullets_amount <= 0) {
				break;
			}
			const real_t step = cross_step2d(helper_bullets_amount, helper_cross_arm_count, (real_t)helper_cross_arm_length, (real_t)helper_cross_spacing);
			for (int a = 0; a < helper_cross_arm_count; a++) {
				const int depth = cross_arm_bullets2d(helper_bullets_amount, helper_cross_arm_count, a);
				if (depth <= 0) {
					continue;
				}
				const real_t ang = (real_t)helper_cross_base_rotation + Math::TAU * (real_t)a / (real_t)helper_cross_arm_count;
				const Vector2 tip = track_origin + Vector2(Math::cos(ang), Math::sin(ang)) * (step * (real_t)depth);
				if (!tip.is_finite()) {
					continue;
				}
				sink.push_global(track_origin);
				sink.push_global(tip);
			}
			break;
		}
		case PATTERN_SHAPE_FAN: {
			// Cone: two boundary rays from the origin plus an arc at
			// the tip connecting them. Mirrors the generator's spread
			// around marker rotation + direction_angle (the generator
			// composes base_rotation = marker rotation + direction);
			// centered spreads symmetrically.
			// The generator stacks every bullet AT the origin (facing
			// differs), so the cone is the meaningful shape — its
			// radius is a fixed representative length, independent of
			// bullet count.
			if (!Math::is_finite(helper_fan_spread) || helper_fan_spread <= 0.0) {
				break;
			}
			PackedVector2Array cone;
			build_cone_strip(track_origin, track_marker.get_rotation() + (real_t)helper_fan_direction_angle, (real_t)(helper_fan_spread * 0.5), 150.0, (real_t)helper_fan_spread, cone);
			for (int i = 0; i < cone.size(); ++i) {
				sink.push_global(cone[i]);
			}
			break;
		}
		case PATTERN_SHAPE_AIMED: {
			// Cone toward the live target (same shape as FAN). If no
			// target is assigned, draw nothing — match the volley
			// behavior (quiet break, the dots also draw nothing).
			if (!in.has_aim_target) {
				break;
			}
			const Vector2 aim_pos = in.aim_position;
			const Vector2 to_target = aim_pos - track_origin;
			if (!to_target.is_finite() || to_target.length_squared() <= 0.0) {
				break;
			}
			const real_t dir_ang = to_target.angle();
			if (!Math::is_finite(helper_aimed_spread) || helper_aimed_spread <= 0.0) {
				break;
			}
			const real_t tip_r = to_target.length();
			if (!Math::is_finite(tip_r) || tip_r <= 0.0) {
				break;
			}
			PackedVector2Array cone;
			build_cone_strip(track_origin, dir_ang, (real_t)(helper_aimed_spread * 0.5), tip_r, (real_t)helper_aimed_spread, cone);
			for (int i = 0; i < cone.size(); ++i) {
				sink.push_global(cone[i]);
			}
			break;
		}
		case PATTERN_SHAPE_CORRIDOR: {
			// The two wall runs flanking the gap, mirroring the
			// generator's even spread: slots fill [-w/2, +w/2]
			// skipping |x| < gap/2, so the track draws exactly the
			// two runs the volley occupies. Two push pairs = two
			// strips (no bridge across the dodge door).
			const Vector2 corridor_aim = in.corridor_aim;
			if (!corridor_aim.is_finite() || corridor_aim.length_squared() <= 0.0) {
				break;
			}
			if (!Math::is_finite(helper_corridor_width) || helper_corridor_width <= 0.0) {
				break;
			}
			const real_t half_width = (real_t)(helper_corridor_width * 0.5);
			const real_t half_gap = Math::is_finite(helper_corridor_gap_width) && helper_corridor_gap_width > 0.0
					? (real_t)(helper_corridor_gap_width * 0.5)
					: 0.0;
			if (half_gap >= half_width) {
				break;
			}
			const Vector2 aim = corridor_aim.normalized();
			const Vector2 perp = aim.orthogonal();
			sink.push_global(track_origin - perp * half_width);
			sink.push_global(track_origin - perp * half_gap);
			// Separator: the polyline must not bridge the dodge door.
			sink.push_separator();
			sink.push_global(track_origin + perp * half_gap);
			sink.push_global(track_origin + perp * half_width);
			break;
		}
		case PATTERN_SHAPE_SCATTER: {
			// Burst-disc extent ring (dots carry density). A narrowed
			// sector draws its arc instead, so the track never
			// overstates the cone. Both are marker-relative offsets
			// like the factory samplers (origin + offset), not locals.
			if (helper_scatter_arc < Math::TAU && Math::is_finite(helper_scatter_arc) && helper_scatter_arc > 0.0 &&
					helper_scatter_direction.is_finite() && helper_scatter_direction.length_squared() > 1e-12 &&
					Math::is_finite(helper_scatter_burst_radius) && helper_scatter_burst_radius > 0.0) {
				const real_t base = helper_scatter_direction.normalized().angle();
				const real_t half = (real_t)(helper_scatter_arc * 0.5);
				const int arc_n = MAX(8, MIN(64, (int)(helper_scatter_arc * 16.0)));
				for (int i = 0; i <= arc_n; i++) {
					const real_t ang = base - half + (real_t)helper_scatter_arc * (real_t)i / (real_t)arc_n;
					sink.push_global(track_origin + Vector2(Math::cos(ang), Math::sin(ang)) * (real_t)helper_scatter_burst_radius);
				}
				sink.set_closed(false);
			} else {
				sink.push_dict(BulletPatterns2D::helper_sample_outline_circle(helper_scatter_burst_radius));
			}
			break;
		}
		default:
			break;
	}
}

bool PatternKnobs2D::outline_layer_rings_drawable(int source) const {
	// Quiet validation mirror of the factory: rings draw only for
	// settings the volley accepts (custom scales checked entry-wise).
	bool layers_ok = pattern_shape_supports_outline2d(source) && helper_outline_placement == (int)BulletPatterns2D::OUTLINE_LAYERS && helper_outline_layer_count > 1 && Math::is_finite(helper_outline_layer_scale) && helper_outline_layer_scale > 0.0 && helper_outline_layer_scale <= 8.0 && helper_outline_layer_side >= (int)BulletPatterns2D::OUTLINE_LAYER_OUTWARD && helper_outline_layer_side <= (int)BulletPatterns2D::OUTLINE_LAYER_BOTH && helper_outline_layer_fill >= (int)BulletPatterns2D::OUTLINE_LAYER_INTERLEAVED && helper_outline_layer_fill <= (int)BulletPatterns2D::OUTLINE_LAYER_PINGPONG && helper_outline_layer_start_offset >= 0 && helper_outline_layer_scale_curve >= (int)BulletPatterns2D::OUTLINE_LAYER_CURVE_LINEAR && helper_outline_layer_scale_curve <= (int)BulletPatterns2D::OUTLINE_LAYER_CURVE_EXPONENTIAL && helper_outline_layer_scales.size() <= 64;
	if (layers_ok) {
		for (int ci = 0; ci < helper_outline_layer_scales.size(); ++ci) {
			const double cs = (double)helper_outline_layer_scales[ci];
			if (!Math::is_finite(cs) || cs < 0.05 || cs > 64.0) {
				layers_ok = false;
				break;
			}
		}
	}
	return layers_ok;
}

} //namespace BlastBullets2D
