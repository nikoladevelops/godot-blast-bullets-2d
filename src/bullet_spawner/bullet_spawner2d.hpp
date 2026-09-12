#pragma once

#include "factory/bullet_factory2d.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/core/property_info.hpp"
#include "godot_cpp/variant/node_path.hpp"
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
            TRANSFORMS_FROM_HELPER_AIMED
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
        mutable BulletFactory2D *bullet_factory = nullptr;
        // Plain Node2D reference from the scene tree whose children provide
        // spawn transforms (same NodePath pattern as the factory, but
        // type-filtered to Node2D since native types always match).
        NodePath transforms_generator_path;
        // Runtime cache of the resolved generator. Not a bound property.
        mutable Node2D *transforms_generator = nullptr;
        Ref<DirectionalBulletsData2D> spawn_data;

        NodePath get_bullet_factory_path() const;
        void set_bullet_factory_path(const NodePath &p_path);

        BulletFactory2D *get_bullet_factory() const;
        void set_bullet_factory(BulletFactory2D *factory);

        NodePath get_transforms_generator_path() const;
        void set_transforms_generator_path(const NodePath &p_path);

        Node2D *get_transforms_generator() const;
        void set_transforms_generator(Node2D *generator);

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
        mutable Node2D *helper_aimed_target = nullptr;
        double helper_aimed_spread = 0.3;
        double helper_aimed_step_offset = 0.0;
        bool helper_aimed_centered = true;

        // PATTERN PREVIEW (EDITOR ONLY)
        //
        // Draws the volley pattern in the editor so helper options can be
        // tuned visually: one dot per spawn transform plus one facing arrow.
        // Rendered by two self-repainting PatternPreviewLayer2D nodes (custom
        // _draw, re-invoked by the engine on every repaint) inside an
        // owner-less holder node: the holder is never saved to the scene,
        // never exported, and never created at runtime.
        bool show_pattern_preview = true;
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
        bool shoot_once();
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
        // Spin runtime state (never stored, advances in _process only).
        double spin_angle_deg = 0.0;
        double spin_time_sec = 0.0;
        // Editor-only pattern preview holder (null at runtime, never saved),
        // plus its two self-repainting _draw layers (dots + arrows).
        Node2D *preview_holder = nullptr;
        PatternPreviewLayer2D *preview_dots_layer = nullptr;
        PatternPreviewLayer2D *preview_arrows_layer = nullptr;

        bool auto_shooting_active() const;
        // Advances spin_angle_deg by delta according to spin_mode.
        void advance_spin(double delta);
        // (Re)builds the editor preview from the current pattern.
        // No-op outside the editor or when the preview is disabled.
        void rebuild_preview();
        // collect_spawn_transforms() with error reporting: the public method
        // reports problems, the preview passes true to stay quiet.
        TypedArray<Transform2D> collect_spawn_transforms_impl(bool quiet) const;



};
} // namespace BlastBullets2D

// Need this in order to expose the enum to Godot Engine
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::TransformsSource);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::SpinMode);
