#pragma once

#include "bullet_spawner/volley_tracker2d.hpp"
#include "bullets/directional_bullets2d.hpp"
#include "factory/bullet_factory2d.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/random_number_generator.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/core/property_info.hpp"
#include "godot_cpp/variant/node_path.hpp"
#include "godot_cpp/variant/packed_vector2_array.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "spawn-data/directional_bullets_data2d.hpp"

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
        PackedVector2Array arrow_tails;
        PackedVector2Array arrow_dirs;
        Color arrow_color = Color(1.0, 0.05, 0.05);
        float arrow_length = 16.0f;
        float arrow_width = 2.0f;
        float arrow_head_length = 8.0f;
        float arrow_head_width = 10.0f;

        void set_dots_data(const PackedVector2Array &p_dots, const Color &p_color, float p_radius);
        void set_arrows_data(const PackedVector2Array &p_tails, const PackedVector2Array &p_dirs, const Color &p_color, float p_length, float p_width, float p_head_length, float p_head_width);

        void _draw() override;

    protected:
        static void _bind_methods();
};

class BulletSpawner2D : public Node2D{
    GDCLASS(BulletSpawner2D, Node2D)

    // Crash-safety for preview source tracking: a raw Node* cached across
    // frames may dangle after the node is freed (stale object id: the memory
    // can even be reused by an unrelated object). The ONLY safe pattern is:
    // store the pointer AND its instance id, then validate with
    // is_instance_id_valid() + id equality BEFORE touching the pointer.
    // A failed check means "gone": never dereference, just treat as dirty so
    // the next rebuild re-resolves from the tree. Same idea as
    // homing_target_deque's is_homing_target_valid().
    static bool is_tracked_node_alive(const Node *node, uint64_t cached_id) {
        if (node == nullptr || !godot::UtilityFunctions::is_instance_id_valid(cached_id)) {
            return false;
        }
        return node->get_instance_id() == cached_id;
    }

    public:
        // Where volley transforms come from. Children/Self read the scene
        // tree; the helper modes call the BulletFactory2D static generators
        // with the properties below, relative to the generator's transform.
        enum TransformsSource {
            TRANSFORMS_FROM_CHILDREN = 0,
            TRANSFORMS_FROM_SELF,
            TRANSFORMS_FROM_HELPER_GRID,
            TRANSFORMS_FROM_HELPER_RING,
            TRANSFORMS_FROM_HELPER_FAN,
            TRANSFORMS_FROM_HELPER_SPIRAL,
            TRANSFORMS_FROM_HELPER_LINE,
            TRANSFORMS_FROM_HELPER_AIMED,
            TRANSFORMS_FROM_HELPER_FLOWER,
            TRANSFORMS_FROM_HELPER_ELLIPSE,
            TRANSFORMS_FROM_HELPER_RAIN,
            TRANSFORMS_FROM_HELPER_SCATTER,
            TRANSFORMS_FROM_HELPER_POLYGON,
            TRANSFORMS_FROM_HELPER_MULTISPIRAL,
            TRANSFORMS_FROM_HELPER_CROSS,
            TRANSFORMS_FROM_HELPER_STAR,
            TRANSFORMS_FROM_HELPER_HEART,
            TRANSFORMS_FROM_HELPER_WAVE,
            TRANSFORMS_FROM_HELPER_WATERFALL,
            TRANSFORMS_FROM_HELPER_LATTICE,
            TRANSFORMS_FROM_HELPER_ROSE,
            TRANSFORMS_FROM_HELPER_COUNTER_SPIRAL,
            TRANSFORMS_FROM_HELPER_CORRIDOR,
            TRANSFORMS_FROM_HELPER_LISSAJOUS,
            TRANSFORMS_FROM_HELPER_CUSTOM,
            TRANSFORMS_FROM_HELPER_CIRCLE,
            TRANSFORMS_FROM_HELPER_RECTANGLE,
            TRANSFORMS_FROM_HELPER_SQUARE,
            TRANSFORMS_FROM_HELPER_REGULAR_POLYGON,
            TRANSFORMS_FROM_HELPER_PATH2D
        };

        // Custom facing: Normal faces along the edge normal (90 degrees),
        // Tangent faces along the edge direction, Custom faces a fixed
        // angle (helper_edge_custom_angle_deg, marker-relative).
        enum CustomFacing {
            CUSTOM_FACING_NORMAL = 0,
            CUSTOM_FACING_TANGENT,
            CUSTOM_FACING_CUSTOM
        };

        // How the spin angle evolves. CONTINUOUS rotates forever at
        // spin_speed_deg_per_sec (signed: positive = clockwise, per the
        // Godot 2D convention); OSCILLATE swings +-spin_amplitude_deg at
        // spin_frequency_hz instead, ignoring the speed.
        enum SpinMode {
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
        Ref<DirectionalBulletsData2D> spawn_data;

        NodePath get_bullet_factory_path() const;
        void set_bullet_factory_path(const NodePath &p_path);

        BulletFactory2D *get_bullet_factory() const;
        void set_bullet_factory(BulletFactory2D *factory);

        NodePath get_transforms_generator_path() const;
        void set_transforms_generator_path(const NodePath &p_path);

        Node2D *get_transforms_generator() const;
        void set_transforms_generator(Node2D *generator);
        // Anchor every transforms_source mode lives on: the assigned generator,
        // or this spawner when unset/unresolvable. Never null inside the tree.
        Node2D *get_effective_generator() const;

        Ref<DirectionalBulletsData2D> get_spawn_data() const;
        void set_spawn_data(const Ref<DirectionalBulletsData2D> &new_spawn_data);

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
        double transforms_scale = 1.0;
        // Flat global offset added to every bullet right after spawn (muzzle
        // offsets, spawn-then-nudge, whole-volley follows). (0, 0) disables
        // the pass. Must stay finite. Applied through the engine teleport
        // path, so shapes, attachments, and interpolation stay in sync.
        Vector2 spawn_position_offset = Vector2(0, 0);

        // SPIN (ROTATE MARKER)
        //
        // Virtual rotation applied to every volley, no matter which
        // transforms_source is picked: collect_spawn_transforms() rotates each
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
        bool is_spinning() const;
        void start_spinning();
        void stop_spinning();
        void reset_spin_angle();

        // TRANSFORMS SOURCE + HELPER GENERATORS
        //
        // Which behavior collect_spawn_transforms() uses. Children (default)
        // preserves the original behavior exactly; the helper modes call the
        // BulletFactory2D static generators relative to the generator's global
        // transform, using helper_bullets_amount bullets and the matching
        // helper_* properties below (only the active mode's group is shown in
        // the inspector - see _validate_property).
        TransformsSource transforms_source = TRANSFORMS_FROM_CHILDREN;
        // Bullet count for the helper modes (children/self modes derive the
        // count from the collected transforms instead).
        int helper_bullets_amount = 10;

