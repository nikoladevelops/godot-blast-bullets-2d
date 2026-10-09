#include "factory/sound_mixer2d.hpp"

#include "bullet_volley/bullet_volley2d.hpp"
#include "core/cosmetic_rng2d.hpp"
#include "core/warn_once2d.hpp"
#include "factory/bullet_factory2d.hpp"

#include <godot_cpp/classes/audio_stream_mp3.hpp>
#include <godot_cpp/classes/audio_stream_ogg_vorbis.hpp>
#include <godot_cpp/classes/audio_stream_wav.hpp>
#include <godot_cpp/classes/camera2d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/physics_direct_space_state2d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters2d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>

using namespace godot;
namespace BlastBullets2D {

// Warn-once codes (per resource instance, WarnOnce2D).
static constexpr uint32_t SOUND_WARN_EMPTY_STREAM = 0x50DD01;
static constexpr uint32_t SOUND_WARN_BAD_BUS = 0x50DD02;
static constexpr uint32_t SOUND_WARN_BAD_CAMERA = 0x50DD03;

static double sound_dist2(const Vector2 &a, const Vector2 &b) {
	const double dx = (double)a.x - (double)b.x;
	const double dy = (double)a.y - (double)b.y;
	return dx * dx + dy * dy;
}

// Any playable audio: at least one non-null list entry.
static bool sound_has_audio(const BulletSoundData2D *sound) {
	for (int i = 0; i < sound->streams.size(); ++i) {
		const Variant entry = sound->streams[i];
		if (entry.get_type() == Variant::OBJECT && Object::cast_to<AudioStream>((Object *)entry) != nullptr) {
			return true;
		}
	}
	return false;
}

// Offer cache helpers (see OfferCache). Outside the bullet iteration every
// helper reads the engine directly, exactly as before.
bool SoundMixer2D::offer_cache_open(BulletFactory2D &factory) const {
	if (!factory.is_bullets_iterating()) {
		return false;
	}
	if (!offer_cache.open || offer_cache.sweep != factory.get_sweep_counter() || offer_cache.epoch != factory.get_user_code_epoch()) {
		offer_cache = OfferCache();
		offer_cache.open = true;
		offer_cache.sweep = factory.get_sweep_counter();
		offer_cache.epoch = factory.get_user_code_epoch();
	}
	return true;
}

Vector2 SoundMixer2D::offer_godot_listener(BulletFactory2D &factory) const {
	if (!offer_cache_open(factory)) {
		return godot_listener_pos(factory);
	}
	if (!offer_cache.listener_ready) {
		offer_cache.listener = godot_listener_pos(factory);
		offer_cache.listener_ready = true;
	}
	return offer_cache.listener;
}

Camera2D *SoundMixer2D::offer_viewport_camera(BulletFactory2D &factory) const {
	Viewport *vp = factory.get_viewport();
	if (!offer_cache_open(factory)) {
		return vp != nullptr ? vp->get_camera_2d() : nullptr;
	}
	if (!offer_cache.camera_ready) {
		offer_cache.camera = vp != nullptr ? vp->get_camera_2d() : nullptr;
		offer_cache.camera_ready = true;
	}
	return offer_cache.camera;
}

bool SoundMixer2D::offer_has_audio(BulletFactory2D &factory, const BulletSoundData2D *sound) const {
	if (!offer_cache_open(factory)) {
		return sound_has_audio(sound);
	}
	if (offer_cache.audio_sound != sound) {
		offer_cache.audio_sound = sound;
		offer_cache.audio = sound_has_audio(sound);
	}
	return offer_cache.audio;
}

Ref<AudioStream> SoundMixer2D::pick_stream(const BulletSoundData2D *sound, Channel &channel) {
	std::vector<Ref<AudioStream>> usable;
	usable.reserve((size_t)sound->streams.size());
	for (int i = 0; i < sound->streams.size(); ++i) {
		const Variant entry = sound->streams[i];
		AudioStream *s = entry.get_type() == Variant::OBJECT ? Object::cast_to<AudioStream>((Object *)entry) : nullptr;
		if (s != nullptr) {
			usable.push_back(Ref<AudioStream>(s));
		}
	}
	if (usable.empty()) {
		return Ref<AudioStream>();
	}
	const size_t n = usable.size();
	const int mode = sound->stream_mode;
	if (mode == BulletSoundData2D::STREAM_SEQUENCE) {
		const size_t idx = channel.seq_cursor % n;
		channel.seq_cursor = idx + 1;
		return usable[idx];
	}
	if (mode == BulletSoundData2D::STREAM_SHUFFLE) {
		if (channel.shuffle_bag.empty() || channel.shuffle_n != n) {
			channel.shuffle_bag.clear();
			for (size_t i = 0; i < n; ++i) {
				channel.shuffle_bag.push_back(i);
			}
			for (size_t i = n; i > 1; --i) {
				const size_t j = (size_t)CosmeticRng2D::randi() % i;
				std::swap(channel.shuffle_bag[i - 1], channel.shuffle_bag[j]);
			}
			channel.shuffle_n = n;
		}
		const size_t idx = channel.shuffle_bag.back();
		channel.shuffle_bag.pop_back();
		return usable[idx];
	}
	if (mode == BulletSoundData2D::STREAM_WEIGHTED && sound->stream_weights.size() == sound->streams.size()) {
		double total = 0.0;
		for (int i = 0; i < sound->streams.size(); ++i) {
			const Variant entry = sound->streams[i];
			if (entry.get_type() == Variant::OBJECT && Object::cast_to<AudioStream>((Object *)entry) != nullptr && sound->stream_weights[i] > 0.0f) {
				total += (double)sound->stream_weights[i];
			}
		}
		if (total > 0.0) {
			double roll = CosmeticRng2D::randf() * total;
			for (int i = 0; i < sound->streams.size(); ++i) {
				const Variant entry = sound->streams[i];
				AudioStream *s = entry.get_type() == Variant::OBJECT ? Object::cast_to<AudioStream>((Object *)entry) : nullptr;
				if (s == nullptr || sound->stream_weights[i] <= 0.0f) {
					continue;
				}
				roll -= (double)sound->stream_weights[i];
				if (roll <= 0.0) {
					return Ref<AudioStream>(s);
				}
			}
			return usable.back();
		}
	}
	return usable[(size_t)CosmeticRng2D::randi() % n];
}
// AudioStreamRandomizer pitch math: pitch_scale * exp(lerp(log(1/r),
// log(r), randf)). Rolled only when r > 1 (no RNG burn otherwise).
static float sound_roll_pitch(double range) {
	if (!(range > 1.0) || !Math::is_finite(range)) {
		return 1.0f;
	}
	const double u = CosmeticRng2D::randf();
	return (float)Math::exp(Math::log(range) * (2.0 * u - 1.0));
}

float SoundMixer2D::apply_playback_mix(AudioStreamPlayer2D *player, const BulletSoundData2D *sound, const Ref<AudioStream> &picked, float volume_offset_db, std::vector<String> *warned_buses, bool force_centered, const StringName &bus_override) {
	// Bus: unknown names play on Master (one warning per name).
	String bus = bus_override.is_empty() ? String(sound->bus) : String(bus_override);
	AudioServer *server = AudioServer::get_singleton();
	if (server == nullptr || server->get_bus_index(bus) < 0) {
		bool warned = true;
		if (warned_buses != nullptr) {
			warned = false;
			for (size_t bi = 0; bi < warned_buses->size(); ++bi) {
				if ((*warned_buses)[bi] == bus) {
					warned = true;
					break;
				}
			}
			if (!warned) {
				warned_buses->push_back(bus);
			}
		}
		if (!warned) {
			UtilityFunctions::push_warning("BulletSoundData2D: bus \"" + bus + "\" does not exist, playing on Master.");
		}
		bus = "Master";
	}
	float volume = (float)sound->volume_db + volume_offset_db;
	if (Math::is_finite((double)sound->random_volume_offset_db) && sound->random_volume_offset_db > 0.0) {
		volume += (float)CosmeticRng2D::randf_range(-(double)sound->random_volume_offset_db, (double)sound->random_volume_offset_db);
	}
	// Asymmetric volume bounds: uniform in [min, max] (sorted when inverted,
	// so the setters stay independent). 0/0 disables without burning RNG;
	// min == max adds deterministically (tests, musical stings).
	if (Math::is_finite((double)sound->random_volume_min_db) && Math::is_finite((double)sound->random_volume_max_db) && (sound->random_volume_min_db != 0.0 || sound->random_volume_max_db != 0.0)) {
		if (sound->random_volume_min_db == sound->random_volume_max_db) {
			volume += (float)sound->random_volume_min_db;
		} else {
			const double lo = sound->random_volume_min_db < sound->random_volume_max_db ? sound->random_volume_min_db : sound->random_volume_max_db;
			const double hi = sound->random_volume_min_db < sound->random_volume_max_db ? sound->random_volume_max_db : sound->random_volume_min_db;
			volume += (float)CosmeticRng2D::randf_range(lo, hi);
		}
	}
	float pitch = (float)sound->pitch_scale * sound_roll_pitch(sound->random_pitch);
	// Asymmetric pitch bounds (linear multipliers, same sorting/no-RNG rules).
	if (Math::is_finite((double)sound->random_pitch_min) && Math::is_finite((double)sound->random_pitch_max) && (sound->random_pitch_min != 1.0 || sound->random_pitch_max != 1.0)) {
		if (sound->random_pitch_min == sound->random_pitch_max) {
			pitch *= (float)sound->random_pitch_min;
		} else {
			const double lo = sound->random_pitch_min < sound->random_pitch_max ? sound->random_pitch_min : sound->random_pitch_max;
			const double hi = sound->random_pitch_min < sound->random_pitch_max ? sound->random_pitch_max : sound->random_pitch_min;
			pitch *= (float)CosmeticRng2D::randf_range(lo, hi);
		}
	}
	// Final clamps (applied last: trim, randoms and amount gain all count).
	// Inverted bounds sort themselves; non-finite bounds (never via setters)
	// skip instead of poisoning the mix.
	if (Math::is_finite((double)sound->volume_min_db) && Math::is_finite((double)sound->volume_max_db)) {
		const float lo = (float)(sound->volume_min_db < sound->volume_max_db ? sound->volume_min_db : sound->volume_max_db);
		const float hi = (float)(sound->volume_min_db < sound->volume_max_db ? sound->volume_max_db : sound->volume_min_db);
		if (volume < lo) {
			volume = lo;
		} else if (volume > hi) {
			volume = hi;
		}
	}
	if (Math::is_finite((double)sound->pitch_min) && Math::is_finite((double)sound->pitch_max) && sound->pitch_min > 0.0 && sound->pitch_max > 0.0) {
		const float lo = (float)(sound->pitch_min < sound->pitch_max ? sound->pitch_min : sound->pitch_max);
		const float hi = (float)(sound->pitch_min < sound->pitch_max ? sound->pitch_max : sound->pitch_min);
		if (pitch < lo) {
			pitch = lo;
		} else if (pitch > hi) {
			pitch = hi;
		}
	}
	player->set_stream(picked);
	player->set_volume_db(volume);
	player->set_pitch_scale(pitch);
	player->set_bus(bus);
	if (sound->positional && !force_centered) {
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
	return volume;
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
		r_listener_pos = offer_godot_listener(factory);
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

bool SoundMixer2D::sound_in_camera_view(const BulletSoundData2D *sound, const Vector2 &event_pos, const Camera2D *camera, const Viewport *vp) {
	if (sound->audibility_mode != BulletSoundData2D::SOUND_AUDIBILITY_CAMERA && sound->audibility_mode != BulletSoundData2D::SOUND_AUDIBILITY_DISTANCE_AND_CAMERA) {
		return true;
	}
	if (vp == nullptr || camera == nullptr) {
		return true;
	}
	const Vector2 zoom = camera->get_zoom();
	const bool zoom_ok = Math::is_finite((double)zoom.x) && Math::is_finite((double)zoom.y) && zoom.x > 0.0 && zoom.y > 0.0;
	const double zx = zoom_ok ? (double)zoom.x : 1.0;
	const double zy = zoom_ok ? (double)zoom.y : 1.0;
	const Vector2 half = vp->get_visible_rect().size * Vector2(0.5f / (float)zx, 0.5f / (float)zy);
	const double margin = Math::is_finite(sound->camera_margin_px) && sound->camera_margin_px > 0.0 ? sound->camera_margin_px : 0.0;
	// Degenerate zoom reads as 1 everywhere, including the center: the
	// canvas-derived call is skipped outright (it inverts a singular canvas
	// on zero zoom), and a non-finite result still falls back to the node.
	Vector2 center = zoom_ok ? camera->get_screen_center_position() : camera->get_global_position();
	if (!center.is_finite()) {
		center = camera->get_global_position();
	}
	// Exact for any camera rotation: test in the unrotated frame (margin is
	// screen pixels, so it is un-zoomed per axis; rotation 0 with uniform
	// zoom behaves exactly as before).
	const Vector2 local = (event_pos - center).rotated(-camera->get_global_rotation());
	const Vector2 grown = half + Vector2((float)(margin / zx), (float)(margin / zy));
	return Math::abs((double)local.x) <= (double)grown.x && Math::abs((double)local.y) <= (double)grown.y;
}

double SoundMixer2D::sound_effective_max_distance(const BulletSoundData2D *sound, const Camera2D *camera) {
	if (!sound->zoom_scales_distance) {
		return sound->max_distance;
	}
	double zoom = 1.0;
	if (camera != nullptr) {
		const Vector2 z = camera->get_zoom();
		if (Math::is_finite((double)z.x) && Math::is_finite((double)z.y) && z.x > 0.0 && z.y > 0.0) {
			zoom = MIN((double)z.x, (double)z.y);
		}
	}
	if (!(zoom > 0.0) || !Math::is_finite(zoom)) {
		return sound->max_distance;
	}
	return sound->max_distance / zoom;
}

Camera2D *SoundMixer2D::resolve_entry_camera(BulletFactory2D &factory, const BulletSoundData2D *sound, Channel &channel) const {
	Viewport *vp = factory.get_viewport();
	if (sound->camera_path.is_empty()) {
		channel.camera_path = NodePath();
		channel.camera_id = 0;
		return offer_viewport_camera(factory);
	}
	if (channel.camera_id != 0 && channel.camera_path == sound->camera_path) {
		Camera2D *cached = Object::cast_to<Camera2D>(ObjectDB::get_instance(ObjectID(channel.camera_id)));
		if (cached != nullptr && !cached->is_queued_for_deletion() && cached->is_inside_tree() && cached->get_viewport() == vp) {
			return cached;
		}
	}
	channel.camera_path = sound->camera_path;
	channel.camera_id = 0;
	Camera2D *pinned = Object::cast_to<Camera2D>(factory.get_node_or_null(sound->camera_path));
	if (pinned == nullptr || pinned->is_queued_for_deletion() || !pinned->is_inside_tree() || pinned->get_viewport() != vp) {
		if (WarnOnce2D::first(sound->get_instance_id(), SOUND_WARN_BAD_CAMERA)) {
			UtilityFunctions::push_warning("BulletSoundData2D: camera_path does not point to a Camera2D in this viewport, using the viewport camera.");
		}
		return vp != nullptr ? vp->get_camera_2d() : nullptr;
	}
	channel.camera_id = pinned->get_instance_id();
	return pinned;
}

bool SoundMixer2D::offer(BulletFactory2D &factory, const Ref<BulletSoundData2D> &sound, const Vector2 &event_pos, float volume_offset_db, const SoundListenerSpec2D &listener, int volley_amount, uint64_t follow_volley_id, int follow_life_id, int follow_bullet) {
	if (paused || sound.is_null() || !sound->enabled) {
		if (paused) {
			++dropped_total;
		}
		return false;
	}
	if (Engine::get_singleton()->is_editor_hint()) {
		return false;
	}
	if (!offer_has_audio(factory, sound.ptr())) {
		if (WarnOnce2D::first(sound->get_instance_id(), SOUND_WARN_EMPTY_STREAM)) {
			UtilityFunctions::push_warning("BulletSoundData2D: streams is empty, the sound plays nothing.");
		}
		++dropped_total;
		return false;
	}
	if (sound->trigger_chance < 1.0) {
		if (!Math::is_finite((double)sound->trigger_chance) || CosmeticRng2D::randf() > (double)sound->trigger_chance) {
			++dropped_total;
			return false;
		}
	}
	// Amount gates compose with everything by AND, first: a volley outside
	// the size window never takes a candidate slot.
	if ((sound->min_volley_amount > 0 && volley_amount < sound->min_volley_amount) || (sound->max_volley_amount > 0 && volley_amount > sound->max_volley_amount)) {
		++dropped_total;
		return false;
	}
	// One hum per followed bullet: a bullet whose follow voice is still live
	// (not fading) never offers a second copy. Each bullet offers at most once
	// per sweep, so every voice that already plays is visible here.
	if (sound->follow_bullet && follow_volley_id != 0 && follow_bullet >= 0) {
		const uint64_t sid = sound->get_instance_id();
		for (const Voice &voice : voices) {
			if (voice.busy && !voice.fading && voice.following && voice.sound_id == sid && voice.follow_volley == follow_volley_id && voice.follow_life == follow_life_id && voice.follow_bullet == follow_bullet) {
				++dropped_total;
				return false;
			}
		}
	}
	const size_t cap = sound->min_interval_sec > 0.0 ? 1 : (size_t)Math::max(1, sound->max_voices);
	Channel &channel = channel_for(sound->get_instance_id(), cap);
	Camera2D *camera = resolve_entry_camera(factory, sound.ptr(), channel);
	Viewport *vp = factory.get_viewport();
	// Offer-time distance over the current list: evicts the farthest while the
	// bounded slots fill, so winners are nearest. Re-resolved at flush.
	Vector2 listener_pos;
	uint64_t node_id = 0;
	bool is_point = false;
	const bool found = resolve_listener(factory, listener, event_pos, listener_pos, node_id, is_point);
	const Vector2 anchor = found ? listener_pos : offer_godot_listener(factory);
	const double dist = Math::sqrt(sound_dist2(event_pos, anchor));
	const double max_dist = sound_effective_max_distance(sound.ptr(), camera);
	const bool audible = (!sound->positional || !(dist > max_dist)) && sound_in_camera_view(sound.ptr(), event_pos, camera, vp);
	if (!audible) {
		++dropped_total;
		return false;
	}
	Candidate candidate;
	candidate.event_pos = event_pos;
	candidate.volume_offset_db = volume_offset_db;
	candidate.listener = listener;
	candidate.order = offer_order++;
	candidate.volley_amount = volley_amount;
	candidate.follow_volley = follow_volley_id;
	candidate.follow_life = follow_life_id;
	candidate.follow_bullet = follow_bullet;
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
	// Steal at equal or lower priority, never above. A voice already fading
	// out goes first (its tail is cheap to cut). Otherwise the oldest live
	// voice starts its fade and this play waits for the next sweep, so a
	// fade is never restarted and a looper's tail always runs out.
	const double clock = factory.get_graze_clock();
	size_t oldest = voices.size();
	for (size_t i = 0; i < voices.size(); ++i) {
		if (voices[i].busy && voices[i].fading && voices[i].priority <= priority && (oldest == voices.size() || voices[i].use_order < voices[oldest].use_order)) {
			oldest = i;
		}
	}
	if (oldest == voices.size()) {
		for (size_t i = 0; i < voices.size(); ++i) {
			if (voices[i].busy && !voices[i].fading && voices[i].priority <= priority && (oldest == voices.size() || voices[i].use_order < voices[oldest].use_order)) {
				oldest = i;
			}
		}
		if (oldest == voices.size()) {
			return nullptr;
		}
		if (voice_begin_fade(voices[oldest], clock, voice_fade_len(voices[oldest].sound_id))) {
			return nullptr; // fading out now: this play waits for the next sweep
		}
	}
	voices[oldest].player->stop();
	release_voice(voices[oldest]);
	return voices[oldest].player;
}

double SoundMixer2D::voice_fade_len(uint64_t sound_id) {
	Object *obj = ObjectDB::get_instance(ObjectID(sound_id));
	const BulletSoundData2D *sound = Object::cast_to<BulletSoundData2D>(obj);
	if (sound == nullptr) {
		return 0.0;
	}
	return Math::is_finite((double)sound->fade_out_sec) && sound->fade_out_sec > 0.0 ? (double)sound->fade_out_sec : 0.0;
}

bool SoundMixer2D::voice_begin_fade(Voice &voice, double clock, double fade_len) {
	if (voice.fading) {
		return true; // already on its way out: the fade is never restarted
	}
	if (!(fade_len > 0.0) || !Math::is_finite(fade_len) || voice.player == nullptr || ObjectDB::get_instance(ObjectID(voice.player->get_instance_id())) != voice.player || !voice.player->is_playing()) {
		return false;
	}
	voice.fading = true;
	voice.fade_start = clock;
	voice.fade_from = voice.player->get_volume_db();
	voice.fade_len = fade_len;
	return true;
}

void SoundMixer2D::release_voice(Voice &voice) {
	voice.busy = false;
	voice.sound_id = 0;
	voice.follows_node = false;
	voice.listener_node_id = 0;
	voice.max_duration = 0.0;
	voice.fading = false;
	voice.fade_start = 0.0;
	voice.fade_from = 0.0f;
	voice.fade_len = 0.0;
	voice.duck_dip = 0.0f;
	voice.following = false;
	voice.follow_volley = 0;
	voice.follow_life = -1;
	voice.follow_bullet = -1;
	if (voice.player != nullptr && ObjectDB::get_instance(ObjectID(voice.player->get_instance_id())) == voice.player) {
		// No resource retention: a finished voice holds no stream Ref.
		voice.player->set_stream(Ref<AudioStream>());
	}
}

// One voice's live duck rank: the resource's priority and duck amount when
// it is still around, else the stored priority and no dip.
static void duck_source2d(uint64_t sound_id, int stored_priority, int &r_priority, float &r_duck) {
	r_priority = stored_priority;
	r_duck = 0.0f;
	Object *obj = ObjectDB::get_instance(ObjectID(sound_id));
	const BulletSoundData2D *sound = Object::cast_to<BulletSoundData2D>(obj);
	if (sound == nullptr) {
		return;
	}
	r_priority = sound->priority;
	if (Math::is_finite((double)sound->duck_amount_db) && sound->duck_amount_db > 0.0) {
		r_duck = (float)sound->duck_amount_db;
	}
}

// Loudness for one entry's zoom gain (k * log2 of the camera's minimum
// zoom). 0 when disabled, non-finite, zoom 1, or no camera: zoom 1 adds
// nothing, so the default mix is bit-identical.
static double zoom_gain_for2d(const BulletSoundData2D *sound, const Camera2D *camera) {
	if (!(sound->zoom_gain_db > 0.0 || sound->zoom_gain_db < 0.0) || !Math::is_finite((double)sound->zoom_gain_db) || camera == nullptr) {
		return 0.0;
	}
	const Vector2 z = camera->get_zoom();
	if (!Math::is_finite((double)z.x) || !Math::is_finite((double)z.y) || !(z.x > 0.0) || !(z.y > 0.0)) {
		return 0.0;
	}
	const double zmin = MIN((double)z.x, (double)z.y);
	return (double)sound->zoom_gain_db * (Math::log(zmin) / Math::log(2.0));
}

// Occlusion penalty for one winning play: raycasts the anchor (listener)
// to the event against occlusion_mask (bodies only) and returns
// occlusion_db on any hit, else 0. Skipped outright while disabled,
// non-positional, or degenerate: winners only, never per candidate.
static float occlusion_penalty_for2d(BulletFactory2D &factory, const BulletSoundData2D *sound, const Vector2 &from, const Vector2 &to) {
	if (!sound->positional || sound->occlusion_mask == 0) {
		return 0.0f;
	}
	if (!Math::is_finite((double)sound->occlusion_db) || !(sound->occlusion_db > 0.0)) {
		return 0.0f;
	}
	if (!from.is_finite() || !to.is_finite() || from.distance_squared_to(to) < 0.01) {
		return 0.0f;
	}
	Ref<World2D> world = factory.get_world_2d();
	if (world.is_null()) {
		return 0.0f;
	}
	PhysicsDirectSpaceState2D *space = world->get_direct_space_state();
	if (space == nullptr) {
		return 0.0f;
	}
	Ref<PhysicsRayQueryParameters2D> query = PhysicsRayQueryParameters2D::create(from, to, (uint32_t)sound->occlusion_mask);
	const Dictionary hit = space->intersect_ray(query);
	return hit.is_empty() ? 0.0f : (float)sound->occlusion_db;
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
		if (voice.fading) {
			// A fade in progress: ramp to silence, then stop and release.
			// Fading voices keep following their listener below.
			const double t = voice.fade_len > 0.0 ? (clock - voice.fade_start) / voice.fade_len : 1.0;
			if (t >= 1.0) {
				voice.player->stop();
				release_voice(voice);
				continue;
			}
			float target = voice.fade_from + (float)t * (-80.0f - voice.fade_from);
			if (target < -80.0f) {
				target = -80.0f;
			}
			voice.player->set_volume_db(target);
		} else if (voice.max_duration > 0.0 && clock - voice.start_clock >= voice.max_duration) {
			if (!voice_begin_fade(voice, clock, voice_fade_len(voice.sound_id))) {
				voice.player->stop();
				release_voice(voice);
				continue;
			}
		}
		if (voice.following && voice.follow_volley != 0 && voice.follow_bullet >= 0) {
			// Follow-the-bullet: re-pose to the bullet's live pose (the
			// caches are global, same source as the offers). A cleared,
			// expired or pooled-into-a-new-life bullet ends the voice
			// through the fade path; an already-fading one just keeps
			// ramping (never restarted). Owns placement: node-follow below
			// is skipped while the bullet lives.
			BulletVolley2D *followed = Object::cast_to<BulletVolley2D>(ObjectDB::get_instance(ObjectID(voice.follow_volley)));
			if (followed == nullptr || followed->is_queued_for_deletion() || followed->get_life_id() != voice.follow_life || voice.follow_bullet >= followed->get_amount_bullets() || !followed->is_bullet_status_enabled(voice.follow_bullet)) {
				followed = nullptr;
			}
			if (followed == nullptr) {
				if (!voice.fading && !voice_begin_fade(voice, clock, voice_fade_len(voice.sound_id))) {
					voice.player->stop();
					release_voice(voice);
				}
				continue;
			}
			voice.player->set_global_position(followed->get_bullet_transform(voice.follow_bullet).get_origin());
		} else if (voice.follows_node && voice.listener_node_id != 0) {
			Object *obj = ObjectDB::get_instance(ObjectID(voice.listener_node_id));
			if (const Node2D *node = Object::cast_to<Node2D>(obj); node != nullptr) {
				voice.listener_last_pos = node->get_global_position();
				voice.player->set_global_position(godot_listener_pos(factory) + (voice.event_pos - voice.listener_last_pos));
			}
			// A dead listener freezes the voice where it is (never jumps).
		}
		any_busy = true;
	}
	// Ducking (duck_amount_db): every busy voice dips the busy voices of
	// strictly lower priority by the strongest such dip. Fading voices sit
	// out both ways (the fade owns their fader). Runs every sweep, even with
	// no new offers, so dips apply and release on time.
	if (any_busy) {
		// One live rank per busy voice, then one pass from the highest priority
		// down: a voice takes the strongest dip among busy voices of strictly
		// higher priority (equal priorities never dip each other).
		duck_scratch.clear();
		for (size_t i = 0; i < voices.size(); ++i) {
			const Voice &voice = voices[i];
			if (!voice.busy || voice.fading || voice.player == nullptr || ObjectDB::get_instance(ObjectID(voice.player->get_instance_id())) != voice.player) {
				continue;
			}
			DuckEntry entry;
			entry.voice = i;
			duck_source2d(voice.sound_id, voice.priority, entry.priority, entry.duck);
			duck_scratch.push_back(entry);
		}
		std::sort(duck_scratch.begin(), duck_scratch.end(), [](const DuckEntry &a, const DuckEntry &b) {
			return a.priority > b.priority;
		});
		float higher_dip = 0.0f; // strongest duck among strictly higher priorities
		for (size_t k = 0; k < duck_scratch.size();) {
			const int prio = duck_scratch[k].priority;
			size_t group_end = k;
			float group_duck = 0.0f;
			while (group_end < duck_scratch.size() && duck_scratch[group_end].priority == prio) {
				group_duck = std::max(group_duck, duck_scratch[group_end].duck);
				++group_end;
			}
			for (size_t m = k; m < group_end; ++m) {
				Voice &voice = voices[duck_scratch[m].voice];
				if (higher_dip != voice.duck_dip) {
					voice.duck_dip = higher_dip;
					voice.player->set_volume_db(voice.last_volume - higher_dip);
				}
			}
			higher_dip = std::max(higher_dip, group_duck);
			k = group_end;
		}
	}
	bool any_pending = false;
	for (const auto &entry : channels) {
		if (!entry.second.candidates.empty()) {
			any_pending = true;
			break;
		}
	}
	// Dead-resource channels never play again (their interval memory and
	// pick cursors are moot): drop them instead of carrying them forever.
	// Live ones keep everything, even idle. A game minting entries per shot
	// would otherwise grow this map without bound.
	for (auto it = channels.begin(); it != channels.end();) {
		if (it->second.candidates.empty() && ObjectDB::get_instance(ObjectID(it->first)) == nullptr) {
			it = channels.erase(it);
		} else {
			++it;
		}
	}
	if (!any_pending && !any_busy) {
		return;
	}
	if (!any_pending) {
		return;
	}
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
		if (!sound->enabled || !sound_has_audio(sound)) {
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
		Camera2D *camera = resolve_entry_camera(factory, sound, channel);
		Viewport *vp = factory.get_viewport();
		struct Winner {
			Vector2 event_pos;
			float volume_offset_db = 0.0f;
			double dist = 0.0;
			int volley_amount = 1;
			Vector2 listener_pos;
			uint64_t node_id = 0;
			bool is_point = false;
			uint64_t order = 0;
			uint64_t follow_volley = 0;
			int follow_life = -1;
			int follow_bullet = -1;
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
			const double max_dist = sound_effective_max_distance(sound, camera);
			if ((sound->positional && dist > max_dist) || !sound_in_camera_view(sound, candidate.event_pos, camera, vp)) {
				++dropped_total;
				continue;
			}
			Winner winner;
			winner.event_pos = candidate.event_pos;
			winner.volume_offset_db = candidate.volume_offset_db;
			winner.dist = dist;
			winner.volley_amount = candidate.volley_amount;
			winner.listener_pos = anchor;
			winner.node_id = found ? node_id : 0;
			winner.is_point = is_point && found;
			winner.order = candidate.order;
			winner.follow_volley = candidate.follow_volley;
			winner.follow_life = candidate.follow_life;
			winner.follow_bullet = candidate.follow_bullet;
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
			// Fading tails do not count: they are already on their way out.
			size_t busy_same = 0;
			size_t oldest_same = voices.size();
			for (size_t i = 0; i < voices.size(); ++i) {
				if (voices[i].busy && !voices[i].fading && voices[i].sound_id == channel.sound_id) {
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
				size_t victim = oldest_same;
				if (sound->steal_mode == BulletSoundData2D::STEAL_QUIETEST) {
					for (size_t i = 0; i < voices.size(); ++i) {
						if (voices[i].busy && !voices[i].fading && voices[i].sound_id == channel.sound_id && voices[i].last_volume < voices[victim].last_volume) {
							victim = i;
						}
					}
				}
				if (!voice_begin_fade(voices[victim], clock, voice_fade_len(channel.sound_id))) {
					voices[victim].player->stop();
					release_voice(voices[victim]);
				}
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
			// Random pan: perpendicular spread around the listener direction
			// (positional voices only: centered voices have no direction and
			// no panning, so the knob is a no-op there). Burns no RNG while off.
			if (sound->positional && Math::is_finite((double)sound->random_pan) && sound->random_pan > 0.0 && winner.dist >= 1.0) {
				const Vector2 radial = winner.event_pos - winner.listener_pos;
				const Vector2 tangent = Vector2(-radial.y, radial.x) / (float)winner.dist;
				const float spread = (float)CosmeticRng2D::randf_range(-(double)sound->random_pan, (double)sound->random_pan) * (float)winner.dist;
				at += tangent * spread;
			}
			const Ref<AudioStream> picked = pick_stream(sound, channel);
			if (picked.is_null()) {
				++dropped_total;
				continue;
			}
			// Volley-size gain, added with the trim and the randoms (bounded:
			// log10 gives +4 dB at 100 bullets, +8 dB at 10k, for k = 2).
			float amount_gain = 0.0f;
			if (Math::is_finite((double)sound->amount_gain_db) && sound->amount_gain_db > 0.0) {
				amount_gain = (float)((double)sound->amount_gain_db * std::log10((double)MAX(1, winner.volley_amount)));
			}
			// Occlusion is resolved once per winning play: the dip and the bus
			// switch stay coupled (the helper returns 0 exactly when nothing hit).
			const float occlusion = occlusion_penalty_for2d(factory, sound, winner.listener_pos, winner.event_pos);
			const StringName muffle_bus = (occlusion > 0.0f && !sound->occlusion_bus.is_empty()) ? sound->occlusion_bus : StringName();
			const float volume = SoundMixer2D::apply_playback_mix(player, sound, picked, winner.volume_offset_db + amount_gain + (float)zoom_gain_for2d(sound, camera) - occlusion, &warned_buses, false, muffle_bus);
			player->set_global_position(at);
			player->play();
			Voice &voice = voices[slot];
			voice.busy = true;
			voice.sound_id = channel.sound_id;
			voice.priority = sound->priority;
			voice.start_clock = clock;
			voice.last_volume = volume;
			voice.max_duration = Math::is_finite(sound->max_duration_sec) && sound->max_duration_sec > 0.0 ? sound->max_duration_sec : 0.0;
			voice.event_pos = winner.event_pos;
			voice.listener_node_id = winner.node_id;
			voice.listener_last_pos = winner.listener_pos;
			voice.follows_node = winner.node_id != 0;
			voice.following = sound->follow_bullet;
			voice.follow_volley = winner.follow_volley;
			voice.follow_life = winner.follow_life;
			voice.follow_bullet = winner.follow_bullet;
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

// One transient player per entry id under the tree root (never the edited
// scene, so nothing is saved). Stops itself when the stream ends.
static String sound_preview_node_name(uint64_t entry_id) {
	return "BlastSoundPreview_" + String::num_uint64(entry_id);
}

void SoundMixer2D::preview_play(BulletSoundData2D *sound, const Vector2 &global_position, float volume_offset_db, bool use_position) {
	if (sound == nullptr) {
		return;
	}
	SceneTree *tree = SceneTree::get_singleton();
	if (tree == nullptr) {
		return;
	}
	Window *root = tree->get_root();
	if (root == nullptr) {
		return;
	}
	if (!sound->enabled) {
		UtilityFunctions::push_warning("BulletSoundData2D: the sound is disabled, nothing plays.");
		return;
	}
	if (!sound_has_audio(sound)) {
		if (WarnOnce2D::first(sound->get_instance_id(), SOUND_WARN_EMPTY_STREAM)) {
			UtilityFunctions::push_warning("BulletSoundData2D: streams is empty, the sound plays nothing.");
		}
		return;
	}
	// Sequence/shuffle cursors need a channel; preview shares the pick, not
	// the limits (it always plays the picked stream).
	Channel channel;
	const Ref<AudioStream> picked = pick_stream(sound, channel);
	if (picked.is_null()) {
		if (WarnOnce2D::first(sound->get_instance_id(), SOUND_WARN_EMPTY_STREAM)) {
			UtilityFunctions::push_warning("BulletSoundData2D: streams is empty, the sound plays nothing.");
		}
		return;
	}
	const String name = sound_preview_node_name(sound->get_instance_id());
	if (Node *old = root->get_node_or_null(NodePath(name)); old != nullptr) {
		// A second call stops the first synchronously: stop, detach and free
		// now (a queue_free would linger until frame end and force the new
		// player onto a suffixed name the lookup would miss). The player is
		// childless and only ever carries its own finished connection.
		if (AudioStreamPlayer2D *old_player = Object::cast_to<AudioStreamPlayer2D>(old); old_player != nullptr) {
			old_player->stop();
		}
		root->remove_child(old);
		memdelete(old);
	}
	AudioStreamPlayer2D *player = memnew(AudioStreamPlayer2D);
	player->set_name(name);
	player->set_max_polyphony(1);
	player->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
	apply_playback_mix(player, sound, picked, volume_offset_db, nullptr, !use_position);
	player->set_global_position(global_position);
	root->add_child(player);
	player->play();
	player->connect("finished", Callable(player, "queue_free"), Object::CONNECT_ONE_SHOT);
	// Looping entries never finish on their own: stop them after 10 s.
	// Every Godot sample format with a loop switch is covered (WAV modes,
	// OGG/MP3 loop flags); anything else looping runs until replaced.
	bool loops = false;
	if (AudioStreamWAV *wav = Object::cast_to<AudioStreamWAV>(picked.ptr()); wav != nullptr) {
		loops = wav->get_loop_mode() != AudioStreamWAV::LOOP_DISABLED;
	} else if (AudioStreamOggVorbis *ogg = Object::cast_to<AudioStreamOggVorbis>(picked.ptr()); ogg != nullptr) {
		loops = ogg->has_loop();
	} else if (AudioStreamMP3 *mp3 = Object::cast_to<AudioStreamMP3>(picked.ptr()); mp3 != nullptr) {
		loops = mp3->has_loop();
	}
	if (loops) {
		Ref<SceneTreeTimer> timer = tree->create_timer(10.0);
		if (timer.is_valid()) {
			timer->connect("timeout", Callable(player, "queue_free"), Object::CONNECT_ONE_SHOT);
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

Dictionary SoundMixer2D::explain_sound(BulletFactory2D &factory, const Ref<BulletSoundData2D> &sound, const Vector2 &event_pos, const StringName &listener_group, int volley_amount) {
	Dictionary out;
	out["audible"] = false;
	out["blocked_by"] = String("none");
	out["distance"] = 0.0;
	out["effective_max_distance"] = 0.0;
	out["zoom"] = 1.0;
	out["camera"] = String("none");
	out["in_camera_view"] = true;
	out["interval_open"] = true;
	out["trigger_chance"] = 1.0;
	out["chance"] = String("always");
	out["listener"] = Vector2();
	out["occluded"] = false;
	out["volume_base"] = 0.0;
	out["volley_amount"] = volley_amount;
	auto block = [&](const char *reason) {
		out["blocked_by"] = String(reason);
	};
	if (paused || factory.is_bullet_processing_paused()) {
		block("paused");
		return out;
	}
	if (Engine::get_singleton() != nullptr && Engine::get_singleton()->is_editor_hint()) {
		block("editor");
		return out;
	}
	if (sound.is_null()) {
		block("null");
		return out;
	}
	if (!sound->enabled) {
		block("disabled");
		return out;
	}
	if (!sound_has_audio(sound.ptr())) {
		block("streamless");
		return out;
	}
	out["trigger_chance"] = sound->trigger_chance;
	if (!Math::is_finite((double)sound->trigger_chance) || sound->trigger_chance <= 0.0) {
		out["chance"] = String("never");
		block("chance_zero");
		return out;
	}
	if (sound->trigger_chance < 1.0) {
		out["chance"] = String("rolling");
	}
	if ((sound->min_volley_amount > 0 && volley_amount < sound->min_volley_amount) || (sound->max_volley_amount > 0 && volley_amount > sound->max_volley_amount)) {
		block("amount_gate");
		return out;
	}
	SoundListenerSpec2D spec;
	if (!listener_group.is_empty()) {
		spec.kind = SoundListenerSpec2D::NODE_GROUP;
		spec.group = listener_group;
	}
	Vector2 listener_pos;
	uint64_t node_id = 0;
	bool is_point = false;
	const bool found = resolve_listener(factory, spec, event_pos, listener_pos, node_id, is_point);
	const Vector2 anchor = found ? listener_pos : godot_listener_pos(factory);
	out["listener"] = anchor;
	const double dist = sound_dist2(event_pos, anchor);
	out["distance"] = Math::sqrt(dist);
	Channel scratch;
	Camera2D *camera = resolve_entry_camera(factory, sound.ptr(), scratch);
	Viewport *vp = factory.get_viewport();
	if (camera != nullptr) {
		const Vector2 z = camera->get_zoom();
		if (Math::is_finite((double)z.x) && Math::is_finite((double)z.y) && z.x > 0.0 && z.y > 0.0) {
			out["zoom"] = MIN((double)z.x, (double)z.y);
		}
	}
	if (!sound->camera_path.is_empty()) {
		const bool pinned = !sound->camera_path.is_empty() && scratch.camera_id != 0 && scratch.camera_path == sound->camera_path;
		out["camera"] = String(pinned ? String("pinned") : (camera != nullptr ? String("viewport") : String("none")));
	} else {
		out["camera"] = String(camera != nullptr ? String("viewport") : String("none"));
	}
	const double max_dist = sound_effective_max_distance(sound.ptr(), camera);
	out["effective_max_distance"] = max_dist;
	const bool in_view = sound_in_camera_view(sound.ptr(), event_pos, camera, vp);
	out["in_camera_view"] = in_view;
	if (sound->positional && dist > max_dist * max_dist) {
		block("distance");
		return out;
	}
	if (!in_view) {
		block("camera_view");
		return out;
	}
	if (sound->min_interval_sec > 0.0 && Math::is_finite((double)sound->min_interval_sec)) {
		bool open = true;
		const auto it = channels.find(sound->get_instance_id());
		if (it != channels.end() && factory.get_graze_clock() - it->second.last_play_clock < sound->min_interval_sec) {
			open = false;
		}
		out["interval_open"] = open;
	}
	const float occ = occlusion_penalty_for2d(factory, sound.ptr(), anchor, event_pos);
	out["occluded"] = occ > 0.0f;
	float gain = 0.0f;
	if (Math::is_finite((double)sound->amount_gain_db) && sound->amount_gain_db > 0.0) {
		gain = (float)((double)sound->amount_gain_db * std::log10((double)MAX(1, volley_amount)));
	}
	out["volume_base"] = (float)sound->volume_db + gain + (float)zoom_gain_for2d(sound.ptr(), camera) - occ;
	out["audible"] = true;
	return out;
}

Array SoundMixer2D::debug_voices() const {
	Array out;
	for (size_t i = 0; i < voices.size(); ++i) {
		const Voice &voice = voices[i];
		Dictionary d;
		d["index"] = (int)i;
		d["busy"] = voice.busy;
		d["fading"] = voice.fading;
		d["sound"] = (int64_t)voice.sound_id;
		d["priority"] = voice.priority;
		if (voice.player != nullptr && ObjectDB::get_instance(ObjectID(voice.player->get_instance_id())) == voice.player) {
			d["position"] = voice.player->get_global_position();
			d["volume_db"] = voice.player->get_volume_db();
			d["pitch_scale"] = voice.player->get_pitch_scale();
			d["bus"] = voice.player->get_bus();
			const Ref<AudioStream> played = voice.player->get_stream();
			d["stream"] = played.is_valid() ? (int64_t)played->get_instance_id() : (int64_t)0;
			d["max_distance"] = voice.player->get_max_distance();
			d["attenuation"] = voice.player->get_attenuation();
			d["panning_strength"] = voice.player->get_panning_strength();
			d["area_mask"] = voice.player->get_area_mask();
			d["paused"] = voice.player->get_stream_paused();
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
