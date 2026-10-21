#pragma once

#include "bullet_volley/bullet_volley2d.hpp"
#include "core/emit_guard2d.hpp"
#include "core/variant_enum_cast2d.hpp"
#include "debugger/graze_preview_layer2d.hpp"
#include "factory/bullet_factory2d.hpp"
#include "patterns/bullet_patterns2d.hpp"
#include "patterns/pattern_bake_cache2d.hpp"
#include "patterns/pattern_knobs2d.hpp"
#include "patterns/pattern_registry2d.hpp"
#include <memory>
#include <vector>

#include "data/bullet_graze_zone2d.hpp"
#include "data/bullet_sound_data2d.hpp"
#include "data/bullet_volley_data2d.hpp"
#include "godot_cpp/classes/array_mesh.hpp"
#include "godot_cpp/classes/curve.hpp"
#include "godot_cpp/classes/multi_mesh.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/path2d.hpp"
#include "godot_cpp/classes/random_number_generator.hpp"
#include "godot_cpp/classes/tween.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/core/property_info.hpp"
#include "godot_cpp/variant/node_path.hpp"
#include "godot_cpp/variant/packed_vector2_array.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/typed_array.hpp"

namespace BlastBullets2D {
using namespace godot;

// Editor-only preview layer with self-repainting custom drawing.
//
// The engine owns a CanvasItem's command list and can clear/re-issue it on
// any redraw (zoom, pan, selection, idle refresh). One-shot
// RenderingServer::canvas_item_add_* calls therefore vanish as soon as the
// engine repaints, which is why the old preview disappeared ~1s after it was
// built. The documented contract is: custom drawing lives in _draw(), the
// engine calls it on every repaint, and rebuilds only store data +
// queue_redraw(). This class implements exactly that: rebuild_preview()
// fills it with a snapshot, _draw() repaints it forever.
class PatternPreviewLayer2D : public Node2D {
	GDCLASS(PatternPreviewLayer2D, Node2D)

public:
	enum LayerKind {
		LAYER_DOTS = 0,
		LAYER_ARROWS
	};

	LayerKind kind = LAYER_DOTS;

	// Snapshot data (holder-local). Assigned by rebuild_preview(),
	// consumed by _draw(). Plain values: the layer never reads the
	// spawner live, so a repaint cannot depend on stale pointers.
	PackedVector2Array dots;
	Color dot_color = Color(1.0, 0.05, 0.05);
	float dot_radius = 4.0f;
	// Bullet-0 emphasis: drawn last, in its own color and bigger, so the
	// pattern start (anchor / reverse / loop seam) reads at a glance.
	bool show_first_marker = true;
	Color first_dot_color = Color(1.0, 0.85, 0.2);
	float first_dot_radius_scale = 1.6f;
	// Track underlay: the shape/loop/curve bullets ride on, drawn as
	// individual segments under the dots so sparse volleys still show
	// the whole track. Holder-local snapshot; empty when the mode has no
	// drawable track. Width <= 0 hides it.
	PackedVector2Array path_points;
	Color path_color = Color(0.3, 0.85, 1.0, 0.55);
	float path_width = 2.0f;
	bool path_closed = false;
	// Layer rings: one closed run per extra outline layer (LAYERS
	// placement), INF-separated like the track strips above. Centroid-
	// scaled copies of the blue track via the factory's shared helpers
	// (same center + same R + same distance math as the volley), so
	// every ring matches the dots exactly; straight edges stay straight
	// and corners stay sharp. Runs are pre-closed at build time (first
	// point appended), so the open-run drawing path renders them
	// correctly. Empty unless layers are active. Drawn in
	// layer_path_color with the same path_width, always without
	// antialiasing.
	PackedVector2Array layer_path_points;
	Color layer_path_color = Color(1.0, 1.0, 0.0, 0.55);
	PackedVector2Array arrow_tails;
	PackedVector2Array arrow_dirs;
	Color arrow_color = Color(1.0, 0.05, 0.05);
	float arrow_length = 16.0f;
	float arrow_width = 2.0f;
	float arrow_head_length = 8.0f;
	float arrow_head_width = 10.0f;
	// Reused draw scratch: _draw runs on every repaint, so member buffers
	// avoid per-repaint allocations. draw_scratch filters the track;
	// head_tri holds one arrow-head triangle (one polygon per draw call).
	// run_scratch accumulates one finite strip when separators split
	// the track into runs.
	PackedVector2Array draw_scratch;
	PackedVector2Array run_scratch;
	PackedVector2Array head_tri;

	// POSE: the spin is applied as this layer's OWN node transform
	// (set_pose), never by re-drawing. The geometry above is snapshotted
	// unspun in holder space; rotating the layer about the holder origin
	// (= generator origin, the shot's spin pivot) moves every glyph
	// exactly like the volley, and the RenderingServer just updates one
	// canvas-item transform: a spinning 10k-dot preview costs O(1) per
	// frame and _draw() runs only when the geometry really changes.
	void set_pose(const Transform2D &p_pose);
	// Instrumentation for tests/benchmarks: how many times _draw() ran.
	int debug_draw_count = 0;

	// Batched glyphs (one draw_multimesh per kind instead of N canvas
	// commands). Rebuilt in _draw() from the snapshot; shared unit meshes.
	Ref<MultiMesh> dots_multimesh;
	Ref<MultiMesh> rings_multimesh;
	Ref<MultiMesh> heads_multimesh;
	PackedFloat32Array glyph_buffer; // reused set_buffer scratch
	Ref<ArrayMesh> dot_mesh; // unit disc
	Ref<ArrayMesh> ring_mesh; // annulus at ring_mesh_radius/width
	real_t ring_mesh_radius = -1.0;
	real_t ring_mesh_width = -1.0;
	Ref<ArrayMesh> head_mesh; // arrow head, tip at +X
	real_t head_mesh_length = -1.0;
	real_t head_mesh_width = -1.0;

	void set_dots_data(const PackedVector2Array &p_dots, const Color &p_color, float p_radius);
	void set_first_marker(bool p_show, const Color &p_color, float p_radius_scale);
	void set_path_data(const PackedVector2Array &p_points, const Color &p_color, float p_width, bool p_closed);
	void set_layer_path_data(const PackedVector2Array &p_points, const Color &p_color);
	void set_arrows_data(const PackedVector2Array &p_tails, const PackedVector2Array &p_dirs, const Color &p_color, float p_length, float p_width, float p_head_length, float p_head_width);
	// Collision-ring overlay: per-dot outline circle approximating the
	// volley hitbox (bounding radius from spawn_data's shape). Snapshot
	// like dots; radius <= 0 hides. Lets users compare visual vs hitbox
	// directly in the editor instead of misjudging dots.
	float ring_radius = 0.0f;
	Color ring_color = Color(1.0, 1.0, 1.0, 0.7f);
	float ring_width = 1.5f;
	void set_rings_data(float p_radius, const Color &p_color, float p_width);

	void _draw() override;

protected:
	static void _bind_methods();
};

// What a refused shot reports (PatternSpawner2D::shoot_once): the error text
// (empty = none), the volley_skipped reason and whether that signal fires.
struct ShotRefusal2D {
	bool refused = false;
	String message;
	StringName reason;
	bool with_signal = true;
	static ShotRefusal2D refuse(const String &p_message, const StringName &p_reason, bool p_with_signal = true) {
		ShotRefusal2D r;
		r.refused = true;
		r.message = p_message;
		r.reason = p_reason;
		r.with_signal = p_with_signal;
		return r;
	}
};

// What a derived spawner's fire_shot did: a refusal, or the fired element
// count (the On Shot sound's amount gate) and whether a shot handler took
// over the cadence (reset_shooting / fire_n_volleys) while it ran.
struct ShotResult2D {
	ShotRefusal2D refusal;
	int amount = 0;
	bool cadence_replaced = false;
};

// Everything about WHERE and WHEN to fire, shared by the spawners: the
// pattern sources and their preview, the Transform subgroup, cadence (auto
// fire, budgets, bursts, telegraph, pattern lists), spin, Path2D movement,
// time scale, placement randomization, and the graze and sound target
// settings. WHAT fires is the derived class's (BulletSpawner2D: bullets).
class PatternSpawner2D : public Node2D, public PatternKnobs2D {
	GDCLASS(PatternSpawner2D, Node2D)

	// Crash-safety for preview source tracking: a raw Node* cached across
	// frames may dangle after the node is freed (stale object id: the memory
	// can even be reused by an unrelated object). The ONLY safe pattern is:
	// store the pointer AND its instance id, then resolve the id through
	// ObjectDB and compare POINTERS without ever dereferencing the stored
	// one. is_instance_id_valid() + id equality is NOT enough: a recycled id
	// passes both while the stored pointer dangles (UAF read). A failed
	// check means "gone": never dereference, just treat as dirty so the next
	// rebuild re-resolves from the tree. Same idea as
	// homing_target_deque's is_homing_target_valid().
	static bool is_tracked_node_alive(const Node *node, uint64_t cached_id) {
		if (node == nullptr || cached_id == 0) {
			return false;
		}
		const Object *live = ObjectDB::get_instance(ObjectID(cached_id));
		return live == static_cast<const Object *>(node);
	}

public:
	// Refuses a release-build free() while this node emits, and any free()
	// while it runs user code outside a signal (core/emit_guard2d.hpp).
	// Mutable: the const pattern collect runs the aimed target's script.
	mutable EmitGuardState2D emit_guard;

