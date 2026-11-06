#include "data/bullet_sound_data2d.hpp"
#include "core/live_object2d.hpp"

#include "core/transform_math2d.hpp"
#include "factory/sound_mixer2d.hpp"

#include "core/assign_if_changed2d.hpp"
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream_player2d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

// pitch_scale / pitch_min / pitch_max stop here: Godot's audio mixer never
// finishes a looping stream played at ~1e8x speed (measured: fine at 1e7,
// frozen at 1e8, with plain AudioStreamPlayer2D nodes too), so a typo must
// not reach it. About 10 octaves; far beyond any audible use.
static constexpr double SOUND_PITCH_CEILING = 1000.0;

uint64_t BulletSoundData2D::mask_epoch = 0;

BulletSoundData2D::SoundTrigger BulletSoundData2D::get_trigger() const { return static_cast<SoundTrigger>(trigger); }
void BulletSoundData2D::set_trigger(SoundTrigger value) {
	if (value < SOUND_ON_SHOT || value > SOUND_ON_SPLIT) {
		UtilityFunctions::push_error("BulletSoundData2D: trigger must be 0 (On Shot), 1 (On Hit), 2 (On Destroy), 3 (On Bounce), 4 (On Lifetime Over), 5 (On Clear), 6 (On Homing Target Reached), 7 (On Graze), 8 (On Graze Exit), 9 (On Telegraph), 10 (On Flight), 11 (On Despawn) or 12 (On Split), keeping the old value.");
		return;
	}
	if (trigger == value) {
		return;
	}
	trigger = value;
	++mask_epoch;
	notify_property_list_changed(); // ring_index gates on it (_validate_property)
	emit_changed();
}

int BulletSoundData2D::get_ring_index() const { return ring_index; }
void BulletSoundData2D::set_ring_index(int value) {
	if (value < -1 || value > 3) {
		UtilityFunctions::push_error("BulletSoundData2D: ring_index must be -1 (any ring) or 0..3, keeping the old value.");
		return;
	}
	if (ring_index == value) {
		return;
	}
	if (!assign_if_changed2d(ring_index, value)) {
		return;
	}
	emit_changed();
}

int BulletSoundData2D::get_zone_index() const { return zone_index; }
void BulletSoundData2D::set_zone_index(int value) {
	if (value < -1 || value > 3) {
		UtilityFunctions::push_error("BulletSoundData2D: zone_index must be -1 (any zone) or 0..3, keeping the old value.");
		return;
	}
	if (zone_index == value) {
		return;
	}
	if (!assign_if_changed2d(zone_index, value)) {
		return;
	}
	emit_changed();
}

int BulletSoundData2D::get_min_volley_amount() const { return min_volley_amount; }
void BulletSoundData2D::set_min_volley_amount(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletSoundData2D: min_volley_amount must be >= 0, keeping the old value.");
		return;
	}
	if (min_volley_amount == value) {
		return;
	}
	if (!assign_if_changed2d(min_volley_amount, value)) {
		return;
	}
	emit_changed();
}

int BulletSoundData2D::get_max_volley_amount() const { return max_volley_amount; }
void BulletSoundData2D::set_max_volley_amount(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletSoundData2D: max_volley_amount must be >= 0, keeping the old value.");
		return;
	}
	if (max_volley_amount == value) {
		return;
	}
	if (!assign_if_changed2d(max_volley_amount, value)) {
		return;
	}
	emit_changed();
}

double BulletSoundData2D::get_amount_gain_db() const { return amount_gain_db; }
void BulletSoundData2D::set_amount_gain_db(double value) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletSoundData2D: amount_gain_db must be finite and >= 0, keeping the old value.");
		return;
	}
	if (amount_gain_db == value) {
		return;
	}
	if (!assign_if_changed2d(amount_gain_db, value)) {
		return;
	}
	emit_changed();
}

// The voice rides its bullet (SoundMix2D::follow).
bool BulletSoundData2D::get_follow_bullet() const { return follow; }

void BulletSoundData2D::set_follow_bullet(bool value) {
	if (!assign_if_changed2d(follow, value)) {
		return;
	}
	emit_changed();
}

void BulletSoundData2D::_validate_property(PropertyInfo &p_property) const {
	const String name = p_property.name;
	if (name == "ring_index" || name == "zone_index") {
		// Only graze triggers read them (volley sound_fire filters by ring
		// and zone); every other trigger ignores them, so hiding avoids
		// dead config.
		if (trigger != SOUND_ON_GRAZE && trigger != SOUND_ON_GRAZE_EXIT) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	validate_mix_property(p_property);
}

#define SOUND_DATA_CLASS BulletSoundData2D
#define SOUND_DATA_NAME "BulletSoundData2D"
#include "data/sound_mix_accessors2d.inc"
#undef SOUND_DATA_CLASS
#undef SOUND_DATA_NAME

void BulletSoundData2D::bind_own_trigger_properties() {
	ClassDB::bind_method(D_METHOD("get_trigger"), &BulletSoundData2D::get_trigger);
	ClassDB::bind_method(D_METHOD("set_trigger", "value"), &BulletSoundData2D::set_trigger);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "trigger", PROPERTY_HINT_ENUM, "On Shot,On Hit,On Destroy,On Bounce,On Lifetime Over,On Clear,On Homing Target Reached,On Graze,On Graze Exit,On Telegraph,On Flight,On Despawn,On Split"), "set_trigger", "get_trigger");

	ClassDB::bind_method(D_METHOD("get_ring_index"), &BulletSoundData2D::get_ring_index);
	ClassDB::bind_method(D_METHOD("set_ring_index", "value"), &BulletSoundData2D::set_ring_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ring_index", PROPERTY_HINT_RANGE, "-1,3,1"), "set_ring_index", "get_ring_index");

	ClassDB::bind_method(D_METHOD("get_zone_index"), &BulletSoundData2D::get_zone_index);
	ClassDB::bind_method(D_METHOD("set_zone_index", "value"), &BulletSoundData2D::set_zone_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "zone_index", PROPERTY_HINT_RANGE, "-1,3,1"), "set_zone_index", "get_zone_index");
}

