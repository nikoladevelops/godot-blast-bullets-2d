#pragma once

#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <vector>

namespace BlastBullets2D {
using namespace godot;

// The distance from `p` to the nearest point of the polyline `pts` (INF for
// fewer than two points).
inline real_t distance_to_polyline2d(const Vector2 &p, const std::vector<Vector2> &pts) {
	real_t best = Math::INF;
	for (size_t k = 1; k < pts.size(); ++k) {
		const Vector2 a = pts[k - 1];
		const Vector2 ab = pts[k] - a;
		const real_t len2 = ab.x * ab.x + ab.y * ab.y;
		real_t t = len2 > (real_t)0.0 ? ((p - a).x * ab.x + (p - a).y * ab.y) / len2 : (real_t)0.0;
		t = Math::clamp(t, (real_t)0.0, (real_t)1.0);
		const Vector2 q = a + ab * t;
		best = Math::min(best, p.distance_to(q));
	}
	return best;
}

// The facing of the polyline segment nearest to `at`.
// The box a laser's contact query tests for the segment a -> c (`seg` long,
// > 0) of a beam `half_width` wide: centered on the segment, turned along it,
// half extents (seg / 2, half_width). Shared by the query and the debugger
// overlay.
//
// Returns false when the box cannot be represented as finite real_t, so both
// callers skip the segment instead of handing the physics server or the drawn
// overlay a non-finite rect: a finite but huge pair of beam points (1e38 px
// away, 3e38 px long) makes the real_t length INF and the real_t center
// overflow, and the axis, the angle, the center and the extents would come
// out NaN/INF. The fallback recomputes the same box in double first.
inline bool laser_query_box2d(const Vector2 &a, const Vector2 &c, real_t seg, real_t half_width, Transform2D &r_pose, Vector2 &r_extents) {
	if (!Math::is_finite(half_width) || !(half_width > (real_t)0.0)) {
		return false;
	}
	// The shared path keeps the exact expressions the query always used.
	if (Math::is_finite(seg) && seg > (real_t)0.0 && (a + c).is_finite() && (c - a).is_finite()) {
		const Vector2 axis = (c - a) / seg;
		r_pose = Transform2D((real_t)axis.angle(), (a + c) * (real_t)0.5);
		r_extents = Vector2(seg * (real_t)0.5, half_width);
		if (r_pose.is_finite() && r_extents.is_finite()) {
			return true;
		}
	}
	const double dx = (double)c.x - (double)a.x;
	const double dy = (double)c.y - (double)a.y;
	const double length = Math::sqrt(dx * dx + dy * dy);
	const double center_x = ((double)a.x + (double)c.x) * 0.5;
	const double center_y = ((double)a.y + (double)c.y) * 0.5;
	if (!(length > 0.0) || !Math::is_finite(length)) {
		return false;
	}
	r_pose = Transform2D((real_t)Math::atan2(dy, dx), Vector2((real_t)center_x, (real_t)center_y));
	r_extents = Vector2((real_t)(length * 0.5), half_width);
	return r_pose.is_finite() && r_extents.is_finite();
}

// The half width every contact query of a beam `width` wide uses.
inline real_t laser_query_half_width2d(real_t width) {
	return Math::max((real_t)0.5, width * (real_t)0.5);
}

inline real_t polyline_angle_at2d(const std::vector<Vector2> &pts, const Vector2 &at) {
	real_t best = -1.0;
	real_t angle = 0.0;
	for (size_t k = 1; k < pts.size(); ++k) {
		const Vector2 a = pts[k - 1];
		const Vector2 ab = pts[k] - a;
		const real_t len2 = ab.x * ab.x + ab.y * ab.y;
		if (!(len2 > (real_t)0.0)) {
			continue;
		}
		const real_t t = Math::clamp(((at - a).x * ab.x + (at - a).y * ab.y) / len2, (real_t)0.0, (real_t)1.0);
		const real_t dist = (a + ab * t).distance_squared_to(at);
		if (best < (real_t)0.0 || dist < best) {
			best = dist;
			angle = ab.angle();
		}
	}
	return angle;
}

// Whether the segment a-b touches the rectangle `r` (normalized), edges
// included (Liang-Barsky clipping).
inline bool segment_touches_rect2d(const Vector2 &a, const Vector2 &b, const Rect2 &r) {
	real_t t0 = 0.0;
	real_t t1 = 1.0;
	const Vector2 d = b - a;
	const real_t p[4] = { -d.x, d.x, -d.y, d.y };
	const real_t q[4] = { a.x - r.position.x, r.position.x + r.size.x - a.x, a.y - r.position.y, r.position.y + r.size.y - a.y };
	for (int i = 0; i < 4; ++i) {
		if (p[i] == (real_t)0.0) {
			if (q[i] < (real_t)0.0) {
				return false; // parallel to this edge and outside it
			}
			continue;
		}
		const real_t t = q[i] / p[i];
		if (p[i] < (real_t)0.0) {
			if (t > t1) {
				return false;
			}
			t0 = Math::max(t0, t);
		} else {
			if (t < t0) {
				return false;
			}
			t1 = Math::min(t1, t);
		}
	}
	return true;
}

} // namespace BlastBullets2D