        // GRID
        int helper_grid_rows_per_column = 10;
        int helper_grid_alignment = 3; // BulletFactory2D::Alignment, center-left
        double helper_grid_column_offset = 150.0;
        double helper_grid_row_offset = 150.0;
        bool helper_grid_rotate_with_marker = true;
        bool helper_grid_random_local_rotation = false;
        double helper_grid_jitter = 0.0;

        // RING
        double helper_ring_radius = 150.0;
        double helper_ring_start_angle = 0.0;
        double helper_ring_arc = 6.283185307179586; // Math::TAU
        bool helper_ring_rotate_with_marker = true;
        bool helper_ring_random_rotation = false;
        bool helper_ring_face_outward = true;
        double helper_ring_y_scale = 1.0;
        double helper_ring_facing_offset_deg = 0.0;

        // FAN
        double helper_fan_spread = 0.5;
        double helper_fan_direction_angle = 0.0;
        double helper_fan_step_offset = 0.0;
        bool helper_fan_centered = true;
        // Per-slot random angle variance in radians (shotgun spread, 0 = exact).
        double helper_fan_angle_jitter = 0.0;

        // SPIRAL
        double helper_spiral_start_radius = 50.0;
        double helper_spiral_radius_step = 15.0;
        double helper_spiral_angle_step = 0.6;
        bool helper_spiral_rotate_with_marker = true;
        int helper_spiral_facing = 0; // BulletFactory2D::SpiralFacingMode, tangent
        double helper_spiral_facing_offset_deg = 0.0;

        // LINE
        Vector2 helper_line_direction = Vector2(1, 0);
        double helper_line_spacing = 32.0;
        bool helper_line_face_direction = true;
        int helper_line_anchor = 1; // BulletFactory2D::LineAnchor, center
        bool helper_line_perpendicular = false;

        // AIMED
        // Scene-tree reference to the target node the aimed cone centers on.
        // Same NodePath pattern as the other scene references on this node.
        NodePath helper_aimed_target_path;
        // Runtime cache of the resolved target. Not a bound property.
        // helper_aimed_target_id pairs with it (same dangling guard).
        mutable Node2D *helper_aimed_target = nullptr;
        mutable uint64_t helper_aimed_target_id = 0;
        double helper_aimed_spread = 0.3;
        double helper_aimed_step_offset = 0.0;
        bool helper_aimed_centered = true;
        // Blend toward the predicted target position: 0 aims at where the
        // target is now, 1 aims where it will be after helper_aimed_prediction
        // seconds at its current velocity (needs a target that exposes
        // get_velocity(), e.g. CharacterBody2D; otherwise falls back to now).
        double helper_aimed_prediction = 0.0;
        double helper_aimed_prediction_time = 0.5;

        // FLOWER (spell-card blossoms: petals symmetric lobes).
        int helper_flower_petals = 6;
        int helper_flower_bullets_per_petal = 5;
        double helper_flower_radius = 150.0;
        double helper_flower_petal_spread = 0.5;
        double helper_flower_petal_sharpness = 1.0;
        double helper_flower_base_rotation = 0.0;
        bool helper_flower_face_outward = true;
        double helper_flower_facing_offset_deg = 0.0;

        // ELLIPSE (true ellipse ring / arc / wall-with-gaps).
        double helper_ellipse_radius_x = 150.0;
        double helper_ellipse_radius_y = 100.0;
        double helper_ellipse_rotation = 0.0;
        double helper_ellipse_start_angle = 0.0;
        double helper_ellipse_arc = 6.283185307179586;
        int helper_ellipse_mode = 0; // BulletFactory2D::EllipseMode, full
        int helper_ellipse_gap_count = 2;
        double helper_ellipse_gap_width = 0.3;
        bool helper_ellipse_face_outward = true;
        double helper_ellipse_facing_offset_deg = 0.0;

        // RAIN (curtain band of descending bullets).
        double helper_rain_band_width = 600.0;
        Vector2 helper_rain_direction = Vector2(0, 1);
        double helper_rain_drop_spacing = 48.0;
        double helper_rain_jitter = 12.0;

        // SCATTER (biased-random burst disc: explosions, deaths, pops).
        double helper_scatter_burst_radius = 120.0;
        double helper_scatter_facing_jitter = 0.4;
        // 0 = non-deterministic, otherwise reproducible (replays, bosses).
        int helper_scatter_seed = 0;

        // POLYGON (star emphasis: density pulled toward N vertices).
        int helper_polygon_vertices = 5;
        double helper_polygon_radius = 150.0;
        double helper_polygon_vertex_bias = 2.0;
        double helper_polygon_base_rotation = 0.0;
        bool helper_polygon_face_outward = true;
        double helper_polygon_facing_offset_deg = 0.0;

        // MULTISPIRAL (interleaved galaxy/windmill/rose arms).
        int helper_multispiral_arms = 3;
        double helper_multispiral_start_radius = 50.0;
        double helper_multispiral_radius_step = 15.0;
        double helper_multispiral_angle_step = 0.6;
        bool helper_multispiral_rotate_with_marker = true;
        int helper_multispiral_facing = 0; // BulletFactory2D::SpiralFacingMode, tangent
        double helper_multispiral_facing_offset_deg = 0.0;
        int helper_multispiral_arm_stride = 1;

        // CROSS (plus/X barrage: rays from the marker).
        int helper_cross_arm_count = 4;
        double helper_cross_arm_length = 150.0;
        double helper_cross_spacing = 32.0;
        double helper_cross_base_rotation = 0.0;
        bool helper_cross_face_outward = true;
        double helper_cross_facing_offset_deg = 0.0;

        // STAR (true star shell: alternating outer/inner vertices).
        int helper_star_points = 5;
        double helper_star_outer_radius = 150.0;
        double helper_star_inner_radius = 65.0;
        double helper_star_base_rotation = 0.0;
        bool helper_star_face_outward = true;
        double helper_star_facing_offset_deg = 0.0;

        // HEART (parametric heart bloom for boss love attacks).
        double helper_heart_size = 150.0;
        double helper_heart_base_rotation = 0.0;
        bool helper_heart_face_outward = true;
        double helper_heart_facing_offset_deg = 0.0;

        // WAVE (snake row along a sine wave).
        double helper_wave_width = 600.0;
        double helper_wave_amplitude = 48.0;
        double helper_wave_waves = 2.0;
        Vector2 helper_wave_direction = Vector2(1, 0);
        bool helper_wave_face_direction = true;
        double helper_wave_facing_offset_deg = 0.0;

        // WATERFALL (staggered curtain grid).
        int helper_waterfall_columns = 12;
        double helper_waterfall_column_spacing = 48.0;
        int helper_waterfall_rows = 3;
        double helper_waterfall_row_spacing = 64.0;
        double helper_waterfall_stagger = 0.5;
        Vector2 helper_waterfall_rain_direction = Vector2(0, 1);
        double helper_waterfall_jitter = 6.0;
        double helper_waterfall_facing_offset_deg = 0.0;