void BulletSoundData2D::bind_own_limit_properties() {
	ClassDB::bind_method(D_METHOD("get_min_volley_amount"), &BulletSoundData2D::get_min_volley_amount);
	ClassDB::bind_method(D_METHOD("set_min_volley_amount", "value"), &BulletSoundData2D::set_min_volley_amount);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "min_volley_amount", PROPERTY_HINT_RANGE, "0,10000,1,or_greater"), "set_min_volley_amount", "get_min_volley_amount");

	ClassDB::bind_method(D_METHOD("get_max_volley_amount"), &BulletSoundData2D::get_max_volley_amount);
	ClassDB::bind_method(D_METHOD("set_max_volley_amount", "value"), &BulletSoundData2D::set_max_volley_amount);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_volley_amount", PROPERTY_HINT_RANGE, "0,10000,1,or_greater"), "set_max_volley_amount", "get_max_volley_amount");

	ClassDB::bind_method(D_METHOD("get_amount_gain_db"), &BulletSoundData2D::get_amount_gain_db);
	ClassDB::bind_method(D_METHOD("set_amount_gain_db", "value"), &BulletSoundData2D::set_amount_gain_db);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amount_gain_db", PROPERTY_HINT_RANGE, "0,24,0.01,or_greater,suffix:dB"), "set_amount_gain_db", "get_amount_gain_db");
}

void BulletSoundData2D::_bind_methods() {
#define SOUND_MIX_TRIGGER_BINDS() bind_own_trigger_properties();
#define SOUND_MIX_LIMIT_BINDS() bind_own_limit_properties();
#define SOUND_DATA_CLASS BulletSoundData2D
#include "data/sound_mix_binds2d.inc"
#undef SOUND_DATA_CLASS
#undef SOUND_MIX_TRIGGER_BINDS
#undef SOUND_MIX_LIMIT_BINDS

	ClassDB::bind_method(D_METHOD("get_follow_bullet"), &BulletSoundData2D::get_follow_bullet);
	ClassDB::bind_method(D_METHOD("set_follow_bullet", "value"), &BulletSoundData2D::set_follow_bullet);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "follow_bullet"), "set_follow_bullet", "get_follow_bullet");

	BIND_ENUM_CONSTANT(SOUND_ON_SHOT);
	BIND_ENUM_CONSTANT(SOUND_ON_HIT);
	BIND_ENUM_CONSTANT(SOUND_ON_DESTROY);
	BIND_ENUM_CONSTANT(SOUND_ON_BOUNCE);
	BIND_ENUM_CONSTANT(SOUND_ON_LIFETIME_OVER);
	BIND_ENUM_CONSTANT(SOUND_ON_CLEAR);
	BIND_ENUM_CONSTANT(SOUND_ON_HOMING_TARGET_REACHED);
	BIND_ENUM_CONSTANT(SOUND_ON_GRAZE);
	BIND_ENUM_CONSTANT(SOUND_ON_GRAZE_EXIT);
	BIND_ENUM_CONSTANT(SOUND_ON_TELEGRAPH);
	BIND_ENUM_CONSTANT(SOUND_ON_FLIGHT);
	BIND_ENUM_CONSTANT(SOUND_ON_DESPAWN);
	BIND_ENUM_CONSTANT(SOUND_ON_SPLIT);
	BIND_ENUM_CONSTANT(SOUND_LIMIT_REPLACE_OLDEST);
	BIND_ENUM_CONSTANT(SOUND_LIMIT_SKIP_NEW);
	BIND_ENUM_CONSTANT(STEAL_OLDEST);
	BIND_ENUM_CONSTANT(STEAL_QUIETEST);
	BIND_ENUM_CONSTANT(STREAM_RANDOM);
	BIND_ENUM_CONSTANT(STREAM_WEIGHTED);
	BIND_ENUM_CONSTANT(STREAM_SEQUENCE);
	BIND_ENUM_CONSTANT(STREAM_SHUFFLE);
	BIND_ENUM_CONSTANT(SOUND_AUDIBILITY_DISTANCE);
	BIND_ENUM_CONSTANT(SOUND_AUDIBILITY_CAMERA);
	BIND_ENUM_CONSTANT(SOUND_AUDIBILITY_DISTANCE_AND_CAMERA);
}
} //namespace BlastBullets2D
