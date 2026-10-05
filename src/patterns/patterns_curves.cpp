// Closed-curve pattern generators routed through the outline layout engine:
// ring, circle, ellipse, flower, star, heart, rose and lissajous.

#include "patterns/pattern_curves2d.hpp"
#include "patterns/patterns_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Even arc-length parameters for an axis-aligned ellipse arc
// (cos(t)*rx, sin(t)*ry), t in [start, start+span]. A dense chord table
// (720 samples) is walked uniformly: closed loops divide by n (no seam
// duplicate), open arcs pin both endpoints (divide by n-1 in ARC LENGTH, so
// endpoints stay exact while interior gaps stay even). For circles
// (rx == ry) this reproduces angle-even placement exactly.
static void even_ellipse_params(real_t start, real_t span, bool closed, int count, real_t rx, real_t ry, PackedFloat64Array &r_ts) {
	r_ts.clear();
	if (count <= 0) {
		return;
	}
	if (count == 1) {
		r_ts.push_back((double)start);
		return;
	}
	const int dense = 720;
	PackedFloat64Array cum;
	cum.resize(dense + 1);
	cum[0] = 0.0;
	double prev_x = (double)rx * Math::cos((double)start);
	double prev_y = (double)ry * Math::sin((double)start);
	for (int k = 1; k <= dense; ++k) {
		const double tt = (double)start + (double)span * (double)k / (double)dense;
		const double px = (double)rx * Math::cos(tt);
		const double py = (double)ry * Math::sin(tt);
		const double dx = px - prev_x;
		const double dy = py - prev_y;
		const double seg = Math::sqrt(dx * dx + dy * dy);
		cum[k] = cum[k - 1] + (Math::is_finite(seg) ? seg : 0.0);
		prev_x = px;
		prev_y = py;
	}
	const double total = cum[dense];
	if (!(total > 0.0) || !Math::is_finite(total)) {
		for (int i = 0; i < count; ++i) {
			r_ts.push_back((double)start);
		}
		return;
	}
	int seg = 0;
	for (int i = 0; i < count; ++i) {
		const double target = closed ? (total * (double)i / (double)count)
									 : (total * (double)i / (double)(count - 1));
		while (seg < dense - 1 && cum[seg + 1] < target) {
			++seg;
		}
		const double seg_len = cum[seg + 1] - cum[seg];
		double f = (seg_len > 1e-12) ? (target - cum[seg]) / seg_len : 0.0;
		f = Math::clamp(f, 0.0, 1.0);
		r_ts.push_back((double)start + (double)span * ((double)seg + f) / (double)dense);
	}
}

int spirograph_revolutions(double k, int max_m) {
	if (!Math::is_finite(k) || Math::abs(k) < 1e-9) {
		return 1;
	}
	for (int m = 1; m <= max_m; ++m) {
		const double v = k * (double)m;
		if (Math::abs(v - Math::round(v)) < 1e-2) {
			return m;
		}
	}
	return max_m;
}

// WALL placement: gap g is centred at start + span * (g + 0.5) / gap_count
// with width gap_width (parameter radians). The solid stretches between the
// gaps share all `count` bullets in proportion to their arc length
// (largest-remainder rounding), each spanning edge to edge, so the count is
// exact and no bullet sits in a gap. False when the gaps leave no wall.
static bool wall_ellipse_params(real_t start, real_t span, bool closed, int gap_count, real_t gap_width, int count, real_t rx, real_t ry, PackedFloat64Array &r_ts) {
	r_ts.clear();
	const double dir = span < 0.0 ? -1.0 : 1.0;
	const double len = Math::abs((double)span);
	const double half = (double)gap_width * 0.5;
	std::vector<std::pair<double, double>> solids;
	auto center = [&](int g) { return len * ((double)g + 0.5) / (double)gap_count; };
	if (closed) {
		for (int g = 0; g < gap_count; ++g) {
			const double a = center(g) + half;
			const double b = (g + 1 < gap_count ? center(g + 1) : center(0) + len) - half;
			if (b > a + 1e-9) {
				solids.push_back({ a, b });
			}
		}
	} else {
		double prev = 0.0;
		for (int g = 0; g < gap_count; ++g) {
			const double b = center(g) - half;
			if (b > prev + 1e-9) {
				solids.push_back({ prev, b });
			}
			prev = Math::max(prev, center(g) + half);
		}
		if (len > prev + 1e-9) {
			solids.push_back({ prev, len });
		}
	}
	if (solids.empty()) {
		return false;
	}
	auto point_at = [&](double u) {
		const double t = (double)start + dir * u;
		return Vector2((real_t)((double)rx * Math::cos(t)), (real_t)((double)ry * Math::sin(t)));
	};
	std::vector<double> lengths;
	double total = 0.0;
	for (const std::pair<double, double> &sd : solids) {
		double l = 0.0;
		Vector2 prev = point_at(sd.first);
		for (int q = 1; q <= 64; ++q) {
			const Vector2 p = point_at(sd.first + (sd.second - sd.first) * (double)q / 64.0);
			l += (double)prev.distance_to(p);
			prev = p;
		}
		// Circles of radius 0 still split by parameter length.
		lengths.push_back(l > 1e-12 ? l : sd.second - sd.first);
		total += lengths.back();
	}
	std::vector<int> counts(solids.size(), 0);
	std::vector<std::pair<double, int>> rema;
	int given = 0;
	for (size_t k = 0; k < solids.size(); ++k) {
		const double exact = (double)count * lengths[k] / total;
		counts[k] = (int)Math::floor(exact);
		given += counts[k];
		rema.push_back({ exact - (double)counts[k], (int)k });
	}
	std::stable_sort(rema.begin(), rema.end(), [](const std::pair<double, int> &x, const std::pair<double, int> &y) { return x.first > y.first; });
	for (size_t r = 0; given < count && r < rema.size(); ++r, ++given) {
		counts[rema[r].second]++;
	}
	for (size_t k = 0; k < solids.size(); ++k) {
		const int c = counts[k];
		if (c <= 0) {
			continue;
		}
		const double a = solids[k].first;
		const double b = solids[k].second;
		if (c == 1) {
			r_ts.push_back((double)start + dir * (a + b) * 0.5);
			continue;
		}
		PackedFloat64Array part;
		even_ellipse_params((real_t)((double)start + dir * a), (real_t)(dir * (b - a)), false, c, rx, ry, part);
		r_ts.append_array(part);
	}
	return r_ts.size() == count;
}

