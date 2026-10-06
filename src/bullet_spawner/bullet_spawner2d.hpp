#pragma once

#include "bullet_spawner/volley_tracker2d.hpp"
#include "bullet_volley/bullet_volley2d.hpp"
#include "debugger/graze_preview_layer2d.hpp"
#include "factory/bullet_factory2d.hpp"
#include "patterns/bullet_patterns2d.hpp"
#include "patterns/pattern_bake_cache2d.hpp"
#include "patterns/pattern_knobs2d.hpp"
#include "patterns/pattern_registry2d.hpp"
#include <vector>

#include "data/bullet_graze_zone2d.hpp"
#include "data/bullet_volley_data2d.hpp"
#include "godot_cpp/classes/array_mesh.hpp"
#include "godot_cpp/classes/curve.hpp"
#include "godot_cpp/classes/multi_mesh.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/path2d.hpp"
#include "godot_cpp/classes/random_number_generator.hpp"
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

class BulletSpawner2D : public Node2D, public PatternKnobs2D {
	GDCLASS(BulletSpawner2D, Node2D)

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
	enum PatternSource {
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
	enum Path2DDistribution {
		PATH2D_DISTRIBUTION_FIXED_SPACING = 0,
		PATH2D_DISTRIBUTION_EVEN
	};

	// Path2D overflow: what happens when a fixed run does not fit
	// (run longer than the curve, or shifted past its ends). CLAMP piles
	// extras at the end, WRAP continues from the start (mod length),
	// SHRINK_TO_FIT scales spacing down so the run fits exactly.
	enum Path2DOverflow {
		PATH2D_OVERFLOW_CLAMP = 0,
		PATH2D_OVERFLOW_WRAP,
		PATH2D_OVERFLOW_SHRINK_TO_FIT
	};

	// Path2D anchor: where a fixed run sits when shorter than the curve.
	enum Path2DAnchor {
		PATH2D_ANCHOR_START = 0,
		PATH2D_ANCHOR_CENTER,
		PATH2D_ANCHOR_END
	};

	// Path2D facing: ALONG_PATH aims +X with travel (tangent), the two
	// NORMAL modes aim across it (+/-90 degrees). facing_offset applies
	// on top of all three.
	enum Path2DFacing {
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
	enum Path2DSpace {
		PATH2D_SPACE_FOLLOW_GENERATOR = 0,
		PATH2D_SPACE_AT_PATH2D = 1
	};

	// How the spin angle evolves. CONTINUOUS rotates forever at
	// spin_speed_deg_per_sec (signed: positive = clockwise, per the
	// Godot 2D convention); OSCILLATE swings +-spin_amplitude_deg at
	// spin_frequency_hz instead, ignoring the speed.
	enum SpinMode {
		SPIN_CONTINUOUS = 0,
		SPIN_OSCILLATE
	};

	// What happens to this spawner's in-flight volleys when the spawner
	// is FREED (never on a reparent / tree exit). Signal connections
	// live on the emitter, so once the spawner is gone nothing can handle
	// its bullets' hits or expiry - not even a callback that belongs to a
	// node that is still alive. Serialized ids: never renumber.
	enum OrphanedVolleys {
		// Bullets keep flying and dying; their hits reach no one, and a
		// one-time warning explains why (default).
		ORPHANED_VOLLEYS_KEEP_FLYING = 0,
		// The volleys become factory-owned: BulletFactory2D's
		// area_entered / body_entered / life_time_over fire for them.
		ORPHANED_VOLLEYS_HAND_TO_FACTORY = 1,
		// Every live bullet is cleared with its On Clear effects.
		ORPHANED_VOLLEYS_CLEAR = 2,
		// Every live bullet is removed silently.
		ORPHANED_VOLLEYS_REMOVE = 3
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
	Ref<BulletVolleyData2D> spawn_data;

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

	Ref<BulletVolleyData2D> get_spawn_data() const;
	void set_spawn_data(const Ref<BulletVolleyData2D> &new_spawn_data);

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

	// PERFORMANCE / REPRODUCIBILITY.
	// Soft live-bullet fuse: 0 = unlimited, otherwise auto-shooting and
	// retargeting pause while active live bullets reach this count.
	int max_live_bullets = 0;
	// Stagger the retarget phase so N spawners don't scene-scan on the same
	// tick: actual period stays homing_retarget_interval_sec.
	double homing_retarget_phase = 0.0;

	// HOMING (EASY API)
	//
	// The spawner resolves homing targets at volley time and pushes them
	// onto the controllable BulletVolley2D instance returned by the
	// factory, using that class's shared / per-bullet deque API. Nothing
	// is stored in spawn_data: the same .tres stays movement-only while
	// every volley can chase different targets.
	//
	// Engine rules that still apply (handled for you, documented so the
	// behavior never surprises):
	// - Enabling (pool reuse) wipes all homing/orbit state, so the
	//   configuration below is re-applied on EVERY volley.
	// - Orbiting without a homing target does nothing: enable orbiting
	//   only takes effect once a non-empty deque exists, which is why
	//   targets are always pushed before orbiting is enabled.
	// - homing_take_control_of_texture_rotation defaults to TRUE here
	//   (the engine class defaults to false, which silently steers
	//   nothing). Turn it off only when a movement pattern, rotation
	//   data, or orbiting texture mode already owns the facing.
	enum HomingMode {
		HOMING_SHARED = 0, // one deque shared by every bullet of the volley
		HOMING_PER_BULLET // every bullet owns its own deque (same queue each)
	};

