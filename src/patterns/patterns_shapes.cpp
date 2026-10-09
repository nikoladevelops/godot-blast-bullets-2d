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

// Facing of a spiral slot at `angle` and radius r: TANGENT follows
// r(theta) = start + step * theta (dp/dtheta = (step cos - r sin, step sin +
// r cos), exact for collapsed spirals: step 0 -> ring tangent), the radial
// modes use the offset (it carries the radius sign, so a negative radius
// mirrors position and facing together), KEEP_MARKER the base rotation.
static real_t spiral_facing2d(BulletPatterns2D::SpiralFacingMode mode, real_t angle, real_t r, real_t signed_step, const Vector2 &offset, real_t base_rotation) {
	switch (mode) {
		case BulletPatterns2D::SPIRAL_FACING_TANGENT: {
			const Vector2 tangent = Vector2(signed_step * Math::cos(angle) - r * Math::sin(angle), signed_step * Math::sin(angle) + r * Math::cos(angle));
			return (tangent.length_squared() > 0.0) ? tangent.angle() : ((offset.length_squared() > 0.0) ? offset.angle() : angle);
		}
		case BulletPatterns2D::SPIRAL_FACING_RADIAL_OUTWARD:
			return (offset.length_squared() > 0.0) ? offset.angle() : angle;
		case BulletPatterns2D::SPIRAL_FACING_TOWARD_CENTER:
			return ((offset.length_squared() > 0.0) ? offset.angle() : angle) + Math::PI;
		case BulletPatterns2D::SPIRAL_FACING_KEEP_MARKER:
			return base_rotation;
	}
	return angle;
}

