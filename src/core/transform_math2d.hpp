#pragma once

#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/aabb.hpp"
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

// Fixed bounds for every multimesh the plugin draws (bullets, effect
// shards, debugger shapes). A 2D canvas item culls by the bounds it had when
// drawn, and instances move every tick: a box wider than any game world means
// nothing is ever culled by stale bounds (the old +-100000 px box made bullets
// and effects vanish past 100k px; the debugger had none and lost its shapes).
inline AABB never_culled_aabb2d() {
	constexpr real_t extent = (real_t)1.0e12;
	return AABB(Vector3(-extent, -extent, -1.0f), Vector3(2.0f * extent, 2.0f * extent, 2.0f));
}

// An angle wrapped into [-PI, PI) in constant time. Subtracting TAU in a loop
// never ends once TAU is below half the angle's ULP (~1e8 rad as a float, a
// huge spin or node rotation); a non-finite angle becomes 0.
inline double wrap_angle_pi2d(double angle) {
	if (!Math::is_finite(angle)) {
		return 0.0;
	}
	if (Math::abs(angle) <= 64.0 * Math::TAU) {
		// Ordinary angles: the classic loop (at most 64 steps), so results
		// stay bit-identical to the code this replaced.
		while (angle > Math::PI) {
			angle -= Math::TAU;
		}
		while (angle < -Math::PI) {
			angle += Math::TAU;
		}
		return angle;
	}
	return Math::fposmod(angle + Math::PI, Math::TAU) - Math::PI;
}

// v.normalized() that survives a vector whose squared length overflows a
// float (|v| ~1e20: normalized() divides by sqrt(INF) and returns (0, 0), a
// bullet then stalls for good). Ordinary vectors take exactly normalized()
// (bit-identical); only the overflowing ones are scaled down first. Zero or
// non-finite in, (0, 0) out.
inline Vector2 safe_normalized2d(const Vector2 &v) {
	const real_t l2 = v.x * v.x + v.y * v.y;
	if (Math::is_finite(l2)) {
		return l2 > (real_t)0.0 ? v.normalized() : Vector2();
	}
	const real_t big = MAX(Math::abs(v.x), Math::abs(v.y));
	if (!Math::is_finite(big) || !(big > (real_t)0.0)) {
		return Vector2();
	}
	return Vector2(v.x / big, v.y / big).normalized();
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