	// Where volley transforms come from. Children/Self read the scene
	// tree; the helper modes call the BulletFactory2D static generators
	// with the properties below, relative to the generator's transform.
	//
	// SCENE COMPAT LOCK: pattern_source is serialized as a plain int in
	// .tscn files. The ids come from the pattern registry
	// (patterns/pattern_registry2d.hpp, explicit values there), mirrored
	// here with the numbers inline for review. An implicit
	// sequence would silently renumber every later entry if one were
	// inserted, quietly repointing saved scenes at the wrong generator -
	// and only the two static_asserts below would notice, so slots they
	// do not cover (1..11, 29..32) were entirely unguarded. Never
	// renumber: append new sources at the end with the next free integer.
	enum PatternSource : int {
		PATTERN_FROM_CHILDREN = PATTERN_SHAPE_CHILDREN, // 0
		PATTERN_FROM_SELF = PATTERN_SHAPE_SELF, // 1
		PATTERN_FROM_HELPER_GRID = PATTERN_SHAPE_GRID, // 2
		PATTERN_FROM_HELPER_RING = PATTERN_SHAPE_RING, // 3
		PATTERN_FROM_HELPER_FAN = PATTERN_SHAPE_FAN, // 4
		PATTERN_FROM_HELPER_SPIRAL = PATTERN_SHAPE_SPIRAL, // 5
		PATTERN_FROM_HELPER_LINE = PATTERN_SHAPE_LINE, // 6
		PATTERN_FROM_HELPER_AIMED = PATTERN_SHAPE_AIMED, // 7
		PATTERN_FROM_HELPER_FLOWER = PATTERN_SHAPE_FLOWER, // 8
		PATTERN_FROM_HELPER_ELLIPSE = PATTERN_SHAPE_ELLIPSE, // 9
		PATTERN_FROM_HELPER_RAIN = PATTERN_SHAPE_RAIN, // 10
		PATTERN_FROM_HELPER_SCATTER = PATTERN_SHAPE_SCATTER, // 11
		PATTERN_FROM_HELPER_STAR_POLYGON = PATTERN_SHAPE_STAR_POLYGON, // 12
		PATTERN_FROM_HELPER_MULTISPIRAL = PATTERN_SHAPE_MULTISPIRAL, // 13
		PATTERN_FROM_HELPER_CROSS = PATTERN_SHAPE_CROSS, // 14
		PATTERN_FROM_HELPER_STAR = PATTERN_SHAPE_STAR, // 15
		PATTERN_FROM_HELPER_HEART = PATTERN_SHAPE_HEART, // 16
		PATTERN_FROM_HELPER_WAVE = PATTERN_SHAPE_WAVE, // 17
		PATTERN_FROM_HELPER_WATERFALL = PATTERN_SHAPE_WATERFALL, // 18
		PATTERN_FROM_HELPER_LATTICE = PATTERN_SHAPE_LATTICE, // 19
		PATTERN_FROM_HELPER_ROSE = PATTERN_SHAPE_ROSE, // 20
		PATTERN_FROM_HELPER_COUNTER_SPIRAL = PATTERN_SHAPE_COUNTER_SPIRAL, // 21
		PATTERN_FROM_HELPER_CORRIDOR = PATTERN_SHAPE_CORRIDOR, // 22
		PATTERN_FROM_HELPER_LISSAJOUS = PATTERN_SHAPE_LISSAJOUS, // 23
		PATTERN_FROM_HELPER_CUSTOM = PATTERN_SHAPE_CUSTOM, // 24
		PATTERN_FROM_HELPER_CIRCLE = PATTERN_SHAPE_CIRCLE, // 25
		PATTERN_FROM_HELPER_RECTANGLE = PATTERN_SHAPE_RECTANGLE, // 26
		PATTERN_FROM_HELPER_SQUARE = PATTERN_SHAPE_SQUARE, // 27
		PATTERN_FROM_HELPER_POLYGON = PATTERN_SHAPE_POLYGON, // 28
		PATTERN_FROM_HELPER_PATH2D = PATTERN_SHAPE_PATH2D, // 29
		PATTERN_FROM_HELPER_TRIANGLE = PATTERN_SHAPE_TRIANGLE, // 30
		PATTERN_FROM_HELPER_TRAPEZOID = PATTERN_SHAPE_TRAPEZOID, // 31
		PATTERN_FROM_HELPER_DIAMOND = PATTERN_SHAPE_DIAMOND, // 32
		PATTERN_FROM_LAST = PATTERN_SHAPE_COUNT, // 33
	};

	// Belt-and-braces anchors: an explicit enum above already makes an
	// accidental renumber a code review error, but these two long-standing
	// pins are the historical tripwire - keep them, and note that
	// tests/spawner/test_spawner_pattern_source_lock.gd pins ALL of them
	// from the script side as well.
	static_assert((int)PATTERN_FROM_HELPER_STAR_POLYGON == 12, "STAR_POLYGON must stay 12 for saved scenes");
	static_assert((int)PATTERN_FROM_HELPER_POLYGON == 28, "POLYGON must stay 28 for saved scenes");

	// Path2D layout: how bullets are placed along the baked curve.
	// FIXED_SPACING lays count bullets run=(count-1)*spacing apart;
	// EVEN_DISTRIBUTION spreads count bullets over the whole length.
	enum Path2DDistribution : int {
		PATH2D_DISTRIBUTION_FIXED_SPACING = 0,
		PATH2D_DISTRIBUTION_EVEN
	};

	// Path2D overflow: what happens when a fixed run does not fit
	// (run longer than the curve, or shifted past its ends). CLAMP piles
	// extras at the end, WRAP continues from the start (mod length),
	// SHRINK_TO_FIT scales spacing down so the run fits exactly.
	enum Path2DOverflow : int {
		PATH2D_OVERFLOW_CLAMP = 0,
		PATH2D_OVERFLOW_WRAP,
		PATH2D_OVERFLOW_SHRINK_TO_FIT
	};

	// Path2D anchor: where a fixed run sits when shorter than the curve.
	enum Path2DAnchor : int {
		PATH2D_ANCHOR_START = 0,
		PATH2D_ANCHOR_CENTER,
		PATH2D_ANCHOR_END
	};

	// Path2D facing: ALONG_PATH aims +X with travel (tangent), the two
	// NORMAL modes aim across it (+/-90 degrees). facing_offset applies
	// on top of all three.
	enum Path2DFacing : int {
		PATH2D_FACING_ALONG_PATH = 0,
		PATH2D_FACING_NORMAL_P90,
		PATH2D_FACING_NORMAL_M90
	};

	// Path2D space: which frame the baked curve is drawn in.
	// FOLLOW_GENERATOR reads the raw baked points as generator-local
	// offsets (the node's own transform is ignored: only its curve
	// points matter, like every other pattern). AT_PATH2D keeps the
	// legacy pose: the volley materializes where the Path2D node sits
	// in the world.
	enum Path2DSpace : int {
		PATH2D_SPACE_FOLLOW_GENERATOR = 0,
		PATH2D_SPACE_AT_PATH2D = 1
	};

	// How the spin angle evolves. CONTINUOUS rotates forever at
	// spin_speed_deg_per_sec (signed: positive = clockwise, per the
	// Godot 2D convention); OSCILLATE swings +-spin_amplitude_deg at
	// spin_frequency_hz instead, ignoring the speed.
	enum SpinMode : int {
		SPIN_CONTINUOUS = 0,
		SPIN_OSCILLATE
	};

	// Scene-tree reference to the factory. Stored as an unfiltered NodePath
	// so every node in the edited scene is pickable; the typed pointer is
	// resolved on demand with a runtime type check (see get_bullet_factory).
	NodePath bullet_factory_path;
	// Runtime cache of the resolved factory. Not a bound property.
	// bullet_factory_id pairs with it: validate BEFORE dereferencing
	// (see is_tracked_node_alive). A raw pointer outlives freed nodes.
	mutable BulletFactory2D *bullet_factory = nullptr;
	mutable uint64_t bullet_factory_id = 0;
	// Plain Node2D reference from the scene tree whose children provide
	// spawn transforms (same NodePath pattern as the factory, but
	// type-filtered to Node2D since native types always match).
	NodePath transforms_generator_path;
	// Runtime cache of the resolved generator. Not a bound property.
	// transforms_generator_id pairs with it (same dangling guard).
	mutable Node2D *transforms_generator = nullptr;
	mutable uint64_t transforms_generator_id = 0;

	NodePath get_bullet_factory_path() const;
	void set_bullet_factory_path(const NodePath &p_path);

	BulletFactory2D *get_bullet_factory() const;
	void set_bullet_factory(BulletFactory2D *factory);

	NodePath get_transforms_generator_path() const;
	void set_transforms_generator_path(const NodePath &p_path);

	Node2D *get_transforms_generator() const;
	void set_transforms_generator(Node2D *generator);
	// Anchor every pattern_source mode lives on: the assigned generator,
	// or this spawner when unset/unresolvable. Never null inside the tree.
	Node2D *get_effective_generator() const;

	// SHOOTING (TIMER + VOLLEYS)

	// Master switch for automatic shooting. Toggles _process.
	bool shooting_enabled = true;
	// Seconds between volleys. Must stay > 0 (setter rejects the rest).
	double shoot_interval_sec = 1.0;
	// Delay before the first volley after (re)arming.
	double shoot_initial_delay_sec = 0.0;
	// Volleys after which auto-shooting stops. -1 = infinite, 0 = never.
	int max_volleys = -1;
	// Uniform scale applied to every generated transform, relative to the
	// transforms generator (spread radius and bullet size grow together).
	// 1.0 = identity. Must stay finite (setter rejects the rest); 0
	// collapses the whole volley onto the generator, negatives mirror it.
	double pattern_scale = 1.0;
	// Per-transform size scale: multiplies each bullet's local basis
	// (texture size) without touching its spawn position. 1.0 = identity.
	// Must stay finite; 0 hides bullets in place, negatives mirror them.
	// Compose with pattern_scale for big-layout + big-bullet looks.
	double transforms_scale = 1.0;
	// Flat global offset added to every bullet right after spawn (muzzle
	// offsets, spawn-then-nudge, whole-volley follows). (0, 0) disables
	// the pass. Must stay finite. Applied through the engine teleport
	// path, so shapes, attachments, and interpolation stay in sync.
	Vector2 spawn_position_offset = Vector2(0, 0);
	int spawn_position_offset_space = 0; // SpawnOffsetSpace

	// SPIN (ROTATE MARKER)
	//
	// Virtual rotation applied to every volley, no matter which
	// pattern_source is picked: collect_spawn_transforms() rotates each
	// transform around the generator origin by the current spin angle.
	// The scene tree is never touched (no node is rotated). Runtime only:
	// the angle advances in _process and freezes in the editor.
	bool spin_enabled = false;
	double spin_speed_deg_per_sec = 90.0;
	SpinMode spin_mode = SPIN_CONTINUOUS;
	double spin_amplitude_deg = 45.0;
	double spin_frequency_hz = 0.5;

	bool get_spin_enabled() const;
	void set_spin_enabled(bool value);
	double get_spin_speed_deg_per_sec() const;
	void set_spin_speed_deg_per_sec(double value);
	SpinMode get_spin_mode() const;
	void set_spin_mode(SpinMode value);
	double get_spin_amplitude_deg() const;
	void set_spin_amplitude_deg(double value);
	double get_spin_frequency_hz() const;
	void set_spin_frequency_hz(double value);
	// Current spin angle in degrees (runtime state, not stored).
	double get_spin_angle_deg() const;
	// Sets the angle the pattern is turned by (Continuous mode; wrapped to
	// 0..360). Shots and the preview use it at once, spin on or off. In
	// Oscillate mode the angle follows the swing: refused.
	void set_spin_angle_deg(double degrees);
	void reset_spin_angle();

