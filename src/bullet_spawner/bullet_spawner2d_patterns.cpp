// BulletSpawner2D (split from bullet_spawner2d.cpp, same class):
// Pattern generation and the bake cache: raw generation per source,
// motion classification (RIGID/TRANSLATION/NONE), cached re-posing, the
// native span collect used by shoot_once and the cache verifier.

#include "bullet_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Trivial (identity) case for the folded matrix: no spin and unit scale.
static bool spin_is_trivial(real_t radians, real_t scale) {
    return radians == 0.0 && scale == 1.0f;
}

// Spin + pattern-scale folded into ONE matrix about the generator origin.
// The old code rotated per bullet (recomputing the shared angle's trig N
// times) and rebuilt each basis from its angle — which silently destroyed
// mirroring (negative determinant: get_rotation() cannot represent a
// reflection) and shear (set_scale normalizes). M = S(s)·R(r) applied as a
// plain matrix multiply is exact for every determinant and costs ~18
// flops/bullet with zero trig, zero sqrt. Built once per volley by
// make_spin_scale_matrix(); identity (spin 0, scale 1) returns t untouched.
static Transform2D make_spin_scale_matrix(const Vector2 &origin, real_t radians, real_t scale, bool &r_valid) {
    r_valid = false;
    if (spin_is_trivial(radians, scale)) {
        r_valid = true;
        return Transform2D();
    }
    const real_t c = Math::cos(radians);
    const real_t s = Math::sin(radians);
    if (!Math::is_finite(c) || !Math::is_finite(s) || !Math::is_finite(scale) || !origin.is_finite()) {
        return Transform2D();
    }
    // A = s·R: columns of the combined basis.
    const Vector2 ax = Vector2(c, s) * scale;
    const Vector2 ay = Vector2(-s, c) * scale;
    // Origin offset o − A·o, so out = M·t matches rotate-then-scale exactly.
    const Vector2 ao = Vector2(ax.x * origin.x + ay.x * origin.y, ax.y * origin.x + ay.y * origin.y);
    const Vector2 off = origin - ao;
    if (!ax.is_finite() || !ay.is_finite() || !off.is_finite()) {
        return Transform2D();
    }
    r_valid = true;
    return Transform2D(ax, ay, off);
}

bool BulletSpawner2D::supports_outline_layout(PatternSource source) {
    for (const PatternSourceInfo &info : kPatternSources) {
        if (info.id == source) {
            return info.outline;
        }
    }
    return false;
}

PackedVector2Array BulletSpawner2D::sample_path2d_polyline(bool quiet) const {
    Node *node = get_helper_path2d_node();
    if (node == nullptr) {
        if (!quiet) UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: Path2D mode has no node assigned (helper_path2d_path).");
        return PackedVector2Array();
    }
    Path2D *path = Object::cast_to<Path2D>(node);
    if (path == nullptr) {
        if (!quiet) UtilityFunctions::push_error("BulletSpawner2D: Path2D mode needs a Path2D node.");
        return PackedVector2Array();
    }
    Ref<Curve2D> curve = path->get_curve();
    if (curve.is_null()) {
        if (!quiet) UtilityFunctions::push_warning("BulletSpawner2D: Path2D mode curve has no Curve2D.");
        return PackedVector2Array();
    }
    PackedVector2Array pts = curve->get_baked_points();
    if (pts.is_empty()) {
        pts = curve->tessellate();
    }
    for (int i = 0; i < pts.size(); ++i) {
        if (!pts[i].is_finite()) {
            if (!quiet) UtilityFunctions::push_warning("BulletSpawner2D: Path2D curve contains non-finite points; volley skipped.");
            return PackedVector2Array();
        }
    }
    if (pts.size() < 2) {
        if (!quiet) UtilityFunctions::push_warning("BulletSpawner2D: Path2D curve needs at least 2 baked points.");
        return PackedVector2Array();
    }
    // Follow Generator (default): the curve SHAPE is copied onto the
    // generator, resettled with its bounding-box center at the origin. Raw
    // baked points carry wherever they were drawn in path-local space, so
    // without this the volley would inherit the Path2D's old local offset
    // instead of blooming around the generator. Only the shape survives;
    // the node's own transform is ignored outright. Tree membership is
    // irrelevant on this path.
    if (helper_path2d_space == PATH2D_SPACE_FOLLOW_GENERATOR) {
        Vector2 mn = pts[0];
        Vector2 mx = pts[0];
        for (int i = 1; i < pts.size(); ++i) {
            mn.x = MIN(mn.x, pts[i].x);
            mn.y = MIN(mn.y, pts[i].y);
            mx.x = MAX(mx.x, pts[i].x);
            mx.y = MAX(mx.y, pts[i].y);
        }
        const Vector2 center = (mn + mx) * 0.5;
        if (center.is_finite() && center.length_squared() > 0.0) {
            for (int i = 0; i < pts.size(); ++i) {
                pts[i] -= center;
            }
        }
        return pts;
    }
    // At Path2D (legacy): express the curve in the generator's space, which
    // lands the volley where the node sits in the world. A degenerate link
    // (including a singular generator whose inverse is garbage) falls back
    // to raw points instead of NaN.
    Node2D *base = get_effective_generator();
    Node2D *path_2d = Object::cast_to<Node2D>(node);
    if (base != nullptr && path_2d != nullptr && path_2d != base && is_inside_tree() && path_2d->is_inside_tree() && DirectionalBullets2D::is_transform_invertible_safe(base->get_global_transform())) {
        const Transform2D base_inv = base->get_global_transform().affine_inverse();
        const Transform2D node_global = path_2d->get_global_transform();
        if (base_inv.is_finite() && node_global.is_finite()) {
            const Transform2D to_marker = base_inv * node_global;
            if (to_marker.is_finite()) {
                for (int i = 0; i < pts.size(); ++i) {
                    pts[i] = to_marker.xform(pts[i]);
                }
            }
        }
    }
    return pts;
}

