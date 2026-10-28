// Spawner randomization: per-bullet scale / rotation / position / speed rolls
// at shot time, seeded spin start phase and the timing ranges (shoot interval,
// burst interval, telegraph). Rolls never touch the pattern layer: the preview,
// the bake cache and collect_spawn_transforms() stay deterministic.

#include "bullet_spawner/pattern_spawner2d_internal.hpp"
#include "core/transform_math2d.hpp"

using namespace godot;

namespace BlastBullets2D {

// ---- Setters: finite / range only, never against the other end of a pair ----

#define BLAST_RANDOM_SETTER(NAME, FIELD, CONDITION, TEXT)                                                                          \
	void PatternSpawner2D::set_##NAME(double value) {                                                                              \
		if (!Math::is_finite(value) || !(CONDITION)) {                                                                             \
			UtilityFunctions::push_error(String(get_class()) + ": " #NAME " must be finite and " TEXT ", keeping the old value."); \
			return;                                                                                                                \
		}                                                                                                                          \
		FIELD = value;                                                                                                             \
	}

BLAST_RANDOM_SETTER(bullet_scale_min, placement.scale_min, value > 0.0, "> 0")
BLAST_RANDOM_SETTER(bullet_scale_max, placement.scale_max, value > 0.0, "> 0")
BLAST_RANDOM_SETTER(bullet_rotation_min_degrees, placement.rotation_min_degrees, true, "a number")
BLAST_RANDOM_SETTER(bullet_rotation_max_degrees, placement.rotation_max_degrees, true, "a number")
BLAST_RANDOM_SETTER(bullet_position_jitter_min, placement.position_jitter_min, value >= 0.0, ">= 0")
BLAST_RANDOM_SETTER(bullet_position_jitter_max, placement.position_jitter_max, value >= 0.0, ">= 0")
// Timing maxima feed RandomNumberGenerator.randf_range, which takes floats:
// past FLT_MAX the wait became INF and the spawner stalled for good.
BLAST_RANDOM_SETTER(burst_interval_max_sec, cadence.burst_interval_max_sec, is_finite_real2d(value) && value >= 0.0, ">= 0 (0 = off)")
BLAST_RANDOM_SETTER(telegraph_max_sec, cadence.telegraph_max_sec, is_finite_real2d(value) && value >= 0.0, ">= 0 (0 = off)")

#undef BLAST_RANDOM_SETTER

void PatternSpawner2D::set_shoot_interval_max_sec(double value) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error(String(get_class()) + ": shoot_interval_max_sec must be finite and >= 0 (0 = off), keeping the old value.");
		return;
	}
	cadence.shoot_interval_max_sec = value;
	clamp_shoot_wait_to_settings(); // a lower maximum cuts a longer rolled wait
}

void PatternSpawner2D::set_bullet_scale_mode(BulletScaleMode value) {
	if (value < BULLET_SCALE_OFF || value > BULLET_SCALE_PER_AXIS) {
		UtilityFunctions::push_error((String(get_class()) + ": bullet_scale_mode must be 0 (Off), 1 (Uniform) or 2 (Per Axis), keeping the old value."));
		return;
	}
	placement.scale_mode = value;
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
	placement.scale_min_xy = value;
}

void PatternSpawner2D::set_bullet_scale_max_xy(const Vector2 &value) {
	if (!scale_xy_ok(value)) {
		UtilityFunctions::push_error((String(get_class()) + ": bullet_scale_max_xy must be finite with both components > 0, keeping the old value."));
		return;
	}
	placement.scale_max_xy = value;
}

// ---- Rolls ----------------------------------------------------------------------------

// [0, 1) of this shot (the next volley's number), bullet `index`, `channel`:
// ShotRandomizer2D's rolls (seeded: a pure hash; seed 0: Godot's global RNG).
double PatternSpawner2D::roll_unit(uint32_t channel, uint64_t index) const {
	return placement.roll_unit((uint64_t)cadence.volleys_fired, channel, index);
}

bool PatternSpawner2D::randomization_active() const {
	return placement.active();
}

void PatternSpawner2D::randomize_shot_transforms(std::vector<Transform2D> &transforms) {
	placement.apply(transforms, (uint64_t)cadence.volleys_fired);
}

// ---- Timing ranges --------------------------------------------------------------------

double PatternSpawner2D::roll_timing_range(double base, double max_value, uint64_t channel) const {
	return cadence.roll_range(base, max_value, channel);
}

double PatternSpawner2D::next_burst_interval_sec() const {
	return cadence.next_burst_interval();
}

double PatternSpawner2D::next_telegraph_sec() const {
	return cadence.next_telegraph();
}

// ---- Spin start phase -----------------------------------------------------------------

void PatternSpawner2D::roll_spin_start_phase() {
	spin.start_at_phase(roll_unit(CHANNEL_SPIN, 0));
}

} // namespace BlastBullets2D