// Lissajous x = sin(fx t + phase), y = sin(fy t): the parameter window that
// draws the curve exactly once. Integer frequencies with gcd g close after
// TAU / g. The curve is traced back and forth (an open arc) when it is
// symmetric under t -> c - t, i.e. b*c = PI (mod TAU) and
// a*c + 2*phase = PI (mod TAU) for the reduced a, b; then [c/2, c/2 + PI/g']
// is one pass. Non-integer frequencies keep the full turn.
void lissajous_sweep2d(double fx, double fy, double phase, double &r_t0, double &r_span, bool &r_open) {
	r_t0 = 0.0;
	r_span = Math::TAU;
	r_open = false;
	const double ax = Math::round(fx);
	const double ay = Math::round(fy);
	if (Math::abs(fx - ax) > 1e-9 || Math::abs(fy - ay) > 1e-9 || ax < 0.0 || ay < 0.0 || (ax == 0.0 && ay == 0.0) || ax > 1e6 || ay > 1e6) {
		return;
	}
	int64_t a = (int64_t)ax;
	int64_t b = (int64_t)ay;
	int64_t g = a;
	for (int64_t r = b; r != 0;) {
		const int64_t t = g % r;
		g = r;
		r = t;
	}
	if (g <= 0) {
		return;
	}
	a /= g;
	b /= g;
	const double period = Math::TAU / (double)g; // one closure
	r_span = period;
	// Back-and-forth test in the reduced parameter u = g*t (period TAU):
	// a turning time c with p(u) == p(c - u) for every u. A zero frequency
	// makes its axis constant (always symmetric).
	auto wrapped = [](double v) { return Math::abs(Math::fposmod(v + Math::PI, Math::TAU) - Math::PI); };
	const int64_t lead = b != 0 ? b : a;
	for (int64_t n = 0; n < 2 * lead && n < 4096; ++n) {
		const double c = (b != 0) ? (Math::PI + Math::TAU * (double)n) / (double)b
								  : (Math::PI - 2.0 * phase + Math::TAU * (double)n) / (double)a;
		const bool x_sym = (a == 0) || wrapped((double)a * c + 2.0 * phase - Math::PI) < 1e-7;
		const bool y_sym = (b == 0) || wrapped((double)b * c - Math::PI) < 1e-7;
		if (x_sym && y_sym) {
			r_open = true;
			r_t0 = (c * 0.5) / (double)g;
			r_span = period * 0.5;
			return;
		}
	}
}

PatternSlots2D BulletPatterns2D::generate_ring2d(int transforms_amount, Transform2D marker_transform, const RingParams2D &params) {
	real_t radius = params.radius;
	real_t start_angle = params.start_angle;
	real_t arc = params.arc;
	bool rotate_with_marker = params.rotate_with_marker;
	bool random_rotation = params.random_rotation;
	bool face_outward = params.face_outward;
	real_t y_scale = params.y_scale;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_placement = params.outline.outline_placement;
	uint64_t seed = params.seed;
	int layer_layout = params.outline.layer_layout;

	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (transforms_amount < 0 || transforms_amount > HELPER_MAX_TRANSFORMS) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: transforms_amount must be between 0 and " + String::num_int64(HELPER_MAX_TRANSFORMS) + ".");
		return PatternSlots2D();
	}
	if (!Math::is_finite(radius) || !Math::is_finite(start_angle) || !Math::is_finite(arc)) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: radius, start_angle and arc must be finite numbers.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(y_scale) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: y_scale and facing_offset_degrees must be finite numbers.");
		return PatternSlots2D();
	}
	if (!marker_transform.get_origin().is_finite() || !Math::is_finite(marker_transform.get_rotation()) || !marker_transform.get_scale().is_finite()) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: marker_transform contains NaN/Inf.");
		return PatternSlots2D();
	}
	// The outline layout inverts the marker (global loop -> slot space):
	// a singular marker would poison every slot, so reject it here like
	// danmaku_validate_head does for the other layout users.
	if (!is_transform_invertible_safe2d(marker_transform)) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: marker_transform is singular (zero or degenerate scale); volley skipped.");
		return PatternSlots2D();
	}
	if (radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: radius must be >= 0.");
		return PatternSlots2D();
	}
	const real_t base_rotation = rotate_with_marker ? marker_transform.get_rotation() : 0.0;
	// Closed ring (arc ~= TAU): divide by n so first/last don't stack on the same
	// spot. Open arcs keep the n-1 divisor so the endpoints land on start/arc end.
	// Placement is even by ARC LENGTH (even_ellipse_params), so stretched rings
	// (y_scale != 1) keep uniform gaps instead of bunching at the flanks;
	// circles reproduce angle-even placement exactly.
	// Closed when the arc is a whole turn or more, or so close to one that the
	// open layout would leave the last bullet within half a slot of the
	// first (6.28 typed by hand). Arcs past a full turn would retrace.
	const real_t ring_half_slot = (transforms_amount > 1) ? Math::abs(arc) * 0.5 / (real_t)(transforms_amount - 1) : 0.0;
	const bool is_closed_ring = Math::abs(arc) >= Math::TAU - Math::max(ring_half_slot, (real_t)0.0001);
	const real_t ring_span = is_closed_ring ? (arc < 0.0 ? -Math::TAU : Math::TAU) : arc;
	PackedFloat64Array ring_ts;
	even_ellipse_params(start_angle, ring_span, is_closed_ring, transforms_amount, radius, radius * y_scale, ring_ts);
	// Slot loop in global space plus geometric (radial) outward normals; the
	// shared outline worker assembles facings so placement/fill/shell stay
	// uniform. Random rotation survives as a per-slot facing override.
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	PackedFloat32Array facing_override;
	loop_points.resize(transforms_amount);
	loop_normals.resize(transforms_amount);
	if (random_rotation) {
		facing_override.resize(transforms_amount);
	}
	Ref<RandomNumberGenerator> ring_rng;
	const bool ring_seeded = seed != 0;
	if (ring_seeded) {
		ring_rng.instantiate();
		ring_rng->set_seed(seed);
	}
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t angle = base_rotation + (i < ring_ts.size() ? (real_t)ring_ts[i] : start_angle);
		loop_points[i] = marker_transform.get_origin() + Vector2(Math::cos(angle) * radius, Math::sin(angle) * radius * y_scale);
		loop_normals[i] = Vector2(Math::cos(angle), Math::sin(angle));
		if (random_rotation) {
			facing_override[i] = (ring_seeded ? ring_rng->randf() : UtilityFunctions::randf()) * Math::TAU;
		}
	}
	PackedVector2Array fill_outline;
	if (outline_placement == OUTLINE_FILL_INSIDE) {
		PackedFloat64Array dense_ts;
		even_ellipse_params(start_angle, ring_span, is_closed_ring, 256, radius, radius * y_scale, dense_ts);
		for (int q = 0; q < dense_ts.size(); ++q) {
			const real_t angle = base_rotation + (real_t)dense_ts[q];
			fill_outline.push_back(marker_transform.get_origin() + Vector2(Math::cos(angle) * radius, Math::sin(angle) * radius * y_scale));
		}
	}
	return layout_outline_slots("helper_generate_transforms_ring", marker_transform, loop_points, loop_normals, false, 0.0, face_outward, facing_offset_degrees, facing_override,  params.outline, CornerLayout2D::smooth(), PackedVector2Array(), is_closed_ring, true, fill_outline);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_ring(
		int transforms_amount,
		Transform2D marker_transform,
		real_t radius,
		real_t start_angle,
		real_t arc,
		bool rotate_with_marker,
		bool random_rotation,
		bool face_outward,
		real_t y_scale,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		uint64_t seed,
		int layer_layout) {
	RingParams2D params;
	params.radius = radius;
	params.start_angle = start_angle;
	params.arc = arc;
	params.rotate_with_marker = rotate_with_marker;
	params.random_rotation = random_rotation;
	params.face_outward = face_outward;
	params.y_scale = y_scale;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.seed = seed;
	params.outline.layer_layout = layer_layout;
	return pattern_slots_to_array(generate_ring2d(transforms_amount, marker_transform, params));
}

