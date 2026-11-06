#include "data/bullet_emitter_data2d.hpp"

#include "core/transform_math2d.hpp"

#include "core/assign_if_changed2d.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

bool BulletEmitterData2D::accept_seconds(double value, const char *property_name, bool strictly_positive) {
	// real_t-finite: the volley adds these to its float32 pose math.
	if (!is_finite_real2d(value) || (strictly_positive ? !(value > 0.0) : value < 0.0)) {
		UtilityFunctions::push_error(String("BulletEmitterData2D: ") + property_name + (strictly_positive ? " must be finite and > 0" : " must be finite and >= 0") + ", keeping the old value.");
		return false;
	}
	return true;
}

void BulletEmitterData2D::set_interval_sec(double value) {
	if (!accept_seconds(value, "interval_sec", true)) {
		return;
	}
	if (!assign_if_changed2d(interval_sec, value)) {
		return;
	}
	++rules_epoch;
	emit_changed();
}

void BulletEmitterData2D::set_start_delay_sec(double value) {
	if (!accept_seconds(value, "start_delay_sec", false)) {
		return;
	}
	if (!assign_if_changed2d(start_delay_sec, value)) {
		return;
	}
	++rules_epoch;
	emit_changed();
}

void BulletEmitterData2D::set_stagger_sec(double value) {
	if (!accept_seconds(value, "stagger_sec", false)) {
		return;
	}
	if (!assign_if_changed2d(stagger_sec, value)) {
		return;
	}
	++rules_epoch;
	emit_changed();
}

void BulletEmitterData2D::set_max_shots(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletEmitterData2D: max_shots must be >= 0 (0 = no limit), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(max_shots, value)) {
		return;
	}
	++rules_epoch;
	emit_changed();
}

void BulletEmitterData2D::set_chance(double value) {
	if (!Math::is_finite(value) || value < 0.0 || value > 1.0) {
		UtilityFunctions::push_error("BulletEmitterData2D: chance must be between 0 and 1, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(chance, value)) {
		return;
	}
	++rules_epoch;
	emit_changed();
}

void BulletEmitterData2D::set_seed(int64_t value) {
	if (!assign_if_changed2d(seed, value)) {
		return;
	}
	++rules_epoch;
	emit_changed();
}

void BulletEmitterData2D::set_aim(EmitterAim value) {
	if (value < EMITTER_AIM_HEADING || value > EMITTER_AIM_TEMPLATE) {
		UtilityFunctions::push_error("BulletEmitterData2D: aim must be 0 (Heading), 1 (World) or 2 (Template), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(aim, value)) {
		return;
	}
	++rules_epoch;
	emit_changed();
}

void BulletEmitterData2D::set_spin_per_shot_degrees(double value) {
	if (!is_finite_real2d(value)) {
		UtilityFunctions::push_error("BulletEmitterData2D: spin_per_shot_degrees must be finite, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(spin_per_shot_degrees, value)) {
		return;
	}
	++rules_epoch;
	emit_changed();
}

void BulletEmitterData2D::set_inherit_bullet_velocity(bool value) {
	if (!assign_if_changed2d(inherit_bullet_velocity, value)) {
		return;
	}
	++rules_epoch;
	emit_changed();
}

void BulletEmitterData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_interval_sec"), &BulletEmitterData2D::get_interval_sec);
	ClassDB::bind_method(D_METHOD("set_interval_sec", "value"), &BulletEmitterData2D::set_interval_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "interval_sec", PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater"), "set_interval_sec", "get_interval_sec");
	ClassDB::bind_method(D_METHOD("get_start_delay_sec"), &BulletEmitterData2D::get_start_delay_sec);
	ClassDB::bind_method(D_METHOD("set_start_delay_sec", "value"), &BulletEmitterData2D::set_start_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start_delay_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_start_delay_sec", "get_start_delay_sec");
	ClassDB::bind_method(D_METHOD("get_stagger_sec"), &BulletEmitterData2D::get_stagger_sec);
	ClassDB::bind_method(D_METHOD("set_stagger_sec", "value"), &BulletEmitterData2D::set_stagger_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stagger_sec", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater"), "set_stagger_sec", "get_stagger_sec");
	ClassDB::bind_method(D_METHOD("get_max_shots"), &BulletEmitterData2D::get_max_shots);
	ClassDB::bind_method(D_METHOD("set_max_shots", "value"), &BulletEmitterData2D::set_max_shots);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_shots", PROPERTY_HINT_RANGE, "0,100,1,or_greater"), "set_max_shots", "get_max_shots");
	ClassDB::bind_method(D_METHOD("get_chance"), &BulletEmitterData2D::get_chance);
	ClassDB::bind_method(D_METHOD("set_chance", "value"), &BulletEmitterData2D::set_chance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "chance", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_chance", "get_chance");
	ClassDB::bind_method(D_METHOD("get_seed"), &BulletEmitterData2D::get_seed);
	ClassDB::bind_method(D_METHOD("set_seed", "value"), &BulletEmitterData2D::set_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
	ClassDB::bind_method(D_METHOD("get_aim"), &BulletEmitterData2D::get_aim);
	ClassDB::bind_method(D_METHOD("set_aim", "value"), &BulletEmitterData2D::set_aim);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "aim", PROPERTY_HINT_ENUM, "Heading,World,Template"), "set_aim", "get_aim");
	ClassDB::bind_method(D_METHOD("get_spin_per_shot_degrees"), &BulletEmitterData2D::get_spin_per_shot_degrees);
	ClassDB::bind_method(D_METHOD("set_spin_per_shot_degrees", "value"), &BulletEmitterData2D::set_spin_per_shot_degrees);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_per_shot_degrees", PROPERTY_HINT_RANGE, "-360,360,0.1,or_less,or_greater"), "set_spin_per_shot_degrees", "get_spin_per_shot_degrees");
	ClassDB::bind_method(D_METHOD("get_inherit_bullet_velocity"), &BulletEmitterData2D::get_inherit_bullet_velocity);
	ClassDB::bind_method(D_METHOD("set_inherit_bullet_velocity", "value"), &BulletEmitterData2D::set_inherit_bullet_velocity);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "inherit_bullet_velocity"), "set_inherit_bullet_velocity", "get_inherit_bullet_velocity");

	BIND_ENUM_CONSTANT(EMITTER_AIM_HEADING);
	BIND_ENUM_CONSTANT(EMITTER_AIM_WORLD);
	BIND_ENUM_CONSTANT(EMITTER_AIM_TEMPLATE);
}
} //namespace BlastBullets2D