	// TRANSFORMS SOURCE + HELPER GENERATORS
	//
	// Which behavior collect_spawn_transforms() uses. Children (default)
	// preserves the original behavior exactly; the helper modes call the
	// BulletFactory2D static generators relative to the generator's global
	// transform, using helper_bullets_amount bullets and the matching
	// helper_* properties below (only the active mode's group is shown in
	// the inspector - see _validate_property).
	PatternSource pattern_source = PATTERN_FROM_CHILDREN;
	// Every helper_* knob is storage inherited from PatternKnobs2D
	// (patterns/pattern_knobs2d.hpp): the bound accessors below read and
	// write those fields.

	// BURST (multi-volley danmaku phrasing: N shots per trigger).
	bool burst_enabled = false;
	// Shots per trigger. Must stay >= 1.
	int burst_count = 3;
	// Seconds between burst shots. Must stay > 0.
	double burst_interval_sec = 0.15;
	// Mirror every other burst volley: the classic reverse-the-angle
	// rhythm without scripting. A mirrored shot negates BOTH the emitter
	// spin and the spiral-family winding (spiral / multispiral /
	// counter-spiral angle_step), so the arms genuinely sweep the other
	// way. BEHAVIOR CHANGE: earlier this only negated the emitter spin, so
	// a mirrored spiral wound identically and merely pointed backwards.
	// Non-spiral patterns are unaffected (they have no winding to flip).
	bool burst_alternate_mirror = false;
	// Telegraph: warn before each burst volley fires.
	bool telegraph_enabled = false;
	// Seconds of warning before the volley. Must stay >= 0.
	double telegraph_sec = 0.5;

	// How homing_node_name is compared against node names (node-name source).
	enum HomingNodeNameMatch : int {
		HOMING_NAME_MATCH_EXACT = 0, // "Player" matches only "Player"
		HOMING_NAME_MATCH_CONTAINS, // "Player" matches "Player2", "EnemyPlayer", ...
		HOMING_NAME_MATCH_STARTS_WITH, // "Player" matches "Player2" but not "EnemyPlayer"
		HOMING_NAME_MATCH_ENDS_WITH // "Player" matches "EnemyPlayer" but not "Player2"
	};
	// Reload jitter: each auto volley waits shoot_interval_sec +/-
	// random * reload_jitter_sec (seeded by reload_jitter_seed). 0 = exact.
	// Must stay finite and >= 0.
	double reload_jitter_sec = 0.0;
	// Seed for reload jitter. 0 = non-deterministic, >0 = reproducible
	// (seed + volleys_fired, so volleys still vary per shot).
	int reload_jitter_seed = 0;

	// GRAZE (EASY API, bullet_spawner2d_graze.cpp)
	//
	// Every fired volley is armed with graze_zones (BulletGrazeZone2D: a
	// target group and up to 4 rings around each target). A bullet passing
	// within a ring of a zone target fires bullet_grazed, and
	// bullet_graze_exited when it leaves that zone alive: on this spawner
	// while it lives, then on the BulletFactory2D, which receives every
	// graze (also for bullets whose spawner was already freed, whatever
	// orphaned_volleys says, as long as the bullets fly). A volley keeps
	// its zones: edits made INSIDE a zone resource reach bullets in flight,
	// while graze_enabled / graze_zones changes apply to the next shots.
	//
	// WHO grazes is decided here and only here (the zones say how):
	// graze_target_source and the settings below (the same choices as
	// homing's), the same targets for every zone. They are shared with every
	// volley this spawner fired, so edits and refresh_graze_targets() reach
	// bullets in flight; orphaned volleys keep the settings and the nodes
	// the paths last pointed to. No cap on how many targets.
	bool graze_enabled = false;
	// Up to BulletVolley2D::MAX_GRAZE_ZONES zones; null entries are kept
	// (the inspector adds them) and skipped.
	TypedArray<BulletGrazeZone2D> graze_zones;
	// SERIALIZED ids (GrazeDetector2D::Source): never renumber.
	enum GrazeTargetSource : int {
		GRAZE_SOURCE_NODE_GROUP = 0, // Node2D members of graze_node_group (default)
		GRAZE_SOURCE_NODE_PATH = 1, // the Node2D at graze_target_path
		GRAZE_SOURCE_NODE_NAME = 2, // Node2Ds named like graze_node_name (scene scan)
		GRAZE_SOURCE_NODE_CHILDREN = 3, // Node2D children of graze_children_parent_path
		GRAZE_SOURCE_MOUSE = 4, // the cursor, in the bullets' canvas coordinates
		GRAZE_SOURCE_GLOBAL_POSITIONS = 5 // every entry of graze_global_positions
	};
	GrazeTargetSource graze_target_source = GRAZE_SOURCE_NODE_GROUP;
	StringName graze_node_group;
	// Allow-list for every source: only nodes in this group count.
	StringName graze_filter_group;
	NodePath graze_target_path;
	// Scanned from the current scene (never inside a bullet factory or this
	// spawner), compared per graze_node_name_match_mode (homing's values).
	String graze_node_name;
	HomingNodeNameMatch graze_node_name_match_mode = HOMING_NAME_MATCH_CONTAINS;
	bool graze_node_name_case_sensitive = false;
	NodePath graze_children_parent_path;
	bool graze_children_recursive = false;
	// Global Positions source: one graze target per entry (finite only),
	// read every tick (edits reach bullets in flight). No cap.
	PackedVector2Array graze_global_positions;
	// Seconds between target scans (factory time); 0 = every physics tick.
	// Positions are read every tick either way; a target freed or queued
	// for deletion stops counting at once, a new one waits for the next
	// scan (or refresh_graze_targets()).
	double graze_update_interval = 0.0;
	// Ring preview: each ring of each zone drawn around every resolved
	// target, in the zone's preview_color (editor by default; at runtime
	// only when graze_preview_during_runtime is on).
	bool graze_show_preview = true;
	bool graze_preview_during_runtime = false;
	double graze_preview_line_width = 1.5;

	bool get_graze_enabled() const;
	void set_graze_enabled(bool value);
	TypedArray<BulletGrazeZone2D> get_graze_zones() const;
	void set_graze_zones(const Array &value);
	bool get_graze_show_preview() const;
	void set_graze_show_preview(bool value);
	bool get_graze_preview_during_runtime() const;
	void set_graze_preview_during_runtime(bool value);
	double get_graze_preview_line_width() const;
	void set_graze_preview_line_width(double value);
	GrazeTargetSource get_graze_target_source() const;
	void set_graze_target_source(GrazeTargetSource value);
	StringName get_graze_node_group() const;
	void set_graze_node_group(const StringName &value);
	StringName get_graze_filter_group() const;
	void set_graze_filter_group(const StringName &value);
	NodePath get_graze_target_path() const;
	void set_graze_target_path(const NodePath &p_path);
	String get_graze_node_name() const;
	void set_graze_node_name(const String &value);
	HomingNodeNameMatch get_graze_node_name_match_mode() const;
	void set_graze_node_name_match_mode(HomingNodeNameMatch value);
	bool get_graze_node_name_case_sensitive() const;
	void set_graze_node_name_case_sensitive(bool value);
	NodePath get_graze_children_parent_path() const;
	void set_graze_children_parent_path(const NodePath &p_path);
	bool get_graze_children_recursive() const;
	void set_graze_children_recursive(bool value);
	PackedVector2Array get_graze_global_positions() const;
	void set_graze_global_positions(const PackedVector2Array &value);
	double get_graze_update_interval() const;
	void set_graze_update_interval(double value);
	// Scans for graze targets now (whatever graze_update_interval says):
	// the bullets this spawner fired test the nodes found from their next
	// tick on. Returns how many targets it has now.
	int refresh_graze_targets();
	// The graze target finder this spawner shares with every volley it
	// armed (created on first use). C++ only: the factory's runtime ring
	// preview draws around its lists.
	GrazeDetector2D &graze_detector_ref() const;
	// The targets the runtime tests right now (graze_target_source, every
	// zone rings them, in tree order; this spawner itself never counts).
	Array resolve_graze_targets() const;
	// The rings exactly as the preview draws them:
	// [{center, radius, color, zone_index, ring_index, target_id}].
	Array debug_get_graze_preview_circles() const;
	// {active, visible, circles, draws}: ring preview instrumentation.
	Dictionary debug_get_graze_preview_stats() const;
	// {scans, lists, shares_factory_lists}: this spawner's graze detector
	// (scans run by its own lists; shared lists count on the factory).
	Dictionary debug_get_graze_detector_stats() const;

	// SOUND (bullet_spawner2d_sound.cpp)
	//
	// Every fired volley is armed with sound_effects (BulletSoundData2D: one
	// entry per trigger), each playing its stream when its trigger fires,
	// subject to its own limits (shared per resource across the factory).
	// A volley keeps its entries: edits made INSIDE a sound resource reach
	// bullets in flight, while sound_enabled / sound_effects changes apply
	// to the next shots.
	//
	// WHO the sounds are measured from is decided here and only here (the
	// entries say how): sound_listener_source and the settings below (the
	// same choices as graze and homing, plus Godot's own 2D listener). They
	// are shared with every volley this spawner fired, so edits and
	// refresh_sound_listeners() reach bullets in flight; orphaned volleys
	// keep the settings and the nodes the paths last pointed to. Sounds
	// whose listener source finds nobody fall back to Godot's listener.
	// No cap on how many listeners.
	bool sound_enabled = false;
	// Stacked entries; null entries are kept (the inspector adds them) and
	// skipped.
	Array sound_effects;
	// Added to every entry's volume_db (a per-spawner mix trim).
	double sound_volume_db = 0.0;
	// SERIALIZED ids: never renumber. Like GrazeDetector2D::Source with a
	// Godot-listener default in front (never cast between the two enums).
	enum SoundListenerSource : int {
		SOUND_LISTENER_GODOT_LISTENER = 0, // Godot's current AudioListener2D / camera
		SOUND_LISTENER_NODE_GROUP = 1, // Node2D members of sound_listener_node_group
		SOUND_LISTENER_NODE_PATH = 2, // the Node2D at sound_listener_path
		SOUND_LISTENER_NODE_NAME = 3, // Node2Ds named like sound_listener_node_name (scene scan)
		SOUND_LISTENER_NODE_CHILDREN = 4, // Node2D children of sound_listener_children_parent_path
		SOUND_LISTENER_MOUSE = 5, // the cursor, in the bullets' canvas coordinates
		SOUND_LISTENER_GLOBAL_POSITIONS = 6 // every entry of sound_listener_global_positions
	};
	SoundListenerSource sound_listener_source = SOUND_LISTENER_GODOT_LISTENER;
	StringName sound_listener_node_group;
	// Allow-list for every node source: only nodes in this group count.
	StringName sound_listener_filter_group;
	NodePath sound_listener_path;
	// Scanned from the current scene (never inside a bullet factory or this
	// spawner), compared per sound_listener_node_name_match_mode (homing's
	// values).
	String sound_listener_node_name;
	HomingNodeNameMatch sound_listener_node_name_match_mode = HOMING_NAME_MATCH_CONTAINS;
	bool sound_listener_node_name_case_sensitive = false;
	NodePath sound_listener_children_parent_path;
	bool sound_listener_children_recursive = false;
	// Global Positions source: one listener per entry (finite only), read
	// every sweep (edits reach bullets in flight). No cap.
	PackedVector2Array sound_listener_global_positions;
	// Seconds between listener scans (factory time); 0 = every physics tick.
	// Positions are read every sweep either way; a listener freed or queued
	// for deletion stops counting at once, a new one waits for the next
	// scan (or refresh_sound_listeners()).
	double sound_listener_update_interval = 0.0;

