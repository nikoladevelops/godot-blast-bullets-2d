// Spawner randomization: per-bullet scale / rotation / position / speed rolls
// at shot time, seeded spin start phase and the timing ranges (shoot interval,
// burst interval, telegraph). Rolls never touch the pattern layer: the preview,
// the bake cache and collect_spawn_transforms() stay deterministic.

#include "bullet_spawner/bullet_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

namespace {
// Roll channels: each knob draws from its own stream, so switching one on
// never shifts what another rolls for the same seed.
enum RollChannel : uint32_t {
	CHANNEL_SCALE_X = 1,
	CHANNEL_SCALE_Y = 2,
	CHANNEL_ROTATION = 3,
	CHANNEL_POSITION_ANGLE = 4,
	CHANNEL_POSITION_RADIUS = 5,
	CHANNEL_SPEED = 6,
	CHANNEL_SPIN = 7,
};

inline uint64_t mix64(uint64_t x) {
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

inline double lerp_range(double a, double b, double t) {
	const double lo = a < b ? a : b;
	const double hi = a < b ? b : a;
	return lo + (hi - lo) * t;
}
} // namespace

// ---- Setters: finite / range only, never against the other end of a pair ----

#define BLAST_RANDOM_SETTER(NAME, CONDITION, TEXT)                                                                          \
	void BulletSpawner2D::set_##NAME(double value) {                                                                        \
		if (!Math::is_finite(value) || !(CONDITION)) {                                                                      \
			UtilityFunctions::push_error("BulletSpawner2D: " #NAME " must be finite and " TEXT ", keeping the old value."); \
			return;                                                                                                         \
		}                                                                                                                   \
		NAME = value;                                                                                                       \
	}

BLAST_RANDOM_SETTER(bullet_scale_min, value > 0.0, "> 0")
BLAST_RANDOM_SETTER(bullet_scale_max, value > 0.0, "> 0")
BLAST_RANDOM_SETTER(bullet_rotation_min_degrees, true, "a number")
BLAST_RANDOM_SETTER(bullet_rotation_max_degrees, true, "a number")
BLAST_RANDOM_SETTER(bullet_position_jitter_min, value >= 0.0, ">= 0")
BLAST_RANDOM_SETTER(bullet_position_jitter_max, value >= 0.0, ">= 0")
BLAST_RANDOM_SETTER(bullet_speed_scale_min, value >= 0.0, ">= 0")
BLAST_RANDOM_SETTER(bullet_speed_scale_max, value >= 0.0, ">= 0")
BLAST_RANDOM_SETTER(shoot_interval_max_sec, value >= 0.0, ">= 0 (0 = off)")
BLAST_RANDOM_SETTER(burst_interval_max_sec, value >= 0.0, ">= 0 (0 = off)")
BLAST_RANDOM_SETTER(telegraph_max_sec, value >= 0.0, ">= 0 (0 = off)")

#undef BLAST_RANDOM_SETTER

void BulletSpawner2D::set_bullet_scale_mode(int value) {
	if (value < BULLET_SCALE_OFF || value > BULLET_SCALE_PER_AXIS) {
		UtilityFunctions::push_error("BulletSpawner2D: bullet_scale_mode must be 0 (Off), 1 (Uniform) or 2 (Per Axis), keeping the old value.");
		return;
	}
	bullet_scale_mode = value;
	notify_property_list_changed(); // the range knobs gate on it
}

static bool scale_xy_ok(const Vector2 &v) {
	return Math::is_finite(v.x) && Math::is_finite(v.y) && v.x > 0.0 && v.y > 0.0;
}

void BulletSpawner2D::set_bullet_scale_min_xy(const Vector2 &value) {
	if (!scale_xy_ok(value)) {
		UtilityFunctions::push_error("BulletSpawner2D: bullet_scale_min_xy must be finite with both components > 0, keeping the old value.");
		return;
	}
	bullet_scale_min_xy = value;
}

void BulletSpawner2D::set_bullet_scale_max_xy(const Vector2 &value) {
	if (!scale_xy_ok(value)) {
		UtilityFunctions::push_error("BulletSpawner2D: bullet_scale_max_xy must be finite with both components > 0, keeping the old value.");
		return;
	}
	bullet_scale_max_xy = value;
}

Array BulletSpawner2D::convert_live_bullets_to_pickups(const Ref<BulletPickupData2D> &pickup, const Variant &target) {
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		UtilityFunctions::push_error("BulletSpawner2D.convert_live_bullets_to_pickups: the spawner has no BulletFactory2D, nothing converted.");
		return Array();
	}
	BulletFactory2D::PickupSelection selection;
	selection.kind = BulletFactory2D::PickupSelection::OWNER;
	selection.owner_spawner_id = get_instance_id();
	return factory->convert_to_pickups_impl("BulletSpawner2D.convert_live_bullets_to_pickups", pickup, target, selection);
}

