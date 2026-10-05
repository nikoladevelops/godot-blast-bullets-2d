// Polygon pattern generators (rectangle, polygon, triangle, trapezoid, diamond)
// and the symmetric polygon loop machinery (corner seats, corner facing).

#include "patterns/patterns_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Outward edge normal for the directed edge a -> b, oriented against ref
// (a corner-averaged normal): falls back to a zero vector when degenerate,
// letting callers substitute their own fallback.
Vector2 oriented_edge_normal(const Vector2 &a, const Vector2 &b, const Vector2 &ref) {
	const Vector2 seg = b - a;
	Vector2 edge_n = seg.length_squared() > 1e-12 ? seg.orthogonal().normalized() : Vector2(0, 0);
	if (edge_n.length_squared() > 1e-12 && ref.length_squared() > 1e-12 && edge_n.dot(ref) < 0.0) {
		edge_n = -edge_n;
	}
	return edge_n;
}

// Resolves which adjoining edge owns corner `c` (between incoming edge
// prev -> c and outgoing edge c -> next) under a corner priority, and returns
// that edge's outward normal (via oriented_edge_normal against ref).
// HORIZONTAL: the horizontal adjoining edge wins (ties go outgoing, i.e. the
// previous behavior); VERTICAL: the vertical one wins; BALANCED: always the
// outgoing edge (previous behavior exactly).
static Vector2 corner_owner_normal(const PackedVector2Array &corners, int c, const Vector2 &ref, int corner_priority) {
	const int n = corners.size();
	const Vector2 prev = corners[(c - 1 + n) % n];
	const Vector2 cur = corners[c];
	const Vector2 next = corners[(c + 1) % n];
	const bool in_h = edge_is_horizontal(prev, cur);
	const bool out_h = edge_is_horizontal(cur, next);
	int use_outgoing = 1;
	if (corner_priority == BlastBullets2D::BulletPatterns2D::OUTLINE_CORNER_PRIORITY_HORIZONTAL) {
		if (in_h && !out_h) {
			use_outgoing = 0;
		} else {
			use_outgoing = 1;
		}
	} else if (corner_priority == BlastBullets2D::BulletPatterns2D::OUTLINE_CORNER_PRIORITY_VERTICAL) {
		if (!in_h && out_h) {
			use_outgoing = 0;
		} else {
			use_outgoing = 1;
		}
	} else {
		use_outgoing = 1;
	}
	Vector2 edge_n;
	if (use_outgoing) {
		edge_n = oriented_edge_normal(cur, next, ref);
	} else {
		edge_n = oriented_edge_normal(prev, cur, ref);
	}
	if (edge_n.length_squared() <= 1e-12) {
		edge_n = ref;
	}
	return edge_n.length_squared() > 1e-12 ? edge_n.normalized() : Vector2(0, -1);
}

// Bisector (miter) normal of corner `c`: normalized sum of the two adjoining
// outward edge normals. Points along the corner's angle bisector (triangle
// apexes face UP, star tips read radial). Falls back to the averaged corner
// normal when the edges oppose (straight continuation) or degenerate.
Vector2 miter_normal(const PackedVector2Array &corners, int c, const Vector2 &ref) {
	const int n = corners.size();
	const Vector2 prev = corners[(c - 1 + n) % n];
	const Vector2 cur = corners[c];
	const Vector2 next = corners[(c + 1) % n];
	Vector2 n1 = oriented_edge_normal(prev, cur, ref);
	Vector2 n2 = oriented_edge_normal(cur, next, ref);
	if (n1.length_squared() <= 1e-12) {
		n1 = ref;
	}
	if (n2.length_squared() <= 1e-12) {
		n2 = ref;
	}
	Vector2 s = n1 + n2;
	if (s.length_squared() <= 1e-12) {
		s = ref;
	}
	return s.length_squared() > 1e-12 ? s.normalized() : Vector2(0, -1);
}

// Corner-dot facing resolver shared by every corner-anchored loop builder.
// SIDE keeps the priority owner's edge normal (stable default); MITER faces
// the bisector; SMOOTH uses the averaged loop normal at the corner.
static Vector2 resolve_corner_facing(const PackedVector2Array &corners, int c, const Vector2 &ref, int corner_priority, int corner_facing) {
	if (corner_facing == BlastBullets2D::BulletPatterns2D::OUTLINE_CORNER_FACING_MITER) {
		return miter_normal(corners, c, ref);
	}
	if (corner_facing == BlastBullets2D::BulletPatterns2D::OUTLINE_CORNER_FACING_SMOOTH) {
		return ref.length_squared() > 1e-12 ? ref.normalized() : Vector2(0, -1);
	}
	return corner_owner_normal(corners, c, ref, corner_priority);
}

