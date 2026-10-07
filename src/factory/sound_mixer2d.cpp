#include "factory/sound_mixer2d.hpp"

#include "bullet_volley/bullet_volley2d.hpp"
#include "core/warn_once2d.hpp"
#include "factory/bullet_factory2d.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>

using namespace godot;
namespace BlastBullets2D {

// Warn-once codes (per resource instance, WarnOnce2D).
static constexpr uint32_t SOUND_WARN_EMPTY_STREAM = 0x50DD01;
static constexpr uint32_t SOUND_WARN_BAD_BUS = 0x50DD02;

static double sound_dist2(const Vector2 &a, const Vector2 &b) {
	const double dx = (double)a.x - (double)b.x;
	const double dy = (double)a.y - (double)b.y;
	return dx * dx + dy * dy;
}

// AudioStreamRandomizer pitch math: pitch_scale * exp(lerp(log(1/r),
// log(r), randf)). Rolled only when r > 1 (no RNG burn otherwise).
static float sound_roll_pitch(double range) {
	if (!(range > 1.0) || !Math::is_finite(range)) {
		return 1.0f;
	}
	const double u = UtilityFunctions::randf();
	return (float)Math::exp(Math::log(range) * (2.0 * u - 1.0));
}

Vector2 SoundMixer2D::godot_listener_pos(BulletFactory2D &factory) {
	Viewport *vp = factory.get_viewport();
	if (vp != nullptr) {
		if (AudioListener2D *listener = vp->get_audio_listener_2d(); listener != nullptr) {
			return listener->get_global_position();
		}
		const Rect2 visible = vp->get_visible_rect();
		const Transform2D canvas = vp->get_global_canvas_transform() * vp->get_canvas_transform();
		if (BulletVolley2D::is_transform_invertible_safe(canvas)) {
			return canvas.affine_inverse().xform(visible.size * 0.5f);
		}
	}
	return factory.get_global_position();
}

SoundMixer2D::Channel &SoundMixer2D::channel_for(uint64_t sound_id, size_t cap) {
	auto it = channels.find(sound_id);
	if (it != channels.end()) {
		if (it->second.cap != cap) {
			it->second.cap = cap;
			if (it->second.candidates.size() > cap) {
				it->second.candidates.resize(cap);
			}
			it->second.candidates.reserve(cap);
		}
		return it->second;
	}
	Channel channel;
	channel.sound_id = sound_id;
	channel.cap = cap;
	channel.candidates.reserve(cap);
	channels[sound_id] = channel;
	return channels[sound_id];
}

bool SoundMixer2D::resolve_listener(BulletFactory2D &factory, const SoundListenerSpec2D &spec, const Vector2 &event_pos, Vector2 &r_listener_pos, uint64_t &r_node_id, bool &r_is_point) const {
	r_node_id = 0;
	r_is_point = false;
	if (spec.kind == SoundListenerSpec2D::GODOT_LISTENER) {
		r_listener_pos = godot_listener_pos(factory);
		return true;
	}
	const GrazeTargetList2D *list = nullptr;
	if (spec.kind == SoundListenerSpec2D::NODE_GROUP) {
		list = factory.get_graze_default_detector().list_for_group(spec.group, factory);
	} else if (spec.detector != nullptr) {
		list = spec.detector->list(factory);
	}
	if (list == nullptr || list->targets.empty()) {
		return false;
	}
	// Nearest target wins, tree order (list order) breaks ties.
	size_t best = 0;
	double best_d2 = sound_dist2(event_pos, list->targets[0].position);
	for (size_t i = 1; i < list->targets.size(); ++i) {
		const double d2 = sound_dist2(event_pos, list->targets[i].position);
		if (d2 < best_d2) {
			best_d2 = d2;
			best = i;
		}
	}
	const uint64_t id = list->targets[best].id;
	r_listener_pos = list->targets[best].position;
	if (graze_is_point_target2d(id)) {
		r_is_point = true;
	} else {
		r_node_id = id;
	}
	return true;
}

bool SoundMixer2D::offer(BulletFactory2D &factory, const Ref<BulletSoundData2D> &sound, const Vector2 &event_pos, float volume_offset_db, const SoundListenerSpec2D &listener) {
	if (paused || sound.is_null() || !sound->enabled) {
		if (paused) {
			++dropped_total;
		}
		return false;
	}
	if (Engine::get_singleton()->is_editor_hint()) {
		return false;
	}
	if (sound->stream.is_null()) {
		if (WarnOnce2D::first(sound->get_instance_id(), SOUND_WARN_EMPTY_STREAM)) {
			UtilityFunctions::push_warning("BulletSoundData2D: stream is empty, the sound plays nothing.");
		}
		++dropped_total;
		return false;
	}
	if (sound->trigger_chance < 1.0) {
		if (!Math::is_finite((double)sound->trigger_chance) || UtilityFunctions::randf() > (double)sound->trigger_chance) {
			++dropped_total;
			return false;
		}
	}
	const size_t cap = sound->min_interval_sec > 0.0 ? 1 : (size_t)Math::max(1, sound->max_voices);
	Channel &channel = channel_for(sound->get_instance_id(), cap);
	// Offer-time distance over the current list: evicts the farthest while the
	// bounded slots fill, so winners are nearest. Re-resolved at flush.
	Vector2 listener_pos;
	uint64_t node_id = 0;
	bool is_point = false;
	const bool found = resolve_listener(factory, listener, event_pos, listener_pos, node_id, is_point);
	const Vector2 anchor = found ? listener_pos : godot_listener_pos(factory);
	const double dist = Math::sqrt(sound_dist2(event_pos, anchor));
	const bool audible = !sound->positional || !(dist > sound->max_distance);
	if (!audible) {
		++dropped_total;
		return false;
	}
	Candidate candidate;
	candidate.event_pos = event_pos;
	candidate.volume_offset_db = volume_offset_db;
	candidate.listener = listener;
	candidate.order = offer_order++;
	candidate.dist = dist;
	if (channel.candidates.size() < channel.cap) {
		channel.candidates.push_back(candidate);
		return true;
	}
	size_t farthest = 0;
	for (size_t i = 1; i < channel.candidates.size(); ++i) {
		if (channel.candidates[i].dist > channel.candidates[farthest].dist) {
			farthest = i;
		}
	}
	if (candidate.dist < channel.candidates[farthest].dist) {
		channel.candidates[farthest] = candidate;
		++dropped_total; // the evicted event never plays
		return true;
	}
	++dropped_total;
	return false;
}

void SoundMixer2D::ensure_voices_container(BulletFactory2D &factory) {
	if (voices_container != nullptr && ObjectDB::get_instance(ObjectID(voices_container->get_instance_id())) == voices_container) {
		return;
	}
	voices.clear();
	voices_container = memnew(Node2D);
	voices_container->set_name("SoundVoices");
	// Voices are positioned explicitly every play/follow: never interpolated.
	voices_container->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
	factory.add_child(voices_container);
}

AudioStreamPlayer2D *SoundMixer2D::alloc_voice(BulletFactory2D &factory, int priority) {
	ensure_voices_container(factory);
	for (size_t i = 0; i < voices.size(); ++i) {
		if (!voices[i].busy && voices[i].player != nullptr && ObjectDB::get_instance(ObjectID(voices[i].player->get_instance_id())) == voices[i].player) {
			return voices[i].player;
		}
	}
	if ((int)voices.size() < max_voices_total) {
		AudioStreamPlayer2D *player = memnew(AudioStreamPlayer2D);
		player->set_name("SoundVoice_" + String::num_int64((int64_t)voices.size()));
		player->set_max_polyphony(1);
		player->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
		voices_container->add_child(player);
		Voice voice;
		voice.player = player;
		voices.push_back(voice);
		return player;
	}
	// Steal the oldest busy voice at equal or lower priority, never above.
	size_t oldest = voices.size();
	for (size_t i = 0; i < voices.size(); ++i) {
		if (voices[i].busy && voices[i].priority <= priority && (oldest == voices.size() || voices[i].use_order < voices[oldest].use_order)) {
			oldest = i;
		}
	}
	if (oldest == voices.size()) {
		return nullptr;
	}
	voices[oldest].player->stop();
	release_voice(voices[oldest]);
	return voices[oldest].player;
}

void SoundMixer2D::release_voice(Voice &voice) {
	voice.busy = false;
	voice.sound_id = 0;
	voice.follows_node = false;
	voice.listener_node_id = 0;
	voice.max_duration = 0.0;
	if (voice.player != nullptr && ObjectDB::get_instance(ObjectID(voice.player->get_instance_id())) == voice.player) {
		// No resource retention: a finished voice holds no stream Ref.
		voice.player->set_stream(Ref<AudioStream>());
	}
}

void SoundMixer2D::flush(BulletFactory2D &factory) {
	if (Engine::get_singleton()->is_editor_hint()) {
		for (auto &entry : channels) {
			entry.second.candidates.clear();
		}
		return;
	}
	if (voices_container != nullptr && ObjectDB::get_instance(ObjectID(voices_container->get_instance_id())) != voices_container) {
		voices.clear();
		voices_container = nullptr;
	}
	const double clock = factory.get_graze_clock();
	// Poll: finished voices release, loopers obey max_duration, node
	// listeners are followed every sweep.
	bool any_busy = false;
	for (size_t i = 0; i < voices.size(); ++i) {
		Voice &voice = voices[i];
		if (!voice.busy || voice.player == nullptr || ObjectDB::get_instance(ObjectID(voice.player->get_instance_id())) != voice.player) {
			continue;
		}
		if (!voice.player->is_playing()) {
			release_voice(voice);
			continue;
		}
		if (voice.max_duration > 0.0 && clock - voice.start_clock >= voice.max_duration) {
			voice.player->stop();
			release_voice(voice);
			continue;
		}
		if (voice.follows_node && voice.listener_node_id != 0) {
			Object *obj = ObjectDB::get_instance(ObjectID(voice.listener_node_id));
			if (const Node2D *node = Object::cast_to<Node2D>(obj); node != nullptr) {
				voice.listener_last_pos = node->get_global_position();
				voice.player->set_global_position(godot_listener_pos(factory) + (voice.event_pos - voice.listener_last_pos));
			}
			// A dead listener freezes the voice where it is (never jumps).
		}
		any_busy = true;
	}
	bool any_pending = false;
	for (const auto &entry : channels) {
		if (!entry.second.candidates.empty()) {
			any_pending = true;
			break;
		}
	}
	if (!any_pending && !any_busy) {
		return;
	}
	if (!any_pending) {
		return;
	}
	AudioServer *server = AudioServer::get_singleton();
	for (auto &entry : channels) {
		Channel &channel = entry.second;
		if (channel.candidates.empty()) {
			continue;
		}
		Object *obj = ObjectDB::get_instance(ObjectID(channel.sound_id));
		BulletSoundData2D *sound = Object::cast_to<BulletSoundData2D>(obj);
		if (sound == nullptr) {
			channel.candidates.clear();
			continue;
		}
		if (!sound->enabled || sound->stream.is_null()) {
			channel.candidates.clear();
			continue;
		}
		if (sound->min_interval_sec > 0.0 && clock - channel.last_play_clock < sound->min_interval_sec) {
			dropped_total += channel.candidates.size();
			channel.candidates.clear();
			continue;
		}
		// Fresh nearest per candidate: membership rescans on the interval,
		// positions stay live between scans.
		struct Winner {
			Vector2 event_pos;
			float volume_offset_db = 0.0f;
			double dist = 0.0;
			Vector2 listener_pos;
			uint64_t node_id = 0;
			bool is_point = false;
			uint64_t order = 0;
		};
		std::vector<Winner> winners;
		winners.reserve(channel.candidates.size());
		for (size_t i = 0; i < channel.candidates.size(); ++i) {
			const Candidate &candidate = channel.candidates[i];
			Vector2 listener_pos;
			uint64_t node_id = 0;
			bool is_point = false;
			const bool found = resolve_listener(factory, candidate.listener, candidate.event_pos, listener_pos, node_id, is_point);
			const Vector2 anchor = found ? listener_pos : godot_listener_pos(factory);
			const double dist = Math::sqrt(sound_dist2(candidate.event_pos, anchor));
			if (sound->positional && dist > sound->max_distance) {
				++dropped_total;
				continue;
			}
			Winner winner;
			winner.event_pos = candidate.event_pos;
			winner.volume_offset_db = candidate.volume_offset_db;
			winner.dist = dist;
			winner.listener_pos = anchor;
			winner.node_id = found ? node_id : 0;
			winner.is_point = is_point && found;
			winner.order = candidate.order;
			winners.push_back(winner);
		}
		channel.candidates.clear();
		if (winners.empty()) {
			continue;
		}
		std::sort(winners.begin(), winners.end(), [](const Winner &a, const Winner &b) {
			if (a.dist != b.dist) {
				return a.dist < b.dist;
			}
			return a.order < b.order;
		});
		const Vector2 godot_pos = godot_listener_pos(factory);
		const size_t take = sound->min_interval_sec > 0.0 ? 1 : (size_t)Math::max(1, sound->max_voices);
		for (size_t w = 0; w < winners.size() && w < take; ++w) {
			const Winner &winner = winners[w];
			// Per-sound busy cap.
			size_t busy_same = 0;
			size_t oldest_same = voices.size();
			for (size_t i = 0; i < voices.size(); ++i) {
				if (voices[i].busy && voices[i].sound_id == channel.sound_id) {
					++busy_same;
					if (oldest_same == voices.size() || voices[i].use_order < voices[oldest_same].use_order) {
						oldest_same = i;
					}
				}
			}
			if (busy_same >= (size_t)Math::max(1, sound->max_voices)) {
				if (sound->when_limit_reached != BulletSoundData2D::SOUND_LIMIT_REPLACE_OLDEST || oldest_same == voices.size()) {
					++dropped_total;
					continue;
				}
				voices[oldest_same].player->stop();
				release_voice(voices[oldest_same]);
			}
			AudioStreamPlayer2D *player = alloc_voice(factory, sound->priority);
			if (player == nullptr) {
				++dropped_total;
				continue;
			}
			size_t slot = voices.size();
			for (size_t i = 0; i < voices.size(); ++i) {
				if (voices[i].player == player) {
					slot = i;
					break;
				}
			}
			// Bus: unknown names play on Master (one warning per name).
			String bus = sound->bus;
			if (server == nullptr || server->get_bus_index(bus) < 0) {
				bool warned = false;
				for (size_t bi = 0; bi < warned_buses.size(); ++bi) {
					if (warned_buses[bi] == bus) {
						warned = true;
						break;
					}
				}
				if (!warned) {
					warned_buses.push_back(bus);
					UtilityFunctions::push_warning("BulletSoundData2D: bus \"" + bus + "\" does not exist, playing on Master.");
				}
				bus = "Master";
			}
			// Placement: Godot listener -> the event position (Godot does
			// falloff/panning); a node listener L of event P -> G + (P - L)
			// so the engine measures from L; point listeners are fixed.
			Vector2 at = winner.event_pos;
			const bool has_listener = winner.node_id != 0 || winner.is_point;
			if (sound->positional && has_listener) {
				at = godot_pos + (winner.event_pos - winner.listener_pos);
			} else if (!sound->positional) {
				at = godot_pos;
			}
			float volume = (float)sound->volume_db + winner.volume_offset_db;
			if (Math::is_finite((double)sound->random_volume_offset_db) && sound->random_volume_offset_db > 0.0) {
				volume += (float)UtilityFunctions::randf_range(-(double)sound->random_volume_offset_db, (double)sound->random_volume_offset_db);
			}
			const float pitch = (float)sound->pitch_scale * sound_roll_pitch(sound->random_pitch);
			player->set_stream(sound->stream);
			player->set_volume_db(volume);
			player->set_pitch_scale(pitch);
			player->set_bus(bus);
			if (sound->positional) {
				player->set_max_distance((float)sound->max_distance);
				player->set_attenuation((float)sound->attenuation);
				player->set_panning_strength((float)sound->panning_strength);
				player->set_area_mask((int)sound->area_mask);
			} else {
				player->set_max_distance(1e30f);
				player->set_attenuation(0.0f);
				player->set_panning_strength(0.0f);
				player->set_area_mask(0);
			}
			player->set_global_position(at);
			player->play();
			Voice &voice = voices[slot];
			voice.busy = true;
			voice.sound_id = channel.sound_id;
			voice.priority = sound->priority;
			voice.start_clock = clock;
			voice.max_duration = Math::is_finite(sound->max_duration_sec) && sound->max_duration_sec > 0.0 ? sound->max_duration_sec : 0.0;
			voice.event_pos = winner.event_pos;
			voice.listener_node_id = winner.node_id;
			voice.listener_last_pos = winner.listener_pos;
			voice.follows_node = winner.node_id != 0;
			voice.use_order = offer_order++;
			channel.last_play_clock = clock;
			++plays_total;
			if (log_enabled) {
				if (sound_log.size() >= 4096) {
					sound_log.remove_at(0);
				}
				Dictionary e;
				e["sound"] = (int64_t)channel.sound_id;
				e["position"] = at;
				e["frame"] = (int64_t)Engine::get_singleton()->get_physics_frames();
				sound_log.push_back(e);
			}
		}
	}
}

void SoundMixer2D::set_paused(bool p_paused) {
	paused = p_paused;
	for (size_t i = 0; i < voices.size(); ++i) {
		if (voices[i].busy && voices[i].player != nullptr && ObjectDB::get_instance(ObjectID(voices[i].player->get_instance_id())) == voices[i].player) {
			voices[i].player->set_stream_paused(p_paused);
		}
	}
	if (p_paused) {
		for (auto &entry : channels) {
			dropped_total += entry.second.candidates.size();
			entry.second.candidates.clear();
		}
	}
}

void SoundMixer2D::stop_all() {
	for (size_t i = 0; i < voices.size(); ++i) {
		if (voices[i].busy && voices[i].player != nullptr && ObjectDB::get_instance(ObjectID(voices[i].player->get_instance_id())) == voices[i].player) {
			voices[i].player->stop();
		}
		release_voice(voices[i]);
	}
}

void SoundMixer2D::clear() {
	stop_all();
	channels.clear();
}

void SoundMixer2D::set_log_enabled(bool enabled) {
	log_enabled = enabled;
	if (!enabled) {
		sound_log.clear();
	}
}

Array SoundMixer2D::debug_voices() const {
	Array out;
	for (size_t i = 0; i < voices.size(); ++i) {
		const Voice &voice = voices[i];
		Dictionary d;
		d["index"] = (int)i;
		d["busy"] = voice.busy;
		d["sound"] = (int64_t)voice.sound_id;
		d["priority"] = voice.priority;
		if (voice.player != nullptr && ObjectDB::get_instance(ObjectID(voice.player->get_instance_id())) == voice.player) {
			d["position"] = voice.player->get_global_position();
			d["volume_db"] = voice.player->get_volume_db();
			d["pitch_scale"] = voice.player->get_pitch_scale();
			d["bus"] = voice.player->get_bus();
			d["max_distance"] = voice.player->get_max_distance();
			d["attenuation"] = voice.player->get_attenuation();
			d["panning_strength"] = voice.player->get_panning_strength();
			d["area_mask"] = voice.player->get_area_mask();
		}
		out.push_back(d);
	}
	return out;
}

Dictionary SoundMixer2D::debug_stats() const {
	Dictionary d;
	d["voices_total"] = (int)voices.size();
	int busy = 0;
	for (size_t i = 0; i < voices.size(); ++i) {
		if (voices[i].busy) {
			++busy;
		}
	}
	d["voices_busy"] = busy;
	d["channels"] = (int)channels.size();
	size_t pending = 0;
	for (const auto &entry : channels) {
		pending += entry.second.candidates.size();
	}
	d["pending"] = (int)pending;
	d["plays_total"] = (int64_t)plays_total;
	d["dropped_total"] = (int64_t)dropped_total;
	return d;
}

} // namespace BlastBullets2D
