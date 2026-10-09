#pragma once

// Per-factory sound mixing: channels (one per sound resource, shared by every
// spawner using it), per-sweep candidate selection (bounded, nearest wins) and
// the voice pool (one AudioStreamPlayer2D per simultaneous sound, polyphony 1).
// Offers arrive any time (mid-sweep trigger sites, idle spawner shots, user
// play_sound); winning candidates play at the end of the sweep. Costs nothing
// when unused: trigger sites test one mask bit, flush returns on one branch
// when nothing is pending and no voice is busy.

#include "data/bullet_sound_data2d.hpp"
#include "factory/graze_detector2d.hpp"

#include <godot_cpp/classes/audio_listener2d.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream_player2d.hpp>
#include <godot_cpp/classes/camera2d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class BulletFactory2D;

// WHO the sound is measured from: Godot's own 2D listener, the nearest member
// of a group (BulletFactory2D.play_sound), or the spawner's listener detector
// (shared with its volleys, like graze). Resolved at flush: idle offers use
// next-sweep positions.
struct SoundListenerSpec2D {
	enum Kind {
		GODOT_LISTENER = 0,
		NODE_GROUP = 1,
		DETECTOR = 2,
	};
	Kind kind = GODOT_LISTENER;
	StringName group;
	std::shared_ptr<GrazeDetector2D> detector;
};

class SoundMixer2D {
public:
	// The whole pool (BulletFactory2D.sound_max_voices): voices are created on
	// demand under an internal SoundVoices container (interpolation OFF).
	int max_voices_total = 32;

	// One event offer. False = dropped (paused, editor, disabled, streamless,
	// chance, amount gate). True = queued for this sweep's flush (or the next
	// sweep's when offered outside a sweep). volley_amount is the firing
	// volley's size (1 for manual hatches): the amount gates read it first,
	// amount_gain_db is added to the played volume. The follow triple carries
	// the firing bullet for follow_bullet voices (volley sites only).
	bool offer(BulletFactory2D &factory, const Ref<BulletSoundData2D> &sound, const Vector2 &event_pos, float volume_offset_db, const SoundListenerSpec2D &listener, int volley_amount = 1, uint64_t follow_volley_id = 0, int follow_life_id = -1, int follow_bullet = -1);
	// End of sweep: release finished voices, enforce durations, follow node
	// listeners, pick winners per channel, play them. Returns on one branch
	// when nothing is pending and no voice is busy.
	void flush(BulletFactory2D &factory);
	// Factory pause: drops pending offers, freezes busy voices (stream_paused);
	// resume unpauses them.
	void set_paused(bool paused);
	// Stops every voice (keeps channels). reset() clears channels too.
	void stop_all();
	void clear();

	void set_log_enabled(bool enabled);
	Array get_log() const { return sound_log; }
	void clear_log() { sound_log.clear(); }
	// Diagnosis for one entry at one pose (debug_explain_sound): which gate
	// would drop the play, with the measured numbers. Read-only except
	// reusing the resolve paths (a bad camera_path fires its one warning:
	// diagnosing it is the point). Never queues candidates.
	Dictionary explain_sound(BulletFactory2D &factory, const Ref<BulletSoundData2D> &sound, const Vector2 &event_pos, const StringName &listener_group, int volley_amount);
	Array debug_voices() const;
	Dictionary debug_stats() const;
	// Applies one play's mix to a player (stream, volume + offset + randoms
	// clamped into volume_min/max_db, pitch * randoms clamped into
	// pitch_min/max, bus with Master fallback, spatial or centered
	// constants). Shared by the sweep flush and the editor preview. Returns
	// the applied volume (Quietest steal ranks by it).
	// bus_override routes one play elsewhere (occluded voices): empty keeps
	// the entry bus. Shared with the sweep flush and the editor preview.
	static float apply_playback_mix(AudioStreamPlayer2D *player, const BulletSoundData2D *sound, const Ref<AudioStream> &picked, float volume_offset_db, std::vector<String> *warned_buses, bool force_centered = false, const StringName &bus_override = StringName());
	// Editor preview: plays the entry's picked stream with its mix through a
	// transient player under the tree root (never the edited scene). A second
	// call stops the first, one-shots free themselves on finished, looping
	// entries are stopped after 10 s. Allowed at runtime too (harmless).
	static void preview_play(BulletSoundData2D *sound, const Vector2 &global_position, float volume_offset_db, bool use_position);

private:
	struct Candidate {
		Vector2 event_pos;
		float volume_offset_db = 0.0f;
		SoundListenerSpec2D listener;
		uint64_t order = 0;
		// Firing volley's size (1 for manual hatches): the amount gates and
		// the gain read it at flush.
		int volley_amount = 1;
		// Follow target (volley offer sites only, else 0/-1): the winning
		// voice tracks this bullet while follow_bullet is on.
		uint64_t follow_volley = 0;
		int follow_life = -1;
		int follow_bullet = -1;
		// Offer-time distance to the resolved listener: evicts the farthest
		// while the per-sweep slots fill, so winners are nearest.
		double dist = 0.0;
	};
	struct Channel {
		uint64_t sound_id = 0;
		double last_play_clock = -1e30;
		// Bounded per-sweep slots: 1 when min_interval_sec > 0, else up to
		// max_voices. Reserved once: offers never allocate after warmup.
		std::vector<Candidate> candidates;
		size_t cap = 1;
		// Stream-list pick state (Sequence cursor, Shuffle bag): per
		// resource, shared like the limits.
		size_t seq_cursor = 0;
		std::vector<size_t> shuffle_bag;
		size_t shuffle_n = 0;
		// Pinned-camera cache (camera_path, per factory: factory-relative
		// paths resolve differently per factory sharing this resource).
		NodePath camera_path;
		uint64_t camera_id = 0;
	};
	struct Voice {
		AudioStreamPlayer2D *player = nullptr;
		bool busy = false;
		uint64_t sound_id = 0;
		int priority = 0;
		double start_clock = 0.0;
		double max_duration = 0.0;
		// Fade-out state (fade_out_sec): while fading the voice still counts
		// busy (caps, steal order) and keeps following its listener; the
		// sweep ramps fade_from down to silence, then stops and releases it.
		bool fading = false;
		double fade_start = 0.0;
		float fade_from = 0.0f;
		double fade_len = 0.0;
		// Last applied duck dip in dB (duck_amount_db): the player sits at
		// last_volume - duck_dip while dipped, last_volume when open.
		float duck_dip = 0.0f;
		// Effective played volume (volume + trim + random): Quietest steal
		// ranks by it (ties go to the oldest).
		float last_volume = 0.0f;
		// Follow state for node listeners: the event P is fixed, L is tracked
		// by id and the voice sits at G + (P - L) every sweep. A dead L
		// freezes the voice where it is. Point listeners never follow.
		Vector2 event_pos;
		uint64_t listener_node_id = 0;
		Vector2 listener_last_pos;
		bool follows_node = false;
		// Follow-the-bullet state (follow_bullet): the voice is re-posed to
		// this bullet's live pose every sweep; a cleared/expired/reused
		// bullet ends it through the fade path. following snapshots the
		// entry flag at play.
		bool following = false;
		uint64_t follow_volley = 0;
		int follow_life = -1;
		int follow_bullet = -1;
		uint64_t use_order = 0;
	};

