// BulletSpawner2D (split from bullet_spawner2d.cpp, same class):
// Pattern preview: snapshot + rebuild, zero-cost spin/move pose, dirty
// detection and the debug_* coincidence bindings used by tests.

#include "bullet_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Cone strip shared by the fan/aimed preview tracks: two boundary rays from
// the origin plus the tip arc connecting them, closed back at the origin.
// Appends marker-GLOBAL points in draw order; the caller feeds each through
// the spin/scale pipeline. Preconditions (checked by callers): finite spread
// > 0 and tip_radius > 0.
static void build_cone_strip(const Vector2 &origin, real_t dir_angle, real_t half_spread, real_t tip_radius, real_t spread, PackedVector2Array &r_out) {
    const real_t left_ang = dir_angle + half_spread;
    const real_t right_ang = dir_angle - half_spread;
    const Vector2 left_dir = Vector2(Math::cos(left_ang), Math::sin(left_ang));
    const Vector2 right_dir = Vector2(Math::cos(right_ang), Math::sin(right_ang));
    const Vector2 left_tip = origin + left_dir * tip_radius;
    const Vector2 right_tip = origin + right_dir * tip_radius;
    // Left ray: origin -> tip.
    r_out.push_back(origin);
    r_out.push_back(left_tip);
    // Arc from left tip to right tip (across the spread).
    const int arc_n = Math::clamp((int)(tip_radius * spread / 8.0), 8, 64);
    for (int i = 1; i < arc_n; i++) {
        const real_t t = (real_t)i / (real_t)arc_n;
        const real_t ang = left_ang + (right_ang - left_ang) * t;
        r_out.push_back(origin + Vector2(Math::cos(ang), Math::sin(ang)) * tip_radius);
    }
    // Right ray: tip -> origin (closes the cone visually).
    r_out.push_back(right_tip);
    r_out.push_back(origin);
}

bool BulletSpawner2D::get_show_pattern_preview() const {
    return show_pattern_preview;
}

void BulletSpawner2D::set_show_pattern_preview(bool value) {
    show_pattern_preview = value;
    // _validate_property hides preview_* knobs while both toggles are off.
    notify_property_list_changed();
    update_preview_process_state();
    on_pattern_changed();
}

bool BulletSpawner2D::get_show_preview_during_runtime() const {
    return show_preview_during_runtime;
}

void BulletSpawner2D::set_show_preview_during_runtime(bool value) {
    show_preview_during_runtime = value;
    notify_property_list_changed();
    // Runtime preview piggy-backs the shooting/spin/retarget loop: enabling
    // it mid-game while that loop sleeps must wake it, or the preview builds
    // once here and never refreshes marker moves afterwards.
    if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
        refresh_process_state();
    }
    update_preview_process_state();
    on_pattern_changed();
}

// Preview colours: a NaN channel would poison the draw (reject-and-keep
// like every other knob). Colours never touch geometry, so their setters
// rebuild the preview without bumping the pattern version (bake kept).
static bool preview_color_ok(const Color &value, const char *name) {
    if (Math::is_finite(value.r) && Math::is_finite(value.g) && Math::is_finite(value.b) && Math::is_finite(value.a)) {
        return true;
    }
    UtilityFunctions::push_error(String("BulletSpawner2D: ") + name + " must be a finite colour, keeping the old value.");
    return false;
}

Color BulletSpawner2D::get_preview_dot_color() const {
    return preview_dot_color;
}

void BulletSpawner2D::set_preview_dot_color(const Color &value) {
    if (!preview_color_ok(value, "preview_dot_color")) {
        return;
    }
    preview_dot_color = value;
    rebuild_preview(); // appearance only: the pattern bake stays valid
}

Color BulletSpawner2D::get_preview_arrow_color() const {
    return preview_arrow_color;
}

void BulletSpawner2D::set_preview_arrow_color(const Color &value) {
    if (!preview_color_ok(value, "preview_arrow_color")) {
        return;
    }
    preview_arrow_color = value;
    rebuild_preview(); // appearance only: the pattern bake stays valid
}

Color BulletSpawner2D::get_preview_first_dot_color() const {
    return preview_first_dot_color;
}

void BulletSpawner2D::set_preview_first_dot_color(const Color &value) {
    if (!preview_color_ok(value, "preview_first_dot_color")) {
        return;
    }
    preview_first_dot_color = value;
    rebuild_preview(); // appearance only: the pattern bake stays valid
}

Color BulletSpawner2D::get_preview_path_color() const {
    return preview_path_color;
}

void BulletSpawner2D::set_preview_path_color(const Color &value) {
    if (!preview_color_ok(value, "preview_path_color")) {
        return;
    }
    preview_path_color = value;
    rebuild_preview(); // appearance only: the pattern bake stays valid
}

Color BulletSpawner2D::get_preview_layer_path_color() const {
    return preview_layer_path_color;
}

void BulletSpawner2D::set_preview_layer_path_color(const Color &value) {
    if (!preview_color_ok(value, "preview_layer_path_color")) {
        return;
    }
    preview_layer_path_color = value;
    rebuild_preview(); // appearance only: the pattern bake stays valid
}

double BulletSpawner2D::get_preview_path_width() const {
    return preview_path_width;
}

void BulletSpawner2D::set_preview_path_width(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_path_width must be finite and >= 0 (0 hides the track), keeping the old value.");
        return;
    }
    preview_path_width = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_preview_dot_radius() const {
    return preview_dot_radius;
}

void BulletSpawner2D::set_preview_dot_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_dot_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_dot_radius = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_preview_arrow_gap() const {
    return preview_arrow_gap;
}

