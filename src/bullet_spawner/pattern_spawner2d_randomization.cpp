// Spawner randomization: per-bullet scale / rotation / position / speed rolls
// at shot time, seeded spin start phase and the timing ranges (shoot interval,
// burst interval, telegraph). Rolls never touch the pattern layer: the preview,
// the bake cache and collect_spawn_transforms() stay deterministic.

#include "bullet_spawner/pattern_spawner2d_internal.hpp"
#include "core/transform_math2d.hpp"

using namespace godot;

namespace BlastBullets2D {

// ---- Setters: finite / range only, never against the other end of a pair ----

#define BLAST_RANDOM_SETTER(NAME, CONDITION, TEXT)                                                                                 \
	void PatternSpawner2D::set_##NAME(double value) {                                                                              \
		if (!Math::is_finite(value) || !(CONDITION)) {                                                                             \
			UtilityFunctions::push_error(String(get_class()) + ": " #NAME " must be finite and " TEXT ", keeping the old value."); \
			return;                                                                                                                \
		}                                                                                                                          \
		NAME = value;                                                                                                              \
	}

BLAST_RANDOM_SETTER(bullet_scale_min, value > 0.0, "> 0")
BLAST_RANDOM_SETTER(bullet_scale_max, value > 0.0, "> 0")
BLAST_RANDOM_SETTER(bullet_rotation_min_degrees, true, "a number")
BLAST_RANDOM_SETTER(bullet_rotation_max_degrees, true, "a number")
BLAST_RANDOM_SETTER(bullet_position_jitter_min, value >= 0.0, ">= 0")
BLAST_RANDOM_SETTER(bullet_position_jitter_max, value >= 0.0, ">= 0")
// Timing maxima feed RandomNumberGenerator.randf_range, which takes floats:
// past FLT_MAX the wait became INF and the spawner stalled for good.
BLAST_RANDOM_SETTER(shoot_interval_max_sec, is_finite_real2d(value) && value >= 0.0, ">= 0 (0 = off)")
BLAST_RANDOM_SETTER(burst_interval_max_sec, is_finite_real2d(value) && value >= 0.0, ">= 0 (0 = off)")
BLAST_RANDOM_SETTER(telegraph_max_sec, is_finite_real2d(value) && value >= 0.0, ">= 0 (0 = off)")

#undef BLAST_RANDOM_SETTER

void PatternSpawner2D::set_bullet_scale_mode(BulletScaleMode value) {
	if (value < BULLET_SCALE_OFF || value > BULLET_SCALE_PER_AXIS) {
		UtilityFunctions::push_error((String(get_class()) + ": bullet_scale_mode must be 0 (Off), 1 (Uniform) or 2 (Per Axis), keeping the old value."));
		return;
	}
	bullet_scale_mode = value;
	notify_property_list_changed(); // the range knobs gate on it
}

static bool scale_xy_ok(const Vector2 &v) {
	return Math::is_finite(v.x) && Math::is_finite(v.y) && v.x > 0.0 && v.y > 0.0;
}

void PatternSpawner2D::set_bullet_scale_min_xy(const Vector2 &value) {
	if (!scale_xy_ok(value)) {
		UtilityFunctions::push_error((String(get_class()) + ": bullet_scale_min_xy must be finite with both components > 0, keeping the old value."));
		return;
	}
	bullet_scale_min_xy = value;
}

void PatternSpawner2D::set_bullet_scale_max_xy(const Vector2 &value) {
	if (!scale_xy_ok(value)) {
		UtilityFunctions::push_error((String(get_class()) + ": bullet_scale_max_xy must be finite with both components > 0, keeping the old value."));
		return;
	}
	bullet_scale_max_xy = value;
}

// ---- Rolls ----------------------------------------------------------------------------

// [0, 1). Seeded: a pure function of (seed, shots fired, channel, index), no
// state and no allocation. Unseeded: Godot's global RNG, so a game's seed()
// still reproduces it.
double PatternSpawner2D::roll_unit(uint32_t channel, uint64_t index) const {
	if (bullet_random_seed == 0) {
		return UtilityFunctions::randf();
	}
	uint64_t h = mix64((uint64_t)(int64_t)bullet_random_seed);
	h = mix64(h ^ ((uint64_t)volleys_fired * 0xD6E8FEB86659FD93ull));
	h = mix64(h ^ (((uint64_t)channel << 40) + index));
	return (double)(h >> 11) * (1.0 / 9007199254740992.0);
}

bool PatternSpawner2D::randomization_active() const {
	return bullet_scale_mode != BULLET_SCALE_OFF || bullet_rotation_min_degrees != 0.0 || bullet_rotation_max_degrees != 0.0 || bullet_position_jitter_max > 0.0 || bullet_position_jitter_min > 0.0;
}

void PatternSpawner2D::randomize_shot_transforms(std::vector<Transform2D> &transforms) {
	const bool scale_uniform = bullet_scale_mode == BULLET_SCALE_UNIFORM;
	const bool scale_axes = bullet_scale_mode == BULLET_SCALE_PER_AXIS;
	const bool rotate = bullet_rotation_min_degrees != 0.0 || bullet_rotation_max_degrees != 0.0;
	const bool jitter = bullet_position_jitter_max > 0.0 || bullet_position_jitter_min > 0.0;
	for (size_t i = 0; i < transforms.size(); ++i) {
		Transform2D &t = transforms[i];
		const Transform2D before_rolls = t;
		const uint64_t index = (uint64_t)i;
		if (scale_uniform) {
			const real_t s = (real_t)lerp_range(bullet_scale_min, bullet_scale_max, roll_unit(CHANNEL_SCALE_X, index));
			t.columns[0] *= s;
			t.columns[1] *= s;
		} else if (scale_axes) {
			const real_t sx = (real_t)lerp_range(bullet_scale_min_xy.x, bullet_scale_max_xy.x, roll_unit(CHANNEL_SCALE_X, index));
			const real_t sy = (real_t)lerp_range(bullet_scale_min_xy.y, bullet_scale_max_xy.y, roll_unit(CHANNEL_SCALE_Y, index));
			t.columns[0] *= sx;
			t.columns[1] *= sy;
		}
		if (rotate) {
			const real_t angle = (real_t)Math::deg_to_rad(lerp_range(bullet_rotation_min_degrees, bullet_rotation_max_degrees, roll_unit(CHANNEL_ROTATION, index)));
			t = t * Transform2D(angle, Vector2());
		}
		if (jitter) {
			// Uniform over the annulus' area, not its radius (no clustering at the centre).
			const double lo = bullet_position_jitter_min < bullet_position_jitter_max ? bullet_position_jitter_min : bullet_position_jitter_max;
			const double hi = bullet_position_jitter_min < bullet_position_jitter_max ? bullet_position_jitter_max : bullet_position_jitter_min;
			const double radius = Math::sqrt(lerp_range(lo * lo, hi * hi, roll_unit(CHANNEL_POSITION_RADIUS, index)));
			const double theta = Math::TAU * roll_unit(CHANNEL_POSITION_ANGLE, index);
			t.columns[2] += Vector2((real_t)(Math::cos(theta) * radius), (real_t)(Math::sin(theta) * radius));
		}
		// Settings so large that float32 overflows must never reach the physics
		// server: such a bullet keeps the pattern's own pose.
		if (!transform_is_sane2d(t)) {
			t = before_rolls;
		}
	}
}

// ---- Timing ranges --------------------------------------------------------------------

double PatternSpawner2D::roll_timing_range(double base, double max_value, uint64_t channel) const {
	if (!Math::is_finite(max_value) || !(max_value > base)) {
		return base;
	}
	Ref<RandomNumberGenerator> rng = jitter_rng;
	if (rng.is_null()) {
		rng.instantiate();
		jitter_rng = rng;
	}
	if (reload_jitter_seed != 0) {
		// Same family as the reload jitter: (seed, shot index) hashed, with the
		// channel folded in so the ranges never replay each other's stream.
		uint64_t h = (uint64_t)reload_jitter_seed * 0x9E3779B97F4A7C15ull + (uint64_t)volleys_fired + channel * 0x632BE59BD9B4E019ull;
		rng->set_seed(mix64(h));
		jitter_rng_randomized = false; // seed 0 later must not continue this stream
	} else if (!jitter_rng_randomized) {
		rng->randomize();
		jitter_rng_randomized = true;
	}
	return rng->randf_range(base, max_value);
}

double PatternSpawner2D::next_burst_interval_sec() const {
	return roll_timing_range(burst_interval_sec, burst_interval_max_sec, 2);
}

double PatternSpawner2D::next_telegraph_sec() const {
	return roll_timing_range(telegraph_sec, telegraph_max_sec, 3);
}

// ---- Spin start phase -----------------------------------------------------------------

void PatternSpawner2D::roll_spin_start_phase() {
	const double unit = roll_unit(CHANNEL_SPIN, 0);
	if (spin_mode == SPIN_OSCILLATE) {
		if (spin_frequency_hz > 0.0 && Math::is_finite(spin_frequency_hz)) {
			spin_time_sec = unit / spin_frequency_hz; // a point inside the period
			spin_angle_deg = spin_amplitude_deg * Math::sin(Math::TAU * spin_frequency_hz * spin_time_sec);
		}
		return;
	}
	spin_angle_deg = 360.0 * unit;
}

} // namespace BlastBullets2D