        // LATTICE (staggered honeycomb wall).
        int helper_lattice_columns = 8;
        int helper_lattice_rows = 5;
        double helper_lattice_spacing_x = 48.0;
        double helper_lattice_spacing_y = 42.0;
        bool helper_lattice_stagger_rows = true;
        bool helper_lattice_face_outward = true;
        double helper_lattice_facing_offset_deg = 0.0;

        // ROSE (exact rhodonea rose for petal-storm / blossom-finale blooms).
        int helper_rose_petals = 6;
        double helper_rose_radius = 150.0;
        double helper_rose_lobe_sharpness = 1.0;
        double helper_rose_base_rotation = 0.0;
        bool helper_rose_face_outward = true;
        double helper_rose_facing_offset_deg = 0.0;

        // COUNTER SPIRAL (twin counter-rotating galaxy / windmill arms).
        int helper_counter_spiral_arms = 2;
        double helper_counter_spiral_start_radius = 50.0;
        double helper_counter_spiral_radius_step = 15.0;
        double helper_counter_spiral_angle_step = 0.6;
        bool helper_counter_spiral_rotate_with_marker = true;
        int helper_counter_spiral_facing = 0; // BulletFactory2D::SpiralFacingMode, tangent
        double helper_counter_spiral_facing_offset_deg = 0.0;
        int helper_counter_spiral_arm_stride = 1;
        bool helper_counter_spiral_mirror_alternate_arms = true;

        // CORRIDOR (aimed trap: dense wall with a carved center dodge door).
        // Slots spread evenly across helper_corridor_width; helper_corridor
        // _spacing is reserved (unused) so the signature stays stable.
        Vector2 helper_corridor_aim_direction = Vector2(0, 1);
        double helper_corridor_width = 400.0;
        double helper_corridor_spacing = 32.0;
        double helper_corridor_gap_width = 96.0;
        bool helper_corridor_face_aim = true;
        double helper_corridor_facing_offset_deg = 0.0;

        // LISSAJOUS (figure-8 / weave openings for crossing curtains).
        double helper_lissajous_size_x = 200.0;
        double helper_lissajous_size_y = 120.0;
        double helper_lissajous_freq_x = 3.0;
        double helper_lissajous_freq_y = 2.0;
        double helper_lissajous_phase = 0.0;
        bool helper_lissajous_face_outward = true;
        double helper_lissajous_facing_offset_deg = 0.0;

        // CUSTOM (freeform outline — the reference spray). Pass a polygon or
        // polyline in helper_edge_points: each bullet spawns at a point along
        // it, facing per helper_edge_facing (normal = 90 degrees). The
        // helper_edge_side knob puts the spray outside, inside, or both.
        // Defaults ARE the reference look out of the box: a demo sine crest
        // with a one-sided falloff below it.
        PackedVector2Array helper_edge_points = make_default_edge_crest();
        CustomFacing helper_edge_facing = CUSTOM_FACING_NORMAL;
        double helper_edge_custom_angle_deg = 0.0;
        // Which side of the outline the spray lives on. Shared SideMode
        // values from the factory: 1 = outside, 2 = inside, 3 = both.
        // (0 = on-path is meaningless here; spread = 0 covers it.)
        int helper_edge_side = 2;
        bool helper_edge_closed = false;
        bool helper_edge_random_sample = true;
        double helper_edge_jitter = 3.0;
        double helper_edge_facing_offset_deg = 0.0;
        int helper_edge_seed = 0;
        double helper_edge_spread = 220.0;
        double helper_edge_spread_exponent = 2.2;
        double helper_edge_tangent_jitter = 2.0;
        // CIRCLE (exact loop outline).
        double helper_circle_radius = 150.0;
        bool helper_circle_face_outward = true;
        double helper_circle_facing_offset_deg = 0.0;
        // RECTANGLE (perimeter walk, centered on the generator).
        Vector2 helper_rectangle_size = Vector2(300, 200);
        bool helper_rectangle_face_outward = true;
        double helper_rectangle_facing_offset_deg = 0.0;
        // SQUARE (perimeter walk, centered; single side length).
        double helper_square_size = 300.0;
        bool helper_square_face_outward = true;
        double helper_square_facing_offset_deg = 0.0;
        // REGULAR POLYGON (true perimeter, not star-biased scatter).
        int helper_regular_polygon_vertices = 6;
        double helper_regular_polygon_radius = 150.0;
        double helper_regular_polygon_rotation = 0.0;
        bool helper_regular_polygon_face_outward = true;
        double helper_regular_polygon_facing_offset_deg = 0.0;
        // PATH2D (live curve outline; shares Custom's spray knobs below).
        NodePath helper_path2d_path;
        // Runtime cache of the resolved Path2D. Not a bound property. The id
        // pairs with it: validate BEFORE dereferencing (a raw pointer
        // outlives freed nodes — see crash history).
        mutable Node *helper_path2d_cache = nullptr;
        mutable uint64_t helper_path2d_id = 0;
        // Universal side pass for the closed-outline modes (Custom owns its
        // spray via helper_edge_side instead). Identity by default, so
        // existing scenes never change. Open modes (Children, Self, Grid,
        // Fan, Spiral, Line, Aimed, Rain, Scatter, MultiSpiral, Cross,
        // Heart, Wave, Waterfall, Lattice, Counter Spiral, Corridor) reject
        // it loudly: inside/outside is meaningless without a loop.
        int helper_side_mode = 0; // BulletFactory2D::SideMode
        double helper_side_spread = 0.0;
        double helper_side_spread_exponent = 2.0;
        int helper_side_seed = 0;

        // NEGATIVE SPACE (skip slots by index: dodge doors, bullet text).
        PackedInt32Array helper_skip_indices;

        // BURST (multi-volley danmaku phrasing: N shots per trigger).
        bool burst_enabled = false;
        // Shots per trigger. Must stay >= 1.
        int burst_count = 3;
        // Seconds between burst shots. Must stay > 0.
        double burst_interval_sec = 0.15;
        // Mirror every other burst volley (fan/spiral chirality flip): the
        // classic reverse-the-angle rhythm without scripting.
        bool burst_alternate_mirror = false;
        // Telegraph: warn before each burst volley fires.
        bool telegraph_enabled = false;
        // Seconds of warning before the volley. Must stay >= 0.
        double telegraph_sec = 0.5;

        // PERFORMANCE / REPRODUCIBILITY.
        // Deterministic helper randomness: 0 = non-deterministic, otherwise
        // seeds grid jitter, ring random rotation and scatter without an
        // explicit seed. Replays and boss patterns stay reproducible.
        int pattern_seed = 0;
        // Soft live-bullet fuse: 0 = unlimited, otherwise auto-shooting and
        // retargeting pause while active live bullets reach this count.
        int max_live_bullets = 0;
        // Stagger the retarget phase so N spawners don't scene-scan on the same
        // tick: actual period stays homing_retarget_interval_sec.
        double homing_retarget_phase = 0.0;

