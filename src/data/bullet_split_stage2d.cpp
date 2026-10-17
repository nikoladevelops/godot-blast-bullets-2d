#include "data/bullet_split_stage2d.hpp"

#include "patterns/pattern_inputs2d.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

void BulletSplitStage2D::store_flag(bool &field, bool value) {
	if (field == value) {
		return;
	}
	field = value;
	emit_changed();
}

void BulletSplitStage2D::store_amount(real_t &field, real_t value, const char *property_name, real_t min, real_t max) {
	if (!Math::is_finite(value) || value < min || (max >= 0.0 && value > max)) {
		// Bounds are whole numbers: printed without a fraction.
		const String range = max >= 0.0 ? String(" must be finite and between ") + String::num_int64((int64_t)min) + " and " + String::num_int64((int64_t)max) : String(" must be finite and >= ") + String::num_int64((int64_t)min);
		UtilityFunctions::push_error(String("BulletSplitStage2D: ") + property_name + range + ", keeping the old value.");
		return;
	}
	if (field == value) {
		return;
	}
	field = value;
	emit_changed();
}

void BulletSplitStage2D::set_count(int value) {
	const int limit = pattern_max_bullets();
	if (value < 1 || value > limit) {
		UtilityFunctions::push_error(String("BulletSplitStage2D: count must be between 1 and ") + String::num_int64(limit) + ", keeping the old value.");
		return;
	}
	if (count == value) {
		return;
	}
	count = value;
	emit_changed();
}

void BulletSplitStage2D::set_parent_survives(bool value) {
	if (parent_survives == value) {
		return;
	}
	parent_survives = value;
	notify_property_list_changed();
	emit_changed();
}

void BulletSplitStage2D::set_max_splits_per_bullet(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletSplitStage2D: max_splits_per_bullet must be >= 0 (0 = no limit), keeping the old value.");
		return;
	}
	if (max_splits_per_bullet == value) {
		return;
	}
	max_splits_per_bullet = value;
	emit_changed();
}

void BulletSplitStage2D::set_seed(int64_t value) {
	if (seed == value) {
		return;
	}
	seed = value;
	emit_changed();
}

void BulletSplitStage2D::set_direction_mode(DirectionMode value) {
	if (value < DIRECTION_FAN || value > DIRECTION_RANDOM_IN_SPREAD) {
		UtilityFunctions::push_error("BulletSplitStage2D: direction_mode must be 0 (Fan), 1 (Ring) or 2 (Random In Spread), keeping the old value.");
		return;
	}
	if (direction_mode == value) {
		return;
	}
	direction_mode = value;
	notify_property_list_changed();
	emit_changed();
}

void BulletSplitStage2D::set_angle_offset_degrees(real_t value) {
	if (!Math::is_finite(value)) {
		UtilityFunctions::push_error("BulletSplitStage2D: angle_offset_degrees must be finite, keeping the old value.");
		return;
	}
	if (angle_offset_degrees == value) {
		return;
	}
	angle_offset_degrees = value;
	emit_changed();
}

void BulletSplitStage2D::set_speed_mode(SpeedMode value) {
	if (value < SPEED_MULTIPLY_PARENT || value > SPEED_FROM_SPAWN_DATA) {
		UtilityFunctions::push_error("BulletSplitStage2D: speed_mode must be 0 (Multiply Parent), 1 (Fixed) or 2 (From Spawn Data), keeping the old value.");
		return;
	}
	if (speed_mode == value) {
		return;
	}
	speed_mode = value;
	notify_property_list_changed();
	emit_changed();
}

void BulletSplitStage2D::set_speed_distribution(SpeedDistribution value) {
	if (value < SPEED_RAMP || value > SPEED_RANDOM) {
		UtilityFunctions::push_error("BulletSplitStage2D: speed_distribution must be 0 (Ramp) or 1 (Random), keeping the old value.");
		return;
	}
	if (speed_distribution == value) {
		return;
	}
	speed_distribution = value;
	emit_changed();
}