PatternSlots2D BulletPatterns2D::generate_flower2d(int transforms_amount, Transform2D marker_transform, const FlowerParams2D &params) {
	int petals = params.petals;
	real_t radius = params.radius;
	real_t petal_spread = params.petal_spread;
	real_t petal_sharpness = params.petal_sharpness;
	real_t base_rotation = params.base_rotation;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_placement = params.outline.outline_placement;
	int flower_type = params.flower_type;
	double inner_radius_scale = params.inner_radius_scale;
	double spiro_roller = params.spiro_roller;
	double spiro_pen = params.spiro_pen;
	double super_lobes = params.super_lobes;
	double super_fullness = params.super_fullness;
	int layer_layout = params.outline.layer_layout;

	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (!danmaku_validate_head("helper_generate_transforms_flower", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (flower_type < FLOWER_FAN || flower_type > FLOWER_SUPERFORMULA) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: unknown flower_type.");
		return PatternSlots2D();
	}
	if (petals < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: petals must be >= 1.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(radius) || radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: radius must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(petal_spread) || petal_spread < 0.0 || !Math::is_finite(petal_sharpness) || petal_sharpness < 0.0 || !Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: petal_spread, petal_sharpness, base_rotation and facing_offset_degrees must be finite (spreads/sharpness >= 0).");
		return PatternSlots2D();
	}
	if (!Math::is_finite(inner_radius_scale) || inner_radius_scale < 0.0 || inner_radius_scale >= 1.0) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: inner_radius_scale must be finite in [0, 1).");
		return PatternSlots2D();
	}
	if (!Math::is_finite(spiro_roller) || spiro_roller <= 0.0 || !Math::is_finite(spiro_pen) || spiro_pen < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: spiro_roller must be finite and > 0, spiro_pen finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(super_lobes) || super_lobes < 2.0 || super_lobes > 64.0 || !Math::is_finite(super_fullness) || super_fullness <= 0.0 || super_fullness > 8.0) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: super_lobes must be finite in [2, 64], super_fullness finite in (0, 8].");
		return PatternSlots2D();
	}
	// Shared golden angle for the phyllotaxis disc.
	const double golden_angle = Math::PI * (3.0 - Math::sqrt(5.0));
	// Build loop_points/loop_normals per bloom kind, then the shared outline
	// worker assembles facings (fill uses the angular-sorted silhouette).
	// Continuous sweeps (RHODONEA/SPIROGRAPH/SUPERFORMULA) build arc-even
	// from a dense ideal sweep; FAN/PHYLLOTAXIS keep their legacy slot order
	// (petal-major fan, golden-angle disc: not spatial loops).
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	PackedVector2Array fill_outline; // dense curve for Fill / Layers (count-independent)
	PackedVector2Array fill_normals;
	const Vector2 origin = marker_transform.get_origin();
	if (flower_type == FLOWER_FAN) {
		// Petal-major fan: the amount is split over the petals
		// (flower_fan_petal_count spreads the remainder symmetrically), so
		// every bullet gets its own slot at any amount. Slots fan across
		// petal_spread around each lobe axis; when neighboring fans touch or
		// overlap (spread * petals >= TAU) the slots are half-open so the edge
		// slots of two petals never coincide.
		const bool fans_touch = petal_spread * (real_t)petals >= Math::TAU - 1e-6;
		for (int petal = 0; petal < petals; ++petal) {
			const int in_petal = flower_fan_petal_count(petal, petals, transforms_amount);
			const real_t lobe_center = base_rotation + Math::TAU * (real_t)petal / (real_t)petals;
			for (int s = 0; s < in_petal; ++s) {
				real_t frac = 0.0;
				if (fans_touch) {
					frac = ((real_t)s + 0.5) / (real_t)in_petal - 0.5;
				} else if (in_petal > 1) {
					frac = (real_t)s / (real_t)(in_petal - 1) - 0.5;
				}
				const real_t angle = lobe_center + frac * petal_spread;
				// Rhodonea-style radius modulation: sharpness pinches the waist
				// between lobes so higher values read as tighter flowers.
				const real_t waist = flower_fan_waist2d(petal_sharpness, frac);
				loop_points.push_back(origin + Vector2(Math::cos(angle), Math::sin(angle)) * (radius * waist));
				loop_normals.push_back(Vector2(Math::cos(angle), Math::sin(angle)));
			}
		}
		if (outline_placement == OUTLINE_FILL_INSIDE) {
			// Silhouette of every petal arc (independent of the amount).
			PackedVector2Array arcs;
			for (int petal = 0; petal < petals; ++petal) {
				const real_t lobe_center = base_rotation + Math::TAU * (real_t)petal / (real_t)petals;
				for (int q = 0; q <= 16; ++q) {
					const real_t frac = (real_t)q / 16.0 - 0.5;
					const real_t angle = lobe_center + frac * petal_spread;
					const real_t waist = flower_fan_waist2d(petal_sharpness, frac);
					arcs.push_back(origin + Vector2(Math::cos(angle), Math::sin(angle)) * (radius * waist));
				}
			}
			Vector2 unused_center;
			outline_build_boundary(arcs, fill_outline, unused_center);
		}
	} else if (flower_type == FLOWER_RHODONEA) {
		// Continuous rhodonea sweep r = R*|cos(k*theta/2)|^p over the slot
		// loop; the inner scale lifts the waist into a ring when asked.
		// Arc-even from a dense ideal sweep so dots sit exactly on the
		// curve with even gaps (param sweeps bunch where the curve runs slow).
		const real_t sharp = (petal_sharpness < 0.0) ? 0.0 : petal_sharpness;
		PackedVector2Array dense_pts;
		PackedVector2Array dense_nrms;
		for (int k = 0; k < 720; ++k) {
			const real_t theta = Math::TAU * (real_t)k / 720.0 + base_rotation;
			const real_t r = flower_rhodonea_radius2d(petals, theta, radius, sharp, inner_radius_scale);
			dense_pts.push_back(Vector2(Math::cos(theta), Math::sin(theta)) * r);
			const Vector2 radial = Vector2(Math::cos(theta), Math::sin(theta));
			dense_nrms.push_back((radial.length_squared() > 1e-12) ? radial : Vector2(1, 0));
		}
		PackedVector2Array even_local;
		PackedVector2Array even_nrms;
		PackedFloat32Array even_ovr;
		resample_loop_even_distinct(dense_pts, dense_nrms, PackedFloat32Array(), transforms_amount, true, even_local, even_nrms, even_ovr);
		fill_outline = fill_outline_from(outline_placement, dense_pts, origin);
		fill_normals = fill_outline.is_empty() ? PackedVector2Array() : dense_nrms;
		for (int i = 0; i < even_local.size(); ++i) {
			loop_points.push_back(origin + even_local[i]);
			loop_normals.push_back(even_nrms[i]);
		}
	} else if (flower_type == FLOWER_PHYLLOTAXIS) {
		if (outline_placement == OUTLINE_FILL_INSIDE) {
			for (int q = 0; q < 128; ++q) {
				const real_t a = base_rotation + Math::TAU * (real_t)q / 128.0;
				fill_outline.push_back(origin + Vector2(Math::cos(a), Math::sin(a)) * radius);
			}
		}
		// Vogel golden-angle disc: slot i sits at angle i*GA, radius
		// R*sqrt((i+0.5)/n) blended from the inner edge outward.
		for (int i = 0; i < transforms_amount; ++i) {
			const double frac = (transforms_amount > 0) ? ((double)i + 0.5) / (double)transforms_amount : 0.0;
			const real_t angle = base_rotation + (real_t)((double)i * golden_angle);
			const real_t r = radius * (real_t)(inner_radius_scale + (1.0 - inner_radius_scale) * Math::sqrt(Math::clamp(frac, 0.0, 1.0)));
			loop_points.push_back(origin + Vector2(Math::cos(angle), Math::sin(angle)) * r);
			const Vector2 radial = Vector2(Math::cos(angle), Math::sin(angle));
			loop_normals.push_back((radial.length_squared() > 1e-12) ? radial : Vector2(1, 0));
		}
	} else if (flower_type == FLOWER_SPIROGRAPH) {
		// Hypotrochoid: x = (R-r)cos t + d cos((R-r)t/r),
		// y = (R-r)sin t - d sin((R-r)t/r). Clamp wild rollers so huge
		// values cannot NaN the loop.
		const Spirograph2D spiro = spirograph_setup2d(radius, spiro_roller, spiro_pen);
		// Sweep the full closure: with k = 7/3 (R=150,r=45) the curve only
		// closes after 3 revolutions, so a single 0..TAU pass draws 1/3 of it.
		// Degenerate rollers would stack bullets: pen 0 is a plain circle of
		// radius R-r (one turn, not `revolutions` laps over itself), and a
		// roller equal to R pins the centre so the pen draws a circle of
		// radius pen instead of a single point.
		const bool pen_free = spiro_pen <= 1e-9;
		const int revolutions = (spiro.centre_pinned || pen_free) ? 1 : spirograph_revolutions(spiro.k);
		const int dense_n = spirograph_dense_samples(revolutions);
		// Arc-even from a dense ideal sweep (multi-turn closure included).
		PackedVector2Array dense_pts;
		PackedVector2Array dense_nrms;
		for (int q = 0; q < dense_n; ++q) {
			const double t = Math::TAU * (double)revolutions * (double)q / (double)dense_n;
			Vector2 local = spirograph_point2d(spiro, t).rotated(base_rotation);
			if (!local.is_finite()) {
				local = Vector2(0, 0);
			}
			dense_pts.push_back(local);
			dense_nrms.push_back((local.length_squared() > 1e-12) ? local.normalized() : Vector2(1, 0));
		}
		PackedVector2Array even_local;
		PackedVector2Array even_nrms;
		PackedFloat32Array even_ovr;
		resample_loop_even_distinct(dense_pts, dense_nrms, PackedFloat32Array(), transforms_amount, true, even_local, even_nrms, even_ovr);
		fill_outline = fill_outline_from(outline_placement, dense_pts, origin);
		fill_normals = fill_outline.is_empty() ? PackedVector2Array() : dense_nrms;
		for (int i = 0; i < even_local.size(); ++i) {
			loop_points.push_back(origin + even_local[i]);
			loop_normals.push_back(even_nrms[i]);
		}
	} else {
		// Simplified Gielis superformula with a = b = 1, n2 = n3 = fullness:
		// r = (|cos(mt/4)|^f + |sin(mt/4)|^f)^(-1/f). m = super_lobes.
		// Arc-even from a dense ideal sweep.
		PackedVector2Array dense_pts;
		PackedVector2Array dense_nrms;
		for (int q = 0; q < 720; ++q) {
			const double t = Math::TAU * (double)q / 720.0;
			const double r_norm = superformula_norm2d(super_lobes, super_fullness, t);
			const real_t r = radius * (real_t)(inner_radius_scale + (1.0 - inner_radius_scale) * (r_norm * 0.5));
			const real_t ang = base_rotation + (real_t)t;
			dense_pts.push_back(Vector2(Math::cos(ang), Math::sin(ang)) * r);
			const Vector2 radial = Vector2(Math::cos(ang), Math::sin(ang));
			dense_nrms.push_back((radial.length_squared() > 1e-12) ? radial : Vector2(1, 0));
		}
		PackedVector2Array even_local;
		PackedVector2Array even_nrms;
		PackedFloat32Array even_ovr;
		resample_loop_even_distinct(dense_pts, dense_nrms, PackedFloat32Array(), transforms_amount, true, even_local, even_nrms, even_ovr);
		fill_outline = fill_outline_from(outline_placement, dense_pts, origin);
		fill_normals = fill_outline.is_empty() ? PackedVector2Array() : dense_nrms;
		for (int i = 0; i < even_local.size(); ++i) {
			loop_points.push_back(origin + even_local[i]);
			loop_normals.push_back(even_nrms[i]);
		}
	}
	// The flower always lays its layers out on one shared loop.
	OutlineLayout2D flower_outline = params.outline;
	flower_outline.layer_layout = 0;
	return layout_outline_slots("helper_generate_transforms_flower", marker_transform, loop_points, loop_normals, false, 0.0, face_outward, facing_offset_degrees, PackedFloat32Array(),  flower_outline, CornerLayout2D::smooth(), PackedVector2Array(), true, flower_type != FLOWER_FAN && flower_type != FLOWER_PHYLLOTAXIS, fill_outline, fill_normals);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_flower(
		int transforms_amount,
		Transform2D marker_transform,
		int petals,
		real_t radius,
		real_t petal_spread,
		real_t petal_sharpness,
		real_t base_rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int flower_type,
		double inner_radius_scale,
		double spiro_roller,
		double spiro_pen,
		double super_lobes,
		double super_fullness,
		int layer_layout) {
	FlowerParams2D params;
	params.petals = petals;
	params.radius = radius;
	params.petal_spread = petal_spread;
	params.petal_sharpness = petal_sharpness;
	params.base_rotation = base_rotation;
	params.face_outward = face_outward;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.flower_type = flower_type;
	params.inner_radius_scale = inner_radius_scale;
	params.spiro_roller = spiro_roller;
	params.spiro_pen = spiro_pen;
	params.super_lobes = super_lobes;
	params.super_fullness = super_fullness;
	params.outline.layer_layout = layer_layout;
	return pattern_slots_to_array(generate_flower2d(transforms_amount, marker_transform, params));
}