        // HOMING (EASY API)
        //
        // The spawner resolves homing targets at volley time and pushes them
        // onto the controllable DirectionalBullets2D instance returned by the
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
        // transforms_source): only the active source's options show in the
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
        // random * reload_jitter_sec (seeded by pattern_seed). 0 = exact.
        // Must stay finite and >= 0.
        double reload_jitter_sec = 0.0;
        // How many targets enter the queue (1 = classic single-target homing).
        // Only the multi-target sources (node group, node name) use it.
        // Must stay >= 1 (setter rejects the rest).
        int homing_max_targets = 1;
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
        // Mirrors DirectionalBulletsData2D.adjust_direction_based_on_rotation
        // per volley: bullet directions follow their rotation data. Applied
        // with the steering block; interacts with take-control above (see
        // the engine inert-warning path when both fight).
        bool adjust_direction_based_on_rotation = false;
        // Pop the reached target in per-bullet mode (queues advance).
        bool homing_auto_pop_after_target_reached = false;
        // Pop the reached target in shared mode (queue advances once per tick).
        bool shared_homing_auto_pop_after_target_reached = false;
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
        // When true, every homing volley prints how many targets it resolved
        // (and the first one's name/position). Cheap printf debugging for
        // "why do my bullets fly straight" moments. Off by default.
        bool homing_debug_log_volleys = false;

        // ORBITING (EASY API)
        //
        // Applied after the homing targets of the same volley, so freshly
        // spawned bullets can lock onto their ring immediately. Requires a
        // homing target: orbiting_enabled without homing_enabled warns and
        // does nothing (engine rule, see above).
        bool orbiting_enabled = false;
        // Ring radius in pixels. Must stay >= 0.01 (setter rejects the rest).
        double orbiting_radius = 64.0;
        DirectionalBullets2D::OrbitingDirection orbiting_direction = DirectionalBullets2D::OrbitRight;
        DirectionalBullets2D::OrbitingTextureRotation orbiting_texture_rotation = DirectionalBullets2D::FaceTarget;
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
        DirectionalBullets2D::OrbitingFollowMode orbiting_follow_mode = DirectionalBullets2D::FollowTarget;
        // Deadzone radius in pixels, used only by FollowDeadzone.
        double orbiting_follow_deadzone = 8.0;
        // What drops the lock: RelockAlways re-acquires every retarget,
        // StayLocked never unlocks on retarget/empty (only explicit
        // disable, clear, or a freed target), RelockOnTargetChange unlocks
        // only when the front target is a different target.
        DirectionalBullets2D::OrbitingLockPolicy orbiting_lock_policy = DirectionalBullets2D::RelockAlways;
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
        bool get_adjust_direction_based_on_rotation() const;
        void set_adjust_direction_based_on_rotation(bool value);
        bool get_homing_auto_pop_after_target_reached() const;
        void set_homing_auto_pop_after_target_reached(bool value);
        bool get_shared_homing_auto_pop_after_target_reached() const;
        void set_shared_homing_auto_pop_after_target_reached(bool value);
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
        bool get_homing_debug_log_volleys() const;
        void set_homing_debug_log_volleys(bool value);