// Arc-length layout along a generator-local polyline with tangent-first
// facing. Crash-safe by construction: every segment access is bounds-checked
// (ia/ib clamped into [0, n)), zero-length segments fall back to the nearest
// valid tangent, and degenerate curves (total <= 0) stack every slot at the
// first finite point with marker-relative fallback facing. Positions come out
// in global space (marker.xform), exactly like the factory edge generator,
// so the preview and the spin/scale passes downstream stay consistent.
TypedArray<Transform2D> BulletSpawner2D::collect_path2d_transforms(const Transform2D &marker, const PackedVector2Array &path_pts, int count, bool quiet) const {
    TypedArray<Transform2D> out;
    if (count <= 0) {
        return out;
    }
    const int n = path_pts.size();
    if (n < 2) {
        if (!quiet) UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: Path2D mode produced no points.");
        return out;
    }
    // Marker scale rides along (same as every factory generator); rotation
    // composes below. Guard once: a non-finite marker poisons everything.
    if (!marker.is_finite()) {
        if (!quiet) UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: Path2D mode marker transform is not finite.");
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
    if (helper_path2d_closed && n > 2) {
        const double raw_closing = (double)path_pts[n - 1].distance_to(path_pts[0]);
        if (Math::is_finite(raw_closing) && raw_closing > 1e-9) {
            closing_len = raw_closing;
            total += closing_len;
        }
    }
    const bool loop = closing_len > 0.0;
    const int seg_count = loop ? n : n - 1;
    // A curve drawn as a loop (last point ON the first) needs no closing
    // stretch, but with helper_path2d_closed it is still a ring: spreading
    // with the open n-1 divisor put the last bullet on the first.
    const bool ring = loop || (helper_path2d_closed && n > 2);
    real_t facing_extra = 0.0;
    if (helper_path2d_facing == PATH2D_FACING_NORMAL_P90) {
        facing_extra = Math::PI * 0.5;
    } else if (helper_path2d_facing == PATH2D_FACING_NORMAL_M90) {
        facing_extra = -Math::PI * 0.5;
    }
    const real_t facing_offset = facing_extra + Math::deg_to_rad((real_t)helper_path2d_facing_offset_deg);
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
    if (helper_path2d_distribution == PATH2D_DISTRIBUTION_EVEN) {
        // Even cover of the whole curve. Loops wrap as a ring (no duplicated
        // seam bullet); open paths include both endpoints. start_offset
        // phase-rotates a loop, or pushes an open run toward the end (clamped).
        double phase = Math::is_finite(helper_path2d_start_offset) ? helper_path2d_start_offset : 0.0;
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
        double eff_spacing = helper_path2d_spacing;
        if (!(eff_spacing > 0.0) || !Math::is_finite(eff_spacing)) {
            if (!quiet) UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: Path2D spacing is not usable.");
            return TypedArray<Transform2D>();
        }
        const double run = (double)(count - 1) * eff_spacing;
        if (helper_path2d_overflow == PATH2D_OVERFLOW_SHRINK_TO_FIT && count > 1 && run > total) {
            // A ring has `count` gaps (the seam is one of them); an open run
            // has count - 1 and keeps both endpoints.
            eff_spacing = ring ? total / (double)count : total / (double)(count - 1);
        }
        const double eff_run = (double)(count - 1) * eff_spacing;
        double base = 0.0;
        if (helper_path2d_anchor == PATH2D_ANCHOR_CENTER) {
            base = (total - eff_run) * 0.5;
        } else if (helper_path2d_anchor == PATH2D_ANCHOR_END) {
            base = total - eff_run;
        }
        if (base < 0.0) {
            base = 0.0; // run still longer (clamp/wrap handle it below)
        }
        for (int i = 0; i < count; ++i) {
            const double raw = base + (double)i * eff_spacing + helper_path2d_start_offset;
            if (helper_path2d_overflow == PATH2D_OVERFLOW_WRAP) {
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
            if (helper_path2d_distribution == PATH2D_DISTRIBUTION_EVEN && ring) {
                d = Math::fposmod(d, total);
            } else if (helper_path2d_distribution != PATH2D_DISTRIBUTION_EVEN && helper_path2d_overflow == PATH2D_OVERFLOW_WRAP) {
                d = Math::fposmod(d, total);
            } else {
                d = total;
            }
        }
        // Exact mirror: d == total (curve end, End anchor / Clamp pile-up)
        // must land on total, NOT wrap to 0 — the old fposmod() here pinned
        // bullet 0 at the start while everything else mirrored.
        if (helper_path2d_reverse) {
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
        if (helper_path2d_reverse) {
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

// Predictive lead shared by the aimed volley and its preview cone: blends
// the live position toward where the target will be after prediction_time at
// its current velocity (CharacterBody2D-style get_velocity; anything else
// aims live). Both paths must agree or the preview cone would point
// somewhere the volley never flies.
Vector2 BulletSpawner2D::predict_target_pos(Node2D *target) const {
    Vector2 aim_pos = target->get_global_transform().get_origin();
    if (helper_aimed_prediction > 0.0 && helper_aimed_prediction_time > 0.0) {
        Vector2 target_vel = Vector2(0, 0);
        bool has_vel = false;
        if (target->has_method("get_velocity")) {
            Variant v = target->call("get_velocity");
            if (v.get_type() == Variant::VECTOR2) {
                target_vel = v;
                has_vel = target_vel.is_finite();
            }
        }
        if (has_vel) {
            aim_pos += target_vel * (real_t)(helper_aimed_prediction_time * helper_aimed_prediction);
        }
    }
    return aim_pos;
}

// Corridor aim resolution shared by the corridor volley and its preview
// wall: prefers the live aimed target (like the Aimed case), falls back to
// the static aim direction when no target is assigned. Callers validate the
// fallback themselves, exactly as before.
Vector2 BulletSpawner2D::resolve_corridor_aim(const Vector2 &fallback, const Vector2 &origin) const {
    if (Node2D *target = get_helper_aimed_target()) {
        const Vector2 to_target = target->get_global_transform().get_origin() - origin;
        if (to_target.is_finite() && to_target.length_squared() > 0.0) {
            return to_target.normalized();
        }
    }
    return fallback;
}

TypedArray<Transform2D> BulletSpawner2D::collect_spawn_transforms() const {
    return collect_spawn_transforms_impl(false);
}

TypedArray<Transform2D> BulletSpawner2D::generate_raw_pattern(Node2D *base, const Transform2D &marker, real_t mirror_sign, bool quiet) const {
    TypedArray<Transform2D> raw;
    switch (pattern_source) {
        case PATTERN_FROM_SELF:
            raw.push_back(marker);
            break;
        case PATTERN_FROM_CHILDREN: {
            bool collected = false;
            for (int i = 0; i < base->get_child_count(); ++i) {
                Node2D *as_2d = Object::cast_to<Node2D>(base->get_child(i));
                // The editor preview holder is a Node2D child too, but it is
                // visualization only and must never become a spawn marker.
                if (as_2d != nullptr && !as_2d->has_meta(PREVIEW_META_KEY)) {
                    raw.push_back(as_2d->get_global_transform());
                    collected = true;
                }
            }
            if (!collected) {
                raw.push_back(marker);
            }
            break;
        }
        case PATTERN_FROM_HELPER_GRID:
            raw = BulletFactory2D::helper_generate_transforms_grid(helper_bullets_amount, marker, helper_grid_rows_per_column, (BulletFactory2D::Alignment)helper_grid_alignment, helper_grid_column_offset, helper_grid_row_offset, helper_grid_rotate_with_marker, helper_grid_random_local_rotation, helper_grid_jitter, helper_grid_seed > 0 ? (uint64_t)helper_grid_seed : 0);
            break;
        case PATTERN_FROM_HELPER_RING:
            raw = BulletFactory2D::helper_generate_transforms_ring(helper_bullets_amount, marker, helper_ring_radius, helper_ring_start_angle, helper_ring_arc, helper_ring_rotate_with_marker, helper_ring_random_rotation, helper_ring_face_outward, helper_ring_y_scale, helper_ring_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_ring_seed > 0 ? (uint64_t)helper_ring_seed : 0, helper_outline_layer_layout);
            break;
        case PATTERN_FROM_HELPER_FAN:
            raw = BulletFactory2D::helper_generate_transforms_fan(helper_bullets_amount, marker, helper_fan_spread, helper_fan_direction_angle, helper_fan_step_offset, helper_fan_centered, helper_fan_angle_jitter, helper_fan_seed > 0 ? (uint64_t)helper_fan_seed : 0);
            break;
        case PATTERN_FROM_HELPER_SPIRAL:
            // True chirality: negating angle_step winds the spiral the other
            // way, which is what "mirror" promises. The old code only negated
            // the emitter spin, so a mirrored spiral still wound identically.
            raw = BulletFactory2D::helper_generate_transforms_spiral(helper_bullets_amount, marker, helper_spiral_start_radius, helper_spiral_radius_step, helper_spiral_angle_step * mirror_sign, helper_spiral_rotate_with_marker, (BulletFactory2D::SpiralFacingMode)helper_spiral_facing, helper_spiral_facing_offset_deg);
            break;
        case PATTERN_FROM_HELPER_LINE: {
            // helper_line_perpendicular retired: helper_line_facing rotates
            // the composed facing instead. Order ops mirror/rotate the row,
            // then the facing turns, then the axis shift applies on top
            // (direction is global-space, like the generator uses it).
            TypedArray<Transform2D> line_raw = BulletFactory2D::helper_generate_transforms_line(helper_bullets_amount, marker, helper_line_direction, helper_line_spacing, helper_line_face_direction, (BulletFactory2D::LineAnchor)helper_line_anchor, false);
            const int ln = line_raw.size();
            const int facing_sel = (helper_line_facing >= 0 && helper_line_facing <= 2) ? helper_line_facing : 0;
            const real_t line_sel = facing_sel == 1 ? Math::PI * 0.5 : (facing_sel == 2 ? -Math::PI * 0.5 : 0.0);
            Vector2 line_axis = Vector2(1, 0);
            if (helper_line_direction.is_finite() && helper_line_direction.length_squared() > 1e-12) {
                line_axis = helper_line_direction.normalized();
            }
            const Vector2 line_shift = line_axis * (real_t)MAX(helper_line_start_offset, 0.0);
            for (int i = 0; i < ln; ++i) {
                int j = helper_line_reverse ? (ln - 1 - i) : i;
                if (ln > 1) {
                    int k = helper_line_slot_offset % ln;
                    if (k < 0) {
                        k += ln;
                    }
                    j = helper_line_reverse ? (ln - 1 - ((i + k) % ln)) : ((i + k) % ln);
                }
                Transform2D slot = line_raw[j];
                if (!slot.is_finite()) {
                    continue;
                }
                const Vector2 shifted = slot.get_origin() + line_shift;
                if (!shifted.is_finite()) {
                    continue;
                }
                slot.set_origin(shifted);
                const real_t rot = slot.get_rotation() + line_sel;
                if (Math::is_finite((double)rot)) {
                    slot.set_rotation(rot);
                }
                raw.push_back(slot);
            }
            break;
        }
        case PATTERN_FROM_HELPER_AIMED: {
            Node2D *target = get_helper_aimed_target();
            if (target == nullptr) {
                if (!quiet) {
                    UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: no aimed target assigned (helper_aimed_target_path).");
                }
                break;
            }
            // Predictive lead via the shared helper so the volley and the
            // preview cone always agree on where the target will be.
            Vector2 aim_pos = predict_target_pos(target);
            raw = BulletFactory2D::helper_generate_transforms_aimed(helper_bullets_amount, marker, aim_pos, helper_aimed_spread, helper_aimed_step_offset, helper_aimed_centered);
            break;
        }
        case PATTERN_FROM_HELPER_FLOWER:
            raw = BulletFactory2D::helper_generate_transforms_flower(helper_bullets_amount, marker, helper_flower_petals, helper_flower_radius, helper_flower_petal_spread, helper_flower_petal_sharpness, helper_flower_base_rotation, helper_flower_face_outward, helper_flower_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_flower_type, helper_flower_inner_radius_scale, helper_flower_spiro_roller, helper_flower_spiro_pen, helper_flower_super_lobes, helper_flower_super_fullness, helper_outline_layer_layout);
            break;
        case PATTERN_FROM_HELPER_ELLIPSE:
            raw = BulletFactory2D::helper_generate_transforms_ellipse(helper_bullets_amount, marker, helper_ellipse_radius_x, helper_ellipse_radius_y, helper_ellipse_rotation, helper_ellipse_start_angle, helper_ellipse_arc, (BulletFactory2D::EllipseMode)helper_ellipse_mode, helper_ellipse_gap_count, helper_ellipse_gap_width, helper_ellipse_face_outward, helper_ellipse_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_layer_layout);
            break;
        case PATTERN_FROM_HELPER_RAIN:
            raw = BulletFactory2D::helper_generate_transforms_rain(helper_bullets_amount, marker, helper_rain_band_width, helper_rain_direction, helper_rain_drop_spacing, helper_rain_jitter, helper_rain_seed > 0 ? (uint64_t)helper_rain_seed : 0);
            break;
        case PATTERN_FROM_HELPER_SCATTER: {
            uint64_t scatter_seed = helper_scatter_seed > 0 ? (uint64_t)helper_scatter_seed : 0;
            if (quiet && scatter_seed == 0) {
                // Preview stability: an unseeded layout re-rolls on every
                // collect, and the preview re-collects on every tracked
                // change (spin sweeps included), so a live seed would make
                // the dots jump chaotically instead of rotating coherently.
                // Pin the preview to one representative layout; live volleys
                // keep per-shot randomness.
                scatter_seed = 0x5CA77E5u;
            }
            raw = BulletFactory2D::helper_generate_transforms_scatter(helper_bullets_amount, marker, helper_scatter_burst_radius, helper_scatter_facing_jitter, scatter_seed, helper_scatter_inner_radius, helper_scatter_direction, helper_scatter_arc, (BulletFactory2D::ScatterFacingMode)helper_scatter_facing);
            break;
        }
        case PATTERN_FROM_HELPER_STAR_POLYGON:
            raw = BulletFactory2D::helper_generate_transforms_star_polygon(helper_bullets_amount, marker, helper_star_polygon_vertices, helper_star_polygon_radius, helper_star_polygon_vertex_bias, helper_star_polygon_base_rotation, helper_star_polygon_face_outward, helper_star_polygon_facing_offset_deg);
            break;
        case PATTERN_FROM_HELPER_MULTISPIRAL:
            raw = BulletFactory2D::helper_generate_transforms_multispiral(helper_bullets_amount, marker, helper_multispiral_arms, helper_multispiral_start_radius, helper_multispiral_radius_step, helper_multispiral_angle_step * mirror_sign, helper_multispiral_rotate_with_marker, (BulletFactory2D::SpiralFacingMode)helper_multispiral_facing, helper_multispiral_facing_offset_deg, helper_multispiral_arm_stride);
            break;
        case PATTERN_FROM_HELPER_CROSS:
            raw = BulletFactory2D::helper_generate_transforms_cross(helper_bullets_amount, marker, helper_cross_arm_count, helper_cross_arm_length, helper_cross_spacing, helper_cross_base_rotation, helper_cross_face_outward, helper_cross_facing_offset_deg);
            break;
        case PATTERN_FROM_HELPER_STAR:
            raw = BulletFactory2D::helper_generate_transforms_star(helper_bullets_amount, marker, helper_star_points, helper_star_outer_radius, helper_star_inner_radius, helper_star_base_rotation, helper_star_face_outward, helper_star_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_FROM_HELPER_HEART:
            raw = BulletFactory2D::helper_generate_transforms_heart(helper_bullets_amount, marker, helper_heart_size, helper_heart_base_rotation, helper_heart_face_outward, helper_heart_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_layer_layout);
            break;
        case PATTERN_FROM_HELPER_WAVE:
            raw = BulletFactory2D::helper_generate_transforms_wave(helper_bullets_amount, marker, helper_wave_width, helper_wave_amplitude, helper_wave_waves, helper_wave_direction, helper_wave_face_direction, helper_wave_facing_offset_deg);
            break;
        case PATTERN_FROM_HELPER_WATERFALL:
            // Each side is capped in its setter; the product is checked here
            // (load order) with one warning, then the shot has no slots.
            if ((int64_t)helper_waterfall_columns * (int64_t)helper_waterfall_rows > (int64_t)kMaxGridSlots) {
                if (!quiet) {
                    WarnOnce2D::warn(get_instance_id(), kWarnGridTooLarge, helper_waterfall_columns, helper_waterfall_rows, String("BulletSpawner2D: helper_waterfall_columns * helper_waterfall_rows exceeds ") + itos(kMaxGridSlots) + " slots; lower them.");
                }
                break;
            }
            raw = BulletFactory2D::helper_generate_transforms_waterfall(helper_bullets_amount, marker, helper_waterfall_columns, helper_waterfall_column_spacing, helper_waterfall_rows, helper_waterfall_row_spacing, helper_waterfall_stagger, helper_waterfall_rain_direction, helper_waterfall_jitter, helper_waterfall_facing_offset_deg, helper_waterfall_seed > 0 ? (uint64_t)helper_waterfall_seed : 0);
            break;
        case PATTERN_FROM_HELPER_LATTICE:
            // Each side is capped in its setter; the product is checked here
            // (load order) with one warning, then the shot has no slots.
            if ((int64_t)helper_lattice_columns * (int64_t)helper_lattice_rows > (int64_t)kMaxGridSlots) {
                if (!quiet) {
                    WarnOnce2D::warn(get_instance_id(), kWarnGridTooLarge, helper_lattice_columns, helper_lattice_rows, String("BulletSpawner2D: helper_lattice_columns * helper_lattice_rows exceeds ") + itos(kMaxGridSlots) + " slots; lower them.");
                }
                break;
            }
            raw = BulletFactory2D::helper_generate_transforms_lattice(helper_bullets_amount, marker, helper_lattice_columns, helper_lattice_rows, helper_lattice_spacing_x, helper_lattice_spacing_y, helper_lattice_stagger_rows, helper_lattice_face_outward, helper_lattice_facing_offset_deg);
            break;
        case PATTERN_FROM_HELPER_ROSE:
            raw = BulletFactory2D::helper_generate_transforms_rose(helper_bullets_amount, marker, helper_rose_petals, helper_rose_radius, helper_rose_lobe_sharpness, helper_rose_base_rotation, helper_rose_face_outward, helper_rose_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_layer_layout);
            break;
        case PATTERN_FROM_HELPER_COUNTER_SPIRAL:
            raw = BulletFactory2D::helper_generate_transforms_counter_spiral(helper_bullets_amount, marker, helper_counter_spiral_arms, helper_counter_spiral_start_radius, helper_counter_spiral_radius_step, helper_counter_spiral_angle_step * mirror_sign, helper_counter_spiral_rotate_with_marker, (BulletFactory2D::SpiralFacingMode)helper_counter_spiral_facing, helper_counter_spiral_facing_offset_deg, helper_counter_spiral_arm_stride, helper_counter_spiral_mirror_alternate_arms);
            break;
        case PATTERN_FROM_HELPER_CORRIDOR: {
            // Corridor is aimed by design (AIMED_TRAP preset flows through
            // here): prefer the live target like the Aimed case, fall back to
            // the static aim direction when no target is assigned or the
            // spawner runs outside the tree (preview still shows the wall).
            const Vector2 corridor_aim = resolve_corridor_aim(helper_corridor_aim_direction, marker.get_origin());
            // Width and gap are set independently (scene load order must not
            // matter). A door as wide as the wall is clamped to half the
            // width here, with one warning per (gap, width) pair.
            double corridor_gap = helper_corridor_gap_width;
            if (corridor_gap >= helper_corridor_width) {
                corridor_gap = helper_corridor_width * 0.5;
                if (!quiet) {
                    WarnOnce2D::warn(get_instance_id(), kWarnCorridorGap, (int64_t)(helper_corridor_gap_width * 1000.0), (int64_t)(helper_corridor_width * 1000.0),
                            "BulletSpawner2D: helper_corridor_gap_width must be smaller than helper_corridor_width; using half the width for the door.");
                }
            }
            raw = BulletFactory2D::helper_generate_transforms_corridor(helper_bullets_amount, marker, corridor_aim, helper_corridor_width, 32.0, corridor_gap, helper_corridor_face_aim, helper_corridor_facing_offset_deg); // spacing reserved (unused) upstream: factory default
            break;
        }
        case PATTERN_FROM_HELPER_LISSAJOUS:
            raw = BulletFactory2D::helper_generate_transforms_lissajous(helper_bullets_amount, marker, helper_lissajous_size_x, helper_lissajous_size_y, helper_lissajous_freq_x, helper_lissajous_freq_y, helper_lissajous_phase, helper_lissajous_face_outward, helper_lissajous_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_layer_layout);
            break;
        case PATTERN_FROM_HELPER_CUSTOM: {
            // Hand-placed transforms: every stored slot spawns exactly where
            // the user put it (generator-local, composed as marker * local so
            // spin and scales keep working). Order ops mirror/rotate the
            // array, then the facing selector rewrites rotations. Non-finite
            // entries can never spawn safely, so they are skipped (the setter
            // already rejects them wholesale; this is belt-and-braces for
            // scenes saved by older builds).
            const int cn = helper_custom_transforms.size();
            if (cn <= 0) {
                if (!quiet) UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: helper_custom_transforms is empty.");
                break;
            }
            if (!marker.is_finite()) {
                if (!quiet) UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: Custom mode marker transform is not finite.");
                break;
            }
            const int custom_sel = (helper_custom_facing >= 0 && helper_custom_facing <= 4) ? helper_custom_facing : 0;
            const real_t custom_offset = Math::is_finite(helper_custom_facing_offset_deg) ? Math::deg_to_rad((real_t)helper_custom_facing_offset_deg) : 0.0;
            for (int i = 0; i < cn; ++i) {
                int j = helper_custom_reverse ? (cn - 1 - i) : i;
                if (cn > 1) {
                    int k = helper_custom_slot_offset % cn;
                    if (k < 0) {
                        k += cn;
                    }
                    j = helper_custom_reverse ? (cn - 1 - ((i + k) % cn)) : ((i + k) % cn);
                }
                const Variant stored = (j >= 0 && j < helper_custom_transforms.size()) ? helper_custom_transforms[j] : Variant();
                if (stored.get_type() != Variant::TRANSFORM2D) {
                    continue;
                }
                Transform2D local = stored;
                if (!local.is_finite()) {
                    continue;
                }
                real_t rot = local.get_rotation();
                if (custom_sel != 0) {
                    const Vector2 radial = local.get_origin();
                    const double ra = (radial.is_finite() && radial.length_squared() > 1e-12) ? radial.angle() : 0.0;
                    if (custom_sel == 1) {
                        rot = (real_t)ra;
                    } else if (custom_sel == 2) {
                        rot = (real_t)ra + Math::PI;
                    } else if (custom_sel == 3) {
                        rot = local.get_rotation() + Math::PI * 0.5;
                    } else {
                        rot = local.get_rotation() - Math::PI * 0.5;
                    }
                }
                rot += custom_offset;
                if (!Math::is_finite((double)rot)) {
                    continue;
                }
                local.set_rotation(rot);
                Transform2D slot = marker * local;
                if (!slot.is_finite()) {
                    continue;
                }
                raw.push_back(slot);
            }
            break;
        }
        case PATTERN_FROM_HELPER_CIRCLE:
            raw = BulletFactory2D::helper_generate_transforms_circle(helper_bullets_amount, marker, (real_t)helper_circle_radius, helper_circle_face_outward, (real_t)helper_circle_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_layer_layout);
            break;
        case PATTERN_FROM_HELPER_RECTANGLE:
            raw = BulletFactory2D::helper_generate_transforms_rectangle(helper_bullets_amount, marker, helper_rectangle_size, helper_rectangle_face_outward, (real_t)helper_rectangle_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_FROM_HELPER_SQUARE:
            raw = BulletFactory2D::helper_generate_transforms_rectangle(helper_bullets_amount, marker, Vector2((real_t)helper_square_size, (real_t)helper_square_size), helper_square_face_outward, (real_t)helper_square_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_FROM_HELPER_POLYGON:
            raw = BulletFactory2D::helper_generate_transforms_polygon(helper_bullets_amount, marker, helper_polygon_vertices, (real_t)helper_polygon_radius, (real_t)helper_polygon_rotation, helper_polygon_face_outward, (real_t)helper_polygon_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_FROM_HELPER_TRIANGLE:
            raw = BulletFactory2D::helper_generate_transforms_triangle(helper_bullets_amount, marker, (BulletFactory2D::TriangleType)helper_triangle_type, helper_triangle_size_a, helper_triangle_size_b, helper_triangle_rotation, helper_triangle_face_outward, helper_triangle_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_FROM_HELPER_TRAPEZOID:
            raw = BulletFactory2D::helper_generate_transforms_trapezoid(helper_bullets_amount, marker, helper_trapezoid_base_top, helper_trapezoid_base_bottom, helper_trapezoid_height, helper_trapezoid_rotation, helper_trapezoid_face_outward, helper_trapezoid_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_FROM_HELPER_DIAMOND:
            raw = BulletFactory2D::helper_generate_transforms_diamond(helper_bullets_amount, marker, helper_diamond_diagonal_x, helper_diamond_diagonal_y, helper_diamond_rotation, helper_diamond_face_outward, helper_diamond_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_FROM_HELPER_PATH2D: {
            // Standalone live-curve layout (no spray rig): bullets sit ON the
            // baked curve, spaced per helper_path2d_distribution, facing
            // tangent-first per helper_path2d_facing. One tree read per
            // volley, so drawn terrain that animates just works.
            PackedVector2Array path_pts = sample_path2d_polyline(quiet);
            if (path_pts.is_empty()) {
                break; // the sampler already reported the exact cause
            }
            raw = collect_path2d_transforms(marker, path_pts, helper_bullets_amount, quiet);
            break;
        }
        default: {
            // Unknown source (corrupt scene int, version skew): fail loud so
            // a dead mode can never hide as Children behavior again.
            if (!quiet) {
                UtilityFunctions::push_error(String("BulletSpawner2D::collect_spawn_transforms: unknown pattern_source ") + itos((int)pattern_source) + ", falling back to the generator itself.");
            }
            raw.push_back(marker);
            break;
        }
    }
    return raw;
}

// ---- Pattern bake cache ----------------------------------------------------

bool BulletSpawner2D::debug_pattern_cache_verify = false;

void BulletSpawner2D::mark_pattern_dirty() {
    ++pattern_version;
}

void BulletSpawner2D::on_pattern_changed() {
    mark_pattern_dirty();
    if (pattern_batch_depth > 0) {
        pattern_batch_dirty = true;
        return;
    }
    rebuild_preview();
}

void BulletSpawner2D::begin_pattern_batch() {
    ++pattern_batch_depth;
}

void BulletSpawner2D::end_pattern_batch() {
    if (pattern_batch_depth > 0 && --pattern_batch_depth == 0 && pattern_batch_dirty) {
        pattern_batch_dirty = false;
        rebuild_preview();
    }
}

// Own properties a pattern-list entry or preset may touch: helper_*, spin_*,
// pattern_source (+ spawn_data for entries). Read from ClassDB so new knobs
// are covered without a hand-kept list.
static void pattern_state_property_names(bool include_spawn_data, bool reset_only, std::vector<StringName> &r_names) {
    r_names.clear();
    const TypedArray<Dictionary> props = ClassDBSingleton::get_singleton()->class_get_property_list("BulletSpawner2D", true);
    for (int i = 0; i < props.size(); ++i) {
        const Dictionary p = props[i];
        const String name = p.get("name", String());
        const int usage = (int)p.get("usage", 0);
        if ((usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) != 0) {
            continue;
        }
        const bool knob = name.begins_with("helper_") || name.begins_with("spin_") || name == "pattern_source";
        if (!knob && !(include_spawn_data && name == "spawn_data")) {
            continue;
        }
        if (reset_only) {
            // Presets keep wiring (node paths, user arrays) and the Transform
            // subgroup (skip indices; the scales/offset are not helper_*).
            const int type = (int)p.get("type", 0);
            if (type != Variant::INT && type != Variant::FLOAT && type != Variant::BOOL && type != Variant::VECTOR2) {
                continue;
            }
        }
        r_names.push_back(StringName(name));
    }
}

Dictionary BulletSpawner2D::snapshot_pattern_state() const {
    std::vector<StringName> names;
    pattern_state_property_names(true, false, names);
    Dictionary out;
    for (const StringName &name : names) {
        out[name] = get(name);
    }
    return out;
}

void BulletSpawner2D::restore_pattern_state(const Dictionary &snapshot) {
    begin_pattern_batch();
    const Array keys = snapshot.keys();
    for (int i = 0; i < keys.size(); ++i) {
        const StringName name = keys[i];
        const Variant saved = snapshot[name];
        if (get(name) != saved) {
            set(name, saved);
        }
    }
    end_pattern_batch();
}

void BulletSpawner2D::reset_pattern_knobs_to_defaults() {
    std::vector<StringName> names;
    pattern_state_property_names(false, true, names);
    ClassDBSingleton *class_db = ClassDBSingleton::get_singleton();
    begin_pattern_batch();
    for (const StringName &name : names) {
        const Variant def = class_db->class_get_property_default_value("BulletSpawner2D", name);
        if (def.get_type() != Variant::NIL && get(name) != def) {
            set(name, def);
        }
    }
    end_pattern_batch();
}

// Sources whose raw transforms read state OTHER than the marker + the
// spawner's own properties (child nodes, a target, a Path2D curve, a user
// array that can be mutated in place): always regenerated. Everything else is
// classified by probing (see classify_pattern_motion).
static bool pattern_source_reads_external_state(int source) {
    switch (source) {
        case BulletSpawner2D::PATTERN_FROM_CHILDREN:
        case BulletSpawner2D::PATTERN_FROM_HELPER_AIMED:
        case BulletSpawner2D::PATTERN_FROM_HELPER_CORRIDOR:
        case BulletSpawner2D::PATTERN_FROM_HELPER_CUSTOM:
        case BulletSpawner2D::PATTERN_FROM_HELPER_PATH2D:
            return true;
        default:
            return false;
    }
}

// Rotation + translation only (unit columns, orthogonal, det > 0).
static bool basis_is_rigid(const Transform2D &t, real_t eps = (real_t)1e-5) {
    const Vector2 x = t.columns[0];
    const Vector2 y = t.columns[1];
    return Math::abs(x.length_squared() - 1.0) <= eps && Math::abs(y.length_squared() - 1.0) <= eps &&
            Math::abs(x.dot(y)) <= eps && (x.x * y.y - x.y * y.x) > 0.0;
}

static bool basis_equal(const Transform2D &a, const Transform2D &b, real_t eps = (real_t)1e-6) {
    return (a.columns[0] - b.columns[0]).length_squared() <= eps * eps && (a.columns[1] - b.columns[1]).length_squared() <= eps * eps;
}

// Transform equality for cache parity: origins within a pixel-fraction,
// basis columns (rotation AND scale) within a tight relative tolerance.
static bool transforms_match(const Transform2D &a, const Transform2D &b) {
    if (!a.is_finite() || !b.is_finite()) {
        return a.is_finite() == b.is_finite();
    }
    const real_t origin_tol = (real_t)1e-3 + (real_t)1e-5 * MAX(a.columns[2].length(), b.columns[2].length());
    if ((a.columns[2] - b.columns[2]).length() > origin_tol) {
        return false;
    }
    for (int k = 0; k < 2; ++k) {
        const real_t tol = (real_t)1e-4 * MAX((real_t)1.0, a.columns[k].length());
        if ((a.columns[k] - b.columns[k]).length() > tol) {
            return false;
        }
    }
    return true;
}

static bool raw_sets_match(const std::vector<Transform2D> &a, const TypedArray<Transform2D> &b, int *r_first_bad = nullptr) {
    if ((int)a.size() != b.size()) {
        if (r_first_bad) {
            *r_first_bad = -1;
        }
        return false;
    }
    for (int i = 0; i < (int)a.size(); ++i) {
        if (!transforms_match(a[i], b[i])) {
            if (r_first_bad) {
                *r_first_bad = i;
            }
            return false;
        }
    }
    return true;
}

int BulletSpawner2D::classify_pattern_motion(Node2D *base, const Transform2D &marker, real_t mirror_sign, bool quiet, const std::vector<Transform2D> &raw) const {
    if (pattern_cache_mode == PATTERN_CACHE_OFF || pattern_source_reads_external_state(pattern_source)) {
        return PATTERN_MOTION_NONE;
    }
    if (raw.empty() || !marker.is_finite() || !DirectionalBullets2D::is_transform_invertible_safe(marker)) {
        return PATTERN_MOTION_NONE;
    }
    // Probe 1: a rigid motion with an awkward angle + offset. A generator
    // whose output follows (positions AND facings) is RIGID. Unseeded
    // randomness re-rolls between the two calls and fails here by itself.
    const Transform2D rigid_probe(0.7311, Vector2(37.25, -19.5));
    const TypedArray<Transform2D> probe_raw = generate_raw_pattern(base, rigid_probe * marker, mirror_sign, true);
    if (probe_raw.size() == (int)raw.size()) {
        bool ok = true;
        for (int i = 0; i < (int)raw.size() && ok; ++i) {
            ok = transforms_match(rigid_probe * raw[i], probe_raw[i]);
        }
        if (ok) {
            return PATTERN_MOTION_RIGID;
        }
    }
    // Probe 2: pure translation (world-direction patterns like rain).
    const Transform2D shift_probe(0.0, Vector2(41.5, -23.75));
    const TypedArray<Transform2D> shift_raw = generate_raw_pattern(base, shift_probe * marker, mirror_sign, true);
    if (shift_raw.size() == (int)raw.size()) {
        bool ok = true;
        for (int i = 0; i < (int)raw.size() && ok; ++i) {
            ok = transforms_match(shift_probe * raw[i], shift_raw[i]);
        }
        if (ok) {
            return PATTERN_MOTION_TRANSLATION;
        }
    }
    (void)quiet;
    return PATTERN_MOTION_NONE;
}

void BulletSpawner2D::resolve_raw_pattern(Node2D *base, const Transform2D &marker, real_t mirror_sign, bool quiet, std::vector<Transform2D> &r_raw) const {
    r_raw.clear();
    const bool cache_on = pattern_cache_mode == PATTERN_CACHE_AUTO && !pattern_cache_bypass;
    PatternBake &bake = pattern_bakes[quiet ? 2 : (mirror_sign < 0.0 ? 1 : 0)];
    if (cache_on && bake.valid && bake.version == pattern_version && bake.motion_class != PATTERN_MOTION_NONE) {
        bool served = false;
        if (bake.motion_class == PATTERN_MOTION_RIGID) {
            const Transform2D delta = marker * bake.marker_inv;
            if (delta.is_finite() && basis_is_rigid(delta)) {
                r_raw.resize(bake.raw.size());
                for (size_t i = 0; i < bake.raw.size(); ++i) {
                    r_raw[i] = delta * bake.raw[i];
                }
                served = true;
            }
        } else if (basis_equal(marker, bake.marker)) {
            const Vector2 shift = marker.columns[2] - bake.marker.columns[2];
            r_raw = bake.raw;
            for (Transform2D &t : r_raw) {
                t.columns[2] += shift;
            }
            served = true;
        }
        if (served) {
            ++pattern_cache_hits;
            if (debug_pattern_cache_verify) {
                pattern_cache_bypass = true;
                const TypedArray<Transform2D> fresh = generate_raw_pattern(base, marker, mirror_sign, true);
                pattern_cache_bypass = false;
                int bad = 0;
                if (!raw_sets_match(r_raw, fresh, &bad)) {
                    String detail = bad < 0 ? String("size ") + itos((int64_t)r_raw.size()) + " vs " + itos(fresh.size())
                                            : String("slot ") + itos(bad) + ": cached " + String(Variant(r_raw[bad])) + " vs fresh " + String(Variant(fresh[bad]));
                    UtilityFunctions::push_error(String("BulletSpawner2D: pattern cache mismatch (pattern_source ") + itos(pattern_source) + ", class " + itos(bake.motion_class) + ") " + detail);
                }
            }
            return;
        }
    }
    // Miss: generate, then (re)bake. A version change re-probes the class;
    // a marker change outside the reuse rule just re-anchors the bake.
    ++pattern_cache_misses;
    const TypedArray<Transform2D> fresh = generate_raw_pattern(base, marker, mirror_sign, quiet);
    const int n = fresh.size();
    r_raw.resize(n);
    for (int i = 0; i < n; ++i) {
        r_raw[i] = fresh[i];
    }
    if (!cache_on) {
        return;
    }
    if (!bake.valid || bake.version != pattern_version) {
        bake.motion_class = classify_pattern_motion(base, marker, mirror_sign, quiet, r_raw);
        bake.version = pattern_version;
        bake.valid = true;
    }
    if (bake.motion_class == PATTERN_MOTION_NONE) {
        bake.raw.clear();
        return;
    }
    bake.marker = marker;
    bake.marker_inv = marker.affine_inverse();
    bake.raw = r_raw;
    ++pattern_cache_bakes;
}

bool BulletSpawner2D::preview_survives_marker_move(const Transform2D &old_marker, const Transform2D &new_marker) const {
    const PatternBake &bake = pattern_bakes[2];
    if (pattern_cache_mode != PATTERN_CACHE_AUTO || !bake.valid || bake.version != pattern_version) {
        return false;
    }
    if (!old_marker.is_finite() || !new_marker.is_finite()) {
        return false;
    }
    if (bake.motion_class == PATTERN_MOTION_RIGID) {
        if (!DirectionalBullets2D::is_transform_invertible_safe(old_marker)) {
            return false;
        }
        return basis_is_rigid(new_marker * old_marker.affine_inverse());
    }
    if (bake.motion_class == PATTERN_MOTION_TRANSLATION) {
        return basis_equal(old_marker, new_marker);
    }
    return false;
}

int BulletSpawner2D::get_pattern_cache_mode() const {
    return pattern_cache_mode;
}

void BulletSpawner2D::set_pattern_cache_mode(int value) {
    if (value < PATTERN_CACHE_AUTO || value > PATTERN_CACHE_OFF) {
        UtilityFunctions::push_error("BulletSpawner2D: pattern_cache_mode must be 0 (Auto) or 1 (Off), keeping the old value.");
        return;
    }
    pattern_cache_mode = value;
    mark_pattern_dirty();
}

Dictionary BulletSpawner2D::debug_get_pattern_cache_info() const {
    Dictionary d;
    d["hits"] = (int64_t)pattern_cache_hits;
    d["misses"] = (int64_t)pattern_cache_misses;
    d["bakes"] = (int64_t)pattern_cache_bakes;
    d["version"] = (int64_t)pattern_version;
    const PatternBake &shot = pattern_bakes[0];
    const PatternBake &prev = pattern_bakes[2];
    d["shot_class"] = (shot.valid && shot.version == pattern_version) ? shot.motion_class : -1;
    d["preview_class"] = (prev.valid && prev.version == pattern_version) ? prev.motion_class : -1;
    // Spawn-data duplicate cache (shoot_once reuses one spawner-owned copy;
    // resource swaps and in-place edits invalidate it).
    d["template_valid"] = cached_volley_template.is_valid();
    const uint64_t live_id = spawn_data.is_valid() ? spawn_data->get_instance_id() : 0;
    d["spawn_id_match"] = cached_volley_template.is_valid() && cached_spawn_data_id == live_id && live_id != 0;
    return d;
}

TypedArray<Transform2D> BulletSpawner2D::debug_collect_spawn_transforms_uncached() const {
    pattern_cache_bypass = true;
    const TypedArray<Transform2D> out = collect_spawn_transforms_impl(false);
    pattern_cache_bypass = false;
    return out;
}

void BulletSpawner2D::debug_set_pattern_cache_verify(bool enabled) {
    debug_pattern_cache_verify = enabled;
}

bool BulletSpawner2D::debug_get_pattern_cache_verify() {
    return debug_pattern_cache_verify;
}

void BulletSpawner2D::collect_spawn_transforms_native(bool quiet, std::vector<Transform2D> &r_out) const {
    r_out.clear();
    Node2D *base = get_effective_generator();
    if (base == nullptr || !base->is_inside_tree()) {
        // Outside the tree there is no valid global transform; report loudly
        // for real shots, stay silent for the preview.
        if (!quiet) {
            UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: spawner is outside the scene tree.");
        }
        return;
    }
    const Vector2 base_origin = base->get_global_transform().get_origin();
    const Transform2D marker = base->get_global_transform();
    // Burst mirror sign, applied to the generators' WINDING (angle_step) as
    // well as the emitter spin below. -1 reverses a spiral/multispiral/
    // counter-spiral so the arms sweep the other way, which is what
    // "mirror" actually promises; the previous code only negated the spin, so
    // a mirrored spiral wound identically and merely pointed the opposite
    // way. Always 1 outside a mirrored burst shot, so every other path is
    // bit-identical to before.
    const real_t mirror_sign = (burst_alternate_mirror && burst_mirror_next) ? (real_t)-1.0 : (real_t)1.0;
    std::vector<Transform2D> raw;
    resolve_raw_pattern(base, marker, mirror_sign, quiet, raw);
    // Spin first, then scale: both pivot around the generator origin, so a
    // spinning emitter orbits positions and turns facings together while the
    // scale pass keeps working exactly as before (identity at 1.0).
    // Burst mirror flips chirality every other burst volley (fan/spiral
    // rhythm without scripting): negate the spin contribution for this
    // volley only — the stored spin_angle_deg keeps advancing untouched.
    // preview_suppress_spin is set while snapshotting the gizmo so the
    // geometry is stored unspun and the layer can apply the angle at draw
    // time instead (see PatternPreviewLayer2D::posed). Without this, an
    // advancing spin angle changed the geometry every frame and forced a
    // full 10k-element rebuild per frame.
    real_t spin_radians = 0.0;
    if (!preview_suppress_spin) {
        spin_radians = Math::deg_to_rad((real_t)spin_angle_deg);
        if (burst_alternate_mirror && burst_mirror_next) {
            spin_radians = -spin_radians;
        }
    }
    // Finite-guarded: a corrupted scale degrades to identity instead of
    // NaN-poisoning the whole volley (setters reject non-finite values, this
    // covers hand-built state).
    const real_t pattern_factor = Math::is_finite(pattern_scale) ? (real_t)pattern_scale : 1.0f;
    const real_t local_factor = Math::is_finite(transforms_scale) ? (real_t)transforms_scale : 1.0f;
    // Spin + pattern scale folded into one matrix (see make_spin_scale_matrix):
    // one trig pair per volley, exact for mirrored/sheared bases. Trivial
    // or degenerate inputs leave the slot raw: trivial is already correct,
    // and degenerate inputs stay visible to the downstream finite guards
    // (which fail loud) instead of being silently rewritten.
    bool spin_scale_ok = false;
    const Transform2D spin_scale_mx = make_spin_scale_matrix(base_origin, spin_radians, pattern_factor, spin_scale_ok);
    const bool use_spin_scale_mx = spin_scale_ok && !spin_is_trivial(spin_radians, pattern_factor);
    // Negative space (post spin/scale so indexes match the preview): carve
    // dodge doors or bullet text out of any helper layout. Same contract as
    // BulletFactory2D::helper_apply_skip_indices (OOB ignored, warned once).
    const int raw_count = (int)raw.size();
    skip_mask_scratch.assign(raw_count, 0);
    bool any_skip = false;
    if (!helper_skip_indices.is_empty() && pattern_source >= PATTERN_FROM_HELPER_GRID) {
        bool warned_oob = false;
        const int32_t *skip_p = helper_skip_indices.ptr();
        for (int k = 0; k < helper_skip_indices.size(); ++k) {
            const int idx = skip_p[k];
            if (idx < 0 || idx >= raw_count) {
                // Once per (pattern version, bullet count), never from the
                // preview: the shot path would otherwise warn every volley.
                if (!warned_oob && !quiet) {
                    WarnOnce2D::warn(get_instance_id(), kWarnSkipIndexOutOfRange, (int64_t)pattern_version, raw_count,
                            "BulletSpawner2D: helper_skip_indices holds an index outside 0.." + itos(raw_count - 1) + "; it is ignored.");
                }
                warned_oob = true;
                continue;
            }
            skip_mask_scratch[idx] = 1;
            any_skip = true;
        }
    }
    r_out.reserve(raw_count);
    for (int i = 0; i < raw_count; ++i) {
        if (any_skip && skip_mask_scratch[i]) {
            continue;
        }
        Transform2D t = raw[i];
        if (use_spin_scale_mx) {
            t = spin_scale_mx * t;
        }
        // Per-bullet size last: basis only, origins untouched, so skipped
        // indexes below still match the preview.
        // Plain column scaling: set_scale(get_scale() * f) normalizes the
        // columns and re-applies a signed y scale, which flips mirrored
        // (det < 0) bases back and drops shear.
        if (local_factor != 1.0f) {
            t.columns[0] *= local_factor;
            t.columns[1] *= local_factor;
        }
        r_out.push_back(t);
    }
}

TypedArray<Transform2D> BulletSpawner2D::collect_spawn_transforms_impl(bool quiet) const {
    // Script-facing form: one boxing pass at the very end (the shot path uses
    // collect_spawn_transforms_native directly and never boxes).
    collect_spawn_transforms_native(quiet, collect_scratch);
    TypedArray<Transform2D> transforms;
    transforms.resize((int)collect_scratch.size());
    for (int i = 0; i < (int)collect_scratch.size(); ++i) {
        transforms[i] = collect_scratch[i];
    }
    return transforms;
}

} // namespace BlastBullets2D