// ---- Rolls ----------------------------------------------------------------------------

// [0, 1). Seeded: a pure function of (seed, shots fired, channel, index), no
// state and no allocation. Unseeded: Godot's global RNG, so a game's seed()
// still reproduces it.
double BulletSpawner2D::roll_unit(uint32_t channel, uint64_t index) const {
	if (bullet_random_seed == 0) {
		return UtilityFunctions::randf();
	}
	uint64_t h = mix64((uint64_t)(int64_t)bullet_random_seed);
	h = mix64(h ^ ((uint64_t)volleys_fired * 0xD6E8FEB86659FD93ull));
	h = mix64(h ^ (((uint64_t)channel << 40) + index));
	return (double)(h >> 11) * (1.0 / 9007199254740992.0);
}

bool BulletSpawner2D::randomization_active() const {
	return bullet_scale_mode != BULLET_SCALE_OFF || bullet_rotation_min_degrees != 0.0 || bullet_rotation_max_degrees != 0.0 || bullet_position_jitter_max > 0.0 || bullet_position_jitter_min > 0.0 || bullet_speed_scale_min != 1.0 || bullet_speed_scale_max != 1.0;
}

void BulletSpawner2D::randomize_shot_transforms(std::vector<Transform2D> &transforms) {
	const bool scale_uniform = bullet_scale_mode == BULLET_SCALE_UNIFORM;
	const bool scale_axes = bullet_scale_mode == BULLET_SCALE_PER_AXIS;
	const bool rotate = bullet_rotation_min_degrees != 0.0 || bullet_rotation_max_degrees != 0.0;
	const bool jitter = bullet_position_jitter_max > 0.0 || bullet_position_jitter_min > 0.0;
	for (size_t i = 0; i < transforms.size(); ++i) {
		Transform2D &t = transforms[i];
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
	}
}

void BulletSpawner2D::roll_speed_factors(size_t count) {
	shot_speed_factors.clear();
	if (bullet_speed_scale_min == 1.0 && bullet_speed_scale_max == 1.0) {
		return;
	}
	shot_speed_factors.resize(count);
	for (size_t i = 0; i < count; ++i) {
		shot_speed_factors[i] = (real_t)lerp_range(bullet_speed_scale_min, bullet_speed_scale_max, roll_unit(CHANNEL_SPEED, (uint64_t)i));
	}
}

// ---- Timing ranges --------------------------------------------------------------------

double BulletSpawner2D::roll_timing_range(double base, double max_value, uint64_t channel) const {
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
	} else if (!jitter_rng_randomized) {
		rng->randomize();
		jitter_rng_randomized = true;
	}
	return rng->randf_range(base, max_value);
}

double BulletSpawner2D::next_burst_interval_sec() const {
	return roll_timing_range(burst_interval_sec, burst_interval_max_sec, 2);
}

double BulletSpawner2D::next_telegraph_sec() const {
	return roll_timing_range(telegraph_sec, telegraph_max_sec, 3);
}

// ---- Spin start phase -----------------------------------------------------------------

void BulletSpawner2D::roll_spin_start_phase() {
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
