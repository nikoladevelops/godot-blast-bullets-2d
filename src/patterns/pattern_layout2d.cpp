// Shared machinery behind the pattern generators: the outline layout engine
// (layout_outline_slots: On Outline / Layers / Fill Inside), even resampling of
// closed curves, outline layers, danmaku slot validation and skip indices.

#include "patterns/patterns_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Clamp finished slot arrays into a sane world box: huge-but-finite inputs
// (1e30 spacing on an 8k volley) overflow slot math to Inf/NaN, which would
// poison the whole volley downstream. Slots that blew out land at the clamped
// edge, valid slots pass through untouched. Warns once when it fires.
void danmaku_clamp_slots_finite(const char *caller_name, TypedArray<Transform2D> &slots) {
	const double BOUND = 400000.0;
	bool clamped = false;
	for (int i = 0; i < slots.size(); ++i) {
		Transform2D t = slots[i];
		Vector2 o = t.get_origin();
		real_t r = t.get_rotation();
		bool bad = !o.is_finite() || !Math::is_finite(r);
		if (!bad && (Math::abs(o.x) > BOUND || Math::abs(o.y) > BOUND)) {
			bad = true;
		}
		if (!bad) {
			continue;
		}
		clamped = true;
		real_t cx = Math::is_finite(o.x) ? (real_t)Math::clamp((double)o.x, -BOUND, BOUND) : 0.0;
		real_t cy = Math::is_finite(o.y) ? (real_t)Math::clamp((double)o.y, -BOUND, BOUND) : 0.0;
		real_t cr = Math::is_finite(r) ? r : 0.0;
		t.set_rotation_and_scale(cr, t.get_scale());
		t.set_origin(Vector2(cx, cy));
		slots[i] = t;
	}
	if (clamped) {
		UtilityFunctions::push_warning(String(caller_name) + ": some slots overflowed to Inf/NaN or left the world box and were clamped to its edge; shrink the spacing/radius.");
	}
}

// Outline layout engine shared by the closed-loop shape generators below.
// points/normals are parallel arrays in SLOT order (slot i = points[i]):
// slot-space origins plus geometric OUTWARD unit vectors (before any
// face_outward flip). points_are_local selects marker.xform for origins;
// rot_add carries a generator rotation quirk (0, or marker rot for the
// xform builders). facing_override (empty, or per-slot) replaces composed
// facing (ring random rotation). Default args reproduce each generator's
// legacy output exactly; non-default args remap:
//   reverse mirrors the slot order (winding flip), slot_offset rotates which
//     slot becomes bullet 0 (normalized mod count, negatives wrap).
//   facing selector rotates every default facing (0 / +90 / -90 deg).
//   FILL_INSIDE swaps the loop for a row-major grid masked to the loop
//     interior (even-odd rule on the angular-sorted silhouette, capped at
//     slot_count, may return fewer on small shapes); facings go radial from
//     the silhouette center. LAYERS keeps the slot count and spreads it over
//     concentric rings (layer 0 sits exactly on the outline; extras step
//     per layer_side, dealt per layer_fill).
// Crash-safe: every index is bounds-checked, non-finite inputs fall back to
// the marker origin instead of poisoning the volley, degenerate loops yield
// an empty (loud, when misused) array instead of garbage.
static bool outline_point_in_poly(const PackedVector2Array &poly, const Vector2 &p) {
    const int n = poly.size();
    if (n < 3 || !p.is_finite()) {
        return false;
    }
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const Vector2 a = poly[i];
        const Vector2 b = poly[j];
        if (!a.is_finite() || !b.is_finite()) {
            continue;
        }
        if ((a.y > p.y) != (b.y > p.y)) {
            const double x_int = (double)b.x + ((double)p.y - (double)b.y) / ((double)a.y - (double)b.y) * ((double)a.x - (double)b.x);
            if (Math::is_finite(x_int) && (double)p.x < x_int) {
                inside = !inside;
            }
        }
    }
    return inside;
}

double outline_point_seg_dist(const Vector2 &p, const Vector2 &a, const Vector2 &b) {
    const Vector2 ab = b - a;
    const double len_sq = (double)ab.length_squared();
    if (!(len_sq > 1e-12) || !p.is_finite() || !a.is_finite() || !b.is_finite()) {
        return (double)p.distance_to(a);
    }
    double t = (double)(p - a).dot(ab) / len_sq;
    t = Math::clamp(t, 0.0, 1.0);
    return (double)p.distance_to(a + ab * (real_t)t);
}

