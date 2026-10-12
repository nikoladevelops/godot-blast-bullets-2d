#pragma once

#include "data/bullet_effect_layer_data2d.hpp"
#include "data/bullet_sound_data2d.hpp"
#include "data/bullet_volley_data2d.hpp"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace BlastBullets2D {
using namespace godot;

// What a bullet becomes when it is turned into a pickup (coin, gem, score
// item): how it looks (texture or animation, size, tint/opacity, fades), how
// it bursts out of the bullet's motion (kept velocity, scatter, drag), how it
// is pulled to its target (delay, acceleration, top speed, steering), when it
// counts as collected (radius, value, sound, effect), how long it lives and,
// optionally, a collision layer so a player Area2D can touch it too.
//
// Used by BulletVolley2D.convert_to_pickups, BulletSpawner2D.
// convert_live_bullets_to_pickups and BulletFactory2D.convert_*_to_pickups.
// The converted bullets leave their volleys (silently, or with their clear
// effects) and one new pickup volley per call is spawned; collecting a
// pickup emits BulletFactory2D.pickup_collected. Every accepted change emits
// `changed` and drops the cached spawn data.
class BulletPickupData2D : public Resource {
	GDCLASS(BulletPickupData2D, Resource)

public:
	// Look
	Ref<Texture2D> texture;
	Ref<SpriteFrames> sprite_frames;
	Vector2 texture_size = Vector2(0, 0); // (0, 0): the texture's own size
	double scale_multiplier = 1.0;
	Color tint = Color(1, 1, 1, 1); // alpha = opacity
	double fade_in_sec = 0.0;
	double fade_out_sec = 0.5;
	double lifetime_sec = 12.0; // 0 = never expires
	// Burst out of the bullet's motion
	double initial_velocity_scale = 0.4;
	double scatter_speed_min = 0.0;
	double scatter_speed_max = 0.0;
	double drag = 4.0;
	// Pulled to the target
	double magnet_delay_sec = 0.35;
	double magnet_acceleration = 1200.0;
	double magnet_max_speed = 900.0;
	double magnet_smoothing = 0.0;
	// Collection
	double collect_radius = 24.0;
	Variant value;
	Ref<BulletSoundData2D> collect_sound;
	Ref<BulletEffectLayerData2D> collect_effect;
	// Optional physics (0/0: pure distance collection)
	int collision_layer = 0;
	int collision_mask = 0;
	double collision_radius = 8.0;
	// Conversion
	bool inherit_custom_data = false;
	bool fire_clear_effects = false;

	// The spawn data of the pickup volley (cached until a setter runs).
	Ref<BulletVolleyData2D> get_volley_data();

#define BLAST_PICKUP_ACCESSORS(TYPE, NAME)   \
	TYPE get_##NAME() const { return NAME; } \
	void set_##NAME(TYPE value);
	BLAST_PICKUP_ACCESSORS(Ref<Texture2D>, texture)
	BLAST_PICKUP_ACCESSORS(Ref<SpriteFrames>, sprite_frames)
	BLAST_PICKUP_ACCESSORS(Vector2, texture_size)
	BLAST_PICKUP_ACCESSORS(double, scale_multiplier)
	BLAST_PICKUP_ACCESSORS(Color, tint)
	BLAST_PICKUP_ACCESSORS(double, fade_in_sec)
	BLAST_PICKUP_ACCESSORS(double, fade_out_sec)
	BLAST_PICKUP_ACCESSORS(double, lifetime_sec)
	BLAST_PICKUP_ACCESSORS(double, initial_velocity_scale)
	BLAST_PICKUP_ACCESSORS(double, scatter_speed_min)
	BLAST_PICKUP_ACCESSORS(double, scatter_speed_max)
	BLAST_PICKUP_ACCESSORS(double, drag)
	BLAST_PICKUP_ACCESSORS(double, magnet_delay_sec)
	BLAST_PICKUP_ACCESSORS(double, magnet_acceleration)
	BLAST_PICKUP_ACCESSORS(double, magnet_max_speed)
	BLAST_PICKUP_ACCESSORS(double, magnet_smoothing)
	BLAST_PICKUP_ACCESSORS(double, collect_radius)
	BLAST_PICKUP_ACCESSORS(Variant, value)
	BLAST_PICKUP_ACCESSORS(Ref<BulletSoundData2D>, collect_sound)
	BLAST_PICKUP_ACCESSORS(Ref<BulletEffectLayerData2D>, collect_effect)
	BLAST_PICKUP_ACCESSORS(int, collision_layer)
	BLAST_PICKUP_ACCESSORS(int, collision_mask)
	BLAST_PICKUP_ACCESSORS(double, collision_radius)
	BLAST_PICKUP_ACCESSORS(bool, inherit_custom_data)
	BLAST_PICKUP_ACCESSORS(bool, fire_clear_effects)
#undef BLAST_PICKUP_ACCESSORS

protected:
	static void _bind_methods();

private:
	Ref<BulletVolleyData2D> cached_volley_data;
	void accepted_change();
};

} // namespace BlastBullets2D
