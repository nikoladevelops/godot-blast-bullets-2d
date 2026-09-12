#pragma once

#include "factory/bullet_factory2d.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/variant/node_path.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "spawn-data/directional_bullets_data2d.hpp"

namespace BlastBullets2D {
using namespace godot;

class BulletSpawner2D : public Node2D{
    GDCLASS(BulletSpawner2D, Node2D)

    public:
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


    protected:
	static void _bind_methods();


    private:
        // Countdown to the next volley; volleys fired since (re)arming.
        double shoot_time_left = 0.0;
        int volleys_fired = 0;

        bool auto_shooting_active() const;



};
}