	bool get_sound_enabled() const;
	void set_sound_enabled(bool value);
	Array get_sound_effects() const;
	void set_sound_effects(const Array &value);
	double get_sound_volume_db() const;
	void set_sound_volume_db(double value);
	SoundListenerSource get_sound_listener_source() const;
	void set_sound_listener_source(SoundListenerSource value);
	StringName get_sound_listener_node_group() const;
	void set_sound_listener_node_group(const StringName &value);
	StringName get_sound_listener_filter_group() const;
	void set_sound_listener_filter_group(const StringName &value);
	NodePath get_sound_listener_path() const;
	void set_sound_listener_path(const NodePath &p_path);
	String get_sound_listener_node_name() const;
	void set_sound_listener_node_name(const String &value);
	HomingNodeNameMatch get_sound_listener_node_name_match_mode() const;
	void set_sound_listener_node_name_match_mode(HomingNodeNameMatch value);
	bool get_sound_listener_node_name_case_sensitive() const;
	void set_sound_listener_node_name_case_sensitive(bool value);
	NodePath get_sound_listener_children_parent_path() const;
	void set_sound_listener_children_parent_path(const NodePath &p_path);
	bool get_sound_listener_children_recursive() const;
	void set_sound_listener_children_recursive(bool value);
	PackedVector2Array get_sound_listener_global_positions() const;
	void set_sound_listener_global_positions(const PackedVector2Array &value);
	double get_sound_listener_update_interval() const;
	void set_sound_listener_update_interval(double value);
	// Scans for listeners now (whatever sound_listener_update_interval
	// says): the bullets this spawner fired measure from the nodes found
	// from their next tick on. Returns how many listeners it has now.
	int refresh_sound_listeners();
	// The listener finder this spawner shares with every volley it armed
	// (created on first use). C++ only.
	std::shared_ptr<GrazeDetector2D> sound_detector_ref() const;
	// The listeners the runtime measures from right now
	// (sound_listener_source, in tree order; this spawner itself never
	// counts). Node2D entries, Vector2 for point sources, empty for Godot's
	// listener.
	Array resolve_sound_listeners() const;
	// Plays one Sound group entry with this spawner's mix applied (editor
	// tool: listen without running the game; allowed at runtime too).
	// Wrong index errors, null/streamless entries warn, nothing crashes.
	void preview_sound_effect(int index);
	// {scans, lists, shares_factory_lists}: this spawner's sound detector.
	Dictionary debug_get_sound_detector_stats() const;

	// BURST / TELEGRAPH / TARGETING / PERF accessors (members above).
	bool get_burst_enabled() const;
	void set_burst_enabled(bool value);
	int get_burst_count() const;
	void set_burst_count(int value);
	double get_burst_interval_sec() const;
	void set_burst_interval_sec(double value);
	bool get_burst_alternate_mirror() const;
	void set_burst_alternate_mirror(bool value);
	bool get_telegraph_enabled() const;
	void set_telegraph_enabled(bool value);
	double get_telegraph_sec() const;
	void set_telegraph_sec(double value);
	int get_reload_jitter_seed() const;
	void set_reload_jitter_seed(int value);
	// Helper accessors for the new danmaku generators + aimed prediction.
	// Table-driven pattern knob accessors (patterns/pattern_knob_table2d.inc):
	// get_<knob>() / set_<knob>(value) for every PATTERN_KNOB row.
#define PATTERN_SUBGROUP(TITLE, PREFIX)
#define PATTERN_PROPERTY(VTYPE, NAME, HINT, HINT_STRING, SETTER, GETTER)
#define PATTERN_KNOB(PTYPE, CTYPE, VTYPE, NAME, ...) \
	CTYPE get_##NAME() const;                        \
	void set_##NAME(PTYPE value);
#include "patterns/pattern_knob_table2d.inc"
#undef PATTERN_SUBGROUP
#undef PATTERN_PROPERTY
#undef PATTERN_KNOB
	TypedArray<Transform2D> get_helper_custom_transforms() const;
	void set_helper_custom_transforms(const TypedArray<Transform2D> &value);
	NodePath get_helper_path2d_path() const;
	void set_helper_path2d_path(const NodePath &p_path);
	Path2DSpace get_helper_path2d_space() const;
	void set_helper_path2d_space(Path2DSpace value);
	Node *get_helper_path2d_node() const;
	void set_helper_path2d_node(Node *node);
	Path2DDistribution get_helper_path2d_distribution() const;
	void set_helper_path2d_distribution(Path2DDistribution value);
	Path2DOverflow get_helper_path2d_overflow() const;
	void set_helper_path2d_overflow(Path2DOverflow value);
	Path2DAnchor get_helper_path2d_anchor() const;
	void set_helper_path2d_anchor(Path2DAnchor value);
	Path2DFacing get_helper_path2d_facing() const;
	void set_helper_path2d_facing(Path2DFacing value);
	PackedFloat32Array get_helper_outline_layer_scales() const;
	void set_helper_outline_layer_scales(const PackedFloat32Array &value);
	double get_reload_jitter_sec() const;
	void set_reload_jitter_sec(double value);
	// One-call preset fill (see BulletPatterns2D::PatternPreset).
	void apply_pattern_preset(int preset);
	// Sequencer: queue pattern entries, then fire them in order
	// (interval apart) or all at once. Returns entries queued.
	int spawn_pattern_list(const Array &entries, bool simultaneous = false, double interval_sec = 0.25);
	// One shot with pattern-list entry overrides (preset, pattern_source,
	// helper_bullets_amount, spawn_data): what the overrides changed goes
	// back after the shot, a shot handler's edits of other knobs stick.
	// A normal shot otherwise (volley_fired, max_volleys, the nesting
	// rule); no pattern_list_finished. True when it fired.
	bool shoot_once_with(const Dictionary &overrides);
	void stop_pattern_list();
	bool is_pattern_list_active() const;
	// Next auto-shot interval with reload jitter applied.
	double next_shoot_interval_sec() const;
	// Live introspection for waves, budgets and debug.
	int get_burst_shots_left() const;

	double time_scale = 1.0;
	bool time_scale_affects_volleys = true;
	// False (default): a muzzle outside the factory's despawn bounds (while they
	// are enabled) does not shoot, the bullets would be culled at once.
	bool shoot_outside_despawn_bounds = false;

	// ---- Randomization (bullet_spawner2d_randomization.cpp) ----
	// Per-bullet rolls applied at shot time on top of the pattern (never to the
	// preview, the bake cache or collect_spawn_transforms()). Min/max pairs
	// are NOT checked against each other: they are normalized at use, so
	// assignment order never matters (scene load sets min before max).
	enum BulletScaleMode : int {
		BULLET_SCALE_OFF = 0,
		BULLET_SCALE_UNIFORM,
		BULLET_SCALE_PER_AXIS
	};
	int bullet_scale_mode = BULLET_SCALE_OFF;
	Vector2 bullet_scale_min_xy = Vector2(1, 1);
	Vector2 bullet_scale_max_xy = Vector2(1, 1);
	int bullet_random_seed = 0; // 0 = Godot's global RNG
	bool spin_random_start_phase = false;
	double bullet_scale_min = 1.0;
	double bullet_scale_max = 1.0;
	double bullet_rotation_min_degrees = 0.0;
	double bullet_rotation_max_degrees = 0.0;
	double bullet_position_jitter_min = 0.0;
	double bullet_position_jitter_max = 0.0;
	double shoot_interval_max_sec = 0.0;
	double burst_interval_max_sec = 0.0;
	double telegraph_max_sec = 0.0;

	// ---- Time (the spawner's own clock) ----
	// The spawner's cadence, initial delay, telegraph, bursts, pattern list,
	// retarget, spin and movement run on delta x time_scale x the factory's
	// time_scale (frozen at 0). With time_scale_affects_volleys the volleys it
	// fired run at the same scale (the factory multiplies it into their tick
	// while the spawner lives).
	double get_time_scale() const { return time_scale; }
	void set_time_scale(double value);
	bool get_time_scale_affects_volleys() const { return time_scale_affects_volleys; }
	void set_time_scale_affects_volleys(bool value) { time_scale_affects_volleys = value; }
	// spawner scale x factory scale, 0 when either is not finite and positive.
	double get_process_time_scale() const;
	bool get_shoot_outside_despawn_bounds() const { return shoot_outside_despawn_bounds; }
	void set_shoot_outside_despawn_bounds(bool value) { shoot_outside_despawn_bounds = value; }
	// True while the factory's despawn bounds are on, this spawner may not shoot
	// from outside them, and its muzzle is outside. Shots and telegraphs ask.
	bool muzzle_outside_despawn_bounds(const BulletFactory2D *factory) const;

