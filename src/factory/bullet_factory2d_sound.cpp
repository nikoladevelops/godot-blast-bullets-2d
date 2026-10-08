// Factory-owned sound triggers: user hatch (play_sound), voice pool cap,
// debug readouts. Volley and spawner trigger sites offer through sound_offer;
// the mixer plays the sweep's winners at the end of _physics_process.

#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

bool BulletFactory2D::sound_offer(const Ref<BulletSoundData2D> &sound, const Vector2 &event_pos, float volume_offset_db, const SoundListenerSpec2D &listener, int volley_amount, uint64_t follow_volley_id, int follow_life_id, int follow_bullet) {
	if (is_tearing_down) {
		return false;
	}
	return sound_mixer.offer(*this, sound, event_pos, volume_offset_db, listener, volley_amount, follow_volley_id, follow_life_id, follow_bullet);
}

bool BulletFactory2D::play_sound(const Ref<BulletSoundData2D> &sound, const Vector2 &global_position, const StringName &listener_group) {
	if (sound.is_null()) {
		UtilityFunctions::push_error("BulletFactory2D.play_sound: sound is null, nothing plays.");
		return false;
	}
	if (!sound->enabled) {
		return false;
	}
	if (!is_inside_tree() || is_tearing_down) {
		UtilityFunctions::push_error("BulletFactory2D.play_sound: the factory is outside the tree, the sound plays nothing.");
		return false;
	}
	if (!is_factory_processing_bullets) {
		return false;
	}
	SoundListenerSpec2D spec;
	if (!listener_group.is_empty()) {
		spec.kind = SoundListenerSpec2D::NODE_GROUP;
		spec.group = listener_group;
	}
	return sound_offer(sound, global_position, 0.0f, spec);
}

void BulletFactory2D::stop_sounds() {
	sound_mixer.stop_all();
}

int BulletFactory2D::get_sound_max_voices() const {
	return sound_mixer.max_voices_total;
}

void BulletFactory2D::set_sound_max_voices(int value) {
	if (value < 1 || value > 256) {
		UtilityFunctions::push_error("BulletFactory2D: sound_max_voices must be between 1 and 256, keeping the old value.");
		return;
	}
	sound_mixer.max_voices_total = value;
}

void BulletFactory2D::debug_set_sound_log_enabled(bool enabled) {
	sound_mixer.set_log_enabled(enabled);
}

Array BulletFactory2D::debug_get_sound_log() const {
	return sound_mixer.get_log();
}

void BulletFactory2D::debug_clear_sound_log() {
	sound_mixer.clear_log();
}

Array BulletFactory2D::debug_get_sound_voices() const {
	return sound_mixer.debug_voices();
}

Dictionary BulletFactory2D::debug_explain_sound(const Ref<BulletSoundData2D> &sound, const Vector2 &global_position, const StringName &listener_group, int volley_amount) {
	return sound_mixer.explain_sound(*this, sound, global_position, listener_group, volley_amount);
}

Dictionary BulletFactory2D::debug_get_sound_stats() const {
	return sound_mixer.debug_stats();
}

void BulletFactory2D::debug_stop_sound_voices() {
	sound_mixer.stop_all();
}

} // namespace BlastBullets2D