// Symmetric largest-remainder apportionment of (count - corners) interior
// slots across polygon edges by length, so every corner always carries a
// bullet and the rest spread proportionally with opposite sides kept equal.
// distribution: 0 = legacy winding-order tie-break (first edges win), 1 =
// symmetric (opposite pairs share leftovers; a single odd leftover breaks
// one pair by exactly one, which is unavoidable). Degenerate loops yield
// all zeros.
static void apportion_polygon_slots(const PackedVector2Array &corners, int count, PackedInt32Array &r_interior, int distribution = 1) {
	const int n = corners.size();
	r_interior.clear();
	if (n <= 0) {
		return;
	}
	r_interior.resize(n);
	for (int e = 0; e < n; ++e) {
		r_interior[e] = 0;
	}
	const int rest = count - n;
	if (rest <= 0) {
		return;
	}
	double total = 0.0;
	for (int e = 0; e < n; ++e) {
		const double len = (double)corners[e].distance_to(corners[(e + 1) % n]);
		if (Math::is_finite(len) && len > 0.0) {
			total += len;
		}
	}
	if (!(total > 0.0) || !Math::is_finite(total)) {
		return;
	}
	PackedFloat64Array frac;
	frac.resize(n);
	int assigned = 0;
	for (int e = 0; e < n; ++e) {
		const double exact = (double)rest * (double)corners[e].distance_to(corners[(e + 1) % n]) / total;
		// Snap near-integers: single-precision lengths measured in global
		// coordinates can round exact integer shares (e.g. 1.0 on a regular
		// star) to 0.9999999, which would floor a slot away and deal it
		// elsewhere. Anything within 1e-4 of an integer is float noise.
		int base = 0;
		if (exact > 0.0 && Math::is_finite(exact)) {
			const double snapped = Math::round(exact);
			if (Math::abs(exact - snapped) < 0.0001) {
				base = (int)snapped;
			} else {
				base = (int)Math::floor(exact);
			}
		}
		r_interior[e] = base;
		frac[e] = exact - (double)base;
		assigned += base;
	}
	int left = rest - assigned;
	if (distribution == 0) {
		while (left-- > 0) {
			int best = 0;
			for (int e = 1; e < n; ++e) {
				if (frac[e] > frac[best]) {
					best = e;
				}
			}
			r_interior[best] = r_interior[best] + 1;
			frac[best] = -1.0;
		}
		return;
	}
	// Symmetric: hand out leftovers in opposite-pair rounds so opposite
	// sides stay equal whenever the count allows it. Edges are paired as
	// (e, e + n/2) for even n; for odd n there are no true opposites, so
	// leftovers spread in maximally-spaced winding order instead.
	if (n % 2 == 0) {
		const int half = n / 2;
		// Pair score = mean fractional remainder of the pair; pairs with the
		// largest claim go first, keeping both halves of the figure in step.
		while (left > 0) {
			int best_pair = -1;
			double best_score = -1.0;
			for (int e = 0; e < half; ++e) {
				const int o = e + half;
				// Skip spent pairs (both halves already topped up this round).
				if (frac[e] < 0.0 && frac[o] < 0.0) {
					continue;
				}
				const double score = (MAX(frac[e], 0.0) + MAX(frac[o], 0.0)) * 0.5;
				if (score > best_score) {
					best_score = score;
					best_pair = e;
				}
			}
			if (best_pair < 0) {
				break;
			}
			const int o = best_pair + half;
			if (left >= 2) {
				r_interior[best_pair] = r_interior[best_pair] + 1;
				r_interior[o] = r_interior[o] + 1;
				frac[best_pair] = -1.0;
				frac[o] = -1.0;
				left -= 2;
			} else {
				// Odd leftover: one pair must break by exactly one. Give it
				// to the longer half of the best pair so spacing stays closest.
				const int pick = (frac[o] > frac[best_pair]) ? o : best_pair;
				r_interior[pick] = r_interior[pick] + 1;
				frac[pick] = -1.0;
				left -= 1;
			}
		}
		if (left > 0) {
			// Pair rounds exhausted but leftovers remain (rounding): fall
			// back to single largest-remainder for the tail.
			while (left-- > 0) {
				int best = 0;
				for (int e = 1; e < n; ++e) {
					if (frac[e] > frac[best]) {
						best = e;
					}
				}
				r_interior[best] = r_interior[best] + 1;
				frac[best] = -1.0;
			}
		}
		return;
	}
	// Odd edge count: no opposite pairs exist. Spread the tail in
	// maximally-spaced order (Bresenham-style stride) instead of clumping
	// on the first edges, so the figure keeps rotational balance.
	PackedInt32Array order;
	order.resize(n);
	for (int e = 0; e < n; ++e) {
		order[e] = e;
	}
	// Insertion sort by frac desc (stable: ties keep winding order).
	for (int i = 1; i < n; ++i) {
		const int key = order[i];
		const double key_f = frac[key];
		int j = i - 1;
		while (j >= 0 && frac[order[j]] < key_f) {
			order[j + 1] = order[j];
			--j;
		}
		order[j + 1] = key;
	}
	// Take the top `left` in spaced order: stride by n/left to avoid runs.
	if (left > 0 && left < n) {
		const int stride = n / left;
		int at = 0;
		for (int k = 0; k < left; ++k) {
			const int pick = order[at % n];
			r_interior[pick] = r_interior[pick] + 1;
			frac[pick] = -1.0;
			at += (stride > 0 ? stride : 1);
		}
		return;
	}
	while (left-- > 0) {
		int best = 0;
		for (int e = 1; e < n; ++e) {
			if (frac[e] > frac[best]) {
				best = e;
			}
		}
		r_interior[best] = r_interior[best] + 1;
		frac[best] = -1.0;
	}
}

