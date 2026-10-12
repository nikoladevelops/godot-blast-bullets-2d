#pragma once

#include "data/bullet_curves_data2d.hpp"
#include "data/bullet_effect_layer_data2d.hpp"
#include "data/bullet_rotation_data2d.hpp"
#include "data/bullet_sound_data2d.hpp"
#include "data/bullet_speed_data2d.hpp"
#include "data/bullet_volley_data2d.hpp"

#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace BlastBullets2D {
using namespace godot;

// What a bullet becomes when it is turned into a pickup (coin, gem, score
// item): how it looks (an animation from SpriteFrames, or one texture; size,
// tint/opacity, fades, material, attachment, effect layers), how it moves
// (the built-in burst and magnet, the bullet's own motion, or the
// developer's own BulletSpeedData2D / BulletCurvesData2D / BulletRotationData2D),
// when it counts as collected (a radius around the target, or touching the
// target physically; value, sound, effect, callback) and how long it lives.
//
// Used by BulletVolley2D.convert_to_pickups, BulletSpawner2D.
// convert_live_bullets_to_pickups and BulletFactory2D.convert_*_to_pickups.
// The converted bullets leave their volleys (silently, or with their clear
// effects) and one new pickup volley per call is spawned; collecting a
// pickup emits BulletFactory2D.pickup_collected. Every accepted change emits
// `changed` and drops the cached spawn data (the next conversion sees it,
// pickups already flying keep what they were made with).
class BulletPickupData2D : public Resource {
	GDCLASS(BulletPickupData2D, Resource)

public:
	// SERIALIZED ids: never renumber.
	enum MotionMode {
		// Built-in coin physics: keeps part of the bullet's velocity, drifts
		// out with drag, then the magnet accelerates it to the target.
		MOTION_BURST_AND_MAGNET = 0,
		// Each pickup keeps the speed, top speed and acceleration of the
		// bullet it was (as the bullet was already moving); only the pull
		// (steering) towards the target is added.
		MOTION_KEEP_BULLET_MOTION = 1,
		// The developer's own speed_data / curves_data decide the speed.
		MOTION_CUSTOM = 2
	};

	// Look
	Ref<SpriteFrames> sprite_frames;
	StringName animation = StringName("default");
	Ref<Texture2D> texture;
	Vector2 texture_size = Vector2(0, 0); // (0, 0): the size of the first frame
	double scale_multiplier = 1.0;
	double scale_random_min = 1.0;
	double scale_random_max = 1.0;
	double rotation_random_min_degrees = 0.0;
	double rotation_random_max_degrees = 0.0;
	Color tint = Color(1, 1, 1, 1); // alpha = opacity
	double fade_in_sec = 0.0;
	double fade_out_sec = 0.5;
	double lifetime_sec = 12.0; // 0 = never expires
	Ref<Material> material;
	int z_index = 0;
	Ref<PackedScene> attachment;
	Vector2 attachment_offset = Vector2(0, 0);
	TypedArray<BulletEffectLayerData2D> effect_layers;
	// Motion
	int motion_mode = MOTION_BURST_AND_MAGNET;
	Ref<BulletSpeedData2D> speed_data;
	Ref<BulletCurvesData2D> curves_data;
	Ref<BulletRotationData2D> rotation_data;
	// Burst out of the bullet's motion (MOTION_BURST_AND_MAGNET)
	double initial_velocity_scale = 0.4;
	double scatter_speed_min = 0.0;
	double scatter_speed_max = 0.0;
	double drag = 4.0;
	// Pulled to the target
	double magnet_delay_sec = 0.35;
	double magnet_acceleration = 1200.0; // MOTION_BURST_AND_MAGNET
	double magnet_max_speed = 900.0; // MOTION_BURST_AND_MAGNET
	double magnet_smoothing = 0.0;
	// Collection
	double collect_radius = 24.0;
	bool collect_on_collision = false;
	Variant value;
	Ref<BulletSoundData2D> collect_sound;
	Ref<BulletEffectLayerData2D> collect_effect;
	Callable collect_callback; // runtime only, never saved
	// Optional physics (0/0: pure distance collection)
	int collision_layer = 0;
	int collision_mask = 0;
	double collision_radius = 8.0;
	// Conversion
	bool inherit_custom_data = false;
	bool fire_clear_effects = false;
	bool follow_source_time_scale = false;
	// False: the factory's despawn bounds never cull these pickups.
	bool obey_despawn_bounds = true;

