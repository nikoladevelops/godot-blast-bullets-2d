// Volley sound state: arming (sound_set_effects for factory volleys,
// sound_arm_from_spawner for spawner volleys) and pool-time release. The
// trigger sites (collision, bounce, lifetime, graze, homing, spawner shots)
// offer through sound_fire in bullet_volley2d_sound.cpp's second half.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

static bool sound_collect_entries(const Array &effects, std::vector<Ref<BulletSoundData2D>> &r_entries, uint32_t &r_mask, const char *caller) {
	r_entries.clear();
	r_mask = 0;
	for (int i = 0; i < effects.size(); ++i) {
		const Variant &entry = effects[i];
		if (entry.get_type() == Variant::NIL) {
			r_entries.push_back(Ref<BulletSoundData2D>());
			continue;
		}
		BulletSoundData2D *sound = Object::cast_to<BulletSoundData2D>(entry.get_type() == Variant::OBJECT ? (Object *)entry : nullptr);
		if (sound == nullptr) {
			UtilityFunctions::push_error(String(caller) + ": entry " + String::num_int64(i) + " is not a BulletSoundData2D, nothing changed.");
			return false;
		}
		r_entries.push_back(Ref<BulletSoundData2D>(sound));
		const int trigger = sound->get_trigger();
		if (sound->get_enabled() && trigger >= 0 && trigger < 32) {
			r_mask |= (1u << trigger);
		}
	}
	return true;
}

bool BulletVolley2D::sound_set_effects(const Array &effects, const StringName &listener_group) {
	if (reject_pooled_handle("sound_set_effects")) {
		return false;
	}
	std::vector<Ref<BulletSoundData2D>> entries;
	uint32_t mask = 0;
	if (!sound_collect_entries(effects, entries, mask, "sound_set_effects")) {
		return false;
	}
	sound_release();
	sound_effects = entries;
	sound_trigger_mask = mask;
	sound_listener_group = listener_group;
	return true;
}

bool BulletVolley2D::sound_arm_from_spawner(const Array &effects, const std::shared_ptr<GrazeDetector2D> &detector, float volume_offset_db) {
	if (detector == nullptr) {
		return false;
	}
	std::vector<Ref<BulletSoundData2D>> entries;
	uint32_t mask = 0;
	if (!sound_collect_entries(effects, entries, mask, "sound_arm_from_spawner")) {
		return false;
	}
	sound_release();
	sound_effects = entries;
	sound_trigger_mask = mask;
	// Shared: listener edits and refresh_sound_listeners() reach it in flight.
	sound_detector = detector;
	sound_volume_offset_db = volume_offset_db;
	return true;
}

Array BulletVolley2D::sound_get_effects() const {
	Array out;
	for (const Ref<BulletSoundData2D> &entry : sound_effects) {
		// A plain null for an empty slot (not a null object).
		out.push_back(entry.is_valid() ? Variant(entry) : Variant());
	}
	return out;
}

StringName BulletVolley2D::sound_get_listener_group() const {
	return sound_listener_group;
}

void BulletVolley2D::sound_release() {
	sound_effects.clear();
	sound_trigger_mask = 0;
	sound_detector.reset();
	sound_listener_group = StringName();
	sound_volume_offset_db = 0.0f;
}

} // namespace BlastBullets2D