// Angular-sorted silhouette of a slot cloud around its average: recovers a
// clean simple polygon for star-shaped outlines (star/flower/rose loops),
// approximates one for self-intersecting weaves. Consecutive near-dupes
// (repeated star vertices, closed-curve seams) collapse. False when no
// usable interior exists.
bool outline_build_boundary(const PackedVector2Array &points, PackedVector2Array &r_boundary, Vector2 &r_center) {
    PackedVector2Array finite;
    for (int i = 0; i < points.size(); ++i) {
        if (points[i].is_finite()) {
            finite.push_back(points[i]);
        }
    }
    if (finite.size() < 3) {
        return false;
    }
    Vector2 avg(0, 0);
    for (int i = 0; i < finite.size(); ++i) {
        avg += finite[i];
    }
    avg /= (real_t)finite.size();
    if (!avg.is_finite()) {
        return false;
    }
    // Insertion sort by polar angle (slot clouds are small; no allocations).
    PackedInt32Array order;
    order.resize(finite.size());
    for (int i = 0; i < finite.size(); ++i) {
        order[i] = i;
    }
    for (int i = 1; i < order.size(); ++i) {
        const int key = order[i];
        const double key_a = Math::atan2((double)finite[key].y - (double)avg.y, (double)finite[key].x - (double)avg.x);
        int j = i - 1;
        while (j >= 0) {
            const double ja = Math::atan2((double)finite[order[j]].y - (double)avg.y, (double)finite[order[j]].x - (double)avg.x);
            if (ja <= key_a) {
                break;
            }
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }
    r_boundary.clear();
    for (int i = 0; i < order.size(); ++i) {
        const Vector2 p = finite[order[i]];
        if (r_boundary.is_empty() || r_boundary[r_boundary.size() - 1].distance_to(p) > 1e-6) {
            r_boundary.push_back(p);
        }
    }
    if (r_boundary.size() >= 2 && r_boundary[0].distance_to(r_boundary[r_boundary.size() - 1]) <= 1e-6) {
        r_boundary.resize(r_boundary.size() - 1);
    }
    if (r_boundary.size() < 3) {
        r_boundary.clear();
        return false;
    }
    // Interior-safe center: AABB middle always reads central for these
    // silhouettes (an average can land on a figure-8 crossing).
    Vector2 mn = r_boundary[0];
    Vector2 mx = r_boundary[0];
    for (int i = 1; i < r_boundary.size(); ++i) {
        mn.x = MIN(mn.x, r_boundary[i].x);
        mn.y = MIN(mn.y, r_boundary[i].y);
        mx.x = MAX(mx.x, r_boundary[i].x);
        mx.y = MAX(mx.y, r_boundary[i].y);
    }
    r_center = (mn + mx) * 0.5;
    if (!r_center.is_finite()) {
        return false;
    }
    return true;
}

// Even arc-length resample of a slot-space polyline into m points (plus
// interpolated parallel normals/override facings). Closed loops wrap around
// (no seam duplicate, so decimated subsets can never strand a 1-step seam
// gap next to bullet 0); open polylines pin both endpoints. Override angles
// interpolate along the shortest arc. Degenerate input yields copies of the
// first point so callers always get exactly m outputs.
void resample_loop_even(const PackedVector2Array &pts, const PackedVector2Array &nrms, const PackedFloat32Array &ovr, int m, bool closed, PackedVector2Array &r_pts, PackedVector2Array &r_nrms, PackedFloat32Array &r_ovr, double phase) {
	r_pts.clear();
	r_nrms.clear();
	r_ovr.clear();
	const int n = pts.size();
	if (n <= 0 || m <= 0) {
		return;
	}
	const Vector2 fallback_n = (!nrms.is_empty() && nrms[0].is_finite()) ? nrms[0] : Vector2(1, 0);
	const real_t fallback_o = !ovr.is_empty() ? ovr[0] : 0.0f;
	if (n == 1 || m == 1) {
		const Vector2 anchor = pts[0].is_finite() ? pts[0] : Vector2(0, 0);
		for (int k = 0; k < m; ++k) {
			r_pts.push_back(anchor);
			r_nrms.push_back(fallback_n);
			if (!ovr.is_empty()) {
				r_ovr.push_back(fallback_o);
			}
		}
		return;
	}
	const int segs = closed ? n : n - 1;
	PackedFloat64Array cum;
	cum.resize(segs + 1);
	cum[0] = 0.0;
	for (int s = 0; s < segs; ++s) {
		const Vector2 a = pts[s % n];
		const Vector2 b = pts[(s + 1) % n];
		double seg = 0.0;
		if (a.is_finite() && b.is_finite()) {
			seg = (double)a.distance_to(b);
		}
		cum[s + 1] = cum[s] + (Math::is_finite(seg) && seg > 0.0 ? seg : 0.0);
	}
	const double total = cum[segs];
	if (!(total > 0.0) || !Math::is_finite(total)) {
		const Vector2 anchor = pts[0].is_finite() ? pts[0] : Vector2(0, 0);
		for (int k = 0; k < m; ++k) {
			r_pts.push_back(anchor);
			r_nrms.push_back(fallback_n);
			if (!ovr.is_empty()) {
				r_ovr.push_back(fallback_o);
			}
		}
		return;
	}
	const bool use_nrms = nrms.size() == n;
	const bool use_ovr = !ovr.is_empty() && ovr.size() == n;
	int seg = 0;
	for (int k = 0; k < m; ++k) {
		// Closed loops may start part of a step in (phase in [0, 1)); open
		// runs always keep both endpoints.
		const double target = closed ? (total * Math::fmod((double)k + phase, (double)m) / (double)m)
									 : (total * (double)k / (double)(m - 1));
		if (closed && target < cum[seg]) {
			seg = 0; // the phase wrapped the last slot back to the start
		}
		while (seg < segs - 1 && cum[seg + 1] < target) {
			++seg;
		}
		const double seg_len = cum[seg + 1] - cum[seg];
		double t = (seg_len > 1e-12) ? (target - cum[seg]) / seg_len : 0.0;
		t = Math::clamp(t, 0.0, 1.0);
		const int ia = seg % n;
		const int ib = (seg + 1) % n;
		const Vector2 a = pts[ia].is_finite() ? pts[ia] : pts[0];
		const Vector2 b = pts[ib].is_finite() ? pts[ib] : a;
		r_pts.push_back(a.lerp(b, (real_t)t));
		if (use_nrms) {
			Vector2 nm = nrms[ia].lerp(nrms[ib], (real_t)t);
			if (nm.length_squared() <= 1e-12 || !nm.is_finite()) {
				nm = nrms[ia].length_squared() > 1e-12 ? nrms[ia] : fallback_n;
			}
			r_nrms.push_back(nm.normalized());
		} else {
			r_nrms.push_back(fallback_n);
		}
		if (use_ovr) {
			const double oa = (double)ovr[ia];
			double ob = (double)ovr[ib];
			if (Math::is_finite(oa) && Math::is_finite(ob)) {
				while (ob - oa > Math::PI) {
					ob -= Math::TAU;
				}
				while (ob - oa < -Math::PI) {
					ob += Math::TAU;
				}
				r_ovr.push_back((real_t)(oa + (ob - oa) * t));
			} else if (Math::is_finite(oa)) {
				r_ovr.push_back((real_t)oa);
			} else {
				r_ovr.push_back(fallback_o);
			}
		}
	}
}

// Closest-pair test for the distinct-resample retry: true when two points
// lie closer than `min_gap` (sort by x, sweep a window; O(m log m)).
static bool points_have_near_pair(const PackedVector2Array &pts, real_t min_gap) {
	const int m = pts.size();
	if (m < 2) {
		return false;
	}
	std::vector<Vector2> sorted(pts.ptr(), pts.ptr() + m);
	std::sort(sorted.begin(), sorted.end(), [](const Vector2 &a, const Vector2 &b) { return a.x < b.x; });
	const real_t gap2 = min_gap * min_gap;
	for (int i = 0; i < m; ++i) {
		for (int j = i + 1; j < m && sorted[j].x - sorted[i].x < min_gap; ++j) {
			if (sorted[i].distance_squared_to(sorted[j]) < gap2) {
				return true;
			}
		}
	}
	return false;
}

// Arc-even resample that keeps every slot visible: a closed curve that
// crosses itself (rose/rhodonea petals meeting at the centre, Lissajous
// crossings) can land two slots on the same crossing for some counts. Phase 0
// is tried first (existing layouts stay identical); otherwise the start
// slides by a fraction of a step until no two slots coincide. Open runs and
// true retraces are not this function's job (their sweeps are fixed at the
// source).
static constexpr real_t kDistinctSlotGap = 0.5;

// `avoid_radius` > 0 also keeps every slot that far from `avoid_point`
// (layer rings scale about it: a slot there would stack on every ring).
void resample_loop_even_distinct(const PackedVector2Array &pts, const PackedVector2Array &nrms, const PackedFloat32Array &ovr, int m, bool closed, PackedVector2Array &r_pts, PackedVector2Array &r_nrms, PackedFloat32Array &r_ovr, const Vector2 &avoid_point, real_t avoid_radius) {
	auto clashes = [&](const PackedVector2Array &cand) {
		if (points_have_near_pair(cand, kDistinctSlotGap)) {
			return true;
		}
		if (avoid_radius > 0.0) {
			for (int i = 0; i < cand.size(); ++i) {
				if (cand[i].distance_to(avoid_point) < avoid_radius) {
					return true;
				}
			}
		}
		return false;
	};
	resample_loop_even(pts, nrms, ovr, m, closed, r_pts, r_nrms, r_ovr, 0.0);
	if (!closed || m < 1 || !clashes(r_pts)) {
		return;
	}
	// Prefer phases that land samples on source vertices (one vertex step =
	// m / n of a slot step): the slots stay exactly on the drawn curve. The
	// fractional phases are the fallback.
	std::vector<double> phases;
	const int src_n = pts.size();
	for (int j = 1; j <= 8 && src_n > 0; ++j) {
		const double p = Math::fmod((double)j * (double)m / (double)src_n, 1.0);
		if (p > 1e-9) {
			phases.push_back(p);
		}
	}
	for (double p : { 0.5, 0.25, 0.75, 0.125, 0.375, 0.625, 0.875 }) {
		phases.push_back(p);
	}
	for (double phase : phases) {
		PackedVector2Array tp;
		PackedVector2Array tn;
		PackedFloat32Array to;
		resample_loop_even(pts, nrms, ovr, m, closed, tp, tn, to, phase);
		if (!clashes(tp)) {
			r_pts = tp;
			r_nrms = tn;
			r_ovr = to;
			return;
		}
	}
}

// Per-layer bullet deal shared by both even-per-layer layout branches
// (polygonal rebuild + smooth resample): which layer each bullet index rides
// (bullet order), how many each ring keeps after the max_dots cap (layer 0
// never capped), and which indices drop as overflow (first-kept).
struct LayerDeal {
	PackedInt32Array layer_of;
	int keep[64];
	PackedByteArray dropped;
};

static void deal_layer_membership(int n, int layer_count, int layer_fill, int layer_start_offset, int layer_max_dots, LayerDeal &r_deal) {
	r_deal.layer_of.clear();
	r_deal.dropped.clear();
	r_deal.layer_of.resize(n);
	r_deal.dropped.resize(n);
	int counts[64] = { 0 };
	for (int i = 0; i < n; ++i) {
		const int bl = BulletPatterns2D::helper_bullet_layer_index(i, n, layer_count, layer_fill, layer_start_offset);
		r_deal.layer_of[i] = bl;
		if (bl >= 0 && bl < 64) {
			counts[bl]++;
		}
	}
	for (int L = 0; L < 64; ++L) {
		r_deal.keep[L] = 0;
	}
	for (int L = 0; L < 64 && L < layer_count; ++L) {
		r_deal.keep[L] = counts[L];
		if (L > 0 && layer_max_dots > 0 && r_deal.keep[L] > layer_max_dots) {
			r_deal.keep[L] = layer_max_dots;
		}
	}
	int seen[64] = { 0 };
	for (int i = 0; i < n; ++i) {
		const int bl = r_deal.layer_of[i];
		r_deal.dropped[i] = 0;
		if (bl > 0 && bl < 64 && layer_max_dots > 0) {
			if (seen[bl] >= layer_max_dots) {
				r_deal.dropped[i] = 1;
			}
			seen[bl]++;
		}
	}
}

// ---- Fill Inside helpers ---------------------------------------------------
// Boundary for the interior fill, in the same space as the slot points:
// the caller's dense outline (curves: independent of the bullet count), else
// the polygon corners, else the legacy angular silhouette of the slot loop.
// Dense inputs are decimated to at most 512 vertices (margin math is
// O(cells x vertices)).
static bool fill_build_boundary(const PackedVector2Array &points, const PackedVector2Array &corners, const PackedVector2Array &outline, PackedVector2Array &r_boundary, Vector2 &r_center) {
	const PackedVector2Array &src = outline.size() >= 3 ? outline : corners;
	if (src.size() < 3) {
		return outline_build_boundary(points, r_boundary, r_center);
	}
	r_boundary.clear();
	const int stride = Math::max(1, (int)Math::ceil((double)src.size() / 512.0));
	for (int i = 0; i < src.size(); i += stride) {
		if (src[i].is_finite()) {
			r_boundary.push_back(src[i]);
		}
	}
	if (r_boundary.size() < 3) {
		return false;
	}
	double area2 = 0.0;
	Vector2 mn = r_boundary[0];
	Vector2 mx = r_boundary[0];
	for (int i = 0; i < r_boundary.size(); ++i) {
		const Vector2 a = r_boundary[i];
		const Vector2 b = r_boundary[(i + 1) % r_boundary.size()];
		area2 += (double)a.x * (double)b.y - (double)b.x * (double)a.y;
		mn = Vector2(MIN(mn.x, a.x), MIN(mn.y, a.y));
		mx = Vector2(MAX(mx.x, a.x), MAX(mx.y, a.y));
	}
	if (!(Math::abs(area2) > 1e-9) || !Math::is_finite(area2)) {
		return false;
	}
	r_center = (mn + mx) * 0.5;
	return r_center.is_finite();
}

// Row-major interior grid at `spacing` (even-odd rule, same crossing test
// as outline_point_in_poly): counts cells and optionally collects them.
// stop_after > 0 ends the count early once reached (the spacing search only
// needs "enough"). Guarded against runaway grids.
static int fill_scan_cells(const PackedVector2Array &poly, double spacing, bool stagger, double margin, std::vector<Vector2> *r_cells, int stop_after) {
	const int bn = poly.size();
	if (bn < 3 || !(spacing > 0.0) || !Math::is_finite(spacing)) {
		return 0;
	}
	Vector2 mn = poly[0];
	Vector2 mx = poly[0];
	for (int i = 1; i < bn; ++i) {
		mn = Vector2(MIN(mn.x, poly[i].x), MIN(mn.y, poly[i].y));
		mx = Vector2(MAX(mx.x, poly[i].x), MAX(mx.y, poly[i].y));
	}
	const double rows = ((double)mx.y - (double)mn.y) / spacing + 1.0;
	const double cols = ((double)mx.x - (double)mn.x) / spacing + 2.0;
	if (!(rows * cols < 4.0e7)) {
		return 0; // spacing absurdly small for the shape
	}
	int count = 0;
	int row = 0;
	std::vector<double> xs;
	for (double y = (double)mn.y; y <= (double)mx.y + 1e-9; y += spacing, ++row) {
		xs.clear();
		for (int i = 0, j = bn - 1; i < bn; j = i++) {
			const Vector2 a = poly[i];
			const Vector2 b = poly[j];
			if (((double)a.y > y) != ((double)b.y > y)) {
				const double x_int = (double)b.x + (y - (double)b.y) / ((double)a.y - (double)b.y) * ((double)a.x - (double)b.x);
				if (Math::is_finite(x_int)) {
					xs.push_back(x_int);
				}
			}
		}
		if (xs.empty()) {
			continue;
		}
		std::sort(xs.begin(), xs.end());
		const double x0 = (double)mn.x + ((stagger && (row % 2 == 1)) ? spacing * 0.5 : 0.0);
		for (double x = x0; x <= (double)mx.x + 1e-9; x += spacing) {
			// Inside when an odd number of crossings lie strictly right of x.
			const size_t right = xs.end() - std::upper_bound(xs.begin(), xs.end(), x);
			if ((right & 1) == 0) {
				continue;
			}
			const Vector2 cell((real_t)x, (real_t)y);
			if (margin > 0.0) {
				double clearance = 1e30;
				for (int k = 0; k < bn && clearance >= margin; ++k) {
					clearance = MIN(clearance, outline_point_seg_dist(cell, poly[k], poly[(k + 1) % bn]));
				}
				if (!(clearance >= margin)) {
					continue;
				}
			}
			++count;
			if (r_cells != nullptr) {
				r_cells->push_back(cell);
			} else if (stop_after > 0 && count >= stop_after) {
				return count;
			}
		}
	}
	return count;
}

// Largest spacing <= fill_spacing whose grid holds at least n cells:
// fill_spacing itself whenever everything fits, else halve to bracket and
// bisect. Deterministic (pure function of its inputs).
static double fill_spacing_that_fits(const PackedVector2Array &poly, double fill_spacing, bool stagger, double margin, int n) {
	if (fill_scan_cells(poly, fill_spacing, stagger, margin, nullptr, n) >= n) {
		return fill_spacing;
	}
	Vector2 mn = poly[0];
	Vector2 mx = poly[0];
	for (int i = 1; i < poly.size(); ++i) {
		mn = Vector2(MIN(mn.x, poly[i].x), MIN(mn.y, poly[i].y));
		mx = Vector2(MAX(mx.x, poly[i].x), MAX(mx.y, poly[i].y));
	}
	const double extent = MAX((double)(mx.x - mn.x), (double)(mx.y - mn.y));
	double hi = fill_spacing;
	double lo = fill_spacing * 0.5;
	while (fill_scan_cells(poly, lo, stagger, margin, nullptr, n) < n) {
		hi = lo;
		lo *= 0.5;
		if (lo < extent * 1e-5) {
			return lo; // no interior room (margin eats the shape): caller reports
		}
	}
	for (int it = 0; it < 24; ++it) {
		const double mid = 0.5 * (lo + hi);
		if (fill_scan_cells(poly, mid, stagger, margin, nullptr, n) >= n) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	return lo;
}

// Dense outline for Fill Inside / Layers: marker-relative sweep points
// shifted by `origin`, or nothing for On Outline (unused there).
PackedVector2Array fill_outline_from(int outline_placement, const PackedVector2Array &local_pts, const Vector2 &origin) {
	PackedVector2Array out;
	if (outline_placement != BulletPatterns2D::OUTLINE_FILL_INSIDE && outline_placement != BulletPatterns2D::OUTLINE_LAYERS) {
		return out;
	}
	out.resize(local_pts.size());
	for (int i = 0; i < local_pts.size(); ++i) {
		out[i] = origin + local_pts[i];
	}
	return out;
}

TypedArray<Transform2D> layout_outline_slots(const char *caller_name, const Transform2D &marker_transform, const PackedVector2Array &points, const PackedVector2Array &normals, bool points_are_local, real_t rot_add, bool face_outward, real_t facing_offset_degrees, const PackedFloat32Array &facing_override, int outline_placement, int outline_facing, bool outline_reverse, int outline_slot_offset, double fill_spacing, bool fill_stagger, double fill_margin, int layer_count, double layer_scale, int layer_side, int layer_fill, int layer_start_offset, int layer_scale_curve, const PackedFloat32Array &layer_custom_scales, int layer_twist, int layer_max_dots, int outline_distribution, int layer_layout, const PackedVector2Array &polygon_corners, int corner_priority, int corner_mode, double edge_margin, bool loop_closed, bool allow_resample, int corner_facing, const PackedVector2Array &dense_outline, const PackedVector2Array &dense_normals, const PackedFloat32Array &dense_overrides) {
    const int n = points.size();
    TypedArray<Transform2D> out;
    if (n <= 0) {
        return out;
    }
    // Slot-space copy of the loop: the LAYERS rescale below runs in slot
    // space (local for the xform builders, marker-global otherwise), so
    // marker translation must not leak into the scale. Conversion back to
    // global happens once, in place_origin(). Slot space is origin-centered
    // by construction (every loop generator builds centered shapes), so
    // layers scale about Vector2(0, 0) — the marker origin — on both the
    // volley and the preview side, exactly, at any bullet density.
    PackedVector2Array slot_points;
    slot_points.resize(n);
    for (int i = 0; i < n; ++i) {
        const Vector2 gp = points[i];
        slot_points[i] = points_are_local ? gp : marker_transform.affine_inverse().xform(gp);
    }
    // Loop centroid in slot space: every extra layer rescales the slot loop
    // about this point, so each ring is the same figure at a different size
    // (like a second spawner with a bigger shape). Falls back to the
    // slot-space origin (the marker origin) when degenerate.
    if (normals.size() != n) {
        UtilityFunctions::push_error(String(caller_name) + ": outline points/normals mismatch.");
        return out;
    }
    if (outline_placement < 0 || outline_placement > 2) {
        UtilityFunctions::push_error(String(caller_name) + ": outline_placement must be 0 (on outline), 1 (layers) or 2 (fill inside).");
        return out;
    }
    if (outline_facing < 0 || outline_facing > 2) {
        UtilityFunctions::push_error(String(caller_name) + ": outline_facing must be 0 (normal), 1 (+90 deg) or 2 (-90 deg).");
        return out;
    }
    if (!Math::is_finite(facing_offset_degrees)) {
        UtilityFunctions::push_error(String(caller_name) + ": facing_offset_degrees must be finite.");
        return out;
    }
    const bool use_override = !facing_override.is_empty();
    if (use_override && facing_override.size() != n) {
        UtilityFunctions::push_error(String(caller_name) + ": facing override size mismatch.");
        return out;
    }
    if (outline_placement == BulletPatterns2D::OUTLINE_FILL_INSIDE) {
        if (!Math::is_finite(fill_spacing) || fill_spacing <= 0.0 || !Math::is_finite(fill_margin) || fill_margin < 0.0) {
            UtilityFunctions::push_error(String(caller_name) + ": fill_spacing must be finite and > 0, fill_margin finite and >= 0.");
            return out;
        }
    }
    if (outline_placement == BulletPatterns2D::OUTLINE_LAYERS) {
        if (layer_count < 1 || layer_count > 64 || !Math::is_finite(layer_scale) || layer_scale <= 0.0 || layer_scale > 8.0) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_count must be in [1, 64], layer_scale finite in (0, 8].");
            return out;
        }
        if (layer_side < BulletPatterns2D::OUTLINE_LAYER_OUTWARD || layer_side > BulletPatterns2D::OUTLINE_LAYER_BOTH) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_side must be 0 (outward), 1 (inward) or 2 (both).");
            return out;
        }
        if (layer_fill < BulletPatterns2D::OUTLINE_LAYER_INTERLEAVED || layer_fill > BulletPatterns2D::OUTLINE_LAYER_PINGPONG) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_fill must be 0 (interleaved), 1 (sequential), 2 (outer first) or 3 (ping-pong).");
            return out;
        }
        if (layer_start_offset < 0) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_start_offset must be >= 0 (0 = start on the outline).");
            return out;
        }
        if (layer_scale_curve < BulletPatterns2D::OUTLINE_LAYER_CURVE_LINEAR || layer_scale_curve > BulletPatterns2D::OUTLINE_LAYER_CURVE_EXPONENTIAL) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_scale_curve must be 0 (linear) or 1 (exponential).");
            return out;
        }
        if (layer_custom_scales.size() > 64) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_custom_scales holds at most 64 entries.");
            return out;
        }
        for (int ci = 0; ci < layer_custom_scales.size(); ++ci) {
            const double cs = (double)layer_custom_scales[ci];
            if (!Math::is_finite(cs) || cs < 0.05 || cs > 64.0) {
                UtilityFunctions::push_error(String(caller_name) + ": layer_custom_scales entries must be finite in [0.05, 64].");
                return out;
            }
        }
        if (layer_max_dots < 0) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_max_dots must be >= 0 (0 = unlimited).");
            return out;
        }
        if (layer_layout < 0 || layer_layout > 1) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_layout must be 0 (shared loop) or 1 (even per layer).");
            return out;
        }
        if (outline_distribution < 0 || outline_distribution > 1) {
            UtilityFunctions::push_error(String(caller_name) + ": outline_distribution must be 0 (legacy) or 1 (symmetric).");
            return out;
        }
        if (corner_priority < 0 || corner_priority > 2) {
            UtilityFunctions::push_error(String(caller_name) + ": corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
            return out;
        }
        if (corner_mode < 0 || corner_mode > 1) {
            UtilityFunctions::push_error(String(caller_name) + ": corner_mode must be 0 (pin corners) or 1 (even arc).");
            return out;
        }
        if (!Math::is_finite(edge_margin) || edge_margin < 0.0) {
            UtilityFunctions::push_error(String(caller_name) + ": edge_margin must be finite and >= 0.");
            return out;
        }
        if (corner_facing < 0 || corner_facing > 2) {
            UtilityFunctions::push_error(String(caller_name) + ": corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
            return out;
        }
        // Collapse precheck: the smallest dealt scale must stay usable,
        // otherwise deep inward layers would pile onto (or through) the
        // center. Only layers that receive bullets are tested, so sparse
        // sequentials never trip on empty rings. Fail loud instead of
        // spawning a collapsed volley.
        bool seen[64] = { false };
        for (int i = 0; i < n; ++i) {
            const int bl = BulletPatterns2D::helper_bullet_layer_index(i, n, layer_count, layer_fill, layer_start_offset);
            if (bl > 0 && bl < layer_count) {
                seen[bl] = true;
            }
        }
        for (int L = 1; L < layer_count; ++L) {
            if (!seen[L]) {
                continue;
            }
            const double s = BulletPatterns2D::helper_layer_scale_factor(L, layer_scale, layer_side, layer_scale_curve, layer_custom_scales);
            if (!Math::is_finite(s) || s < 0.05) {
                UtilityFunctions::push_error(String(caller_name) + ": inward layers collapse below 5% size (lower the count/step).");
                return out;
            }
        }
    }
    const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
    const real_t selector = outline_facing == 1 ? Math::PI * 0.5 : (outline_facing == 2 ? -Math::PI * 0.5 : 0.0);
    const Vector2 marker_scale = marker_transform.get_scale();
    auto place_origin = [&](const Vector2 &local_pt) -> Vector2 {
        if (!local_pt.is_finite()) {
            return marker_transform.get_origin();
        }
        const Vector2 p = points_are_local ? marker_transform.xform(local_pt) : local_pt;
        return p.is_finite() ? p : marker_transform.get_origin();
    };
    // Slot order: reverse mirrors the winding, then the offset rotates which
    // slot becomes bullet 0 (normalized, negatives wrap).
    PackedInt32Array idx;
    idx.resize(n);
    for (int i = 0; i < n; ++i) {
        idx[i] = outline_reverse ? (n - 1 - i) : i;
    }
    if (n > 1) {
        int k = outline_slot_offset % n;
        if (k < 0) {
            k += n;
        }
        if (k != 0) {
            PackedInt32Array rotated;
            rotated.resize(n);
            for (int i = 0; i < n; ++i) {
                rotated[i] = idx[(i + k) % n];
            }
            idx = rotated;
        }
    }
    auto compose_facing = [&](int j) -> real_t {
        if (use_override) {
            const real_t o = (j >= 0 && j < facing_override.size()) ? facing_override[j] : 0.0f;
            return Math::is_finite((double)o) ? o + selector : rot_add + selector;
        }
        const Vector2 nrm = (j >= 0 && j < normals.size()) ? normals[j] : Vector2(1, 0);
        const double na = (nrm.is_finite() && nrm.length_squared() > 1e-12) ? nrm.angle() : 0.0;
        real_t rot = rot_add + (real_t)na + (face_outward ? 0.0f : Math::PI) + selector + facing_offset;
        if (!Math::is_finite((double)rot)) {
            rot = rot_add;
        }
        return rot;
    };
    if (outline_placement == BulletPatterns2D::OUTLINE_FILL_INSIDE) {
        // The interior comes from a boundary that does not depend on the
        // bullet count (dense curve outline, polygon corners, or legacy slot
        // silhouette). Exactly n cells are used: when fewer fit at
        // fill_spacing the spacing shrinks just enough, and when more fit the
        // n cells are picked evenly over the whole shape (not the top rows).
        PackedVector2Array boundary;
        Vector2 center;
        if (!fill_build_boundary(points, polygon_corners, dense_outline, boundary, center)) {
            UtilityFunctions::push_error(String(caller_name) + ": fill inside needs a usable loop interior (degenerate outline).");
            return out;
        }
        const double spacing = fill_spacing_that_fits(boundary, fill_spacing, fill_stagger, fill_margin, n);
        std::vector<Vector2> cells;
        fill_scan_cells(boundary, spacing, fill_stagger, fill_margin, &cells, 0);
        const int count = (int)cells.size();
        if (count == 0) {
            UtilityFunctions::push_error(String(caller_name) + ": fill inside found no room for bullets (fill_margin too large for the shape?).");
            return out;
        }
        const int take = MIN(n, count);
        for (int k = 0; k < take; ++k) {
            const Vector2 cell = cells[(size_t)((((int64_t)2 * k + 1) * count) / ((int64_t)2 * take))];
            const Vector2 radial = cell - center;
            const double ra = (radial.is_finite() && radial.length_squared() > 1e-12) ? radial.angle() : 0.0;
            real_t rot = rot_add + (real_t)ra + (face_outward ? 0.0f : Math::PI) + selector + facing_offset;
            if (!Math::is_finite((double)rot)) {
                rot = rot_add;
            }
            Transform2D slot(rot, place_origin(cell));
            slot.set_scale(marker_scale);
            if (slot.is_finite()) {
                out.push_back(slot);
            }
        }
        return out;
    }
    // Even-per-layer layout for corner-anchored polygons: each ring gets its
    // own symmetric loop (corners on every ring, even gaps), instead of
    // decimating one shared loop (which strands corners on a single ring).
    // The smooth-loop sibling below handles star-free shapes (circle, ring,
    // ellipse, heart, flower, rose, lissajous) by arc-length resampling.
    if (outline_placement == BulletPatterns2D::OUTLINE_LAYERS && layer_layout == BulletPatterns2D::OUTLINE_LAYER_LAYOUT_EVEN_PER_LAYER && !polygon_corners.is_empty()) {
        // Corners in slot space (same conversion as slot_points above).
        PackedVector2Array corner_slot;
        for (int c = 0; c < polygon_corners.size(); ++c) {
            const Vector2 gp = polygon_corners[c];
            const Vector2 sp = points_are_local ? gp : marker_transform.affine_inverse().xform(gp);
            if (sp.is_finite()) {
                corner_slot.push_back(sp);
            }
        }
        // Corner normals for the per-layer builder: reuse the averaged
        // corner normals from the base loop when available, else +X.
        // Base loop corners sit at known strides; simplest robust source is
        // recomputing averaged normals from the corner polygon itself.
        PackedVector2Array corner_normals;
        if (!compute_edge_normals_quiet(corner_slot, true, false, corner_normals) || corner_normals.size() != corner_slot.size()) {
            corner_normals.clear();
            for (int c = 0; c < corner_slot.size(); ++c) {
                corner_normals.push_back(Vector2(0, -1));
            }
        }
        // Deal bullets to layers (bullet order), then cap outer rings
        // (shared with the smooth sibling branch below).
        LayerDeal deal;
        deal_layer_membership(n, layer_count, layer_fill, layer_start_offset, layer_max_dots, deal);
        PackedInt32Array &bullet_layer_of = deal.layer_of;
        int (&layer_keep)[64] = deal.keep;
        PackedByteArray &dropped = deal.dropped;
        // Build one symmetric loop per non-empty layer.
        struct LayerLoop {
            PackedVector2Array pts;
            PackedVector2Array nrms;
        };
        LayerLoop layers[64];
        for (int L = 0; L < layer_count && L < 64; ++L) {
            if (layer_keep[L] <= 0) {
                continue;
            }
            PackedVector2Array lp;
            PackedVector2Array ln;
            // Mirror winding first when reversed, so every ring mirrors.
            PackedVector2Array use_corners = corner_slot;
            PackedVector2Array use_normals = corner_normals;
            if (outline_reverse && use_corners.size() >= 3) {
                PackedVector2Array mc;
                PackedVector2Array mn;
                mc.push_back(use_corners[0]);
                mn.push_back(use_normals[0]);
                for (int k = (int)use_corners.size() - 1; k >= 1; --k) {
                    mc.push_back(use_corners[k]);
                    mn.push_back(use_normals[k]);
                }
                use_corners = mc;
                use_normals = mn;
            }
            if (!build_symmetric_polygon_loop(use_corners, use_normals, layer_keep[L], outline_distribution, lp, ln, corner_priority, corner_mode, edge_margin, corner_facing)) {
                continue;
            }
            // Per-layer offset/twist: rotate each ring so stacked rings
            // interleave instead of spoking. Layer 0 keeps the canonical
            // offset only.
            int rot = 0;
            if (lp.size() > 1) {
                int off = outline_slot_offset % (int)lp.size();
                if (off < 0) {
                    off += (int)lp.size();
                }
                rot = off;
                if (L > 0 && layer_twist != 0) {
                    const int64_t ring_size = (int64_t)lp.size();
                    const int64_t tw = ((int64_t)L * (int64_t)layer_twist) % ring_size;
                    // Normalize into [0, ring_size): C++ % keeps the
                    // dividend's sign, so a negative twist would index
                    // before the buffer (hard crash). Same wrap rule as
                    // the shared-loop branch below.
                    rot = (int)(((int64_t)rot + tw) % ring_size + ring_size) % (int)ring_size;
                }
            }
            if (rot != 0 && lp.size() > 1) {
                PackedVector2Array rp;
                PackedVector2Array rn;
                for (int k = 0; k < (int)lp.size(); ++k) {
                    rp.push_back(lp[(k + rot) % (int)lp.size()]);
                    rn.push_back(ln[(k + rot) % (int)ln.size()]);
                }
                lp = rp;
                ln = rn;
            }
            // Scale about the slot-space origin (the marker origin).
            const double layer_s = (L == 0) ? 1.0 : BulletPatterns2D::helper_layer_scale_factor(L, layer_scale, layer_side, layer_scale_curve, layer_custom_scales);
            if (L > 0 && (!Math::is_finite(layer_s) || layer_s < 0.05)) {
                continue;
            }
            if (L > 0) {
                for (int k = 0; k < lp.size(); ++k) {
                    const Vector2 s = lp[k] * (real_t)layer_s;
                    lp[k] = s.is_finite() ? s : lp[k];
                }
            }
            layers[L].pts = lp;
            layers[L].nrms = ln;
        }
        // Emit in bullet index order (drops omitted), k-th member of a layer
        // takes the k-th loop slot in winding order.
        int layer_cursor[64] = { 0 };
        for (int i = 0; i < n; ++i) {
            if (dropped[i]) {
                continue;
            }
            const int bl = bullet_layer_of[i];
            if (bl < 0 || bl >= 64 || bl >= layer_count) {
                continue;
            }
            const int k = layer_cursor[bl]++;
            if (k < 0 || k >= layers[bl].pts.size() || k >= layers[bl].nrms.size()) {
                continue;
            }
            Vector2 local = layers[bl].pts[k];
            const Vector2 snrm = layers[bl].nrms[k];
            const double na = (snrm.is_finite() && snrm.length_squared() > 1e-12) ? snrm.angle() : 0.0;
            real_t rot = rot_add + (real_t)na + (face_outward ? 0.0f : Math::PI) + selector + facing_offset;
            if (!Math::is_finite((double)rot)) {
                rot = rot_add;
            }
            if (!points_are_local && local.is_finite()) {
                const Vector2 back = marker_transform.xform(local);
                local = back.is_finite() ? back : marker_transform.get_origin();
            }
            Transform2D slot(rot, place_origin(local));
            slot.set_scale(marker_scale);
            if (!slot.is_finite()) {
                slot = Transform2D(rot_add, marker_transform.get_origin());
                slot.set_scale(marker_scale);
            }
            out.push_back(slot);
        }
        return out;
    }
    // Even-per-layer layout for smooth loops (no corners): each ring resamples
    // the shared base loop evenly by arc length, so decimated subsets can
    // never strand a 1-step seam gap next to bullet 0 (the circle-55 defect).
    // Open arcs pin both endpoints per ring; closed loops wrap seamlessly.
    // Always active for multi-ring layers (smooth loops have no corner
    // policy to tune); single-ring output stays exactly the base loop.
    // Non-loop slot orders (flower FAN/PHYLLOTAXIS) opt out via allow_resample.
    if (outline_placement == BulletPatterns2D::OUTLINE_LAYERS && layer_count > 1 && polygon_corners.is_empty() && allow_resample && layer_layout == BulletPatterns2D::OUTLINE_LAYER_LAYOUT_EVEN_PER_LAYER) {
        PackedVector2Array base_nrms;
        if (normals.size() == n) {
            base_nrms = normals;
        } else {
            for (int i = 0; i < n; ++i) {
                base_nrms.push_back(Vector2(1, 0));
            }
        }
        LayerDeal deal;
        deal_layer_membership(n, layer_count, layer_fill, layer_start_offset, layer_max_dots, deal);
        PackedInt32Array &bullet_layer_of = deal.layer_of;
        int (&layer_keep)[64] = deal.keep;
        PackedByteArray &dropped = deal.dropped;
        struct SmoothRing {
            PackedVector2Array pts;
            PackedVector2Array nrms;
            PackedFloat32Array ovr;
        };
        SmoothRing rings[64];
        // Resample source: the dense ideal curve when the caller gave one
        // (rings land exactly on the drawn curve), else the slot loop.
        PackedVector2Array src_pts = slot_points;
        PackedVector2Array src_nrms = base_nrms;
        PackedFloat32Array src_ovr = facing_override;
        const bool dense_ok = dense_outline.size() >= 3 && dense_normals.size() == dense_outline.size() &&
                (facing_override.is_empty() || dense_overrides.size() == dense_outline.size());
        if (dense_ok) {
            src_pts.resize(dense_outline.size());
            const Transform2D to_slot = marker_transform.affine_inverse();
            for (int q = 0; q < dense_outline.size(); ++q) {
                src_pts[q] = points_are_local ? dense_outline[q] : to_slot.xform(dense_outline[q]);
            }
            src_nrms = dense_normals;
            src_ovr = facing_override.is_empty() ? PackedFloat32Array() : dense_overrides;
        }
        for (int L = 0; L < layer_count && L < 64; ++L) {
            if (layer_keep[L] <= 0) {
                continue;
            }
            PackedVector2Array lp;
            PackedVector2Array ln;
            PackedFloat32Array lo;
            // Rings scale about the slot-space origin: with several rings, no
            // slot may sit there (it would stack on every ring).
            const double ring_scale = (L == 0) ? 1.0 : BulletPatterns2D::helper_layer_scale_factor(L, layer_scale, layer_side, layer_scale_curve, layer_custom_scales);
            const real_t keep_off = (layer_count > 1 && Math::is_finite(ring_scale)) ? kDistinctSlotGap / (real_t)MAX(ring_scale, 0.05) : 0.0;
            resample_loop_even_distinct(src_pts, src_nrms, src_ovr, layer_keep[L], loop_closed, lp, ln, lo, Vector2(), keep_off);
            if (outline_reverse && lp.size() > 1) {
                PackedVector2Array rp;
                PackedVector2Array rn;
                PackedFloat32Array ro;
                const bool has_ovr = lo.size() == lp.size();
                for (int k = (int)lp.size() - 1; k >= 0; --k) {
                    rp.push_back(lp[k]);
                    rn.push_back(ln[k]);
                    if (has_ovr) {
                        ro.push_back(lo[k]);
                    }
                }
                lp = rp;
                ln = rn;
                lo = ro;
            }
            int rot = 0;
            if (lp.size() > 1) {
                int off = outline_slot_offset % (int)lp.size();
                if (off < 0) {
                    off += (int)lp.size();
                }
                rot = off;
                if (L > 0 && layer_twist != 0) {
                    const int64_t ring_size = (int64_t)lp.size();
                    const int64_t tw = ((int64_t)L * (int64_t)layer_twist) % ring_size;
                    // Normalize into [0, ring_size): C++ % keeps the
                    // dividend's sign, so a negative twist would index
                    // before the buffer (hard crash). Same wrap rule as
                    // the shared-loop branch below.
                    rot = (int)(((int64_t)rot + tw) % ring_size + ring_size) % (int)ring_size;
                }
            }
            if (rot != 0 && lp.size() > 1) {
                PackedVector2Array rp;
                PackedVector2Array rn;
                PackedFloat32Array ro;
                const bool has_ovr = lo.size() == lp.size();
                for (int k = 0; k < (int)lp.size(); ++k) {
                    rp.push_back(lp[(k + rot) % (int)lp.size()]);
                    rn.push_back(ln[(k + rot) % (int)ln.size()]);
                    if (has_ovr) {
                        ro.push_back(lo[(k + rot) % (int)lo.size()]);
                    }
                }
                lp = rp;
                ln = rn;
                lo = ro;
            }
            const double layer_s = (L == 0) ? 1.0 : BulletPatterns2D::helper_layer_scale_factor(L, layer_scale, layer_side, layer_scale_curve, layer_custom_scales);
            if (L > 0 && (!Math::is_finite(layer_s) || layer_s < 0.05)) {
                continue;
            }
            if (L > 0) {
                for (int k = 0; k < lp.size(); ++k) {
                    const Vector2 s = lp[k] * (real_t)layer_s;
                    lp[k] = s.is_finite() ? s : lp[k];
                }
            }
            rings[L].pts = lp;
            rings[L].nrms = ln;
            rings[L].ovr = lo;
        }
        int layer_cursor[64] = { 0 };
        const bool has_ovr = !facing_override.is_empty() && facing_override.size() == n;
        for (int i = 0; i < n; ++i) {
            if (dropped[i]) {
                continue;
            }
            const int bl = bullet_layer_of[i];
            if (bl < 0 || bl >= 64 || bl >= layer_count) {
                continue;
            }
            const int k = layer_cursor[bl]++;
            if (k < 0 || k >= rings[bl].pts.size()) {
                continue;
            }
            Vector2 local = rings[bl].pts[k];
            real_t rot;
            if (has_ovr && k < rings[bl].ovr.size()) {
                const real_t o = rings[bl].ovr[k];
                rot = Math::is_finite((double)o) ? o + selector : rot_add + selector;
            } else {
                const Vector2 snrm = (k < rings[bl].nrms.size()) ? rings[bl].nrms[k] : Vector2(1, 0);
                const double na = (snrm.is_finite() && snrm.length_squared() > 1e-12) ? snrm.angle() : 0.0;
                rot = rot_add + (real_t)na + (face_outward ? 0.0f : Math::PI) + selector + facing_offset;
                if (!Math::is_finite((double)rot)) {
                    rot = rot_add;
                }
            }
            if (!points_are_local && local.is_finite()) {
                const Vector2 back = marker_transform.xform(local);
                local = back.is_finite() ? back : marker_transform.get_origin();
            }
            Transform2D slot(rot, place_origin(local));
            slot.set_scale(marker_scale);
            if (!slot.is_finite()) {
                slot = Transform2D(rot_add, marker_transform.get_origin());
                slot.set_scale(marker_scale);
            }
            out.push_back(slot);
        }
        return out;
    }
    // Per-layer bullet counters for the max_dots density cap. Layer 0 is
    // never capped (the base outline always reads complete); overflow on
    // outer rings is dropped, first-kept in bullet order.
    int layer_used[64] = { 0 };
    for (int i = 0; i < n; ++i) {
        // Bullet i rides the (possibly reverse/offset-edited) slot loop in
        // order: the loop IS the figure, so layers only rescale that slot's
        // point about the loop center and never reseat bullets onto other
        // slots. (The fill deal only chooses WHICH layer each slot rides.)
        int j = (i >= 0 && i < idx.size()) ? idx[i] : ((n > 0) ? (i % n) : 0);
        int bullet_layer = 0;
        if (outline_placement == BulletPatterns2D::OUTLINE_LAYERS) {
            bullet_layer = BulletPatterns2D::helper_bullet_layer_index(i, n, layer_count, layer_fill, layer_start_offset);
            // Twist: rotate each successive layered ring's slot assignment so
            // stacked rings interleave angularly instead of sitting in
            // spokes. Layer 0 is never twisted (the base outline stays exact).
            // Facing follows the twisted slot below.
            if (bullet_layer > 0 && layer_twist != 0 && n > 1) {
                const int64_t shift = ((int64_t)bullet_layer * (int64_t)layer_twist) % (int64_t)n;
                j = (int)(((int64_t)j + shift) % (int64_t)n + (int64_t)n) % n;
            }
            // Density cap: first max_dots bullets per extra layer are kept,
            // the rest are dropped (total may shrink below
            // helper_bullets_amount). Layer 0 is never capped.
            if (layer_max_dots > 0 && bullet_layer > 0 && bullet_layer < 64) {
                if (layer_used[bullet_layer] >= layer_max_dots) {
                    continue;
                }
                layer_used[bullet_layer]++;
            }
        }
        const Vector2 slot_base = (j >= 0 && j < slot_points.size()) ? slot_points[j] : Vector2(0, 0);
        Vector2 local = slot_base;
        if (outline_placement == BulletPatterns2D::OUTLINE_LAYERS) {
            // Layer 0 always sits exactly on the outline. Higher layers
            // re-spawn the same slot scaled about the loop center, so every
            // layer is the identical figure at a different size. Facings
            // still use the slot normals below and never change across
            // layers. Scale and deal come from the shared helpers so the
            // preview (which scales identically) can never disagree with
            // the volley.
            // NOTE: slot_points are already in slot space (local for the
            // xform builders, marker-global otherwise), which is
            // origin-centered by construction, so scale here and convert to
            // global once below: translation can never leak into the scale,
            // at any marker position.
            if (bullet_layer > 0) {
                const double layer_s = BulletPatterns2D::helper_layer_scale_factor(bullet_layer, layer_scale, layer_side, layer_scale_curve, layer_custom_scales);
                if (Math::is_finite(layer_s) && layer_s >= 0.05) {
                    const Vector2 scaled = local * (real_t)layer_s;
                    if (scaled.is_finite()) {
                        local = scaled;
                    }
                }
            }
        }
        // Slot-space -> global: local builders xform through place_origin;
        // global builders stored origin-relative slots that must be composed
        // back onto the marker (place_origin passes those through untouched).
        if (!points_are_local && local.is_finite()) {
            const Vector2 back = marker_transform.xform(local);
            local = back.is_finite() ? back : marker_transform.get_origin();
        }
        Transform2D slot(compose_facing(j), place_origin(local));
        slot.set_scale(marker_scale);
        if (!slot.is_finite()) {
            slot = Transform2D(rot_add, marker_transform.get_origin());
            slot.set_scale(marker_scale);
        }
        out.push_back(slot);
    }
    return out;
}

