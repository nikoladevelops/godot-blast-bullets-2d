#pragma once

#include "core/variant_enum_cast2d.hpp"
#include "data/bullet_effect_layer_data2d.hpp"
#include <godot_cpp/classes/resource.hpp>

namespace BlastBullets2D {
using namespace godot;

// One one-shot effect a laser plays on one of its events
// (LaserData2D.effects): a spark where it hits, a flash at the muzzle when it
// fires, dust where a wall stops it. The effect is a one-shot
// BulletEffectLayerData2D played through the factory's effect engine (the
// same one spawn_layer_effect uses), posed at the event's point and turned
// with the beam there. Every accepted change emits `changed`.
class LaserEffectData2D : public Resource {
	GDCLASS(LaserEffectData2D, Resource)

public:
	// SERIALIZED ids, the same as LaserSoundData2D's laser triggers (On Shot
	// and On Telegraph belong to the spawner: no laser plays them).
	enum LaserEffectTrigger : int {
		LASER_EFFECT_ON_CHARGE = 2, // per beam, at its muzzle, when the laser starts charging
		LASER_EFFECT_ON_FIRE = 3, // per beam, at its muzzle, when the beams start
		LASER_EFFECT_ON_ACTIVE = 4, // every hitting tick, per beam
		LASER_EFFECT_ON_HIT = 5, // every laser_hit, at the contact
		LASER_EFFECT_ON_PIERCE_START = 6,
		LASER_EFFECT_ON_PIERCE_END = 7,
		LASER_EFFECT_ON_WALL_HIT = 8,
		LASER_EFFECT_ON_GRAZE = 9,
		LASER_EFFECT_ON_GRAZE_EXIT = 10,
		LASER_EFFECT_ON_FINISH = 11, // per beam, at its tip
		LASER_EFFECT_ON_ENDED_EARLY = 12, // per beam, at its tip
		LASER_EFFECT_ON_DESPAWN = 13, // where a bolt or curvy head left the bounds
	};
	// SERIALIZED ids.
	enum EffectPoint : int {
		AT_EVENT = 0, // where the event happened (the contact, the wall point, ...)
		AT_MUZZLE = 1, // the muzzle of the event's beam
		AT_TIP = 2, // the end of the event's beam (the head of a bolt)
	};

	int trigger = LASER_EFFECT_ON_HIT;
	int at = AT_EVENT;
	Ref<BulletEffectLayerData2D> effect;
	bool align_to_beam = true; // the effect turns with the beam at its point
	bool enabled = true;

	LaserEffectTrigger get_trigger() const { return (LaserEffectTrigger)trigger; }
	void set_trigger(LaserEffectTrigger value);
	EffectPoint get_at() const { return (EffectPoint)at; }
	void set_at(EffectPoint value);
	Ref<BulletEffectLayerData2D> get_effect() const { return effect; }
	void set_effect(const Ref<BulletEffectLayerData2D> &value);
	bool get_align_to_beam() const { return align_to_beam; }
	void set_align_to_beam(bool value);
	bool get_enabled() const { return enabled; }
	void set_enabled(bool value);

protected:
	static void _bind_methods();
};

} // namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserEffectData2D::LaserEffectTrigger);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserEffectData2D::EffectPoint);