	// Where volley homing targets come from. Each source owns its own
	// setting group below (same dropdown-and-details pattern as
	// pattern_source): only the active source's options show in the
	// inspector, plus the shared steering block.
	enum HomingTargetSource {
		HOMING_SOURCE_NODE_GROUP = 0, // poll get_nodes_in_group(homing_node_group)
		HOMING_SOURCE_MOUSE, // chase the mouse cursor position
		HOMING_SOURCE_GLOBAL_POSITION, // chase homing_global_position (snapshot per volley)
		HOMING_SOURCE_NODE_PATH, // chase the Node2D at homing_target_path
		HOMING_SOURCE_NODE_NAME, // chase Node2Ds whose name matches homing_node_name
		HOMING_SOURCE_NODE_CHILDREN // chase Node2D children of homing_children_parent_path
	};

	// How homing_node_name is compared against node names (node-name source).
	enum HomingNodeNameMatch {
		HOMING_NAME_MATCH_EXACT = 0, // "Player" matches only "Player"
		HOMING_NAME_MATCH_CONTAINS, // "Player" matches "Player2", "EnemyPlayer", ...
		HOMING_NAME_MATCH_STARTS_WITH, // "Player" matches "Player2" but not "EnemyPlayer"
		HOMING_NAME_MATCH_ENDS_WITH // "Player" matches "EnemyPlayer" but not "Player2"
	};

	// Which of the detected nodes become targets (node-group source).
	enum HomingTargetSelection {
		HOMING_SELECT_NEAREST = 0, // closest to the spawner, up to homing_max_targets
		HOMING_SELECT_RANDOM, // random pick, up to homing_max_targets
		HOMING_SELECT_FIRST, // tree order, up to homing_max_targets
		HOMING_SELECT_ROUND_ROBIN, // cycle through the group across volleys
		HOMING_SELECT_DISTRIBUTE // deal targets across bullets: bullet i chases pool[i % pool]
	};

	// Whether already-flying volleys keep chasing fresh targets.
	enum HomingRetargetMode {
		HOMING_RETARGET_OFF = 0,
		HOMING_RETARGET_ON_INTERVAL // re-resolve + replace every homing_retarget_interval_sec
	};

