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

        // RING
        double helper_ring_radius = 150.0;
        double helper_ring_start_angle = 0.0;
        double helper_ring_arc = 6.283185307179586; // Math::TAU
        bool helper_ring_rotate_with_marker = true;
        bool helper_ring_random_rotation = false;
        bool helper_ring_face_outward = true;

        // FAN
        double helper_fan_spread = 0.5;
        double helper_fan_direction_angle = 0.0;
        double helper_fan_step_offset = 0.0;

        // SPIRAL
        double helper_spiral_start_radius = 50.0;
        double helper_spiral_radius_step = 15.0;
        double helper_spiral_angle_step = 0.6;
        bool helper_spiral_rotate_with_marker = true;

        // LINE
        Vector2 helper_line_direction = Vector2(1, 0);
        double helper_line_spacing = 32.0;
        bool helper_line_face_direction = true;

        // AIMED
        // Scene-tree reference to the target node the aimed cone centers on.
        // Same NodePath pattern as the other scene references on this node.
        NodePath helper_aimed_target_path;
        // Runtime cache of the resolved target. Not a bound property.
        mutable Node2D *helper_aimed_target = nullptr;
        double helper_aimed_spread = 0.3;
        double helper_aimed_step_offset = 0.0;

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

        double get_helper_fan_spread() const;
        void set_helper_fan_spread(double value);
        double get_helper_fan_direction_angle() const;
        void set_helper_fan_direction_angle(double value);
        double get_helper_fan_step_offset() const;
        void set_helper_fan_step_offset(double value);

        double get_helper_spiral_start_radius() const;
        void set_helper_spiral_start_radius(double value);
        double get_helper_spiral_radius_step() const;
        void set_helper_spiral_radius_step(double value);
        double get_helper_spiral_angle_step() const;
        void set_helper_spiral_angle_step(double value);
        bool get_helper_spiral_rotate_with_marker() const;
        void set_helper_spiral_rotate_with_marker(bool value);

        Vector2 get_helper_line_direction() const;
        void set_helper_line_direction(const Vector2 &value);
        double get_helper_line_spacing() const;
        void set_helper_line_spacing(double value);
        bool get_helper_line_face_direction() const;
        void set_helper_line_face_direction(bool value);

        NodePath get_helper_aimed_target_path() const;
        void set_helper_aimed_target_path(const NodePath &p_path);

        Node2D *get_helper_aimed_target() const;
        void set_helper_aimed_target(Node2D *target);

        double get_helper_aimed_spread() const;
        void set_helper_aimed_spread(double value);
        double get_helper_aimed_step_offset() const;
        void set_helper_aimed_step_offset(double value);

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

        bool auto_shooting_active() const;



};
}

// Need this in order to expose the enum to Godot Engine
VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::TransformsSource);
