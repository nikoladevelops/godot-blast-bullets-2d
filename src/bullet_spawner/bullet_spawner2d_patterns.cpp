// BulletSpawner2D (split from bullet_spawner2d.cpp, same class):
// Pattern generation and the bake cache: raw generation per source,
// motion classification (RIGID/TRANSLATION/NONE), cached re-posing, the
// native span collect used by shoot_once and the cache verifier.

#include "bullet_spawner/bullet_spawner2d_internal.hpp"
#include "patterns/pattern_pose2d.hpp"
#include "patterns/patterns_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

bool BulletSpawner2D::supports_outline_layout(PatternSource source) {
    return pattern_shape_supports_outline2d((int)source);
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
    if (base != nullptr && path_2d != nullptr && path_2d != base && is_inside_tree() && path_2d->is_inside_tree() && BulletVolley2D::is_transform_invertible_safe(base->get_global_transform())) {
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

static_assert((int)BulletSpawner2D::PATH2D_DISTRIBUTION_FIXED_SPACING == (int)BulletPatterns2D::POLYLINE_DISTRIBUTION_FIXED_SPACING && (int)BulletSpawner2D::PATH2D_DISTRIBUTION_EVEN == (int)BulletPatterns2D::POLYLINE_DISTRIBUTION_EVEN, "Path2D distribution ids mirror the polyline layout");
static_assert((int)BulletSpawner2D::PATH2D_OVERFLOW_CLAMP == (int)BulletPatterns2D::POLYLINE_OVERFLOW_CLAMP && (int)BulletSpawner2D::PATH2D_OVERFLOW_WRAP == (int)BulletPatterns2D::POLYLINE_OVERFLOW_WRAP && (int)BulletSpawner2D::PATH2D_OVERFLOW_SHRINK_TO_FIT == (int)BulletPatterns2D::POLYLINE_OVERFLOW_SHRINK_TO_FIT, "Path2D overflow ids mirror the polyline layout");
static_assert((int)BulletSpawner2D::PATH2D_ANCHOR_START == (int)BulletPatterns2D::POLYLINE_ANCHOR_START && (int)BulletSpawner2D::PATH2D_ANCHOR_CENTER == (int)BulletPatterns2D::POLYLINE_ANCHOR_CENTER && (int)BulletSpawner2D::PATH2D_ANCHOR_END == (int)BulletPatterns2D::POLYLINE_ANCHOR_END, "Path2D anchor ids mirror the polyline layout");
static_assert((int)BulletSpawner2D::PATH2D_FACING_ALONG_PATH == (int)BulletPatterns2D::POLYLINE_FACING_ALONG_PATH && (int)BulletSpawner2D::PATH2D_FACING_NORMAL_P90 == (int)BulletPatterns2D::POLYLINE_FACING_NORMAL_P90 && (int)BulletSpawner2D::PATH2D_FACING_NORMAL_M90 == (int)BulletPatterns2D::POLYLINE_FACING_NORMAL_M90, "Path2D facing ids mirror the polyline layout");

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
    // Resolve the scene-tree inputs the source reads, then hand generation
    // to the patterns module (pattern_dispatch2d.cpp).
    PatternInputs2D in;
    in.source = (int)pattern_source;
    in.marker = marker;
    in.mirror_sign = mirror_sign;
    in.quiet = quiet;
    in.warn_owner_id = get_instance_id();
    std::vector<Transform2D> children;
    PackedVector2Array path_pts;
    switch (pattern_source) {
        case PATTERN_FROM_CHILDREN:
            for (int i = 0; i < base->get_child_count(); ++i) {
                Node2D *as_2d = Object::cast_to<Node2D>(base->get_child(i));
                // The editor preview holder is a Node2D child too, but it is
                // visualization only and must never become a spawn marker.
                if (as_2d != nullptr && !as_2d->has_meta(PREVIEW_META_KEY)) {
                    children.push_back(as_2d->get_global_transform());
                }
            }
            in.children = &children;
            break;
        case PATTERN_FROM_HELPER_AIMED:
            if (Node2D *target = get_helper_aimed_target()) {
                in.has_aim_target = true;
                in.aim_position = predict_target_pos(target);
            }
            break;
        case PATTERN_FROM_HELPER_CORRIDOR:
            // Corridor is aimed by design (AIMED_TRAP preset flows through
            // here): prefer the live target, fall back to the static aim
            // direction (preview still shows the wall outside the tree).
            in.corridor_aim = resolve_corridor_aim(helper_corridor_aim_direction, marker.get_origin());
            break;
        case PATTERN_FROM_HELPER_PATH2D:
            path_pts = sample_path2d_polyline(quiet);
            in.path_points = &path_pts;
            break;
        default:
            break;
    }
    return generate_raw(in);
}

// ---- Pattern bake cache ----------------------------------------------------

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
// array that can be mutated in place) are always regenerated: the registry
// flags them (reads_external_state). Everything else is classified by
// probing (PatternBakeCache2D::classify).
static bool pattern_source_reads_external_state(int source) {
    return pattern_shape_reads_external_state2d(source);
}

void BulletSpawner2D::resolve_raw_pattern(Node2D *base, const Transform2D &marker, real_t mirror_sign, bool quiet, std::vector<Transform2D> &r_raw) const {
    const bool cache_on = pattern_cache_mode == PATTERN_CACHE_AUTO && !pattern_cache_bypass;
    const int channel = quiet ? PatternBakeCache2D::CHANNEL_PREVIEW : (mirror_sign < 0.0 ? PatternBakeCache2D::CHANNEL_MIRRORED_SHOT : PatternBakeCache2D::CHANNEL_SHOT);
    const PatternBakeCache2D::Generator generate = [this, base, mirror_sign](const Transform2D &m, bool q, std::vector<Transform2D> &r_out) {
        const TypedArray<Transform2D> fresh = generate_raw_pattern(base, m, mirror_sign, q);
        r_out.resize(fresh.size());
        for (int i = 0; i < fresh.size(); ++i) {
            r_out[i] = fresh[i];
        }
    };
    pattern_cache.resolve(channel, pattern_version, cache_on, pattern_source_reads_external_state(pattern_source), marker, quiet, generate, r_raw, String("BulletSpawner2D: pattern cache mismatch (pattern_source ") + itos(pattern_source));
}

bool BulletSpawner2D::preview_survives_marker_move(const Transform2D &old_marker, const Transform2D &new_marker) const {
    return pattern_cache.survives_marker_move(pattern_version, pattern_cache_mode == PATTERN_CACHE_AUTO, old_marker, new_marker);
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
    d["hits"] = (int64_t)pattern_cache.hits;
    d["misses"] = (int64_t)pattern_cache.misses;
    d["bakes"] = (int64_t)pattern_cache.bakes;
    d["version"] = (int64_t)pattern_version;
    d["shot_class"] = pattern_cache.motion_class(PatternBakeCache2D::CHANNEL_SHOT, pattern_version);
    d["preview_class"] = pattern_cache.motion_class(PatternBakeCache2D::CHANNEL_PREVIEW, pattern_version);
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
    PatternBakeCache2D::verify = enabled;
}

bool BulletSpawner2D::debug_get_pattern_cache_verify() {
    return PatternBakeCache2D::verify;
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
    // Spin + pattern scale folded into one matrix (PatternPose2D): one trig
    // pair per volley, exact for mirrored/sheared bases. Trivial or
    // degenerate inputs leave the slot raw: trivial is already correct, and
    // degenerate inputs stay visible to the downstream finite guards (which
    // fail loud) instead of being silently rewritten.
    bool spin_scale_ok = false;
    const Transform2D spin_scale_mx = PatternPose2D::make_spin_scale_matrix(base_origin, spin_radians, pattern_factor, spin_scale_ok);
    const bool use_spin_scale_mx = spin_scale_ok && !PatternPose2D::spin_is_trivial(spin_radians, pattern_factor);
    // Negative space (post spin/scale so indexes match the preview): carve
    // dodge doors or bullet text out of any helper layout. Same contract as
    // BulletPatterns2D::helper_apply_skip_indices (OOB ignored, warned once).
    const int raw_count = (int)raw.size();
    bool any_skip = false;
    if (!helper_skip_indices.is_empty() && pattern_source >= PATTERN_FROM_HELPER_GRID) {
        bool out_of_range = false;
        any_skip = PatternPose2D::build_skip_mask(helper_skip_indices, raw_count, skip_mask_scratch, out_of_range);
        // Once per (pattern version, bullet count), never from the preview:
        // the shot path would otherwise warn every volley.
        if (out_of_range && !quiet) {
            WarnOnce2D::warn(get_instance_id(), kWarnSkipIndexOutOfRange, (int64_t)pattern_version, raw_count,
                    "BulletSpawner2D: helper_skip_indices holds an index outside 0.." + itos(raw_count - 1) + "; it is ignored.");
        }
    }
    PatternPose2D::pose(raw, spin_scale_mx, use_spin_scale_mx, any_skip ? &skip_mask_scratch : nullptr, local_factor, r_out);
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