// Even corner seats for small counts (count <= corner count): picks evenly
// spaced corners including corner 0, so 2 bullets on a square land on
// opposite corners and 5 bullets on a 5-point star land on the 5 outer tips.
// Returns corner indices in winding order starting at corner 0.
static PackedInt32Array even_corner_seats(int corner_count, int count) {
	PackedInt32Array seats;
	if (corner_count <= 0 || count <= 0) {
		return seats;
	}
	if (count >= corner_count) {
		seats.resize(corner_count);
		for (int i = 0; i < corner_count; ++i) {
			seats[i] = i;
		}
		return seats;
	}
	for (int i = 0; i < count; ++i) {
		const int c = (int)Math::round((double)i * (double)corner_count / (double)count);
		const int cc = ((c % corner_count) + corner_count) % corner_count;
		if (!seats.has(cc)) {
			seats.push_back(cc);
		}
	}
	// Rounding collisions (e.g. count close to corner_count) must still
	// yield exactly `count` distinct seats: fill forward from 0.
	for (int k = 0; k < corner_count && seats.size() < count; ++k) {
		if (!seats.has(k)) {
			seats.push_back(k);
		}
	}
	seats.sort();
	return seats;
}

// Shared corner-anchored polygon loop builder: every corner carries a slot
// and interiors spread per edge via apportion_polygon_slots, each slot riding
// its edge-constant outward normal (built from corners + averaged corner
// normals). count <= corners seats evenly spaced corners. corner_priority
// picks which adjoining edge owns a shared corner dot (HORIZONTAL default:
// top/bottom own it; VERTICAL: left/right; BALANCED: outgoing edge, i.e. the
// previous behavior). corner_mode EVEN_ARC skips corner pinning and spreads
// purely evenly by arc length from corner 0. edge_margin keeps interior dots
// at least that many px away from corners along their edge (clamped per
// edge). Returns false when degenerate (caller stacks at the marker).
bool build_symmetric_polygon_loop(const PackedVector2Array &corners, const PackedVector2Array &corner_normals, int count, const CornerLayout2D &corner, PackedVector2Array &r_points, PackedVector2Array &r_normals) {
	// Locals: out-of-range values are clamped below.
	const int distribution = corner.outline_distribution;
	int corner_priority = corner.outline_corner_priority;
	const int corner_mode = corner.outline_corner_mode;
	double edge_margin = corner.outline_edge_margin;
	int corner_facing = corner.outline_corner_facing;
	const int n = corners.size();
	r_points.clear();
	r_normals.clear();
	if (n < 3 || count <= 0 || corner_normals.size() != n) {
		return false;
	}
	if (corner_priority < 0 || corner_priority > 2) {
		corner_priority = 0;
	}
	if (corner_facing < 0 || corner_facing > 2) {
		corner_facing = 0;
	}
	if (edge_margin < 0.0 || !Math::is_finite(edge_margin)) {
		edge_margin = 0.0;
	}
	if (corner_mode == BlastBullets2D::BulletPatterns2D::OUTLINE_CORNER_MODE_EVEN_ARC) {
		// Pure arc-length walk from corner 0: uniform gaps everywhere, corners
		// coincide only when the count aligns (no pinning distortion).
		PackedFloat64Array cum;
		cum.resize(n + 1);
		cum[0] = 0.0;
		for (int e = 0; e < n; ++e) {
			const double seg = (double)corners[e].distance_to(corners[(e + 1) % n]);
			cum[e + 1] = cum[e] + (Math::is_finite(seg) && seg > 0.0 ? seg : 0.0);
		}
		const double total = cum[n];
		if (!(total > 0.0) || !Math::is_finite(total)) {
			return false;
		}
		const double step = total / (double)count;
		int seg = 0;
		for (int i = 0; i < count; ++i) {
			double d = step * (double)i;
			while (seg < n - 1 && d >= cum[seg + 1]) {
				++seg;
			}
			const double seg_len = cum[seg + 1] - cum[seg];
			double t = (seg_len > 1e-9) ? (d - cum[seg]) / seg_len : 0.0;
			t = Math::clamp(t, 0.0, 1.0);
			const int ia = seg % n;
			const int ib = (seg + 1) % n;
			const Vector2 ref = corner_normals[ia];
			Vector2 edge_n = oriented_edge_normal(corners[ia], corners[ib], ref);
			if (edge_n.length_squared() <= 1e-12) {
				edge_n = ref;
			}
			if (corner_facing == BlastBullets2D::BulletPatterns2D::OUTLINE_CORNER_FACING_SMOOTH) {
				Vector2 sn = corner_normals[ia].lerp(corner_normals[ib], (real_t)t);
				edge_n = sn.length_squared() > 1e-12 ? sn.normalized() : edge_n;
			}
			// A sample landing exactly on a corner inherits the resolved
			// corner facing (priority owner / miter / smooth) so facings
			// never guess.
			if (t <= 1e-9) {
				edge_n = resolve_corner_facing(corners, ia, ref, corner_priority, corner_facing);
			}
			r_points.push_back(corners[ia].lerp(corners[ib], (real_t)t));
			r_normals.push_back(edge_n.length_squared() > 1e-12 ? edge_n.normalized() : Vector2(0, -1));
		}
		return r_points.size() == count && r_normals.size() == count;
	}
	if (count <= n) {
		const PackedInt32Array seats = even_corner_seats(n, count);
		for (int s = 0; s < seats.size(); ++s) {
			const int c = seats[s];
			const Vector2 ref = corner_normals[c];
			const Vector2 edge_n = resolve_corner_facing(corners, c, ref, corner_priority, corner_facing);
			r_points.push_back(corners[c]);
			r_normals.push_back(edge_n);
		}
		return r_points.size() == count;
	}
	PackedInt32Array interior;
	apportion_polygon_slots(corners, count, interior, distribution);
	int made = 0;
	for (int e = 0; e < n && made < count; ++e) {
		const int ia = e % n;
		const int ib = (e + 1) % n;
		Vector2 edge_n = oriented_edge_normal(corners[ia], corners[ib], corner_normals[ia]);
		if (edge_n.length_squared() <= 1e-12) {
			edge_n = corner_normals[ia];
		}
		edge_n = edge_n.length_squared() > 1e-12 ? edge_n.normalized() : Vector2(0, -1);
		// Corner dot faces with its resolved facing (priority owner / miter /
		// smooth), not blindly outgoing.
		const Vector2 corner_n = resolve_corner_facing(corners, ia, corner_normals[ia], corner_priority, corner_facing);
		r_points.push_back(corners[ia]);
		r_normals.push_back(corner_n);
		++made;
		const double edge_len = (double)corners[ia].distance_to(corners[ib]);
		const double eff_margin = (edge_len > 0.0 && Math::is_finite(edge_len)) ? MIN(edge_margin, edge_len * 0.5) : 0.0;
		for (int m = 0; m < interior[e] && made < count; ++m) {
			double t = (double)(m + 1) / (double)(interior[e] + 1);
			if (eff_margin > 0.0 && edge_len > 0.0) {
				t = (eff_margin + (edge_len - 2.0 * eff_margin) * (double)(m + 1) / (double)(interior[e] + 1)) / edge_len;
				t = Math::clamp(t, 0.0, 1.0);
			}
			r_points.push_back(corners[ia].lerp(corners[ib], (real_t)t));
			if (corner_facing == BlastBullets2D::BulletPatterns2D::OUTLINE_CORNER_FACING_SMOOTH) {
				Vector2 sn = corner_normals[ia].lerp(corner_normals[ib], (real_t)t);
				r_normals.push_back(sn.length_squared() > 1e-12 ? sn.normalized() : edge_n);
			} else {
				r_normals.push_back(edge_n);
			}
			++made;
		}
	}
	while (made < count) {
		r_points.push_back(corners[0]);
		r_normals.push_back(corner_normals[0].length_squared() > 1e-12 ? corner_normals[0].normalized() : Vector2(0, -1));
		++made;
	}
	return r_points.size() == count && r_normals.size() == count;
}

