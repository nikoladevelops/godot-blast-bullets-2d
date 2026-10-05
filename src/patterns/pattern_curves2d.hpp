#pragma once

// Parametric curves shared by the pattern generators and their preview
// samplers: ONE expression per curve, so the drawn track is the curve the
// bullets sit on (invariant 4; pinned for every source by
// spawner/test_spawner_preview_track_coincidence.gd). Each function is the
// generator's expression verbatim (same casts, same order), so moving the
// generators onto it left their output bit-identical.

#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/vector2.hpp"

namespace BlastBullets2D {
using namespace godot;

// Heart: x = 16 sin^3 t, y = 13 cos t - 5 cos 2t - 2 cos 3t - cos 4t, with
// screen y down (the lobes point up). Unrotated, scaled by `scale`.
static inline Vector2 heart_point2d(real_t t, real_t scale) {
	const real_t hx = (real_t)16.0 * Math::pow((double)Math::sin(t), 3.0);
	const real_t hy = 13.0 * Math::cos(t) - 5.0 * Math::cos(2.0 * t) - 2.0 * Math::cos(3.0 * t) - Math::cos(4.0 * t);
	return Vector2(hx, -hy) * scale;
}

// Rose r = cos(k theta)^p (signed): closes after half a turn for odd k (k
// petals) and a full turn for even k (2k petals). Sweeping a full turn for
// odd k traces every petal twice.
static inline real_t rose_sweep2d(int petals) {
	return (petals % 2 == 1) ? (real_t)Math::PI : (real_t)Math::TAU;
}

// Signed rose radius at theta; r_cos_k reports the lobe sign (the petal axis
// flips by pi where cos(k theta) < 0).
static inline real_t rose_radius2d(int petals, real_t theta, real_t radius, real_t lobe_sharpness, real_t &r_cos_k) {
	r_cos_k = Math::cos((real_t)petals * theta);
	const real_t mag = Math::pow((double)Math::abs(r_cos_k), (double)lobe_sharpness);
	return radius * ((r_cos_k >= 0.0) ? (real_t)mag : -(real_t)mag);
}

// Lissajous point at parameter t.
static inline Vector2 lissajous_point2d(real_t size_x, real_t size_y, real_t freq_x, real_t freq_y, real_t phase, real_t t) {
	return Vector2(size_x * Math::sin(freq_x * t + phase), size_y * Math::sin(freq_y * t));
}

// Flower FAN: the waist pinch between lobes at frac (-0.5..0.5 within a
// petal); higher sharpness reads as a tighter flower.
static inline real_t flower_fan_waist2d(real_t petal_sharpness, real_t frac) {
	return 1.0 - (petal_sharpness / (1.0 + petal_sharpness)) * 0.55 * Math::abs(Math::sin(frac * Math::PI));
}

// Flower RHODONEA: r = R * inner + R * (1 - inner) * |cos(k theta / 2)|^p.
static inline real_t flower_rhodonea_radius2d(int petals, real_t theta, real_t radius, real_t sharp, double inner_radius_scale) {
	const real_t inner_keep = (real_t)(1.0 - inner_radius_scale);
	const real_t cos_k = Math::cos((real_t)petals * theta * 0.5);
	const real_t mag = Math::pow((double)Math::abs(cos_k), (double)sharp);
	return radius * (real_t)inner_radius_scale + radius * inner_keep * (real_t)mag;
}

// Flower SPIROGRAPH (hypotrochoid x = (R-r)cos t + d cos(k t),
// y = (R-r)sin t - d sin(k t), k = (R-r)/r). Wild rollers are clamped so
// huge values cannot NaN the loop; a roller equal to R pins the centre and
// the pen draws a circle of radius d instead of a single point.
struct Spirograph2D {
	double diff = 0.0;
	double k = 0.0;
	double pen = 0.0;
	bool centre_pinned = false;
};

static inline Spirograph2D spirograph_setup2d(real_t radius, double spiro_roller, double spiro_pen) {
	Spirograph2D s;
	const double outer_r = (double)radius;
	double roller = spiro_roller;
	if (roller < 1.0) {
		roller = 1.0;
	}
	if (roller > Math::max(outer_r * 4.0, 512.0)) {
		roller = Math::max(outer_r * 4.0, 512.0);
	}
	s.diff = outer_r - roller;
	s.k = s.diff / roller;
	s.pen = spiro_pen;
	s.centre_pinned = Math::abs(s.diff) < 1e-6 * Math::max(outer_r, 1.0);
	return s;
}

// Unrotated hypotrochoid point at t.
static inline Vector2 spirograph_point2d(const Spirograph2D &s, double t) {
	double px = s.diff * Math::cos(t) + s.pen * Math::cos(s.k * t);
	double py = s.diff * Math::sin(t) - s.pen * Math::sin(s.k * t);
	if (s.centre_pinned) {
		px = s.pen * Math::cos(t);
		py = -s.pen * Math::sin(t);
	}
	return Vector2((real_t)px, (real_t)py);
}

// Flower SUPERFORMULA: simplified Gielis (a = b = 1, n2 = n3 = fullness),
// r = (|cos(m t / 4)|^f + |sin(m t / 4)|^f)^(-1/f), normalized radius in
// (0, 4]; lobes and fullness are clamped like the generator always did.
static inline double superformula_norm2d(double super_lobes, double super_fullness, double t) {
	const double lobes = Math::clamp(super_lobes, 2.0, 64.0);
	const double full = Math::clamp(super_fullness, 0.05, 8.0);
	const double c = Math::abs(Math::cos(lobes * t * 0.25));
	const double s = Math::abs(Math::sin(lobes * t * 0.25));
	double r_norm = Math::pow(Math::pow(c, full) + Math::pow(s, full), -1.0 / full);
	if (!Math::is_finite(r_norm) || r_norm <= 0.0) {
		r_norm = 1.0;
	}
	if (r_norm > 4.0) {
		r_norm = 4.0;
	}
	return r_norm;
}

// Lissajous sweep: the parameter window [t0, t0 + span] that traces the
// figure exactly once, and whether the figure is an open run (defined in
// patterns_curves.cpp).
void lissajous_sweep2d(double fx, double fy, double phase, double &r_t0, double &r_span, bool &r_open);

} //namespace BlastBullets2D
