#include "data/laser_sound_data2d.hpp"

#include "core/assign_if_changed2d.hpp"
#include "core/live_object2d.hpp"
#include "core/transform_math2d.hpp"
#include "factory/sound_mixer2d.hpp"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream_player2d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

// Same ceiling as every sound resource (see bullet_sound_data2d.cpp): a
// looping stream never finishes at ~1e8x speed.
static constexpr double SOUND_PITCH_CEILING = 1000.0;

uint64_t LaserSoundData2D::mask_epoch = 0;

LaserSoundData2D::LaserSoundTrigger LaserSoundData2D::get_trigger() const { return static_cast<LaserSoundTrigger>(trigger); }
void LaserSoundData2D::set_trigger(LaserSoundTrigger value) {
	if (value < LASER_SOUND_ON_SHOT || value > LASER_SOUND_ON_DESPAWN) {
		UtilityFunctions::push_error("LaserSoundData2D: trigger must be 0 (On Shot), 1 (On Telegraph), 2 (On Charge), 3 (On Fire), 4 (On Active), 5 (On Hit), 6 (On Pierce Start), 7 (On Pierce End), 8 (On Wall Hit), 9 (On Graze), 10 (On Graze Exit), 11 (On Finish), 12 (On Ended Early) or 13 (On Despawn), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(trigger, (int)value)) {
		return;
	}
	++mask_epoch;
	notify_property_list_changed(); // ring_index gates on it (_validate_property)
	emit_changed();
}

int LaserSoundData2D::get_ring_index() const { return ring_index; }
void LaserSoundData2D::set_ring_index(int value) {
	if (value < -1 || value > 3) {
		UtilityFunctions::push_error("LaserSoundData2D: ring_index must be -1 (any ring) or 0..3, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(ring_index, value)) {
		return;
	}
	emit_changed();
}

int LaserSoundData2D::get_zone_index() const { return zone_index; }
void LaserSoundData2D::set_zone_index(int value) {
	if (value < -1 || value > 3) {
		UtilityFunctions::push_error("LaserSoundData2D: zone_index must be -1 (any zone) or 0..3, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(zone_index, value)) {
		return;
	}
	emit_changed();
}

// The voice rides its beam (SoundMix2D::follow).
bool LaserSoundData2D::get_follow_beam() const { return follow; }
void LaserSoundData2D::set_follow_beam(bool value) {
	if (!assign_if_changed2d(follow, value)) {
		return;
	}
	emit_changed();
}

LaserSoundData2D::FollowPoint LaserSoundData2D::get_follow_point() const { return static_cast<FollowPoint>(follow_point); }
void LaserSoundData2D::set_follow_point(FollowPoint value) {
	if (value < FOLLOW_TIP || value > FOLLOW_MUZZLE) {
		UtilityFunctions::push_error("LaserSoundData2D: follow_point must be 0 (Tip) or 1 (Muzzle), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(follow_point, (int)value)) {
		return;
	}
	emit_changed();
}

void LaserSoundData2D::_validate_property(PropertyInfo &p_property) const {
	const String name = p_property.name;
	if (name == "ring_index" || name == "zone_index") {
		// Only the graze triggers read them.
		if (trigger != LASER_SOUND_ON_GRAZE && trigger != LASER_SOUND_ON_GRAZE_EXIT) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	validate_mix_property(p_property);
}

#define SOUND_DATA_CLASS LaserSoundData2D
#define SOUND_DATA_NAME "LaserSoundData2D"
#include "data/sound_mix_accessors2d.inc"
#undef SOUND_DATA_CLASS
#undef SOUND_DATA_NAME

void LaserSoundData2D::bind_own_trigger_properties() {
	ClassDB::bind_method(D_METHOD("get_trigger"), &LaserSoundData2D::get_trigger);
	ClassDB::bind_method(D_METHOD("set_trigger", "value"), &LaserSoundData2D::set_trigger);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "trigger", PROPERTY_HINT_ENUM, "On Shot,On Telegraph,On Charge,On Fire,On Active,On Hit,On Pierce Start,On Pierce End,On Wall Hit,On Graze,On Graze Exit,On Finish,On Ended Early,On Despawn"), "set_trigger", "get_trigger");

	ClassDB::bind_method(D_METHOD("get_ring_index"), &LaserSoundData2D::get_ring_index);
	ClassDB::bind_method(D_METHOD("set_ring_index", "value"), &LaserSoundData2D::set_ring_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ring_index", PROPERTY_HINT_RANGE, "-1,3,1"), "set_ring_index", "get_ring_index");

	ClassDB::bind_method(D_METHOD("get_zone_index"), &LaserSoundData2D::get_zone_index);
	ClassDB::bind_method(D_METHOD("set_zone_index", "value"), &LaserSoundData2D::set_zone_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "zone_index", PROPERTY_HINT_RANGE, "-1,3,1"), "set_zone_index", "get_zone_index");
}

void LaserSoundData2D::_bind_methods() {
#define SOUND_MIX_TRIGGER_BINDS() bind_own_trigger_properties();
#define SOUND_MIX_LIMIT_BINDS()
#define SOUND_DATA_CLASS LaserSoundData2D
#include "data/sound_mix_binds2d.inc"
#undef SOUND_DATA_CLASS
#undef SOUND_MIX_TRIGGER_BINDS
#undef SOUND_MIX_LIMIT_BINDS

	ClassDB::bind_method(D_METHOD("get_follow_beam"), &LaserSoundData2D::get_follow_beam);
	ClassDB::bind_method(D_METHOD("set_follow_beam", "value"), &LaserSoundData2D::set_follow_beam);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "follow_beam"), "set_follow_beam", "get_follow_beam");
	ClassDB::bind_method(D_METHOD("get_follow_point"), &LaserSoundData2D::get_follow_point);
	ClassDB::bind_method(D_METHOD("set_follow_point", "value"), &LaserSoundData2D::set_follow_point);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "follow_point", PROPERTY_HINT_ENUM, "Tip,Muzzle"), "set_follow_point", "get_follow_point");

	BIND_ENUM_CONSTANT(LASER_SOUND_ON_SHOT);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_TELEGRAPH);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_CHARGE);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_FIRE);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_ACTIVE);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_HIT);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_PIERCE_START);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_PIERCE_END);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_WALL_HIT);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_GRAZE);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_GRAZE_EXIT);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_FINISH);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_ENDED_EARLY);
	BIND_ENUM_CONSTANT(LASER_SOUND_ON_DESPAWN);
	BIND_ENUM_CONSTANT(FOLLOW_TIP);
	BIND_ENUM_CONSTANT(FOLLOW_MUZZLE);
	// The shared mix enums (typed as BulletSoundData2D's in the method
	// signatures, where the C++ enums are registered) are spelled on this
	// class too, so a laser script never names a bullet class.
	const StringName cls = get_class_static();
	ClassDB::bind_integer_constant(cls, "SoundLimitMode", "SOUND_LIMIT_REPLACE_OLDEST", SOUND_LIMIT_REPLACE_OLDEST);
	ClassDB::bind_integer_constant(cls, "SoundLimitMode", "SOUND_LIMIT_SKIP_NEW", SOUND_LIMIT_SKIP_NEW);
	ClassDB::bind_integer_constant(cls, "StealMode", "STEAL_OLDEST", STEAL_OLDEST);
	ClassDB::bind_integer_constant(cls, "StealMode", "STEAL_QUIETEST", STEAL_QUIETEST);
	ClassDB::bind_integer_constant(cls, "StreamMode", "STREAM_RANDOM", STREAM_RANDOM);
	ClassDB::bind_integer_constant(cls, "StreamMode", "STREAM_WEIGHTED", STREAM_WEIGHTED);
	ClassDB::bind_integer_constant(cls, "StreamMode", "STREAM_SEQUENCE", STREAM_SEQUENCE);
	ClassDB::bind_integer_constant(cls, "StreamMode", "STREAM_SHUFFLE", STREAM_SHUFFLE);
	ClassDB::bind_integer_constant(cls, "AudibilityMode", "SOUND_AUDIBILITY_DISTANCE", SOUND_AUDIBILITY_DISTANCE);
	ClassDB::bind_integer_constant(cls, "AudibilityMode", "SOUND_AUDIBILITY_CAMERA", SOUND_AUDIBILITY_CAMERA);
	ClassDB::bind_integer_constant(cls, "AudibilityMode", "SOUND_AUDIBILITY_DISTANCE_AND_CAMERA", SOUND_AUDIBILITY_DISTANCE_AND_CAMERA);
}

} // namespace BlastBullets2D
