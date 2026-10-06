// Preview tracks (helper_sample_outline_*): the curve each pattern's bullets sit
// on, drawn by BulletSpawner2D's preview layer.

#include "patterns/pattern_curves2d.hpp"
#include "patterns/patterns_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Closed-loop track samplers for the editor preview (marker-local points +
// closed flag; the spawner draws them under the bullet dots). Densities are
// fixed and adaptive so tracks never depend on bullet counts. Invalid input
// yields an empty track (loud, like the generators).
static int outline_sweep_count(double perimeter) {
	if (!Math::is_finite(perimeter) || perimeter <= 0.0) {
		return 0;
	}
	return Math::clamp((int)(perimeter / 8.0), 32, 256);
}

static Dictionary outline_track_result(const PackedVector2Array &points, bool closed) {
	Dictionary result;
	result["points"] = points;
	result["closed"] = closed;
	return result;
}

// Shared per-type flower curve evaluator: marker-relative offset for
// parameter t in [0, TAU). Mirrors the generator branches exactly so the
// preview track and the volley agree. Returns false when the point is
// unusable (caller falls back to origin). petal_override/frac_override pin
// a FAN evaluation to an explicit (petal, frac) slot (-1/2.0 = derive from
// t): the arc endpoint frac = +0.5 maps to the next petal's t, so exact
// endpoints are only reachable through the override.
static bool flower_curve_point(int flower_type, int petals, real_t radius, real_t petal_spread, real_t petal_sharpness, double inner_radius_scale, double spiro_roller, double spiro_pen, double super_lobes, double super_fullness, real_t base_rotation, double t, Vector2 &r_offset, int petal_override = -1, double frac_override = 2.0) {
	const double clamped_inner = Math::clamp(inner_radius_scale, 0.0, 0.999);
	if (flower_type == BulletPatterns2D::FLOWER_FAN) {
		// Trace the fan's petal arcs so the preview matches the generator's
		// per-petal layout: each of `petals` lobes is centered on its lobe
		// axis and fanned across petal_spread, with the radius pinched
		// between lobes by the waist term. Sampling the arcs back-to-back
		// over one revolution reproduces the flower outline.
		const double pf = ((double)t / Math::TAU) * (double)petals;
		int petal_idx = Math::clamp((int)Math::floor(pf), 0, petals - 1);
		double frac = pf - (double)petal_idx - 0.5; // -0.5..0.5 within petal
		if (petal_override >= 0 && petal_override < petals && frac_override >= -0.5 && frac_override <= 0.5) {
			petal_idx = petal_override;
			frac = frac_override;
		}
		const real_t lobe_center = base_rotation + Math::TAU * (real_t)petal_idx / (real_t)petals;
		const real_t angle = lobe_center + (real_t)(frac * petal_spread);
		const real_t waist = flower_fan_waist2d(petal_sharpness, (real_t)frac);
		r_offset = Vector2(Math::cos(angle), Math::sin(angle)) * (radius * waist);
		return r_offset.is_finite();
	}
	if (flower_type == BulletPatterns2D::FLOWER_RHODONEA) {
		const real_t sharp = (petal_sharpness < 0.0) ? (real_t)0.0 : petal_sharpness;
		const real_t theta = base_rotation + (real_t)t;
		const real_t r = flower_rhodonea_radius2d(petals, theta, radius, sharp, clamped_inner);
		r_offset = Vector2(Math::cos(theta), Math::sin(theta)) * r;
		return r_offset.is_finite();
	}
	if (flower_type == BulletPatterns2D::FLOWER_PHYLLOTAXIS) {
		// Disc has no outline curve; trace the outer rim at full radius.
		const real_t ang = base_rotation + (real_t)t;
		r_offset = Vector2(Math::cos(ang), Math::sin(ang)) * radius;
		return r_offset.is_finite();
	}
	if (flower_type == BulletPatterns2D::FLOWER_SPIROGRAPH) {
		const Spirograph2D spiro = spirograph_setup2d(radius, spiro_roller, spiro_pen);
		if (!Math::is_finite(spiro.diff) || !Math::is_finite(spiro.k)) {
			return false;
		}
		Vector2 local = spirograph_point2d(spiro, t).rotated(base_rotation);
		if (!local.is_finite()) {
			return false;
		}
		r_offset = local;
		return true;
	}
	// FLOWER_SUPERFORMULA: the generator's own normalized radius.
	const double r_norm = superformula_norm2d(super_lobes, super_fullness, t);
	const real_t r = radius * (real_t)(clamped_inner + (1.0 - clamped_inner) * (r_norm * 0.5));
	const real_t ang = base_rotation + (real_t)t;
	r_offset = Vector2(Math::cos(ang), Math::sin(ang)) * r;
	return r_offset.is_finite();
}