	BulletScaleMode get_bullet_scale_mode() const { return static_cast<BulletScaleMode>(bullet_scale_mode); }
	void set_bullet_scale_mode(BulletScaleMode value);
	Vector2 get_bullet_scale_min_xy() const { return bullet_scale_min_xy; }
	void set_bullet_scale_min_xy(const Vector2 &value);
	Vector2 get_bullet_scale_max_xy() const { return bullet_scale_max_xy; }
	void set_bullet_scale_max_xy(const Vector2 &value);
	int get_bullet_random_seed() const { return bullet_random_seed; }
	void set_bullet_random_seed(int value) { bullet_random_seed = value; }
	bool get_spin_random_start_phase() const { return spin_random_start_phase; }
	void set_spin_random_start_phase(bool value) { spin_random_start_phase = value; }
	double get_bullet_scale_min() const { return bullet_scale_min; }
	void set_bullet_scale_min(double value);
	double get_bullet_scale_max() const { return bullet_scale_max; }
	void set_bullet_scale_max(double value);
	double get_bullet_rotation_min_degrees() const { return bullet_rotation_min_degrees; }
	void set_bullet_rotation_min_degrees(double value);
	double get_bullet_rotation_max_degrees() const { return bullet_rotation_max_degrees; }
	void set_bullet_rotation_max_degrees(double value);
	double get_bullet_position_jitter_min() const { return bullet_position_jitter_min; }
	void set_bullet_position_jitter_min(double value);
	double get_bullet_position_jitter_max() const { return bullet_position_jitter_max; }
	void set_bullet_position_jitter_max(double value);
	double get_shoot_interval_max_sec() const { return shoot_interval_max_sec; }
	void set_shoot_interval_max_sec(double value);
	double get_burst_interval_max_sec() const { return burst_interval_max_sec; }
	void set_burst_interval_max_sec(double value);
	double get_telegraph_max_sec() const { return telegraph_max_sec; }
	void set_telegraph_max_sec(double value);
	// Rolls for one shot: transforms are rewritten in place; speed factors
	// (empty when off) are applied by the caller after the spawn.
	bool randomization_active() const;
	void randomize_shot_transforms(std::vector<Transform2D> &transforms);
	// A value in [base, max_value] (max <= base or non-finite = base), seeded
	// by reload_jitter_seed like the reload jitter.
	double roll_timing_range(double base, double max_value, uint64_t channel) const;
	double next_burst_interval_sec() const;
	double next_telegraph_sec() const;
	void roll_spin_start_phase();
	double roll_unit(uint32_t channel, uint64_t index) const;
	// Debug readout of the yellow preview rings: one PackedVector2Array
	// per extra outline layer, exactly as drawn (holder-local). Empty
	// when layers are inactive. Lets scripts/tests verify bullets sit
	// on their rings without screenshotting the editor.
	Array debug_get_layer_rings() const;
	// Debug readouts of the preview snapshot (holder-local, exactly as
	// drawn): bullet dots and the base track polyline. Empty when the
	// preview is off. Lets tests prove dots sit on the track (rotation
	// parity between volley geometry and gizmo) without an editor.
	PackedVector2Array debug_get_preview_dot_points() const;
	PackedVector2Array debug_get_preview_track_points() const;
	// Whether the drawn base track closes its last point back to the first.
	bool debug_get_preview_track_closed() const;
	// Debug coincidence check: verifies every preview dot sits on the
	// drawn geometry (base track or yellow rings). Returns { checked,
	// layers, max_deviation_px, mean_deviation_px, ok }. ok = max
	// deviation <= tolerance_px. Empty/unavailable preview (no dots)
	// returns checked=false.
	Dictionary debug_check_layer_coincidence(double tolerance_px = 1.0) const;
	// Starts a burst chain by hand (waves, boss phases): burst_count shots,
	// burst_interval_sec apart, telegraphed first when enabled. Manual
	// chains run whether auto-fire is on or not.
	void begin_burst();
	// Internal chain machinery (the auto loop drives these).
	void begin_burst_chain(bool auto_started);
	// One pattern-list entry: apply, fire, restore. True when it fired.
	bool fire_pattern_list_override(const Variant &entry, const char *caller = "BulletSpawner2D::spawn_pattern_list");
	void begin_telegraph(bool auto_started);
	void fire_burst_volley();
	// Ends a running chain early (auto-fire off, burst mode off, count
	// shrunk) and reports burst_finished once.
	void cancel_burst_chain();

	// PATTERN PREVIEW (EDITOR ONLY)
	//
	// Draws the volley pattern in the editor so helper options can be
	// tuned visually: one dot per spawn transform plus one facing arrow.
	// Rendered by two self-repainting PatternPreviewLayer2D nodes (custom
	// _draw, re-invoked by the engine on every repaint) inside an
	// owner-less holder node: the holder is never saved to the scene,
	// never exported, and never created at runtime.
	bool show_pattern_preview = true;
	// When true, the preview holder + layers are also built at runtime
	// (fresh at _ready, never serialized) and follow live transforms.
	bool show_preview_during_runtime = false;
	Color preview_dot_color = Color(1.0, 0.05, 0.05);
	Color preview_arrow_color = Color(1.0, 0.05, 0.05);
	// Bullet-0 emphasis color (pattern start: anchor / reverse / loop
	// seam). Drawn bigger on top of the regular dot.
	Color preview_first_dot_color = Color(1.0, 0.85, 0.2);
	Color preview_path_color = Color(0.3, 0.85, 1.0, 0.55);
	// Extra outline-layer rings (LAYERS placement): yellow, same alpha
	// as the base track, always drawn without antialiasing.
	Color preview_layer_path_color = Color(1.0, 1.0, 0.0, 0.55);
	double preview_path_width = 2.0;
	double preview_dot_radius = 4.0;
	// Extra pixels between the dot edge and the arrow tail: the shaft
	// starts at dot_radius + gap so it never hides under the dot.
	double preview_arrow_gap = 3.0;
	// Shaft length in pixels, measured from the gap end to the tip.
	double preview_arrow_length = 16.0;
	double preview_arrow_width = 2.0;
	double preview_arrow_head_length = 8.0;
	double preview_arrow_head_width = 10.0;
	// Collision-ring overlay: when true and spawn_data carries a valid
	// collision shape, the dots layer also draws one outline circle per
	// dot with the shape's bounding radius (circle r, rect min/2,
	// capsule height/2). Default false preserves the classic dots look.
	bool preview_draw_collision_rings = false;
	Color preview_collision_ring_color = Color(1.0, 1.0, 1.0, 0.7);
	double preview_collision_ring_width = 1.5;

	bool get_show_pattern_preview() const;
	void set_show_pattern_preview(bool value);
	bool get_show_preview_during_runtime() const;
	void set_show_preview_during_runtime(bool value);
	Color get_preview_dot_color() const;
	void set_preview_dot_color(const Color &value);
	Color get_preview_arrow_color() const;
	void set_preview_arrow_color(const Color &value);
	Color get_preview_first_dot_color() const;
	void set_preview_first_dot_color(const Color &value);
	Color get_preview_path_color() const;
	void set_preview_path_color(const Color &value);
	Color get_preview_layer_path_color() const;
	void set_preview_layer_path_color(const Color &value);
	double get_preview_path_width() const;
	void set_preview_path_width(double value);
	double get_preview_dot_radius() const;
	void set_preview_dot_radius(double value);
	double get_preview_arrow_gap() const;
	void set_preview_arrow_gap(double value);
	double get_preview_arrow_length() const;
	void set_preview_arrow_length(double value);
	double get_preview_arrow_width() const;
	void set_preview_arrow_width(double value);
	double get_preview_arrow_head_length() const;
	void set_preview_arrow_head_length(double value);
	double get_preview_arrow_head_width() const;
	void set_preview_arrow_head_width(double value);
	bool get_preview_draw_collision_rings() const;
	void set_preview_draw_collision_rings(bool value);
	Color get_preview_collision_ring_color() const;
	void set_preview_collision_ring_color(const Color &value);
	double get_preview_collision_ring_width() const;
	void set_preview_collision_ring_width(double value);

	bool get_shooting_enabled() const;
	void set_shooting_enabled(bool value);
	// Effective state: switch on, not paused, cap not reached.
	// Configured state, not ticking state: true outside the tree too.
	bool is_shooting_active() const;
	double get_shoot_interval_sec() const;
	void set_shoot_interval_sec(double value);
	double get_shoot_initial_delay_sec() const;
	void set_shoot_initial_delay_sec(double value);
	int get_max_volleys() const;
	void set_max_volleys(int value);
	int get_volleys_fired() const;
	double get_pattern_scale() const;
	void set_pattern_scale(double value);
	double get_transforms_scale() const;
	void set_transforms_scale(double value);
	Vector2 get_spawn_position_offset() const;
	void set_spawn_position_offset(const Vector2 &value);
	// How spawn_position_offset is interpreted: GLOBAL (default, the
	// historical behavior) shifts every volley by a world-space vector;
	// LOCAL treats it as a muzzle in the generator's frame, so it turns
	// (and scales) with the spawner / generator.
	enum SpawnOffsetSpace : int {
		SPAWN_OFFSET_GLOBAL = 0,
		SPAWN_OFFSET_LOCAL = 1,
	};
	SpawnOffsetSpace get_spawn_position_offset_space() const;
	void set_spawn_position_offset_space(SpawnOffsetSpace value);
	// The world-space shift a shot applies right now (space resolved).
	Vector2 resolve_spawn_offset_global() const;

	PatternSource get_pattern_source() const;
	void set_pattern_source(PatternSource value);

	NodePath get_helper_aimed_target_path() const;
	void set_helper_aimed_target_path(const NodePath &p_path);

	Node2D *get_helper_aimed_target() const;
	void set_helper_aimed_target(Node2D *target);

	// Collects one global transform per volley bullet: the Node2D children
	// of the transforms generator (else the generator itself, else this).
	TypedArray<Transform2D> collect_spawn_transforms() const;

	// ---- Movement along a Path2D (inspector group "Movement") ----------
	// The spawner node itself travels along movement_path at runtime
	// (never in the editor). Progress per leg = easing(leg time) using
	// Godot's Tween transitions, or a custom progress Curve.
	enum MovementSpace : int {
		MOVEMENT_SPACE_ATTACH = 0, // spawner sits ON the path (PathFollow2D-like)
		MOVEMENT_SPACE_RELATIVE_TO_START = 1, // path shape replayed from where the spawner started
	};
	enum MovementLoopMode : int {
		MOVEMENT_LOOP_ONCE = 0,
		MOVEMENT_LOOP_LOOP = 1, // restart from the beginning every leg
		MOVEMENT_LOOP_PING_PONG = 2, // back and forth
	};
	enum MovementDirection : int {
		MOVEMENT_DIRECTION_FORWARD = 0,
		MOVEMENT_DIRECTION_REVERSE = 1,
	};
	enum MovementTiming : int {
		MOVEMENT_TIMING_DURATION = 0, // each leg takes movement_duration_sec
		MOVEMENT_TIMING_SPEED = 1, // each leg takes length / movement_speed
	};

