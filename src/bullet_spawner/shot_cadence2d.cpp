#include "bullet_spawner/shot_cadence2d.hpp"

#include "patterns/shot_randomizer2d.hpp"

namespace BlastBullets2D {

RandomNumberGenerator &ShotCadence2D::rng() const {
	if (jitter_rng.is_null()) {
		jitter_rng.instantiate();
	}
	return **jitter_rng;
}

double ShotCadence2D::roll_range(double base, double max_value, uint64_t channel) const {
	if (!Math::is_finite(max_value) || !(max_value > base)) {
		return base;
	}
	RandomNumberGenerator &r = rng();
	if (reload_jitter_seed != 0) {
		// Same family as the reload jitter: (seed, shot index) hashed, with the
		// channel folded in so the ranges never replay each other's stream.
		uint64_t h = (uint64_t)reload_jitter_seed * 0x9E3779B97F4A7C15ull + (uint64_t)volleys_fired + channel * 0x632BE59BD9B4E019ull;
		r.set_seed(shot_roll_mix64(h));
		jitter_rng_randomized = false; // seed 0 later must not continue this stream
	} else if (!jitter_rng_randomized) {
		r.randomize();
		jitter_rng_randomized = true;
	}
	return r.randf_range(base, max_value);
}

double ShotCadence2D::next_shoot_interval() const {
	// Reload jitter: +/- uniform jitter around the base interval (seeded by
	// reload_jitter_seed for replays; non-deterministic at 0). Floored so a huge
	// jitter can never invert or stall the timer; the floor sits at 1ms so
	// fast base intervals keep working with jitter on (the per-tick pull cap
	// in _process absorbs the rest instead of hot-looping).
	const double base_interval = roll_range(shoot_interval_sec, shoot_interval_max_sec, 1);
	if (reload_jitter_sec <= 0.0 || !Math::is_finite(reload_jitter_sec)) {
		return base_interval;
	}
	RandomNumberGenerator &r = rng();
	if (reload_jitter_seed != 0) {
		// Hash (seed, shot index): seed + index made consecutive seeds replay
		// one sequence a shot apart (SplitMix64 finalizer).
		uint64_t h = (uint64_t)reload_jitter_seed * 0x9E3779B97F4A7C15ull + (uint64_t)volleys_fired;
		h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
		h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
		r.set_seed(h ^ (h >> 31));
		jitter_rng_randomized = false; // seed 0 later must not continue this stream
	} else if (!jitter_rng_randomized) {
		r.randomize();
		jitter_rng_randomized = true;
	}
	const double jitter = r.randf_range(-reload_jitter_sec, reload_jitter_sec);
	return Math::max(0.001, base_interval + jitter);
}

} // namespace BlastBullets2D