PatternSlots2D BulletPatterns2D::generate_ellipse2d(int transforms_amount, Transform2D marker_transform, const EllipseParams2D &params) {
	real_t radius_x = params.radius_x;
	real_t radius_y = params.radius_y;
	real_t ellipse_rotation = params.ellipse_rotation;
	real_t start_angle = params.start_angle;
	real_t arc = params.arc;
	EllipseMode mode = params.mode;
	int gap_count = params.gap_count;
	real_t gap_width = params.gap_width;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_placement = params.outline.outline_placement;
	int layer_layout = params.outline.layer_layout;

	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (!danmaku_validate_head("helper_generate_transforms_ellipse", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(radius_x) || radius_x < 0.0 || !Math::is_finite(radius_y) || radius_y < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: radius_x and radius_y must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(ellipse_rotation) || !Math::is_finite(start_angle) || !Math::is_finite(arc) || !Math::is_finite(gap_width) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: ellipse_rotation, start_angle, arc, gap_width and facing_offset_degrees must be finite.");
		return PatternSlots2D();
	}
	if (mode < ELLIPSE_FULL || mode > ELLIPSE_WALL) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: unknown mode.");
		return PatternSlots2D();
	}
	if (gap_count < 0) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: gap_count must be >= 0.");
		return PatternSlots2D();
	}
	// Uncapped gap_count turns the per-bullet gap loop below into O(n*gap):
	// n=1000 with gap=INT_MAX would hang for hours. Walls don't need more
	// gaps than bullets anyway.
	if (gap_count > HELPER_MAX_TRANSFORMS) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: gap_count is absurdly large; keep it near the bullet count.");
		return PatternSlots2D();
	}
	if (gap_width < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: gap_width must be >= 0.");
		return PatternSlots2D();
	}
	// Slot loop in global space plus geometric (gradient) outward normals.
	// WALL gaps filter the loop before the shared outline worker sees it, so
	// reverse/offset/fill/shell all operate on the surviving slots.
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	const Vector2 origin = marker_transform.get_origin();
	const real_t ellipse_cos = Math::cos(ellipse_rotation);
	const real_t ellipse_sin = Math::sin(ellipse_rotation);
	// WALL mode needs dense coverage to resolve gaps: map slots evenly over
	// the arc, then drop the ones landing inside a gap. FULL closes the loop
	// (divide by n, no duplicated seam bullet like the old n-1 divisor);
	// ARC/WALL keep endpoints (divide by n-1 in arc length). Placement is
	// even by ARC LENGTH, so non-circular ellipses keep uniform gaps instead
	// of bunching at the major-axis ends.
	const bool is_wall = (mode == ELLIPSE_WALL && gap_count > 0 && gap_width > 0.0);
	// An ARC/WALL spanning a whole turn (or within half a slot of one) is a
	// closed ring: the n-1 divisor would put the seam bullet on bullet 0.
	const real_t half_slot = (transforms_amount > 1) ? Math::abs(arc) * 0.5 / (real_t)(transforms_amount - 1) : 0.0;
	const bool is_closed = (mode == ELLIPSE_FULL) || Math::abs(arc) >= Math::TAU - half_slot;
	const real_t span = is_closed ? ((mode != ELLIPSE_FULL && arc < 0.0) ? -Math::TAU : Math::TAU) : arc;
	PackedFloat64Array ell_ts;
	if (is_wall) {
		if (!wall_ellipse_params(start_angle, span, is_closed, gap_count, gap_width, transforms_amount, radius_x, radius_y, ell_ts)) {
			UtilityFunctions::push_error("helper_generate_transforms_ellipse: the gaps cover the whole wall (lower gap_width or gap_count).");
			return PatternSlots2D();
		}
	} else {
		even_ellipse_params(start_angle, span, is_closed, transforms_amount, radius_x, radius_y, ell_ts);
	}
	const real_t rx2 = Math::max((real_t)(radius_x * radius_x), (real_t)0.0001);
	const real_t ry2 = Math::max((real_t)(radius_y * radius_y), (real_t)0.0001);
	for (int i = 0; i < ell_ts.size(); ++i) {
		const real_t t = (real_t)ell_ts[i];
		const real_t ex = Math::cos(t) * radius_x;
		const real_t ey = Math::sin(t) * radius_y;
		loop_points.push_back(origin + Vector2(ex * ellipse_cos - ey * ellipse_sin, ex * ellipse_sin + ey * ellipse_cos));
		// Outward normal: the gradient in the ellipse's own frame
		// (ex/rx^2, ey/ry^2), then rotated with the ellipse.
		const Vector2 local_n = Vector2(ex / rx2, ey / ry2);
		Vector2 normal = Vector2(local_n.x * ellipse_cos - local_n.y * ellipse_sin, local_n.x * ellipse_sin + local_n.y * ellipse_cos);
		if (normal.length_squared() <= 0.0 || !normal.is_finite()) {
			normal = Vector2(Math::cos(t + ellipse_rotation), Math::sin(t + ellipse_rotation));
		}
		loop_normals.push_back(normal.normalized());
	}
	PackedVector2Array fill_outline;
	if (outline_placement == OUTLINE_FILL_INSIDE) {
		PackedFloat64Array dense_ts;
		even_ellipse_params(start_angle, span, is_closed, 256, radius_x, radius_y, dense_ts);
		for (int q = 0; q < dense_ts.size(); ++q) {
			const real_t ex = Math::cos((real_t)dense_ts[q]) * radius_x;
			const real_t ey = Math::sin((real_t)dense_ts[q]) * radius_y;
			fill_outline.push_back(origin + Vector2(ex * ellipse_cos - ey * ellipse_sin, ex * ellipse_sin + ey * ellipse_cos));
		}
	}
	// WALL keeps shared-loop layers (resampling would pave over the gaps).
	return layout_outline_slots("helper_generate_transforms_ellipse", marker_transform, loop_points, loop_normals, false, 0.0, face_outward, facing_offset_degrees, PackedFloat32Array(),  params.outline, CornerLayout2D::smooth(), PackedVector2Array(), is_closed, !is_wall, fill_outline);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_ellipse(
		int transforms_amount,
		Transform2D marker_transform,
		real_t radius_x,
		real_t radius_y,
		real_t ellipse_rotation,
		real_t start_angle,
		real_t arc,
		EllipseMode mode,
		int gap_count,
		real_t gap_width,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int layer_layout) {
	EllipseParams2D params;
	params.radius_x = radius_x;
	params.radius_y = radius_y;
	params.ellipse_rotation = ellipse_rotation;
	params.start_angle = start_angle;
	params.arc = arc;
	params.mode = mode;
	params.gap_count = gap_count;
	params.gap_width = gap_width;
	params.face_outward = face_outward;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.outline.layer_layout = layer_layout;
	return pattern_slots_to_array(generate_ellipse2d(transforms_amount, marker_transform, params));
}

