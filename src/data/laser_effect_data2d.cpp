#include "data/laser_effect_data2d.hpp"

#include "core/assign_if_changed2d.hpp"
#include "data/laser_sound_data2d.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

// One event id space for every laser event (the sound and the effect sites
// share LaserVolley2D::fire_event).
static_assert((int)LaserEffectData2D::LASER_EFFECT_ON_CHARGE == (int)LaserSoundData2D::LASER_SOUND_ON_CHARGE, "laser event ids");
static_assert((int)LaserEffectData2D::LASER_EFFECT_ON_FIRE == (int)LaserSoundData2D::LASER_SOUND_ON_FIRE, "laser event ids");
static_assert((int)LaserEffectData2D::LASER_EFFECT_ON_ACTIVE == (int)LaserSoundData2D::LASER_SOUND_ON_ACTIVE, "laser event ids");
static_assert((int)LaserEffectData2D::LASER_EFFECT_ON_HIT == (int)LaserSoundData2D::LASER_SOUND_ON_HIT, "laser event ids");
static_assert((int)LaserEffectData2D::LASER_EFFECT_ON_PIERCE_START == (int)LaserSoundData2D::LASER_SOUND_ON_PIERCE_START, "laser event ids");
static_assert((int)LaserEffectData2D::LASER_EFFECT_ON_PIERCE_END == (int)LaserSoundData2D::LASER_SOUND_ON_PIERCE_END, "laser event ids");
static_assert((int)LaserEffectData2D::LASER_EFFECT_ON_WALL_HIT == (int)LaserSoundData2D::LASER_SOUND_ON_WALL_HIT, "laser event ids");
static_assert((int)LaserEffectData2D::LASER_EFFECT_ON_GRAZE == (int)LaserSoundData2D::LASER_SOUND_ON_GRAZE, "laser event ids");
static_assert((int)LaserEffectData2D::LASER_EFFECT_ON_GRAZE_EXIT == (int)LaserSoundData2D::LASER_SOUND_ON_GRAZE_EXIT, "laser event ids");
static_assert((int)LaserEffectData2D::LASER_EFFECT_ON_FINISH == (int)LaserSoundData2D::LASER_SOUND_ON_FINISH, "laser event ids");
static_assert((int)LaserEffectData2D::LASER_EFFECT_ON_ENDED_EARLY == (int)LaserSoundData2D::LASER_SOUND_ON_ENDED_EARLY, "laser event ids");
static_assert((int)LaserEffectData2D::LASER_EFFECT_ON_DESPAWN == (int)LaserSoundData2D::LASER_SOUND_ON_DESPAWN, "laser event ids");

void LaserEffectData2D::set_trigger(LaserEffectTrigger value) {
	if (value < LASER_EFFECT_ON_CHARGE || value > LASER_EFFECT_ON_DESPAWN) {
		UtilityFunctions::push_error("LaserEffectData2D: trigger must be 2 (On Charge), 3 (On Fire), 4 (On Active), 5 (On Hit), 6 (On Pierce Start), 7 (On Pierce End), 8 (On Wall Hit), 9 (On Graze), 10 (On Graze Exit), 11 (On Finish), 12 (On Ended Early) or 13 (On Despawn), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(trigger, (int)value)) {
		return;
	}
	emit_changed();
}

void LaserEffectData2D::set_at(EffectPoint value) {
	if (value < AT_EVENT || value > AT_TIP) {
		UtilityFunctions::push_error("LaserEffectData2D: at must be 0 (Event), 1 (Muzzle) or 2 (Tip), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(at, (int)value)) {
		return;
	}
	emit_changed();
}

void LaserEffectData2D::set_effect(const Ref<BulletEffectLayerData2D> &value) {
	if (value.is_valid() && value->trigger == BulletEffectLayerData2D::EFFECT_TRAIL_FOLLOW) {
		UtilityFunctions::push_error("LaserEffectData2D: effect must be a one-shot layer (a trail follows a bullet), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(effect, value)) {
		return;
	}
	emit_changed();
}

void LaserEffectData2D::set_align_to_beam(bool value) {
	if (!assign_if_changed2d(align_to_beam, value)) {
		return;
	}
	emit_changed();
}

void LaserEffectData2D::set_enabled(bool value) {
	if (!assign_if_changed2d(enabled, value)) {
		return;
	}
	emit_changed();
}

void LaserEffectData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_enabled"), &LaserEffectData2D::get_enabled);
	ClassDB::bind_method(D_METHOD("set_enabled", "value"), &LaserEffectData2D::set_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");
	ClassDB::bind_method(D_METHOD("get_trigger"), &LaserEffectData2D::get_trigger);
	ClassDB::bind_method(D_METHOD("set_trigger", "value"), &LaserEffectData2D::set_trigger);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "trigger", PROPERTY_HINT_ENUM, "On Charge:2,On Fire:3,On Active:4,On Hit:5,On Pierce Start:6,On Pierce End:7,On Wall Hit:8,On Graze:9,On Graze Exit:10,On Finish:11,On Ended Early:12,On Despawn:13"), "set_trigger", "get_trigger");
	ClassDB::bind_method(D_METHOD("get_at"), &LaserEffectData2D::get_at);
	ClassDB::bind_method(D_METHOD("set_at", "value"), &LaserEffectData2D::set_at);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "at", PROPERTY_HINT_ENUM, "Event,Muzzle,Tip"), "set_at", "get_at");
	ClassDB::bind_method(D_METHOD("get_effect"), &LaserEffectData2D::get_effect);
	ClassDB::bind_method(D_METHOD("set_effect", "value"), &LaserEffectData2D::set_effect);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "BulletEffectLayerData2D"), "set_effect", "get_effect");
	ClassDB::bind_method(D_METHOD("get_align_to_beam"), &LaserEffectData2D::get_align_to_beam);
	ClassDB::bind_method(D_METHOD("set_align_to_beam", "value"), &LaserEffectData2D::set_align_to_beam);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "align_to_beam"), "set_align_to_beam", "get_align_to_beam");

	BIND_ENUM_CONSTANT(LASER_EFFECT_ON_CHARGE);
	BIND_ENUM_CONSTANT(LASER_EFFECT_ON_FIRE);
	BIND_ENUM_CONSTANT(LASER_EFFECT_ON_ACTIVE);
	BIND_ENUM_CONSTANT(LASER_EFFECT_ON_HIT);
	BIND_ENUM_CONSTANT(LASER_EFFECT_ON_PIERCE_START);
	BIND_ENUM_CONSTANT(LASER_EFFECT_ON_PIERCE_END);
	BIND_ENUM_CONSTANT(LASER_EFFECT_ON_WALL_HIT);
	BIND_ENUM_CONSTANT(LASER_EFFECT_ON_GRAZE);
	BIND_ENUM_CONSTANT(LASER_EFFECT_ON_GRAZE_EXIT);
	BIND_ENUM_CONSTANT(LASER_EFFECT_ON_FINISH);
	BIND_ENUM_CONSTANT(LASER_EFFECT_ON_ENDED_EARLY);
	BIND_ENUM_CONSTANT(LASER_EFFECT_ON_DESPAWN);
	BIND_ENUM_CONSTANT(AT_EVENT);
	BIND_ENUM_CONSTANT(AT_MUZZLE);
	BIND_ENUM_CONSTANT(AT_TIP);
}

} // namespace BlastBullets2D