	// Master switch. False = volleys fly exactly as spawn_data says.
	bool homing_enabled = false;
	HomingMode homing_mode = HOMING_SHARED;
	HomingTargetSource homing_target_source = HOMING_SOURCE_NODE_GROUP;
	// Polled group for HOMING_SOURCE_NODE_GROUP. Only Node2D members are
	// usable; the rest are skipped quietly.
	StringName homing_node_group = "enemies";
	// Extra allow-list applied on top of any source: when non-empty, only
	// targets that are ALSO in this group are kept. Empty = no filtering.
	StringName homing_filter_group;
	HomingTargetSelection homing_target_selection = HOMING_SELECT_NEAREST;
	// Seed for HOMING_SELECT_RANDOM picks. 0 = non-deterministic, >0 = reproducible.
	int homing_random_seed = 0;
	// Seconds of straight flight before volley steering starts (0 = steer
	// immediately). Must stay finite and >= 0.
	double homing_delay_sec = 0.0;
	// Seconds of steering before volleys fly straight (0 = infinite).
	// Must stay finite and >= 0.
	double homing_duration_sec = 0.0;
	// Steering pauses beyond this distance from the target (0 = unlimited).
	// Must stay finite and >= 0.
	double homing_lose_range_px = 0.0;
	// Fire cone: volleys whose aim deviates more than half this from the
	// target bearing are skipped (0 = omnidirectional). Must stay finite and >= 0.
	double homing_fire_arc_deg = 0.0;
	// Reload jitter: each auto volley waits shoot_interval_sec +/-
	// random * reload_jitter_sec (seeded by reload_jitter_seed). 0 = exact.
	// Must stay finite and >= 0.
	double reload_jitter_sec = 0.0;
	// Seed for reload jitter. 0 = non-deterministic, >0 = reproducible
	// (seed + volleys_fired, so volleys still vary per shot).
	int reload_jitter_seed = 0;
	// How many targets enter the queue (1 = classic single-target homing).
	// Only the multi-target sources (node group, node name) use it.
	// Must stay >= 1 (setter rejects the rest).
	// NOTE: DISTRIBUTE needs 2+ to actually spread targets across the
	// volley. At 1 (this default) the deal is `pool[i % 1] == pool[0]` for
	// every bullet, so DISTRIBUTE behaves exactly like SHARED and warns.
	int homing_max_targets = 1;
	// One-shot latch for that DISTRIBUTE-degenerates warning; re-armed
	// when the selection leaves DISTRIBUTE (see resolve_homing_targets).
	mutable bool homing_distribute_degenerate_warned = false;
	// Detection radius around the spawner for the multi-target sources
	// (node group, node name). 0 = unlimited. Must stay finite and >= 0.
	double homing_max_detection_range = 0.0;
	// Snapshot chased per volley for HOMING_SOURCE_GLOBAL_POSITION.
	Vector2 homing_global_position = Vector2(0, 0);
	// Node chased per volley for HOMING_SOURCE_NODE_PATH. Must point at a
	// Node2D; a missing/freed/wrong-type target skips homing for that
	// volley with a warning, never aborts it.
	NodePath homing_target_path;
	// NODE-NAME SOURCE (HOMING_SOURCE_NODE_NAME)
	//
	// Finds targets by node name instead of group membership: the whole
	// scene is scanned for Node2Ds whose name matches homing_node_name
	// per homing_node_name_match_mode, then the shared selection
	// (nearest/random/first/round robin), homing_max_targets, range, and
	// filter-group rules pick the queue. Handy when enemies are spawned
	// dynamically and never added to a group.
	String homing_node_name = "Player";
	HomingNodeNameMatch homing_node_name_match_mode = HOMING_NAME_MATCH_CONTAINS;
	// Case-insensitive by default: "Player" matches "PlAyEr". Turn on for
	// strict comparison.
	bool homing_node_name_case_sensitive = false;
	// NODE-CHILDREN SOURCE (HOMING_SOURCE_NODE_CHILDREN)
	//
	// Chases the Node2D children of one parent node instead of a group or
	// a name scan: point it at an enemy container, a carrier, or anything
	// whose children should be hunted. The path is required (empty
	// resolves nothing with a warning); a missing/freed/non-Node parent
	// skips homing for that volley, never aborts it. The shared selection
	// (nearest/random/first/round robin), homing_max_targets, range, and
	// filter-group rules pick the queue, exactly like the group source.
	NodePath homing_children_parent_path;
	// When true, grandchildren and deeper descendants are included too.
	bool homing_children_recursive = false;
	// Max turn rate in radians/sec. 0 = snap instantly. Must stay finite
	// and >= 0 (setter rejects the rest).
	double homing_smoothing = 5.0;
	// Seconds between target position refreshes. 0 = every tick.
	double homing_update_interval = 0.0;
	// A target counts as reached inside this distance (predictive, so
	// fast bullets still trigger). Must stay finite and >= 0.
	double homing_distance_before_reached = 5.0;
	bool homing_take_control_of_texture_rotation = true;
	// Pop the reached target so the queue advances: per bullet in
	// per-bullet mode, once per tick for the shared queue in shared mode.
	// (Bullet directions following rotation is the spawn data's own
	// adjust_direction_based_on_rotation; the spawner no longer
	// overrides it.)
	bool homing_auto_pop_after_target_reached = false;
	// When true, per-bullet smoothing fans out linearly: bullet i steers
	// with homing_smoothing_start + homing_smoothing_step * i.
	bool homing_per_bullet_smoothing_enabled = false;
	double homing_smoothing_start = 5.0;
	double homing_smoothing_step = 0.0;
	HomingRetargetMode homing_retarget_mode = HOMING_RETARGET_ON_INTERVAL;
	// Seconds between retarget passes. Must stay > 0. Short by default so
	// group / name / children membership (spawns, deaths) is picked up
	// live; each pass is cheap (id validation + one re-resolve, skipped
	// volleys untouched).
	double homing_retarget_interval_sec = 0.5;
	// When true (default), interval retargeting re-aims every tracked
	// live volley. When false, only the newest tracked volley is
	// re-aimed: older volleys keep flying at whatever they chased last
	// (or nothing). Useful for "latest shot follows the player, old
	// shots go dumb" patterns.
	bool homing_retarget_previous_volleys = true;

	// ORBITING (EASY API)
	//
	// Applied after the homing targets of the same volley, so freshly
	// spawned bullets can lock onto their ring immediately. Requires a
	// homing target: orbiting_enabled without homing_enabled warns and
	// does nothing (engine rule, see above).
	bool orbiting_enabled = false;
	// Ring radius in pixels. Must stay >= 0.01 (setter rejects the rest).
	double orbiting_radius = 64.0;
	BulletVolley2D::OrbitingDirection orbiting_direction = BulletVolley2D::OrbitRight;
	BulletVolley2D::OrbitingTextureRotation orbiting_texture_rotation = BulletVolley2D::FaceTarget;
	// When true, bullet i orbits with orbiting_radius_start +
	// orbiting_radius_step * i (concentric shells) instead of the flat
	// orbiting_radius.
	bool orbiting_radius_linear_enabled = false;
	double orbiting_radius_start = 64.0;
	double orbiting_radius_step = 0.0;
	// How the locked ring follows a moving target: FollowTarget tracks it
	// 1:1, FollowDeadzone pins the center until the target walks
	// farther than orbiting_follow_deadzone from it (jitter zone), Anchored
	// freezes the ring where it locked and ignores target motion.
	BulletVolley2D::OrbitingFollowMode orbiting_follow_mode = BulletVolley2D::FollowTarget;
	// Deadzone radius in pixels, used only by FollowDeadzone.
	double orbiting_follow_deadzone = 8.0;
	// What drops the lock: RelockAlways re-acquires every retarget,
	// StayLocked never unlocks on retarget/empty (only explicit
	// disable, clear, or a freed target), RelockOnTargetChange unlocks
	// only when the front target is a different target.
	BulletVolley2D::OrbitingLockPolicy orbiting_lock_policy = BulletVolley2D::RelockAlways;
	// When on, locked OrbitLeft/OrbitRight bullets translate 1:1 with the
	// target and keep circling (rings never lag or stretch when the
	// target moves). When off, locked bullets chase the ring center
	// clamped to speed * delta, so slow bullets trail fast targets.
	bool orbiting_rigid_follow = true;