// Signed shoelace area (> 0 matches the rectangle primitive winding, which
// the shared edge normals read as outward). Non-finite corners yield NaN.
static double polygon_signed_area(const PackedVector2Array &corners) {
	double area = 0.0;
	const int n = corners.size();
	for (int k = 0; k < n; ++k) {
		const Vector2 &a = corners[k];
		const Vector2 &b = corners[(k + 1) % n];
		area += (double)a.x * (double)b.y - (double)b.x * (double)a.y;
	}
	return area * 0.5;
}

// Marker-local corner builders shared by the polygon primitives below and
// their preview samplers (single source of truth: the track can never drift
// from the volley). All windings come out rectangle-positive (outward edge
// normals); rotation spins the finished corners.
PackedVector2Array build_triangle_corners(int triangle_type, real_t size_a, real_t size_b, real_t rotation) {
	PackedVector2Array corners;
	const real_t rot_cos = Math::cos(rotation);
	const real_t rot_sin = Math::sin(rotation);
	auto spin = [&](const Vector2 &c) -> Vector2 {
		return Vector2(c.x * rot_cos - c.y * rot_sin, c.x * rot_sin + c.y * rot_cos);
	};
	if (triangle_type == BulletPatterns2D::TRIANGLE_EQUILATERAL) {
		for (int k = 0; k < 3; ++k) {
			const real_t a = -Math::PI * 0.5 + Math::TAU * (real_t)k / 3.0;
			corners.push_back(spin(Vector2(Math::cos(a), Math::sin(a)) * size_a));
		}
	} else if (triangle_type == BulletPatterns2D::TRIANGLE_ISOSCELES) {
		corners.push_back(spin(Vector2(0.0, -size_b * 0.5)));
		corners.push_back(spin(Vector2(size_a * 0.5, size_b * 0.5)));
		corners.push_back(spin(Vector2(-size_a * 0.5, size_b * 0.5)));
	} else {
		const Vector2 raw[3] = { Vector2(0, 0), Vector2(size_a, 0), Vector2(0, size_b) };
		const Vector2 centroid = (raw[0] + raw[1] + raw[2]) / 3.0;
		for (int k = 0; k < 3; ++k) {
			corners.push_back(spin(raw[k] - centroid));
		}
	}
	if (corners.size() == 3 && Math::is_finite(polygon_signed_area(corners)) && polygon_signed_area(corners) < 0.0) {
		const Vector2 tmp = corners[1];
		corners[1] = corners[2];
		corners[2] = tmp;
	}
	return corners;
}

