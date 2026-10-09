#pragma once

// What a pattern stage needs from the outside world. The patterns module
// never touches the scene tree: its caller (BulletSpawner2D) resolves nodes
// (children, aimed target, Path2D curve) and hands the results in here, so
// generation and preview tracks stay pure functions of knobs + inputs.

#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_vector2_array.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include <cstdint>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

// The per-call bullet limit shared by generation, preview and the spawner
// setters (helper_bullets_amount, helper_custom_transforms, shape counts).
// A developer setting, not a constant: BulletPatterns2D.max_bullets_per_pattern
// (project setting blastbullets2d/patterns/max_bullets_per_pattern). It only
// guards against a typo'd count freezing or OOM-ing the game. Main thread only.
static constexpr int kPatternDefaultMaxBullets = 100000;
inline int g_pattern_max_bullets = kPatternDefaultMaxBullets;
static inline int pattern_max_bullets() { return g_pattern_max_bullets; }
// Grid slots (waterfall/lattice columns * rows) follow the same limit, 4x.
static inline int64_t pattern_max_grid_slots() { return (int64_t)g_pattern_max_bullets * 4; }
// Bumped on every accepted change of the limit: the bake cache must not serve
// a bake generated under another limit (the same knobs can now be refused or
// accepted), so spawners fold it into the version the cache sees.
inline uint64_t g_pattern_limit_generation = 0;
static inline uint64_t pattern_cache_version2d(uint64_t pattern_version) {
	return pattern_version ^ (g_pattern_limit_generation << 48);
}

// Limits shared by generation, preview and the spawner setters.
static constexpr int kPatternMaxTrackPoints = 256; // path/cross track decimation target
static constexpr int kPatternMaxOutlineLayers = 64; // helper_outline_layer_count + helper_outline_layer_scales

// WarnOnce2D codes raised by pattern generation (spawner range 101+).
static constexpr uint32_t kPatternWarnCorridorGap = 101; // gap >= width at generation
static constexpr uint32_t kPatternWarnGridTooLarge = 103; // waterfall/lattice columns*rows

struct PatternInputs2D {
	int source = 0; // PatternShape2D id
	Transform2D marker; // generator global transform (spin/scale NOT applied)
	real_t mirror_sign = 1.0; // -1 winds spirals the other way
	bool quiet = false; // preview/probe: never report
	uint64_t warn_owner_id = 0; // WarnOnce2D owner (the spawner)
	// CHILDREN: the global transforms of the marker's Node2D children
	// (empty or null: the marker itself).
	const std::vector<Transform2D> *children = nullptr;
	// AIMED: the predicted target position (has_aim_target false: no target).
	bool has_aim_target = false;
	Vector2 aim_position;
	// CORRIDOR: resolved aim direction (live target, else the static knob).
	Vector2 corridor_aim;
	// PATH2D: the sampled curve in generator space (empty: already reported).
	const PackedVector2Array *path_points = nullptr;
};

// Where a preview track goes. The spawner implements it on its preview
// buffers (holder space, spin/scale pipeline, layer-ring loop capture).
struct PatternTrackSink2D {
	virtual ~PatternTrackSink2D() = default;
	// A sampler dict ({points, closed}): marker-relative offsets, or
	// marker-local points when local_points is true. INF points split strips.
	virtual void push_dict(const Dictionary &track, bool local_points = false) = 0;
	// One marker-GLOBAL point through the spin/scale pipeline.
	virtual void push_global(const Vector2 &point) = 0;
	// One marker-LOCAL point (composed with the marker first).
	virtual void push_local(const Vector2 &point) = 0;
	// A spawned slot's global origin, drawn as is (no spin/scale pipeline).
	virtual void push_slot_origin(const Vector2 &global_origin) = 0;
	// Strip break: the polyline never bridges it.
	virtual void push_separator() = 0;
	virtual void set_closed(bool closed) = 0;
};

struct PatternTrackInputs2D {
	int source = 0;
	Transform2D marker; // the marker the track is drawn around
	real_t mirror_sign = 1.0;
	// CUSTOM: the collected spawn transforms (global).
	const TypedArray<Transform2D> *slots = nullptr;
	// PATH2D: the curve sampled quietly in generator space.
	const PackedVector2Array *path_points = nullptr;
	// AIMED / CORRIDOR: as in PatternInputs2D.
	bool has_aim_target = false;
	Vector2 aim_position;
	Vector2 corridor_aim;
};

// What a preset decides beyond the knobs (spawner-owned state).
struct PatternPresetResult2D {
	int source = 0; // PatternShape2D id
	bool spin_enabled = false; // true: the preset turns spin on
	double spin_speed_deg_per_sec = 0.0;
};

} //namespace BlastBullets2D