	// The spawn data of the pickup volley (cached until a setter runs).
	Ref<BulletVolleyData2D> get_volley_data();

#define BLAST_PICKUP_ACCESSORS(TYPE, NAME)   \
	TYPE get_##NAME() const { return NAME; } \
	void set_##NAME(TYPE p_value);
	BLAST_PICKUP_ACCESSORS(Ref<SpriteFrames>, sprite_frames)
	BLAST_PICKUP_ACCESSORS(StringName, animation)
	BLAST_PICKUP_ACCESSORS(Ref<Texture2D>, texture)
	BLAST_PICKUP_ACCESSORS(Vector2, texture_size)
	BLAST_PICKUP_ACCESSORS(double, scale_multiplier)
	BLAST_PICKUP_ACCESSORS(double, scale_random_min)
	BLAST_PICKUP_ACCESSORS(double, scale_random_max)
	BLAST_PICKUP_ACCESSORS(double, rotation_random_min_degrees)
	BLAST_PICKUP_ACCESSORS(double, rotation_random_max_degrees)
	BLAST_PICKUP_ACCESSORS(Color, tint)
	BLAST_PICKUP_ACCESSORS(double, fade_in_sec)
	BLAST_PICKUP_ACCESSORS(double, fade_out_sec)
	BLAST_PICKUP_ACCESSORS(double, lifetime_sec)
	BLAST_PICKUP_ACCESSORS(Ref<Material>, material)
	BLAST_PICKUP_ACCESSORS(int, z_index)
	BLAST_PICKUP_ACCESSORS(Ref<PackedScene>, attachment)
	BLAST_PICKUP_ACCESSORS(Vector2, attachment_offset)
	BLAST_PICKUP_ACCESSORS(TypedArray<BulletEffectLayerData2D>, effect_layers)
	BLAST_PICKUP_ACCESSORS(int, motion_mode)
	BLAST_PICKUP_ACCESSORS(Ref<BulletSpeedData2D>, speed_data)
	BLAST_PICKUP_ACCESSORS(Ref<BulletCurvesData2D>, curves_data)
	BLAST_PICKUP_ACCESSORS(Ref<BulletRotationData2D>, rotation_data)
	BLAST_PICKUP_ACCESSORS(double, initial_velocity_scale)
	BLAST_PICKUP_ACCESSORS(double, scatter_speed_min)
	BLAST_PICKUP_ACCESSORS(double, scatter_speed_max)
	BLAST_PICKUP_ACCESSORS(double, drag)
	BLAST_PICKUP_ACCESSORS(double, magnet_delay_sec)
	BLAST_PICKUP_ACCESSORS(double, magnet_acceleration)
	BLAST_PICKUP_ACCESSORS(double, magnet_max_speed)
	BLAST_PICKUP_ACCESSORS(double, magnet_smoothing)
	BLAST_PICKUP_ACCESSORS(double, collect_radius)
	BLAST_PICKUP_ACCESSORS(bool, collect_on_collision)
	BLAST_PICKUP_ACCESSORS(Variant, value)
	BLAST_PICKUP_ACCESSORS(Ref<BulletSoundData2D>, collect_sound)
	BLAST_PICKUP_ACCESSORS(Ref<BulletEffectLayerData2D>, collect_effect)
	BLAST_PICKUP_ACCESSORS(Callable, collect_callback)
	BLAST_PICKUP_ACCESSORS(int, collision_layer)
	BLAST_PICKUP_ACCESSORS(int, collision_mask)
	BLAST_PICKUP_ACCESSORS(double, collision_radius)
	BLAST_PICKUP_ACCESSORS(bool, inherit_custom_data)
	BLAST_PICKUP_ACCESSORS(bool, fire_clear_effects)
	BLAST_PICKUP_ACCESSORS(bool, follow_source_time_scale)
	BLAST_PICKUP_ACCESSORS(bool, obey_despawn_bounds)
#undef BLAST_PICKUP_ACCESSORS

	void _validate_property(PropertyInfo &p_property) const;

protected:
	static void _bind_methods();

private:
	Ref<BulletVolleyData2D> cached_volley_data;
	void accepted_change();
};

} // namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::BulletPickupData2D::MotionMode);