PackedVector2Array build_trapezoid_corners(real_t base_top, real_t base_bottom, real_t height, real_t rotation) {
	PackedVector2Array corners;
	const real_t rot_cos = Math::cos(rotation);
	const real_t rot_sin = Math::sin(rotation);
	auto spin = [&](const Vector2 &c) -> Vector2 {
		return Vector2(c.x * rot_cos - c.y * rot_sin, c.x * rot_sin + c.y * rot_cos);
	};
	corners.push_back(spin(Vector2(-base_top * 0.5, -height * 0.5)));
	corners.push_back(spin(Vector2(base_top * 0.5, -height * 0.5)));
	corners.push_back(spin(Vector2(base_bottom * 0.5, height * 0.5)));
	corners.push_back(spin(Vector2(-base_bottom * 0.5, height * 0.5)));
	if (corners.size() == 4 && Math::is_finite(polygon_signed_area(corners)) && polygon_signed_area(corners) < 0.0) {
		const Vector2 tmp = corners[1];
		corners[1] = corners[3];
		corners[3] = tmp;
	}
	return corners;
}

PackedVector2Array build_diamond_corners(real_t diagonal_x, real_t diagonal_y, real_t rotation) {
	PackedVector2Array corners;
	const real_t rot_cos = Math::cos(rotation);
	const real_t rot_sin = Math::sin(rotation);
	auto spin = [&](const Vector2 &c) -> Vector2 {
		return Vector2(c.x * rot_cos - c.y * rot_sin, c.x * rot_sin + c.y * rot_cos);
	};
	corners.push_back(spin(Vector2(0.0, -diagonal_y * 0.5)));
	corners.push_back(spin(Vector2(diagonal_x * 0.5, 0.0)));
	corners.push_back(spin(Vector2(0.0, diagonal_y * 0.5)));
	corners.push_back(spin(Vector2(-diagonal_x * 0.5, 0.0)));
	if (corners.size() == 4 && Math::is_finite(polygon_signed_area(corners)) && polygon_signed_area(corners) < 0.0) {
		const Vector2 tmp = corners[1];
		corners[1] = corners[3];
		corners[3] = tmp;
	}
	return corners;
}

