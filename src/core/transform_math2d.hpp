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

} //namespace BlastBullets2D
