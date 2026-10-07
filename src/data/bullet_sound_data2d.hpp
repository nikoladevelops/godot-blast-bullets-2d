#pragma once

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace BlastBullets2D {
using namespace godot;

// One stackable sound entry for a bullet volley. A spawner carries an array
// of these (BulletSpawner2D.sound_effects); each entry plays its stream when
// its trigger fires, subject to its own limits. A factory volley carries its
// own entries via BulletVolley2D.sound_set_effects.
//
// Channels are shared per resource across the whole factory: 20 enemies
// sharing hit.tres share one min_interval_sec / max_voices budget, nearest
// events win each sweep. See BulletFactory2D (SoundMixer2D) for the mixing
// contract; this class only says HOW one sound plays.
class BulletSoundData2D : public Resource {
	GDCLASS(BulletSoundData2D, Resource)

public:
	// SERIALIZED ids (BulletSoundData2D.trigger): never renumber.
	enum SoundTrigger {
		SOUND_ON_SHOT = 0,
		SOUND_ON_HIT = 1,
		SOUND_ON_DESTROY = 2,
		SOUND_ON_BOUNCE = 3,
		SOUND_ON_LIFETIME_OVER = 4,
		SOUND_ON_CLEAR = 5,
		SOUND_ON_HOMING_TARGET_REACHED = 6,
		SOUND_ON_GRAZE = 7,
		SOUND_ON_GRAZE_EXIT = 8,
		SOUND_ON_TELEGRAPH = 9
	};
	// SERIALIZED ids (BulletSoundData2D.when_limit_reached): never renumber.
	enum SoundLimitMode {
		SOUND_LIMIT_REPLACE_OLDEST = 0,
		SOUND_LIMIT_SKIP_NEW = 1
	};

	// Master switch: lets users toggle a sound without removing it.
	bool enabled = true;
	// Which event plays this sound (see SoundTrigger). On Shot and On
	// Telegraph are spawner-only: a factory volley never fires them.
	int trigger = SOUND_ON_SHOT;
	// What plays. Null (default) plays nothing (warns once). Any Godot
	// AudioStream works: WAV/OGG/MP3, AudioStreamRandomizer (variations,
	// weighted, no repeats), AudioStreamPlaylist (sequences),
	// AudioStreamSynchronized (layers).
	Ref<AudioStream> stream;

	// Base loudness in dB (the spawner's sound_volume_db is added on top,
	// plus the random offset below when set).
	double volume_db = 0.0;
	// Base pitch multiplier (multiplied by the random pitch below when set).
	double pitch_scale = 1.0;
	// Random pitch range (AudioStreamRandomizer math): each play multiplies
	// pitch_scale by exp(lerp(log(1/r), log(r), randf)) with r = random_pitch.
	// 1 (default) disables it.
	double random_pitch = 1.0;
	// Random loudness range in dB: each play adds a uniform offset in
	// [-v, +v]. 0 (default) disables it.
	double random_volume_offset_db = 0.0;
	// Play probability per event. 0 never plays, 1 always plays.
	double trigger_chance = 1.0;
	// Mixer bus (reverb/delay/distortion live on buses as AudioEffects).
	// Unknown names play on Master with one warning per name.
	StringName bus = "Master";

	// False = constant volume, centered (a UI sting); true = Godot's own
	// distance falloff + panning from the listener.
	bool positional = true;
	// Past this distance from the listener the play is dropped (inaudible:
	// never takes a voice).
	double max_distance = 2000.0;
	// Falloff curve exponent (Godot's volume math).
	double attenuation = 1.0;
	// Stereo spread (Godot's panning math).
	double panning_strength = 1.0;
	// Area2D audio bus overrides listen on these physics layers.
	int area_mask = 1;

	// At most one play per interval across the whole factory (shared with
	// every spawner using this resource); the nearest event wins each sweep.
	// 0 plays up to max_voices per sweep instead.
	double min_interval_sec = 0.05;
	// Simultaneous voices of this sound across the whole factory.
	int max_voices = 4;
	// What happens past max_voices (see SoundLimitMode).
	int when_limit_reached = SOUND_LIMIT_REPLACE_OLDEST;
	// Voice-stealing rank for the factory-wide pool (-16..16).
	int priority = 0;
	// Stops a voice after this long (looping streams). 0 = whole stream.
	double max_duration_sec = 0.0;

	bool get_enabled() const;
	void set_enabled(bool value);

	int get_trigger() const;
	void set_trigger(int value);

	Ref<AudioStream> get_stream() const;
	void set_stream(const Ref<AudioStream> &new_stream);

	double get_volume_db() const;
	void set_volume_db(double value);

	double get_pitch_scale() const;
	void set_pitch_scale(double value);

	double get_random_pitch() const;
	void set_random_pitch(double value);

	double get_random_volume_offset_db() const;
	void set_random_volume_offset_db(double value);

	double get_trigger_chance() const;
	void set_trigger_chance(double value);

	StringName get_bus() const;
	void set_bus(const StringName &value);

	bool get_positional() const;
	void set_positional(bool value);

	double get_max_distance() const;
	void set_max_distance(double value);

	double get_attenuation() const;
	void set_attenuation(double value);

	double get_panning_strength() const;
	void set_panning_strength(double value);

	int get_area_mask() const;
	void set_area_mask(int value);

	double get_min_interval_sec() const;
	void set_min_interval_sec(double value);

	int get_max_voices() const;
	void set_max_voices(int value);

	int get_when_limit_reached() const;
	void set_when_limit_reached(int value);

	int get_priority() const;
	void set_priority(int value);

	double get_max_duration_sec() const;
	void set_max_duration_sec(double value);

	// Spatial knobs hide while positional is off; the bus enum lists the
	// project's audio buses (like Godot's AudioStreamPlayer).
	void _validate_property(PropertyInfo &p_property) const;

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::BulletSoundData2D::SoundTrigger);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSoundData2D::SoundLimitMode);