	bool get_homing_enabled() const;
	void set_homing_enabled(bool value);
	HomingMode get_homing_mode() const;
	void set_homing_mode(HomingMode value);
	HomingTargetSource get_homing_target_source() const;
	void set_homing_target_source(HomingTargetSource value);
	StringName get_homing_node_group() const;
	void set_homing_node_group(const StringName &value);
	StringName get_homing_filter_group() const;
	void set_homing_filter_group(const StringName &value);
	HomingTargetSelection get_homing_target_selection() const;
	void set_homing_target_selection(HomingTargetSelection value);
	int get_homing_max_targets() const;
	void set_homing_max_targets(int value);
	double get_homing_max_detection_range() const;
	void set_homing_max_detection_range(double value);
	Vector2 get_homing_global_position() const;
	void set_homing_global_position(const Vector2 &value);
	NodePath get_homing_target_path() const;
	void set_homing_target_path(const NodePath &p_path);
	String get_homing_node_name() const;
	void set_homing_node_name(const String &value);
	HomingNodeNameMatch get_homing_node_name_match_mode() const;
	void set_homing_node_name_match_mode(HomingNodeNameMatch value);
	bool get_homing_node_name_case_sensitive() const;
	void set_homing_node_name_case_sensitive(bool value);
	NodePath get_homing_children_parent_path() const;
	void set_homing_children_parent_path(const NodePath &p_path);
	bool get_homing_children_recursive() const;
	void set_homing_children_recursive(bool value);
	double get_homing_smoothing() const;
	void set_homing_smoothing(double value);
	double get_homing_update_interval() const;
	void set_homing_update_interval(double value);
	double get_homing_distance_before_reached() const;
	void set_homing_distance_before_reached(double value);
	bool get_homing_take_control_of_texture_rotation() const;
	void set_homing_take_control_of_texture_rotation(bool value);
	bool get_homing_auto_pop_after_target_reached() const;
	void set_homing_auto_pop_after_target_reached(bool value);
	bool get_homing_per_bullet_smoothing_enabled() const;
	void set_homing_per_bullet_smoothing_enabled(bool value);
	double get_homing_smoothing_start() const;
	void set_homing_smoothing_start(double value);
	double get_homing_smoothing_step() const;
	void set_homing_smoothing_step(double value);
	HomingRetargetMode get_homing_retarget_mode() const;
	void set_homing_retarget_mode(HomingRetargetMode value);
	double get_homing_retarget_interval_sec() const;
	void set_homing_retarget_interval_sec(double value);
	bool get_homing_retarget_previous_volleys() const;
	void set_homing_retarget_previous_volleys(bool value);