void BulletSpawner2D::set_preview_arrow_gap(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_arrow_gap must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_arrow_gap = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_preview_arrow_length() const {
    return preview_arrow_length;
}

void BulletSpawner2D::set_preview_arrow_length(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_arrow_length must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_arrow_length = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_preview_arrow_width() const {
    return preview_arrow_width;
}

void BulletSpawner2D::set_preview_arrow_width(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_arrow_width must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_arrow_width = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_preview_arrow_head_length() const {
    return preview_arrow_head_length;
}

void BulletSpawner2D::set_preview_arrow_head_length(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_arrow_head_length must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_arrow_head_length = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_preview_arrow_head_width() const {
    return preview_arrow_head_width;
}

void BulletSpawner2D::set_preview_arrow_head_width(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_arrow_head_width must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_arrow_head_width = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_preview_draw_collision_rings() const {
    return preview_draw_collision_rings;
}

void BulletSpawner2D::set_preview_draw_collision_rings(bool value) {
    preview_draw_collision_rings = value;
    on_pattern_changed();
}

Color BulletSpawner2D::get_preview_collision_ring_color() const {
    return preview_collision_ring_color;
}

void BulletSpawner2D::set_preview_collision_ring_color(const Color &value) {
    if (!preview_color_ok(value, "preview_collision_ring_color")) {
        return;
    }
    preview_collision_ring_color = value;
    rebuild_preview(); // appearance only: the pattern bake stays valid
}

double BulletSpawner2D::get_preview_collision_ring_width() const {
    return preview_collision_ring_width;
}

void BulletSpawner2D::set_preview_collision_ring_width(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_collision_ring_width must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_collision_ring_width = value;
    on_pattern_changed();
}

Array BulletSpawner2D::debug_get_layer_rings() const {
    return preview_last_layer_rings.duplicate();
}

PackedVector2Array BulletSpawner2D::debug_get_preview_dot_points() const {
    if (preview_dots_layer == nullptr) {
        return PackedVector2Array();
    }
    return preview_dots_layer->dots;
}

PackedVector2Array BulletSpawner2D::debug_get_preview_track_points() const {
    if (preview_dots_layer == nullptr) {
        return PackedVector2Array();
    }
    return preview_dots_layer->path_points;
}

// Debugging function: proves dots sit on the drawn geometry (base track or
// yellow rings — same center-scale rule on both sides). Point-to-segment
// match against the holder-local base track plus every stored ring: exact on
// sharp shapes, sub-pixel on dense sweeps. Because pairing is purely
// geometric, twist, caps and fill deals can never confuse it. Large
// deviation = factory/preview disagree (stale snapshot).
Dictionary BulletSpawner2D::debug_check_layer_coincidence(double tolerance_px) const {
    Dictionary out;
    out["checked"] = false;
    out["layers"] = 0;
    out["max_deviation_px"] = -1.0;
    out["mean_deviation_px"] = -1.0;
    out["ok"] = false;
    if (preview_dots_layer == nullptr) {
        return out;
    }
    const PackedVector2Array dots = preview_dots_layer->dots;
    if (dots.is_empty()) {
        return out;
    }
    if (!supports_outline_layout(pattern_source)
            || helper_outline_placement != (int)BulletFactory2D::OUTLINE_LAYERS) {
        return out;
    }
    // Candidate segments: base track runs plus every stored ring run.
    // Runs split on INF separators; a run closes only when it is the sole
    // run and the track is flagged closed (multi-run tracks like flower
    // petals stay open). Every run boundary gets an explicit separator so
    // no phantom segment can bridge base into rings (or rings into each
    // other) and fake a near-match.
    PackedVector2Array guides;
    {
        const PackedVector2Array base = preview_dots_layer->path_points;
        Array base_runs;
        {
            PackedVector2Array cur;
            for (int k = 0; k < base.size(); ++k) {
                if (!base[k].is_finite()) {
                    if (cur.size() >= 1) {
                        base_runs.push_back(cur);
                    }
                    cur.clear();
                    continue;
                }
                cur.push_back(base[k]);
            }
            if (cur.size() >= 1) {
                base_runs.push_back(cur);
            }
        }
        const bool close_base = preview_dots_layer->path_closed && base_runs.size() == 1;
        for (int r = 0; r < base_runs.size(); ++r) {
            const PackedVector2Array run = base_runs[r];
            for (int k = 0; k < run.size(); ++k) {
                guides.push_back(run[k]);
            }
            if (close_base && run.size() >= 2 && run[0].is_finite()) {
                guides.push_back(run[0]);
            }
            guides.push_back(Vector2(Math::INF, Math::INF));
        }
    }
    for (int i = 0; i < preview_last_layer_rings.size(); ++i) {
        const PackedVector2Array ring = preview_last_layer_rings[i];
        for (int k = 0; k < ring.size(); ++k) {
            guides.push_back(ring[k]);
        }
        guides.push_back(Vector2(Math::INF, Math::INF));
    }
    double max_dev = 0.0;
    double sum_dev = 0.0;
    int counted = 0;
    for (int i = 0; i < dots.size(); ++i) {
        if (!dots[i].is_finite()) {
            continue;
        }
        double best_d2 = 1e30;
        for (int k = 0; k + 1 < guides.size(); ++k) {
            const Vector2 a = guides[k];
            const Vector2 b = guides[k + 1];
            if (!a.is_finite() || !b.is_finite()) {
                continue;
            }
            const Vector2 ab = b - a;
            const double len_sq = (double)ab.length_squared();
            double t = (len_sq > 1e-12) ? (double)(dots[i] - a).dot(ab) / len_sq : 0.0;
            t = Math::clamp(t, 0.0, 1.0);
            const double d2 = dots[i].distance_squared_to(a + ab * (real_t)t);
            if (d2 < best_d2) {
                best_d2 = d2;
            }
        }
        if (!(best_d2 < 1e30) || !Math::is_finite(best_d2)) {
            continue;
        }
        const double dev = Math::sqrt(best_d2);
        max_dev = MAX(max_dev, dev);
        sum_dev += dev;
        counted++;
    }
    if (counted <= 0) {
        return out;
    }
    out["checked"] = true;
    out["layers"] = preview_last_layer_rings.size();
    out["max_deviation_px"] = max_dev;
    out["mean_deviation_px"] = sum_dev / (double)counted;
    out["ok"] = max_dev <= (double)tolerance_px;
    return out;
}

Dictionary BulletSpawner2D::debug_get_preview_stats() const {
    Dictionary d;
    d["rebuilds"] = (int64_t)preview_rebuild_count;
    d["last_rebuild_usec"] = (int64_t)preview_last_rebuild_usec;
    d["dots_draws"] = preview_dots_layer != nullptr ? preview_dots_layer->debug_draw_count : 0;
    d["arrows_draws"] = preview_arrows_layer != nullptr ? preview_arrows_layer->debug_draw_count : 0;
    return d;
}

bool BulletSpawner2D::preview_allowed_here() const {
    if (!is_inside_tree()) {
        return false;
    }
    if (Engine::get_singleton()->is_editor_hint()) {
        return true;
    }
    return show_preview_during_runtime;
}

bool BulletSpawner2D::preview_active() const {
    return show_pattern_preview && preview_allowed_here();
}

// Re-poses the existing preview snapshot for a new spin angle. This is the
// cheap path an advancing spin takes instead of rebuild_preview(): the
// geometry is unchanged, only its rotation changed, so the layers just need a
// redraw. Burst mirror never reaches here (a mirrored volley is a real shot,
// and the flag is only set around the firing path), so the plain angle is
// correct for the gizmo.
void BulletSpawner2D::set_preview_pose(double spin_angle_degrees) {
    if (!Math::is_finite(spin_angle_degrees)) {
        return;
    }
    // Keep the snapshot in step so a later rebuild reproduces the same pose
    // instead of treating the current angle as fresh geometry.
    tracked_spin_angle = spin_angle_degrees;
    if (preview_holder == nullptr || (preview_dots_layer == nullptr && preview_arrows_layer == nullptr)) {
        return;
    }
    // The shot spins about the generator origin in GLOBAL space; the layers
    // live in holder space (holder global == generator global). The same
    // motion expressed locally is Hb^-1 * R(theta) * Hb (Hb = holder basis):
    // R(theta) for rotation + uniform scale, R(-theta) for a mirrored
    // generator, and an exact (non-rotation) matrix for skew / non-uniform
    // scale. Applied as the layers' node transform: no redraw, O(1).
    Transform2D pose;
    const real_t radians = Math::deg_to_rad((real_t)spin_angle_degrees);
    if (radians != 0.0) {
        Transform2D hb = preview_holder->get_global_transform();
        hb.columns[2] = Vector2();
        if (hb.is_finite() && BulletVolley2D::is_transform_invertible_safe(hb)) {
            pose = hb.affine_inverse() * Transform2D(radians, Vector2()) * hb;
        } else {
            pose = Transform2D(radians, Vector2());
        }
    }
    // Muzzle offset: the shot shifts every bullet by a world vector AFTER
    // spin, so the gizmo shifts the posed layer by the same vector expressed
    // in holder space (GLOBAL: Hb^-1 * offset, LOCAL: the generator-frame
    // offset itself).
    if (spawn_position_offset != Vector2(0, 0)) {
        Vector2 shift = spawn_position_offset;
        if (spawn_position_offset_space == SPAWN_OFFSET_GLOBAL) {
            Transform2D hb = preview_holder->get_global_transform();
            hb.columns[2] = Vector2();
            if (hb.is_finite() && BulletVolley2D::is_transform_invertible_safe(hb)) {
                shift = hb.affine_inverse().xform(spawn_position_offset);
            }
        }
        if (shift.is_finite()) {
            pose.columns[2] = shift;
        }
    }
    if (preview_dots_layer != nullptr) {
        preview_dots_layer->set_pose(pose);
    }
    if (preview_arrows_layer != nullptr) {
        preview_arrows_layer->set_pose(pose);
    }
}

// Snapshots the effective generator (same fallback as the collect path) plus
// the aimed target, storing ids and global transforms. Never assumes an old
// pointer: everything is freshly resolved from paths here.
// Deferred flush for editor-coalesced rebuilds (see rebuild_preview): runs
// the build synchronously exactly once, bypassing re-queue.
void BulletSpawner2D::_do_queued_preview_rebuild() {
    preview_rebuild_queued = false;
    if (!preview_allowed_here()) {
        return;
    }
    preview_sync_rebuild = true;
    rebuild_preview();
    preview_sync_rebuild = false;
}

void BulletSpawner2D::snapshot_preview_sources() {
    tracked_has_self = true;
    tracked_base = get_effective_generator();
    if (tracked_base == nullptr) {
        tracked_base_id = 0;
        tracked_has_base_global = false;
    } else {
        tracked_base_id = tracked_base->get_instance_id();
        tracked_base_global = tracked_base->get_global_transform();
        tracked_has_base_global = true;
    }
    tracked_spin_angle = spin_angle_deg;
    tracked_target_id = 0;
    tracked_has_target_origin = false;
    if (pattern_source == PATTERN_FROM_HELPER_AIMED || pattern_source == PATTERN_FROM_HELPER_CORRIDOR) {
        Node2D *target = get_helper_aimed_target();
        if (target != nullptr) {
            tracked_target_id = target->get_instance_id();
            tracked_target_origin = target->get_global_transform();
            tracked_has_target_origin = true;
        }
    }
    // Custom tracking: snapshot the stored array for the dirty check below
    // (an inspector edit swaps the whole array, so size + compare catches
    // it). Path2D re-samples live (curve edits and node motion both change
    // the compiled points, so both are caught).
    tracked_custom_transforms.clear();
    if (pattern_source == PATTERN_FROM_HELPER_CUSTOM) {
        // Deep copy, never an assignment: TypedArray assignment only shares
        // (COW), and Array::clear() does NOT duplicate before wiping, so a
        // shared snapshot would let the clear() above destroy the user's
        // placed transforms on the next rebuild (silent data loss + dead
        // spawner). Bounded by the setter cap, so the copy stays cheap.
        tracked_custom_transforms = helper_custom_transforms.duplicate();
    } else if (pattern_source == PATTERN_FROM_HELPER_PATH2D) {
        tracked_custom_transforms.clear();
        const PackedVector2Array live_pts = sample_path2d_polyline(true);
        for (int i = 0; i < live_pts.size(); ++i) {
            tracked_custom_transforms.push_back(Transform2D(0.0, live_pts[i]));
        }
        // Node identity + transform for the gated dirty check below (it
        // resamples sparingly and compares the cheap node state in between).
        Node *path_node = get_helper_path2d_node();
        tracked_path2d_node_id = path_node != nullptr ? path_node->get_instance_id() : 0;
        if (Node2D *path_2d = Object::cast_to<Node2D>(path_node)) {
            tracked_path2d_node_global = path_2d->get_global_transform();
            tracked_has_path2d_node_global = true;
        } else {
            tracked_has_path2d_node_global = false;
        }
        preview_path2d_sample_cooldown = 0;
    }
    tracked_marker_origins.clear();
    tracked_marker_rots.clear();
    tracked_marker_ids.clear();
    tracked_child_count = -1;
    if (!is_inside_tree() || tracked_base == nullptr) {
        return;
    }
    // Marker slots: origin + rotation + id per child index. A non-Node2D
    // child and a freed-then-reused slot both compare unequal (dirty), which
    // triggers a rebuild that re-resolves - never a dereference of garbage.
    tracked_child_count = tracked_base->get_child_count();
    tracked_marker_origins.resize(tracked_child_count);
    tracked_marker_rots.resize(tracked_child_count);
    // Two int64 entries per child (low/high 32 bits): instance ids don't fit
    // a double bit-exactly, and float packing would alias distinct objects.
    tracked_marker_ids.resize(tracked_child_count * 2);
    for (int i = 0; i < tracked_child_count; i++) {
        Node2D *marker = Object::cast_to<Node2D>(tracked_base->get_child(i));
        if (marker == nullptr) {
            tracked_marker_origins[i] = Vector2(Math::INF, Math::INF);
            tracked_marker_rots[i] = Math::INF;
            tracked_marker_ids[i * 2] = 0;
            tracked_marker_ids[i * 2 + 1] = 0;
            continue;
        }
        tracked_marker_origins[i] = marker->get_global_transform().get_origin();
        tracked_marker_rots[i] = marker->get_global_rotation();
        const uint64_t id = marker->get_instance_id();
        tracked_marker_ids[i * 2] = (int64_t)(id & 0xFFFFFFFFu);
        tracked_marker_ids[i * 2 + 1] = (int64_t)(id >> 32);
    }
}

void BulletSpawner2D::rebuild_preview() {
    // Preview may exist in the editor (toggle decides) and at runtime only
    // when the user opted in via show_preview_during_runtime. Never before
    // the node is inside the tree (scene load sets properties first).
    if (!preview_allowed_here()) {
        return;
    }
    // Editor coalescing: slider drags and scene-load restoration fire dozens
    // of setter rebuilds (up to 10k dots each) per second. In the editor
    // only, collapse them to one deferred rebuild per frame. Runtime stays
    // synchronous so script readback after a setter is exact the same frame.
    // The bypass flag below lets the deferred flush build immediately
    // instead of re-queuing forever.
    if (Engine::get_singleton()->is_editor_hint() && is_inside_tree() && !preview_rebuild_in_progress && !preview_sync_rebuild) {
        if (!preview_rebuild_queued) {
            preview_rebuild_queued = true;
            callable_mp(this, &BulletSpawner2D::_do_queued_preview_rebuild).call_deferred();
        }
        return;
    }
    // Nested entry (holder/layer add_child firing NOTIFICATION_CHILD_ORDER_CHANGED
    // synchronously, undo/redo bursts, transform notices mid-build) is always
    // redundant — the outer pass re-reads everything — so bail instead of
    // recursing into a stack overflow.
    if (preview_rebuild_in_progress) {
        return;
    }
    preview_rebuild_in_progress = true;
    ++preview_rebuild_count;
    const uint64_t rebuild_t0 = Time::get_singleton()->get_ticks_usec();
    // Crash-safety: the effective generator is resolved from a cached
    // pointer that may dangle after the target node is freed. get_transforms_generator()
    // already validates its cache via ObjectDB before touching it, so the
    // pointer returned here is safe to use. Never add manual instance-id
    // checks that dereference first — that was the old editor crash.
    // Same base the shots use (get_effective_generator: the generator when
    // it is in the tree, else this spawner), so the gizmo can never sit
    // under a node the volleys ignore.
    Node2D *effective_base = is_inside_tree() ? get_effective_generator() : nullptr;
    if (effective_base == nullptr) {
        effective_base = const_cast<BulletSpawner2D *>(this);
    }
    if (effective_base == nullptr) {
        preview_rebuild_in_progress = false;
        return;
    }
    // Removes a preview holder child from a parent without trusting caches.
    // Returns true when something was removed.
    auto remove_holder_from = [&](Node *parent) -> bool {
        if (parent == nullptr) {
            return false;
        }
        Node *live = parent->get_node_or_null(NodePath(PREVIEW_HOLDER_NAME));
        if (live == nullptr) {
            return false;
        }
        parent->remove_child(live);
        // memdelete (not queue_free) is REQUIRED here: the caller recreates
        // the holder under the same name immediately below, and a queued
        // node would still resolve by name until the flush (name clash +
        // reuse of a dying holder). This runs on the preview path only
        // (never physics callbacks), so immediate deletion is safe.
        memdelete(live);
        return true;
    };
    if (!show_pattern_preview) {
        remove_holder_from(this);
        if (effective_base != this) {
            remove_holder_from(effective_base);
        }
        // The generator may have been retargeted earlier: also clear a holder
        // left under the previously tracked base.
        if (tracked_base != nullptr && tracked_base != this && tracked_base != effective_base &&
                is_tracked_node_alive(tracked_base, tracked_base_id)) {
            remove_holder_from(tracked_base);
        }
        preview_holder = nullptr;
        preview_dots_layer = nullptr;
        preview_arrows_layer = nullptr;
        preview_rebuild_in_progress = false;
        return;
    }
    // Preview lives under the effective generator so patterns are drawn where
    // they spawn. Heal from the tree every rebuild: never trust cached
    // pointers (undo/redo, duplication, scene reload can drop nodes).
    preview_holder = Object::cast_to<Node2D>(effective_base->get_node_or_null(NodePath(PREVIEW_HOLDER_NAME)));
    if (preview_holder == nullptr) {
        // Generator was retargeted: drop the stale holder under the spawner
        // (or under the previously tracked base) so only one preview exists.
        remove_holder_from(this);
        if (tracked_base != nullptr && tracked_base != this && tracked_base != effective_base &&
                is_tracked_node_alive(tracked_base, tracked_base_id)) {
            remove_holder_from(tracked_base);
        }
        preview_holder = Object::cast_to<Node2D>(effective_base->get_node_or_null(NodePath(PREVIEW_HOLDER_NAME)));
    }
    if (preview_holder == nullptr) {
        // Owner-less on purpose: never written to the scene, never exported.
        preview_holder = memnew(Node2D);
        preview_holder->set_name(PREVIEW_HOLDER_NAME);
        preview_holder->set_meta(PREVIEW_META_KEY, true);
        effective_base->add_child(preview_holder);
    } else if (preview_holder->get_parent() != effective_base) {
        // Healed a holder from the wrong parent (e.g. duplicated subtree).
        Node *old_parent = preview_holder->get_parent();
        if (old_parent != nullptr) {
            old_parent->remove_child(preview_holder);
        }
        effective_base->add_child(preview_holder);
    }
    // Cleanup first: older versions drew the preview with Line2D strips
    // ("Dots"/"Ticks"). Remove any leftover before adopting/creating the
    // plain Node2D layers, so a healed holder can never clash on child names
    // and upgraded scenes keep no stray-segment nodes around.
    TypedArray<Node> stray_lines = preview_holder->find_children("*", "Line2D", false, false);
    for (int i = 0; i < stray_lines.size(); i++) {
        Node *stray = Object::cast_to<Node>(stray_lines[i]);
        if (stray != nullptr) {
            preview_holder->remove_child(stray);
            memdelete(stray);
        }
    }
    // Layers are likewise re-resolved from the tree every rebuild, so an
    // externally removed layer is recreated instead of silently killing the
    // preview. Only 3 nodes total regardless of bullet count.
    preview_dots_layer = Object::cast_to<PatternPreviewLayer2D>(preview_holder->get_node_or_null(NodePath("Dots")));
    if (preview_dots_layer == nullptr) {
        // Wrong-type leftover (plain Node2D from the RenderingServer build):
        // drop it so the typed layer can take the canonical name.
        Node *old_dots = preview_holder->get_node_or_null(NodePath("Dots"));
        if (old_dots != nullptr) {
            preview_holder->remove_child(old_dots);
            memdelete(old_dots);
        }
        preview_dots_layer = memnew(PatternPreviewLayer2D);
        preview_dots_layer->set_name("Dots");
        preview_dots_layer->kind = PatternPreviewLayer2D::LAYER_DOTS;
        preview_dots_layer->set_z_index(kPreviewZIndex);
        preview_holder->add_child(preview_dots_layer);
    }
    preview_arrows_layer = Object::cast_to<PatternPreviewLayer2D>(preview_holder->get_node_or_null(NodePath("Arrows")));
    if (preview_arrows_layer == nullptr) {
        // Same wrong-type migration as Dots above.
        Node *old_arrows = preview_holder->get_node_or_null(NodePath("Arrows"));
        if (old_arrows != nullptr) {
            preview_holder->remove_child(old_arrows);
            memdelete(old_arrows);
        }
        preview_arrows_layer = memnew(PatternPreviewLayer2D);
        preview_arrows_layer->set_name("Arrows");
        preview_arrows_layer->kind = PatternPreviewLayer2D::LAYER_ARROWS;
        preview_arrows_layer->set_z_index(kPreviewZIndex);
        preview_holder->add_child(preview_arrows_layer);
    }
    // Quiet collect: the preview must visualize, never scold (e.g. aimed
    // without a target simply draws nothing instead of erroring per rebuild).
    // Snapshot the geometry UNSPUN; the layer applies spin_angle_deg at draw
    // time, so an advancing spin only re-poses the cached points instead of
    // rebuilding every transform (which at 10k bullets is a per-frame cliff).
    preview_suppress_spin = true;
    const TypedArray<Transform2D> transforms = collect_spawn_transforms_impl(true);
    preview_suppress_spin = false;
    const Transform2D holder_global = preview_holder->get_global_transform();
    if (!holder_global.is_finite()) {
        preview_rebuild_in_progress = false;
        return;
    }
    const Transform2D to_local = holder_global.affine_inverse();
    if (!to_local.is_finite()) {
        preview_rebuild_in_progress = false;
        return;
    }
    const real_t holder_rotation = holder_global.get_rotation();
    // Glyphs ride along with both scales (abs: negative scales mirror the
    // layout but must not invert dot/arrow sizes): pattern_scale grows the
    // layout, transforms_scale grows each bullet. Without this, a large
    // scale flings tiny fixed-size glyphs across the canvas and the preview
    // reads as "turned off". Non-finite scales fall back to 1.0.
    double glyph_scale = 1.0;
    if (Math::is_finite(pattern_scale) && Math::is_finite(transforms_scale)) {
        glyph_scale = Math::abs(pattern_scale * transforms_scale);
    } else if (Math::is_finite(pattern_scale)) {
        glyph_scale = Math::abs(pattern_scale);
    } else if (Math::is_finite(transforms_scale)) {
        glyph_scale = Math::abs(transforms_scale);
    }
    if (!(glyph_scale > 0.0) || !Math::is_finite(glyph_scale)) {
        glyph_scale = 1.0;
    }
    const real_t dot_radius = (real_t)(preview_dot_radius * glyph_scale);
    const real_t arrow_gap = (real_t)(preview_arrow_gap * glyph_scale);
    const real_t arrow_length = (real_t)(preview_arrow_length * glyph_scale);
    const real_t arrow_width = (real_t)(preview_arrow_width * glyph_scale);
    const real_t arrow_head_length = (real_t)(preview_arrow_head_length * glyph_scale);
    const real_t arrow_head_width = (real_t)(preview_arrow_head_width * glyph_scale);
    // Snapshot into the layers: _draw() repaints this data on every engine
    // redraw by itself, so zoom/pan/selection/idle can never wipe the gizmo.
    // (One-shot RenderingServer canvas_item_add_* calls cannot do this: the
    // engine owns the command list and drops it on the next repaint.)
    // Compacted snapshot: non-finite transforms are dropped (not left as
    // (0,0) entries, which would draw stray dots at the holder origin), so
    // dots/tails/dirs stay aligned by construction.
    PackedVector2Array dots;
    PackedVector2Array tails;
    PackedVector2Array dirs;
    for (int i = 0; i < transforms.size(); ++i) {
        const Transform2D t = transforms[i];
        // Skip non-finite transforms: feeding NaN/INF into canvas draw calls
        // crashes the editor's rendering server. A degenerate edge transform
        // is a misconfiguration, not something to draw.
        if (!t.is_finite()) continue;
        const Vector2 p = to_local.xform(t.get_origin());
        if (!p.is_finite()) continue;
        dots.push_back(p);
        // Holder-local facing: strip the holder rotation so moving/rotating
        // the spawner does not skew the arrow. The tail starts outside the
        // dot (radius + gap); _draw() builds the shaft + head from there.
        const Vector2 dir = Vector2(1.0, 0.0).rotated(t.get_rotation() - holder_rotation);
        tails.push_back(p + dir * (dot_radius + arrow_gap));
        dirs.push_back(dir);
    }
    preview_dots_layer->set_dots_data(dots, preview_dot_color, (float)dot_radius);
    preview_dots_layer->set_first_marker(!dots.is_empty(), preview_first_dot_color, kFirstDotRadiusScale);
    // The snapshot above is spin-free, so the rebuilt gizmo must be re-posed
    // to the current angle here (otherwise a rebuild during spin would snap
    // back to 0 until the next advance_spin).
    set_preview_pose(spin_angle_deg);
    // Collision-ring overlay: bounding radius from the volley's shape so
    // the editor shows hitbox vs visual. Off by default; invalid shapes hide.
    {
        float ring_r = 0.0f;
        if (preview_draw_collision_rings && spawn_data.is_valid() && spawn_data->collision_shape.is_valid()) {
            if (const CircleShape2D *c = Object::cast_to<CircleShape2D>(spawn_data->collision_shape.ptr())) {
                const float r = c->get_radius();
                if (r > 0.0f && Math::is_finite(r)) {
                    ring_r = r;
                }
            } else if (const RectangleShape2D *rc = Object::cast_to<RectangleShape2D>(spawn_data->collision_shape.ptr())) {
                const Vector2 s = rc->get_size();
                if (s.x > 0.0f && s.y > 0.0f && s.is_finite()) {
                    ring_r = 0.5f * (float)Math::min(s.x, s.y);
                }
            } else if (const CapsuleShape2D *cap = Object::cast_to<CapsuleShape2D>(spawn_data->collision_shape.ptr())) {
                const float h = cap->get_height();
                if (h > 0.0f && Math::is_finite(h)) {
                    ring_r = 0.5f * h;
                }
            }
            ring_r = (float)(ring_r * glyph_scale);
            if (!(ring_r > 0.0f) || !Math::is_finite(ring_r)) {
                ring_r = 0.0f;
            }
        }
        preview_dots_layer->set_rings_data(ring_r, preview_collision_ring_color, (float)preview_collision_ring_width);
    }
    // Track snapshot: the shape/loop/curve bullets ride on, drawn as
    // segments under the dots. Geometry-sourced (never bullet dots, except
    // Custom's explicit order strip), so sparse volleys still show the whole
    // track. Marker-local loop points run through the same spin/pattern
    // scale as volley positions, so the track always sits under the dots.
    PackedVector2Array track;
    bool track_closed = false;
    PackedVector2Array layer_track;
    // Marker-space loop (global, pre-spin/scale) for the layer rings below:
    // captured alongside the holder-local track so rings run through the
    // identical spin/scale pipeline as the dots.
    PackedVector2Array shape_loop;
    const bool capture_shape_loop = supports_outline_layout(pattern_source);
    // Stale-ring guard: cleared on EVERY rebuild, not just when the track
    // below builds. A dead generator / zero path width skips the block but
    // must not serve the previous rebuild's rings to debug_get_layer_rings()
    // and the coincidence check (which would then lie about dots on rings).
    preview_last_layer_rings.clear();
    {
        Node2D *track_base = get_effective_generator();
        const bool track_live = track_base != nullptr && is_inside_tree() && preview_path_width > 0.0 && Math::is_finite(preview_path_width);
        const Transform2D track_marker = track_live ? track_base->get_global_transform() : Transform2D();
        if (track_live && track_marker.is_finite()) {
            const Vector2 track_origin = track_marker.get_origin();
            // Unspun like the dots: the layer pose applies the spin. (This
            // used to read the live angle after preview_suppress_spin was
            // already reset, so tracks/rings were spun twice on any rebuild
            // during a runtime spin.)
            const real_t track_spin = 0.0;
            // Track strips are snapshotted spin-free like the dots, so the
            // layer's draw-time pose rotates them. Mirroring is a per-burst-shot
            // property of a REAL volley and never applies to the persistent
            // gizmo, so the samplers always use +1 here (a preview built
            // mid-burst shows the unmirrored pattern, which is the state the
            // next non-mirrored shot will use).
            const real_t preview_mirror_sign = (real_t)1.0;
            const double track_scale = Math::is_finite(pattern_scale) ? pattern_scale : 1.0;
            // Global point -> spin/scale around the generator origin (collect
            // parity) -> holder-local. Bad points drop their segment, never
            // the whole track. The xform core is shared so layer rings reuse
            // the exact pipeline with their own target array.
            auto xform_track_global = [&](const Vector2 &global_pt, PackedVector2Array &out) {
                if (!global_pt.is_finite()) {
                    return;
                }
                Vector2 g = track_origin + (global_pt - track_origin).rotated(track_spin);
                g = track_origin + (g - track_origin) * (real_t)track_scale;
                const Vector2 h = to_local.xform(g);
                if (h.is_finite()) {
                    out.push_back(h);
                }
            };
            auto push_track_global = [&](const Vector2 &global_pt) {
                xform_track_global(global_pt, track);
            };
            auto push_track_local = [&](const Vector2 &local_pt) {
                if (!local_pt.is_finite()) {
                    return;
                }
                push_track_global(track_marker.xform(local_pt));
            };
            // Factory sampler dict ({points, closed}) -> track. Most samplers
            // return marker-relative offsets (never baked globals); the track
            // applies the same origin-relative math the generators use
            // (origin + offset, plus marker spin/scale below). Shapes whose
            // generator treats the loop as marker-LOCAL (rectangle, square,
            // polygon, triangle, trapezoid, diamond, star: points_are_local
            // with rot_add = marker rotation) pass local_points = true, so
            // the track composes track_marker.xform like the volley does -
            // otherwise rotating the marker spins the dots but not the track.
            // The legacy "local" flag is ignored (grid/others disagree on its
            // meaning).
            // Samplers cap density internally; only Path2D curves stride
            // here. INF points are row/arm separators, not bad data: forward
            // them verbatim so _draw() can split strips instead of bridging.
            auto push_track_dict = [&](const Dictionary &tr, bool local_points = false) {
                const Variant pv = tr.get("points", PackedVector2Array());
                const Variant cv = tr.get("closed", false);
                if (pv.get_type() != Variant::PACKED_VECTOR2_ARRAY || cv.get_type() != Variant::BOOL) {
                    return;
                }
                const PackedVector2Array pts = pv;
                track_closed = (bool)cv;
                for (int i = 0; i < pts.size(); ++i) {
                    if (!pts[i].is_finite()) {
                        // INF points are strip separators (petal arcs, grid
                        // rows, spiral arms): both the drawn track AND the
                        // shape loop (which feeds the layer rings) must split
                        // here, or rings bridge strips the volley never flies.
                        track.push_back(pts[i]);
                        if (capture_shape_loop) {
                            shape_loop.push_back(pts[i]);
                        }
                        continue;
                    }
                    if (capture_shape_loop) {
                        // Layer rings scale this loop about the generator
                        // origin: capture the same marker-composed global the
                        // track draws, so rings rotate with the marker too.
                        shape_loop.push_back(local_points ? track_marker.xform(pts[i]) : track_origin + pts[i]);
                    }
                    if (local_points) {
                        push_track_local(pts[i]);
                    } else {
                        push_track_global(track_origin + pts[i]);
                    }
                }
            };
            switch (pattern_source) {
                case PATTERN_FROM_HELPER_RING: {
                    const real_t start_abs = (helper_ring_rotate_with_marker ? track_marker.get_rotation() : 0.0) + (real_t)helper_ring_start_angle;
                    push_track_dict(BulletFactory2D::helper_sample_outline_ring(helper_ring_radius, helper_ring_arc, helper_ring_y_scale, start_abs));
                    break;
                }
                case PATTERN_FROM_HELPER_ELLIPSE:
                    push_track_dict(BulletFactory2D::helper_sample_outline_ellipse(helper_ellipse_radius_x, helper_ellipse_radius_y, helper_ellipse_rotation, helper_ellipse_start_angle, helper_ellipse_arc, helper_ellipse_mode));
                    break;
                case PATTERN_FROM_HELPER_STAR:
                    push_track_dict(BulletFactory2D::helper_sample_outline_star(helper_star_points, helper_star_outer_radius, helper_star_inner_radius, helper_star_base_rotation), true);
                    break;
                case PATTERN_FROM_HELPER_ROSE:
                    push_track_dict(BulletFactory2D::helper_sample_outline_rose(helper_rose_petals, helper_rose_radius, helper_rose_lobe_sharpness, helper_rose_base_rotation));
                    break;
                case PATTERN_FROM_HELPER_LISSAJOUS:
                    push_track_dict(BulletFactory2D::helper_sample_outline_lissajous(helper_lissajous_size_x, helper_lissajous_size_y, helper_lissajous_freq_x, helper_lissajous_freq_y, helper_lissajous_phase));
                    break;
                case PATTERN_FROM_HELPER_CIRCLE:
                    push_track_dict(BulletFactory2D::helper_sample_outline_circle(helper_circle_radius));
                    break;
                case PATTERN_FROM_HELPER_RECTANGLE:
                    push_track_dict(BulletFactory2D::helper_sample_outline_rectangle(helper_rectangle_size), true);
                    break;
                case PATTERN_FROM_HELPER_SQUARE:
                    push_track_dict(BulletFactory2D::helper_sample_outline_rectangle(Vector2((real_t)helper_square_size, (real_t)helper_square_size)), true);
                    break;
                case PATTERN_FROM_HELPER_TRIANGLE:
                    push_track_dict(BulletFactory2D::helper_sample_outline_triangle(helper_triangle_type, helper_triangle_size_a, helper_triangle_size_b, helper_triangle_rotation), true);
                    break;
                case PATTERN_FROM_HELPER_TRAPEZOID:
                    push_track_dict(BulletFactory2D::helper_sample_outline_trapezoid(helper_trapezoid_base_top, helper_trapezoid_base_bottom, helper_trapezoid_height, helper_trapezoid_rotation), true);
                    break;
                case PATTERN_FROM_HELPER_DIAMOND:
                    push_track_dict(BulletFactory2D::helper_sample_outline_diamond(helper_diamond_diagonal_x, helper_diamond_diagonal_y, helper_diamond_rotation), true);
                    break;
                case PATTERN_FROM_HELPER_POLYGON:
                    push_track_dict(BulletFactory2D::helper_sample_outline_polygon(helper_polygon_vertices, helper_polygon_radius, helper_polygon_rotation), true);
                    break;
                case PATTERN_FROM_HELPER_PATH2D: {
                    // Generator-local shape points: the collector composes them
                    // as marker.xform, so the track xforms them identically
                    // (then spin/scales like every other track point).
                    PackedVector2Array curve = sample_path2d_polyline(true);
                    track_closed = helper_path2d_closed;
                    const int step = curve.size() > kMaxPreviewTrackPoints ? (int)((curve.size() + kMaxPreviewTrackPoints - 1) / kMaxPreviewTrackPoints) : 1;
                    for (int i = 0; i < curve.size(); i += step) {
                        push_track_local(curve[i]);
                    }
                    break;
                }
                case PATTERN_FROM_HELPER_LINE: {
                    // Row segment from the same formula the generator uses
                    // (anchor picks the marker seat, start_offset shifts on
                    // top). Count-independent: a lone bullet still shows its
                    // rail. Reverse/offset preserve the position set.
                    const int count = helper_bullets_amount;
                    Vector2 axis = Vector2(1, 0);
                    if (helper_line_direction.is_finite() && helper_line_direction.length_squared() > 1e-12) {
                        axis = helper_line_direction.normalized();
                    } else {
                        break;
                    }
                    if (count <= 0 || !Math::is_finite(helper_line_spacing)) {
                        break;
                    }
                    double anchor_seat = (double)(count - 1) * 0.5;
                    if (helper_line_anchor == (int)BulletFactory2D::LINE_ANCHOR_START) {
                        anchor_seat = 0.0;
                    } else if (helper_line_anchor == (int)BulletFactory2D::LINE_ANCHOR_END) {
                        anchor_seat = (double)(count - 1);
                    }
                    const double shift = Math::is_finite(helper_line_start_offset) ? MAX(helper_line_start_offset, 0.0) : 0.0;
                    // Same span the generator fills (first/last slot, shifted):
                    // no half-spacing overhang past the end bullets. A lone
                    // bullet is a point (no rail to draw), matching the dots.
                    const Vector2 first = track_origin + axis * (real_t)(helper_line_spacing * (0.0 - anchor_seat) + shift);
                    const Vector2 last = track_origin + axis * (real_t)(helper_line_spacing * ((double)(count - 1) - anchor_seat) + shift);
                    if (count > 1 && (last - first).length_squared() > 1e-12) {
                        push_track_global(first);
                        push_track_global(last);
                    }
                    break;
                }
                case PATTERN_FROM_HELPER_CUSTOM: {
                    // Explicit order strip through the stored slots (finite
                    // only): visualizes what reverse/offset do. Strided so a
                    // huge array cannot spam the canvas.
                    const int step = transforms.size() > 512 ? (int)((transforms.size() + 511) / 512) : 1;
                    for (int i = 0; i < transforms.size(); i += step) {
                        const Variant tv = transforms[i];
                        if (tv.get_type() != Variant::TRANSFORM2D) {
                            continue;
                        }
                        const Transform2D tt = tv;
                        if (!tt.is_finite()) {
                            continue;
                        }
                        const Vector2 h = to_local.xform(tt.get_origin());
                        if (h.is_finite()) {
                            track.push_back(h);
                        }
                    }
                    break;
                }
                case PATTERN_FROM_HELPER_GRID: {
                    // Row strips via the factory sampler (INF-separated). The
                    // sampler mirrors the generator's rows/columns/alignment;
                    // rotation carries the marker spin when asked.
                    const real_t base_rot_abs = (helper_grid_rotate_with_marker ? track_marker.get_rotation() : 0.0);
                    push_track_dict(BulletFactory2D::helper_sample_outline_grid(helper_bullets_amount, helper_grid_rows_per_column, helper_grid_alignment, (real_t)helper_grid_column_offset, (real_t)helper_grid_row_offset, base_rot_abs, true));
                    break;
                }
                case PATTERN_FROM_HELPER_LATTICE: {
                    // Same row-strip sampler family as Grid: columns × rows on
                    // a staggered lattice. No per-row jitter (volley noise).
                    push_track_dict(BulletFactory2D::helper_sample_outline_lattice(helper_bullets_amount, helper_lattice_columns, helper_lattice_rows, (real_t)helper_lattice_spacing_x, (real_t)helper_lattice_spacing_y, helper_lattice_stagger_rows));
                    break;
                }
                case PATTERN_FROM_HELPER_WATERFALL: {
                    // Explicit row strips, staggered along the rain direction.
                    push_track_dict(BulletFactory2D::helper_sample_outline_waterfall(helper_bullets_amount, helper_waterfall_columns, (real_t)helper_waterfall_column_spacing, helper_waterfall_rows, (real_t)helper_waterfall_row_spacing, (real_t)helper_waterfall_stagger, helper_waterfall_rain_direction));
                    break;
                }
                case PATTERN_FROM_HELPER_RAIN: {
                    // Row strips running along the band; the sampler builds
                    // the same row grouping as the generator.
                    push_track_dict(BulletFactory2D::helper_sample_outline_rain(helper_bullets_amount, (real_t)helper_rain_band_width, helper_rain_direction, (real_t)helper_rain_drop_spacing));
                    break;
                }
                case PATTERN_FROM_HELPER_WAVE: {
                    // The sine sweep itself, at fixed density.
                    push_track_dict(BulletFactory2D::helper_sample_outline_wave((real_t)helper_wave_width, (real_t)helper_wave_amplitude, (real_t)helper_wave_waves, helper_wave_direction));
                    break;
                }
                case PATTERN_FROM_HELPER_SPIRAL: {
                    // Arm sweep replicating the generator's (r, angle) formula.
                    const real_t base_rot_abs = (helper_spiral_rotate_with_marker ? track_marker.get_rotation() : 0.0);
                    push_track_dict(BulletFactory2D::helper_sample_outline_spiral(helper_bullets_amount, (real_t)helper_spiral_start_radius, (real_t)helper_spiral_radius_step, (real_t)helper_spiral_angle_step * preview_mirror_sign, base_rot_abs));
                    break;
                }
                case PATTERN_FROM_HELPER_MULTISPIRAL: {
                    // One strip per arm (INF-separated), each replicating the
                    // generator's spiral formula.
                    const real_t base_rot_abs = (helper_multispiral_rotate_with_marker ? track_marker.get_rotation() : 0.0);
                    push_track_dict(BulletFactory2D::helper_sample_outline_multispiral(helper_bullets_amount, helper_multispiral_arms, (real_t)helper_multispiral_start_radius, (real_t)helper_multispiral_radius_step, (real_t)helper_multispiral_angle_step * preview_mirror_sign, base_rot_abs, helper_multispiral_arm_stride));
                    break;
                }
                case PATTERN_FROM_HELPER_COUNTER_SPIRAL: {
                    // Mirrored-arm variant of multispiral: same strip-per-arm
                    // structure, alternate arms wound the other way.
                    const real_t base_rot_abs = (helper_counter_spiral_rotate_with_marker ? track_marker.get_rotation() : 0.0);
                    push_track_dict(BulletFactory2D::helper_sample_outline_counter_spiral(helper_bullets_amount, helper_counter_spiral_arms, (real_t)helper_counter_spiral_start_radius, (real_t)helper_counter_spiral_radius_step, (real_t)helper_counter_spiral_angle_step * preview_mirror_sign, base_rot_abs, helper_counter_spiral_arm_stride, helper_counter_spiral_mirror_alternate_arms));
                    break;
                }
                case PATTERN_FROM_HELPER_HEART: {
                    // Parametric sweep of the heart curve.
                    push_track_dict(BulletFactory2D::helper_sample_outline_heart((real_t)helper_heart_size, (real_t)helper_heart_base_rotation));
                    break;
                }
                case PATTERN_FROM_HELPER_FLOWER: {
                    // Per-type bloom track at fixed density: FAN traces the
                    // petal-tip ring, RHODONEA/SPIROGRAPH/SUPERFORMULA trace the
                    // actual curve, PHYLLOTAXIS bounds the Vogel disc rim.
                    push_track_dict(BulletFactory2D::helper_sample_outline_flower(helper_flower_type, helper_flower_petals, (real_t)helper_flower_radius, (real_t)helper_flower_petal_spread, (real_t)helper_flower_petal_sharpness, helper_flower_inner_radius_scale, helper_flower_spiro_roller, helper_flower_spiro_pen, helper_flower_super_lobes, helper_flower_super_fullness, (real_t)helper_flower_base_rotation,
                            // FAN on outline: skip petals that hold no bullet.
                            helper_outline_placement == BulletFactory2D::OUTLINE_ON_OUTLINE ? helper_bullets_amount : -1));
                    break;
                }
                case PATTERN_FROM_HELPER_STAR_POLYGON: {
                    // Vertex skeleton: star polygon through N vertices,
                    // showing the emphasis frame the bias pulls toward. Same
                    // sampler/corners the generator uses.
                    push_track_dict(BulletFactory2D::helper_sample_outline_polygon(helper_star_polygon_vertices, (real_t)helper_star_polygon_radius, (real_t)helper_star_polygon_base_rotation));
                    break;
                }
                case PATTERN_FROM_HELPER_CROSS: {
                    // Radial arm rays from origin, replicating the generator's
                    // arm layout (arm_count rays, evenly angled). Each arm is a
                    // segment origin -> tip; radial density capped.
                    if (helper_cross_arm_count < 1 || !Math::is_finite(helper_cross_arm_length) || helper_cross_arm_length <= 0.0) {
                        break;
                    }
                    const real_t arm_angle = Math::TAU / (real_t)helper_cross_arm_count;
                    const real_t base_rot = (real_t)helper_cross_base_rotation;
                    const int radial_steps = MIN(helper_bullets_amount, kMaxCrossTrackSteps);
                    for (int a = 0; a < helper_cross_arm_count; a++) {
                        const real_t ang = base_rot + arm_angle * (real_t)a;
                        const Vector2 dir = Vector2(Math::cos(ang), Math::sin(ang));
                        bool first = true;
                        for (int s = 1; s <= radial_steps; s++) {
                            const real_t dist = (real_t)helper_cross_spacing * (real_t)s;
                            if (!Math::is_finite(dist) || dist > (real_t)helper_cross_arm_length) {
                                break;
                            }
                            const Vector2 global = track_origin + dir * dist;
                            if (!global.is_finite()) {
                                continue;
                            }
                            if (first) {
                                push_track_global(track_origin);
                                first = false;
                            }
                            push_track_global(global);
                        }
                    }
                    break;
                }
                case PATTERN_FROM_HELPER_FAN: {
                    // Cone: two boundary rays from the origin plus an arc at
                    // the tip connecting them. Mirrors the generator's spread
                    // around marker rotation + direction_angle (the generator
                    // composes base_rotation = marker rotation + direction);
                    // centered spreads symmetrically.
                    // The generator stacks every bullet AT the origin (facing
                    // differs), so the cone is the meaningful shape — its
                    // radius is a fixed representative length, independent of
                    // bullet count.
                    if (!Math::is_finite(helper_fan_spread) || helper_fan_spread <= 0.0) {
                        break;
                    }
                    PackedVector2Array cone;
                    build_cone_strip(track_origin, track_marker.get_rotation() + (real_t)helper_fan_direction_angle, (real_t)(helper_fan_spread * 0.5), 150.0, (real_t)helper_fan_spread, cone);
                    for (int i = 0; i < cone.size(); ++i) {
                        push_track_global(cone[i]);
                    }
                    break;
                }
                case PATTERN_FROM_HELPER_AIMED: {
                    // Cone toward the live target (same shape as FAN). If no
                    // target is assigned, draw nothing — match the volley
                    // behavior (quiet break, the dots also draw nothing).
                    Node2D *target = get_helper_aimed_target();
                    if (target == nullptr) {
                        break;
                    }
                    const Vector2 aim_pos = predict_target_pos(target);
                    const Vector2 to_target = aim_pos - track_origin;
                    if (!to_target.is_finite() || to_target.length_squared() <= 0.0) {
                        break;
                    }
                    const real_t dir_ang = to_target.angle();
                    if (!Math::is_finite(helper_aimed_spread) || helper_aimed_spread <= 0.0) {
                        break;
                    }
                    const real_t tip_r = to_target.length();
                    if (!Math::is_finite(tip_r) || tip_r <= 0.0) {
                        break;
                    }
                    PackedVector2Array cone;
                    build_cone_strip(track_origin, dir_ang, (real_t)(helper_aimed_spread * 0.5), tip_r, (real_t)helper_aimed_spread, cone);
                    for (int i = 0; i < cone.size(); ++i) {
                        push_track_global(cone[i]);
                    }
                    break;
                }
                case PATTERN_FROM_HELPER_CORRIDOR: {
                    // The two wall runs flanking the gap, mirroring the
                    // generator's even spread: slots fill [-w/2, +w/2]
                    // skipping |x| < gap/2, so the track draws exactly the
                    // two runs the volley occupies. Two push pairs = two
                    // strips (no bridge across the dodge door).
                    const Vector2 corridor_aim = resolve_corridor_aim(helper_corridor_aim_direction, track_origin);
                    if (!corridor_aim.is_finite() || corridor_aim.length_squared() <= 0.0) {
                        break;
                    }
                    if (!Math::is_finite(helper_corridor_width) || helper_corridor_width <= 0.0) {
                        break;
                    }
                    const real_t half_width = (real_t)(helper_corridor_width * 0.5);
                    const real_t half_gap = Math::is_finite(helper_corridor_gap_width) && helper_corridor_gap_width > 0.0
                            ? (real_t)(helper_corridor_gap_width * 0.5)
                            : 0.0;
                    if (half_gap >= half_width) {
                        break;
                    }
                    const Vector2 aim = corridor_aim.normalized();
                    const Vector2 perp = aim.orthogonal();
                    push_track_global(track_origin - perp * half_width);
                    push_track_global(track_origin - perp * half_gap);
                    // Separator: the polyline must not bridge the dodge door.
                    track.push_back(Vector2(Math::INF, Math::INF));
                    push_track_global(track_origin + perp * half_gap);
                    push_track_global(track_origin + perp * half_width);
                    break;
                }
                case PATTERN_FROM_HELPER_SCATTER: {
                    // Burst-disc extent ring (dots carry density). A narrowed
                    // sector draws its arc instead, so the track never
                    // overstates the cone. Both are marker-relative offsets
                    // like the factory samplers (origin + offset), not locals.
                    if (helper_scatter_arc < Math::TAU && Math::is_finite(helper_scatter_arc) && helper_scatter_arc > 0.0 &&
                            helper_scatter_direction.is_finite() && helper_scatter_direction.length_squared() > 1e-12 &&
                            Math::is_finite(helper_scatter_burst_radius) && helper_scatter_burst_radius > 0.0) {
                        const real_t base = helper_scatter_direction.normalized().angle();
                        const real_t half = (real_t)(helper_scatter_arc * 0.5);
                        const int arc_n = MAX(8, MIN(64, (int)(helper_scatter_arc * 16.0)));
                        for (int i = 0; i <= arc_n; i++) {
                            const real_t ang = base - half + (real_t)helper_scatter_arc * (real_t)i / (real_t)arc_n;
                            push_track_global(track_origin + Vector2(Math::cos(ang), Math::sin(ang)) * (real_t)helper_scatter_burst_radius);
                        }
                        track_closed = false;
                    } else {
                        push_track_dict(BulletFactory2D::helper_sample_outline_circle(helper_scatter_burst_radius));
                    }
                    break;
                }
                default:
                    break;
            }
            // Extra outline-layer rings: scaled copies of the base track
            // about the loop center, so every ring is the same figure the
            // volley uses (the factory scales identically around the same
            // reference radius). Only loop shapes with outline support draw
            // rings; anything else keeps dots only. (Cleared unconditionally
            // above, so reaching here with layers_ok == false still drops
            // the previous rebuild's rings.)
            // Quiet validation mirror of the factory: rings draw only for
            // Quiet validation mirror of the factory: rings draw only for
            // settings the volley accepts (custom scales checked entry-wise).
            bool layers_ok = supports_outline_layout(pattern_source)
                    && helper_outline_placement == (int)BulletFactory2D::OUTLINE_LAYERS
                    && helper_outline_layer_count > 1
                    && Math::is_finite(helper_outline_layer_scale) && helper_outline_layer_scale > 0.0 && helper_outline_layer_scale <= 8.0
                    && helper_outline_layer_side >= (int)BulletFactory2D::OUTLINE_LAYER_OUTWARD
                    && helper_outline_layer_side <= (int)BulletFactory2D::OUTLINE_LAYER_BOTH
                    && helper_outline_layer_fill >= (int)BulletFactory2D::OUTLINE_LAYER_INTERLEAVED
                    && helper_outline_layer_fill <= (int)BulletFactory2D::OUTLINE_LAYER_PINGPONG
                    && helper_outline_layer_start_offset >= 0
                    && helper_outline_layer_scale_curve >= (int)BulletFactory2D::OUTLINE_LAYER_CURVE_LINEAR
                    && helper_outline_layer_scale_curve <= (int)BulletFactory2D::OUTLINE_LAYER_CURVE_EXPONENTIAL
                    && helper_outline_layer_scales.size() <= 64;
            if (layers_ok) {
                for (int ci = 0; ci < helper_outline_layer_scales.size(); ++ci) {
                    const double cs = (double)helper_outline_layer_scales[ci];
                    if (!Math::is_finite(cs) || cs < 0.05 || cs > 64.0) {
                        layers_ok = false;
                        break;
                    }
                }
            }
            if (layers_ok) {
                // Ring builder: the same center-scaling the volley layout
                // applies (see layout_outline_slots), so dots and rings share
                // one rule and can never drift apart: each extra layer
                // re-spawns the selected shape scaled about the generator
                // origin (the loop center). Rings grow from the pre-spin/scale
                // marker-space loop and run through the same spin/scale
                // pipeline as the dots, so spin, pattern_scale (either sign)
                // and holder transforms can never separate them. No per-shape
                // formulas live here by design.
                // Split the marker-space loop into INF-separated runs
                // (flower petals); closed loops never emit separators.
                // Dense runs stride to ~256 pts; short runs (polygon corners)
                // keep every point so no corner is ever eaten.
                Array run_list;
                {
                    PackedVector2Array cur;
                    for (int i = 0; i < shape_loop.size(); ++i) {
                        if (!shape_loop[i].is_finite()) {
                            if (cur.size() >= 2) {
                                run_list.push_back(cur);
                            }
                            cur.clear();
                            continue;
                        }
                        cur.push_back(shape_loop[i]);
                    }
                    if (cur.size() >= 2) {
                        run_list.push_back(cur);
                    }
                    for (int ri = 0; ri < run_list.size(); ++ri) {
                        PackedVector2Array run = run_list[ri];
                        if (run.size() > 32) {
                            PackedVector2Array sub;
                            const int stride = (run.size() + 255) / 256;
                            for (int i = 0; i < run.size(); i += stride) {
                                sub.push_back(run[i]);
                            }
                            if (sub[sub.size() - 1] != run[run.size() - 1]) {
                                sub.push_back(run[run.size() - 1]);
                            }
                            run_list[ri] = sub;
                        }
                    }
                }
                const int extra_layers = MIN(helper_outline_layer_count - 1, kMaxOutlineLayers);
                for (int L = 1; L <= extra_layers; ++L) {
                    const double layer_s = BulletFactory2D::helper_layer_scale_factor(L, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_scale_curve, helper_outline_layer_scales);
                    if (!Math::is_finite(layer_s) || layer_s < 0.05) {
                        continue;
                    }
                    // Stored holder-local (post-pipeline), like the dots:
                    // one entry per extra layer, runs INF-separated with a
                    // closing duplicate per closed run (mirrors layer_track).
                    PackedVector2Array stored;
                    for (int ri = 0; ri < run_list.size(); ++ri) {
                        const PackedVector2Array run_in = run_list[ri];
                        const bool run_closed = track_closed && run_list.size() == 1;
                        // Same photocopy the volley makes: scale about the
                        // generator origin (the loop center).
                        PackedVector2Array ring;
                        for (int k = 0; k < run_in.size(); ++k) {
                            if (!run_in[k].is_finite()) {
                                continue;
                            }
                            const Vector2 q = track_origin + (run_in[k] - track_origin) * (real_t)layer_s;
                            if (q.is_finite()) {
                                ring.push_back(q);
                            }
                        }
                        if (ring.size() < (run_closed ? 3 : 2)) {
                            continue;
                        }
                        PackedVector2Array drawn;
                        for (int k = 0; k < ring.size(); ++k) {
                            xform_track_global(ring[k], drawn);
                        }
                        if (drawn.size() < (run_closed ? 3 : 2)) {
                            continue;
                        }
                        for (int k = 0; k < drawn.size(); ++k) {
                            stored.push_back(drawn[k]);
                            layer_track.push_back(drawn[k]);
                        }
                        if (run_closed) {
                            stored.push_back(drawn[0]);
                            layer_track.push_back(drawn[0]);
                        }
                        stored.push_back(Vector2(Math::INF, Math::INF));
                        layer_track.push_back(Vector2(Math::INF, Math::INF));
                    }
                    if (stored.size() >= 2) {
                        preview_last_layer_rings.push_back(stored);
                    }
                }
            }
        }
    }
    preview_dots_layer->set_layer_path_data(layer_track, preview_layer_path_color);
    preview_dots_layer->set_path_data(track, preview_path_color, (float)preview_path_width, track_closed);
    preview_arrows_layer->set_arrows_data(tails, dirs, preview_arrow_color, (float)arrow_length, (float)arrow_width, (float)arrow_head_length, (float)arrow_head_width);
    // The snapshot must match what was just drawn: dirty-checks compare
    // against this, so snap AFTER the collect, not before.
    snapshot_preview_sources();
    preview_last_rebuild_usec = Time::get_singleton()->get_ticks_usec() - rebuild_t0;
    preview_rebuild_in_progress = false;
}

// Compares live source state against the snapshot. Crash-safe by
// construction: cached pointers are NEVER dereferenced here. Everything is
// freshly resolved from paths (resolve_node_path / get_child); the stored
// ids only participate in integer comparisons. A freed node (or a slot
// reused by an unrelated object, caught via the id check) reports dirty and
// the rebuild re-resolves - never a touch of garbage memory.
bool BulletSpawner2D::preview_sources_dirty() {
    // No snapshot yet (first frames): dirty so the loop rebuilds + snaps.
    if (!tracked_has_self) {
        return true;
    }
    if (!is_inside_tree()) {
        return false;
    }
    Node2D *base = get_effective_generator();
    if (base == nullptr) {
        return true;
    }
    if (base->get_instance_id() != tracked_base_id || !is_tracked_node_alive(base, tracked_base_id)) {
        return true;
    }
    // Generator moves: the holder is the generator's child and the snapshot
    // is holder-local, so a move the pattern provably follows (rigid for a
    // RIGID-class pattern, same basis for TRANSLATION) needs NO rebuild -
    // the canvas items ride along. Only the pose is refreshed (it depends on
    // the holder basis for skewed generators). Anything else rebuilds.
    const Transform2D base_global = base->get_global_transform();
    if (!tracked_has_base_global || base_global != tracked_base_global) {
        if (!tracked_has_base_global || !preview_survives_marker_move(tracked_base_global, base_global)) {
            return true;
        }
        tracked_base_global = base_global;
        set_preview_pose(spin_angle_deg);
    }
    // The spawner's own transform only matters when it IS the base (handled
    // above); for an external generator it never affects the pattern.
    if (spin_angle_deg != tracked_spin_angle) {
        // Spin is POSE, not geometry. advance_spin changes the angle every
        // frame, so treating it as dirt forced a full rebuild_preview() - which
        // regenerates every transform and reallocates the whole dot/arrow
        // arrays - on every single frame. At helper_bullets_amount = 10000
        // that is a 10k-element rebuild per frame, which is exactly the trap
        // the re-entrancy latch never covered (it only stops nested calls).
        //
        // A spinning emitter's pattern is the same shape rotated, so just
        // repaint the existing snapshot with the new angle. _draw() already
        // re-applies this rotation to the cached points, and the layer
        // repaints every frame anyway, so the visual is identical.
        // Re-pose and KEEP CHECKING: returning here used to hide target,
        // Path2D, custom and child-marker changes for as long as spin ran.
        if (!Math::is_finite(spin_angle_deg)) {
            return true;
        }
        set_preview_pose(spin_angle_deg);
    }
    if (pattern_source == PATTERN_FROM_HELPER_AIMED || pattern_source == PATTERN_FROM_HELPER_CORRIDOR) {
        Node2D *target = get_helper_aimed_target();
        const uint64_t target_id = target != nullptr ? target->get_instance_id() : 0;
        if (target_id != tracked_target_id) {
            return true;
        }
        if (target != nullptr) {
            if (!is_tracked_node_alive(target, tracked_target_id)) {
                return true;
            }
            if (tracked_has_target_origin && target->get_global_transform() != tracked_target_origin) {
                return true;
            }
        } else if (tracked_has_target_origin) {
            return true;
        }
    } else if (tracked_target_id != 0 || tracked_has_target_origin) {
        return true;
    }
    if (pattern_source == PATTERN_FROM_HELPER_CUSTOM || pattern_source == PATTERN_FROM_HELPER_PATH2D) {
        bool skip_path2d_sample = false;
        if (pattern_source == PATTERN_FROM_HELPER_PATH2D) {
            // Curve baking + heap traffic every tick is the most expensive
            // dirty check in the preview loop. Gate it: node identity and
            // transform compare every tick (cheap), full resample only on
            // node change or every 15th tick (~0.25 s staleness bound for
            // in-place curve edits, gizmo-only).
            Node *path_node = get_helper_path2d_node();
            const uint64_t path_id = path_node != nullptr ? path_node->get_instance_id() : 0;
            bool node_moved = (path_id != tracked_path2d_node_id);
            if (!node_moved && path_node != nullptr) {
                if (Node2D *path_2d = Object::cast_to<Node2D>(path_node)) {
                    node_moved = !tracked_has_path2d_node_global || path_2d->get_global_transform() != tracked_path2d_node_global;
                } else if (tracked_has_path2d_node_global) {
                    node_moved = true;
                }
            } else if (path_node == nullptr && (tracked_path2d_node_id != 0 || tracked_has_path2d_node_global)) {
                node_moved = true;
            }
            if (node_moved || ++preview_path2d_sample_cooldown >= 15) {
                preview_path2d_sample_cooldown = 0;
            } else {
                skip_path2d_sample = true;
            }
        }
        TypedArray<Transform2D> cur;
        if (pattern_source == PATTERN_FROM_HELPER_CUSTOM) {
            cur = helper_custom_transforms;
        } else if (!skip_path2d_sample) {
            const PackedVector2Array live_pts = sample_path2d_polyline(true);
            for (int i = 0; i < live_pts.size(); ++i) {
                cur.push_back(Transform2D(0.0, live_pts[i]));
            }
        } else {
            // Gated tick: reuse the snapshot comparison (equal -> clean).
            // Any node change above already forced a resample.
            cur = tracked_custom_transforms.duplicate();
        }
        if (cur.size() != tracked_custom_transforms.size()) return true;
        for (int i = 0; i < cur.size(); ++i) {
            // Typed fetch first: a blind Variant cast on a transient
            // inspector state is what wedged the editor.
            const Variant va = cur[i];
            const Variant vb = tracked_custom_transforms[i];
            if (va.get_type() != Variant::TRANSFORM2D || vb.get_type() != Variant::TRANSFORM2D) return true;
            const Transform2D a = va;
            const Transform2D b = vb;
            // Finite-safe compare: NaN != NaN would falsely report dirty
            // every frame and thrash the preview rebuild loop.
            if (a.is_finite() != b.is_finite()) return true;
            if (a.is_finite() && a != b) return true;
        }
    } else if (!tracked_custom_transforms.is_empty()) {
        return true;
    }
    // Child markers only shape the pattern for the Children source. For every
    // other source a child moving (including the preview holder itself, which
    // rides along with the generator) must not force a rebuild.
    if (pattern_source != PATTERN_FROM_CHILDREN) {
        return false;
    }
    const int count = base->get_child_count();
    if (count != tracked_child_count) {
        return true;
    }
    if (tracked_marker_origins.size() != count || tracked_marker_rots.size() != count || tracked_marker_ids.size() != count * 2) {
        return true;
    }
    for (int i = 0; i < count; i++) {
        Node2D *marker = Object::cast_to<Node2D>(base->get_child(i));
        if (marker == nullptr) {
            if (tracked_marker_origins[i] != Vector2(Math::INF, Math::INF)) {
                return true;
            }
            continue;
        }
        // Identity first (catches freed + slot-reused objects), then pose:
        // a marker rotated exactly in place keeps its origin, so the
        // rotation check is what catches your inspector-rotation bug.
        const uint64_t id = marker->get_instance_id();
        if (tracked_marker_ids[i * 2] != (int64_t)(id & 0xFFFFFFFFu) || tracked_marker_ids[i * 2 + 1] != (int64_t)(id >> 32)) {
            return true;
        }
        if (marker->get_global_transform().get_origin() != tracked_marker_origins[i] || marker->get_global_rotation() != tracked_marker_rots[i]) {
            return true;
        }
    }
    return false;
}

void BulletSpawner2D::update_preview_process_state() {
    // Editor: processing runs only while the preview is on, so inspector
    // rotations, gizmo drags, and add/remove marker refresh live.
    // Runtime: never touched here - shooting/spinning own _process, and the
    // runtime preview piggy-backs that loop via preview_sources_dirty().
    if (Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
        if (show_pattern_preview) {
            set_process(true);
        } else {
            set_process(false);
        }
    }
}

} // namespace BlastBullets2D
