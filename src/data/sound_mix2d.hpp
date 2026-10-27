#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace BlastBullets2D {
using namespace godot;

// HOW one sound entry plays: what it plays (streams, stream mode, weights),
// its mix (volume, pitch, randoms, pan, chance, bus), where it is heard
// (positional, distance, camera, zoom, occlusion), its limits (interval,
// voices, priority, duration, clamps), fades and ducking, and whether a voice
// follows its source. ONE part with ONE set of accessors
// (sound_mix_accessors2d.inc) carried by every sound resource:
// BulletSoundData2D (bullet triggers) and LaserSoundData2D (laser triggers).
// The mixer (factory/sound_mixer2d) reads only this part. Plain data: no
// Godot class, no base resource.
struct SoundMix2D {
	// SERIALIZED ids (BulletSoundData2D.when_limit_reached): never renumber.
	enum SoundLimitMode : int {
		SOUND_LIMIT_REPLACE_OLDEST = 0,
		SOUND_LIMIT_SKIP_NEW = 1
	};
	// SERIALIZED ids (BulletSoundData2D.steal_mode): never renumber.
	enum StealMode : int {
		STEAL_OLDEST = 0,
		STEAL_QUIETEST = 1
	};
	// SERIALIZED ids (BulletSoundData2D.stream_mode): never renumber.
	enum StreamMode : int {
		STREAM_RANDOM = 0,
		STREAM_WEIGHTED = 1,
		STREAM_SEQUENCE = 2,
		STREAM_SHUFFLE = 3
	};
	// SERIALIZED ids (BulletSoundData2D.audibility_mode): never renumber.
	enum AudibilityMode : int {
		SOUND_AUDIBILITY_DISTANCE = 0,
		SOUND_AUDIBILITY_CAMERA = 1,
		SOUND_AUDIBILITY_DISTANCE_AND_CAMERA = 2
	};

	// Master switch: lets users toggle a sound without removing it.
	bool enabled = true;
	// What plays: each play picks one entry per stream_mode (nulls skipped).
	// Empty or all-null plays nothing (warns once). Any Godot AudioStream
	// works: WAV/OGG/MP3, AudioStreamRandomizer (variations, weighted, no
	// repeats), AudioStreamPlaylist (sequences), AudioStreamSynchronized
	// (layers). Five hit variations without building an AudioStreamRandomizer.
	Array streams;

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
	// Loudness added per zoom octave: k * log2 of the camera's minimum zoom,
	// so k = 3 adds +3 dB at zoom 2 and -3 dB at zoom 0.5 (zoom 1 adds
	// nothing). Negative inverts it. 0 (default) disables it. Applied with
	// the trim and the other gains, before the volume clamp.
	double zoom_gain_db = 0.0;
	// Occlusion: walls between the listener and the event muffle the play.
	// Each winning play raycasts listener to event against these physics
	// layers (bodies only); any hit dips the volume by occlusion_db before
	// the clamp. 0 (default) never raycasts. Positional plays only; keep the
	// mask disjoint from characters (a listener inside a body starts the ray
	// inside it). Editor preview skips occlusion.
	int occlusion_mask = 0;
	double occlusion_db = 0.0;
	// Muffle bus for occluded plays: when the ray hits, the voice routes here
	// instead of bus (put reverb/lowpass on it as Godot effects). Empty
	// (default) keeps bus. Unknown names reuse the Master fallback.
	StringName occlusion_bus;

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
	// Pitch follows the time scale of what the sound belongs to (opt-in).
	bool pitch_follows_time_scale = false;
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
	// Follow the bullet: the voice is re-posed to its bullet's live pose
	// every sweep instead of freezing where it started (a hum riding its
	// bullet). Meant for On Flight; harmless elsewhere. When the bullet is
	// gone (cleared, expired, pooled into a new life) the voice ends through
	// the fade path. False (default) freezes at the fire pose, like before.
	bool follow = false;

	// The resource that carries this mix: the mixer keys its channels by it,
	// keeps it alive while it plays and names it in every warning.
	virtual Resource *sound_resource() = 0;
	virtual const Resource *sound_resource() const = 0;
	uint64_t sound_id() const { return sound_resource()->get_instance_id(); }
	// The resource's trigger id (its own trigger enum: bullet or laser).
	virtual int sound_trigger() const = 0;
	String sound_class() const { return sound_resource()->get_class(); }

	virtual ~SoundMix2D() = default;
};

// The mix a live object carries (a sound resource), or null.
inline SoundMix2D *sound_mix_of2d(Object *object) {
	return dynamic_cast<SoundMix2D *>(object);
}

// The inspector gating of the mix settings (shared by every sound resource's
// _validate_property): spatial knobs hide while positional is off, camera
// knobs outside the camera modes, occlusion knobs without a mask, weights
// outside Weighted; the bus enum lists the project's audio buses.
void validate_sound_mix_property2d(const SoundMix2D &mix, PropertyInfo &p_property);

} // namespace BlastBullets2D