PatternSlots2D BulletPatterns2D::generate_star2d(int transforms_amount, Transform2D marker_transform, const StarParams2D &params) {
	int points = params.points;
	real_t outer_radius = params.outer_radius;
	real_t inner_radius = params.inner_radius;
	real_t base_rotation = params.base_rotation;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_distribution = params.corner.outline_distribution;
	int layer_layout = params.outline.layer_layout;
	int outline_corner_priority = params.corner.outline_corner_priority;
	int outline_corner_mode = params.corner.outline_corner_mode;
	double outline_edge_margin = params.corner.outline_edge_margin;
	int outline_corner_facing = params.corner.outline_corner_facing;

	if (!danmaku_validate_head("helper_generate_transforms_star", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (points < 2) {
		UtilityFunctions::push_error("helper_generate_transforms_star: points must be >= 2.");
		return PatternSlots2D();
	}
	// points*2 corners get built below: cap points so a hostile value can't
	// turn the corner loop into a multi-GB hang (and INT_MAX/2 can't wrap).
	if (points > HELPER_MAX_TRANSFORMS) {
		UtilityFunctions::push_error("helper_generate_transforms_star: points is absurdly large; keep it near the bullet count.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(outer_radius) || outer_radius < 0.0 || !Math::is_finite(inner_radius) || inner_radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_star: outer_radius and inner_radius must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_star: base_rotation and facing_offset_degrees must be finite.");
		return PatternSlots2D();
	}
	if (outline_distribution < 0 || outline_distribution > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_star: outline_distribution must be 0 (legacy) or 1 (symmetric).");
		return PatternSlots2D();
	}
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_star: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (outline_corner_priority < 0 || outline_corner_priority > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_star: outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
		return PatternSlots2D();
	}
	if (outline_corner_mode < 0 || outline_corner_mode > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_star: outline_corner_mode must be 0 (pin corners) or 1 (even arc).");
		return PatternSlots2D();
	}
	if (!Math::is_finite(outline_edge_margin) || outline_edge_margin < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_star: outline_edge_margin must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (outline_corner_facing < 0 || outline_corner_facing > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_star: outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
		return PatternSlots2D();
	}
	const real_t marker_rot = marker_transform.get_rotation();
	// Star outline corners in winding order (alternating outer/inner), built
	// marker-LOCAL like every other polygon primitive, so marker rotation
	// spins star volleys exactly like rectangle/polygon ones (identity
	// markers reproduce the legacy global loop byte-identically). Slots walk
	// the outline edges (not just the vertices), so every tip/valley carries
	// a bullet and the rest spread evenly along the edges instead of
	// stacking on vertices when the count exceeds the corner count.
	const int corner_count = points * 2;
	PackedVector2Array corners;
	for (int c = 0; c < corner_count; ++c) {
		const bool is_outer = (c % 2) == 0;
		const real_t angle = base_rotation + Math::TAU * (real_t)c / (real_t)corner_count;
		const real_t r = is_outer ? outer_radius : inner_radius;
		const Vector2 p = Vector2(Math::cos(angle), Math::sin(angle)) * r;
		if (p.is_finite()) {
			corners.push_back(p);
		}
	}
	if (corners.size() < 3) {
		UtilityFunctions::push_error("helper_generate_transforms_star: degenerate star.");
		return PatternSlots2D();
	}
	PackedVector2Array corner_normals;
	if (!compute_edge_normals_quiet(corners, true, false, corner_normals) || corner_normals.size() != corners.size()) {
		UtilityFunctions::push_error("helper_generate_transforms_star: degenerate star.");
		return PatternSlots2D();
	}
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	// Zero bullets is a valid empty request (every other generator returns
	// [] silently); only a real geometry failure below is "degenerate".
	if (transforms_amount == 0) {
		return PatternSlots2D();
	}
	if (!build_symmetric_polygon_loop(corners, corner_normals, transforms_amount, outline_distribution, loop_points, loop_normals, outline_corner_priority, outline_corner_mode, outline_edge_margin, outline_corner_facing)) {
		UtilityFunctions::push_error("helper_generate_transforms_star: degenerate star.");
		return PatternSlots2D();
	}
	return layout_outline_slots("helper_generate_transforms_star", marker_transform, loop_points, loop_normals, true, marker_rot, face_outward, facing_offset_degrees, PackedFloat32Array(),  params.outline, params.corner, corners, true, true);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_star(
		int transforms_amount,
		Transform2D marker_transform,
		int points,
		real_t outer_radius,
		real_t inner_radius,
		real_t base_rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int outline_distribution,
		int layer_layout,
		int outline_corner_priority,
		int outline_corner_mode,
		double outline_edge_margin,
		int outline_corner_facing) {
	StarParams2D params;
	params.points = points;
	params.outer_radius = outer_radius;
	params.inner_radius = inner_radius;
	params.base_rotation = base_rotation;
	params.face_outward = face_outward;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.corner.outline_distribution = outline_distribution;
	params.outline.layer_layout = layer_layout;
	params.corner.outline_corner_priority = outline_corner_priority;
	params.corner.outline_corner_mode = outline_corner_mode;
	params.corner.outline_edge_margin = outline_edge_margin;
	params.corner.outline_corner_facing = outline_corner_facing;
	return pattern_slots_to_array(generate_star2d(transforms_amount, marker_transform, params));
}

PatternSlots2D BulletPatterns2D::generate_heart2d(int transforms_amount, Transform2D marker_transform, const HeartParams2D &params) {
	real_t size = params.size;
	real_t base_rotation = params.base_rotation;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_placement = params.outline.outline_placement;
	int outline_facing = params.outline.outline_facing;
	int layer_layout = params.outline.layer_layout;

	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_heart: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (!danmaku_validate_head("helper_generate_transforms_heart", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(size) || size <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_heart: size must be finite and > 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_heart: base_rotation and facing_offset_degrees must be finite.");
		return PatternSlots2D();
	}
	// Slot loop plus center-radial outward normals; layer offsets run
	// radially so every ring keeps the heart figure. Facings ride a per-slot
	// override holding the legacy radial facings byte-exact (the shared
	// worker only adds the outline_facing selector on top), so on-outline
	// output is unchanged by the layout routing. The base loop is arc-even
	// from a dense ideal sweep (param sweeps bunch where the curve runs
	// slow/fast, e.g. near the cusp), so dots sit exactly on the curve with
	// even gaps at any count.
	PackedVector2Array dense_pts;
	PackedVector2Array dense_nrms;
	PackedFloat32Array dense_ovr;
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	const Vector2 fallback_dir = Vector2(Math::cos(base_rotation), Math::sin(base_rotation));
	const real_t scale = size / 32.0;
	for (int k = 0; k < 720; ++k) {
		const real_t t = Math::TAU * (real_t)k / 720.0;
		Vector2 local = heart_point2d(t, scale).rotated(base_rotation);
		if (!local.is_finite()) {
			local = Vector2(0, 0);
		}
		dense_pts.push_back(local);
		dense_nrms.push_back((local.length_squared() > 1e-12) ? local.normalized() : fallback_dir);
		const real_t radial = (local.length_squared() > 0.0) ? local.angle() : base_rotation;
		dense_ovr.push_back((face_outward ? radial : radial + Math::PI) + facing_offset);
	}
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	PackedFloat32Array facing_override;
	PackedVector2Array even_local;
	PackedVector2Array even_nrms;
	PackedFloat32Array even_ovr;
	resample_loop_even(dense_pts, dense_nrms, dense_ovr, transforms_amount, true, even_local, even_nrms, even_ovr);
	const PackedVector2Array fill_outline = fill_outline_from(outline_placement, dense_pts, origin);
	for (int i = 0; i < even_local.size(); ++i) {
		loop_points.push_back(origin + even_local[i]);
		loop_normals.push_back(even_nrms[i]);
		facing_override.push_back(even_ovr[i]);
	}
	return layout_outline_slots("helper_generate_transforms_heart", marker_transform, loop_points, loop_normals, false, 0.0, face_outward, facing_offset_degrees, facing_override,  params.outline, CornerLayout2D::smooth(), PackedVector2Array(), true, true, fill_outline, dense_nrms, dense_ovr);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_heart(
		int transforms_amount,
		Transform2D marker_transform,
		real_t size,
		real_t base_rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int layer_layout) {
	HeartParams2D params;
	params.size = size;
	params.base_rotation = base_rotation;
	params.face_outward = face_outward;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.outline.layer_layout = layer_layout;
	return pattern_slots_to_array(generate_heart2d(transforms_amount, marker_transform, params));
}

PatternSlots2D BulletPatterns2D::generate_rose2d(int transforms_amount, Transform2D marker_transform, const RoseParams2D &params) {
	int petals = params.petals;
	real_t radius = params.radius;
	real_t lobe_sharpness = params.lobe_sharpness;
	real_t base_rotation = params.base_rotation;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_placement = params.outline.outline_placement;
	int layer_layout = params.outline.layer_layout;

	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_rose: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (!danmaku_validate_head("helper_generate_transforms_rose", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (petals < 2) {
		UtilityFunctions::push_error("helper_generate_transforms_rose: petals must be >= 2.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(radius) || radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_rose: radius must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(lobe_sharpness) || lobe_sharpness < 0.0 || !Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_rose: lobe_sharpness, base_rotation and facing_offset_degrees must be finite (sharpness >= 0).");
		return PatternSlots2D();
	}
	// Theta sweep plus petal-axis outward normals (coherent across the flip);
	// the shared outline worker assembles facings. Arc-even from a dense
	// ideal sweep so dots sit exactly on the petals with even gaps.
	PackedVector2Array dense_pts;
	PackedVector2Array dense_nrms;
	const Vector2 origin = marker_transform.get_origin();
	// r = cos(k*theta) closes after half a turn for odd k (k petals) and a
	// full turn for even k (2k petals): sweeping a full turn for odd k traced
	// every petal twice, stacking bullet i+n/2 on bullet i.
	const real_t sweep = rose_sweep2d(petals);
	for (int k = 0; k < 720; ++k) {
		const real_t theta = sweep * (real_t)k / 720.0 + base_rotation;
		real_t cos_k = 0.0;
		const real_t r = rose_radius2d(petals, theta, radius, lobe_sharpness, cos_k);
		dense_pts.push_back(Vector2(Math::cos(theta), Math::sin(theta)) * r);
		const real_t shape_angle = (cos_k >= 0.0) ? theta : theta + Math::PI;
		dense_nrms.push_back(Vector2(Math::cos(shape_angle), Math::sin(shape_angle)));
	}
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	PackedVector2Array even_local;
	PackedVector2Array even_nrms;
	PackedFloat32Array even_ovr;
	resample_loop_even_distinct(dense_pts, dense_nrms, PackedFloat32Array(), transforms_amount, true, even_local, even_nrms, even_ovr);
	const PackedVector2Array fill_outline = fill_outline_from(outline_placement, dense_pts, origin);
	for (int i = 0; i < even_local.size(); ++i) {
		loop_points.push_back(origin + even_local[i]);
		loop_normals.push_back(even_nrms[i]);
	}
	return layout_outline_slots("helper_generate_transforms_rose", marker_transform, loop_points, loop_normals, false, 0.0, face_outward, facing_offset_degrees, PackedFloat32Array(),  params.outline, CornerLayout2D::smooth(), PackedVector2Array(), true, true, fill_outline, dense_nrms);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_rose(
		int transforms_amount,
		Transform2D marker_transform,
		int petals,
		real_t radius,
		real_t lobe_sharpness,
		real_t base_rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int layer_layout) {
	RoseParams2D params;
	params.petals = petals;
	params.radius = radius;
	params.lobe_sharpness = lobe_sharpness;
	params.base_rotation = base_rotation;
	params.face_outward = face_outward;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.outline.layer_layout = layer_layout;
	return pattern_slots_to_array(generate_rose2d(transforms_amount, marker_transform, params));
}

PatternSlots2D BulletPatterns2D::generate_lissajous2d(int transforms_amount, Transform2D marker_transform, const LissajousParams2D &params) {
	real_t size_x = params.size_x;
	real_t size_y = params.size_y;
	real_t freq_x = params.freq_x;
	real_t freq_y = params.freq_y;
	real_t phase = params.phase;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_placement = params.outline.outline_placement;
	int layer_layout = params.outline.layer_layout;

	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_lissajous: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (!danmaku_validate_head("helper_generate_transforms_lissajous", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(size_x) || size_x < 0.0 || !Math::is_finite(size_y) || size_y < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_lissajous: size_x and size_y must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(freq_x) || freq_x < 0.0 || !Math::is_finite(freq_y) || freq_y < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_lissajous: freq_x and freq_y must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(phase) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_lissajous: phase and facing_offset_degrees must be finite.");
		return PatternSlots2D();
	}
	// Weave sweep plus center-radial outward normals (marker rotation when a
	// sample lands exactly on the center); the shared outline worker
	// assembles facings. Arc-even from a dense ideal sweep so dots sit
	// exactly on the weave with even gaps.
	PackedVector2Array dense_pts;
	PackedVector2Array dense_nrms;
	const Vector2 origin = marker_transform.get_origin();
	const real_t marker_rot = marker_transform.get_rotation();
	// Integer ratios with a shared factor g trace the curve g times per full
	// turn (3:3 = 1:1 drawn three times): sweep one closure only. Some
	// curves are also traced back and forth (1:1 at phase 0 is a line):
	// those resample as an OPEN run between the two turning points.
	double t0 = 0.0;
	double t_span = Math::TAU;
	bool open_run = false;
	lissajous_sweep2d(freq_x, freq_y, phase, t0, t_span, open_run);
	for (int k = 0; k < 720; ++k) {
		const real_t t = (real_t)(t0 + t_span * (double)k / (open_run ? 719.0 : 720.0));
		const Vector2 offset = lissajous_point2d(size_x, size_y, freq_x, freq_y, phase, t);
		dense_pts.push_back(offset);
		dense_nrms.push_back((offset.length_squared() > 0.0) ? offset.normalized() : Vector2(Math::cos(marker_rot), Math::sin(marker_rot)));
	}
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	PackedVector2Array even_local;
	PackedVector2Array even_nrms;
	PackedFloat32Array even_ovr;
	resample_loop_even_distinct(dense_pts, dense_nrms, PackedFloat32Array(), transforms_amount, !open_run, even_local, even_nrms, even_ovr);
	const PackedVector2Array fill_outline = fill_outline_from(outline_placement, dense_pts, origin);
	for (int i = 0; i < even_local.size(); ++i) {
		loop_points.push_back(origin + even_local[i]);
		loop_normals.push_back(even_nrms[i]);
	}
	return layout_outline_slots("helper_generate_transforms_lissajous", marker_transform, loop_points, loop_normals, false, 0.0, face_outward, facing_offset_degrees, PackedFloat32Array(),  params.outline, CornerLayout2D::smooth(), PackedVector2Array(), !open_run, true, fill_outline, dense_nrms);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_lissajous(
		int transforms_amount,
		Transform2D marker_transform,
		real_t size_x,
		real_t size_y,
		real_t freq_x,
		real_t freq_y,
		real_t phase,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int layer_layout) {
	LissajousParams2D params;
	params.size_x = size_x;
	params.size_y = size_y;
	params.freq_x = freq_x;
	params.freq_y = freq_y;
	params.phase = phase;
	params.face_outward = face_outward;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.outline.layer_layout = layer_layout;
	return pattern_slots_to_array(generate_lissajous2d(transforms_amount, marker_transform, params));
}

PatternSlots2D BulletPatterns2D::generate_circle2d(int transforms_amount, Transform2D marker_transform, const CircleParams2D &params) {
	real_t radius = params.radius;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_placement = params.outline.outline_placement;
	int layer_layout = params.outline.layer_layout;

	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_circle: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (!danmaku_validate_head("helper_generate_transforms_circle", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(radius) || radius < 0.0 || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_circle: radius must be finite and >= 0, facing_offset_degrees finite.");
		return PatternSlots2D();
	}
	// Marker-local loop plus radial outward normals. rot_add carries the
	// historical double marker rotation (angle folds it in AND the facing
	// adds it again); the shared outline worker preserves it exactly.
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	loop_points.resize(transforms_amount);
	loop_normals.resize(transforms_amount);
	const real_t marker_rot = marker_transform.get_rotation();
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t angle = marker_rot + Math::TAU * (real_t)i / (real_t)transforms_amount;
		loop_points[i] = Vector2(Math::cos(angle), Math::sin(angle)) * radius;
		loop_normals[i] = Vector2(Math::cos(angle), Math::sin(angle));
	}
	PackedVector2Array fill_outline;
	if (outline_placement == OUTLINE_FILL_INSIDE) {
		for (int q = 0; q < 256; ++q) {
			const real_t angle = marker_rot + Math::TAU * (real_t)q / 256.0;
			fill_outline.push_back(Vector2(Math::cos(angle), Math::sin(angle)) * radius);
		}
	}
	return layout_outline_slots("helper_generate_transforms_circle", marker_transform, loop_points, loop_normals, true, marker_rot, face_outward, facing_offset_degrees, PackedFloat32Array(),  params.outline, CornerLayout2D::smooth(), PackedVector2Array(), true, true, fill_outline);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_circle(
		int transforms_amount,
		Transform2D marker_transform,
		real_t radius,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int layer_layout) {
	CircleParams2D params;
	params.radius = radius;
	params.face_outward = face_outward;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.outline.layer_layout = layer_layout;
	return pattern_slots_to_array(generate_circle2d(transforms_amount, marker_transform, params));
}

} // namespace BlastBullets2D