	bool get_orbiting_enabled() const;
	void set_orbiting_enabled(bool value);
	double get_orbiting_radius() const;
	void set_orbiting_radius(double value);
	BulletVolley2D::OrbitingDirection get_orbiting_direction() const;
	void set_orbiting_direction(BulletVolley2D::OrbitingDirection value);
	BulletVolley2D::OrbitingTextureRotation get_orbiting_texture_rotation() const;
	void set_orbiting_texture_rotation(BulletVolley2D::OrbitingTextureRotation value);
	bool get_orbiting_radius_linear_enabled() const;
	void set_orbiting_radius_linear_enabled(bool value);
	double get_orbiting_radius_start() const;
	void set_orbiting_radius_start(double value);
	double get_orbiting_radius_step() const;
	void set_orbiting_radius_step(double value);
	BulletVolley2D::OrbitingFollowMode get_orbiting_follow_mode() const;
	void set_orbiting_follow_mode(BulletVolley2D::OrbitingFollowMode value);
	double get_orbiting_follow_deadzone() const;
	void set_orbiting_follow_deadzone(double value);
	BulletVolley2D::OrbitingLockPolicy get_orbiting_lock_policy() const;
	void set_orbiting_lock_policy(BulletVolley2D::OrbitingLockPolicy value);
	bool get_orbiting_rigid_follow() const;
	void set_orbiting_rigid_follow(bool value);

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
	// Who the targets are: graze_target_source and the settings below
	// (the same choices as homing's). They are shared with every volley
	// this spawner fired, so edits and refresh_graze_targets() reach
	// bullets in flight; orphaned volleys keep the settings and the nodes
	// the paths last pointed to.
	bool graze_enabled = false;
	// Up to BulletVolley2D::MAX_GRAZE_ZONES zones; null entries are kept
	// (the inspector adds them) and skipped.
	TypedArray<BulletGrazeZone2D> graze_zones;
	// SERIALIZED ids (GrazeDetector2D::Source): never renumber.
	enum GrazeTargetSource {
		GRAZE_SOURCE_ZONE_GROUPS = 0, // each zone's target_group (default)
		GRAZE_SOURCE_NODE_PATH = 1, // the Node2D at graze_target_path
		GRAZE_SOURCE_NODE_NAME = 2, // Node2Ds named like graze_node_name (scene scan)
		GRAZE_SOURCE_NODE_CHILDREN = 3 // Node2D children of graze_children_parent_path
	};
	GrazeTargetSource graze_target_source = GRAZE_SOURCE_ZONE_GROUPS;
	// Allow-list for every source: only nodes in this group count.
	StringName graze_filter_group;
	NodePath graze_target_path;
	// Scanned from the current scene (never inside a bullet factory or this
	// spawner), compared per graze_node_name_match_mode (homing's values).
	String graze_node_name = "Player";
	HomingNodeNameMatch graze_node_name_match_mode = HOMING_NAME_MATCH_CONTAINS;
	bool graze_node_name_case_sensitive = false;
	NodePath graze_children_parent_path;
	bool graze_children_recursive = false;
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
	double get_graze_update_interval() const;
	void set_graze_update_interval(double value);
	// Scans for graze targets now (whatever graze_update_interval says):
	// the bullets this spawner fired test the nodes found from their next
	// tick on. Returns how many distinct targets its zones have now.
	int refresh_graze_targets();
	// The graze target finder this spawner shares with every volley it
	// armed (created on first use). C++ only: the factory's runtime ring
	// preview draws around its lists.
	GrazeDetector2D &graze_detector_ref() const;
	// The targets the runtime would test for zone `zone_index` right now
	// (graze_target_source, the first BulletGrazeZone2D::MAX_TARGETS in
	// tree order; this spawner itself never counts).
	Array resolve_graze_targets(int zone_index) const;
	// The rings exactly as the preview draws them:
	// [{center, radius, color, zone_index, ring_index, target_id}].
	Array debug_get_graze_preview_circles() const;
	// {active, visible, circles, draws}: ring preview instrumentation.
	Dictionary debug_get_graze_preview_stats() const;
	// {scans, lists, shares_factory_lists}: this spawner's graze detector
	// (scans run by its own lists; shared lists count on the factory).
	Dictionary debug_get_graze_detector_stats() const;

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
	int get_homing_random_seed() const;
	void set_homing_random_seed(int value);
	int get_max_live_bullets() const;
	void set_max_live_bullets(int value);
	double get_homing_retarget_phase() const;
	void set_homing_retarget_phase(double value);
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
	double get_homing_delay_sec() const;
	void set_homing_delay_sec(double value);
	double get_homing_duration_sec() const;
	void set_homing_duration_sec(double value);
	double get_homing_lose_range_px() const;
	void set_homing_lose_range_px(double value);
	double get_homing_fire_arc_deg() const;
	void set_homing_fire_arc_deg(double value);
	double get_reload_jitter_sec() const;
	void set_reload_jitter_sec(double value);
	// One-call preset fill (see BulletPatterns2D::PatternPreset).
	void apply_pattern_preset(int preset);
	// Sequencer: queue pattern entries, then fire them in order
	// (interval apart) or all at once. Returns entries queued.
	int spawn_pattern_list(const Array &entries, bool simultaneous = false, double interval_sec = 0.25);
	void stop_pattern_list();
	bool is_pattern_list_active() const;
	// Next auto-shot interval with reload jitter applied.
	double next_shoot_interval_sec() const;
	// Live introspection for waves, budgets and debug.
	int get_burst_shots_left() const;
	int get_active_live_bullet_count() const;