void BulletSplitStage2D::set_scale_multiplier(real_t value) {
	if (!Math::is_finite(value) || value <= 0.0) {
		UtilityFunctions::push_error("BulletSplitStage2D: scale_multiplier must be finite and > 0, keeping the old value.");
		return;
	}
	if (scale_multiplier == value) {
		return;
	}
	scale_multiplier = value;
	emit_changed();
}

void BulletSplitStage2D::set_child_data(const Ref<BulletVolleyData2D> &value) {
	if (child_data == value) {
		return;
	}
	child_data = value;
	emit_changed();
}

real_t BulletSplitStage2D::child_angle_radians(int k, int children, double random_roll, double jitter_roll) const {
	const double spread = Math::deg_to_rad((double)spread_degrees);
	double angle = 0.0;
	switch (direction_mode) {
		case DIRECTION_RING:
			angle = children > 0 ? Math::TAU * (double)k / (double)children : 0.0;
			break;
		case DIRECTION_RANDOM_IN_SPREAD:
			angle = -0.5 * spread + spread * random_roll;
			break;
		case DIRECTION_FAN:
		default:
			angle = children > 1 ? -0.5 * spread + spread * (double)k / (double)(children - 1) : 0.0;
			break;
	}
	angle += Math::deg_to_rad((double)angle_offset_degrees);
	if (angle_jitter_degrees > 0.0) {
		angle += Math::deg_to_rad((double)angle_jitter_degrees) * (2.0 * jitter_roll - 1.0);
	}
	return (real_t)angle;
}

real_t BulletSplitStage2D::child_speed_multiplier(int k, int children, double random_roll) const {
	// Min/max are normalized here, never validated against each other (a
	// scene sets min before max).
	const double lo = Math::min(speed_multiplier_min, speed_multiplier_max);
	const double hi = Math::max(speed_multiplier_min, speed_multiplier_max);
	if (speed_distribution == SPEED_RANDOM) {
		return (real_t)(lo + (hi - lo) * random_roll);
	}
	return children > 1 ? (real_t)(lo + (hi - lo) * (double)k / (double)(children - 1)) : (real_t)lo;
}

void BulletSplitStage2D::_validate_property(PropertyInfo &p_property) const {
	const bool hide = (p_property.name == StringName("spread_degrees") && direction_mode == DIRECTION_RING) || (p_property.name == StringName("fixed_speed") && speed_mode != SPEED_FIXED) || (p_property.name == StringName("max_splits_per_bullet") && !parent_survives);
	if (hide) {
		p_property.usage &= ~PROPERTY_USAGE_EDITOR;
	}
}