// Row-structure track samplers for the grid-family generators: row strips
// with INF separators (the spawner preview breaks strips on non-finite
// points, so one array carries every row). Ideal structure only: jitter,
// random rotation and overflow piling are volley noise, not track shape.
// Points are origin-relative offsets ("local" false: added to the marker
// origin, never xformed, exactly like the generators build them).
static void outline_emit_inf(PackedVector2Array &r_pts) {
	r_pts.push_back(Vector2(Math::INF, Math::INF));
}

static void outline_emit_spiral_arms(PackedVector2Array &r_pts, int transforms_amount, int arms, real_t start_radius, real_t radius_step, real_t angle_step, real_t base_rotation_abs, int arm_index_stride, bool mirror_alternate_arms) {
	for (int arm = 0; arm < arms; ++arm) {
		for (int i = 0; i < transforms_amount; ++i) {
			int ia = 0;
			int step_index = 0;
			spiral_arm_step(i, arms, arm_index_stride, ia, step_index);
			if (ia != arm) {
				continue;
			}
			const real_t dir_sign = (mirror_alternate_arms && (arm % 2 == 1)) ? -1.0 : 1.0;
			const real_t arm_phase = Math::TAU * (real_t)arm / (real_t)arms;
			const real_t r = start_radius + radius_step * (real_t)step_index;
			const real_t angle = base_rotation_abs + arm_phase + dir_sign * angle_step * (real_t)step_index;
			const Vector2 p = Vector2(Math::cos(angle), Math::sin(angle)) * r;
			if (p.is_finite()) {
				r_pts.push_back(p);
			}
		}
		outline_emit_inf(r_pts);
	}
}

