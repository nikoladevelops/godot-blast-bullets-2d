// Polyline layout: arc-length placement along any generator-local polyline
// (the spawner's Path2D mode feeds a Path2D's baked curve through it, and
// GDScript can call BulletPatterns2D.helper_generate_transforms_polyline
// with any point list).

#include "patterns/patterns_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Arc-length layout along a generator-local polyline with tangent-first
// facing. Crash-safe by construction: every segment access is bounds-checked
// (ia/ib clamped into [0, n)), zero-length segments fall back to the nearest
// valid tangent, and degenerate curves (total <= 0) stack every slot at the
// first finite point with marker-relative fallback facing. Positions come out
// in global space (marker.xform), exactly like the factory edge generator,
// so the preview and the spin/scale passes downstream stay consistent.
PatternSlots2D polyline_layout2d(const Transform2D &marker, const PackedVector2Array &path_pts, int count, const PolylineLayout2D &p, bool quiet, const char *caller) {
    PatternSlots2D out;
    if (count <= 0) {
        return out;
    }
    const int n = path_pts.size();
    if (n < 2) {
        if (!quiet) UtilityFunctions::push_error(String(caller) + ": Path2D mode produced no points.");
        return out;
    }
    // Marker scale rides along (same as every factory generator); rotation
    // composes below. Guard once: a non-finite marker poisons everything.
    if (!marker.is_finite()) {
        if (!quiet) UtilityFunctions::push_error(String(caller) + ": Path2D mode marker transform is not finite.");
        return out;
    }
    const real_t marker_rot = marker.get_rotation();
    const Vector2 marker_scale = marker.get_scale();
    // Arc-length table in doubles (open) plus optional closing segment.
    PackedFloat64Array cum;
    cum.resize(n);
    cum[0] = 0.0;
    for (int i = 1; i < n; ++i) {
        const double seg = (double)path_pts[i - 1].distance_to(path_pts[i]);
        cum[i] = cum[i - 1] + (Math::is_finite(seg) && seg > 0.0 ? seg : 0.0);
    }
    double total = cum[n - 1];
    // Optional loop closure: only when the last-to-first stretch has real
    // length. Curves drawn as loops usually end ON the start point (zero
    // stretch, nothing to add); two-point outlines stay open (an out-and-back
    // doubling would read as a bug, not a loop).
    double closing_len = 0.0;
    if (p.closed && n > 2) {
        const double raw_closing = (double)path_pts[n - 1].distance_to(path_pts[0]);
        if (Math::is_finite(raw_closing) && raw_closing > 1e-9) {
            closing_len = raw_closing;
            total += closing_len;
        }
    }
    const bool loop = closing_len > 0.0;
    const int seg_count = loop ? n : n - 1;
    // A curve drawn as a loop (last point ON the first) needs no closing
    // stretch, but with p.closed it is still a ring: spreading
    // with the open n-1 divisor put the last bullet on the first.
    const bool ring = loop || (p.closed && n > 2);
    real_t facing_extra = 0.0;
    if (p.facing == BulletPatterns2D::POLYLINE_FACING_NORMAL_P90) {
        facing_extra = Math::PI * 0.5;
    } else if (p.facing == BulletPatterns2D::POLYLINE_FACING_NORMAL_M90) {
        facing_extra = -Math::PI * 0.5;
    }
    const real_t facing_offset = facing_extra + Math::deg_to_rad((real_t)p.facing_offset_deg);
    auto fallback_slot = [&](const Vector2 &local_pt) {
        Vector2 lp = local_pt.is_finite() ? local_pt : Vector2(0, 0);
        Transform2D slot(marker_rot + facing_offset, marker.xform(lp));
        slot.set_scale(marker_scale);
        if (!slot.is_finite()) {
            slot = Transform2D(marker_rot, marker.get_origin());
            slot.set_scale(marker_scale);
        }
        return slot;
    };
    if (!(total > 0.0) || !Math::is_finite(total)) {
        // Degenerate curve (all points coincide): stack at the first point.
        // Tangent is meaningless; face marker-relative + offset.
        for (int i = 0; i < count; ++i) {
            out.push_back(fallback_slot(path_pts[0]));
        }
        return out;
    }
    // Target distances along [0, total].
    PackedFloat64Array dists;
    dists.resize(count);
    if (p.distribution == BulletPatterns2D::POLYLINE_DISTRIBUTION_EVEN) {
        // Even cover of the whole curve. Loops wrap as a ring (no duplicated
        // seam bullet); open paths include both endpoints. start_offset
        // phase-rotates a loop, or pushes an open run toward the end (clamped).
        double phase = Math::is_finite(p.start_offset) ? p.start_offset : 0.0;
        if (phase < 0.0) {
            phase = 0.0;
        }
        if (ring) {
            const double ring_start = Math::fposmod(phase, total);
            for (int i = 0; i < count; ++i) {
                dists[i] = Math::fposmod(ring_start + total * (double)i / (double)count, total);
            }
        } else if (count == 1) {
            dists[0] = Math::clamp(phase, 0.0, total);
        } else {
            const double step = total / (double)(count - 1);
            for (int i = 0; i < count; ++i) {
                dists[i] = Math::clamp(phase + step * (double)i, 0.0, total);
            }
        }
    } else {
        double eff_spacing = p.spacing;
        if (!(eff_spacing > 0.0) || !Math::is_finite(eff_spacing)) {
            if (!quiet) UtilityFunctions::push_error(String(caller) + ": Path2D spacing is not usable.");
            return PatternSlots2D();
        }
        const double run = (double)(count - 1) * eff_spacing;
        if (p.overflow == BulletPatterns2D::POLYLINE_OVERFLOW_SHRINK_TO_FIT && count > 1 && run > total) {
            // A ring has `count` gaps (the seam is one of them); an open run
            // has count - 1 and keeps both endpoints.
            eff_spacing = ring ? total / (double)count : total / (double)(count - 1);
        }
        const double eff_run = (double)(count - 1) * eff_spacing;
        double base = 0.0;
        if (p.anchor == BulletPatterns2D::POLYLINE_ANCHOR_CENTER) {
            base = (total - eff_run) * 0.5;
        } else if (p.anchor == BulletPatterns2D::POLYLINE_ANCHOR_END) {
            base = total - eff_run;
        }
        if (base < 0.0) {
            base = 0.0; // run still longer (clamp/wrap handle it below)
        }
        for (int i = 0; i < count; ++i) {
            const double raw = base + (double)i * eff_spacing + p.start_offset;
            if (p.overflow == BulletPatterns2D::POLYLINE_OVERFLOW_WRAP) {
                dists[i] = Math::fposmod(raw, total);
            } else {
                dists[i] = Math::clamp(raw, 0.0, total);
            }
        }
    }
    for (int i = 0; i < count; ++i) {
        double d = dists[i];
        if (!Math::is_finite(d)) {
            d = 0.0;
        }
        if (d < 0.0) {
            d = 0.0;
        }
        // Endpoint-inclusive clamp: d == total is the exact curve end (End
        // anchor / Clamp pile-up live here). Only true overshoot folds, and
        // only in wrapping modes — dists already carry each mode's rule, so
        // this is just float-error insurance that can never relocate a bullet.
        if (d > total) {
            if (p.distribution == BulletPatterns2D::POLYLINE_DISTRIBUTION_EVEN && ring) {
                d = Math::fposmod(d, total);
            } else if (p.distribution != BulletPatterns2D::POLYLINE_DISTRIBUTION_EVEN && p.overflow == BulletPatterns2D::POLYLINE_OVERFLOW_WRAP) {
                d = Math::fposmod(d, total);
            } else {
                d = total;
            }
        }
        // Exact mirror: d == total (curve end, End anchor / Clamp pile-up)
        // must land on total, NOT wrap to 0 — the old fposmod() here pinned
        // bullet 0 at the start while everything else mirrored.
        if (p.reverse) {
            d = total - d;
        }
        // Locate segment: first seg whose end is past d (lower bound over the
        // monotone cumulative table). Binary search: O(log points) per
        // bullet instead of the old O(points) scan (a 10k-bullet volley on a
        // densely baked curve did 10k x ~1000 steps).
        int seg = 0;
        {
            int lo = 0;
            int hi = seg_count - 1;
            const double *cum_p = cum.ptr();
            while (lo < hi) {
                const int mid = (lo + hi) >> 1;
                const double seg_end = (mid + 1 < n) ? cum_p[mid + 1] : total;
                if (d < seg_end) {
                    hi = mid;
                } else {
                    lo = mid + 1;
                }
            }
            seg = lo;
        }
        if (seg < 0) {
            seg = 0;
        }
        if (seg >= seg_count) {
            seg = seg_count - 1;
        }
        int ia = seg % n;
        int ib = (seg + 1) % n;
        if (ia < 0 || ia >= n) {
            ia = 0;
        }
        if (ib < 0 || ib >= n) {
            ib = ia;
        }
        const double seg_start = (seg < n) ? cum[seg] : total;
        const double seg_end = (seg + 1 < n) ? cum[seg + 1] : total;
        const double seg_len = seg_end - seg_start;
        double t = (seg_len > 1e-9) ? (d - seg_start) / seg_len : 0.0;
        t = Math::clamp(t, 0.0, 1.0);
        const Vector2 pa = path_pts[ia];
        const Vector2 pb = path_pts[ib];
        Vector2 local = pa.lerp(pb, (real_t)t);
        if (!local.is_finite()) {
            local = pa.is_finite() ? pa : Vector2(0, 0);
        }
        // Tangent: this segment, else nearest valid one, else +X fallback.
        Vector2 tangent = pb - pa;
        if (!tangent.is_finite() || tangent.length_squared() <= 1e-12) {
            tangent = Vector2(0, 0);
            for (int s = 0; s < seg_count; ++s) {
                const int ja = s % n;
                const int jb = (s + 1) % n;
                if (ja < 0 || ja >= n || jb < 0 || jb >= n) {
                    continue;
                }
                const Vector2 cand = path_pts[jb] - path_pts[ja];
                if (cand.is_finite() && cand.length_squared() > 1e-12) {
                    tangent = cand;
                    break;
                }
            }
            if (tangent.length_squared() <= 1e-12) {
                tangent = Vector2(1, 0);
            }
        }
        if (p.reverse) {
            tangent = -tangent;
        }
        const real_t rot = marker_rot + tangent.angle() + facing_offset;
        if (!Math::is_finite((double)rot)) {
            out.push_back(fallback_slot(local));
            continue;
        }
        Transform2D slot(rot, marker.xform(local));
        slot.set_scale(marker_scale);
        if (!slot.is_finite()) {
            out.push_back(fallback_slot(local));
            continue;
        }
        out.push_back(slot);
    }
    return out;
}

