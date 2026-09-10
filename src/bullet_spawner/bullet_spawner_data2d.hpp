#pragma once


#include "godot_cpp/classes/packed_scene.hpp"
#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/classes/shape2d.hpp"
#include "godot_cpp/classes/sprite_frames.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "shared/bullet_curves_data2d.hpp"
#include "spawn-data/directional_bullets_data2d.hpp"

namespace BlastBullets2D{
    using namespace godot;

    class BulletSpawnerData2D : public Resource{
    GDCLASS(BulletSpawnerData2D, Resource)

    public:
        Ref<DirectionalBulletsData2D> data = nullptr;
        Ref<SpriteFrames> sprite_frames;
        StringName animation;
        Ref<BulletCurvesData2D> speed_curves;
        int collision_layer = 1;
        int collision_mask = 1;
        Ref<Shape2D> collision_shape;
        Vector2 collision_shape_offset = Vector2(0, 0);
        // Movement pattern flags (see MultiMeshBullets2D::set_bullet_movement_pattern_from_path).
        // The Path2D itself lives on BulletSpawner2D (only Nodes can resolve
        // scene-tree paths); the Curve2D alternative was removed in favor of it.
        bool movement_pattern_face_movement_direction = false;
        bool movement_pattern_repeat = true;
        int light_mask = 1;
        int visibility_layer = 1;
        // PackedScene expected to contain a BulletAttachment2D. Stored as-is;
        // content is validated at runtime, not at bind time.
        Ref<PackedScene> bullet_attachment;
        Vector2 bullet_attachment_offset = Vector2(0, 0);


        Ref<DirectionalBulletsData2D> get_data() const;
        void set_data(const Ref<DirectionalBulletsData2D> &new_data);

        Ref<SpriteFrames> get_sprite_frames() const;
        void set_sprite_frames(const Ref<SpriteFrames> &new_sprite_frames);

        StringName get_animation() const;
        void set_animation(const StringName &new_animation);

        Ref<BulletCurvesData2D> get_speed_curves() const;
        void set_speed_curves(const Ref<BulletCurvesData2D> &new_speed_curves);

        int get_collision_layer() const;
        void set_collision_layer(int new_collision_layer);

        int get_collision_mask() const;
        void set_collision_mask(int new_collision_mask);

        Ref<Shape2D> get_collision_shape() const;
        void set_collision_shape(const Ref<Shape2D> &new_shape);

        Vector2 get_collision_shape_offset() const;
        void set_collision_shape_offset(const Vector2 &new_offset);

        bool get_movement_pattern_face_movement_direction() const;
        void set_movement_pattern_face_movement_direction(bool value);

        bool get_movement_pattern_repeat() const;
        void set_movement_pattern_repeat(bool value);

        int get_light_mask() const;
        void set_light_mask(int new_light_mask);

        int get_visibility_layer() const;
        void set_visibility_layer(int new_visibility_layer);

        Ref<PackedScene> get_bullet_attachment() const;
        void set_bullet_attachment(const Ref<PackedScene> &new_attachment);

        Vector2 get_bullet_attachment_offset() const;
        void set_bullet_attachment_offset(const Vector2 &new_offset);

    protected:
    static void _bind_methods();

    };
}
