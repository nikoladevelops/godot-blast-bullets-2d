#pragma once

#include "core/variant_enum_cast2d.hpp"
// The shared mix enums are typed for scripts there (BulletSoundData2D.StreamMode, ...).
#include "data/bullet_sound_data2d.hpp"
#include "data/sound_mix2d.hpp"
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_player2d.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/property_info.hpp>

namespace BlastBullets2D {
using namespace godot;

// One stackable sound entry for a laser. A LaserSpawner2D carries an array of
// these (sound_effects); a factory laser gets its own through
// LaserVolley2D.sound_set_effects. HOW it plays is the shared SoundMix2D part
// (the same streams, mix, spatial, limit, fade and duck settings as a bullet
// sound, mixed by the same factory mixer: channels per resource, nearest
// wins); WHEN it plays is a laser event (LaserSoundTrigger).
class LaserSoundData2D : public Resource, public SoundMix2D {
	GDCLASS(LaserSoundData2D, Resource)

public:
	// Bumped whenever an accepted edit changes `enabled` or `trigger`.
	static uint64_t mask_epoch;

	// SERIALIZED ids (LaserSoundData2D.trigger): never renumber.
	enum LaserSoundTrigger : int {
		LASER_SOUND_ON_SHOT = 0, // spawner: a shot fired (at the spawner)
		LASER_SOUND_ON_TELEGRAPH = 1, // spawner: a telegraph began
		LASER_SOUND_ON_CHARGE = 2, // the laser begins charging (charge_sec > 0), at its first muzzle
		LASER_SOUND_ON_FIRE = 3, // the beams start (the Growing phase), at the first muzzle
		LASER_SOUND_ON_ACTIVE = 4, // every hitting tick, per beam (a hum: follow_beam keeps one voice per beam)
		LASER_SOUND_ON_HIT = 5, // every laser_hit (repeats included), at the contact
		LASER_SOUND_ON_PIERCE_START = 6, // laser_pierce_started, at the contact
		LASER_SOUND_ON_PIERCE_END = 7, // laser_pierce_ended, at the contact
		LASER_SOUND_ON_WALL_HIT = 8, // laser_wall_hit (once per block), at the wall point
		LASER_SOUND_ON_GRAZE = 9,
		LASER_SOUND_ON_GRAZE_EXIT = 10,
		LASER_SOUND_ON_FINISH = 11, // the laser ran its phases out (or every bolt was spent)
		LASER_SOUND_ON_ENDED_EARLY = 12, // end_laser cut it short (a script, a clear, a bomb, an orphan policy)
		LASER_SOUND_ON_DESPAWN = 13, // a bolt or curvy head left the despawn bounds
	};
	// SERIALIZED ids (LaserSoundData2D.follow_point): never renumber.
	enum FollowPoint : int {
		FOLLOW_TIP = 0,
		FOLLOW_MUZZLE = 1,
	};

	// Which laser event plays this sound (see LaserSoundTrigger).
	int trigger = LASER_SOUND_ON_HIT;
	// Which graze ring / zone slot the graze triggers listen to (-1 = any).
	int ring_index = -1;
	int zone_index = -1;
	// Where a beam's sound plays and, with follow_beam, which point its voice
	// rides: the tip (the head of a bolt) or the muzzle.
	int follow_point = FOLLOW_TIP;

#include "data/sound_mix_declarations2d.inc"

	LaserSoundTrigger get_trigger() const;
	void set_trigger(LaserSoundTrigger value);
	int get_ring_index() const;
	void set_ring_index(int value);
	int get_zone_index() const;
	void set_zone_index(int value);
	bool get_follow_beam() const;
	void set_follow_beam(bool value);
	FollowPoint get_follow_point() const;
	void set_follow_point(FollowPoint value);

	// Graze filters show only for the graze triggers; the mix gating is shared.
	void _validate_property(PropertyInfo &p_property) const;

protected:
	static void _bind_methods();
	static void bind_own_trigger_properties();
};

} // namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserSoundData2D::LaserSoundTrigger);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserSoundData2D::FollowPoint);