// Closed-polygon outline sampler shared by the triangle/trapezoid/diamond
// primitives: delegates to build_symmetric_polygon_loop (corner ownership,
// mode and margin honored). Corners must wind like the rectangle primitive
// (positive shoelace area); callers orientation-fix first. False when
// degenerate (caller stacks at the marker, rectangle precedent).
static bool sample_closed_polygon_loop(const PackedVector2Array &corners, int count, const CornerLayout2D &corner, PackedVector2Array &r_points, PackedVector2Array &r_normals) {
	const int n = corners.size();
	if (n < 3 || count <= 0) {
		return false;
	}
	PackedVector2Array normals;
	if (!compute_edge_normals_quiet(corners, true, false, normals) || normals.size() != n) {
		return false;
	}
	double total = 0.0;
	for (int k = 0; k < n; ++k) {
		const double seg = (double)corners[k].distance_to(corners[(k + 1) % n]);
		if (!Math::is_finite(seg) || seg < 0.0) {
			return false;
		}
		total += seg;
	}
	if (!(total > 0.0) || !Math::is_finite(total)) {
		return false;
	}
	r_points.clear();
	r_normals.clear();
	return build_symmetric_polygon_loop(corners, normals, count, corner, r_points, r_normals);
}

// Every slot stacked at the marker facing its rotation (+ offset): what a
// zero-size / zero-area shape emits instead of garbage.
static PatternSlots2D stack_at_marker2d(int transforms_amount, const Transform2D &marker_transform, real_t facing_offset_degrees) {
	PatternSlots2D stacked = danmaku_make_slots(transforms_amount);
	for (int i = 0; i < transforms_amount; ++i) {
		Transform2D slot(marker_transform.get_rotation() + Math::deg_to_rad(facing_offset_degrees), marker_transform.get_origin());
		danmaku_apply_marker_scale(slot, marker_transform);
		stacked[i] = slot;
	}
	return stacked;
}

// Marker-local corners -> sampled closed loop -> outline layout. A degenerate
// (zero-area) polygon stacks at the marker (stack_at_marker2d).
static PatternSlots2D layout_polygon_corners2d(const char *caller, int transforms_amount, const Transform2D &marker_transform, const PackedVector2Array &corners, bool face_outward, real_t facing_offset_degrees, const OutlineLayout2D &outline, const CornerLayout2D &corner) {
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	if (!sample_closed_polygon_loop(corners, transforms_amount, corner, loop_points, loop_normals)) {
		return stack_at_marker2d(transforms_amount, marker_transform, facing_offset_degrees);
	}
	return layout_outline_slots(caller, marker_transform, loop_points, loop_normals, true, marker_transform.get_rotation(), face_outward, facing_offset_degrees, PackedFloat32Array(), outline, corner, corners, true, true);
}

