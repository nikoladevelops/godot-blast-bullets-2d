#pragma once

// Private to the bullet_spawner2d*.cpp translation units: file-local
// helpers, limits and tables shared by more than one of them. Never
// include from anywhere else. Functions are `static inline` (one private
// copy per TU, no ODR coupling, no unused-function warnings).

#include "bullet_spawner2d.hpp"
#include "../shared/warn_once2d.hpp"

#include <functional>
#include "../shared/easing2d.hpp"
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include "bullets/directional_bullets2d.hpp"
#include "godot_cpp/classes/capsule_shape2d.hpp"
#include "godot_cpp/classes/circle_shape2d.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/classes/curve2d.hpp"
#include "godot_cpp/classes/path2d.hpp"
#include "godot_cpp/classes/rectangle_shape2d.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/classes/window.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/variant/callable.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

namespace BlastBullets2D {


// Resolves a stored NodePath to a typed node. When inside the tree the path is
// authoritative: a missing target clears the cache instead of serving a stale
// pointer. Outside the tree the previously cached pointer is returned.
template <typename T>
static inline T *resolve_node_path(const Node *self, const NodePath &p_path, T *&r_cache) {
    if (self->is_inside_tree() && !p_path.is_empty()) {
        Node *node = self->get_node_or_null(p_path);
        if (node == nullptr) {
            r_cache = nullptr;
            return nullptr;
        }
        T *typed = Object::cast_to<T>(node);
        r_cache = typed;
        return typed;
    }
    return r_cache;
}


template <typename T>
static inline void assign_node_to_path(const Node *self, T *node, NodePath &r_path, T *&r_cache) {
    r_cache = node;
    // Path updates only when both ends share one tree: get_path_to across
    // trees errors (or yields a path that later resolves elsewhere), and an
    // out-of-tree assignment must not keep a stale path that would silently
    // replace this node on re-entry (see validate_cached_node).
    if (node != nullptr && self->is_inside_tree() && node->is_inside_tree() && self->get_tree() == node->get_tree()) {
        r_path = self->get_path_to(node);
    } else if (node == nullptr) {
        r_path = NodePath();
    }
}


// Validates a cached node pointer against its stored instance id, then falls
// back to the stored path. Every cached node getter (factory, generator,
// aimed target, Path2D node) shares this exact shape: a stale cache is
// cleared and reported as missing WITHOUT consulting the path (the path may
// since resolve elsewhere), while an empty cache resolves through the path.
// ObjectDB is consulted before any dereference, so a freed node can never be
// touched here.
template <typename T>
static inline T *validate_cached_node(const Node *self, const NodePath &p_path, T *&r_cache, uint64_t &r_id) {
    if (r_cache != nullptr) {
        if (r_id == 0 || !UtilityFunctions::is_instance_id_valid(r_id)) {
            r_cache = nullptr;
            r_id = 0;
        } else if (ObjectDB::get_instance(ObjectID(r_id)) != (Object *)r_cache) {
            r_cache = nullptr;
            r_id = 0;
        } else {
            // Validated cache wins outright: re-resolving the path here
            // would let a stale path silently replace a manually assigned
            // node (assign_node_to_path deliberately leaves the path behind
            // out-of-tree assignments). Setters clear the cache, so a live
            // cache always agrees with the path. This also skips the
            // string-parse + tree walk on every getter call.
            return r_cache;
        }
        if (r_cache == nullptr) {
            return nullptr;
        }
    }
    T *resolved = resolve_node_path(self, p_path, r_cache);
    if (resolved == nullptr) {
        r_cache = nullptr;
        r_id = 0;
        return nullptr;
    }
    r_id = resolved->get_instance_id();
    return resolved;
}


// Metadata tag + node name for the editor-only pattern preview holder.
// The children-mode collection skips anything carrying the tag, so the
// preview can never become a spawn marker. The tilde sorts it last and marks
// it as internal. The holder is owner-less: never saved, never exported.
static constexpr const char *PREVIEW_META_KEY = "blastbullets_pattern_preview";

static constexpr const char *PREVIEW_HOLDER_NAME = "~BlastBulletsPatternPreview";


// Shared limits (single definition so validation, generation and preview
// agree; the inspector hint strings in _bind_methods mirror these).
static constexpr int kMaxBulletsPerVolley = 10000; // helper_bullets_amount + helper_custom_transforms

static constexpr int kMaxHomingTargets = 10000; // homing_max_targets scene-scan bound

static constexpr int kMaxHomingDequeTargets = 256; // engine queue cap per volley

static constexpr int kMaxOutlineLayers = 64; // helper_outline_layer_count + helper_outline_layer_scales

static constexpr int kMaxPreviewTrackPoints = 256; // path/cross track decimation stride target

static constexpr int kMaxCrossTrackSteps = 256; // cross arm radial density cap

static constexpr int kPreviewZIndex = 4000; // dots + arrows layers

static constexpr float kFirstDotRadiusScale = 1.6f; // bullet-0 emphasis marker


// Single source of truth for pattern-source metadata: the inspector hint
// string, pattern_source_name() and both supports_* predicates all read this
// table, so display names, ids and shape capabilities can never drift apart.
// Row order matches the historical hint string (not enum order) to keep the
// inspector and saved scenes byte-identical.
struct PatternSourceInfo {
    BulletSpawner2D::PatternSource id;
    const char *name;
    bool outline;
    bool corners;
};

static const PatternSourceInfo kPatternSources[] = {
    { BulletSpawner2D::PATTERN_FROM_CHILDREN, "From Children", false, false },
    { BulletSpawner2D::PATTERN_FROM_SELF, "From Self", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_PATH2D, "Path2D", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_AIMED, "Aimed", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_CUSTOM, "Custom", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_CIRCLE, "Circle", true, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_SQUARE, "Square", true, true },
    { BulletSpawner2D::PATTERN_FROM_HELPER_RECTANGLE, "Rectangle", true, true },
    { BulletSpawner2D::PATTERN_FROM_HELPER_TRIANGLE, "Triangle", true, true },
    { BulletSpawner2D::PATTERN_FROM_HELPER_DIAMOND, "Diamond", true, true },
    { BulletSpawner2D::PATTERN_FROM_HELPER_TRAPEZOID, "Trapezoid", true, true },
    { BulletSpawner2D::PATTERN_FROM_HELPER_POLYGON, "Polygon", true, true },
    { BulletSpawner2D::PATTERN_FROM_HELPER_ELLIPSE, "Ellipse", true, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_RING, "Ring", true, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_STAR, "Star", true, true },
    { BulletSpawner2D::PATTERN_FROM_HELPER_HEART, "Heart", true, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_STAR_POLYGON, "Star Polygon", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_FLOWER, "Flower", true, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_ROSE, "Rose", true, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_LISSAJOUS, "Lissajous", true, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_LINE, "Line", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_GRID, "Grid", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_LATTICE, "Lattice", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_RAIN, "Rain", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_WATERFALL, "Waterfall", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_WAVE, "Wave", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_FAN, "Fan", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_CORRIDOR, "Corridor", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_SPIRAL, "Spiral", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_MULTISPIRAL, "Multi Spiral", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_COUNTER_SPIRAL, "Counter Spiral", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_CROSS, "Cross", false, false },
    { BulletSpawner2D::PATTERN_FROM_HELPER_SCATTER, "Scatter", false, false },
};


// Human-readable mode name for error messages (mirrors the pattern_source enum hint).
static inline const char *pattern_source_name(BulletSpawner2D::PatternSource source) {
    for (const PatternSourceInfo &info : kPatternSources) {
        if (info.id == source) {
            return info.name;
        }
    }
    return "unknown";
}


// Inspector hint built from the same table, so the dropdown can never list
// a different set than the name lookup and predicates above.
static inline String pattern_source_hint() {
    String out;
    for (const PatternSourceInfo &info : kPatternSources) {
        if (!out.is_empty()) {
            out += ",";
        }
        out += String(info.name) + ":" + itos((int)info.id);
    }
    return out;
}


// Corner-anchored polygon loops: the only shapes with shared corner dots
// (rectangle, square, polygon, triangle, trapezoid, diamond, star). Corner
// priority/mode/margin/facing knobs show for these only.
static inline bool supports_corner_layout(BulletSpawner2D::PatternSource source) {
    for (const PatternSourceInfo &info : kPatternSources) {
        if (info.id == source) {
            return info.corners;
        }
    }
    return false;
}

// WarnOnce2D codes owned by the spawner (1..99 belong to the volleys).
static constexpr uint32_t kWarnCorridorGap = 101; // gap >= width at generation
static constexpr uint32_t kWarnSkipIndexOutOfRange = 102; // helper_skip_indices
static constexpr uint32_t kWarnGridTooLarge = 103; // waterfall/lattice columns*rows
static constexpr uint32_t kWarnOrbitWithoutHoming = 104; // orbiting needs homing

// Grids (waterfall/lattice) may hold columns * rows up to this many slots
// (the factory refuses more); each side is capped the same in its setter.
static constexpr int kMaxGridSlots = kMaxBulletsPerVolley * 4;
// Burst chains longer than this are a typo, not a pattern.
static constexpr int kMaxBurstCount = 1024;

// NodePath setters: inside the tree, a path that resolves to a node of the
// wrong type is rejected loudly and the old value kept. A path that does
// not resolve yet is accepted (the node may be added later, and scene
// loading assigns properties before the spawner enters the tree).
template <typename T>
static inline bool node_path_type_ok(const Node *self, const NodePath &p_path, const char *prop, const char *type_name) {
    if (p_path.is_empty() || !self->is_inside_tree()) {
        return true;
    }
    Node *node = self->get_node_or_null(p_path);
    if (node == nullptr || Object::cast_to<T>(node) != nullptr) {
        return true;
    }
    UtilityFunctions::push_error(String("BulletSpawner2D: ") + prop + " must point to a " + type_name + ", keeping the old value.");
    return false;
}

} // namespace BlastBullets2D
