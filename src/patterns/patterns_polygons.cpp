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
bool build_symmetric_polygon_loop(const PackedVector2Array &corners, const PackedVector2Array &corner_normals, int count, int distribution, PackedVector2Array &r_points, PackedVector2Array &r_normals, int corner_priority, int corner_mode, double edge_margin, int corner_facing) {
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
static bool sample_closed_polygon_loop(const PackedVector2Array &corners, int count, PackedVector2Array &r_points, PackedVector2Array &r_normals, int distribution = 1, int corner_priority = 0, int corner_mode = 0, double edge_margin = 0.0, int corner_facing = 0) {
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
	return build_symmetric_polygon_loop(corners, normals, count, distribution, r_points, r_normals, corner_priority, corner_mode, edge_margin, corner_facing);
}

PatternSlots2D BulletPatterns2D::generate_rectangle2d(int transforms_amount, Transform2D marker_transform, const RectangleParams2D &params) {
	const Vector2 &size = params.size;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_distribution = params.corner.outline_distribution;
	int layer_layout = params.outline.layer_layout;
	int outline_corner_priority = params.corner.outline_corner_priority;
	int outline_corner_mode = params.corner.outline_corner_mode;
	double outline_edge_margin = params.corner.outline_edge_margin;
	int outline_corner_facing = params.corner.outline_corner_facing;

	if (!danmaku_validate_head("helper_generate_transforms_rectangle", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (!size.is_finite() || size.x < 0.0 || size.y < 0.0 || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: size must be finite with sides >= 0, facing_offset_degrees finite.");
		return PatternSlots2D();
	}
	if (outline_distribution < 0 || outline_distribution > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: outline_distribution must be 0 (legacy) or 1 (symmetric).");
		return PatternSlots2D();
	}
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (outline_corner_priority < 0 || outline_corner_priority > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
		return PatternSlots2D();
	}
	if (outline_corner_mode < 0 || outline_corner_mode > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: outline_corner_mode must be 0 (pin corners) or 1 (even arc).");
		return PatternSlots2D();
	}
	if (!Math::is_finite(outline_edge_margin) || outline_edge_margin < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: outline_edge_margin must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (outline_corner_facing < 0 || outline_corner_facing > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
		return PatternSlots2D();
	}
	// Counter-clockwise outline from top-left; corners double as normals via
	// the shared edge worker so joints face clean diagonals. Normals stay
	// geometric here (the outline worker applies the face_outward flip).
	const Vector2 hw(size.x * 0.5, size.y * 0.5);
	PackedVector2Array corners;
	corners.push_back(Vector2(-hw.x, -hw.y));
	corners.push_back(Vector2(hw.x, -hw.y));
	corners.push_back(Vector2(hw.x, hw.y));
	corners.push_back(Vector2(-hw.x, hw.y));
	PackedVector2Array normals;
	if (!compute_edge_normals_quiet(corners, true, false, normals)) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: degenerate rectangle.");
		return PatternSlots2D();
	}
	const real_t total = 2.0 * (size.x + size.y);
	const real_t marker_rot = marker_transform.get_rotation();
	if (!(total > 0.0)) {
		// Zero-size box: every slot stacks at the marker facing outward.
		PatternSlots2D stacked = danmaku_make_slots(transforms_amount);
		for (int i = 0; i < transforms_amount; ++i) {
			Transform2D slot(marker_rot + Math::deg_to_rad(facing_offset_degrees), marker_transform.get_origin());
			danmaku_apply_marker_scale(slot, marker_transform);
			stacked[i] = slot;
		}
		return stacked;
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
	if (!build_symmetric_polygon_loop(corners, normals, transforms_amount, outline_distribution, loop_points, loop_normals, outline_corner_priority, outline_corner_mode, outline_edge_margin, outline_corner_facing)) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: degenerate rectangle.");
		return PatternSlots2D();
	}
	return layout_outline_slots("helper_generate_transforms_rectangle", marker_transform, loop_points, loop_normals, true, marker_rot, face_outward, facing_offset_degrees, PackedFloat32Array(), params.outline, params.corner, corners, true, true);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_rectangle(
		int transforms_amount,
		Transform2D marker_transform,
		const Vector2 &size,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int outline_distribution,
		int layer_layout,
		int outline_corner_priority,
		int outline_corner_mode,
		double outline_edge_margin,
		int outline_corner_facing) {
	RectangleParams2D params;
	params.size = size;
	params.face_outward = face_outward;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.corner.outline_distribution = outline_distribution;
	params.outline.layer_layout = layer_layout;
	params.corner.outline_corner_priority = outline_corner_priority;
	params.corner.outline_corner_mode = outline_corner_mode;
	params.corner.outline_edge_margin = outline_edge_margin;
	params.corner.outline_corner_facing = outline_corner_facing;
	return pattern_slots_to_array(generate_rectangle2d(transforms_amount, marker_transform, params));
}

PatternSlots2D BulletPatterns2D::generate_polygon2d(int transforms_amount, Transform2D marker_transform, const PolygonParams2D &params) {
	int vertices = params.vertices;
	real_t radius = params.radius;
	real_t base_rotation = params.base_rotation;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_distribution = params.corner.outline_distribution;
	int layer_layout = params.outline.layer_layout;
	int outline_corner_priority = params.corner.outline_corner_priority;
	int outline_corner_mode = params.corner.outline_corner_mode;
	double outline_edge_margin = params.corner.outline_edge_margin;
	int outline_corner_facing = params.corner.outline_corner_facing;

	if (!danmaku_validate_head("helper_generate_transforms_polygon", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (vertices < 3 || !Math::is_finite(radius) || radius < 0.0 || !Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: vertices must be >= 3, radius finite and >= 0, rotations finite.");
		return PatternSlots2D();
	}
	// The corner loop below builds `vertices` corners: cap it like star's
	// points so hostile input can't hang the game.
	if (vertices > HELPER_MAX_TRANSFORMS) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: vertices is absurdly large; keep it near the bullet count.");
		return PatternSlots2D();
	}
	if (outline_distribution < 0 || outline_distribution > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: outline_distribution must be 0 (legacy) or 1 (symmetric).");
		return PatternSlots2D();
	}
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (outline_corner_priority < 0 || outline_corner_priority > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
		return PatternSlots2D();
	}
	if (outline_corner_mode < 0 || outline_corner_mode > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: outline_corner_mode must be 0 (pin corners) or 1 (even arc).");
		return PatternSlots2D();
	}
	if (!Math::is_finite(outline_edge_margin) || outline_edge_margin < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: outline_edge_margin must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (outline_corner_facing < 0 || outline_corner_facing > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
		return PatternSlots2D();
	}
	PackedVector2Array corners;
	for (int k = 0; k < vertices; ++k) {
		const real_t a = base_rotation + Math::TAU * (real_t)k / (real_t)vertices;
		corners.push_back(Vector2(Math::cos(a), Math::sin(a)) * radius);
	}
	PackedVector2Array normals;
	if (!compute_edge_normals_quiet(corners, true, false, normals)) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: degenerate polygon.");
		return PatternSlots2D();
	}
	// Arc-length walk so slots spread evenly even on stretched shapes; the
	// shared outline worker assembles facings (marker_rot rides as rot_add).
	// Normals stay geometric here (the worker applies the face_outward flip).
	real_t total = 0.0;
	for (int k = 0; k < vertices; ++k) {
		total += corners[k].distance_to(corners[(k + 1) % vertices]);
	}
	const real_t marker_rot = marker_transform.get_rotation();
	if (!(total > 0.0) || !Math::is_finite(total)) {
		PatternSlots2D stacked = danmaku_make_slots(transforms_amount);
		for (int i = 0; i < transforms_amount; ++i) {
			Transform2D slot(marker_rot + Math::deg_to_rad(facing_offset_degrees), marker_transform.get_origin());
			danmaku_apply_marker_scale(slot, marker_transform);
			stacked[i] = slot;
		}
		return stacked;
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
	if (!build_symmetric_polygon_loop(corners, normals, transforms_amount, outline_distribution, loop_points, loop_normals, outline_corner_priority, outline_corner_mode, outline_edge_margin, outline_corner_facing)) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: degenerate polygon.");
		return PatternSlots2D();
	}
	return layout_outline_slots("helper_generate_transforms_polygon", marker_transform, loop_points, loop_normals, true, marker_rot, face_outward, facing_offset_degrees, PackedFloat32Array(), params.outline, params.corner, corners, true, true);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_polygon(
		int transforms_amount,
		Transform2D marker_transform,
		int vertices,
		real_t radius,
		real_t base_rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int outline_distribution,
		int layer_layout,
		int outline_corner_priority,
		int outline_corner_mode,
		double outline_edge_margin,
		int outline_corner_facing) {
	PolygonParams2D params;
	params.vertices = vertices;
	params.radius = radius;
	params.base_rotation = base_rotation;
	params.face_outward = face_outward;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.corner.outline_distribution = outline_distribution;
	params.outline.layer_layout = layer_layout;
	params.corner.outline_corner_priority = outline_corner_priority;
	params.corner.outline_corner_mode = outline_corner_mode;
	params.corner.outline_edge_margin = outline_edge_margin;
	params.corner.outline_corner_facing = outline_corner_facing;
	return pattern_slots_to_array(generate_polygon2d(transforms_amount, marker_transform, params));
}

PatternSlots2D BulletPatterns2D::generate_triangle2d(int transforms_amount, Transform2D marker_transform, const TriangleParams2D &params) {
	TriangleType triangle_type = params.triangle_type;
	real_t size_a = params.size_a;
	real_t size_b = params.size_b;
	real_t rotation = params.rotation;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_distribution = params.corner.outline_distribution;
	int layer_layout = params.outline.layer_layout;
	int outline_corner_priority = params.corner.outline_corner_priority;
	int outline_corner_mode = params.corner.outline_corner_mode;
	double outline_edge_margin = params.corner.outline_edge_margin;
	int outline_corner_facing = params.corner.outline_corner_facing;

	if (!danmaku_validate_head("helper_generate_transforms_triangle", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (triangle_type < TRIANGLE_EQUILATERAL || triangle_type > TRIANGLE_RIGHT) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: unknown triangle_type.");
		return PatternSlots2D();
	}
	if (!Math::is_finite(size_a) || size_a < 0.0 || !Math::is_finite(size_b) || size_b < 0.0 || !Math::is_finite(rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: size_a/size_b must be finite and >= 0, rotation and facing_offset_degrees finite.");
		return PatternSlots2D();
	}
	if (outline_distribution < 0 || outline_distribution > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: outline_distribution must be 0 (legacy) or 1 (symmetric).");
		return PatternSlots2D();
	}
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (outline_corner_priority < 0 || outline_corner_priority > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
		return PatternSlots2D();
	}
	if (outline_corner_mode < 0 || outline_corner_mode > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: outline_corner_mode must be 0 (pin corners) or 1 (even arc).");
		return PatternSlots2D();
	}
	if (!Math::is_finite(outline_edge_margin) || outline_edge_margin < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: outline_edge_margin must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (outline_corner_facing < 0 || outline_corner_facing > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
		return PatternSlots2D();
	}
	PackedVector2Array corners = build_triangle_corners(triangle_type, size_a, size_b, rotation);
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	if (!sample_closed_polygon_loop(corners, transforms_amount, loop_points, loop_normals, outline_distribution, outline_corner_priority, outline_corner_mode, outline_edge_margin, outline_corner_facing)) {
		// Degenerate (zero-area) triangle: stack at the marker like the
		// rectangle primitive instead of emitting garbage.
		PatternSlots2D stacked = danmaku_make_slots(transforms_amount);
		for (int i = 0; i < transforms_amount; ++i) {
			Transform2D slot(marker_transform.get_rotation() + Math::deg_to_rad(facing_offset_degrees), marker_transform.get_origin());
			danmaku_apply_marker_scale(slot, marker_transform);
			stacked[i] = slot;
		}
		return stacked;
	}
	return layout_outline_slots("helper_generate_transforms_triangle", marker_transform, loop_points, loop_normals, true, marker_transform.get_rotation(), face_outward, facing_offset_degrees, PackedFloat32Array(), params.outline, params.corner, corners, true, true);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_triangle(
		int transforms_amount,
		Transform2D marker_transform,
		TriangleType triangle_type,
		real_t size_a,
		real_t size_b,
		real_t rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int outline_distribution,
		int layer_layout,
		int outline_corner_priority,
		int outline_corner_mode,
		double outline_edge_margin,
		int outline_corner_facing) {
	TriangleParams2D params;
	params.triangle_type = triangle_type;
	params.size_a = size_a;
	params.size_b = size_b;
	params.rotation = rotation;
	params.face_outward = face_outward;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.corner.outline_distribution = outline_distribution;
	params.outline.layer_layout = layer_layout;
	params.corner.outline_corner_priority = outline_corner_priority;
	params.corner.outline_corner_mode = outline_corner_mode;
	params.corner.outline_edge_margin = outline_edge_margin;
	params.corner.outline_corner_facing = outline_corner_facing;
	return pattern_slots_to_array(generate_triangle2d(transforms_amount, marker_transform, params));
}

PatternSlots2D BulletPatterns2D::generate_trapezoid2d(int transforms_amount, Transform2D marker_transform, const TrapezoidParams2D &params) {
	real_t base_top = params.base_top;
	real_t base_bottom = params.base_bottom;
	real_t height = params.height;
	real_t rotation = params.rotation;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_distribution = params.corner.outline_distribution;
	int layer_layout = params.outline.layer_layout;
	int outline_corner_priority = params.corner.outline_corner_priority;
	int outline_corner_mode = params.corner.outline_corner_mode;
	double outline_edge_margin = params.corner.outline_edge_margin;
	int outline_corner_facing = params.corner.outline_corner_facing;

	if (!danmaku_validate_head("helper_generate_transforms_trapezoid", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(base_top) || base_top < 0.0 || !Math::is_finite(base_bottom) || base_bottom < 0.0 || !Math::is_finite(height) || height < 0.0 || !Math::is_finite(rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: bases and height must be finite and >= 0, rotation and facing_offset_degrees finite.");
		return PatternSlots2D();
	}
	if (outline_distribution < 0 || outline_distribution > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: outline_distribution must be 0 (legacy) or 1 (symmetric).");
		return PatternSlots2D();
	}
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (outline_corner_priority < 0 || outline_corner_priority > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
		return PatternSlots2D();
	}
	if (outline_corner_mode < 0 || outline_corner_mode > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: outline_corner_mode must be 0 (pin corners) or 1 (even arc).");
		return PatternSlots2D();
	}
	if (!Math::is_finite(outline_edge_margin) || outline_edge_margin < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: outline_edge_margin must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (outline_corner_facing < 0 || outline_corner_facing > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
		return PatternSlots2D();
	}
	PackedVector2Array corners = build_trapezoid_corners(base_top, base_bottom, height, rotation);
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	if (!sample_closed_polygon_loop(corners, transforms_amount, loop_points, loop_normals, outline_distribution, outline_corner_priority, outline_corner_mode, outline_edge_margin, outline_corner_facing)) {
		PatternSlots2D stacked = danmaku_make_slots(transforms_amount);
		for (int i = 0; i < transforms_amount; ++i) {
			Transform2D slot(marker_transform.get_rotation() + Math::deg_to_rad(facing_offset_degrees), marker_transform.get_origin());
			danmaku_apply_marker_scale(slot, marker_transform);
			stacked[i] = slot;
		}
		return stacked;
	}
	return layout_outline_slots("helper_generate_transforms_trapezoid", marker_transform, loop_points, loop_normals, true, marker_transform.get_rotation(), face_outward, facing_offset_degrees, PackedFloat32Array(), params.outline, params.corner, corners, true, true);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_trapezoid(
		int transforms_amount,
		Transform2D marker_transform,
		real_t base_top,
		real_t base_bottom,
		real_t height,
		real_t rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int outline_distribution,
		int layer_layout,
		int outline_corner_priority,
		int outline_corner_mode,
		double outline_edge_margin,
		int outline_corner_facing) {
	TrapezoidParams2D params;
	params.base_top = base_top;
	params.base_bottom = base_bottom;
	params.height = height;
	params.rotation = rotation;
	params.face_outward = face_outward;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.corner.outline_distribution = outline_distribution;
	params.outline.layer_layout = layer_layout;
	params.corner.outline_corner_priority = outline_corner_priority;
	params.corner.outline_corner_mode = outline_corner_mode;
	params.corner.outline_edge_margin = outline_edge_margin;
	params.corner.outline_corner_facing = outline_corner_facing;
	return pattern_slots_to_array(generate_trapezoid2d(transforms_amount, marker_transform, params));
}

PatternSlots2D BulletPatterns2D::generate_diamond2d(int transforms_amount, Transform2D marker_transform, const DiamondParams2D &params) {
	real_t diagonal_x = params.diagonal_x;
	real_t diagonal_y = params.diagonal_y;
	real_t rotation = params.rotation;
	bool face_outward = params.face_outward;
	real_t facing_offset_degrees = params.facing_offset_degrees;
	int outline_distribution = params.corner.outline_distribution;
	int layer_layout = params.outline.layer_layout;
	int outline_corner_priority = params.corner.outline_corner_priority;
	int outline_corner_mode = params.corner.outline_corner_mode;
	double outline_edge_margin = params.corner.outline_edge_margin;
	int outline_corner_facing = params.corner.outline_corner_facing;

	if (!danmaku_validate_head("helper_generate_transforms_diamond", transforms_amount, marker_transform)) {
		return PatternSlots2D();
	}
	if (!Math::is_finite(diagonal_x) || diagonal_x < 0.0 || !Math::is_finite(diagonal_y) || diagonal_y < 0.0 || !Math::is_finite(rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: diagonals must be finite and >= 0, rotation and facing_offset_degrees finite.");
		return PatternSlots2D();
	}
	if (outline_distribution < 0 || outline_distribution > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: outline_distribution must be 0 (legacy) or 1 (symmetric).");
		return PatternSlots2D();
	}
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return PatternSlots2D();
	}
	if (outline_corner_priority < 0 || outline_corner_priority > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
		return PatternSlots2D();
	}
	if (outline_corner_mode < 0 || outline_corner_mode > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: outline_corner_mode must be 0 (pin corners) or 1 (even arc).");
		return PatternSlots2D();
	}
	if (!Math::is_finite(outline_edge_margin) || outline_edge_margin < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: outline_edge_margin must be finite and >= 0.");
		return PatternSlots2D();
	}
	if (outline_corner_facing < 0 || outline_corner_facing > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
		return PatternSlots2D();
	}
	PackedVector2Array corners = build_diamond_corners(diagonal_x, diagonal_y, rotation);
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	if (!sample_closed_polygon_loop(corners, transforms_amount, loop_points, loop_normals, outline_distribution, outline_corner_priority, outline_corner_mode, outline_edge_margin, outline_corner_facing)) {
		PatternSlots2D stacked = danmaku_make_slots(transforms_amount);
		for (int i = 0; i < transforms_amount; ++i) {
			Transform2D slot(marker_transform.get_rotation() + Math::deg_to_rad(facing_offset_degrees), marker_transform.get_origin());
			danmaku_apply_marker_scale(slot, marker_transform);
			stacked[i] = slot;
		}
		return stacked;
	}
	return layout_outline_slots("helper_generate_transforms_diamond", marker_transform, loop_points, loop_normals, true, marker_transform.get_rotation(), face_outward, facing_offset_degrees, PackedFloat32Array(), params.outline, params.corner, corners, true, true);
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_diamond(
		int transforms_amount,
		Transform2D marker_transform,
		real_t diagonal_x,
		real_t diagonal_y,
		real_t rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int outline_distribution,
		int layer_layout,
		int outline_corner_priority,
		int outline_corner_mode,
		double outline_edge_margin,
		int outline_corner_facing) {
	DiamondParams2D params;
	params.diagonal_x = diagonal_x;
	params.diagonal_y = diagonal_y;
	params.rotation = rotation;
	params.face_outward = face_outward;
	params.facing_offset_degrees = facing_offset_degrees;
	params.outline.outline_placement = outline_placement;
	params.outline.outline_facing = outline_facing;
	params.outline.outline_reverse = outline_reverse;
	params.outline.outline_slot_offset = outline_slot_offset;
	params.outline.fill_spacing = fill_spacing;
	params.outline.fill_stagger = fill_stagger;
	params.outline.fill_margin = fill_margin;
	params.outline.layer_count = layer_count;
	params.outline.layer_scale = layer_scale;
	params.outline.layer_side = layer_side;
	params.outline.layer_fill = layer_fill;
	params.outline.layer_start_offset = layer_start_offset;
	params.outline.layer_scale_curve = layer_scale_curve;
	params.outline.layer_custom_scales = layer_custom_scales;
	params.outline.layer_twist = layer_twist;
	params.outline.layer_max_dots = layer_max_dots;
	params.corner.outline_distribution = outline_distribution;
	params.outline.layer_layout = layer_layout;
	params.corner.outline_corner_priority = outline_corner_priority;
	params.corner.outline_corner_mode = outline_corner_mode;
	params.corner.outline_edge_margin = outline_edge_margin;
	params.corner.outline_corner_facing = outline_corner_facing;
	return pattern_slots_to_array(generate_diamond2d(transforms_amount, marker_transform, params));
}

} // namespace BlastBullets2D