PatternSlots2D BulletPatterns2D::generate_rectangle2d(int transforms_amount, Transform2D marker_transform, const RectangleParams2D &p) {
	const char *caller = "helper_generate_transforms_rectangle";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(!p.size.is_finite() || p.size.x < 0.0 || p.size.y < 0.0 || !Math::is_finite(p.facing_offset_degrees), "size must be finite with sides >= 0, facing_offset_degrees finite.");
	PATTERN_REQUIRE(pattern_check_corner_layout(caller, p.corner, p.outline.layer_layout));
	// Counter-clockwise outline from top-left; corners double as normals via
	// the shared edge worker so joints face clean diagonals. Normals stay
	// geometric here (the outline worker applies the face_outward flip).
	const Vector2 hw(p.size.x * 0.5, p.size.y * 0.5);
	PackedVector2Array corners;
	corners.push_back(Vector2(-hw.x, -hw.y));
	corners.push_back(Vector2(hw.x, -hw.y));
	corners.push_back(Vector2(hw.x, hw.y));
	corners.push_back(Vector2(-hw.x, hw.y));
	PackedVector2Array normals;
	PATTERN_REJECT_IF(!compute_edge_normals_quiet(corners, true, false, normals), "degenerate rectangle.");
	const real_t total = 2.0 * (p.size.x + p.size.y);
	const real_t marker_rot = marker_transform.get_rotation();
	if (!(total > 0.0)) {
		// Zero-size box: every slot stacks at the marker facing outward.
		return stack_at_marker2d(transforms_amount, marker_transform, p.facing_offset_degrees);
	}
	// Corner-anchored symmetric walk over top, right, bottom, left: every
	// corner always carries a bullet (squares read as squares at any count)
	// and the rest spread per edge by length with opposite sides kept equal;
	// small counts seat evenly spaced corners (2 bullets = opposite corners).
	// The shared outline worker assembles facings (marker_rot rides as
	// rot_add). All slots ride edge-constant normals, so bullet 0 faces
	// straight along the top edge.
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	// Zero bullets is a valid empty request (every other generator returns
	// [] silently); only a real geometry failure below is "degenerate".
	if (transforms_amount == 0) {
		return PatternSlots2D();
	}
	PATTERN_REJECT_IF(!build_symmetric_polygon_loop(corners, normals, transforms_amount, p.corner, loop_points, loop_normals), "degenerate rectangle.");
	return layout_outline_slots(caller, marker_transform, loop_points, loop_normals, true, marker_rot, p.face_outward, p.facing_offset_degrees, PackedFloat32Array(), p.outline, p.corner, corners, true, true);
}

PatternSlots2D BulletPatterns2D::generate_polygon2d(int transforms_amount, Transform2D marker_transform, const PolygonParams2D &p) {
	const char *caller = "helper_generate_transforms_polygon";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(p.vertices < 3 || !Math::is_finite(p.radius) || p.radius < 0.0 || !Math::is_finite(p.base_rotation) || !Math::is_finite(p.facing_offset_degrees), "vertices must be >= 3, radius finite and >= 0, rotations finite.");
	// The corner loop below builds `vertices` corners: cap it like star's
	// points so hostile input can't hang the game.
	PATTERN_REJECT_IF(p.vertices > HELPER_MAX_TRANSFORMS, "vertices is absurdly large; keep it near the bullet count.");
	PATTERN_REQUIRE(pattern_check_corner_layout(caller, p.corner, p.outline.layer_layout));
	PackedVector2Array corners;
	for (int k = 0; k < p.vertices; ++k) {
		const real_t a = p.base_rotation + Math::TAU * (real_t)k / (real_t)p.vertices;
		corners.push_back(Vector2(Math::cos(a), Math::sin(a)) * p.radius);
	}
	PackedVector2Array normals;
	PATTERN_REJECT_IF(!compute_edge_normals_quiet(corners, true, false, normals), "degenerate polygon.");
	// Arc-length walk so slots spread evenly even on stretched shapes; the
	// shared outline worker assembles facings (marker_rot rides as rot_add).
	// Normals stay geometric here (the worker applies the face_outward flip).
	real_t total = 0.0;
	for (int k = 0; k < p.vertices; ++k) {
		total += corners[k].distance_to(corners[(k + 1) % p.vertices]);
	}
	const real_t marker_rot = marker_transform.get_rotation();
	if (!(total > 0.0) || !Math::is_finite(total)) {
		return stack_at_marker2d(transforms_amount, marker_transform, p.facing_offset_degrees);
	}
	// Corner-anchored symmetric walk: every corner carries a slot, small
	// counts seat evenly spaced corners, opposite sides stay equal.
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	// Zero bullets is a valid empty request (every other generator returns
	// [] silently); only a real geometry failure below is "degenerate".
	if (transforms_amount == 0) {
		return PatternSlots2D();
	}
	PATTERN_REJECT_IF(!build_symmetric_polygon_loop(corners, normals, transforms_amount, p.corner, loop_points, loop_normals), "degenerate polygon.");
	return layout_outline_slots(caller, marker_transform, loop_points, loop_normals, true, marker_rot, p.face_outward, p.facing_offset_degrees, PackedFloat32Array(), p.outline, p.corner, corners, true, true);
}

