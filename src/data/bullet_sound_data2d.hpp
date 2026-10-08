#pragma once

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_player2d.hpp>
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
		SOUND_ON_TELEGRAPH = 9,
		SOUND_ON_FLIGHT = 10
	};
	// SERIALIZED ids (BulletSoundData2D.when_limit_reached): never renumber.
	enum SoundLimitMode {
		SOUND_LIMIT_REPLACE_OLDEST = 0,
		SOUND_LIMIT_SKIP_NEW = 1
	};
	// SERIALIZED ids (BulletSoundData2D.steal_mode): never renumber.
	enum StealMode {
		STEAL_OLDEST = 0,
		STEAL_QUIETEST = 1
	};
	// SERIALIZED ids (BulletSoundData2D.stream_mode): never renumber.
	enum StreamMode {
		STREAM_RANDOM = 0,
		STREAM_WEIGHTED = 1,
		STREAM_SEQUENCE = 2,
		STREAM_SHUFFLE = 3
	};
	// SERIALIZED ids (BulletSoundData2D.audibility_mode): never renumber.
	enum AudibilityMode {
		SOUND_AUDIBILITY_DISTANCE = 0,
		SOUND_AUDIBILITY_CAMERA = 1,
		SOUND_AUDIBILITY_DISTANCE_AND_CAMERA = 2
	};

	// Master switch: lets users toggle a sound without removing it.
	bool enabled = true;
	// Which event plays this sound (see SoundTrigger). On Shot and On
	// Telegraph are spawner-only: a factory volley never fires them.
	int trigger = SOUND_ON_SHOT;
	// What plays: each play picks one entry per stream_mode (nulls skipped).
	// Empty or all-null plays nothing (warns once). Any Godot AudioStream
	// works: WAV/OGG/MP3, AudioStreamRandomizer (variations, weighted, no
	// repeats), AudioStreamPlaylist (sequences), AudioStreamSynchronized
	// (layers). Five hit variations without building an AudioStreamRandomizer.
	Array streams;

	// Which graze ring this entry listens to (-1 = any ring, else 0..3). A
	// zone with fewer rings simply never matches (silent, no warning:
	// zones legitimately vary). Ignored by every non-graze trigger.
	int ring_index = -1;
	// How streams is picked (see StreamMode). Weighted reads
	// stream_weights; Sequence round-robins and Shuffle deals every entry
	// once per cycle (both cursors live in the shared channel, like the
	// limits: every spawner using this resource shares the order).
	int stream_mode = STREAM_RANDOM;
	// Per-entry weight for Weighted mode, parallel to streams (all-zero,
	// empty or size-mismatched = uniform). Swapping the array mid-fight
	// works: the next play re-reads it.
	PackedFloat32Array stream_weights;

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
	// Asymmetric random loudness bounds in dB: each play adds a uniform
	// offset in [min, max] (sorted when inverted, so no setter depends on the
	// other). 0/0 (default) disables it; min == max adds without burning RNG.
	double random_volume_min_db = 0.0;
	double random_volume_max_db = 0.0;
	// Asymmetric random pitch bounds (linear multipliers): each play
	// multiplies by a uniform roll in [min, max] (sorted when inverted).
	// 1/1 (default) disables it; min == max multiplies without burning RNG.
	double random_pitch_min = 1.0;
	double random_pitch_max = 1.0;
	// Random placement spread (fraction of the listener distance): each
	// positional play is offset perpendicular to the listener direction by a
	// uniform roll in [-v, +v] times the distance, so repeated shots spread
	// across the stereo field without changing their distance much. 0
	// (default) disables it; centered voices stay centered (no-op).
	double random_pan = 0.0;
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
	// Which audibility rule drops events (see AudibilityMode): distance only
	// (today's max_distance), camera view only, or both.
	int audibility_mode = SOUND_AUDIBILITY_DISTANCE;
	// The camera the view test and the zoom scaling measure from. Empty
	// (default) follows the viewport's current camera; a set path is resolved
	// against the factory (absolute paths work from anywhere) and must point
	// at a Camera2D in the same viewport, else the viewport camera is used
	// with one warning. Hidden unless the audibility mode includes the camera.
	NodePath camera_path;
	// The camera view rect grows by this many pixels on every side before an
	// event counts as outside.
	double camera_margin_px = 200.0;
	// When true, the effective max_distance is divided by the camera's
	// minimum zoom (zoomed in hears less, pulled back hears more).
	bool zoom_scales_distance = false;

	// At most one play per interval across the whole factory (shared with
	// every spawner using this resource); the nearest event wins each sweep.
	// 0 plays up to max_voices per sweep instead.
	double min_interval_sec = 0.05;
	// Simultaneous voices of this sound across the whole factory.
	int max_voices = 4;
	// What happens past max_voices (see SoundLimitMode).
	int when_limit_reached = SOUND_LIMIT_REPLACE_OLDEST;
	// Which voice Replace Oldest stops (see StealMode): the oldest, or the
	// quietest (lowest played volume, ties go to the oldest).
	int steal_mode = STEAL_OLDEST;
	// Voice-stealing rank for the factory-wide pool (-16..16).
	int priority = 0;
	// Stops a voice after this long (looping streams). 0 = whole stream.
	double max_duration_sec = 0.0;
	// Offers only from volleys with at least this many bullets (0 = off).
	int min_volley_amount = 0;
	// Offers only from volleys with at most this many bullets (0 = no cap).
	int max_volley_amount = 0;
	// Loudness added per volley size: k * log10(max(1, amount)), so k = 2
	// adds +4 dB at 100 bullets and +8 dB at 10k. 0 = off. Manual hatches
	// count as a single bullet.
	double amount_gain_db = 0.0;
	// Final mixed volume clamp in dB (applied last, after the trim, the
	// randoms and the amount gain; inverted bounds sort themselves). Wide
	// open by default, so existing mixes play untouched.
	double volume_min_db = -80.0;
	double volume_max_db = 80.0;
	// Final pitch clamp (linear multipliers, applied last; inverted bounds
	// sort themselves). Wide open by default.
	double pitch_min = 0.01;
	double pitch_max = 16.0;
	// Fade-out before a voice stops (duration expiry, voice stealing).
	// Ramps the volume to silence over this long instead of cutting, so
	// loopers never click. 0 (default) stops instantly, like before.
	// stop_sounds(), reset() and pause stay instant either way.
	double fade_out_sec = 0.0;
	// Ducking: while a voice of this entry is busy, every busy voice of
	// strictly lower priority is dipped by this many dB (the strongest such
	// dip wins per victim; ties never dip). Reuses priority, so the steal
	// rank and the duck rank are one rank. 0 (default) never dips. Fading
	// voices sit out both ways.
	double duck_amount_db = 0.0;

	bool get_enabled() const;
	void set_enabled(bool value);

	int get_trigger() const;
	void set_trigger(int value);

	int get_ring_index() const;
	void set_ring_index(int value);

	Array get_streams() const;
	void set_streams(const Array &value);

	int get_stream_mode() const;
	void set_stream_mode(int value);

	PackedFloat32Array get_stream_weights() const;
	void set_stream_weights(const PackedFloat32Array &value);

	double get_volume_db() const;
	void set_volume_db(double value);

	double get_pitch_scale() const;
	void set_pitch_scale(double value);

	double get_random_pitch() const;
	void set_random_pitch(double value);

	double get_random_volume_offset_db() const;
	void set_random_volume_offset_db(double value);

	double get_random_volume_min_db() const;
	void set_random_volume_min_db(double value);

	double get_random_volume_max_db() const;
	void set_random_volume_max_db(double value);

	double get_random_pitch_min() const;
	void set_random_pitch_min(double value);

	double get_random_pitch_max() const;
	void set_random_pitch_max(double value);

	double get_random_pan() const;
	void set_random_pan(double value);

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

	int get_audibility_mode() const;
	void set_audibility_mode(int value);

	NodePath get_camera_path() const;
	void set_camera_path(const NodePath &value);

	double get_camera_margin_px() const;
	void set_camera_margin_px(double value);

	bool get_zoom_scales_distance() const;
	void set_zoom_scales_distance(bool value);

	double get_min_interval_sec() const;
	void set_min_interval_sec(double value);

	int get_max_voices() const;
	void set_max_voices(int value);

	int get_when_limit_reached() const;
	void set_when_limit_reached(int value);

	int get_steal_mode() const;
	void set_steal_mode(int value);

	int get_priority() const;
	void set_priority(int value);

	double get_max_duration_sec() const;
	void set_max_duration_sec(double value);

	int get_min_volley_amount() const;
	void set_min_volley_amount(int value);

	int get_max_volley_amount() const;
	void set_max_volley_amount(int value);

	double get_amount_gain_db() const;
	void set_amount_gain_db(double value);

	double get_volume_min_db() const;
	void set_volume_min_db(double value);

	double get_volume_max_db() const;
	void set_volume_max_db(double value);

	double get_pitch_min() const;
	void set_pitch_min(double value);

	double get_pitch_max() const;
	void set_pitch_max(double value);

	double get_fade_out_sec() const;
	void set_fade_out_sec(double value);

	double get_duck_amount_db() const;
	void set_duck_amount_db(double value);

	// Editor mix preview: plays the picked stream with this entry's mix
	// applied (volume, one pitch roll, bus, centered), so pitch/volume edits
	// are audible without running the game. Editor only (else an error);
	// a second call stops the first, one-shots free themselves on finished,
	// looping entries are stopped after 10 s. Never saved anywhere.
	void preview();
	// Same, at a global pose with a volume offset and positioning honored
	// (the spawner's preview path). Editor only like preview(), but also
	// allowed at runtime (harmless: same as a play).
	void preview_at(const Vector2 &global_position, float volume_offset_db, bool use_position);
	// This entry's live preview player, or null (tests and the smoke probe
	// read mix state through it).
	AudioStreamPlayer2D *debug_get_preview_player() const;

	// Spatial knobs hide while positional is off; the bus enum lists the
	// project's audio buses (like Godot's AudioStreamPlayer).
	void _validate_property(PropertyInfo &p_property) const;

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::BulletSoundData2D::SoundTrigger);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSoundData2D::SoundLimitMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSoundData2D::StealMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSoundData2D::StreamMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletSoundData2D::AudibilityMode);