	bool get_movement_enabled() const;
	void set_movement_enabled(bool value);
	NodePath get_movement_path() const;
	void set_movement_path(const NodePath &p_path);
	Path2D *get_movement_path_node() const;
	void set_movement_path_node(Path2D *node);
	MovementSpace get_movement_space() const;
	void set_movement_space(MovementSpace value);
	MovementLoopMode get_movement_loop_mode() const;
	void set_movement_loop_mode(MovementLoopMode value);
	MovementDirection get_movement_direction() const;
	void set_movement_direction(MovementDirection value);
	int get_movement_loops() const;
	void set_movement_loops(int value);
	MovementTiming get_movement_timing() const;
	void set_movement_timing(MovementTiming value);
	double get_movement_duration_sec() const;
	void set_movement_duration_sec(double value);
	double get_movement_speed() const;
	void set_movement_speed(double value);
	Tween::TransitionType get_movement_transition() const;
	void set_movement_transition(Tween::TransitionType value);
	Tween::EaseType get_movement_ease() const;
	void set_movement_ease(Tween::EaseType value);
	Ref<Curve> get_movement_progress_curve() const;
	void set_movement_progress_curve(const Ref<Curve> &value);
	double get_movement_start_ratio() const;
	void set_movement_start_ratio(double value);
	double get_movement_start_delay_sec() const;
	void set_movement_start_delay_sec(double value);
	double get_movement_endpoint_pause_sec() const;
	void set_movement_endpoint_pause_sec(double value);
	bool get_movement_rotate_with_path() const;
	void set_movement_rotate_with_path(bool value);
	double get_movement_rotation_offset_deg() const;
	void set_movement_rotation_offset_deg(double value);
	bool get_movement_cubic_sampling() const;
	void set_movement_cubic_sampling(bool value);
	bool get_movement_autostart() const;
	void set_movement_autostart(bool value);
	bool get_inherit_movement_velocity() const;
	void set_inherit_movement_velocity(bool value);
	double get_movement_velocity_inherit_factor() const;
	void set_movement_velocity_inherit_factor(double value);

	// Playback API (runtime). play() starts or resumes; a finished ONCE
	// run restarts from the beginning. stop() halts and optionally snaps
	// back to the start pose. seek() jumps to a progress ratio of the
	// current leg (0..1, along the leg's direction).
	void movement_play();
	void movement_pause();
	void movement_stop(bool reset_to_start = true);
	void movement_seek(double ratio);
	// Flips the current leg's direction in place (keeps the position).
	void movement_reverse();
	// Moves the spawner to a global position without travel: no velocity
	// for the jump (nothing to inherit), and a playing or paused movement
	// goes on from the new spot with the same path shape (Relative To
	// Start shifts its anchor, Attach shifts the path; a stop back to the
	// start or a fresh play drops that shift). carry_live_bullets moves this
	// spawner's flying bullets by the same jump.
	void teleport(const Vector2 &new_global_position, bool carry_live_bullets = false);
	bool is_movement_playing() const;
	// Distance ratio along the path, 0 = path start, 1 = path end.
	double get_movement_progress() const;
	// Completed legs since play() (a leg = one traversal of the path).
	int get_movement_leg() const;
	// Velocity of the last movement step, px/s (zero while not moving).
	Vector2 get_movement_velocity() const;
	// Test hook: the C++ easing used by movement (Tween parity tests).
	static double debug_ease(double t, int transition, int ease);
	PackedStringArray _get_configuration_warnings() const override;
	// Same list as the editor warning triangle, callable from scripts
	// (debug UIs, CI scene checks).
	PackedStringArray get_setup_warnings() const { return _get_configuration_warnings(); }

	// Pattern bake cache (see PatternBake). AUTO: patterns whose raw
	// transforms provably follow the generator rigidly are generated once
	// and re-posed per shot; OFF: always regenerate (debugging, or
	// scripts that mutate pattern inputs behind the setters' back).
	enum PatternCacheMode : int {
		PATTERN_CACHE_AUTO = 0,
		PATTERN_CACHE_OFF = 1,
	};
	PatternCacheMode get_pattern_cache_mode() const;
	void set_pattern_cache_mode(PatternCacheMode value);
	// {hits, misses, bakes, version, shot_class, preview_class}
	// (class: 0 none, 1 translation, 2 rigid, -1 not baked yet).
	virtual Dictionary debug_get_pattern_cache_info() const;
	// Uncached collect (same output contract as collect_spawn_transforms):
	// the reference the parity tests compare the cache against.
	TypedArray<Transform2D> debug_collect_spawn_transforms_uncached() const;
	static void debug_set_pattern_cache_verify(bool enabled);
	static bool debug_get_pattern_cache_verify();
	// {rebuilds, dots_draws, arrows_draws, last_rebuild_usec}: preview
	// instrumentation (spin/move must not rebuild or redraw).
	Dictionary debug_get_preview_stats() const;
	// Fires one volley immediately (counts, re-arms the timer). Returns
	// false when misconfigured or the factory refused (busy/teardown).
	// Safe to call from _physics_process (same-shape pool reuse applies
	// immediately; only a shape-type change defers internally). Never call
	// from inside a spawner signal handler (volley_homing_configured,
	// homing_targets_resolved, volley_fired): nested calls are rejected;
	// use shoot_once_deferred() there instead.
	bool shoot_once();
	// Deferred variant for signal handlers / colliding contexts: queues a
	// shoot_once() with call_deferred() and returns true when queued.
	// Rejected (false) when the spawner is not in the tree.
	bool shoot_once_deferred();
	// Zeroes the volley counter and re-arms with the initial delay.
	void reset_shooting();
	// Fire exactly n more volleys, then pause (one-shot budget counting
	// every fired volley, auto or manual). max_volleys is never touched,
	// so unlimited stays unlimited. Replaces any previous budget.
	// n <= 0 is rejected.
	void fire_n_volleys(int n);
	// Pause auto-shooting keeping all counters (unlike shooting_enabled
	// which is the master switch): resume continues the current wave.
	void pause_shooting();
	void resume_shooting();
	bool is_shooting_paused() const;
	// Volleys left before max_volleys trips or the one-shot budget runs
	// out, whichever is smaller. -1 = infinite (no cap, no budget).
	int volleys_remaining() const;
	// Countdowns in seconds of the spawner's own clock (time_scale applies),
	// 0 when nothing counts down: the next automatic trigger pull, the next
	// burst shot of a running chain, the warning in progress, the next
	// pattern-list entry.
	double get_shoot_time_left() const;
	double get_burst_time_left() const;
	bool is_telegraph_pending() const;
	double get_telegraph_time_left() const;
	double get_pattern_list_time_left() const;

	virtual void _ready() override;
	virtual void _process(double delta) override;
	// Plain name (no override): godot-cpp routes notifications to this
	// method through the binding machinery, same as BulletFactory2D.
	void _notification(int p_what);

	// Hides the helper_* property groups that don't belong to the active
	// pattern_source, so the inspector only shows relevant options.
	// (Name hiding, picked up by the binding machinery - not an override.)
	void _validate_property(PropertyInfo &p_property) const;

	// ---- Hooks for the derived spawner -------------------------------
	// shoot_once runs: factory check -> shot_preflight -> despawn-bounds
	// muzzle check -> shot_prepare -> pattern collect + placement rolls ->
	// fire_shot -> On Shot sound, budgets, cap transition. A refusal ends
	// the shot exactly like the base checks do (error, volley_skipped).
	virtual ShotRefusal2D shot_preflight(BulletFactory2D *factory) { return ShotRefusal2D(); }
	virtual ShotRefusal2D shot_prepare() { return ShotRefusal2D(); }
	// Spawns what this spawner fires along `transforms` (posed, rolled) and
	// counts the shot (volleys_fired) once it is reported.
	virtual ShotResult2D fire_shot(std::vector<Transform2D> &transforms);
	// Extra reasons to keep _process alive, and its per-frame work (after
	// the preview refresh, before the cadence; false = this node was freed).
	virtual bool needs_process_owned() const { return false; }
	virtual bool process_owned(double delta) { return true; }
	// _ready at runtime and reset_shooting: the derived clocks restart.
	virtual void restart_owned_clocks() {}
	// PREDELETE once the free is accepted (not refused by a guard), with the
	// live factory (skipped while it tears down or in the editor).
	virtual void on_predelete_owned(BulletFactory2D *factory) {}
	// EXIT_TREE: drop scratch that may hold scene nodes.
	virtual void on_exit_tree_owned() {}
	// Every config setter (on_config_changed): re-arm warn-once latches.
	virtual void on_config_changed_owned() {}
	// teleport(..., carry_live_bullets = true): move what this spawner fired.
	virtual void shift_owned_shots(const Vector2 &jump) {}
	// Setup warnings of the derived class, in the base's order: what fires
	// (right after the factory checks), then its steering groups (after
	// movement, before graze and sound).
	virtual void append_payload_warnings(PackedStringArray &r_warnings) const {}
	virtual void append_setup_warnings(PackedStringArray &r_warnings) const {}
	// The editor preview's hitbox ring radius (0 = none).
	virtual real_t preview_collision_ring_radius() const { return 0.0; }
	// True when what this spawner fires never culls at the despawn bounds
	// (the muzzle check then never refuses a shot).
	virtual bool shots_ignore_despawn_bounds() const { return false; }
	// The pattern-list / shoot_once_with entry key that swaps what the
	// spawner fires (nullptr = none), its class for the error text, and the
	// swap (false = wrong type, nothing changed).
	virtual const char *payload_key() const { return nullptr; }
	virtual String payload_class_name() const { return String(); }
	virtual bool apply_payload_override(const Variant &value) { return false; }

protected:
	static void _bind_methods();