PatternSlots2D BulletPatterns2D::generate_triangle2d(int transforms_amount, Transform2D marker_transform, const TriangleParams2D &p) {
	const char *caller = "helper_generate_transforms_triangle";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(p.triangle_type < TRIANGLE_EQUILATERAL || p.triangle_type > TRIANGLE_RIGHT, "unknown triangle_type.");
	PATTERN_REJECT_IF(!Math::is_finite(p.size_a) || p.size_a < 0.0 || !Math::is_finite(p.size_b) || p.size_b < 0.0 || !Math::is_finite(p.rotation) || !Math::is_finite(p.facing_offset_degrees), "size_a/size_b must be finite and >= 0, rotation and facing_offset_degrees finite.");
	PATTERN_REQUIRE(pattern_check_corner_layout(caller, p.corner, p.outline.layer_layout));
	PackedVector2Array corners = build_triangle_corners(p.triangle_type, p.size_a, p.size_b, p.rotation);
	return layout_polygon_corners2d(caller, transforms_amount, marker_transform, corners, p.face_outward, p.facing_offset_degrees, p.outline, p.corner);
}

PatternSlots2D BulletPatterns2D::generate_trapezoid2d(int transforms_amount, Transform2D marker_transform, const TrapezoidParams2D &p) {
	const char *caller = "helper_generate_transforms_trapezoid";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(!Math::is_finite(p.base_top) || p.base_top < 0.0 || !Math::is_finite(p.base_bottom) || p.base_bottom < 0.0 || !Math::is_finite(p.height) || p.height < 0.0 || !Math::is_finite(p.rotation) || !Math::is_finite(p.facing_offset_degrees), "bases and height must be finite and >= 0, rotation and facing_offset_degrees finite.");
	PATTERN_REQUIRE(pattern_check_corner_layout(caller, p.corner, p.outline.layer_layout));
	PackedVector2Array corners = build_trapezoid_corners(p.base_top, p.base_bottom, p.height, p.rotation);
	return layout_polygon_corners2d(caller, transforms_amount, marker_transform, corners, p.face_outward, p.facing_offset_degrees, p.outline, p.corner);
}

PatternSlots2D BulletPatterns2D::generate_diamond2d(int transforms_amount, Transform2D marker_transform, const DiamondParams2D &p) {
	const char *caller = "helper_generate_transforms_diamond";

	PATTERN_REQUIRE(danmaku_validate_head(caller, transforms_amount, marker_transform));
	PATTERN_REJECT_IF(!Math::is_finite(p.diagonal_x) || p.diagonal_x < 0.0 || !Math::is_finite(p.diagonal_y) || p.diagonal_y < 0.0 || !Math::is_finite(p.rotation) || !Math::is_finite(p.facing_offset_degrees), "diagonals must be finite and >= 0, rotation and facing_offset_degrees finite.");
	PATTERN_REQUIRE(pattern_check_corner_layout(caller, p.corner, p.outline.layer_layout));
	PackedVector2Array corners = build_diamond_corners(p.diagonal_x, p.diagonal_y, p.rotation);
	return layout_polygon_corners2d(caller, transforms_amount, marker_transform, corners, p.face_outward, p.facing_offset_degrees, p.outline, p.corner);
}

} // namespace BlastBullets2D