        bool get_orbiting_enabled() const;
        void set_orbiting_enabled(bool value);
        double get_orbiting_radius() const;
        void set_orbiting_radius(double value);
        DirectionalBullets2D::OrbitingDirection get_orbiting_direction() const;
        void set_orbiting_direction(DirectionalBullets2D::OrbitingDirection value);
        DirectionalBullets2D::OrbitingTextureRotation get_orbiting_texture_rotation() const;
        void set_orbiting_texture_rotation(DirectionalBullets2D::OrbitingTextureRotation value);
        bool get_orbiting_radius_linear_enabled() const;
        void set_orbiting_radius_linear_enabled(bool value);
        double get_orbiting_radius_start() const;
        void set_orbiting_radius_start(double value);
        double get_orbiting_radius_step() const;
        void set_orbiting_radius_step(double value);
        DirectionalBullets2D::OrbitingFollowMode get_orbiting_follow_mode() const;
        void set_orbiting_follow_mode(DirectionalBullets2D::OrbitingFollowMode value);
        double get_orbiting_follow_deadzone() const;
        void set_orbiting_follow_deadzone(double value);
        DirectionalBullets2D::OrbitingLockPolicy get_orbiting_lock_policy() const;
        void set_orbiting_lock_policy(DirectionalBullets2D::OrbitingLockPolicy value);
        bool get_orbiting_rigid_follow() const;
        void set_orbiting_rigid_follow(bool value);

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
        int get_pattern_seed() const;
        void set_pattern_seed(int value);
        int get_max_live_bullets() const;
        void set_max_live_bullets(int value);
        double get_homing_retarget_phase() const;
        void set_homing_retarget_phase(double value);
        // Helper accessors for the new danmaku generators + aimed prediction.
        double get_helper_aimed_prediction() const;
        void set_helper_aimed_prediction(double value);
        double get_helper_aimed_prediction_time() const;
        void set_helper_aimed_prediction_time(double value);
        int get_helper_flower_petals() const;
        void set_helper_flower_petals(int value);
        int get_helper_flower_bullets_per_petal() const;
        void set_helper_flower_bullets_per_petal(int value);
        double get_helper_flower_radius() const;
        void set_helper_flower_radius(double value);
        double get_helper_flower_petal_spread() const;
        void set_helper_flower_petal_spread(double value);
        double get_helper_flower_petal_sharpness() const;
        void set_helper_flower_petal_sharpness(double value);
        double get_helper_flower_base_rotation() const;
        void set_helper_flower_base_rotation(double value);
        bool get_helper_flower_face_outward() const;
        void set_helper_flower_face_outward(bool value);
        double get_helper_flower_facing_offset_deg() const;
        void set_helper_flower_facing_offset_deg(double value);
        double get_helper_ellipse_radius_x() const;
        void set_helper_ellipse_radius_x(double value);
        double get_helper_ellipse_radius_y() const;
        void set_helper_ellipse_radius_y(double value);
        double get_helper_ellipse_rotation() const;
        void set_helper_ellipse_rotation(double value);
        double get_helper_ellipse_start_angle() const;
        void set_helper_ellipse_start_angle(double value);
        double get_helper_ellipse_arc() const;
        void set_helper_ellipse_arc(double value);
        int get_helper_ellipse_mode() const;
        void set_helper_ellipse_mode(int value);
        int get_helper_ellipse_gap_count() const;
        void set_helper_ellipse_gap_count(int value);
        double get_helper_ellipse_gap_width() const;
        void set_helper_ellipse_gap_width(double value);
        bool get_helper_ellipse_face_outward() const;
        void set_helper_ellipse_face_outward(bool value);
        double get_helper_ellipse_facing_offset_deg() const;
        void set_helper_ellipse_facing_offset_deg(double value);
        double get_helper_rain_band_width() const;
        void set_helper_rain_band_width(double value);
        Vector2 get_helper_rain_direction() const;
        void set_helper_rain_direction(const Vector2 &value);
        double get_helper_rain_drop_spacing() const;
        void set_helper_rain_drop_spacing(double value);
        double get_helper_rain_jitter() const;
        void set_helper_rain_jitter(double value);
        double get_helper_scatter_burst_radius() const;
        void set_helper_scatter_burst_radius(double value);
        double get_helper_scatter_facing_jitter() const;
        void set_helper_scatter_facing_jitter(double value);
        int get_helper_scatter_seed() const;
        void set_helper_scatter_seed(int value);
        int get_helper_polygon_vertices() const;
        void set_helper_polygon_vertices(int value);
        double get_helper_polygon_radius() const;
        void set_helper_polygon_radius(double value);
        double get_helper_polygon_vertex_bias() const;
        void set_helper_polygon_vertex_bias(double value);
        double get_helper_polygon_base_rotation() const;
        void set_helper_polygon_base_rotation(double value);
        bool get_helper_polygon_face_outward() const;
        void set_helper_polygon_face_outward(bool value);
        double get_helper_polygon_facing_offset_deg() const;
        void set_helper_polygon_facing_offset_deg(double value);
        int get_helper_multispiral_arms() const;
        void set_helper_multispiral_arms(int value);
        double get_helper_multispiral_start_radius() const;
        void set_helper_multispiral_start_radius(double value);
        double get_helper_multispiral_radius_step() const;
        void set_helper_multispiral_radius_step(double value);
        double get_helper_multispiral_angle_step() const;
        void set_helper_multispiral_angle_step(double value);
        bool get_helper_multispiral_rotate_with_marker() const;
        void set_helper_multispiral_rotate_with_marker(bool value);
        int get_helper_multispiral_facing() const;
        void set_helper_multispiral_facing(int value);
        double get_helper_multispiral_facing_offset_deg() const;
        void set_helper_multispiral_facing_offset_deg(double value);
        int get_helper_multispiral_arm_stride() const;
        void set_helper_multispiral_arm_stride(int value);
        int get_helper_cross_arm_count() const;
        void set_helper_cross_arm_count(int value);
        double get_helper_cross_arm_length() const;
        void set_helper_cross_arm_length(double value);
        double get_helper_cross_spacing() const;
        void set_helper_cross_spacing(double value);
        double get_helper_cross_base_rotation() const;
        void set_helper_cross_base_rotation(double value);
        bool get_helper_cross_face_outward() const;
        void set_helper_cross_face_outward(bool value);
        double get_helper_cross_facing_offset_deg() const;
        void set_helper_cross_facing_offset_deg(double value);
        int get_helper_star_points() const;
        void set_helper_star_points(int value);
        double get_helper_star_outer_radius() const;
        void set_helper_star_outer_radius(double value);
        double get_helper_star_inner_radius() const;
        void set_helper_star_inner_radius(double value);
        double get_helper_star_base_rotation() const;
        void set_helper_star_base_rotation(double value);
        bool get_helper_star_face_outward() const;
        void set_helper_star_face_outward(bool value);
        double get_helper_star_facing_offset_deg() const;
        void set_helper_star_facing_offset_deg(double value);
        double get_helper_heart_size() const;
        void set_helper_heart_size(double value);
        double get_helper_heart_base_rotation() const;
        void set_helper_heart_base_rotation(double value);
        bool get_helper_heart_face_outward() const;
        void set_helper_heart_face_outward(bool value);
        double get_helper_heart_facing_offset_deg() const;
        void set_helper_heart_facing_offset_deg(double value);
        double get_helper_wave_width() const;
        void set_helper_wave_width(double value);
        double get_helper_wave_amplitude() const;
        void set_helper_wave_amplitude(double value);
        double get_helper_wave_waves() const;
        void set_helper_wave_waves(double value);
        Vector2 get_helper_wave_direction() const;
        void set_helper_wave_direction(const Vector2 &value);
        bool get_helper_wave_face_direction() const;
        void set_helper_wave_face_direction(bool value);
        double get_helper_wave_facing_offset_deg() const;
        void set_helper_wave_facing_offset_deg(double value);
        int get_helper_waterfall_columns() const;
        void set_helper_waterfall_columns(int value);
        double get_helper_waterfall_column_spacing() const;
        void set_helper_waterfall_column_spacing(double value);
        int get_helper_waterfall_rows() const;
        void set_helper_waterfall_rows(int value);
        double get_helper_waterfall_row_spacing() const;
        void set_helper_waterfall_row_spacing(double value);
        double get_helper_waterfall_stagger() const;
        void set_helper_waterfall_stagger(double value);
        Vector2 get_helper_waterfall_rain_direction() const;
        void set_helper_waterfall_rain_direction(const Vector2 &value);
        double get_helper_waterfall_jitter() const;
        void set_helper_waterfall_jitter(double value);
        double get_helper_waterfall_facing_offset_deg() const;
        void set_helper_waterfall_facing_offset_deg(double value);
        int get_helper_lattice_columns() const;
        void set_helper_lattice_columns(int value);
        int get_helper_lattice_rows() const;
        void set_helper_lattice_rows(int value);
        double get_helper_lattice_spacing_x() const;
        void set_helper_lattice_spacing_x(double value);
        double get_helper_lattice_spacing_y() const;
        void set_helper_lattice_spacing_y(double value);
        bool get_helper_lattice_stagger_rows() const;
        void set_helper_lattice_stagger_rows(bool value);
        bool get_helper_lattice_face_outward() const;
        void set_helper_lattice_face_outward(bool value);
        double get_helper_lattice_facing_offset_deg() const;
        void set_helper_lattice_facing_offset_deg(double value);
        int get_helper_rose_petals() const;
        void set_helper_rose_petals(int value);
        double get_helper_rose_radius() const;
        void set_helper_rose_radius(double value);
        double get_helper_rose_lobe_sharpness() const;
        void set_helper_rose_lobe_sharpness(double value);
        double get_helper_rose_base_rotation() const;
        void set_helper_rose_base_rotation(double value);
        bool get_helper_rose_face_outward() const;
        void set_helper_rose_face_outward(bool value);
        double get_helper_rose_facing_offset_deg() const;
        void set_helper_rose_facing_offset_deg(double value);
        int get_helper_counter_spiral_arms() const;
        void set_helper_counter_spiral_arms(int value);
        double get_helper_counter_spiral_start_radius() const;
        void set_helper_counter_spiral_start_radius(double value);
        double get_helper_counter_spiral_radius_step() const;
        void set_helper_counter_spiral_radius_step(double value);
        double get_helper_counter_spiral_angle_step() const;
        void set_helper_counter_spiral_angle_step(double value);
        bool get_helper_counter_spiral_rotate_with_marker() const;
        void set_helper_counter_spiral_rotate_with_marker(bool value);
        int get_helper_counter_spiral_facing() const;
        void set_helper_counter_spiral_facing(int value);
        double get_helper_counter_spiral_facing_offset_deg() const;
        void set_helper_counter_spiral_facing_offset_deg(double value);
        int get_helper_counter_spiral_arm_stride() const;
        void set_helper_counter_spiral_arm_stride(int value);
        bool get_helper_counter_spiral_mirror_alternate_arms() const;
        void set_helper_counter_spiral_mirror_alternate_arms(bool value);
        Vector2 get_helper_corridor_aim_direction() const;
        void set_helper_corridor_aim_direction(const Vector2 &value);
        double get_helper_corridor_width() const;
        void set_helper_corridor_width(double value);
        double get_helper_corridor_spacing() const;
        void set_helper_corridor_spacing(double value);
        double get_helper_corridor_gap_width() const;
        void set_helper_corridor_gap_width(double value);
        bool get_helper_corridor_face_aim() const;
        void set_helper_corridor_face_aim(bool value);
        double get_helper_corridor_facing_offset_deg() const;
        void set_helper_corridor_facing_offset_deg(double value);
        double get_helper_lissajous_size_x() const;
        void set_helper_lissajous_size_x(double value);
        double get_helper_lissajous_size_y() const;
        void set_helper_lissajous_size_y(double value);
        double get_helper_lissajous_freq_x() const;
        void set_helper_lissajous_freq_x(double value);
        double get_helper_lissajous_freq_y() const;
        void set_helper_lissajous_freq_y(double value);
        double get_helper_lissajous_phase() const;
        void set_helper_lissajous_phase(double value);
        bool get_helper_lissajous_face_outward() const;
        void set_helper_lissajous_face_outward(bool value);
        double get_helper_lissajous_facing_offset_deg() const;
        void set_helper_lissajous_facing_offset_deg(double value);
        PackedVector2Array get_helper_edge_points() const;
        void set_helper_edge_points(const PackedVector2Array &value);
        double get_helper_circle_radius() const;
        void set_helper_circle_radius(double value);
        bool get_helper_circle_face_outward() const;
        void set_helper_circle_face_outward(bool value);
        double get_helper_circle_facing_offset_deg() const;
        void set_helper_circle_facing_offset_deg(double value);
        Vector2 get_helper_rectangle_size() const;
        void set_helper_rectangle_size(const Vector2 &value);
        bool get_helper_rectangle_face_outward() const;
        void set_helper_rectangle_face_outward(bool value);
        double get_helper_rectangle_facing_offset_deg() const;
        void set_helper_rectangle_facing_offset_deg(double value);
        double get_helper_square_size() const;
        void set_helper_square_size(double value);
        bool get_helper_square_face_outward() const;
        void set_helper_square_face_outward(bool value);
        double get_helper_square_facing_offset_deg() const;
        void set_helper_square_facing_offset_deg(double value);
        int get_helper_regular_polygon_vertices() const;
        void set_helper_regular_polygon_vertices(int value);
        double get_helper_regular_polygon_radius() const;
        void set_helper_regular_polygon_radius(double value);
        double get_helper_regular_polygon_rotation() const;
        void set_helper_regular_polygon_rotation(double value);
        bool get_helper_regular_polygon_face_outward() const;
        void set_helper_regular_polygon_face_outward(bool value);
        double get_helper_regular_polygon_facing_offset_deg() const;
        void set_helper_regular_polygon_facing_offset_deg(double value);
        NodePath get_helper_path2d_path() const;
        void set_helper_path2d_path(const NodePath &p_path);
        Node *get_helper_path2d_node() const;
        void set_helper_path2d_node(Node *node);
        CustomFacing get_helper_edge_facing() const;
        void set_helper_edge_facing(CustomFacing value);
        double get_helper_edge_custom_angle_deg() const;
        void set_helper_edge_custom_angle_deg(double value);
        int get_helper_edge_side() const;
        void set_helper_edge_side(int value);
        int get_helper_side_mode() const;
        void set_helper_side_mode(int value);
        double get_helper_side_spread() const;
        void set_helper_side_spread(double value);
        double get_helper_side_spread_exponent() const;
        void set_helper_side_spread_exponent(double value);
        int get_helper_side_seed() const;
        void set_helper_side_seed(int value);
        bool get_helper_edge_closed() const;
        void set_helper_edge_closed(bool value);
        bool get_helper_edge_random_sample() const;
        void set_helper_edge_random_sample(bool value);
        double get_helper_edge_jitter() const;
        void set_helper_edge_jitter(double value);
        double get_helper_edge_facing_offset_deg() const;
        void set_helper_edge_facing_offset_deg(double value);
        int get_helper_edge_seed() const;
        void set_helper_edge_seed(int value);
        double get_helper_edge_spread() const;
        void set_helper_edge_spread(double value);
        double get_helper_edge_spread_exponent() const;
        void set_helper_edge_spread_exponent(double value);
        double get_helper_edge_tangent_jitter() const;
        void set_helper_edge_tangent_jitter(double value);
        int get_edge_point_count() const;
        // Edge math API: normals of the compiled Custom outline (same order
        // as the points, unit length, flipped when helper_edge_side is
        // Inside). Empty when there are no usable points.
        PackedVector2Array get_edge_normals() const;
        // Total arc length of the polyline in pixels (closed loops include
        // the closing segment). 0 when unusable.
        double get_edge_total_length() const;
        // Evenly spaced points along the polyline (arc-length parameterized,
        // generator-local). Useful for composing custom per-segment patterns
        // in GDScript. Empty when unusable.
        PackedVector2Array sample_edge_points(int count) const;
        // Cheap pre-flight for scripts: true when the polyline currently
        // holds at least 1 finite point. Never warns or errors.
        bool has_valid_edge_points() const;
        // Good API aliases: pass a polygon/polyline, get transforms back.
        PackedVector2Array get_edge_points() const;
        void set_edge_points(const PackedVector2Array &value);
        PackedInt32Array get_helper_skip_indices() const;
        void set_helper_skip_indices(const PackedInt32Array &value);
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
        // One-call preset fill (see BulletFactory2D::PatternPreset).
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
        int get_pooled_volley_count() const;

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
        // How many spawned volleys are currently tracked for retargeting.
        int get_live_volley_count() const;
        // The tracked live volley instances (pruned first). Lets GDScript
        // call the full DirectionalBullets2D API on each volley directly.
        // Variants auto-null if an instance is freed later.
        Array get_live_volleys() const;
        // Forgets all tracked volleys (they keep flying untouched).
        void clear_live_volleys();
        // Begins a burst chain / telegraph warning / fires the next burst
        // volley. Public so waves and boss phases can drive phrasing by hand
        // (begin + fire on timers) instead of only through the auto loop.
        void begin_burst();
        void begin_telegraph();
        void fire_burst_volley();
        // Takes ownership of a manually-woken volley (see enable_bullet,
        // which detaches spawner ownership): stamps, hooks the reached
        // forwarder, and tracks it for retargeting. Queues are left alone.
        // Returns false for null or non-live (pooled/outside-tree) instances.
        bool adopt_live_volley(DirectionalBullets2D *bullets);
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
        // Forwards the volley instance's bullet_homing_target_reached as the
        // spawner-level volley_bullet_homing_target_reached signal.
        void _on_volley_bullet_homing_target_reached(Object *directional_bullets_instance, int bullet_index, Object *target, const Vector2 &target_global_position);

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
        double preview_dot_radius = 4.0;
        // Extra pixels between the dot edge and the arrow tail: the shaft
        // starts at dot_radius + gap so it never hides under the dot.
        double preview_arrow_gap = 3.0;
        // Shaft length in pixels, measured from the gap end to the tip.
        double preview_arrow_length = 16.0;
        double preview_arrow_width = 2.0;
        double preview_arrow_head_length = 8.0;
        double preview_arrow_head_width = 10.0;