bool danmaku_validate_head(const char *caller_name, int transforms_amount, const Transform2D &marker_transform) {
	if (transforms_amount < 0) {
		UtilityFunctions::push_error(String(caller_name) + ": transforms_amount must be >= 0.");
		return false;
	}
	if (transforms_amount > HELPER_MAX_TRANSFORMS) {
		UtilityFunctions::push_error(String(caller_name) + ": transforms_amount (" + String::num_int64(transforms_amount) + ") exceeds the cap of " + String::num_int64(HELPER_MAX_TRANSFORMS) + " per call; split it into batches.");
		return false;
	}
	if (!marker_transform.get_origin().is_finite() || !Math::is_finite(marker_transform.get_rotation()) || !marker_transform.get_scale().is_finite()) {
		UtilityFunctions::push_error(String(caller_name) + ": marker_transform contains NaN/Inf.");
		return false;
	}
	// Singular markers (e.g. (0, 1) scale: determinant 0) pass every finite
	// check but their affine_inverse() is garbage, which layout_outline_slots
	// and the slot math consume unconditionally. Reject loudly instead of
	// emitting clamped-garbage volleys.
	if (!is_transform_invertible_safe2d(marker_transform)) {
		UtilityFunctions::push_error(String(caller_name) + ": marker_transform is singular (zero or degenerate scale); volley skipped.");
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Layer-ring shared math: single source of truth for BOTH the volley
// (layout_outline_slots below) and the spawner preview (rebuild_preview).
// Each extra layer re-spawns the selected shape scaled about the loop
// center (mean of the base slot loop): same figure at every layer, like a
// second spawner with a bigger shape. Pure functions (no state); identical
// inputs always give identical outputs, so dots and rings can never drift
// apart.
// ---------------------------------------------------------------------------

double BulletPatterns2D::helper_layer_scale_factor(int layer_index, double scale_step, int side, int scale_curve, const PackedFloat32Array &custom_scales) {
	if (layer_index <= 0) {
		return 1.0;
	}
	// pow() overflows to Inf on absurd layers (1.5^100000): let it stay Inf
	// so the collapse precheck below (and volley sizing) still sees the true
	// magnitude. Callers that need a drawable size clamp it themselves; the
	// documented [0.05, 64] band applies to stored custom_scales entries,
	// not to this computed growth factor.
	// (clamp_band removed: it masked sub-5% inward collapses as 0.05 and
	// broke the loud collapse rejection the layer tests rely on.)
	// Explicit per-layer scales win when present (shared with the preview so
	// custom rhythms coincide exactly).
	if (!custom_scales.is_empty()) {
		const int at = layer_index % custom_scales.size();
		const double s = (at >= 0 && at < custom_scales.size()) ? (double)custom_scales[at] : 1.0;
		if (!Math::is_finite(s) || s < 0.05 || s > 64.0) {
			UtilityFunctions::push_error("helper_layer_scale_factor: custom_scales entries must be finite in [0.05, 64].");
			return 1.0;
		}
		return s;
	}
	if (!Math::is_finite(scale_step) || scale_step <= 0.0 || scale_step > 8.0) {
		UtilityFunctions::push_error("helper_layer_scale_factor: scale_step must be finite in (0, 8].");
		return 1.0;
	}
	const bool exponential = (scale_curve == OUTLINE_LAYER_CURVE_EXPONENTIAL);
	if (scale_curve != OUTLINE_LAYER_CURVE_LINEAR && !exponential) {
		UtilityFunctions::push_error("helper_layer_scale_factor: scale_curve must be 0 (linear) or 1 (exponential).");
		return 1.0;
	}
	if (side == OUTLINE_LAYER_INWARD) {
		// Reciprocal so inward layers crowd toward the center without ever
		// crossing through zero and mirroring.
		const double grow = exponential ? Math::pow(1.0 + scale_step, (double)layer_index) : 1.0 + (double)layer_index * scale_step;
		return 1.0 / grow;
	}
	if (side == OUTLINE_LAYER_BOTH) {
		const int k = (layer_index + 1) / 2; // 1,1,2,2,3,3...
		if (layer_index % 2 == 1) {
			return exponential ? Math::pow(1.0 + scale_step, (double)k) : 1.0 + (double)k * scale_step;
		}
		const double grow = exponential ? Math::pow(1.0 + scale_step, (double)k) : 1.0 + (double)k * scale_step;
		return 1.0 / grow;
	}
	if (side != OUTLINE_LAYER_OUTWARD) {
		UtilityFunctions::push_error("helper_layer_scale_factor: side must be 0 (outward), 1 (inward) or 2 (both).");
		return 1.0;
	}
	return exponential ? Math::pow(1.0 + scale_step, (double)layer_index) : 1.0 + (double)layer_index * scale_step;
}

// Deal order shared by the volley layout, the spawner preview and the debug
// coincidence check, so all three agree on which bullet rides which layer.
// INTERLEAVED deals round-robin (bullet i rides layer i % count, preserving
// the winding order); SEQUENTIAL fills contiguous chunks starting from
// layer_start_offset (wrapping, so 0 keeps the outline-first order and small
// volleys still read as the base shape); OUTER_FIRST fills contiguous chunks
// from the outermost ring inward; PINGPONG waves 0..last..0 (start_offset is
// ignored there). Degenerate inputs yield layer 0.
int BulletPatterns2D::helper_bullet_layer_index(
		int bullet_index,
		int slot_count,
		int layer_count,
		int layer_fill,
		int layer_start_offset) {
	if (layer_count <= 1 || slot_count <= 0 || bullet_index < 0) {
		return 0;
	}
	if (layer_fill == OUTLINE_LAYER_SEQUENTIAL || layer_fill == OUTLINE_LAYER_OUTER_FIRST) {
		const int64_t st = ((int64_t)layer_start_offset % (int64_t)layer_count + (int64_t)layer_count) % (int64_t)layer_count;
		const int64_t chunk = ((int64_t)bullet_index * (int64_t)layer_count) / (int64_t)slot_count;
		const int64_t base = (layer_fill == OUTLINE_LAYER_OUTER_FIRST) ? ((int64_t)layer_count - 1 - chunk) : chunk;
		return (int)((base + st) % (int64_t)layer_count);
	}
	if (layer_fill == OUTLINE_LAYER_PINGPONG && layer_count > 1) {
		const int64_t period = (int64_t)2 * (int64_t)layer_count - 2;
		const int64_t m = (int64_t)bullet_index % period;
		return (int)(m < (int64_t)layer_count ? m : period - m);
	}
	return bullet_index % layer_count;
}

TypedArray<Transform2D> BulletPatterns2D::helper_apply_skip_indices(
		const TypedArray<Transform2D> &transforms,
		const PackedInt32Array &skip_indices) {
	TypedArray<Transform2D> out;
	if (skip_indices.is_empty()) {
		return transforms;
	}
	const int n = transforms.size();
	std::vector<uint8_t> skip(n, 0);
	bool warned_oob = false;
	for (int k = 0; k < skip_indices.size(); ++k) {
		const int idx = skip_indices[k];
		if (idx < 0 || idx >= n) {
			if (!warned_oob) {
				UtilityFunctions::push_warning("helper_apply_skip_indices: skip index out of range, ignoring it.");
				warned_oob = true;
			}
			continue;
		}
		skip[idx] = 1;
	}
	for (int i = 0; i < n; ++i) {
		if (!skip[i]) {
			out.push_back(transforms[i]);
		}
	}
	return out;
}

} // namespace BlastBullets2D
