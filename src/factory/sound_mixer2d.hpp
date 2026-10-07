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
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

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
	// chance). True = queued for this sweep's flush (or the next sweep's when
	// offered outside a sweep).
	bool offer(BulletFactory2D &factory, const Ref<BulletSoundData2D> &sound, const Vector2 &event_pos, float volume_offset_db, const SoundListenerSpec2D &listener);
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
	Array debug_voices() const;
	Dictionary debug_stats() const;

private:
	struct Candidate {
		Vector2 event_pos;
		float volume_offset_db = 0.0f;
		SoundListenerSpec2D listener;
		uint64_t order = 0;
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
	};
	struct Voice {
		AudioStreamPlayer2D *player = nullptr;
		bool busy = false;
		uint64_t sound_id = 0;
		int priority = 0;
		double start_clock = 0.0;
		double max_duration = 0.0;
		// Follow state for node listeners: the event P is fixed, L is tracked
		// by id and the voice sits at G + (P - L) every sweep. A dead L
		// freezes the voice where it is. Point listeners never follow.
		Vector2 event_pos;
		uint64_t listener_node_id = 0;
		Vector2 listener_last_pos;
		bool follows_node = false;
		uint64_t use_order = 0;
	};

	Channel &channel_for(uint64_t sound_id, size_t cap);
	// Nearest target of the candidate's spec (fresh list read): returns false
	// when a node source finds nobody (caller falls back to Godot's listener).
	bool resolve_listener(BulletFactory2D &factory, const SoundListenerSpec2D &spec, const Vector2 &event_pos, Vector2 &r_listener_pos, uint64_t &r_node_id, bool &r_is_point) const;
	static Vector2 godot_listener_pos(BulletFactory2D &factory);
	AudioStreamPlayer2D *alloc_voice(BulletFactory2D &factory, int priority);
	void release_voice(Voice &voice);
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
	Array debug_scratch;
};
} // namespace BlastBullets2D
