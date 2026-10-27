#include "patterns/shot_randomizer2d.hpp"

#include "core/transform_math2d.hpp"

#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace BlastBullets2D {

double ShotRandomizer2D::roll_unit(uint64_t shot, uint32_t channel, uint64_t index) const {
	if (seed == 0) {
		return UtilityFunctions::randf();
	}
	uint64_t h = shot_roll_mix64((uint64_t)seed);
	h = shot_roll_mix64(h ^ (shot * 0xD6E8FEB86659FD93ull));
	h = shot_roll_mix64(h ^ (((uint64_t)channel << 40) + index));
	return (double)(h >> 11) * (1.0 / 9007199254740992.0);
}

bool ShotRandomizer2D::active() const {
	return scale_mode != SCALE_OFF || rotation_min_degrees != 0.0 || rotation_max_degrees != 0.0 || position_jitter_max > 0.0 || position_jitter_min > 0.0;
}

void ShotRandomizer2D::apply(Transform2D *transforms, size_t count, uint64_t shot) const {
	const bool scale_uniform = scale_mode == SCALE_UNIFORM;
	const bool scale_axes = scale_mode == SCALE_PER_AXIS;
	const bool rotate = rotation_min_degrees != 0.0 || rotation_max_degrees != 0.0;
	const bool jitter = position_jitter_max > 0.0 || position_jitter_min > 0.0;
	for (size_t i = 0; i < count; ++i) {
		Transform2D &t = transforms[i];
		const Transform2D before_rolls = t;
		const uint64_t index = (uint64_t)i;
		if (scale_uniform) {
			const real_t s = (real_t)shot_roll_lerp(scale_min, scale_max, roll_unit(shot, CHANNEL_SCALE_X, index));
			t.columns[0] *= s;
			t.columns[1] *= s;
		} else if (scale_axes) {
			const real_t sx = (real_t)shot_roll_lerp(scale_min_xy.x, scale_max_xy.x, roll_unit(shot, CHANNEL_SCALE_X, index));
			const real_t sy = (real_t)shot_roll_lerp(scale_min_xy.y, scale_max_xy.y, roll_unit(shot, CHANNEL_SCALE_Y, index));
			t.columns[0] *= sx;
			t.columns[1] *= sy;
		}
		if (rotate) {
			const real_t angle = (real_t)Math::deg_to_rad(shot_roll_lerp(rotation_min_degrees, rotation_max_degrees, roll_unit(shot, CHANNEL_ROTATION, index)));
			t = t * Transform2D(angle, Vector2());
		}
		if (jitter) {
			// Uniform over the annulus' area, not its radius (no clustering at the centre).
			const double lo = position_jitter_min < position_jitter_max ? position_jitter_min : position_jitter_max;
			const double hi = position_jitter_min < position_jitter_max ? position_jitter_max : position_jitter_min;
			const double radius = Math::sqrt(shot_roll_lerp(lo * lo, hi * hi, roll_unit(shot, CHANNEL_POSITION_RADIUS, index)));
			const double theta = Math::TAU * roll_unit(shot, CHANNEL_POSITION_ANGLE, index);
			t.columns[2] += Vector2((real_t)(Math::cos(theta) * radius), (real_t)(Math::sin(theta) * radius));
		}
		// Settings so large that float32 overflows must never reach the physics
		// server: such a bullet keeps the pattern's own pose.
		if (!transform_is_sane2d(t)) {
			t = before_rolls;
		}
	}
}

} // namespace BlastBullets2D