	// Resolves the current homing targets without touching any volley:
	// node-group members (filtered + selected), the node at
	// homing_target_path, or the homing_global_position snapshot.
	// MOUSE source returns an empty array (the cursor is pushed live, not
	// resolved). Entries are Node2D* Variants or Vector2. With quiet =
	// false (default) an empty result warns once until a resolution
	// succeeds again; retarget passes use quiet = true and silently keep
	// the old queues instead. advance_round_robin = false resolves the
	// round-robin pick without consuming the cursor (used by retarget
	// passes so flying volleys keep stable targets).
	Array resolve_homing_targets(bool quiet = false, bool advance_round_robin = true) const;
	// Re-resolves targets and replaces the queues of every still-alive
	// volley owned by this spawner. Volleys with no resolvable targets
	// are skipped quietly. Returns how many volleys were retargeted.
	int retarget_live_volleys();
	// How many ACTIVE volleys this spawner owns (factory census: every
	// shot, homing or not, plus adopted volleys).
	int get_live_volley_count() const;
	// Every ACTIVE volley this spawner owns, in factory order. Lets
	// GDScript call the full BulletVolley2D API on each volley directly.
	Array get_live_volleys() const;
	// The retarget list: homing shots + adopted volleys (capped at 256,
	// pruned first). retarget_live_volleys() walks this list.
	int get_tracked_volley_count() const;
	// Forgets the retarget list (the volleys keep flying untouched).
	void forget_tracked_volleys();
	// Clears every live bullet this spawner owns (fire_clear_effects:
	// On Clear at each pose); parked volleys go back to the pool. Safe
	// from handlers. Returns how many bullets were cleared.
	int clear_active_bullets(bool fire_clear_effects = true);
	int orphaned_volleys = ORPHANED_VOLLEYS_KEEP_FLYING;
	int get_orphaned_volleys() const { return orphaned_volleys; }
	void set_orphaned_volleys(int value);
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
	// Current retarget countdown in seconds (time until the next interval
	// pass). For tests asserting deterministic stagger across spawners.
	double debug_get_retarget_countdown() const { return homing_retarget_time_left; }
	// Starts a burst chain by hand (waves, boss phases): burst_count shots,
	// burst_interval_sec apart, telegraphed first when enabled. Manual
	// chains run whether auto-fire is on or not.
	void begin_burst();
	// Internal chain machinery (the auto loop drives these).
	void begin_burst_chain(bool auto_started);
	// One pattern-list entry: apply, fire, restore. True when it fired.
	bool fire_pattern_list_override(const Variant &entry);
	void begin_telegraph(bool auto_started);
	void fire_burst_volley();
	// Ends a running chain early (auto-fire off, burst mode off, count
	// shrunk) and reports burst_finished once.
	void cancel_burst_chain();
	// Takes ownership of a manually-woken volley (see enable_bullet,
	// which detaches spawner ownership): stamps, hooks the reached
	// forwarder, and tracks it for retargeting. Queues are left alone.
	// Returns false for null or non-live (pooled/outside-tree) instances.
	bool adopt_live_volley(BulletVolley2D *bullets);
	// Kill-switch: clears homing queues and disables orbiting on every
	// tracked live volley (engine clears, counters stay exact). Returns
	// how many volleys were touched. Retargeting will re-arm them while
	// homing stays on; turn homing off (or clear tracking) to keep them dumb.
	int clear_live_volleys_homing();
	// Overwrites velocity on every tracked live volley through the engine
	// setter (curves override direct velocity: the engine warns and
	// no-ops per bullet when a speed curve is assigned). Returns how many
	// volleys were touched. Must be finite.
	int override_live_volleys_velocity(const Vector2 &new_velocity);

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
	enum SpawnOffsetSpace {
		SPAWN_OFFSET_GLOBAL = 0,
		SPAWN_OFFSET_LOCAL = 1,
	};
	int get_spawn_position_offset_space() const;
	void set_spawn_position_offset_space(int value);
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
	enum MovementSpace {
		MOVEMENT_SPACE_ATTACH = 0, // spawner sits ON the path (PathFollow2D-like)
		MOVEMENT_SPACE_RELATIVE_TO_START = 1, // path shape replayed from where the spawner started
	};
	enum MovementLoopMode {
		MOVEMENT_LOOP_ONCE = 0,
		MOVEMENT_LOOP_LOOP = 1, // restart from the beginning every leg
		MOVEMENT_LOOP_PING_PONG = 2, // back and forth
	};
	enum MovementDirection {
		MOVEMENT_DIRECTION_FORWARD = 0,
		MOVEMENT_DIRECTION_REVERSE = 1,
	};
	enum MovementTiming {
		MOVEMENT_TIMING_DURATION = 0, // each leg takes movement_duration_sec
		MOVEMENT_TIMING_SPEED = 1, // each leg takes length / movement_speed
	};

	bool get_movement_enabled() const;
	void set_movement_enabled(bool value);
	NodePath get_movement_path() const;
	void set_movement_path(const NodePath &p_path);
	Path2D *get_movement_path_node() const;
	void set_movement_path_node(Path2D *node);
	int get_movement_space() const;
	void set_movement_space(int value);
	int get_movement_loop_mode() const;
	void set_movement_loop_mode(int value);
	int get_movement_direction() const;
	void set_movement_direction(int value);
	int get_movement_loops() const;
	void set_movement_loops(int value);
	int get_movement_timing() const;
	void set_movement_timing(int value);
	double get_movement_duration_sec() const;
	void set_movement_duration_sec(double value);
	double get_movement_speed() const;
	void set_movement_speed(double value);
	int get_movement_transition() const;
	void set_movement_transition(int value);
	int get_movement_ease() const;
	void set_movement_ease(int value);
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
	enum PatternCacheMode {
		PATTERN_CACHE_AUTO = 0,
		PATTERN_CACHE_OFF = 1,
	};
	int get_pattern_cache_mode() const;
	void set_pattern_cache_mode(int value);
	// {hits, misses, bakes, version, shot_class, preview_class}
	// (class: 0 none, 1 translation, 2 rigid, -1 not baked yet).
	Dictionary debug_get_pattern_cache_info() const;
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

