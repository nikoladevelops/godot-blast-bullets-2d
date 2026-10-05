#pragma once

// Pattern posing: turns a raw pattern (bake output) into the shot's
// transforms. One folded spin + pattern-scale matrix about the generator
// origin (exact for mirrored and sheared bases), the skip-index mask
// (negative space), then the per-bullet basis scale. Pure functions; the
// spawner's shot path and its preview share them, so dots always sit where
// bullets spawn.

#include "godot_cpp/variant/packed_int32_array.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include <cstdint>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

struct PatternPose2D {
	// Identity case for the folded matrix: no spin and unit scale.
	static bool spin_is_trivial(real_t radians, real_t scale) {
		return radians == 0.0 && scale == 1.0f;
	}

	// Spin + pattern scale folded into ONE matrix about the generator origin.
	// Rotating per bullet would recompute the shared trig N times and,
	// rebuilt from get_rotation(), silently destroy mirroring (negative
	// determinant) and shear. M = S(s)·R(r) as a plain multiply is exact for
	// every determinant: ~18 flops/bullet, zero trig, zero sqrt. r_valid is
	// false (identity returned) for non-finite input.
	static Transform2D make_spin_scale_matrix(const Vector2 &origin, real_t radians, real_t scale, bool &r_valid);

	// Builds the skip mask (1 = drop) for `count` slots. Returns whether any
	// slot is skipped; r_out_of_range reports indexes outside 0..count-1
	// (ignored).
	static bool build_skip_mask(const PackedInt32Array &skip_indices, int count, std::vector<uint8_t> &r_mask, bool &r_out_of_range);

	// raw -> posed: spin/scale matrix (when use_matrix), skip mask (when
	// non-null), per-bullet basis scale (columns only; origins untouched, so
	// skipped indexes still match the preview).
	static void pose(const std::vector<Transform2D> &raw, const Transform2D &spin_scale, bool use_matrix, const std::vector<uint8_t> *skip_mask, real_t local_factor, std::vector<Transform2D> &r_out);
};

} //namespace BlastBullets2D
