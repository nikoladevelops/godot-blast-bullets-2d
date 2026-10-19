#pragma once

#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/vector2.hpp"

namespace BlastBullets2D {
using namespace godot;

// Central transform-invertibility check. get_scale().length_squared()
// alone accepts singular bases like scale (0,1) (det == 0) whose
// affine_inverse() is garbage. Every conversion/validation path (volleys,
// patterns, the spawner) uses this one definition.
// Finite as a double AND as real_t: a finite double above FLT_MAX turns into
// INF when narrowed (float builds), so setters feeding real_t math refuse it
// up front instead of failing at every shot or every bullet.
inline bool is_finite_real2d(double v) {
	return Math::is_finite(v) && Math::is_finite((real_t)v);
}

// Same for values handed to the engine as 32-bit floats whatever real_t is
// (AudioStreamPlayer2D volume, distance, attenuation, panning).
inline bool is_finite_float2d(double v) {
	return Math::is_finite(v) && Math::is_finite((float)v);
}

inline bool is_transform_invertible_safe2d(const Transform2D &t) {
	if (!t.is_finite()) {
		return false;
	}
	const Vector2 x = t.columns[0];
	const Vector2 y = t.columns[1];
	if (!x.is_finite() || !y.is_finite()) {
		return false;
	}
	const real_t det = x.x * y.y - x.y * y.x;
	return Math::is_finite(det) && Math::abs(det) > (real_t)1e-8;
}

// A pose that is safe to hand to the physics server and the MultiMesh: every
// component finite, the basis columns' squares and the determinant finite
// (float32 overflows to infinity around 1.8e19 per component) and the basis
// not singular. Used where a setting multiplies a pose (spawner scale rolls,
// pickup scale) so a huge value falls back instead of corrupting a volley.
inline bool transform_is_sane2d(const Transform2D &t) {
	for (int c = 0; c < 3; ++c) {
		if (!Math::is_finite(t.columns[c].x) || !Math::is_finite(t.columns[c].y)) {
			return false;
		}
	}
	const real_t x_sq = t.columns[0].x * t.columns[0].x + t.columns[0].y * t.columns[0].y;
	const real_t y_sq = t.columns[1].x * t.columns[1].x + t.columns[1].y * t.columns[1].y;
	const real_t det = t.columns[0].x * t.columns[1].y - t.columns[0].y * t.columns[1].x;
	return Math::is_finite(x_sq) && Math::is_finite(y_sq) && Math::is_finite(det) && det != 0.0f;
}

} //namespace BlastBullets2D
