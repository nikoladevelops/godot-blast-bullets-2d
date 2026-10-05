// Danmaku pattern generators that place bullets directly (no closed outline):
// grid, fan, spiral, line, aimed, rain, scatter, star polygon, multi-spiral,
// cross, wave, waterfall, lattice, counter-spiral and corridor.

#include "patterns/patterns_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Arm and step of spiral slot i. Interleave (stride < arms) deals slots
// round robin; grouping (stride >= arms) gives each arm runs of
// g = stride / arms + 1 consecutive slots. Always one-to-one: no two slots
// share an (arm, step) pair, so no bullet hides under another.
void spiral_arm_step(int i, int arms, int stride, int &r_arm, int &r_step) {
	if (arms <= 1) {
		r_arm = 0;
		r_step = i;
		return;
	}
	if (stride >= arms) {
		const int g = stride / arms + 1;
		const int chunk = i / g;
		r_arm = chunk % arms;
		r_step = (chunk / arms) * g + (i % g);
		return;
	}
	r_arm = i % arms;
	r_step = i / arms;
}

PatternSlots2D BulletPatterns2D::generate_grid2d(int transforms_amount, Transform2D marker_transform, const GridParams2D &params) {
	int rows_per_column = params.rows_per_column;
	Alignment alignment = params.alignment;
	real_t column_offset = params.column_offset;
	real_t row_offset = params.row_offset;
	bool rotate_grid_with_marker = params.rotate_grid_with_marker;
	bool random_local_rotation = params.random_local_rotation;
	real_t jitter = params.jitter;
	uint64_t seed = params.seed;

	if (!pattern_check_amount("helper_generate_transforms_grid", transforms_amount)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(column_offset) || !Math::is_finite(row_offset)) {
		UtilityFunctions::push_error("helper_generate_transforms_grid: offsets must be finite numbers.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(jitter) || jitter < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_grid: jitter must be a finite number >= 0.");
		return PatternSlots2D();
	}
	if (!pattern_check_marker("helper_generate_transforms_grid", marker_transform, false)) {
		return PatternSlots2D();
	}
	if (rows_per_column <= 0) {
		UtilityFunctions::push_error("helper_generate_transforms_grid: rows_per_column must be > 0.");
		return PatternSlots2D();
	}
	// Initialize the array to hold the transforms
	PatternSlots2D generated_transforms;
	generated_transforms.resize(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}

	// A lone bullet lands exactly on the marker (matches ring/fan/line),
	// carrying the marker scale with it.
	if (transforms_amount == 1) {
		Transform2D lone(marker_transform.get_rotation(), marker_transform.get_origin());
		lone.set_scale(marker_transform.get_scale());
		generated_transforms[0] = lone;
		return generated_transforms;
	}

	int columns_amount = 0;

	// Avoid division by 0
	if (rows_per_column > 0) {
		// Calculate the number of columns needed
		columns_amount = static_cast<int>(Math::ceil(static_cast<real_t>(transforms_amount) / static_cast<real_t>(rows_per_column)));
	}

	// Size by the rows/columns actually used, not the full rows_per_column:
	// otherwise a partial grid (e.g. n=1 with rows=10) centers on empty space.
	const int used_rows = (columns_amount > 1) ? rows_per_column : transforms_amount;
	const int last_column_rows = transforms_amount - (columns_amount - 1) * rows_per_column;

	// Calculate total grid dimensions
	real_t total_width = (columns_amount - 1) * column_offset;
	// Default starting position (centered): -total/2 already centers even
	// counts (n=2 -> -off/2, +off/2). The old +=offset/2 shifted the mean +off/2.
	real_t x_start = -total_width / 2.0f;

	// Default y per column: full columns center on used_rows, the ragged last
	// column centers on its own count so it doesn't hang off-center.
	const real_t full_col_height = (used_rows - 1) * row_offset;
	const real_t last_col_height = (last_column_rows - 1) * row_offset;

	// Adjust starting position based on alignment
	switch (alignment) {
		case Alignment::TOP_LEFT:
			x_start = 0.0;
			break;
		case Alignment::TOP_CENTER:
			x_start = -total_width / 2.0f;
			break;
		case Alignment::TOP_RIGHT:
			x_start = -total_width;
			break;
		case Alignment::CENTER_LEFT:
			x_start = 0.0;
			break;
		case Alignment::CENTER:
			// Already centered by default
			break;
		case Alignment::CENTER_RIGHT:
			x_start = -total_width;
			break;
		case Alignment::BOTTOM_LEFT:
			x_start = 0.0;
			break;
		case Alignment::BOTTOM_CENTER:
			x_start = -total_width / 2.0f;
			break;
		case Alignment::BOTTOM_RIGHT:
			x_start = -total_width;
			break;
		default:
			UtilityFunctions::push_error("helper_generate_transforms_grid: unknown alignment, cannot generate grid.");
			return PatternSlots2D();
	}

	// Counter for spawned transforms
	int count_spawned = 0;

	// Seeded RNG for jitter + random rotation: seed 0 keeps the legacy
	// non-deterministic path, otherwise every call reproduces identically.
	Ref<RandomNumberGenerator> grid_rng;
	const bool grid_seeded = seed != 0;
	if (grid_seeded) {
		grid_rng.instantiate();
		grid_rng->set_seed(seed);
	}
	// Generate transforms in a grid pattern
	for (int column = 0; column < columns_amount; ++column) {
		const bool is_last_column = (column == columns_amount - 1);
		const int rows_this_column = is_last_column ? last_column_rows : rows_per_column;
		const real_t col_height = is_last_column ? last_col_height : full_col_height;
		// Per-column y start so TOP_* / CENTER / BOTTOM_* anchor each column.
		real_t y_start = -col_height / 2.0f;
		switch (alignment) {
			case Alignment::TOP_LEFT:
			case Alignment::TOP_CENTER:
			case Alignment::TOP_RIGHT:
				y_start = 0.0;
				break;
			case Alignment::BOTTOM_LEFT:
			case Alignment::BOTTOM_CENTER:
			case Alignment::BOTTOM_RIGHT:
				y_start = -col_height;
				break;
			default:
				break;
		}
		for (int row = 0; row < rows_this_column; ++row) {
			if (count_spawned >= transforms_amount) {
				break;
			}

			// Calculate local offset for this grid position
			real_t x = x_start + column * column_offset;
			real_t y = y_start + row * row_offset;
			Vector2 local_offset(x, y);

			// Create the new transform, carrying the marker scale: a scaled
			// generator scales its bullets (spin/scale passes preserve it too).
			Transform2D new_transform;
			if (rotate_grid_with_marker) {
				// Rotate the offset with the marker's basis
				Vector2 rotated_offset = marker_transform.basis_xform(local_offset);
				new_transform = Transform2D(marker_transform.get_rotation(), marker_transform.get_origin() + rotated_offset);
			} else {
				// Use the offset directly without rotation
				Vector2 new_origin = marker_transform.get_origin() + local_offset;
				new_transform = Transform2D(marker_transform.get_rotation(), new_origin);
			}
			new_transform.set_scale(marker_transform.get_scale());

			// Apply random local rotation if enabled (scale preserved: the
			// rotation-only constructor resets it to 1).
			if (random_local_rotation) {
				real_t random_angle = (grid_seeded ? grid_rng->randf() : UtilityFunctions::randf()) * Math::TAU;
				new_transform = Transform2D(new_transform.get_rotation() + random_angle, new_transform.get_origin());
				new_transform.set_scale(marker_transform.get_scale());
			}

			// Scatter each origin by up to +-jitter on both axes (0 disables it).
			if (jitter > 0.0) {
				real_t jx = grid_seeded ? grid_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
				real_t jy = grid_seeded ? grid_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
				const Vector2 scatter(jx, jy);
				new_transform = Transform2D(new_transform.get_rotation(), new_transform.get_origin() + scatter);
				new_transform.set_scale(marker_transform.get_scale());
			}

			// Store the transform and increment the counter
			generated_transforms[count_spawned] = new_transform;
			count_spawned++;
		}
	}

	danmaku_clamp_slots_finite("helper_generate_transforms_grid", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_fan2d(int transforms_amount, Transform2D marker_transform, const FanParams2D &params) {
	real_t spread = params.spread;
	real_t direction_angle = params.direction_angle;
	real_t step_offset = params.step_offset;
	bool centered = params.centered;
	real_t angle_jitter = params.angle_jitter;
	uint64_t seed = params.seed;

	if (!pattern_check_amount("helper_generate_transforms_fan", transforms_amount)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(spread) || !Math::is_finite(direction_angle) || !Math::is_finite(step_offset)) {
		UtilityFunctions::push_error("helper_generate_transforms_fan: spread, direction_angle and step_offset must be finite numbers.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(angle_jitter) || angle_jitter < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_fan: angle_jitter must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!pattern_check_marker("helper_generate_transforms_fan", marker_transform, false)) {
		return PatternSlots2D();
	}
	PatternSlots2D generated_transforms;
	generated_transforms.resize(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}

	const real_t base_rotation = marker_transform.get_rotation() + direction_angle;
	const real_t step = (transforms_amount > 1) ? spread / (real_t)(transforms_amount - 1) : 0.0;
	// A lone bullet flies straight down the cone center instead of its edge.
	// A one-sided fan (centered = false) starts at the center direction and
	// opens toward +spread instead of straddling the center.
	const real_t first_angle = (transforms_amount > 1 && centered) ? base_rotation - spread * 0.5 : base_rotation;
	const Vector2 origin = marker_transform.get_origin();
	Ref<RandomNumberGenerator> fan_rng;
	const bool fan_seeded = seed != 0;
	if (fan_seeded) {
		fan_rng.instantiate();
		fan_rng->set_seed(seed);
	}
	for (int i = 0; i < transforms_amount; ++i) {
		real_t angle = first_angle + step * (real_t)i;
		if (angle_jitter > 0.0) {
			angle += fan_seeded ? fan_rng->randf_range(-angle_jitter, angle_jitter) : UtilityFunctions::randf_range(-angle_jitter, angle_jitter);
		}
		const Vector2 dir = Vector2(Math::cos(angle), Math::sin(angle));
		// Stagger origins downrange along each slot's own (possibly
		// jittered) facing: a zero step_offset keeps the classic stacked
		// volley origin, anything else fans the muzzles outward so pellets
		// never spawn inside each other.
		Transform2D fan_transf(angle, origin + dir * (step_offset * (real_t)i));
		fan_transf.set_scale(marker_transform.get_scale());
		generated_transforms[i] = fan_transf;
	}
	danmaku_clamp_slots_finite("helper_generate_transforms_fan", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_spiral2d(int transforms_amount, Transform2D marker_transform, const SpiralParams2D &params) {
	real_t start_radius = params.start_radius;
	real_t radius_step = params.radius_step;
	real_t angle_step = params.angle_step;
	bool rotate_with_marker = params.rotate_with_marker;
	SpiralFacingMode facing_mode = params.facing_mode;
	real_t facing_offset_degrees = params.facing_offset_degrees;

	if (!pattern_check_amount("helper_generate_transforms_spiral", transforms_amount)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(start_radius) || !Math::is_finite(radius_step) || !Math::is_finite(angle_step)) {
		UtilityFunctions::push_error("helper_generate_transforms_spiral: start_radius, radius_step and angle_step must be finite numbers.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_spiral: facing_offset_degrees must be a finite number.");
		return PatternSlots2D();
	}
	if (facing_mode < SPIRAL_FACING_TANGENT || facing_mode > SPIRAL_FACING_KEEP_MARKER) {
		UtilityFunctions::push_error("helper_generate_transforms_spiral: unknown facing_mode.");
		return PatternSlots2D();
	}
	if (!pattern_check_marker("helper_generate_transforms_spiral", marker_transform, false)) {
		return PatternSlots2D();
	}
	if (start_radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_spiral: start_radius must be >= 0.");
		return PatternSlots2D();
	}
	PatternSlots2D generated_transforms;
	generated_transforms.resize(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}

	const real_t base_rotation = rotate_with_marker ? marker_transform.get_rotation() : 0.0;
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t r = start_radius + radius_step * (real_t)i;
		const real_t angle = base_rotation + angle_step * (real_t)i;
		const Vector2 offset = Vector2(Math::cos(angle), Math::sin(angle)) * r;
		real_t facing = angle;
		switch (facing_mode) {
			case SPIRAL_FACING_TANGENT: {
				// Travel direction along r(theta) = start + step * theta:
				// dp/dtheta = (step * cos - r * sin, step * sin + r * cos).
				// Exact for collapsed spirals too (step = 0 -> ring tangent).
				const Vector2 tangent = Vector2(radius_step * Math::cos(angle) - r * Math::sin(angle), radius_step * Math::sin(angle) + r * Math::cos(angle));
				facing = (tangent.length_squared() > 0.0) ? tangent.angle() : ((offset.length_squared() > 0.0) ? offset.angle() : angle);
				break;
			}
			case SPIRAL_FACING_RADIAL_OUTWARD:
				// offset already carries the radius sign, so a negative radius
				// mirrors position and facing together (historical behavior).
				facing = (offset.length_squared() > 0.0) ? offset.angle() : angle;
				break;
			case SPIRAL_FACING_TOWARD_CENTER:
				facing = ((offset.length_squared() > 0.0) ? offset.angle() : angle) + Math::PI;
				break;
			case SPIRAL_FACING_KEEP_MARKER:
				facing = base_rotation;
				break;
		}
		Transform2D spiral_transf(facing + facing_offset, origin + offset);
		spiral_transf.set_scale(marker_transform.get_scale());
		generated_transforms[i] = spiral_transf;
	}
	danmaku_clamp_slots_finite("helper_generate_transforms_spiral", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_line2d(int transforms_amount, Transform2D marker_transform, const LineParams2D &params) {
	const Vector2 &direction = params.direction;
	real_t spacing = params.spacing;
	bool face_direction = params.face_direction;
	LineAnchor anchor = params.anchor;
	bool perpendicular = params.perpendicular;

	if (!pattern_check_amount("helper_generate_transforms_line", transforms_amount)) {
		return PatternSlots2D();
	}
	if (!direction.is_finite() || !Math::is_finite(spacing)) {
		UtilityFunctions::push_error("helper_generate_transforms_line: direction and spacing must be finite.");
		return PatternSlots2D();
	}
	if (!pattern_check_marker("helper_generate_transforms_line", marker_transform, false)) {
		return PatternSlots2D();
	}
	if (direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_line: direction must not be zero, the line axis is undefined.");
		return PatternSlots2D();
	}
	if (anchor < LINE_ANCHOR_START || anchor > LINE_ANCHOR_END) {
		UtilityFunctions::push_error("helper_generate_transforms_line: unknown anchor.");
		return PatternSlots2D();
	}
	PatternSlots2D generated_transforms;
	generated_transforms.resize(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}

	const Vector2 axis = direction.normalized();
	real_t facing = marker_transform.get_rotation();
	if (face_direction) {
		facing = axis.angle();
		if (perpendicular) {
			facing += Math::PI * 0.5; // strafe wall: fly 90 degrees off the axis
		}
	}
	const Vector2 origin = marker_transform.get_origin();
	// Anchor picks where the marker sits on the row: center (historical),
	// start, or end. A single bullet always lands exactly on the marker.
	real_t anchor_offset = (real_t)(transforms_amount - 1) * 0.5;
	if (anchor == LINE_ANCHOR_START) {
		anchor_offset = 0.0;
	} else if (anchor == LINE_ANCHOR_END) {
		anchor_offset = (real_t)(transforms_amount - 1);
	}
	for (int i = 0; i < transforms_amount; ++i) {
		Transform2D line_transf(facing, origin + axis * (spacing * ((real_t)i - anchor_offset)));
		line_transf.set_scale(marker_transform.get_scale());
		generated_transforms[i] = line_transf;
	}
	danmaku_clamp_slots_finite("helper_generate_transforms_line", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_aimed2d(int transforms_amount, Transform2D marker_transform, const AimedParams2D &params) {
	const Vector2 &target_position = params.target_position;
	real_t spread = params.spread;
	real_t step_offset = params.step_offset;
	bool centered = params.centered;

	if (!pattern_check_amount("helper_generate_transforms_aimed", transforms_amount)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(spread) || !Math::is_finite(step_offset)) {
		UtilityFunctions::push_error("helper_generate_transforms_aimed: spread and step_offset must be finite numbers.");
		return PatternSlots2D();
	}
	if (!pattern_check_marker("helper_generate_transforms_aimed", marker_transform, false)) {
		return PatternSlots2D();
	}
	if (!target_position.is_finite()) {
		UtilityFunctions::push_error("helper_generate_transforms_aimed: target_position must be finite.");
		return PatternSlots2D();
	}
	const Vector2 to_target = target_position - marker_transform.get_origin();
	if (to_target.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_aimed: target coincides with the marker, direction is undefined.");
		return PatternSlots2D();
	}
	// Cone centered on the marker-to-target direction, in marker-local terms.
	const real_t direction_angle = to_target.angle() - marker_transform.get_rotation();
	FanParams2D fan;
	fan.spread = spread;
	fan.direction_angle = direction_angle;
	fan.step_offset = step_offset;
	fan.centered = centered;
	fan.angle_jitter = 0.0; // no jitter
	fan.seed = 0;
	return generate_fan2d(transforms_amount, marker_transform, fan);
}

PatternSlots2D BulletPatterns2D::generate_rain2d(int transforms_amount, Transform2D marker_transform, const RainParams2D &params) {
	real_t band_width = params.band_width;
	Vector2 rain_direction = params.rain_direction;
	real_t drop_spacing = params.drop_spacing;
	real_t jitter = params.jitter;
	uint64_t seed = params.seed;

	if (!danmaku_validate_head("helper_generate_transforms_rain", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(band_width) || band_width < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_rain: band_width must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!rain_direction.is_finite() || rain_direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_rain: rain_direction must be finite and non-zero.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(drop_spacing) || drop_spacing < 0.0 || !Math::is_finite(jitter) || jitter < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_rain: drop_spacing and jitter must be finite and >= 0.");
		return PatternSlots2D();
	}
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const Vector2 axis = rain_direction.normalized();
	const Vector2 across = axis.orthogonal();
	const real_t facing = axis.angle();
	Ref<RandomNumberGenerator> rain_rng;
	const bool rain_seeded = seed != 0;
	if (rain_seeded) {
		rain_rng.instantiate();
		rain_rng->set_seed(seed);
	}
	// Layered sheets: each row holds up to `cols` drops spread across the
	// whole band; further rows step upstream by drop_spacing. Volleys that
	// fit one row span the band exactly like a single sheet.
	const int cols = rain_columns2d(transforms_amount, band_width, drop_spacing);
	for (int i = 0; i < transforms_amount; ++i) {
		const int row = i / cols;
		const real_t along = rain_along2d(i, transforms_amount, cols, band_width);
		Vector2 pos = origin + across * along - axis * (real_t)row * drop_spacing;
		if (jitter > 0.0) {
			real_t jx = rain_seeded ? rain_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			real_t jy = rain_seeded ? rain_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			pos += Vector2(jx, jy);
		}
		Transform2D slot(facing, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite("helper_generate_transforms_rain", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_scatter2d(int transforms_amount, Transform2D marker_transform, const ScatterParams2D &params) {
	real_t burst_radius = params.burst_radius;
	real_t facing_jitter = params.facing_jitter;
	uint64_t seed = params.seed;
	real_t inner_radius = params.inner_radius;
	Vector2 sector_direction = params.sector_direction;
	real_t sector_arc = params.sector_arc;
	ScatterFacingMode facing_mode = params.facing_mode;

	if (!danmaku_validate_head("helper_generate_transforms_scatter", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(burst_radius) || burst_radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_scatter: burst_radius must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(facing_jitter) || facing_jitter < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_scatter: facing_jitter must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(inner_radius) || inner_radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_scatter: inner_radius must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (facing_mode < SCATTER_FACING_OUTWARD || facing_mode > SCATTER_FACING_INWARD) {
		UtilityFunctions::push_error("helper_generate_transforms_scatter: facing_mode out of range.");
		return PatternSlots2D();
	}
	// Clamp, don't reject: callers set inner/burst in any order, and an
	// inner edge past the rim just means a thin ring at the rim.
	const real_t outer = burst_radius;
	const real_t inner = MIN(MAX(inner_radius, 0.0), outer);
	// Sector: direction fallback mirrors the line/rain generators (dead knob
	// degrades to +X, never stalls). Arc >= TAU is a full circle.
	Vector2 axis = Vector2(1, 0);
	if (sector_direction.is_finite() && sector_direction.length_squared() > 1e-12) {
		axis = sector_direction.normalized();
	}
	real_t arc = sector_arc;
	if (!Math::is_finite(arc) || arc <= 0.0) {
		arc = Math::TAU;
	} else if (arc > Math::TAU) {
		arc = Math::TAU;
	}
	const real_t base_angle = axis.angle();
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	if (seed != 0) {
		rng->set_seed(seed);
	} else {
		rng->randomize();
	}
	const Vector2 origin = marker_transform.get_origin();
	for (int i = 0; i < transforms_amount; ++i) {
		// sqrt distribution over [inner^2, outer^2]: even annulus density
		// instead of center-clumped (inner = 0 reproduces the old disc).
		const real_t rr = inner * inner + (outer * outer - inner * inner) * rng->randf();
		const real_t r = (rr > 0.0) ? Math::sqrt(rr) : 0.0;
		// Full circle keeps the historical draw (bit-identical sequences for
		// old seeds); sectors center on the aim direction instead.
		const real_t a = (arc >= Math::TAU) ? base_angle + rng->randf() * Math::TAU : base_angle + (rng->randf() - 0.5) * arc;
		const Vector2 offset = Vector2(Math::cos(a), Math::sin(a)) * r;
		const real_t radial = (offset.length_squared() > 0.0) ? offset.angle() : marker_transform.get_rotation();
		real_t facing = radial;
		if (facing_mode == SCATTER_FACING_RANDOM) {
			facing = rng->randf() * Math::TAU;
		} else {
			if (facing_mode == SCATTER_FACING_INWARD) {
				facing += Math::PI;
			}
			facing += rng->randf_range(-facing_jitter, facing_jitter);
		}
		Transform2D slot(facing, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite("helper_generate_transforms_scatter", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_star_polygon2d(int transforms_amount, Transform2D marker_transform, const StarPolygonParams2D &params) {
	int vertices = params.vertices;
	real_t radius = params.radius;
	real_t vertex_bias = params.vertex_bias;
	real_t base_rotation = params.base_rotation;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;

	if (!danmaku_validate_head("helper_generate_transforms_star_polygon", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (vertices < 3) {
		UtilityFunctions::push_error("helper_generate_transforms_star_polygon: vertices must be >= 3.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(radius) || radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_star_polygon: radius must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(vertex_bias) || vertex_bias < 0.0 || !Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_star_polygon: vertex_bias, base_rotation and facing_offset_degrees must be finite (bias >= 0).");
		return PatternSlots2D();
	}
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		// Even base angle, then pulled toward the nearest vertex by the
		// bias: bias 0 = even ring, higher = sharper star.
		const real_t base_angle = Math::TAU * (real_t)i / (real_t)transforms_amount + base_rotation;
		const real_t sector = Math::TAU / (real_t)vertices;
		const real_t local = Math::fposmod(base_angle, sector) / sector - 0.5;
		const real_t pull = local * (vertex_bias / (1.0 + vertex_bias));
		const real_t angle = base_angle - pull * sector * 0.5;
		const Vector2 offset = Vector2(Math::cos(angle), Math::sin(angle)) * radius;
		real_t facing = face_outward ? angle : angle + Math::PI;
		facing += facing_offset;
		Transform2D slot(facing, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite("helper_generate_transforms_star_polygon", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_multispiral2d(int transforms_amount, Transform2D marker_transform, const MultispiralParams2D &params) {
	int arms = params.arms;
	real_t start_radius = params.start_radius;
	real_t radius_step = params.radius_step;
	real_t angle_step = params.angle_step;
	bool rotate_with_marker = params.rotate_with_marker;
	SpiralFacingMode facing_mode = params.facing_mode;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int arm_index_stride = params.arm_index_stride;

	if (!danmaku_validate_head("helper_generate_transforms_multispiral", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (arms < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_multispiral: arms must be >= 1.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(start_radius) || start_radius < 0.0 || !Math::is_finite(radius_step) || !Math::is_finite(angle_step)) {
		UtilityFunctions::push_error("helper_generate_transforms_multispiral: start_radius (>= 0), radius_step and angle_step must be finite.");
		return PatternSlots2D();
	}
	if (facing_mode < SPIRAL_FACING_TANGENT || facing_mode > SPIRAL_FACING_KEEP_MARKER) {
		UtilityFunctions::push_error("helper_generate_transforms_multispiral: unknown facing_mode.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_multispiral: facing_offset_degrees must be finite.");
		return PatternSlots2D();
	}
	if (arm_index_stride < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_multispiral: arm_index_stride must be >= 1.");
		return PatternSlots2D();
	}
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const real_t base_rotation = rotate_with_marker ? marker_transform.get_rotation() : 0.0;
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		// Interleave (stride < arms) or group (stride >= arms) slots.
		int arm = 0;
		int step_index = 0;
		spiral_arm_step(i, arms, arm_index_stride, arm, step_index);
		const real_t arm_phase = Math::TAU * (real_t)arm / (real_t)arms;
		const real_t r = start_radius + radius_step * (real_t)step_index;
		const real_t angle = base_rotation + arm_phase + angle_step * (real_t)step_index;
		const Vector2 offset = Vector2(Math::cos(angle), Math::sin(angle)) * r;
		real_t facing = angle;
		switch (facing_mode) {
			case SPIRAL_FACING_TANGENT: {
				const Vector2 tangent = Vector2(radius_step * Math::cos(angle) - r * Math::sin(angle), radius_step * Math::sin(angle) + r * Math::cos(angle));
				facing = (tangent.length_squared() > 0.0) ? tangent.angle() : ((offset.length_squared() > 0.0) ? offset.angle() : angle);
				break;
			}
			case SPIRAL_FACING_RADIAL_OUTWARD:
				facing = (offset.length_squared() > 0.0) ? offset.angle() : angle;
				break;
			case SPIRAL_FACING_TOWARD_CENTER:
				facing = ((offset.length_squared() > 0.0) ? offset.angle() : angle) + Math::PI;
				break;
			case SPIRAL_FACING_KEEP_MARKER:
				facing = base_rotation;
				break;
		}
		Transform2D slot(facing + facing_offset, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite("helper_generate_transforms_multispiral", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_cross2d(int transforms_amount, Transform2D marker_transform, const CrossParams2D &params) {
	int arm_count = params.arm_count;
	real_t arm_length = params.arm_length;
	real_t spacing = params.spacing;
	real_t base_rotation = params.base_rotation;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;

	if (!danmaku_validate_head("helper_generate_transforms_cross", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (arm_count < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_cross: arm_count must be >= 1.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(arm_length) || arm_length < 0.0 || !Math::is_finite(spacing) || spacing <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_cross: arm_length must be finite and >= 0, spacing finite and > 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_cross: base_rotation and facing_offset_degrees must be finite.");
		return PatternSlots2D();
	}
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	// Interleaved fill (i % arms): full rounds of `arm_count` rays each, so
	// a partial last round still spreads across rays. When an arm cannot
	// hold its slots at `spacing`, the spacing shrinks so the outermost slot
	// lands exactly on the tip (every bullet keeps its own spot).
	const real_t step_spacing = cross_step2d(transforms_amount, arm_count, arm_length, spacing);
	for (int i = 0; i < transforms_amount; ++i) {
		const int arm = i % arm_count;
		const int step = i / arm_count;
		const real_t ray = base_rotation + Math::TAU * (real_t)arm / (real_t)arm_count;
		const real_t dist = step_spacing * (real_t)(step + 1);
		const Vector2 offset = Vector2(Math::cos(ray), Math::sin(ray)) * dist;
		real_t facing = face_outward ? ray : ray + Math::PI;
		Transform2D slot(facing + facing_offset, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite("helper_generate_transforms_cross", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_wave2d(int transforms_amount, Transform2D marker_transform, const WaveParams2D &params) {
	real_t width = params.width;
	real_t amplitude = params.amplitude;
	real_t waves = params.waves;
	Vector2 direction = params.direction;
	bool face_direction = params.face_direction;
	real_t facing_offset_degrees = params.facing_offset_degrees;

	if (!danmaku_validate_head("helper_generate_transforms_wave", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(width) || width < 0.0 || !Math::is_finite(amplitude) || amplitude < 0.0 || !Math::is_finite(waves) || waves < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_wave: width, amplitude and waves must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!direction.is_finite() || direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_wave: direction must be finite and non-zero.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_wave: facing_offset_degrees must be finite.");
		return PatternSlots2D();
	}
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const Vector2 axis = direction.normalized();
	const Vector2 across = axis.orthogonal();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t frac = (transforms_amount > 1) ? ((real_t)i / (real_t)(transforms_amount - 1) - 0.5) : 0.0;
		const real_t along = frac * width;
		const real_t wave = amplitude * Math::sin(frac * waves * Math::TAU);
		const Vector2 pos = origin + axis * along + across * wave;
		const Vector2 tangent = (axis + across * (amplitude * waves * Math::TAU / Math::max(width, (real_t)1.0) * Math::cos(frac * waves * Math::TAU))).normalized();
		real_t facing = face_direction ? tangent.angle() : marker_transform.get_rotation();
		Transform2D slot(facing + facing_offset, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite("helper_generate_transforms_wave", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_waterfall2d(int transforms_amount, Transform2D marker_transform, const WaterfallParams2D &params) {
	int columns = params.columns;
	real_t column_spacing = params.column_spacing;
	int rows = params.rows;
	real_t row_spacing = params.row_spacing;
	real_t stagger = params.stagger;
	Vector2 rain_direction = params.rain_direction;
	real_t jitter = params.jitter;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	uint64_t seed = params.seed;

	if (!danmaku_validate_head("helper_generate_transforms_waterfall", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (columns < 1 || rows < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_waterfall: columns and rows must be >= 1.");
		return PatternSlots2D();
	}
	// columns*rows in 64-bit: 32-bit int math would wrap to negative on
	// hostile input (100000x100000), turning the emit loop below into a
	// billion-iteration hang. Reject absurd grids up front.
	if ((int64_t)columns * (int64_t)rows > (int64_t)HELPER_MAX_TRANSFORMS * 4) {
		UtilityFunctions::push_error("helper_generate_transforms_waterfall: columns*rows is absurdly large; keep the grid reasonable.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(column_spacing) || column_spacing < 0.0 || !Math::is_finite(row_spacing) || row_spacing < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_waterfall: column_spacing and row_spacing must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(stagger) || !Math::is_finite(jitter) || jitter < 0.0 || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_waterfall: stagger and facing_offset_degrees must be finite, jitter finite and >= 0.");
		return PatternSlots2D();
	}
	if (!rain_direction.is_finite() || rain_direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_waterfall: rain_direction must be finite and non-zero.");
		return PatternSlots2D();
	}
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	const Vector2 axis = rain_direction.normalized();
	const Vector2 across = axis.orthogonal();
	const real_t facing = axis.angle() + facing_offset;
	const int capacity = columns * rows;
	const int emit = Math::min(transforms_amount, capacity);
	Ref<RandomNumberGenerator> waterfall_rng;
	const bool waterfall_seeded = seed != 0;
	if (waterfall_seeded) {
		waterfall_rng.instantiate();
		waterfall_rng->set_seed(seed);
	}
	for (int i = 0; i < emit; ++i) {
		const int row = i / columns;
		const int col = i % columns;
		const real_t row_phase = (rows > 1) ? ((real_t)row / (real_t)(rows - 1) - 0.5) : 0.0;
		const real_t col_centered = (columns > 1) ? ((real_t)col / (real_t)(columns - 1) - 0.5) : 0.0;
		Vector2 pos = origin + across * (col_centered * column_spacing * (real_t)(columns - 1) + stagger * column_spacing * row_phase) + axis * ((real_t)row * row_spacing);
		if (jitter > 0.0) {
			real_t jx = waterfall_seeded ? waterfall_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			real_t jy = waterfall_seeded ? waterfall_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			pos += Vector2(jx, jy);
		}
		Transform2D slot(facing, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	// Overflow past capacity keeps the curtain growing instead of stacking:
	// each extra row continues down the fall axis at the same column pitch,
	// including the stagger phase, so row N reads as a seamless extension.
	for (int i = emit; i < transforms_amount; ++i) {
		const int extra = i - emit;
		const int col = extra % columns;
		const int extra_row = rows + extra / columns;
		const real_t extra_phase = (rows > 1) ? ((real_t)(extra_row % rows) / (real_t)(rows - 1) - 0.5) : 0.0;
		const real_t col_centered = (columns > 1) ? ((real_t)col / (real_t)(columns - 1) - 0.5) : 0.0;
		Vector2 pos = origin + across * (col_centered * column_spacing * (real_t)(columns - 1) + stagger * column_spacing * extra_phase) + axis * ((real_t)extra_row * row_spacing);
		if (jitter > 0.0) {
			real_t jx = waterfall_seeded ? waterfall_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			real_t jy = waterfall_seeded ? waterfall_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			pos += Vector2(jx, jy);
		}
		Transform2D slot(facing, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite("helper_generate_transforms_waterfall", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_lattice2d(int transforms_amount, Transform2D marker_transform, const LatticeParams2D &params) {
	int columns = params.columns;
	int rows = params.rows;
	real_t spacing_x = params.spacing_x;
	real_t spacing_y = params.spacing_y;
	bool stagger_rows = params.stagger_rows;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;

	if (!danmaku_validate_head("helper_generate_transforms_lattice", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (columns < 1 || rows < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_lattice: columns and rows must be >= 1.");
		return PatternSlots2D();
	}
	// Same 64-bit guard as waterfall: hostile columns*rows would wrap a
	// 32-bit int and hang the emit loop below.
	if ((int64_t)columns * (int64_t)rows > (int64_t)HELPER_MAX_TRANSFORMS * 4) {
		UtilityFunctions::push_error("helper_generate_transforms_lattice: columns*rows is absurdly large; keep the grid reasonable.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(spacing_x) || spacing_x < 0.0 || !Math::is_finite(spacing_y) || spacing_y < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_lattice: spacing_x and spacing_y must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_lattice: facing_offset_degrees must be finite.");
		return PatternSlots2D();
	}
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const real_t marker_rot = marker_transform.get_rotation();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	const int capacity = columns * rows;
	const int emit = Math::min(transforms_amount, capacity);
	for (int i = 0; i < emit; ++i) {
		const int row = i / columns;
		const int col = i % columns;
		const real_t stagger = (stagger_rows && (row % 2 == 1)) ? spacing_x * 0.5 : 0.0;
		const Vector2 pos = origin + Vector2(((real_t)col - (real_t)(columns - 1) * 0.5) * spacing_x + stagger, ((real_t)row - (real_t)(rows - 1) * 0.5) * spacing_y);
		const real_t radial = (pos - origin).length_squared() > 0.0 ? (pos - origin).angle() : marker_rot;
		real_t facing = face_outward ? radial : radial + Math::PI;
		Transform2D slot(facing + facing_offset, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	// Overflow past capacity grows the honeycomb instead of stacking: extra
	// rows continue below the grid at the same pitch (stagger included), so
	// row N reads as a seamless extension like waterfall.
	for (int i = emit; i < transforms_amount; ++i) {
		const int extra = i - emit;
		const int col = extra % columns;
		const int extra_row = rows + extra / columns;
		const real_t stagger = (stagger_rows && (extra_row % 2 == 1)) ? spacing_x * 0.5 : 0.0;
		const Vector2 pos = origin + Vector2(((real_t)col - (real_t)(columns - 1) * 0.5) * spacing_x + stagger, ((real_t)extra_row - (real_t)(rows - 1) * 0.5) * spacing_y);
		const real_t radial = (pos - origin).length_squared() > 0.0 ? (pos - origin).angle() : marker_rot;
		real_t facing = face_outward ? radial : radial + Math::PI;
		Transform2D slot(facing + facing_offset, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite("helper_generate_transforms_lattice", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_counter_spiral2d(int transforms_amount, Transform2D marker_transform, const CounterSpiralParams2D &params) {
	int arms = params.arms;
	real_t start_radius = params.start_radius;
	real_t radius_step = params.radius_step;
	real_t angle_step = params.angle_step;
	bool rotate_with_marker = params.rotate_with_marker;
	SpiralFacingMode facing_mode = params.facing_mode;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int arm_index_stride = params.arm_index_stride;
	bool mirror_alternate_arms = params.mirror_alternate_arms;

	if (!danmaku_validate_head("helper_generate_transforms_counter_spiral", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (arms < 2) {
		UtilityFunctions::push_error("helper_generate_transforms_counter_spiral: arms must be >= 2 (use multispiral for 1 arm).");
		return PatternSlots2D();
	}
	if (!Math::is_finite(start_radius) || start_radius < 0.0 || !Math::is_finite(radius_step) || !Math::is_finite(angle_step)) {
		UtilityFunctions::push_error("helper_generate_transforms_counter_spiral: start_radius (>= 0), radius_step and angle_step must be finite.");
		return PatternSlots2D();
	}
	if (facing_mode < SPIRAL_FACING_TANGENT || facing_mode > SPIRAL_FACING_KEEP_MARKER) {
		UtilityFunctions::push_error("helper_generate_transforms_counter_spiral: unknown facing_mode.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_counter_spiral: facing_offset_degrees must be finite.");
		return PatternSlots2D();
	}
	if (arm_index_stride < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_counter_spiral: arm_index_stride must be >= 1.");
		return PatternSlots2D();
	}
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const real_t base_rotation = rotate_with_marker ? marker_transform.get_rotation() : 0.0;
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		int arm = 0;
		int step_index = 0;
		spiral_arm_step(i, arms, arm_index_stride, arm, step_index);
		const real_t dir_sign = (mirror_alternate_arms && (arm % 2 == 1)) ? -1.0 : 1.0;
		const real_t arm_phase = Math::TAU * (real_t)arm / (real_t)arms;
		const real_t r = start_radius + radius_step * (real_t)step_index;
		const real_t angle = base_rotation + arm_phase + dir_sign * angle_step * (real_t)step_index;
		const Vector2 offset = Vector2(Math::cos(angle), Math::sin(angle)) * r;
		real_t facing = angle;
		switch (facing_mode) {
			case SPIRAL_FACING_TANGENT: {
				const real_t signed_step = dir_sign * radius_step;
				const Vector2 tangent = Vector2(signed_step * Math::cos(angle) - r * Math::sin(angle), signed_step * Math::sin(angle) + r * Math::cos(angle));
				facing = (tangent.length_squared() > 0.0) ? tangent.angle() : ((offset.length_squared() > 0.0) ? offset.angle() : angle);
				break;
			}
			case SPIRAL_FACING_RADIAL_OUTWARD:
				facing = (offset.length_squared() > 0.0) ? offset.angle() : angle;
				break;
			case SPIRAL_FACING_TOWARD_CENTER:
				facing = ((offset.length_squared() > 0.0) ? offset.angle() : angle) + Math::PI;
				break;
			case SPIRAL_FACING_KEEP_MARKER:
				facing = base_rotation;
				break;
		}
		Transform2D slot(facing + facing_offset, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite("helper_generate_transforms_counter_spiral", generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_corridor2d(int transforms_amount, Transform2D marker_transform, const CorridorParams2D &params) {
	const Vector2 &aim_direction = params.aim_direction;
	real_t width = params.width;
	real_t spacing = params.spacing;
	real_t gap_width = params.gap_width;
	bool face_aim = params.face_aim;
	real_t facing_offset_degrees = params.facing_offset_degrees;

	if (!danmaku_validate_head("helper_generate_transforms_corridor", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (!aim_direction.is_finite() || aim_direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_corridor: aim_direction must be finite and non-zero.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(width) || width < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_corridor: width must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(spacing) || spacing <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_corridor: spacing must be finite and > 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(gap_width) || gap_width < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_corridor: gap_width must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_corridor: facing_offset_degrees must be finite.");
		return PatternSlots2D();
	}
	if (gap_width >= width) {
		UtilityFunctions::push_error("helper_generate_transforms_corridor: gap_width eats the whole wall (must be < width).");
		return PatternSlots2D();
	}
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const Vector2 axis = aim_direction.normalized();
	const Vector2 across = axis.orthogonal();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	const real_t aim_angle = axis.angle();
	// Two equal wall segments flank the door: [-W/2, -G/2] and [G/2, W/2].
	// The amount is split between them (left takes the odd bullet) and each
	// segment spans outer edge to door edge, so exactly transforms_amount
	// bullets are placed and none lands in the door.
	const real_t half_w = width * 0.5;
	const real_t half_g = gap_width * 0.5;
	const int left_count = (transforms_amount + 1) / 2;
	int placed = 0;
	for (int i = 0; i < transforms_amount; ++i) {
		const bool left = i < left_count;
		const int c = left ? left_count : transforms_amount - left_count;
		const int k = left ? i : i - left_count;
		// Segment run in "distance from the outer edge" units.
		const real_t seg_len = half_w - half_g;
		const real_t d = (c > 1) ? seg_len * (real_t)k / (real_t)(c - 1) : seg_len * 0.5;
		const real_t across_coord = left ? (-half_w + d) : (half_g + (seg_len - d));
		const Vector2 pos = origin + across * across_coord;
		real_t facing = aim_angle;
		if (!face_aim) {
			facing = ((pos - origin).length_squared() > 0.0) ? (pos - origin).angle() : aim_angle;
		}
		Transform2D slot(facing + facing_offset, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		if (placed < transforms_amount) {
			generated_transforms[placed] = slot;
			++placed;
		}
	}
	generated_transforms.resize(placed);
	danmaku_clamp_slots_finite("helper_generate_transforms_corridor", generated_transforms);
	return generated_transforms;
}

} // namespace BlastBullets2D