// Multi-arm spiral core (multispiral; counter_spiral with alternate arms
// winding the other way). Arms are evenly phased; spiral_arm_step deals the
// slots over the arms.
static PatternSlots2D arm_spirals2d(const char *caller, int transforms_amount, const Transform2D &marker_transform, int arms, real_t start_radius, real_t radius_step, real_t angle_step, bool rotate_with_marker, BulletPatterns2D::SpiralFacingMode facing_mode, real_t facing_offset_degrees, int arm_index_stride, bool mirror_alternate_arms) {
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
		const real_t facing = spiral_facing2d(facing_mode, angle, r, dir_sign * radius_step, offset, base_rotation);
		Transform2D slot(facing + facing_offset, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_grid2d(int transforms_amount, Transform2D marker_transform, const GridParams2D &p) {
	const char *caller = "helper_generate_transforms_grid";

	PATTERN_REQUIRE(pattern_check_amount(caller, transforms_amount));
	PATTERN_REJECT_IF(!Math::is_finite(p.column_offset) || !Math::is_finite(p.row_offset), "offsets must be finite numbers.");
	PATTERN_REJECT_IF(!Math::is_finite(p.jitter) || p.jitter < 0.0, "jitter must be a finite number >= 0.");
	PATTERN_REQUIRE(pattern_check_marker(caller, marker_transform, false));
	PATTERN_REJECT_IF(p.rows_per_column <= 0, "rows_per_column must be > 0.");
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

	const int columns_amount = static_cast<int>(Math::ceil(static_cast<real_t>(transforms_amount) / static_cast<real_t>(p.rows_per_column)));

	// Size by the rows/columns actually used, not the full rows_per_column:
	// otherwise a partial grid (e.g. n=1 with rows=10) centers on empty space.
	const int used_rows = (columns_amount > 1) ? p.rows_per_column : transforms_amount;
	const int last_column_rows = transforms_amount - (columns_amount - 1) * p.rows_per_column;

	// Alignment = 3 x 3 anchors: column a % 3 (left, center, right), row
	// a / 3 (top, middle, bottom). Centering uses -extent/2, which already
	// centers even counts (n=2 -> -off/2, +off/2).
	PATTERN_REJECT_IF(p.alignment < Alignment::TOP_LEFT || p.alignment > Alignment::BOTTOM_RIGHT, "unknown alignment, cannot generate grid.");
	const int anchor_x = (int)p.alignment % 3;
	const int anchor_y = (int)p.alignment / 3;
	const real_t total_width = (columns_amount - 1) * p.column_offset;
	const real_t x_start = anchor_x == 0 ? 0.0 : (anchor_x == 1 ? -total_width / 2.0f : -total_width);

	// Default y per column: full columns center on used_rows, the ragged last
	// column centers on its own count so it doesn't hang off-center.
	const real_t full_col_height = (used_rows - 1) * p.row_offset;
	const real_t last_col_height = (last_column_rows - 1) * p.row_offset;

	// Counter for spawned transforms
	int count_spawned = 0;

	// Seeded RNG for jitter + random rotation: seed 0 keeps the legacy
	// non-deterministic path, otherwise every call reproduces identically.
	Ref<RandomNumberGenerator> grid_rng;
	const bool grid_seeded = p.seed != 0;
	if (grid_seeded) {
		grid_rng.instantiate();
		grid_rng->set_seed(p.seed);
	}
	// Generate transforms in a grid pattern
	for (int column = 0; column < columns_amount; ++column) {
		const bool is_last_column = (column == columns_amount - 1);
		const int rows_this_column = is_last_column ? last_column_rows : p.rows_per_column;
		const real_t col_height = is_last_column ? last_col_height : full_col_height;
		// Per-column y start so TOP_* / CENTER / BOTTOM_* anchor each column.
		const real_t y_start = anchor_y == 0 ? 0.0 : (anchor_y == 1 ? -col_height / 2.0f : -col_height);
		for (int row = 0; row < rows_this_column; ++row) {
			if (count_spawned >= transforms_amount) {
				break;
			}

			// Calculate local offset for this grid position
			real_t x = x_start + column * p.column_offset;
			real_t y = y_start + row * p.row_offset;
			Vector2 local_offset(x, y);

			// Create the new transform, carrying the marker scale: a scaled
			// generator scales its bullets (spin/scale passes preserve it too).
			Transform2D new_transform;
			if (p.rotate_grid_with_marker) {
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
			if (p.random_local_rotation) {
				real_t random_angle = (grid_seeded ? grid_rng->randf() : UtilityFunctions::randf()) * Math::TAU;
				new_transform = Transform2D(new_transform.get_rotation() + random_angle, new_transform.get_origin());
				new_transform.set_scale(marker_transform.get_scale());
			}

			// Scatter each origin by up to +-jitter on both axes (0 disables it).
			if (p.jitter > 0.0) {
				real_t jx = grid_seeded ? grid_rng->randf_range(-p.jitter, p.jitter) : UtilityFunctions::randf_range(-p.jitter, p.jitter);
				real_t jy = grid_seeded ? grid_rng->randf_range(-p.jitter, p.jitter) : UtilityFunctions::randf_range(-p.jitter, p.jitter);
				const Vector2 scatter(jx, jy);
				new_transform = Transform2D(new_transform.get_rotation(), new_transform.get_origin() + scatter);
				new_transform.set_scale(marker_transform.get_scale());
			}

			// Store the transform and increment the counter
			generated_transforms[count_spawned] = new_transform;
			count_spawned++;
		}
	}

	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_fan2d(int transforms_amount, Transform2D marker_transform, const FanParams2D &p) {
	const char *caller = "helper_generate_transforms_fan";

	PATTERN_REQUIRE(pattern_check_amount(caller, transforms_amount));
	PATTERN_REJECT_IF(!Math::is_finite(p.spread) || !Math::is_finite(p.direction_angle) || !Math::is_finite(p.step_offset), "spread, direction_angle and step_offset must be finite numbers.");
	PATTERN_REJECT_IF(!Math::is_finite(p.angle_jitter) || p.angle_jitter < 0.0, "angle_jitter must be finite and >= 0.");
	PATTERN_REQUIRE(pattern_check_marker(caller, marker_transform, false));
	PatternSlots2D generated_transforms;
	generated_transforms.resize(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}

	const real_t base_rotation = marker_transform.get_rotation() + p.direction_angle;
	const real_t step = (transforms_amount > 1) ? p.spread / (real_t)(transforms_amount - 1) : 0.0;
	// A lone bullet flies straight down the cone center instead of its edge.
	// A one-sided fan (centered = false) starts at the center direction and
	// opens toward +spread instead of straddling the center.
	const real_t first_angle = (transforms_amount > 1 && p.centered) ? base_rotation - p.spread * 0.5 : base_rotation;
	const Vector2 origin = marker_transform.get_origin();
	Ref<RandomNumberGenerator> fan_rng;
	const bool fan_seeded = p.seed != 0;
	if (fan_seeded) {
		fan_rng.instantiate();
		fan_rng->set_seed(p.seed);
	}
	for (int i = 0; i < transforms_amount; ++i) {
		real_t angle = first_angle + step * (real_t)i;
		if (p.angle_jitter > 0.0) {
			angle += fan_seeded ? fan_rng->randf_range(-p.angle_jitter, p.angle_jitter) : UtilityFunctions::randf_range(-p.angle_jitter, p.angle_jitter);
		}
		const Vector2 dir = Vector2(Math::cos(angle), Math::sin(angle));
		// Stagger origins downrange along each slot's own (possibly
		// jittered) facing: a zero step_offset keeps the classic stacked
		// volley origin, anything else fans the muzzles outward so pellets
		// never spawn inside each other.
		Transform2D fan_transf(angle, origin + dir * (p.step_offset * (real_t)i));
		fan_transf.set_scale(marker_transform.get_scale());
		generated_transforms[i] = fan_transf;
	}
	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_spiral2d(int transforms_amount, Transform2D marker_transform, const SpiralParams2D &p) {
	const char *caller = "helper_generate_transforms_spiral";

	PATTERN_REQUIRE(pattern_check_amount(caller, transforms_amount));
	PATTERN_REJECT_IF(!Math::is_finite(p.start_radius) || !Math::is_finite(p.radius_step) || !Math::is_finite(p.angle_step), "start_radius, radius_step and angle_step must be finite numbers.");
	PATTERN_REJECT_IF(!Math::is_finite(p.facing_offset_degrees), "facing_offset_degrees must be a finite number.");
	PATTERN_REJECT_IF(p.facing_mode < SPIRAL_FACING_TANGENT || p.facing_mode > SPIRAL_FACING_KEEP_MARKER, "unknown facing_mode.");
	PATTERN_REQUIRE(pattern_check_marker(caller, marker_transform, false));
	PATTERN_REJECT_IF(p.start_radius < 0.0, "start_radius must be >= 0.");
	PatternSlots2D generated_transforms;
	generated_transforms.resize(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}

	const real_t base_rotation = p.rotate_with_marker ? marker_transform.get_rotation() : 0.0;
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(p.facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t r = p.start_radius + p.radius_step * (real_t)i;
		const real_t angle = base_rotation + p.angle_step * (real_t)i;
		const Vector2 offset = Vector2(Math::cos(angle), Math::sin(angle)) * r;
		const real_t facing = spiral_facing2d(p.facing_mode, angle, r, p.radius_step, offset, base_rotation);
		Transform2D spiral_transf(facing + facing_offset, origin + offset);
		spiral_transf.set_scale(marker_transform.get_scale());
		generated_transforms[i] = spiral_transf;
	}
	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_line2d(int transforms_amount, Transform2D marker_transform, const LineParams2D &p) {
	const char *caller = "helper_generate_transforms_line";

	PATTERN_REQUIRE(pattern_check_amount(caller, transforms_amount));
	PATTERN_REJECT_IF(!p.direction.is_finite() || !Math::is_finite(p.spacing), "direction and spacing must be finite.");
	PATTERN_REQUIRE(pattern_check_marker(caller, marker_transform, false));
	PATTERN_REJECT_IF(p.direction.length_squared() <= 0.0, "direction must not be zero, the line axis is undefined.");
	PATTERN_REJECT_IF(p.anchor < LINE_ANCHOR_START || p.anchor > LINE_ANCHOR_END, "unknown anchor.");
	PatternSlots2D generated_transforms;
	generated_transforms.resize(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}

	const Vector2 axis = p.direction.normalized();
	real_t facing = marker_transform.get_rotation();
	if (p.face_direction) {
		facing = axis.angle();
		if (p.perpendicular) {
			facing += Math::PI * 0.5; // strafe wall: fly 90 degrees off the axis
		}
	}
	const Vector2 origin = marker_transform.get_origin();
	// Anchor picks where the marker sits on the row: center (historical),
	// start, or end. A single bullet always lands exactly on the marker.
	real_t anchor_offset = (real_t)(transforms_amount - 1) * 0.5;
	if (p.anchor == LINE_ANCHOR_START) {
		anchor_offset = 0.0;
	} else if (p.anchor == LINE_ANCHOR_END) {
		anchor_offset = (real_t)(transforms_amount - 1);
	}
	for (int i = 0; i < transforms_amount; ++i) {
		Transform2D line_transf(facing, origin + axis * (p.spacing * ((real_t)i - anchor_offset)));
		line_transf.set_scale(marker_transform.get_scale());
		generated_transforms[i] = line_transf;
	}
	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_aimed2d(int transforms_amount, Transform2D marker_transform, const AimedParams2D &p) {
	const char *caller = "helper_generate_transforms_aimed";

	PATTERN_REQUIRE(pattern_check_amount(caller, transforms_amount));
	PATTERN_REJECT_IF(!Math::is_finite(p.spread) || !Math::is_finite(p.step_offset), "spread and step_offset must be finite numbers.");
	PATTERN_REQUIRE(pattern_check_marker(caller, marker_transform, false));
	PATTERN_REJECT_IF(!p.target_position.is_finite(), "target_position must be finite.");
	const Vector2 to_target = p.target_position - marker_transform.get_origin();
	PATTERN_REJECT_IF(to_target.length_squared() <= 0.0, "target coincides with the marker, direction is undefined.");
	// Cone centered on the marker-to-target direction, in marker-local terms.
	const real_t direction_angle = to_target.angle() - marker_transform.get_rotation();
	FanParams2D fan;
	fan.spread = p.spread;
	fan.direction_angle = direction_angle;
	fan.step_offset = p.step_offset;
	fan.centered = p.centered;
	fan.angle_jitter = 0.0; // no jitter
	fan.seed = 0;
	return generate_fan2d(transforms_amount, marker_transform, fan);
}

PatternSlots2D BulletPatterns2D::generate_rain2d(int transforms_amount, Transform2D marker_transform, const RainParams2D &p) {
	const char *caller = "helper_generate_transforms_rain";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(!Math::is_finite(p.band_width) || p.band_width < 0.0, "band_width must be finite and >= 0.");
	PATTERN_REJECT_IF(!p.rain_direction.is_finite() || p.rain_direction.length_squared() <= 0.0, "rain_direction must be finite and non-zero.");
	PATTERN_REJECT_IF(!Math::is_finite(p.drop_spacing) || p.drop_spacing < 0.0 || !Math::is_finite(p.jitter) || p.jitter < 0.0, "drop_spacing and jitter must be finite and >= 0.");
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const Vector2 axis = p.rain_direction.normalized();
	const Vector2 across = axis.orthogonal();
	const real_t facing = axis.angle();
	Ref<RandomNumberGenerator> rain_rng;
	const bool rain_seeded = p.seed != 0;
	if (rain_seeded) {
		rain_rng.instantiate();
		rain_rng->set_seed(p.seed);
	}
	// Layered sheets: each row holds up to `cols` drops spread across the
	// whole band; further rows step upstream by drop_spacing. Volleys that
	// fit one row span the band exactly like a single sheet.
	const int cols = rain_columns2d(transforms_amount, p.band_width, p.drop_spacing);
	for (int i = 0; i < transforms_amount; ++i) {
		const int row = i / cols;
		const real_t along = rain_along2d(i, transforms_amount, cols, p.band_width);
		Vector2 pos = origin + across * along - axis * (real_t)row * p.drop_spacing;
		if (p.jitter > 0.0) {
			real_t jx = rain_seeded ? rain_rng->randf_range(-p.jitter, p.jitter) : UtilityFunctions::randf_range(-p.jitter, p.jitter);
			real_t jy = rain_seeded ? rain_rng->randf_range(-p.jitter, p.jitter) : UtilityFunctions::randf_range(-p.jitter, p.jitter);
			pos += Vector2(jx, jy);
		}
		Transform2D slot(facing, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_scatter2d(int transforms_amount, Transform2D marker_transform, const ScatterParams2D &p) {
	const char *caller = "helper_generate_transforms_scatter";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(!Math::is_finite(p.burst_radius) || p.burst_radius < 0.0, "burst_radius must be finite and >= 0.");
	PATTERN_REJECT_IF(!Math::is_finite(p.facing_jitter) || p.facing_jitter < 0.0, "facing_jitter must be finite and >= 0.");
	PATTERN_REJECT_IF(!Math::is_finite(p.inner_radius) || p.inner_radius < 0.0, "inner_radius must be finite and >= 0.");
	PATTERN_REJECT_IF(p.facing_mode < SCATTER_FACING_OUTWARD || p.facing_mode > SCATTER_FACING_INWARD, "facing_mode out of range.");
	// Clamp, don't reject: callers set inner/burst in any order, and an
	// inner edge past the rim just means a thin ring at the rim.
	const real_t outer = p.burst_radius;
	const real_t inner = MIN(MAX(p.inner_radius, 0.0), outer);
	// Sector: direction fallback mirrors the line/rain generators (dead knob
	// degrades to +X, never stalls). Arc >= TAU is a full circle.
	Vector2 axis = Vector2(1, 0);
	if (p.sector_direction.is_finite() && p.sector_direction.length_squared() > 1e-12) {
		axis = p.sector_direction.normalized();
	}
	real_t arc = p.sector_arc;
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
	if (p.seed != 0) {
		rng->set_seed(p.seed);
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
		if (p.facing_mode == SCATTER_FACING_RANDOM) {
			facing = rng->randf() * Math::TAU;
		} else {
			if (p.facing_mode == SCATTER_FACING_INWARD) {
				facing += Math::PI;
			}
			facing += rng->randf_range(-p.facing_jitter, p.facing_jitter);
		}
		Transform2D slot(facing, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_star_polygon2d(int transforms_amount, Transform2D marker_transform, const StarPolygonParams2D &p) {
	const char *caller = "helper_generate_transforms_star_polygon";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(p.vertices < 3, "vertices must be >= 3.");
	PATTERN_REJECT_IF(!Math::is_finite(p.radius) || p.radius < 0.0, "radius must be finite and >= 0.");
	PATTERN_REJECT_IF(!Math::is_finite(p.vertex_bias) || p.vertex_bias < 0.0 || !Math::is_finite(p.base_rotation) || !Math::is_finite(p.facing_offset_degrees), "vertex_bias, base_rotation and facing_offset_degrees must be finite (bias >= 0).");
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(p.facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		// Even base angle, then pulled toward the nearest vertex by the
		// bias: bias 0 = even ring, higher = sharper star.
		const real_t base_angle = Math::TAU * (real_t)i / (real_t)transforms_amount + p.base_rotation;
		const real_t sector = Math::TAU / (real_t)p.vertices;
		const real_t local = Math::fposmod(base_angle, sector) / sector - 0.5;
		const real_t pull = local * (p.vertex_bias / (1.0 + p.vertex_bias));
		const real_t angle = base_angle - pull * sector * 0.5;
		const Vector2 offset = Vector2(Math::cos(angle), Math::sin(angle)) * p.radius;
		real_t facing = p.face_outward ? angle : angle + Math::PI;
		facing += facing_offset;
		Transform2D slot(facing, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_multispiral2d(int transforms_amount, Transform2D marker_transform, const MultispiralParams2D &p) {
	const char *caller = "helper_generate_transforms_multispiral";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(p.arms < 1, "arms must be >= 1.");
	PATTERN_REJECT_IF(!Math::is_finite(p.start_radius) || p.start_radius < 0.0 || !Math::is_finite(p.radius_step) || !Math::is_finite(p.angle_step), "start_radius (>= 0), radius_step and angle_step must be finite.");
	PATTERN_REJECT_IF(p.facing_mode < SPIRAL_FACING_TANGENT || p.facing_mode > SPIRAL_FACING_KEEP_MARKER, "unknown facing_mode.");
	PATTERN_REJECT_IF(!Math::is_finite(p.facing_offset_degrees), "facing_offset_degrees must be finite.");
	PATTERN_REJECT_IF(p.arm_index_stride < 1, "arm_index_stride must be >= 1.");
	return arm_spirals2d(caller, transforms_amount, marker_transform, p.arms, p.start_radius, p.radius_step, p.angle_step, p.rotate_with_marker, p.facing_mode, p.facing_offset_degrees, p.arm_index_stride, false);
}

PatternSlots2D BulletPatterns2D::generate_cross2d(int transforms_amount, Transform2D marker_transform, const CrossParams2D &p) {
	const char *caller = "helper_generate_transforms_cross";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(p.arm_count < 1, "arm_count must be >= 1.");
	PATTERN_REJECT_IF(!Math::is_finite(p.arm_length) || p.arm_length < 0.0 || !Math::is_finite(p.spacing) || p.spacing <= 0.0, "arm_length must be finite and >= 0, spacing finite and > 0.");
	PATTERN_REJECT_IF(!Math::is_finite(p.base_rotation) || !Math::is_finite(p.facing_offset_degrees), "base_rotation and facing_offset_degrees must be finite.");
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(p.facing_offset_degrees);
	// Interleaved fill (i % arms): full rounds of `arm_count` rays each, so
	// a partial last round still spreads across rays. When an arm cannot
	// hold its slots at `spacing`, the spacing shrinks so the outermost slot
	// lands exactly on the tip (every bullet keeps its own spot).
	const real_t step_spacing = cross_step2d(transforms_amount, p.arm_count, p.arm_length, p.spacing);
	for (int i = 0; i < transforms_amount; ++i) {
		const int arm = i % p.arm_count;
		const int step = i / p.arm_count;
		const real_t ray = p.base_rotation + Math::TAU * (real_t)arm / (real_t)p.arm_count;
		const real_t dist = step_spacing * (real_t)(step + 1);
		const Vector2 offset = Vector2(Math::cos(ray), Math::sin(ray)) * dist;
		real_t facing = p.face_outward ? ray : ray + Math::PI;
		Transform2D slot(facing + facing_offset, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_wave2d(int transforms_amount, Transform2D marker_transform, const WaveParams2D &p) {
	const char *caller = "helper_generate_transforms_wave";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(!Math::is_finite(p.width) || p.width < 0.0 || !Math::is_finite(p.amplitude) || p.amplitude < 0.0 || !Math::is_finite(p.waves) || p.waves < 0.0, "width, amplitude and waves must be finite and >= 0.");
	PATTERN_REJECT_IF(!p.direction.is_finite() || p.direction.length_squared() <= 0.0, "direction must be finite and non-zero.");
	PATTERN_REJECT_IF(!Math::is_finite(p.facing_offset_degrees), "facing_offset_degrees must be finite.");
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const Vector2 axis = p.direction.normalized();
	const Vector2 across = axis.orthogonal();
	const real_t facing_offset = Math::deg_to_rad(p.facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t frac = (transforms_amount > 1) ? ((real_t)i / (real_t)(transforms_amount - 1) - 0.5) : 0.0;
		const real_t along = frac * p.width;
		const real_t wave = p.amplitude * Math::sin(frac * p.waves * Math::TAU);
		const Vector2 pos = origin + axis * along + across * wave;
		const Vector2 tangent = (axis + across * (p.amplitude * p.waves * Math::TAU / Math::max(p.width, (real_t)1.0) * Math::cos(frac * p.waves * Math::TAU))).normalized();
		real_t facing = p.face_direction ? tangent.angle() : marker_transform.get_rotation();
		Transform2D slot(facing + facing_offset, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_waterfall2d(int transforms_amount, Transform2D marker_transform, const WaterfallParams2D &p) {
	const char *caller = "helper_generate_transforms_waterfall";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(p.columns < 1 || p.rows < 1, "columns and rows must be >= 1.");
	// columns*rows in 64-bit: 32-bit int math would wrap to negative on
	// hostile input (100000x100000), turning the emit loop below into a
	// billion-iteration hang. Reject absurd grids up front.
	PATTERN_REJECT_IF((int64_t)p.columns * (int64_t)p.rows > (int64_t)pattern_max_bullets() * 4, "columns*rows is absurdly large; keep the grid reasonable.");
	PATTERN_REJECT_IF(!Math::is_finite(p.column_spacing) || p.column_spacing < 0.0 || !Math::is_finite(p.row_spacing) || p.row_spacing < 0.0, "column_spacing and row_spacing must be finite and >= 0.");
	PATTERN_REJECT_IF(!Math::is_finite(p.stagger) || !Math::is_finite(p.jitter) || p.jitter < 0.0 || !Math::is_finite(p.facing_offset_degrees), "stagger and facing_offset_degrees must be finite, jitter finite and >= 0.");
	PATTERN_REJECT_IF(!p.rain_direction.is_finite() || p.rain_direction.length_squared() <= 0.0, "rain_direction must be finite and non-zero.");
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(p.facing_offset_degrees);
	const Vector2 axis = p.rain_direction.normalized();
	const Vector2 across = axis.orthogonal();
	const real_t facing = axis.angle() + facing_offset;
	Ref<RandomNumberGenerator> waterfall_rng;
	const bool waterfall_seeded = p.seed != 0;
	if (waterfall_seeded) {
		waterfall_rng.instantiate();
		waterfall_rng->set_seed(p.seed);
	}
	// Row-major over columns x rows. Slots past the capacity keep the
	// curtain growing instead of stacking: extra rows continue down the fall
	// axis at the same pitch with the stagger phase repeating (row % rows),
	// so row N reads as a seamless extension.
	for (int i = 0; i < transforms_amount; ++i) {
		const int row = i / p.columns;
		const int col = i % p.columns;
		const real_t row_phase = (p.rows > 1) ? ((real_t)(row % p.rows) / (real_t)(p.rows - 1) - 0.5) : 0.0;
		const real_t col_centered = (p.columns > 1) ? ((real_t)col / (real_t)(p.columns - 1) - 0.5) : 0.0;
		Vector2 pos = origin + across * (col_centered * p.column_spacing * (real_t)(p.columns - 1) + p.stagger * p.column_spacing * row_phase) + axis * ((real_t)row * p.row_spacing);
		if (p.jitter > 0.0) {
			real_t jx = waterfall_seeded ? waterfall_rng->randf_range(-p.jitter, p.jitter) : UtilityFunctions::randf_range(-p.jitter, p.jitter);
			real_t jy = waterfall_seeded ? waterfall_rng->randf_range(-p.jitter, p.jitter) : UtilityFunctions::randf_range(-p.jitter, p.jitter);
			pos += Vector2(jx, jy);
		}
		Transform2D slot(facing, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_lattice2d(int transforms_amount, Transform2D marker_transform, const LatticeParams2D &p) {
	const char *caller = "helper_generate_transforms_lattice";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(p.columns < 1 || p.rows < 1, "columns and rows must be >= 1.");
	// Same 64-bit guard as waterfall: hostile columns*rows would wrap a
	// 32-bit int and hang the emit loop below.
	PATTERN_REJECT_IF((int64_t)p.columns * (int64_t)p.rows > (int64_t)pattern_max_bullets() * 4, "columns*rows is absurdly large; keep the grid reasonable.");
	PATTERN_REJECT_IF(!Math::is_finite(p.spacing_x) || p.spacing_x < 0.0 || !Math::is_finite(p.spacing_y) || p.spacing_y < 0.0, "spacing_x and spacing_y must be finite and >= 0.");
	PATTERN_REJECT_IF(!Math::is_finite(p.facing_offset_degrees), "facing_offset_degrees must be finite.");
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const real_t marker_rot = marker_transform.get_rotation();
	const real_t facing_offset = Math::deg_to_rad(p.facing_offset_degrees);
	// Row-major honeycomb; slots past columns x rows grow it with extra rows
	// below at the same pitch (stagger included) instead of stacking.
	for (int i = 0; i < transforms_amount; ++i) {
		const int row = i / p.columns;
		const int col = i % p.columns;
		const real_t stagger = (p.stagger_rows && (row % 2 == 1)) ? p.spacing_x * 0.5 : 0.0;
		const Vector2 pos = origin + Vector2(((real_t)col - (real_t)(p.columns - 1) * 0.5) * p.spacing_x + stagger, ((real_t)row - (real_t)(p.rows - 1) * 0.5) * p.spacing_y);
		const real_t radial = (pos - origin).length_squared() > 0.0 ? (pos - origin).angle() : marker_rot;
		real_t facing = p.face_outward ? radial : radial + Math::PI;
		Transform2D slot(facing + facing_offset, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

PatternSlots2D BulletPatterns2D::generate_counter_spiral2d(int transforms_amount, Transform2D marker_transform, const CounterSpiralParams2D &p) {
	const char *caller = "helper_generate_transforms_counter_spiral";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(p.arms < 2, "arms must be >= 2 (use multispiral for 1 arm).");
	PATTERN_REJECT_IF(!Math::is_finite(p.start_radius) || p.start_radius < 0.0 || !Math::is_finite(p.radius_step) || !Math::is_finite(p.angle_step), "start_radius (>= 0), radius_step and angle_step must be finite.");
	PATTERN_REJECT_IF(p.facing_mode < SPIRAL_FACING_TANGENT || p.facing_mode > SPIRAL_FACING_KEEP_MARKER, "unknown facing_mode.");
	PATTERN_REJECT_IF(!Math::is_finite(p.facing_offset_degrees), "facing_offset_degrees must be finite.");
	PATTERN_REJECT_IF(p.arm_index_stride < 1, "arm_index_stride must be >= 1.");
	return arm_spirals2d(caller, transforms_amount, marker_transform, p.arms, p.start_radius, p.radius_step, p.angle_step, p.rotate_with_marker, p.facing_mode, p.facing_offset_degrees, p.arm_index_stride, p.mirror_alternate_arms);
}

PatternSlots2D BulletPatterns2D::generate_corridor2d(int transforms_amount, Transform2D marker_transform, const CorridorParams2D &p) {
	const char *caller = "helper_generate_transforms_corridor";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(!p.aim_direction.is_finite() || p.aim_direction.length_squared() <= 0.0, "aim_direction must be finite and non-zero.");
	PATTERN_REJECT_IF(!Math::is_finite(p.width) || p.width < 0.0, "width must be finite and >= 0.");
	PATTERN_REJECT_IF(!Math::is_finite(p.spacing) || p.spacing <= 0.0, "spacing must be finite and > 0.");
	PATTERN_REJECT_IF(!Math::is_finite(p.gap_width) || p.gap_width < 0.0, "gap_width must be finite and >= 0.");
	PATTERN_REJECT_IF(!Math::is_finite(p.facing_offset_degrees), "facing_offset_degrees must be finite.");
	PATTERN_REJECT_IF(p.gap_width >= p.width, "gap_width eats the whole wall (must be < width).");
	PatternSlots2D generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const Vector2 axis = p.aim_direction.normalized();
	const Vector2 across = axis.orthogonal();
	const real_t facing_offset = Math::deg_to_rad(p.facing_offset_degrees);
	const real_t aim_angle = axis.angle();
	// Two equal wall segments flank the door: [-W/2, -G/2] and [G/2, W/2].
	// The amount is split between them (left takes the odd bullet) and each
	// segment spans outer edge to door edge, so exactly transforms_amount
	// bullets are placed and none lands in the door.
	const real_t half_w = p.width * 0.5;
	const real_t half_g = p.gap_width * 0.5;
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
		if (!p.face_aim) {
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
	danmaku_clamp_slots_finite(caller, generated_transforms);
	return generated_transforms;
}

} // namespace BlastBullets2D
