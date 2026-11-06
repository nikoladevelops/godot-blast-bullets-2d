#pragma once

#include "core/variant_enum_cast2d.hpp"
#include <godot_cpp/classes/resource.hpp>

namespace BlastBullets2D {
using namespace godot;

// Emitter bullets: how often a flying bullet fires its template spawner's
// whole pattern (BulletSpawner2D's Emitter Bullets group, or
// BulletVolley2D.emitter_set for factory volleys). The template decides WHAT
// is fired (pattern, spawn data, homing, sound...); this resource decides
// WHEN and in which direction. Volleys read it every tick, so edits reach
// bullets already in flight. Every accepted change emits `changed`.
class BulletEmitterData2D : public Resource {
	GDCLASS(BulletEmitterData2D, Resource)

public:
	// Which way the fired pattern faces. Serialized ids: never renumber.
	enum EmitterAim : int {
		EMITTER_AIM_HEADING = 0, // turned to the bullet's flight heading
		EMITTER_AIM_WORLD = 1, // unrotated (the bullet is a moving muzzle)
		EMITTER_AIM_TEMPLATE = 2 // the template spawner's own rotation
	};

	// Bumped by every accepted edit of ANY emitter resource: an armed
	// volley whose gate is closed re-checks its bullets after an edit.
	static inline uint64_t rules_epoch = 0;

	double interval_sec = 0.5;
	double start_delay_sec = 0.0;
	double stagger_sec = 0.0;
	int max_shots = 0;
	double chance = 1.0;
	int64_t seed = 0;
	EmitterAim aim = EMITTER_AIM_HEADING;
	double spin_per_shot_degrees = 0.0;
	bool inherit_bullet_velocity = false;

	double get_interval_sec() const { return interval_sec; }
	void set_interval_sec(double value);
	double get_start_delay_sec() const { return start_delay_sec; }
	void set_start_delay_sec(double value);
	double get_stagger_sec() const { return stagger_sec; }
	void set_stagger_sec(double value);
	int get_max_shots() const { return max_shots; }
	void set_max_shots(int value);
	double get_chance() const { return chance; }
	void set_chance(double value);
	int64_t get_seed() const { return seed; }
	void set_seed(int64_t value);
	EmitterAim get_aim() const { return aim; }
	void set_aim(EmitterAim value);
	double get_spin_per_shot_degrees() const { return spin_per_shot_degrees; }
	void set_spin_per_shot_degrees(double value);
	bool get_inherit_bullet_velocity() const { return inherit_bullet_velocity; }
	void set_inherit_bullet_velocity(bool value);

protected:
	static void _bind_methods();

private:
	// Shared reject-and-keep for the second counts (finite, >= min, > when strict).
	bool accept_seconds(double value, const char *property_name, bool strictly_positive);
};
} //namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletEmitterData2D::EmitterAim);