        bool get_show_pattern_preview() const;
        void set_show_pattern_preview(bool value);
        bool get_show_preview_during_runtime() const;
        void set_show_preview_during_runtime(bool value);
        Color get_preview_dot_color() const;
        void set_preview_dot_color(const Color &value);
        Color get_preview_arrow_color() const;
        void set_preview_arrow_color(const Color &value);
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

        bool get_shooting_enabled() const;
        void set_shooting_enabled(bool value);
        double get_shoot_interval_sec() const;
        void set_shoot_interval_sec(double value);
        double get_shoot_initial_delay_sec() const;
        void set_shoot_initial_delay_sec(double value);
        int get_max_volleys() const;
        void set_max_volleys(int value);
        int get_volleys_fired() const;
        double get_transforms_scale() const;
        void set_transforms_scale(double value);
        Vector2 get_spawn_position_offset() const;
        void set_spawn_position_offset(const Vector2 &value);

        TransformsSource get_transforms_source() const;
        void set_transforms_source(TransformsSource value);
        int get_helper_bullets_amount() const;
        void set_helper_bullets_amount(int value);

        int get_helper_grid_rows_per_column() const;
        void set_helper_grid_rows_per_column(int value);
        int get_helper_grid_alignment() const;
        void set_helper_grid_alignment(int value);
        double get_helper_grid_column_offset() const;
        void set_helper_grid_column_offset(double value);
        double get_helper_grid_row_offset() const;
        void set_helper_grid_row_offset(double value);
        bool get_helper_grid_rotate_with_marker() const;
        void set_helper_grid_rotate_with_marker(bool value);
        bool get_helper_grid_random_local_rotation() const;
        void set_helper_grid_random_local_rotation(bool value);
        double get_helper_grid_jitter() const;
        void set_helper_grid_jitter(double value);