	virtual void _ready() override;
	virtual void _process(double delta) override;
	// Plain name (no override): godot-cpp routes notifications to this
	// method through the binding machinery, same as BulletFactory2D.
	void _notification(int p_what);

	// Hides the helper_* property groups that don't belong to the active
	// pattern_source, so the inspector only shows relevant options.
	// (Name hiding, picked up by the binding machinery - not an override.)
	void _validate_property(PropertyInfo &p_property) const;

protected:
	static void _bind_methods();

private:
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
	// Single clear-point for the latch + global depth above.
	void clear_shoot_once_latch();
	// Homing/orbiting runtime state (never stored).
	// Tracked-volley registry for interval retargeting. VolleyTracker2D
	// owns the ids plus the prune-before-touch invariant (every reader
	// prunes freed/re-homed/inactive volleys first, and every resolved
	// volley is re-validated), so pooled or adopted volleys can never be
	// touched by mistake. Mutable: const readers prune dead entries.
	mutable VolleyTracker2D volley_tracker;
	// Countdown to the next retarget pass. 0.0 means "due on the next
	// tick": arming (or reset_shooting) always re-arms to due-now so the
	// first pass never waits a full interval.
	double homing_retarget_time_left = 0.0;
	// Warn-once latch for empty target resolution (mutable: used by the
	// const resolve_homing_targets). One warning per homing target
	// configuration: shots resolve non-quietly, so a typo'd group or a
	// dead target path is reported, but never once per volley.
	mutable bool homing_empty_targets_warned = false;
	// Cursor for HOMING_SELECT_ROUND_ROBIN across volleys (mutable: used
	// by the const resolve_homing_targets).
	mutable int homing_round_robin_cursor = 0;
	// Lazily created RNG for HOMING_SELECT_RANDOM (mutable: used by the
	// const resolve_homing_targets).
	mutable Ref<RandomNumberGenerator> homing_rng;
	// Reusable per-resolve scratch (mutable: used by the const
	// resolve_homing_targets + collectors). Cleared at each entry; never
	// nested in practice (collectors run once per resolve and neither
	// they nor the selection branch emit or call anything user-
	// overridable, and every bound entry runs sequentially on the main
	// thread), so sharing is safe. Saves 2-4 Array allocations every
	// volley and every retarget pass. If a future change lets user code
	// run mid-resolve, add a reentrancy latch here instead of silently
	// corrupting the outer pass.
	mutable Array homing_candidates_scratch;
	// Candidate-pool reuse across the resolves of ONE shot or ONE
	// retarget pass (see resolve_homing_targets). Scoped by
	// HomingCandidatePass in the .cpp; never left active.
	mutable bool homing_candidate_pass_active = false;
	mutable bool homing_candidate_pass_filled = false;
	mutable Array homing_pool_scratch;
	mutable Array homing_scan_stack;
	// Dedicated RNG for reload jitter (mutable: used by the const
	// next_shoot_interval_sec). Separate from homing_rng: jitter reseeds
	// would otherwise corrupt the homing target sequence.
	mutable Ref<RandomNumberGenerator> jitter_rng;
	// Seed 0 (non-deterministic jitter) randomizes the generator once,
	// not on every interval.
	mutable bool jitter_rng_randomized = false;
	// Duplicate cache: shoot_once() must never mutate the user's
	// spawn_data (transforms are overwritten per volley), so the first
	// shot duplicates it and later shots with the same resource reuse the
	// spawner-owned template (transforms overwritten each shot). Cleared
	// on set_spawn_data() and on the resource's changed signal, so
	// in-place inspector edits re-duplicate instead of driving stale data.
	// Mutable: shoot_once() is non-const but resolve paths also touch it.
	mutable Ref<BulletVolleyData2D> cached_volley_template;
	mutable uint64_t cached_spawn_data_id = 0;
	void _on_spawn_data_changed();
	// Spin runtime state (never stored, advances in _process only).
	double spin_angle_deg = 0.0;
	double spin_time_sec = 0.0;
	// Burst runtime state (never stored): shots left in the current burst,
	// countdown to the next burst shot, telegraph countdown, and the
	// mirror flag for the next burst volley.
	int burst_shots_left = 0;
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
	Node2D *preview_holder = nullptr;
	PatternPreviewLayer2D *preview_dots_layer = nullptr;
	PatternPreviewLayer2D *preview_arrows_layer = nullptr;
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
	// Retarget clock: an explicit phase verbatim, else a per-instance
	// stagger so spawners enabled together never scan on the same tick.
	void arm_retarget_countdown();