	Channel &channel_for(uint64_t sound_id, size_t cap);
	// The camera one entry's view test and zoom scaling measure from: the
	// pinned camera_path when it resolves to a live Camera2D in the
	// factory's viewport, else the viewport's current camera (null when
	// there is none: no culling, zoom 1). Bad paths warn once per resource.
	Camera2D *resolve_entry_camera(BulletFactory2D &factory, const BulletSoundData2D *sound, Channel &channel) const;
	// Camera view test for one event: false when the event is outside the
	// given camera's view rect (grown by the entry's margin). No camera, or a
	// Distance-only entry, never culls.
	static bool sound_in_camera_view(const BulletSoundData2D *sound, const Vector2 &event_pos, const Camera2D *camera, const Viewport *vp);
	// Effective max_distance for one entry: divided by the given camera's
	// minimum zoom when zoom_scales_distance is set (no camera means zoom 1).
	static double sound_effective_max_distance(const BulletSoundData2D *sound, const Camera2D *camera);
	// The stream this play uses: streams picked per stream_mode (nulls
	// skipped), or null when there is nothing to play.
	// Round-robin and shuffle state live in the channel (shared).
	static Ref<AudioStream> pick_stream(const BulletSoundData2D *sound, Channel &channel);
	// Nearest target of the candidate's spec (fresh list read): returns false
	// when a node source finds nobody (caller falls back to Godot's listener).
	bool resolve_listener(BulletFactory2D &factory, const SoundListenerSpec2D &spec, const Vector2 &event_pos, Vector2 &r_listener_pos, uint64_t &r_node_id, bool &r_is_point) const;
	// Per-sweep offer cache: flight offers arrive once per live bullet, so the
	// Godot listener, the viewport camera and the has-audio answer are read once
	// per sweep. Used only while the factory iterates bullets; a new sweep or a
	// user callback (epoch bump) starts it over, so camera and listener moves
	// made by user code are never missed.
	struct OfferCache {
		bool open = false;
		uint64_t sweep = 0;
		uint64_t epoch = 0;
		bool listener_ready = false;
		Vector2 listener;
		bool camera_ready = false;
		Camera2D *camera = nullptr;
		const BulletSoundData2D *audio_sound = nullptr;
		bool audio = false;
	};
	bool offer_cache_open(BulletFactory2D &factory) const;
	Vector2 offer_godot_listener(BulletFactory2D &factory) const;
	Camera2D *offer_viewport_camera(BulletFactory2D &factory) const;
	bool offer_has_audio(BulletFactory2D &factory, const BulletSoundData2D *sound) const;
	static Vector2 godot_listener_pos(BulletFactory2D &factory);
	AudioStreamPlayer2D *alloc_voice(BulletFactory2D &factory, int priority);
	void release_voice(Voice &voice);
	// Fade-out support (fade_out_sec): the fade length of one sound resource
	// (0 = stop instantly, like before), and starting a fade on a voice
	// (false = nothing to fade: stop instantly instead).
	static double voice_fade_len(uint64_t sound_id);
	static bool voice_begin_fade(Voice &voice, double clock, double fade_len);
	void ensure_voices_container(BulletFactory2D &factory);

	std::unordered_map<uint64_t, Channel> channels;
	std::vector<Voice> voices;
	Node2D *voices_container = nullptr;
	uint64_t offer_order = 0;
	uint64_t plays_total = 0;
	uint64_t dropped_total = 0;
	bool paused = false;
	bool log_enabled = false;
	Array sound_log;
	// Unknown bus names already warned about (project-level, few).
	std::vector<String> warned_buses;
	mutable OfferCache offer_cache;
};
} // namespace BlastBullets2D