void BulletSplitStage2D::_bind_methods() {
#define SPLIT_BIND(NAME, VARIANT, ...)                                                      \
	ClassDB::bind_method(D_METHOD("get_" #NAME), &BulletSplitStage2D::get_##NAME);          \
	ClassDB::bind_method(D_METHOD("set_" #NAME, "value"), &BulletSplitStage2D::set_##NAME); \
	ADD_PROPERTY(PropertyInfo(Variant::VARIANT, #NAME, ##__VA_ARGS__), "set_" #NAME, "get_" #NAME);

	ADD_GROUP("Triggers", "");
	SPLIT_BIND(split_on_hit, BOOL)
	SPLIT_BIND(split_on_lifetime_over, BOOL)
	SPLIT_BIND(split_after_sec, FLOAT, PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s")
	SPLIT_BIND(split_on_despawn, BOOL)
	SPLIT_BIND(split_on_clear, BOOL)
	SPLIT_BIND(split_on_bounce, BOOL)
	SPLIT_BIND(split_on_non_lethal_hit, BOOL)
	SPLIT_BIND(split_on_homing_reached, BOOL)
	SPLIT_BIND(split_on_graze, BOOL)

	ADD_GROUP("Children", "");
	SPLIT_BIND(count, INT, PROPERTY_HINT_RANGE, "1,64,1,or_greater")
	SPLIT_BIND(chance, FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01")
	SPLIT_BIND(seed, INT)

	ADD_GROUP("Direction", "");
	SPLIT_BIND(direction_mode, INT, PROPERTY_HINT_ENUM, "Fan:0,Ring:1,Random In Spread:2")
	SPLIT_BIND(spread_degrees, FLOAT, PROPERTY_HINT_RANGE, "0,360,0.1,degrees")
	SPLIT_BIND(angle_offset_degrees, FLOAT, PROPERTY_HINT_RANGE, "-180,180,0.1,or_less,or_greater,degrees")
	SPLIT_BIND(relative_to_parent_heading, BOOL)
	SPLIT_BIND(angle_jitter_degrees, FLOAT, PROPERTY_HINT_RANGE, "0,360,0.1,degrees")

	ADD_GROUP("Speed", "");
	SPLIT_BIND(speed_mode, INT, PROPERTY_HINT_ENUM, "Multiply Parent:0,Fixed:1,From Spawn Data:2")
	SPLIT_BIND(speed_multiplier_min, FLOAT, PROPERTY_HINT_RANGE, "0,4,0.01,or_greater")
	SPLIT_BIND(speed_multiplier_max, FLOAT, PROPERTY_HINT_RANGE, "0,4,0.01,or_greater")
	SPLIT_BIND(speed_distribution, INT, PROPERTY_HINT_ENUM, "Ramp:0,Random:1")
	SPLIT_BIND(fixed_speed, FLOAT, PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:px/s")

	ADD_GROUP("Placement", "");
	SPLIT_BIND(spawn_offset_px, FLOAT, PROPERTY_HINT_RANGE, "0,128,0.1,or_greater,suffix:px")
	SPLIT_BIND(scale_multiplier, FLOAT, PROPERTY_HINT_RANGE, "0.01,4,0.01,or_greater")
	SPLIT_BIND(child_data, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "BulletVolleyData2D")

	ADD_GROUP("Keep", "");
	SPLIT_BIND(keep_custom_data, BOOL)
	SPLIT_BIND(keep_collision_count, BOOL)
	SPLIT_BIND(keep_homing, BOOL)
	SPLIT_BIND(keep_graze, BOOL)
	SPLIT_BIND(keep_sound, BOOL)
	SPLIT_BIND(keep_flock, BOOL)
	SPLIT_BIND(inherit_parent_velocity, BOOL)

	ADD_GROUP("Parent", "");
	SPLIT_BIND(fire_death_effects, BOOL)
	SPLIT_BIND(parent_survives, BOOL)
	SPLIT_BIND(max_splits_per_bullet, INT, PROPERTY_HINT_RANGE, "0,16,1,or_greater")
#undef SPLIT_BIND

	ClassDB::bind_method(D_METHOD("child_angle_radians", "child_index", "children", "random_roll", "jitter_roll"), &BulletSplitStage2D::child_angle_radians);
	ClassDB::bind_method(D_METHOD("child_speed_multiplier", "child_index", "children", "random_roll"), &BulletSplitStage2D::child_speed_multiplier);

	BIND_ENUM_CONSTANT(DIRECTION_FAN);
	BIND_ENUM_CONSTANT(DIRECTION_RING);
	BIND_ENUM_CONSTANT(DIRECTION_RANDOM_IN_SPREAD);
	BIND_ENUM_CONSTANT(SPEED_MULTIPLY_PARENT);
	BIND_ENUM_CONSTANT(SPEED_FIXED);
	BIND_ENUM_CONSTANT(SPEED_FROM_SPAWN_DATA);
	BIND_ENUM_CONSTANT(SPEED_RAMP);
	BIND_ENUM_CONSTANT(SPEED_RANDOM);
}
} //namespace BlastBullets2D