        double get_helper_ring_radius() const;
        void set_helper_ring_radius(double value);
        double get_helper_ring_start_angle() const;
        void set_helper_ring_start_angle(double value);
        double get_helper_ring_arc() const;
        void set_helper_ring_arc(double value);
        bool get_helper_ring_rotate_with_marker() const;
        void set_helper_ring_rotate_with_marker(bool value);
        bool get_helper_ring_random_rotation() const;
        void set_helper_ring_random_rotation(bool value);
        bool get_helper_ring_face_outward() const;
        void set_helper_ring_face_outward(bool value);
        double get_helper_ring_y_scale() const;
        void set_helper_ring_y_scale(double value);
        double get_helper_ring_facing_offset_deg() const;
        void set_helper_ring_facing_offset_deg(double value);

        double get_helper_fan_spread() const;
        void set_helper_fan_spread(double value);
        double get_helper_fan_direction_angle() const;
        void set_helper_fan_direction_angle(double value);
        double get_helper_fan_step_offset() const;
        void set_helper_fan_step_offset(double value);
        bool get_helper_fan_centered() const;
        void set_helper_fan_centered(bool value);
        double get_helper_fan_angle_jitter() const;
        void set_helper_fan_angle_jitter(double value);

        double get_helper_spiral_start_radius() const;
        void set_helper_spiral_start_radius(double value);
        double get_helper_spiral_radius_step() const;
        void set_helper_spiral_radius_step(double value);
        double get_helper_spiral_angle_step() const;
        void set_helper_spiral_angle_step(double value);
        bool get_helper_spiral_rotate_with_marker() const;
        void set_helper_spiral_rotate_with_marker(bool value);
        int get_helper_spiral_facing() const;
        void set_helper_spiral_facing(int value);
        double get_helper_spiral_facing_offset_deg() const;
        void set_helper_spiral_facing_offset_deg(double value);

        Vector2 get_helper_line_direction() const;
        void set_helper_line_direction(const Vector2 &value);
        double get_helper_line_spacing() const;
        void set_helper_line_spacing(double value);
        bool get_helper_line_face_direction() const;
        void set_helper_line_face_direction(bool value);
        int get_helper_line_anchor() const;
        void set_helper_line_anchor(int value);
        bool get_helper_line_perpendicular() const;
        void set_helper_line_perpendicular(bool value);

        NodePath get_helper_aimed_target_path() const;
        void set_helper_aimed_target_path(const NodePath &p_path);

        Node2D *get_helper_aimed_target() const;
        void set_helper_aimed_target(Node2D *target);

        double get_helper_aimed_spread() const;
        void set_helper_aimed_spread(double value);
        double get_helper_aimed_step_offset() const;
        void set_helper_aimed_step_offset(double value);
        bool get_helper_aimed_centered() const;
        void set_helper_aimed_centered(bool value);

        // Collects one global transform per volley bullet: the Node2D children
        // of the transforms generator (else the generator itself, else this).
        TypedArray<Transform2D> collect_spawn_transforms() const;
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

        virtual void _ready() override;
        virtual void _process(double delta) override;
        // Plain name (no override): godot-cpp routes notifications to this
        // method through the binding machinery, same as BulletFactory2D.
        void _notification(int p_what);

        // Hides the helper_* property groups that don't belong to the active
        // transforms_source, so the inspector only shows relevant options.
        // (Name hiding, picked up by the binding machinery - not an override.)
        void _validate_property(PropertyInfo &p_property) const;


    protected:
	static void _bind_methods();