Dictionary BulletPatterns2D::helper_sample_outline_flower(int flower_type, int petals, real_t radius, real_t petal_spread, real_t petal_sharpness, double inner_radius_scale, double spiro_roller, double spiro_pen, double super_lobes, double super_fullness, real_t base_rotation, int transforms_amount) {
	if (flower_type < FLOWER_FAN || flower_type > FLOWER_SUPERFORMULA) {
		UtilityFunctions::push_error("helper_sample_outline_flower: unknown flower_type.");
		return outline_track_result(PackedVector2Array(), false);
	}
	if (petals < 1 || !Math::is_finite(radius) || radius <= 0.0 || !Math::is_finite(petal_spread) || petal_spread < 0.0 || !Math::is_finite(petal_sharpness) || petal_sharpness < 0.0 || !Math::is_finite(base_rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_flower: petals >= 1, finite radius > 0, spreads/sharpness finite and >= 0, base_rotation finite.");
		return outline_track_result(PackedVector2Array(), false);
	}
	if (!Math::is_finite(inner_radius_scale) || inner_radius_scale < 0.0 || inner_radius_scale >= 1.0 || !Math::is_finite(spiro_roller) || spiro_roller <= 0.0 || !Math::is_finite(spiro_pen) || spiro_pen < 0.0 || !Math::is_finite(super_lobes) || super_lobes < 2.0 || super_lobes > 64.0 || !Math::is_finite(super_fullness) || super_fullness <= 0.0 || super_fullness > 8.0) {
		UtilityFunctions::push_error("helper_sample_outline_flower: bad bloom knob range.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// Spirographs close only after several revolutions (k=(R-r)/r fractional),
	// so sweep the full closure and scale density to keep the extra winding
	// from coming out undersampled. Other kinds close in one revolution.
	int revolutions = 1;
	if (flower_type == FLOWER_SPIROGRAPH) {
		// Same closure rule as the generator: a pinned centre or a free pen
		// draws one plain circle.
		const Spirograph2D spiro = spirograph_setup2d(radius, spiro_roller, spiro_pen);
		revolutions = (spiro.centre_pinned || spiro_pen <= 1e-9) ? 1 : spirograph_revolutions(spiro.k);
	}
	// Sharp rhodonea / superformula lobe tips need dense chords (the error
	// shrinks with the square of the count): 512 keeps every bullet within
	// half a pixel of the drawn curve. Preview-only, built on rebuild.
	int n = 160;
	if (flower_type == FLOWER_SPIROGRAPH) {
		n = spirograph_dense_samples(revolutions);
	} else if (flower_type == FLOWER_RHODONEA || flower_type == FLOWER_SUPERFORMULA) {
		n = 512;
	}
	PackedVector2Array pts;
	// FAN traces back-to-back petal arcs that jump discontinuously at petal
	// boundaries: sample each petal separately (exact arc endpoints, INF
	// separators between) so tracks and rings draw per-petal arcs instead
	// of bridging spokes, and volley slots at the arc ends sit on ring
	// vertices instead of past the last sample.
	if (flower_type == FLOWER_FAN && petals >= 1) {
		const int per = Math::max(n / petals, 4);
		for (int k = 0; k < petals; ++k) {
			// With a bullet amount, petals that hold no bullet are not drawn
			// (amount < petals): the track shows exactly what fires.
			if (transforms_amount >= 0 && flower_fan_petal_count(k, petals, transforms_amount) == 0) {
				continue;
			}
			for (int j = 0; j <= per; ++j) {
				const double frac = (double)j / (double)per - 0.5;
				const double t = Math::TAU * ((double)k + frac + 0.5) / (double)petals;
				Vector2 off;
				if (flower_curve_point(flower_type, petals, radius, petal_spread, petal_sharpness, inner_radius_scale, spiro_roller, spiro_pen, super_lobes, super_fullness, base_rotation, t, off, k, frac)) {
					pts.push_back(off);
				}
			}
			pts.push_back(Vector2(Math::INF, Math::INF));
		}
		if (pts.size() < 3) {
			return outline_track_result(PackedVector2Array(), false);
		}
		return outline_track_result(pts, true);
	}
	for (int i = 0; i < n; ++i) {
		const double t = Math::TAU * (double)revolutions * (double)i / (double)n;
		Vector2 off;
		if (flower_curve_point(flower_type, petals, radius, petal_spread, petal_sharpness, inner_radius_scale, spiro_roller, spiro_pen, super_lobes, super_fullness, base_rotation, t, off)) {
			pts.push_back(off);
		}
	}
	if (pts.size() < 3) {
		return outline_track_result(PackedVector2Array(), false);
	}
	return outline_track_result(pts, true);
}

Dictionary BulletPatterns2D::helper_sample_outline_rose(int petals, real_t radius, real_t lobe_sharpness, real_t base_rotation) {
	if (petals < 2 || !Math::is_finite(radius) || radius <= 0.0 || !Math::is_finite(lobe_sharpness) || lobe_sharpness < 0.0 || !Math::is_finite(base_rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_rose: petals >= 2, finite radius > 0, sharpness >= 0.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// The generator's theta sweep and signed radius (flips included: the
	// strip chords match the slot jumps between petals); odd petal counts
	// close after half a turn.
	const int n = 128;
	const real_t sweep = rose_sweep2d(petals);
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t theta = sweep * (real_t)i / (real_t)n + base_rotation;
		real_t cos_k = 0.0;
		const real_t r = rose_radius2d(petals, theta, radius, lobe_sharpness, cos_k);
		pts.push_back(Vector2(Math::cos(theta), Math::sin(theta)) * r);
	}
	return outline_track_result(pts, true);
}

Dictionary BulletPatterns2D::helper_sample_outline_lissajous(real_t size_x, real_t size_y, real_t freq_x, real_t freq_y, real_t phase) {
	if (!Math::is_finite(size_x) || size_x < 0.0 || !Math::is_finite(size_y) || size_y < 0.0 || !Math::is_finite(freq_x) || freq_x < 0.0 || !Math::is_finite(freq_y) || freq_y < 0.0 || !Math::is_finite(phase)) {
		UtilityFunctions::push_error("helper_sample_outline_lissajous: sizes/freqs finite and >= 0, phase finite.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// The generator's sweep window: one closure, or an open back-and-forth
	// run between the two turning points (drawn open, like the bullets).
	double t0 = 0.0;
	double t_span = Math::TAU;
	bool open_run = false;
	lissajous_sweep2d(freq_x, freq_y, phase, t0, t_span, open_run);
	const int n = 128;
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t tt = (real_t)(t0 + t_span * (double)i / (open_run ? (double)(n - 1) : (double)n));
		pts.push_back(lissajous_point2d(size_x, size_y, freq_x, freq_y, phase, tt));
	}
	return outline_track_result(pts, !open_run);
}

Dictionary BulletPatterns2D::helper_sample_outline_heart(real_t size, real_t base_rotation) {
	if (!Math::is_finite(size) || size <= 0.0 || !Math::is_finite(base_rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_heart: size must be finite and > 0, base_rotation finite.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	// The generator's heart curve and scale.
	const real_t scale = size / 32.0;
	const int n = 128;
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t tt = Math::TAU * (real_t)i / (real_t)n;
		pts.push_back(heart_point2d(tt, scale).rotated(base_rotation));
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = true;
	result["local"] = false;
	return result;
}

Dictionary BulletPatterns2D::helper_sample_outline_spiral(int transforms_amount, real_t start_radius, real_t radius_step, real_t angle_step, real_t base_rotation_abs) {
	if (transforms_amount < 0 || !Math::is_finite(start_radius) || !Math::is_finite(radius_step) || !Math::is_finite(angle_step) || !Math::is_finite(base_rotation_abs)) {
		UtilityFunctions::push_error("helper_sample_outline_spiral: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	PackedVector2Array pts;
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t r = start_radius + radius_step * (real_t)i;
		const real_t angle = base_rotation_abs + angle_step * (real_t)i;
		const Vector2 p = Vector2(Math::cos(angle), Math::sin(angle)) * r;
		if (p.is_finite()) {
			pts.push_back(p);
		}
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletPatterns2D::helper_sample_outline_multispiral(int transforms_amount, int arms, real_t start_radius, real_t radius_step, real_t angle_step, real_t base_rotation_abs, int arm_index_stride) {
	if (transforms_amount < 0 || arms < 1 || !Math::is_finite(start_radius) || !Math::is_finite(radius_step) || !Math::is_finite(angle_step) || !Math::is_finite(base_rotation_abs) || arm_index_stride < 1) {
		UtilityFunctions::push_error("helper_sample_outline_multispiral: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	PackedVector2Array pts;
	outline_emit_spiral_arms(pts, transforms_amount, arms, start_radius, radius_step, angle_step, base_rotation_abs, arm_index_stride, false);
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletPatterns2D::helper_sample_outline_counter_spiral(int transforms_amount, int arms, real_t start_radius, real_t radius_step, real_t angle_step, real_t base_rotation_abs, int arm_index_stride, bool mirror_alternate_arms) {
	if (transforms_amount < 0 || arms < 2 || !Math::is_finite(start_radius) || !Math::is_finite(radius_step) || !Math::is_finite(angle_step) || !Math::is_finite(base_rotation_abs) || arm_index_stride < 1) {
		UtilityFunctions::push_error("helper_sample_outline_counter_spiral: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	PackedVector2Array pts;
	outline_emit_spiral_arms(pts, transforms_amount, arms, start_radius, radius_step, angle_step, base_rotation_abs, arm_index_stride, mirror_alternate_arms);
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletPatterns2D::helper_sample_outline_grid(int transforms_amount, int rows_per_column, int alignment, real_t column_offset, real_t row_offset, real_t base_rotation_abs, bool rotate_with_marker) {
	if (transforms_amount < 0 || rows_per_column < 1 || alignment < 0 || alignment > 8 || !Math::is_finite(column_offset) || !Math::is_finite(row_offset) || !Math::is_finite(base_rotation_abs)) {
		UtilityFunctions::push_error("helper_sample_outline_grid: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	// Mirrors helper_generate_transforms_grid column/row math (ideal grid:
	// no jitter, no random rotation). Row strips, INF-separated.
	const int columns_amount = (transforms_amount + rows_per_column - 1) / rows_per_column;
	const int used_rows = (columns_amount > 1) ? rows_per_column : transforms_amount;
	const int last_column_rows = transforms_amount - (columns_amount - 1) * rows_per_column;
	const real_t total_width = (real_t)(columns_amount - 1) * column_offset;
	real_t x_start = -total_width * 0.5;
	if (alignment == 0 || alignment == 3 || alignment == 6) {
		x_start = 0.0;
	} else if (alignment == 2 || alignment == 5 || alignment == 8) {
		x_start = -total_width;
	}
	const real_t rot_cos = Math::cos(base_rotation_abs);
	const real_t rot_sin = Math::sin(base_rotation_abs);
	auto place = [&](const Vector2 &cell) -> Vector2 {
		if (!rotate_with_marker) {
			return cell;
		}
		return Vector2(cell.x * rot_cos - cell.y * rot_sin, cell.x * rot_sin + cell.y * rot_cos);
	};
	PackedVector2Array pts;
	for (int row = 0; row < used_rows; ++row) {
		for (int column = 0; column < columns_amount; ++column) {
			const int rows_this_column = (column == columns_amount - 1) ? last_column_rows : rows_per_column;
			if (row >= rows_this_column) {
				continue;
			}
			const real_t col_height = (real_t)(rows_this_column - 1) * row_offset;
			real_t y_start = -col_height * 0.5;
			const int row_group = alignment / 3;
			if (row_group == 0) {
				y_start = 0.0;
			} else if (row_group == 2) {
				y_start = -col_height;
			}
			const Vector2 cell(x_start + (real_t)column * column_offset, y_start + (real_t)row * row_offset);
			if (cell.is_finite()) {
				pts.push_back(place(cell));
			}
		}
		outline_emit_inf(pts);
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = !rotate_with_marker;
	return result;
}

Dictionary BulletPatterns2D::helper_sample_outline_lattice(int transforms_amount, int columns, int rows, real_t spacing_x, real_t spacing_y, bool stagger_rows) {
	if (transforms_amount < 0 || columns < 1 || rows < 1 || !Math::is_finite(spacing_x) || !Math::is_finite(spacing_y)) {
		UtilityFunctions::push_error("helper_sample_outline_lattice: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	// Mirrors helper_generate_transforms_lattice row math, including the
	// overflow rows past capacity.
	const int capacity = columns * rows;
	const int total_rows = rows + (transforms_amount > capacity ? (transforms_amount - capacity + columns - 1) / columns : 0);
	PackedVector2Array pts;
	for (int row = 0; row < total_rows; ++row) {
		const real_t stagger = (stagger_rows && (row % 2 == 1)) ? spacing_x * 0.5 : 0.0;
		for (int col = 0; col < columns; ++col) {
			const Vector2 cell((col - (columns - 1) * 0.5) * spacing_x + stagger, (row - (rows - 1) * 0.5) * spacing_y);
			if (cell.is_finite()) {
				pts.push_back(cell);
			}
		}
		outline_emit_inf(pts);
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletPatterns2D::helper_sample_outline_waterfall(int transforms_amount, int columns, real_t column_spacing, int rows, real_t row_spacing, real_t stagger, const Vector2 &rain_direction) {
	if (transforms_amount < 0 || columns < 1 || rows < 1 || !Math::is_finite(column_spacing) || !Math::is_finite(row_spacing) || !Math::is_finite(stagger) || !rain_direction.is_finite() || rain_direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_sample_outline_waterfall: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	// Mirrors helper_generate_transforms_waterfall row math, including the
	// overflow rows past capacity.
	const Vector2 axis = rain_direction.normalized();
	const Vector2 across = axis.orthogonal();
	const int capacity = columns * rows;
	const int total_rows = rows + (transforms_amount > capacity ? (transforms_amount - capacity + columns - 1) / columns : 0);
	PackedVector2Array pts;
	for (int row = 0; row < total_rows; ++row) {
		const double row_phase = (rows > 1) ? ((double)row / (double)(rows - 1) - 0.5) : 0.0;
		for (int col = 0; col < columns; ++col) {
			const double col_centered = (columns > 1) ? ((double)col / (double)(columns - 1) - 0.5) : 0.0;
			const Vector2 cell = across * (real_t)(col_centered * column_spacing * (columns - 1) + stagger * column_spacing * row_phase) + axis * (real_t)(row * row_spacing);
			if (cell.is_finite()) {
				pts.push_back(cell);
			}
		}
		outline_emit_inf(pts);
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletPatterns2D::helper_sample_outline_rain(int transforms_amount, real_t band_width, const Vector2 &rain_direction, real_t drop_spacing) {
	if (transforms_amount < 0 || !Math::is_finite(band_width) || band_width < 0.0 || !rain_direction.is_finite() || rain_direction.length_squared() <= 0.0 || !Math::is_finite(drop_spacing)) {
		UtilityFunctions::push_error("helper_sample_outline_rain: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	// Mirrors helper_generate_transforms_rain row grouping.
	const Vector2 axis = rain_direction.normalized();
	const Vector2 across = axis.orthogonal();
	// Same grid as the generator (rain_columns2d / rain_along2d): one strip
	// per row through that row's drops.
	const int cols_per_row = rain_columns2d(transforms_amount, band_width, drop_spacing);
	const int total_rows = (transforms_amount + cols_per_row - 1) / cols_per_row;
	PackedVector2Array pts;
	for (int row = 0; row < total_rows; ++row) {
		for (int k = 0; k < cols_per_row; ++k) {
			const int i = row * cols_per_row + k;
			if (i >= transforms_amount) {
				break;
			}
			const real_t along = rain_along2d(i, transforms_amount, cols_per_row, band_width);
			const Vector2 cell = across * along - axis * (real_t)((double)row * drop_spacing);
			if (cell.is_finite()) {
				pts.push_back(cell);
			}
		}
		outline_emit_inf(pts);
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletPatterns2D::helper_sample_outline_wave(real_t width, real_t amplitude, real_t waves, const Vector2 &direction) {
	if (!Math::is_finite(width) || width < 0.0 || !Math::is_finite(amplitude) || amplitude < 0.0 || !Math::is_finite(waves) || !direction.is_finite() || direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_sample_outline_wave: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	// Mirrors helper_generate_transforms_wave center line at fixed density.
	const Vector2 axis = direction.normalized();
	const Vector2 across = axis.orthogonal();
	const int n = 128;
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t frac = (real_t)i / (real_t)(n - 1) - 0.5;
		pts.push_back(axis * (frac * width) + across * (amplitude * Math::sin(frac * waves * Math::TAU)));
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletPatterns2D::helper_sample_outline_circle(real_t radius) {
	if (!Math::is_finite(radius) || radius <= 0.0) {
		UtilityFunctions::push_error("helper_sample_outline_circle: radius must be finite and > 0.");
		return outline_track_result(PackedVector2Array(), false);
	}
	const int n = outline_sweep_count(Math::TAU * (double)radius);
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t a = Math::TAU * (real_t)i / (real_t)n;
		pts.push_back(Vector2(Math::cos(a), Math::sin(a)) * radius);
	}
	return outline_track_result(pts, true);
}

Dictionary BulletPatterns2D::helper_sample_outline_rectangle(const Vector2 &size) {
	if (!size.is_finite() || size.x <= 0.0 || size.y <= 0.0) {
		UtilityFunctions::push_error("helper_sample_outline_rectangle: size must be finite with sides > 0.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// Mirrors helper_generate_transforms_rectangle corner order.
	const Vector2 hw(size.x * 0.5, size.y * 0.5);
	PackedVector2Array pts;
	pts.push_back(Vector2(-hw.x, -hw.y));
	pts.push_back(Vector2(hw.x, -hw.y));
	pts.push_back(Vector2(hw.x, hw.y));
	pts.push_back(Vector2(-hw.x, hw.y));
	return outline_track_result(pts, true);
}

Dictionary BulletPatterns2D::helper_sample_outline_triangle(int triangle_type, real_t size_a, real_t size_b, real_t rotation) {
	if (triangle_type < TRIANGLE_EQUILATERAL || triangle_type > TRIANGLE_RIGHT || !Math::is_finite(size_a) || !Math::is_finite(size_b) || !Math::is_finite(rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_triangle: bad type or non-finite dims.");
		return outline_track_result(PackedVector2Array(), false);
	}
	return outline_track_result(build_triangle_corners(triangle_type, size_a, size_b, rotation), true);
}

Dictionary BulletPatterns2D::helper_sample_outline_trapezoid(real_t base_top, real_t base_bottom, real_t height, real_t rotation) {
	if (!Math::is_finite(base_top) || !Math::is_finite(base_bottom) || !Math::is_finite(height) || !Math::is_finite(rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_trapezoid: dims must be finite.");
		return outline_track_result(PackedVector2Array(), false);
	}
	return outline_track_result(build_trapezoid_corners(base_top, base_bottom, height, rotation), true);
}

Dictionary BulletPatterns2D::helper_sample_outline_diamond(real_t diagonal_x, real_t diagonal_y, real_t rotation) {
	if (!Math::is_finite(diagonal_x) || !Math::is_finite(diagonal_y) || !Math::is_finite(rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_diamond: dims must be finite.");
		return outline_track_result(PackedVector2Array(), false);
	}
	return outline_track_result(build_diamond_corners(diagonal_x, diagonal_y, rotation), true);
}

Dictionary BulletPatterns2D::helper_sample_outline_polygon(int vertices, real_t radius, real_t base_rotation) {
	if (vertices < 3 || !Math::is_finite(radius) || radius <= 0.0 || !Math::is_finite(base_rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_polygon: vertices >= 3, finite radius > 0.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// Mirrors helper_generate_transforms_polygon corner order.
	PackedVector2Array pts;
	for (int k = 0; k < vertices; ++k) {
		const real_t a = base_rotation + Math::TAU * (real_t)k / (real_t)vertices;
		pts.push_back(Vector2(Math::cos(a), Math::sin(a)) * radius);
	}
	return outline_track_result(pts, true);
}

Dictionary BulletPatterns2D::helper_sample_outline_ellipse(real_t radius_x, real_t radius_y, real_t ellipse_rotation, real_t start_angle, real_t arc, int mode) {
	if (!Math::is_finite(radius_x) || radius_x <= 0.0 || !Math::is_finite(radius_y) || radius_y <= 0.0 || !Math::is_finite(ellipse_rotation) || !Math::is_finite(start_angle) || !Math::is_finite(arc)) {
		UtilityFunctions::push_error("helper_sample_outline_ellipse: radii must be finite and > 0, angles finite.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// WALL draws its own arc span in full (gaps read from the missing
	// bullets, not the track); FULL closes the loop.
	const bool full = (mode == ELLIPSE_FULL);
	const real_t span = full ? Math::TAU : arc;
	if (!full && !Math::is_finite((double)span)) {
		return outline_track_result(PackedVector2Array(), false);
	}
	const double peri = Math::PI * (3.0 * ((double)radius_x + (double)radius_y) - Math::sqrt((3.0 * (double)radius_x + (double)radius_y) * ((double)radius_x + 3.0 * (double)radius_y)));
	const double arc_peri = peri * Math::abs((double)span) / Math::TAU;
	const int n = full ? outline_sweep_count(peri) : (outline_sweep_count(arc_peri) >= 2 ? outline_sweep_count(arc_peri) : 2);
	const real_t ec = Math::cos(ellipse_rotation);
	const real_t es = Math::sin(ellipse_rotation);
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t tt = full ? (Math::TAU * (real_t)i / (real_t)n) : (start_angle + span * (real_t)i / (real_t)(n - 1));
		const real_t ex = Math::cos(tt) * radius_x;
		const real_t ey = Math::sin(tt) * radius_y;
		pts.push_back(Vector2(ex * ec - ey * es, ex * es + ey * ec));
	}
	return outline_track_result(pts, full);
}

Dictionary BulletPatterns2D::helper_sample_outline_ring(real_t radius, real_t arc, real_t y_scale, real_t start_angle_abs) {
	if (!Math::is_finite(radius) || radius <= 0.0 || !Math::is_finite(arc) || !Math::is_finite(y_scale) || !Math::is_finite(start_angle_abs)) {
		UtilityFunctions::push_error("helper_sample_outline_ring: radius must be finite and > 0, arc/scale/angles finite.");
		return outline_track_result(PackedVector2Array(), false);
	}
	const bool closed = Math::abs(Math::abs(arc) - Math::TAU) < 0.0001;
	const double span = closed ? Math::TAU : (double)arc;
	if (!closed && !Math::is_finite(span)) {
		return outline_track_result(PackedVector2Array(), false);
	}
	const int arc_n = outline_sweep_count(Math::abs(span) * (double)radius);
	const int n = closed ? outline_sweep_count(Math::TAU * (double)radius) : (arc_n >= 2 ? arc_n : 2);
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t a = start_angle_abs + (real_t)((closed ? Math::TAU : span) * (double)i / (double)(closed ? n : n - 1));
		pts.push_back(Vector2(Math::cos(a) * radius, Math::sin(a) * radius * y_scale));
	}
	return outline_track_result(pts, closed);
}

Dictionary BulletPatterns2D::helper_sample_outline_star(int points, real_t outer_radius, real_t inner_radius, real_t base_rotation) {
	if (points < 2 || !Math::is_finite(outer_radius) || outer_radius < 0.0 || !Math::is_finite(inner_radius) || inner_radius < 0.0 || !Math::is_finite(base_rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_star: points >= 2, finite radii >= 0.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// Mirrors helper_generate_transforms_star corner order.
	const int corners = points * 2;
	PackedVector2Array pts;
	for (int c = 0; c < corners; ++c) {
		const real_t angle = base_rotation + Math::TAU * (real_t)c / (real_t)corners;
		pts.push_back(Vector2(Math::cos(angle), Math::sin(angle)) * ((c % 2 == 0) ? outer_radius : inner_radius));
	}
	return outline_track_result(pts, true);
}

} // namespace BlastBullets2D
