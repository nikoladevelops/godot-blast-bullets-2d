#pragma once

#include "core/variant_enum_cast2d.hpp"
#include "data/sound_mix2d.hpp"
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_player2d.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace BlastBullets2D {
using namespace godot;

// One stackable sound entry for a bullet volley. A spawner carries an array
// of these (BulletSpawner2D.sound_effects); each entry plays one list entry when
// its trigger fires, subject to its own limits. A factory volley carries its
// own entries via BulletVolley2D.sound_set_effects.
//
// Channels are shared per resource across the whole factory: 20 enemies
// sharing hit.tres share one min_interval_sec / max_voices budget, nearest
// events win each sweep. See BulletFactory2D (SoundMixer2D) for the mixing
// contract; this class only says HOW one sound plays.
class BulletSoundData2D : public Resource, public SoundMix2D {
	GDCLASS(BulletSoundData2D, Resource)

public:
	// Bumped whenever an accepted edit changes `enabled` or `trigger`: armed
	// volleys compare it once per tick/event and recompute their trigger mask,
	// so those edits reach volleys already in flight.
	static uint64_t mask_epoch;

	// SERIALIZED ids (BulletSoundData2D.trigger): never renumber.
	enum SoundTrigger : int {
		SOUND_ON_SHOT = 0,
		SOUND_ON_HIT = 1,
		SOUND_ON_DESTROY = 2,
		SOUND_ON_BOUNCE = 3,
		SOUND_ON_LIFETIME_OVER = 4,
		SOUND_ON_CLEAR = 5,
		SOUND_ON_HOMING_TARGET_REACHED = 6,
		SOUND_ON_GRAZE = 7,
		SOUND_ON_GRAZE_EXIT = 8,
		SOUND_ON_TELEGRAPH = 9,
		SOUND_ON_FLIGHT = 10,
		SOUND_ON_DESPAWN = 11,
		SOUND_ON_SPLIT = 12
	};

	// Which event plays this sound (see SoundTrigger). On Shot and On
	// Telegraph are spawner-only: a factory volley never fires them.
	int trigger = SOUND_ON_SHOT;
	// Which graze ring this entry listens to (-1 = any ring, else 0..3). A
	// zone with fewer rings simply never matches (silent, no warning:
	// zones legitimately vary). Ignored by every non-graze trigger.
	int ring_index = -1;
	// Which graze zone slot this entry listens to (-1 = every armed zone,
	// else 0..3 in graze_set_zones order). A slot with no armed zone never
	// matches (silent). Ignored by every non-graze trigger.
	int zone_index = -1;

#include "data/sound_mix_declarations2d.inc"

	SoundTrigger get_trigger() const;
	void set_trigger(SoundTrigger value);
	int get_ring_index() const;
	void set_ring_index(int value);
	int get_zone_index() const;
	void set_zone_index(int value);
	int get_min_volley_amount() const;
	void set_min_volley_amount(int value);
	int get_max_volley_amount() const;
	void set_max_volley_amount(int value);
	double get_amount_gain_db() const;
	void set_amount_gain_db(double value);
	bool get_follow_bullet() const;
	void set_follow_bullet(bool value);

	// Spatial knobs hide while positional is off; the bus enum lists the
	// project's audio buses (like Godot's AudioStreamPlayer).
	void _validate_property(PropertyInfo &p_property) const;

protected:
	static void _bind_methods();
	// The hooks of sound_mix_binds2d.inc: the triggers after `enabled`, the
	// volley-size gates inside Limits.
	static void bind_own_trigger_properties();
	static void bind_own_limit_properties();
};
} //namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSoundData2D::SoundTrigger);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSoundData2D::SoundLimitMode);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSoundData2D::StealMode);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSoundData2D::StreamMode);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSoundData2D::AudibilityMode);
