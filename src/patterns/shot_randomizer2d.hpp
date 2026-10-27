#pragma once

#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstdint>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

// Per-bullet placement rolls applied at shot time on top of a pattern:
// scale (uniform or per axis), rotation and position jitter. Seeded rolls are
// a pure hash of (seed, shot, channel, bullet index): channels never share
// numbers, switching one knob on never shifts another's rolls, and a shot is
// reproducible from its number alone. Seed 0 draws from Godot's global RNG
// (a game's seed() still reproduces it). The spawner's Randomization group
// holds one; scripts reach the same rolls through
// BulletPatterns2D.randomize_transforms. Settings are trusted here: the
// owners validate them.
struct ShotRandomizer2D {
	// Serialized ids (BulletSpawner2D.bullet_scale_mode): never renumber.
	enum ScaleMode : int {
		SCALE_OFF = 0,
		SCALE_UNIFORM = 1,
		SCALE_PER_AXIS = 2,
	};
	// Each knob rolls its own stream.
	enum Channel : uint32_t {
		CHANNEL_SCALE_X = 1,
		CHANNEL_SCALE_Y = 2,
		CHANNEL_ROTATION = 3,
		CHANNEL_POSITION_ANGLE = 4,
		CHANNEL_POSITION_RADIUS = 5,
		CHANNEL_SPEED = 6,
		CHANNEL_SPIN = 7,
		CHANNEL_COLOR = 8,
		CHANNEL_LIFE_TIME = 9,
	};

	int scale_mode = SCALE_OFF;
	double scale_min = 1.0;
	double scale_max = 1.0;
	Vector2 scale_min_xy = Vector2(1, 1);
	Vector2 scale_max_xy = Vector2(1, 1);
	double rotation_min_degrees = 0.0;
	double rotation_max_degrees = 0.0;
	double position_jitter_min = 0.0;
	double position_jitter_max = 0.0;
	int64_t seed = 0; // 0 = Godot's global RNG

	// [0, 1) for bullet `index` of shot `shot` on `channel`.
	double roll_unit(uint64_t shot, uint32_t channel, uint64_t index) const;
	// Any placement roll on (an all-off randomizer draws nothing).
	bool active() const;
	// Rolls every transform of shot `shot` in place. A roll that would
	// overflow float32 keeps the pattern's own pose.
	void apply(Transform2D *transforms, size_t count, uint64_t shot) const;
	void apply(std::vector<Transform2D> &transforms, uint64_t shot) const {
		apply(transforms.data(), transforms.size(), shot);
	}
};

// a..b at t in [0, 1), whichever way round the pair is (scene loading sets
// a minimum before its maximum: pairs are never validated against each other).
inline double shot_roll_lerp(double a, double b, double t) {
	const double lo = a < b ? a : b;
	const double hi = a < b ? b : a;
	return lo + (hi - lo) * t;
}

// The seeded hash under every roll family (SplitMix64's finalizer).
inline uint64_t shot_roll_mix64(uint64_t x) {
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

} // namespace BlastBullets2D