	// Shot, cadence, preview and movement state (the derived spawners read it).
	// Countdown to the next volley; volleys fired since (re)arming.
	double shoot_time_left = 0.0;
	int volleys_fired = 0;
	// Pause latch (pause_shooting/resume_shooting): auto_shooting_active()
	// stays false while set, but volleys_fired/max_volleys are preserved
	// so resume continues the current wave instead of restarting it.
	bool shooting_paused = false;
	// One-shot budget from fire_n_volleys(n): counts every fired volley
	// (auto or manual) down to 0, then pauses. -1 = no budget.
	// Never touches max_volleys, so unlimited (-1) stays unlimited.
	int oneshot_volleys_left = -1;
	// Re-entrancy latch for shoot_once(): its emits run user handlers
	// synchronously, which must not nest another shoot_once(). Held across
	// apply + emit + cap transition; use shoot_once_deferred() instead.
	// Cross-spawner A->B->A nesting is stopped by the file-local global
	// depth guard in the .cpp.
	bool shoot_once_reentrant_guard = false;
	// A free() of this spawner from user code inside a shot (an attachment
	// callback of the new volley runs outside any of our emits) is refused
	// in PREDELETE with cancel_free(): the shot and its callers still use
	// `this`. The latch's clear-point finishes it with queue_free().
	bool refused_free_during_shot = false;
	// A handler inside the running shot freed this spawner (refused, and
	// finished at the end of the frame): the shot stops.
	bool shot_owner_dying() const { return refused_free_during_shot || emit_guard.refused_free || is_queued_for_deletion(); }
	// Single clear-point for the latch + global depth above.
	void clear_shoot_once_latch();
	// Dedicated RNG for reload jitter (mutable: used by the const
	// next_shoot_interval_sec). Separate from homing_rng: jitter reseeds
	// would otherwise corrupt the homing target sequence.
	mutable Ref<RandomNumberGenerator> jitter_rng;
	// Seed 0 (non-deterministic jitter) randomizes the generator once,
	// not on every interval.
	mutable bool jitter_rng_randomized = false;
	// Spin runtime state (never stored, advances in _process only).
	double spin_angle_deg = 0.0;
	double spin_time_sec = 0.0;
	// Burst runtime state (never stored): shots left in the current burst,
	// countdown to the next burst shot, telegraph countdown, and the
	// mirror flag for the next burst volley.
	int burst_shots_left = 0;
	// Bumped when a chain starts or ends: a burst_shot_fired / volley_fired
	// handler that cancels, restarts or resets the chain owns it (the running
	// shot touches no counter after its emits once this moved).
	uint32_t burst_serial = 0;
	// Bumped by reset_shooting and fire_n_volleys: a shot handler calling
	// them owns the cadence (the reporting shot neither spends the new budget
	// nor rearms the interval over the restarted delay).
	uint32_t cadence_serial = 0;
	// Shots fired in the current chain (shot index and mirror rhythm).
	int burst_shots_fired = 0;
	// True when the auto loop started the chain: it then follows the
	// auto-fire rules (clamped to volleys_remaining, cancelled when
	// shooting_enabled turns off, errors latched once).
	bool burst_from_auto = false;
	// Same for a pending telegraph.
	bool telegraph_from_auto = false;
	double burst_time_left = 0.0;
	double telegraph_time_left = 0.0;
	bool burst_mirror_next = false;
	// Consecutive shoot_once() failures inside the current burst chain.
	// A failed shot is retried (not consumed), so transient hitches never
	// eat burst shots; after a full burst's worth of consecutive failures
	// the failure is permanent (bad config, hard over-budget) and the
	// chain aborts with burst_finished instead of retrying forever.
	// Reset on every success and every new chain.
	int burst_consecutive_failures = 0;
	// True once the current burst chain has shown its telegraph: stops
	// the expiry re-firing the warning in a loop instead of firing.
	bool burst_telegraph_done = false;
	// NOTE: nesting protection for burst chains is solely the
	// shoot_once() latch + global depth (a handler calling shoot_once()
	// from any spawner emission is rejected; use shoot_once_deferred()).
	// A dedicated burst_firing flag was removed: it was write-only and
	// shoot_once() never checked it, so it protected nothing.
	// Telegraph runtime: the pending volley config while warning. Stored
	// as a flag only (transforms re-collect at fire time, so markers that
	// move during the warning still aim correctly).
	bool telegraph_pending = false;
	// Pattern sequencer (BLAST-style spawn_list): queued entries fired in
	// order (interval apart) or all at once. Runtime state, never stored.
	// Each entry is a Dictionary: { "pattern_source": int,
	// "helper_bullets_amount": int, "spawn_data": Ref, "preset": int }.
	// Only set keys override the live spawner for that shot.
	Array pattern_list_entries;
	double pattern_list_interval_sec = 0.25;
	int pattern_list_cursor = 0;
	double pattern_list_time_left = 0.0;
	bool pattern_list_active = false;
	// Bumped by every spawn_pattern_list / stop_pattern_list call: a handler
	// that started or stopped a list inside an entry's shot owns what follows.
	uint32_t pattern_list_serial = 0;
	// Preview source tracking: dirty-check state for the live-refresh loop.
	// Only instance ids + global transforms are stored - never assumed
	// alive. Every access goes through is_tracked_node_alive() first, so
	// a freed marker/generator/target can never crash the game.
	uint64_t tracked_base_id = 0;
	Node2D *tracked_base = nullptr;
	Transform2D tracked_base_global;
	bool tracked_has_base_global = false;
	double tracked_spin_angle = 0.0;
	uint64_t tracked_target_id = 0;
	Transform2D tracked_target_origin;
	bool tracked_has_target_origin = false;
	PackedVector2Array tracked_marker_origins;
	PackedRealArray tracked_marker_rots;
	PackedInt64Array tracked_marker_ids;
	int tracked_child_count = -1;
	bool tracked_has_self = false;
	// Custom preview tracking: snapshot of the stored array for live
	// dirty checks (transform compare, no dereference).
	TypedArray<Transform2D> tracked_custom_transforms;
	// Path2D preview gating: curve baking every tick is the priciest
	// dirty check, so the full resample runs on node change or every
	// 15th tick (~0.25 s staleness bound for in-place curve edits);
	// node identity + transform compare every tick (cheap). Mutable:
	// the dirty check runs from const _process.
	mutable int preview_path2d_sample_cooldown = 0;
	mutable uint64_t tracked_path2d_node_id = 0;
	mutable Transform2D tracked_path2d_node_global;
	mutable bool tracked_has_path2d_node_global = false;
	// Editor-only pattern preview holder (null at runtime, never saved),
	// plus its two self-repainting _draw layers (dots + arrows).
	// The pointers may dangle (a script clearing an external generator's
	// children frees the holder with them): read them through the live_*
	// accessors, which check the ids through ObjectDB.
	Node2D *preview_holder = nullptr;
	PatternPreviewLayer2D *preview_dots_layer = nullptr;
	PatternPreviewLayer2D *preview_arrows_layer = nullptr;
	uint64_t preview_holder_id = 0;
	uint64_t preview_dots_layer_id = 0;
	uint64_t preview_arrows_layer_id = 0;
	void set_preview_nodes(Node2D *holder, PatternPreviewLayer2D *dots, PatternPreviewLayer2D *arrows);
	PatternPreviewLayer2D *live_preview_dots() const {
		return is_tracked_node_alive(preview_dots_layer, preview_dots_layer_id) ? preview_dots_layer : nullptr;
	}
	PatternPreviewLayer2D *live_preview_arrows() const {
		return is_tracked_node_alive(preview_arrows_layer, preview_arrows_layer_id) ? preview_arrows_layer : nullptr;
	}
	// A cached preview node was freed behind our back: the gizmo must rebuild.
	bool preview_nodes_lost() const {
		return (preview_holder != nullptr && !is_tracked_node_alive(preview_holder, preview_holder_id)) ||
				(preview_dots_layer != nullptr && live_preview_dots() == nullptr) ||
				(preview_arrows_layer != nullptr && live_preview_arrows() == nullptr);
	}
	// Last built layer rings (one PackedVector2Array per extra layer,
	// holder-local, exactly what the yellow pass draws: runs
	// INF-separated with a closing duplicate per closed run). Debug
	// readout for scripts/tests; empty when layers are inactive.
	Array preview_last_layer_rings;
	// Re-entrancy latch for rebuild_preview(): setters, tree notifications
	// (child order / transform changed) and the preview loop can all ask
	// for a rebuild while one is already running (holder add_child fires
	// NOTIFICATION_CHILD_ORDER_CHANGED synchronously). A nested entry is
	// always redundant — the outer pass re-reads everything — so it
	// returns immediately instead of recursing into a stack overflow.
	// Mutable: rebuilds happen from const setters. Restored on every exit
	// path (early returns included) so one abort can never wedge preview.
	mutable bool preview_rebuild_in_progress = false;
	// Editor coalescing state for rebuild_preview(): a queued deferred
	// flush plus the one-shot bypass that lets it build instead of
	// re-queueing. Runtime path never touches either (synchronous).
	mutable bool preview_rebuild_queued = false;
	mutable bool preview_sync_rebuild = false;
	void _do_queued_preview_rebuild();
	// Set only around the preview's collect_spawn_transforms_impl() call so
	// the snapshotted gizmo geometry is spin-free. The layer then applies
	// spin_angle_deg at draw time, which is what keeps an advancing spin
	// from forcing a full rebuild (and a 10k-element reallocation) every
	// frame. Mutable because the collect path is const.
	mutable bool preview_suppress_spin = false;

	bool auto_shooting_active() const;
	// Advances spin_angle_deg by delta according to spin_mode.
	void advance_spin(double delta);
	// Whether the preview may exist right now: editor always (toggle
	// decides), runtime only when the user opted in.
	bool preview_allowed_here() const;
	// Whether the preview refresh loop must run: allowed + toggled on.
	bool preview_active() const;
	// Re-poses the cached preview snapshot for a new spin angle without
	// regenerating geometry. Returns nothing; a non-finite angle is
	// ignored (the caller falls back to a full rebuild). Keeps
	// tracked_spin_angle in step so the next rebuild is not treated as
	// stale geometry.
	void set_preview_pose(double spin_angle_degrees);
	// Snapshots the currently observed source nodes (validated ids +
	// global transforms) without touching the preview itself.
	void snapshot_preview_sources();
	// True when any tracked source moved, appeared, or vanished
	// (freed nodes count as dirty, never as a crash).
	bool preview_sources_dirty();
	// Enables/disables _process for the preview live-refresh loop.
	// Editor: processing runs only while the preview is on. Runtime:
	// never touched here (shooting/spinning own it) - the runtime
	// preview piggy-backs the existing loop via preview_sources_dirty().
	void update_preview_process_state();
	// (Re)builds the editor preview from the current pattern.
	// No-op outside the editor or when the preview is disabled.
	void rebuild_preview();
	// collect_spawn_transforms() with error reporting: the public method
	// reports problems, the preview passes true to stay quiet.
	TypedArray<Transform2D> collect_spawn_transforms_impl(bool quiet) const;
	// The same collect into a native buffer (no Variant): the shot path.
	void collect_spawn_transforms_native(bool quiet, std::vector<Transform2D> &r_out) const;
	// Reused buffers (no per-shot allocation once warm).
	mutable std::vector<Transform2D> collect_scratch;
	mutable std::vector<uint8_t> skip_mask_scratch;
	std::vector<Transform2D> shot_transforms;

	// ---- Pattern bake cache ------------------------------------------
	// How a pattern's RAW transforms (pre spin/scale/skip) follow the
	// generator marker. Measured at bake time with probe markers (never
	// assumed), so per-source flags (rotate_with_marker, seeds...) are
	// classified by what the generator actually does:
	//   RIGID       raw(D * M) == D * raw(M) for any rigid motion D
	//   TRANSLATION raw(M + d) == raw(M) + d (same basis only)
	//   NONE        regenerate every time (reads other nodes, random)
	// The bake cache itself lives in the patterns module
	// (patterns/pattern_bake_cache2d.hpp): one bake per channel (shot,
	// mirrored shot, preview), classified by probing.
	mutable PatternBakeCache2D pattern_cache;
	int pattern_cache_mode = PATTERN_CACHE_AUTO;