PatternSlots2D BulletPatterns2D::generate_polyline2d(
		int transforms_amount,
		Transform2D marker_transform,
		const PackedVector2Array &points,
		bool closed,
		PolylineDistribution distribution,
		real_t spacing,
		PolylineOverflow overflow,
		PolylineAnchor anchor,
		real_t start_offset,
		bool reverse,
		PolylineFacing facing,
		real_t facing_offset_deg) {
	if (transforms_amount < 0 || transforms_amount > HELPER_MAX_TRANSFORMS) {
		UtilityFunctions::push_error("helper_generate_transforms_polyline: transforms_amount must be in 0.." + itos(HELPER_MAX_TRANSFORMS) + ".");
		return PatternSlots2D();
	}
	for (int i = 0; i < points.size(); ++i) {
		if (!points[i].is_finite()) {
			UtilityFunctions::push_error("helper_generate_transforms_polyline: points contain NaN/Inf.");
			return PatternSlots2D();
		}
	}
	PolylineLayout2D p;
	p.closed = closed;
	p.distribution = (int)distribution;
	p.spacing = spacing;
	p.overflow = (int)overflow;
	p.anchor = (int)anchor;
	p.start_offset = start_offset;
	p.reverse = reverse;
	p.facing = (int)facing;
	p.facing_offset_deg = facing_offset_deg;
	return polyline_layout2d(marker_transform, points, transforms_amount, p, false, "helper_generate_transforms_polyline");
}

TypedArray<Transform2D> BulletPatterns2D::helper_generate_transforms_polyline(
		int transforms_amount,
		Transform2D marker_transform,
		const PackedVector2Array &points,
		bool closed,
		PolylineDistribution distribution,
		real_t spacing,
		PolylineOverflow overflow,
		PolylineAnchor anchor,
		real_t start_offset,
		bool reverse,
		PolylineFacing facing,
		real_t facing_offset_deg) {
	return pattern_slots_to_array(generate_polyline2d(transforms_amount, marker_transform, points, closed, distribution, spacing, overflow, anchor, start_offset, reverse, facing, facing_offset_deg));
}

} // namespace BlastBullets2D