    private:
        // Countdown to the next volley; volleys fired since (re)arming.
        double shoot_time_left = 0.0;
        int volleys_fired = 0;
        // Re-entrancy latch for shoot_once(): the configuring signals emitted
        // during apply_volley_homing_and_orbiting() AND volley_fired below run
        // user code synchronously, which must not nest another shoot_once()
        // (unbounded recursion, counter races, pool double-pop). The latch is
        // held across apply + emit + the max_volleys cap transition (cleared
        // after), so nested calls from ANY of those handlers are rejected; use
        // call_deferred("shoot_once") (or shoot_once_deferred()) instead.
        // Per-spawner: cross-spawner A->B->A nesting is additionally stopped
        // by the file-local global depth guard in the .cpp.
        bool shoot_once_reentrant_guard = false;
        // Single clear-point for the latch + global depth above. Every abort
        // and the normal exit go through here so a new early return can never
        // leak the latch (permanent shoot lockout) or the depth (permanent
        // cross-spawner lockout).
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
        // const resolve_homing_targets). Without it every volley/retarget
        // with no enemies present spams the debugger; the latch clears as
        // soon as any resolution succeeds, so a regression warns again.
        mutable bool homing_empty_targets_warned = false;
        // Cursor for HOMING_SELECT_ROUND_ROBIN across volleys (mutable: used
        // by the const resolve_homing_targets).
        mutable int homing_round_robin_cursor = 0;
        // Lazily created RNG for HOMING_SELECT_RANDOM (mutable: used by the
        // const resolve_homing_targets).
        mutable Ref<RandomNumberGenerator> homing_rng;
        // Spin runtime state (never stored, advances in _process only).
        double spin_angle_deg = 0.0;
        double spin_time_sec = 0.0;
        // Burst runtime state (never stored): shots left in the current burst,
        // countdown to the next burst shot, telegraph countdown, and the
        // mirror flag for the next burst volley.
        int burst_shots_left = 0;
        double burst_time_left = 0.0;
        double telegraph_time_left = 0.0;
        bool burst_mirror_next = false;
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
        // Each entry is a Dictionary: { "transforms_source": int,
        // "helper_bullets_amount": int, "spawn_data": Ref, "preset": int }.
        // Only set keys override the live spawner for that shot.
        Array pattern_list_entries;
        bool pattern_list_simultaneous = false;
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
        Node2D *tracked_target = nullptr;
        Transform2D tracked_target_origin;
        bool tracked_has_target_origin = false;
        PackedVector2Array tracked_marker_origins;
        PackedRealArray tracked_marker_rots;
        PackedInt64Array tracked_marker_ids;
        int tracked_child_count = -1;
        Transform2D tracked_self_global;
        bool tracked_has_self = false;
        // Edge preview tracking: snapshot of the polyline for live dirty checks.
        PackedVector2Array tracked_edge_points;
        // Editor-only pattern preview holder (null at runtime, never saved),
        // plus its two self-repainting _draw layers (dots + arrows).
        Node2D *preview_holder = nullptr;
        PatternPreviewLayer2D *preview_dots_layer = nullptr;
        PatternPreviewLayer2D *preview_arrows_layer = nullptr;
        // Re-entrancy latch for rebuild_preview(): setters, tree notifications
        // (child order / transform changed) and the preview loop can all ask
        // for a rebuild while one is already running (holder add_child fires
        // NOTIFICATION_CHILD_ORDER_CHANGED synchronously). A nested entry is
        // always redundant — the outer pass re-reads everything — so it
        // returns immediately instead of recursing into a stack overflow.
        // Mutable: rebuilds happen from const setters. Restored on every exit
        // path (early returns included) so one abort can never wedge preview.
        mutable bool preview_rebuild_in_progress = false;

        bool auto_shooting_active() const;
        // Advances spin_angle_deg by delta according to spin_mode.
        void advance_spin(double delta);
        // Whether the preview may exist right now: editor always (toggle
        // decides), runtime only when the user opted in.
        bool preview_allowed_here() const;
        // Whether the preview refresh loop must run: allowed + toggled on.
        bool preview_active() const;
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

        // True while interval retargeting must keep _process alive: homing on
        // + retarget armed + inside the tree at runtime.
        bool homing_retarget_active() const;
        // Wakes _process when retargeting becomes active (runtime only).
        void update_homing_process_state();
        // Remembers a fresh volley for retargeting (deduped: pooled instances
        // reuse ids) and prunes dead/foreign entries via the tracker.
        void track_live_volley(DirectionalBullets2D *bullets);
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
        // mutable latch). Quiet passes never warn; a success clears the
        // latch via clear_empty_homing_targets_warning().
        void warn_empty_homing_targets_once(const String &message, bool quiet) const;
        void clear_empty_homing_targets_warning() const;
        // Pushes the steering block (smoothing, update interval, reached
        // distance, texture control, auto-pop flags, per-bullet smoothing
        // fan) onto a volley. Shared by volley setup and retarget passes so
        // runtime tuning reaches flying volleys instead of only new ones.
        void apply_steering_to_volley(DirectionalBullets2D *volley) const;
        // Enables or refreshes orbiting on a volley: bullets whose orbit is
        // not yet enabled get enabled, the rest get radius/direction/texture
        // updated in place (re-enabling would warn and keep stale values).
        // Skipped entirely for DontMove.
        void apply_orbiting_to_volley(DirectionalBullets2D *volley) const;
        // Applies the homing + orbiting configuration to a freshly spawned
        // (or pool-reused) volley: steering props, resolved targets (pushed
        // before orbiting so rings can lock immediately), orbiting, signal
        // hookup, and live-volley tracking. Called from shoot_once() before
        // volley_fired so handlers observe fully configured bullets.
        void apply_volley_homing_and_orbiting(DirectionalBullets2D *bullets);
        // Sequencer internals: validates one Dictionary entry (preset /
        // source / amount / spawn_data overrides) and fires the next queued
        // entry for the _process driver.
        bool apply_pattern_list_entry(const Variant &entry);
        void fire_pattern_list_entry();
        // Fire cone check: true when any resolved target sits within half
        // the fire arc of the spawner's facing. 0 arc = omnidirectional.
        bool fire_arc_covers_targets(const Array &targets) const;
        // make_default_edge_crest() builds the out-of-box demo sine crest so
        // switching to Custom renders the reference spray with zero input.
        static PackedVector2Array make_default_edge_crest();
        // Live Path2D outline in generator-local pixels. Empty when unusable;
        // quiet suppresses warnings (preview).
        PackedVector2Array sample_path2d_polyline(bool quiet) const;
        // True for the closed-outline modes the universal side pass supports.
        static bool supports_side_spread(TransformsSource source);



};
} // namespace BlastBullets2D

// Need this in order to expose the enum to Godot Engine
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::TransformsSource);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::CustomFacing);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::SpinMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingTargetSource);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingNodeNameMatch);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingTargetSelection);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingRetargetMode);