	// True while interval retargeting must keep _process alive: homing on
	// + retarget armed + inside the tree at runtime.
	bool homing_retarget_active() const;
	// Wakes _process when retargeting becomes active (runtime only).
	void update_homing_process_state(bool reset_countdown);
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
	// Remembers a fresh volley for retargeting (deduped: pooled instances
	// reuse ids) and prunes dead/foreign entries via the tracker.
	void track_live_volley(BulletVolley2D *bullets);
	// Recursive scene scan for the node-name source: collects live
	// Node2Ds under p_node whose name matches homing_node_name per
	// homing_node_name_match_mode and homing_node_name_case_sensitive,
	// honoring homing_filter_group.
	void collect_homing_candidates_by_name(Node *p_node, Array &r_candidates) const;
	// Children scan for the node-children source: collects Node2D
	// children of p_parent (whole subtree when recursive), honoring
	// homing_filter_group. Never collects this spawner itself.
	void collect_homing_candidates_from_children(Node *p_parent, bool recursive, Array &r_candidates) const;
	// Warn-once latch helper for empty resolutions (const: flips the
	// mutable latch). Quiet passes never warn. The latch re-arms only
	// when the homing target configuration changes
	// (clear_empty_homing_targets_warning() from those setters), so a
	// group that empties during play warns at most once.
	void warn_empty_homing_targets_once(const String &message, bool quiet) const;
	void clear_empty_homing_targets_warning() const;
	// Pushes the steering block (smoothing, update interval, reached
	// distance, texture control, auto-pop flags, per-bullet smoothing
	// fan) onto a volley. Shared by volley setup and retarget passes so
	// runtime tuning reaches flying volleys instead of only new ones.
	void apply_steering_to_volley(BulletVolley2D *volley) const;
	// Predictive lead for the aimed target, shared by the aimed volley
	// and its preview cone so both agree on where the target will be.
	Vector2 predict_target_pos(Node2D *target) const;
	// Corridor aim resolution, shared by the corridor volley and its
	// preview wall: live aimed target when usable, else the fallback.
	Vector2 resolve_corridor_aim(const Vector2 &fallback, const Vector2 &origin) const;
	// Enables or refreshes orbiting on a volley: bullets whose orbit is
	// not yet enabled get enabled, the rest get radius/direction/texture
	// updated in place (re-enabling would warn and keep stale values).
	// Skipped entirely for DontMove.
	void apply_orbiting_to_volley(BulletVolley2D *volley) const;
	// Applies the homing + orbiting configuration to a freshly spawned
	// (or pool-reused) volley: steering props, resolved targets (pushed
	// before orbiting so rings can lock immediately), orbiting, signal
	// hookup, and live-volley tracking. Called from shoot_once() before
	// volley_fired so handlers observe fully configured bullets.
	// `pre_resolved`: targets the fire-arc gate already approved (one
	// resolution per shot: the volley chases exactly what was approved).
	void apply_volley_homing_and_orbiting(BulletVolley2D *bullets, const Array *pre_resolved = nullptr);
	// Orbiting without homing warns once per configuration (re-armed by
	// on_config_changed()).
	bool orbit_without_homing_warned = false;
	// The graze target finder shared with every volley this spawner armed
	// (created on first use; the factory's runtime preview reads it).
	mutable std::shared_ptr<GrazeDetector2D> graze_detector;
	// The graze_* target settings as a detector configuration.
	GrazeDetector2D::Config graze_detector_config() const;
	// Pushes them into the detector (rescans due), then the config
	// warnings and the ring preview follow.
	void apply_graze_detector_config();
	// The targets of one zone as the runtime tests them (the factory's
	// lists while it runs, a fresh scan otherwise), this spawner excluded.
	void collect_graze_zone_targets(const BulletGrazeZone2D &zone, GrazeTarget2D *r_targets, int &r_count) const;
	// Arms a freshly fired volley with graze_zones (shoot_once, before the
	// homing signals). Runs no user code.
	void apply_volley_graze(BulletVolley2D *volley);
	// graze_zones mutated in place (append past 4 zones, non-zone entries):
	// warned once per assignment, the usable entries still arm.
	bool graze_zones_misuse_warned = false;
	// Zone resources' `changed` -> ring preview refresh.
	void connect_graze_zones(bool connect);
	void _on_graze_zone_changed();
	// Runtime only: graze spawners join GRAZE_SPAWNER_GROUP (the factory's
	// runtime preview scans it) and wake their factory's runtime preview
	// whenever a held zone is flagged preview_during_runtime.
	void update_graze_runtime_preview_hookup();
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
	// Candidate count of the last round-robin resolution (to advance the
	// cursor by what a reused resolution actually took).
	mutable int homing_round_robin_last_count = 0;
	// Sequencer internals: validates one Dictionary entry (preset /
	// source / amount / spawn_data overrides) and fires the next queued
	// entry for the _process driver.
	bool apply_pattern_list_entry(const Variant &entry);
	void fire_pattern_list_entry();
	// Fire cone check: true when any resolved target sits within half
	// the fire arc of the spawner's facing. 0 arc = omnidirectional.
	bool fire_arc_covers_targets(const Array &targets) const;
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
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::PatternSource);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::Path2DDistribution);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::Path2DOverflow);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::Path2DAnchor);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::Path2DFacing);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::Path2DSpace);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::SpinMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::OrphanedVolleys);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingTargetSource);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingNodeNameMatch);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingTargetSelection);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingRetargetMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::GrazeTargetSource);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::PatternCacheMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::MovementSpace);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::MovementLoopMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::MovementDirection);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::MovementTiming);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::SpawnOffsetSpace);