	// Movement properties (see the public block).
	bool movement_enabled = false;
	NodePath movement_path;
	mutable Path2D *movement_path_cache = nullptr;
	mutable uint64_t movement_path_id = 0;
	int movement_space = MOVEMENT_SPACE_ATTACH;
	int movement_loop_mode = MOVEMENT_LOOP_ONCE;
	int movement_direction = MOVEMENT_DIRECTION_FORWARD;
	int movement_loops = 0;
	int movement_timing = MOVEMENT_TIMING_DURATION;
	double movement_duration_sec = 3.0;
	double movement_speed = 200.0;
	int movement_transition = 0; // Tween.TRANS_LINEAR
	int movement_ease = 2; // Tween.EASE_IN_OUT
	Ref<Curve> movement_progress_curve;
	double movement_start_ratio = 0.0;
	double movement_start_delay_sec = 0.0;
	double movement_endpoint_pause_sec = 0.0;
	bool movement_rotate_with_path = false;
	double movement_rotation_offset_deg = 0.0;
	bool movement_cubic_sampling = false;
	bool movement_autostart = true;
	bool inherit_movement_velocity = false;
	double movement_velocity_inherit_factor = 1.0;
	// Movement runtime state.
	bool movement_playing = false;
	bool movement_finished = false;
	double movement_leg_elapsed = 0.0;
	double movement_delay_left = 0.0;
	double movement_pause_left = 0.0;
	int movement_legs_completed = 0;
	bool movement_leg_forward = true;
	double movement_progress = 0.0; // distance ratio along the path
	Vector2 movement_start_origin; // RELATIVE_TO_START anchor (global)
	bool movement_has_last_position = false;
	// The global position the movement itself last applied: velocity is
	// measured from it, so a position written by hand in between is never
	// motion a shot inherits.
	Vector2 movement_last_pose;
	// teleport() jumps an Attach path along (Relative To Start moves its
	// anchor instead); movement_reset_state drops it.
	Vector2 movement_teleport_offset;
	// Bumped by every playback control (play, pause, stop, seek, reverse,
	// enable): a movement signal handler that used one owns what happens next.
	uint32_t movement_epoch = 0;
	// Set on EXIT_TREE: the first pose after re-entry is a jump (no velocity).
	bool movement_jumped = false;
	Vector2 movement_velocity;
	bool movement_warned_unusable_path = false;
	// Advances playback by delta and moves the node. Returns false when
	// a signal handler freed this spawner (caller must stop touching it).
	bool advance_movement(double delta);
	// Moves the node to the pose at the current progress. Returns false
	// when the path is unusable.
	bool apply_movement_pose(double delta);
	double movement_leg_duration(double path_length) const;
	double movement_eased(double leg_ratio) const;
	bool movement_active() const;
	void movement_reset_state();
	// Auto-fire error latch (see shoot_once fail_early).
	bool auto_fire_in_progress = false;
	uint32_t shoot_error_latch = 0;
	void on_config_changed();
	// Bypass flag for debug_collect_spawn_transforms_uncached / verify.
	mutable bool pattern_cache_bypass = false;
	uint64_t preview_rebuild_count = 0;
	uint64_t preview_last_rebuild_usec = 0;
	// Bumped by every geometry-affecting change (mark_pattern_dirty).
	uint64_t pattern_version = 1;
	// The raw pattern for `marker` straight from the generators (the old
	// collect switch). No cache.
	PatternSlots2D generate_raw_pattern(Node2D *base, const Transform2D &marker, real_t mirror_sign, bool quiet) const;
	// Cache-aware raw pattern into r_raw (std::vector, no Variant).
	void resolve_raw_pattern(Node2D *base, const Transform2D &marker, real_t mirror_sign, bool quiet, std::vector<Transform2D> &r_raw) const;
	// Whether the preview snapshot stays valid when the generator moves
	// from `old_marker` to `new_marker` (same rule as cache reuse).
	bool preview_survives_marker_move(const Transform2D &old_marker, const Transform2D &new_marker) const;
	void mark_pattern_dirty();
	// mark_pattern_dirty() + rebuild_preview(): the one call every
	// geometry setter makes. Inside a pattern batch the rebuild waits for
	// the batch to end (one rebuild for many setter calls).
	void on_pattern_changed();
	int pattern_batch_depth = 0;
	bool pattern_batch_dirty = false;
	void begin_pattern_batch();
	void end_pattern_batch();
	// Pattern-list entries are temporary overrides: snapshot every knob an
	// entry or a preset can touch (Bullet Patterns, Spin, spawn_data) and
	// restore the ones that changed.
	Dictionary snapshot_pattern_state() const;
	void restore_pattern_state(const Dictionary &snapshot);
	// Clean presets: Bullet Patterns knobs (except the Transform subgroup
	// and node/array wiring) and Spin back to their class defaults.
	void reset_pattern_knobs_to_defaults();
	// On entering the tree: rewrites each NodePath from the node its
	// id-validated cache points at (reparents, out-of-tree pointer
	// assignments), so the saved paths always name the live target.
	void fill_assigned_node_paths();
	// Canonical keep-awake predicate for _process: shooting, spinning,
	// retargeting, previewing, bursting, telegraphing or sequencing.
	// Every setter uses refresh_process_state() instead of spelling the
	// set out (the members had drifted: some sites forgot burst,
	// telegraph, pattern_list or retarget and could sleep _process while
	// work was pending). The _process sleep path intentionally omits
	// auto_shooting_active (it only runs when auto is off) and is the
	// single exception.
	bool needs_process() const;
	bool needs_process_besides_shooting() const;
	void refresh_process_state();
	// Same, skipped in the editor where the preview owns processing.
	void refresh_process_state_editor_guarded();
	// Predictive lead for the aimed target, shared by the aimed volley
	// and its preview cone so both agree on where the target will be.
	// Uses the velocity snapshot of the current collect (no user code).
	Vector2 predict_target_pos(Node2D *target) const;
	// Reads the aimed target's get_velocity() ONCE per collect: a script
	// method is user code that may free this spawner (refused while it runs
	// and finished with queue_free(), core/emit_guard2d.hpp), the factory or
	// the target. Returns false when this spawner is dying after it.
	bool snapshot_aim_velocity() const;
	mutable Vector2 aim_velocity_snapshot;
	mutable bool aim_velocity_snapshot_valid = false;
	// Corridor aim resolution, shared by the corridor volley and its
	// preview wall: live aimed target when usable, else the fallback.
	Vector2 resolve_corridor_aim(const Vector2 &fallback, const Vector2 &origin) const;
	// The graze target finder shared with every volley this spawner armed
	// (created on first use; the factory's runtime preview reads it).
	mutable std::shared_ptr<GrazeDetector2D> graze_detector;
	// The graze_* target settings as a detector configuration.
	GrazeDetector2D::Config graze_detector_config() const;
	// Pushes them into the detector (rescans due), then the config
	// warnings and the ring preview follow.
	void apply_graze_detector_config();
	// The targets as the runtime tests them (the detector's list while the
	// factory runs, a fresh scan otherwise), this spawner excluded.
	void collect_graze_targets(std::vector<GrazeTarget2D> &r_targets) const;
	mutable std::vector<GrazeTarget2D> graze_targets_scratch;
	// graze_zones as volleys arm them (the first 4 slots, nulls in place;
	// in-place misuse warned once). r_any: at least one zone.
	Array spawner_graze_zones(bool &r_any);
	// graze_zones mutated in place (append past 4 zones, non-zone entries):
	// warned once per assignment, the usable entries still arm.
	bool graze_zones_misuse_warned = false;
	// The sound listener finder shared with every volley this spawner armed
	// (created on first use).
	mutable std::shared_ptr<GrazeDetector2D> sound_detector;
	// The sound_* listener settings as a detector configuration (Godot
	// Listener maps to no detector scan: the mixer measures from Godot).
	GrazeDetector2D::Config sound_detector_config() const;
	// Pushes them into the detector (rescans due), then the config warnings
	// follow.
	void apply_sound_detector_config();
	// The listeners as the runtime measures from them (the detector's list
	// while the factory runs, a fresh scan otherwise), this spawner
	// excluded.
	void collect_sound_listeners(std::vector<GrazeTarget2D> &r_targets) const;
	mutable std::vector<GrazeTarget2D> sound_listeners_scratch;
	// sound_effects as volleys arm them (nulls in place; in-place misuse
	// warned once). r_any: at least one entry.
	Array spawner_sound_entries(bool &r_any);
	// sound_effects mutated in place (non-sound entries): warned once per
	// assignment, the usable entries still arm.
	bool sound_effects_misuse_warned = false;
	// Fires the spawner-only entries (On Shot after a successful shot, On
	// Telegraph with volley_telegraphed) at the spawner's global position.
	// volley_amount is the fired volley's size for the amount gates (1 when
	// unknown, e.g. telegraphs). Runs no user code.
	void fire_spawner_sound(int trigger, int volley_amount = 1);
	// Zone resources' `changed` -> ring preview refresh.
	void connect_graze_zones(bool connect);
	void _on_graze_zone_changed();
	// Ring preview: allowed + toggled on (editor: graze_show_preview;
	// runtime: also graze_preview_during_runtime) + graze on.
	bool graze_preview_active() const;
	// Rebuilds the ring snapshot from the live targets and pushes it to the
	// layer (one redraw only when it changed); hides the layer when the
	// preview is off. Never dereferences a stored target: ids only.
	void refresh_graze_preview();
	// The layer under this spawner (cached pointer validated by id, healed
	// by name), created on demand.
	GrazePreviewLayer2D *resolve_graze_preview_layer(bool create);
	GrazePreviewLayer2D *graze_preview_layer = nullptr;
	uint64_t graze_preview_layer_id = 0;
	std::vector<GrazePreviewCircle2D> graze_preview_scratch;
	// Sequencer internals: validates one Dictionary entry (preset /
	// source / amount / spawn_data overrides) and fires the next queued
	// entry for the _process driver.
	bool apply_pattern_list_entry(const Variant &entry, const char *caller = "BulletSpawner2D::spawn_pattern_list");
	void fire_pattern_list_entry();
	// Live Path2D outline in generator-local pixels. Empty when unusable;
	// quiet suppresses warnings (preview).
	PackedVector2Array sample_path2d_polyline(bool quiet) const;
	// True for the closed-loop outline modes the outline layout engine
	// supports (Ring, Ellipse, Star, Flower, Rose, Lissajous, Circle,
	// Rectangle, Square, Polygon). Scatter/gap modes and open
	// curves are excluded: without a loop there is no inside.
	static bool supports_outline_layout(PatternSource source);
};

} // namespace BlastBullets2D

// Need this in order to expose the enum to Godot Engine
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::PatternSource);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::Path2DDistribution);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::Path2DOverflow);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::Path2DAnchor);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::Path2DFacing);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::Path2DSpace);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::SpinMode);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::BulletScaleMode);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::HomingNodeNameMatch);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::GrazeTargetSource);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::SoundListenerSource);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::PatternCacheMode);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::MovementSpace);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::MovementLoopMode);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::MovementDirection);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::MovementTiming);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::PatternSpawner2D::SpawnOffsetSpace);
