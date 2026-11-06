#pragma once

#include <godot_cpp/core/math.hpp>

namespace BlastBullets2D {
using namespace godot;

// HOW a spawner's shot frame turns over time (the Spin group): Continuous
// turns at a signed speed forever, Oscillate swings +-amplitude at a
// frequency. Holds the settings and the runtime angle and phase clock
// (never stored); the angle is what every shot and the preview turn the
// pattern by. Settings are trusted here: the spawner validates them (its
// error texts name its class).
struct SpawnerSpin2D {
	// Serialized ids (PatternSpawner2D.spin_mode): never renumber.
	enum Mode : int {
		CONTINUOUS = 0,
		OSCILLATE = 1,
	};

	bool enabled = false;
	double speed_deg_per_sec = 90.0;
	int mode = CONTINUOUS;
	double amplitude_deg = 45.0;
	double frequency_hz = 0.5;
	bool random_start_phase = false;

	// Runtime state: the angle shots use (degrees) and the oscillation clock.
	double angle_deg = 0.0;
	double time_sec = 0.0;

	// Advances the angle by delta seconds of the spawner's clock.
	void advance(double delta) {
		if (mode == OSCILLATE) {
			// Wrap the phase clock modulo the oscillation period: an unbounded
			// float clock loses sin() precision after ~12h of spinning (visible
			// stutter). Period = 1/frequency; non-positive frequency holds still.
			if (frequency_hz > 0.0 && Math::is_finite(frequency_hz)) {
				const double period = 1.0 / frequency_hz;
				time_sec = Math::fposmod(time_sec + delta, period);
			} else {
				time_sec += delta;
			}
			const double angle = amplitude_deg * Math::sin(Math::TAU * frequency_hz * time_sec);
			if (Math::is_finite(angle) && Math::is_finite(time_sec)) {
				angle_deg = angle;
			} else {
				time_sec = 0.0; // an overflowed clock restarts (never a NaN angle for good)
			}
		} else {
			// Any finite time_scale is accepted: a step that overflows holds the
			// angle (fposmod of INF is NaN, and NaN would stick forever).
			const double step = speed_deg_per_sec * delta;
			if (Math::is_finite(step)) {
				angle_deg = Math::fposmod(angle_deg + step, 360.0);
			}
		}
	}

	// Back to the start: angle and clock 0.
	void reset() {
		angle_deg = 0.0;
		time_sec = 0.0;
	}

	// Starts at a point of the turn: `unit` in [0, 1) of a full turn
	// (Continuous) or of the oscillation period (Oscillate; a frequency of 0
	// keeps the start).
	void start_at_phase(double unit) {
		if (mode == OSCILLATE) {
			if (frequency_hz > 0.0 && Math::is_finite(frequency_hz)) {
				time_sec = unit / frequency_hz; // a point inside the period
				angle_deg = amplitude_deg * Math::sin(Math::TAU * frequency_hz * time_sec);
			}
			return;
		}
		angle_deg = 360.0 * unit;
	}
};

} // namespace BlastBullets2D
