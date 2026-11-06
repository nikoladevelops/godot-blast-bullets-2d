#pragma once

// Plugin-owned generator (PCG32) for cosmetic rolls only: sound mix, effect
// layer starts and scales. Godot's global RNG stays with gameplay (bounce
// jitter, orbit random, seed-0 patterns), so audio or camera culling can never
// shift what a seeded game rolls. One process-wide state, main thread only.

#include <cstdint>

namespace BlastBullets2D {

struct CosmeticRng2D {
	// Fixed start (never time-seeded): an unseeded run is still reproducible.
	static uint64_t &state() {
		static uint64_t s = 0x853c49e6748fea9bULL;
		return s;
	}

	static void seed(uint64_t value) {
		state() = value * 6364136223846793005ULL + 1442695040888963407ULL;
	}

	static uint32_t next_u32() {
		const uint64_t old = state();
		state() = old * 6364136223846793005ULL + 1442695040888963407ULL;
		const uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
		const uint32_t rot = (uint32_t)(old >> 59u);
		return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
	}

	// Same contracts as UtilityFunctions::randi / randf / randf_range.
	static int64_t randi() {
		return (int64_t)next_u32();
	}

	static double randf() {
		return (double)next_u32() * (1.0 / 4294967296.0);
	}

	static double randf_range(double from, double to) {
		return from + (to - from) * randf();
	}
};

} // namespace BlastBullets2D
