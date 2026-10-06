#pragma once

// Private to the bullet_spawner2d*.cpp translation units: file-local
// helpers, limits and tables shared by more than one of them. Never
// include from anywhere else. Functions are `static inline` (one private
// copy per TU, no ODR coupling, no unused-function warnings).

#include "bullet_spawner/bullet_spawner2d.hpp"
#include "core/warn_once2d.hpp"
#include <godot_cpp/classes/class_db_singleton.hpp>

#include "bullet_volley/bullet_volley2d.hpp"
#include "core/easing2d.hpp"
#include "godot_cpp/classes/capsule_shape2d.hpp"
#include "godot_cpp/classes/circle_shape2d.hpp"
#include "godot_cpp/classes/curve2d.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/global_constants.hpp"
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
#include <functional>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/time.hpp>

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
	} else {
		// Null, or a pointer assigned across trees / while out of the tree:
		// the old path must go (it would resolve the OLD node on re-entry).
		// fill_assigned_node_paths() writes the new one once both share a tree.
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
// The graze ring preview layer (internal child of the spawner).
static constexpr const char *GRAZE_PREVIEW_NAME = "~BlastBulletsGrazePreview";

// Shared limits (single definition so validation, generation and preview
// agree; the inspector hint strings in _bind_methods mirror these).
static constexpr int kMaxBulletsPerVolley = kPatternMaxBullets; // helper_bullets_amount + helper_custom_transforms

static constexpr int kMaxHomingTargets = 10000; // homing_max_targets scene-scan bound

static constexpr int kMaxHomingDequeTargets = 256; // engine queue cap per volley

static constexpr int kMaxOutlineLayers = kPatternMaxOutlineLayers; // helper_outline_layer_count + helper_outline_layer_scales

static constexpr int kMaxPreviewTrackPoints = kPatternMaxTrackPoints; // path/cross track decimation stride target

static constexpr int kPreviewZIndex = 4000; // dots + arrows layers

static constexpr float kFirstDotRadiusScale = 1.6f; // bullet-0 emphasis marker

// Pattern-source metadata lives in the pattern registry
// (patterns/pattern_registry2d.hpp): names, ids, knob prefixes and shape
// capabilities come from that one table. These wrappers keep the spawner's
// historical call sites typed on its enum.
static inline const char *pattern_source_name(BulletSpawner2D::PatternSource source) {
	return pattern_shape_name2d((int)source);
}

static inline String pattern_source_hint() {
	return pattern_shape_hint2d();
}

// Corner-anchored polygon loops: the only shapes with shared corner dots
// (rectangle, square, polygon, triangle, trapezoid, diamond, star). Corner
// priority/mode/margin/facing knobs show for these only.
static inline bool supports_corner_layout(BulletSpawner2D::PatternSource source) {
	return pattern_shape_supports_corners2d((int)source);
}

// WarnOnce2D codes owned by the spawner (1..99 belong to the volleys).
static constexpr uint32_t kWarnCorridorGap = kPatternWarnCorridorGap; // gap >= width at generation
static constexpr uint32_t kWarnSkipIndexOutOfRange = 102; // helper_skip_indices
static constexpr uint32_t kWarnGridTooLarge = kPatternWarnGridTooLarge; // waterfall/lattice columns*rows
static constexpr uint32_t kWarnOrbitWithoutHoming = 104; // orbiting needs homing

// Grids (waterfall/lattice) may hold columns * rows up to this many slots
// (the factory refuses more); each side is capped the same in its setter.
static constexpr int kMaxGridSlots = kPatternMaxGridSlots;
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
