#include "data/laser_data2d.hpp"

#include "core/transform_math2d.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

namespace {

bool color_ok2d(const Color &c) {
	return Math::is_finite(c.r) && Math::is_finite(c.g) && Math::is_finite(c.b) && Math::is_finite(c.a);
}

// The behavior knobs a preset writes (never the looks).
struct LaserPresetRow {
	LaserData2D::LaserType type;
	double length, width, extend_speed, charge, grow, active, fade;
	LaserData2D::LaserAim aim;
	double turn_rate, sweep_speed, sweep_arc, speed, wave_amplitude;
	int64_t wall_mask;
	int reflections, pierce_limit;
	double hit_interval;
};

const LaserPresetRow LASER_PRESETS[] = {
	// Instant Beam: fires at once and hits on its first tick.
	{ LaserData2D::LASER_BEAM, 600.0, 12.0, 0.0, 0.0, 0.05, 0.5, 0.15, LaserData2D::LASER_AIM_PATTERN, 90.0, 0.0, 0.0, 600.0, 0.0, 0, 0, 0, 0.1 },
	// Charged Sweep: a long warning, then a beam swinging 90 degrees.
	{ LaserData2D::LASER_BEAM, 800.0, 16.0, 0.0, 1.0, 0.15, 2.0, 0.3, LaserData2D::LASER_AIM_PATTERN, 90.0, 45.0, 90.0, 600.0, 0.0, 0, 0, 0, 0.1 },
	// Wall Beam: grows from the muzzle and stops at walls (layer 1).
	{ LaserData2D::LASER_BEAM, 1200.0, 12.0, 1500.0, 0.5, 0.1, 1.5, 0.2, LaserData2D::LASER_AIM_PATTERN, 90.0, 0.0, 0.0, 600.0, 0.0, 1, 0, 0, 0.1 },
	// Reflecting Beam: like the wall beam, bouncing off three walls.
	{ LaserData2D::LASER_BEAM, 1600.0, 10.0, 2000.0, 0.5, 0.1, 1.5, 0.2, LaserData2D::LASER_AIM_PATTERN, 90.0, 0.0, 0.0, 600.0, 0.0, 1, 3, 0, 0.1 },
	// Bolt: a short fast segment that stops at the first target.
	{ LaserData2D::LASER_BOLT, 80.0, 8.0, 0.0, 0.0, 0.0, 2.0, 0.1, LaserData2D::LASER_AIM_PATTERN, 90.0, 0.0, 0.0, 900.0, 0.0, 0, 0, 1, 0.0 },
	// Homing Curvy: a snake chasing its target.
	{ LaserData2D::LASER_CURVY, 240.0, 10.0, 0.0, 0.0, 0.0, 3.0, 0.3, LaserData2D::LASER_AIM_TRACK, 180.0, 0.0, 0.0, 500.0, 0.0, 0, 0, 1, 0.0 },
	// Lock-On: warns, then ends right on its target.
	{ LaserData2D::LASER_BEAM, 900.0, 10.0, 0.0, 0.6, 0.1, 1.0, 0.2, LaserData2D::LASER_AIM_LOCK_ON, 90.0, 0.0, 0.0, 600.0, 0.0, 0, 0, 0, 0.1 },
};

} // namespace

void LaserData2D::store(double &field, double value) {
	field = value;
	emit_changed();
}

// Reject-and-keep setters: NAME, the accepted condition, the wording.
#define LASER_DOUBLE_SETTER(NAME, CONDITION, TEXT)                                                                      \
	void LaserData2D::set_##NAME(double value) {                                                                        \
		if (!is_finite_real2d(value) || !(CONDITION)) {                                                                 \
			UtilityFunctions::push_error("LaserData2D: " #NAME " must be finite and " TEXT ", keeping the old value."); \
			return;                                                                                                     \
		}                                                                                                               \
		store(NAME, value);                                                                                             \
	}

LASER_DOUBLE_SETTER(length, value > 0.0, "> 0")
LASER_DOUBLE_SETTER(width, value > 0.0, "> 0")
LASER_DOUBLE_SETTER(extend_speed, value >= 0.0, ">= 0 (0 = full length at once)")
LASER_DOUBLE_SETTER(charge_sec, value >= 0.0, ">= 0")
LASER_DOUBLE_SETTER(grow_sec, value >= 0.0, ">= 0")
LASER_DOUBLE_SETTER(active_sec, value >= 0.0, ">= 0 (0 = until ended)")
LASER_DOUBLE_SETTER(fade_sec, value >= 0.0, ">= 0")
LASER_DOUBLE_SETTER(warning_width, value >= 0.0, ">= 0")
LASER_DOUBLE_SETTER(turn_rate_degrees, value >= 0.0, ">= 0 (0 = snaps)")
LASER_DOUBLE_SETTER(sweep_speed_degrees, true, "a number")
LASER_DOUBLE_SETTER(sweep_arc_degrees, value >= 0.0, ">= 0 (0 = keeps turning)")
LASER_DOUBLE_SETTER(speed, value >= 0.0, ">= 0")
LASER_DOUBLE_SETTER(wave_amplitude, value >= 0.0, ">= 0")
LASER_DOUBLE_SETTER(wave_frequency, value >= 0.0, ">= 0")
LASER_DOUBLE_SETTER(hit_interval_sec, value >= 0.0, ">= 0 (0 = one hit per contact)")
LASER_DOUBLE_SETTER(body_scroll_speed, true, "a number")
LASER_DOUBLE_SETTER(effects_scale, value > 0.0, "> 0")

#undef LASER_DOUBLE_SETTER

void LaserData2D::set_type(LaserType value) {
	if (value < LASER_BEAM || value > LASER_CURVY) {
		UtilityFunctions::push_error("LaserData2D: type must be 0 (Beam), 1 (Bolt) or 2 (Curvy), keeping the old value.");
		return;
	}
	type = value;
	notify_property_list_changed();
	emit_changed();
}

void LaserData2D::set_aim(LaserAim value) {
	if (value < LASER_AIM_PATTERN || value > LASER_AIM_LOCK_ON) {
		UtilityFunctions::push_error("LaserData2D: aim must be 0 (Pattern), 1 (Track) or 2 (Lock On), keeping the old value.");
		return;
	}
	aim = value;
	notify_property_list_changed();
	emit_changed();
}

void LaserData2D::set_body_mode(BodyMode value) {
	if (value < BODY_STRETCH || value > BODY_TILE) {
		UtilityFunctions::push_error("LaserData2D: body_mode must be 0 (Stretch) or 1 (Tile), keeping the old value.");
		return;
	}
	body_mode = value;
	notify_property_list_changed();
	emit_changed();
}

void LaserData2D::set_warning_color(const Color &value) {
	if (!color_ok2d(value)) {
		UtilityFunctions::push_error("LaserData2D: warning_color must be finite, keeping the old value.");
		return;
	}
	warning_color = value;
	emit_changed();
}

void LaserData2D::set_modulate(const Color &value) {
	if (!color_ok2d(value)) {
		UtilityFunctions::push_error("LaserData2D: modulate must be finite, keeping the old value.");
		return;
	}
	modulate = value;
	emit_changed();
}

void LaserData2D::set_hit_while_growing(bool value) {
	hit_while_growing = value;
	emit_changed();
}

void LaserData2D::set_keep_tracking_when_active(bool value) {
	keep_tracking_when_active = value;
	emit_changed();
}

void LaserData2D::set_follow_spawner(bool value) {
	follow_spawner = value;
	emit_changed();
}

void LaserData2D::set_wall_mask(int64_t value) {
	if (value < 0 || value > 0xFFFFFFFFll) {
		UtilityFunctions::push_error("LaserData2D: wall_mask must be a 32-bit layer mask (0 = no walls), keeping the old value.");
		return;
	}
	wall_mask = value;
	notify_property_list_changed(); // reflections gate on it
	emit_changed();
}

void LaserData2D::set_reflections(int value) {
	if (value < 0 || value > 256) {
		UtilityFunctions::push_error("LaserData2D: reflections must be between 0 and 256 (each one is a ray per tick), keeping the old value.");
		return;
	}
	reflections = value;
	emit_changed();
}

void LaserData2D::set_collision_mask(int64_t value) {
	if (value < 0 || value > 0xFFFFFFFFll) {
		UtilityFunctions::push_error("LaserData2D: collision_mask must be a 32-bit layer mask, keeping the old value.");
		return;
	}
	collision_mask = value;
	emit_changed();
}

void LaserData2D::set_collide_with_bodies(bool value) {
	collide_with_bodies = value;
	emit_changed();
}

void LaserData2D::set_collide_with_areas(bool value) {
	collide_with_areas = value;
	emit_changed();
}

void LaserData2D::set_pierce_limit(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("LaserData2D: pierce_limit must be >= 0 (0 = passes through every target), keeping the old value.");
		return;
	}
	pierce_limit = value;
	emit_changed();
}

void LaserData2D::set_max_hits_per_beam(int value) {
	if (value < 1 || value > 4096) {
		UtilityFunctions::push_error("LaserData2D: max_hits_per_beam must be between 1 and 4096, keeping the old value.");
		return;
	}
	max_hits_per_beam = value;
	emit_changed();
}

void LaserData2D::set_color_ramp(const Ref<Gradient> &value) {
	color_ramp = value;
	emit_changed();
}

#define LASER_FRAMES_SETTER(NAME)                                  \
	void LaserData2D::set_##NAME(const Ref<SpriteFrames> &value) { \
		NAME = value;                                              \
		emit_changed();                                            \
	}

LASER_FRAMES_SETTER(body_frames)
LASER_FRAMES_SETTER(charge_frames)
LASER_FRAMES_SETTER(muzzle_frames)
LASER_FRAMES_SETTER(tip_frames)
LASER_FRAMES_SETTER(pierce_frames)
LASER_FRAMES_SETTER(wall_impact_frames)

#undef LASER_FRAMES_SETTER

void LaserData2D::set_material(const Ref<Material> &value) {
	material = value;
	emit_changed();
}

void LaserData2D::set_z_index(int value) {
	if (value < -4096 || value > 4096) {
		UtilityFunctions::push_error("LaserData2D: z_index must be between -4096 and 4096, keeping the old value.");
		return;
	}
	z_index = value;
	emit_changed();
}

void LaserData2D::apply_preset(Preset preset) {
	if (preset == PRESET_CUSTOM) {
		return; // the editor's "Custom" entry: nothing to write
	}
	if (preset < PRESET_INSTANT_BEAM || preset > PRESET_LOCK_ON) {
		UtilityFunctions::push_error("LaserData2D: preset must be one of the Preset values, nothing changed.");
		return;
	}
	const LaserPresetRow &r = LASER_PRESETS[preset - PRESET_INSTANT_BEAM];
	type = r.type;
	length = r.length;
	width = r.width;
	extend_speed = r.extend_speed;
	charge_sec = r.charge;
	grow_sec = r.grow;
	active_sec = r.active;
	fade_sec = r.fade;
	aim = r.aim;
	turn_rate_degrees = r.turn_rate;
	sweep_speed_degrees = r.sweep_speed;
	sweep_arc_degrees = r.sweep_arc;
	speed = r.speed;
	wave_amplitude = r.wave_amplitude;
	wall_mask = r.wall_mask;
	reflections = r.reflections;
	pierce_limit = r.pierce_limit;
	hit_interval_sec = r.hit_interval;
	notify_property_list_changed();
	emit_changed();
}

Ref<LaserData2D> LaserData2D::create_preset(Preset preset) {
	Ref<LaserData2D> out;
	out.instantiate();
	out->apply_preset(preset);
	return out;
}

LaserData2D::Preset LaserData2D::get_matching_preset() const {
	for (int p = PRESET_INSTANT_BEAM; p <= PRESET_LOCK_ON; ++p) {
		const LaserPresetRow &r = LASER_PRESETS[p - PRESET_INSTANT_BEAM];
		if (type == r.type && length == r.length && width == r.width && extend_speed == r.extend_speed && charge_sec == r.charge && grow_sec == r.grow && active_sec == r.active && fade_sec == r.fade && aim == r.aim && turn_rate_degrees == r.turn_rate && sweep_speed_degrees == r.sweep_speed && sweep_arc_degrees == r.sweep_arc && speed == r.speed && wave_amplitude == r.wave_amplitude && wall_mask == r.wall_mask && reflections == r.reflections && pierce_limit == r.pierce_limit && hit_interval_sec == r.hit_interval) {
			return (Preset)p;
		}
	}
	return PRESET_CUSTOM;
}

void LaserData2D::_validate_property(PropertyInfo &p_property) const {
	const String name = p_property.name;
	bool show = true;
	if (name == "extend_speed" || name == "sweep_speed_degrees" || name == "sweep_arc_degrees") {
		show = type == LASER_BEAM;
	} else if (name == "speed") {
		show = type != LASER_BEAM;
	} else if (name == "wave_amplitude" || name == "wave_frequency") {
		show = type == LASER_CURVY;
	} else if (name == "turn_rate_degrees") {
		show = aim == LASER_AIM_TRACK;
	} else if (name == "keep_tracking_when_active") {
		show = aim != LASER_AIM_PATTERN;
	} else if (name == "reflections") {
		show = wall_mask != 0;
	} else if (name == "body_scroll_speed") {
		show = body_mode == BODY_TILE;
	}
	if (!show) {
		p_property.usage &= ~PROPERTY_USAGE_EDITOR;
	}
}

#define LASER_BIND(NAME, VARIANT, ...)                                               \
	ClassDB::bind_method(D_METHOD("get_" #NAME), &LaserData2D::get_##NAME);          \
	ClassDB::bind_method(D_METHOD("set_" #NAME, "value"), &LaserData2D::set_##NAME); \
	ADD_PROPERTY(PropertyInfo(Variant::VARIANT, #NAME, ##__VA_ARGS__), "set_" #NAME, "get_" #NAME);

void LaserData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("apply_preset", "preset"), &LaserData2D::apply_preset);
	ClassDB::bind_method(D_METHOD("get_matching_preset"), &LaserData2D::get_matching_preset);
	ClassDB::bind_method(D_METHOD("get_preset"), &LaserData2D::get_preset);
	ClassDB::bind_static_method("LaserData2D", D_METHOD("create_preset", "preset"), &LaserData2D::create_preset);
	// Editor only (never saved): picking a preset writes the behavior knobs.
	ADD_PROPERTY(PropertyInfo(Variant::INT, "preset", PROPERTY_HINT_ENUM, "Custom,Instant Beam,Charged Sweep,Wall Beam,Reflecting Beam,Bolt,Homing Curvy,Lock On", PROPERTY_USAGE_EDITOR), "apply_preset", "get_preset");

	ADD_GROUP("Laser", "");
	LASER_BIND(type, INT, PROPERTY_HINT_ENUM, "Beam,Bolt,Curvy")
	LASER_BIND(length, FLOAT, PROPERTY_HINT_RANGE, "1,4000,1,or_greater,suffix:px")
	LASER_BIND(width, FLOAT, PROPERTY_HINT_RANGE, "1,200,0.5,or_greater,suffix:px")
	LASER_BIND(extend_speed, FLOAT, PROPERTY_HINT_RANGE, "0,10000,1,or_greater,suffix:px/s")
	LASER_BIND(speed, FLOAT, PROPERTY_HINT_RANGE, "0,5000,1,or_greater,suffix:px/s")
	LASER_BIND(wave_amplitude, FLOAT, PROPERTY_HINT_RANGE, "0,200,0.5,or_greater,suffix:px")
	LASER_BIND(wave_frequency, FLOAT, PROPERTY_HINT_RANGE, "0,20,0.01,or_greater")

	ADD_GROUP("Phases", "");
	LASER_BIND(charge_sec, FLOAT, PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:s")
	LASER_BIND(grow_sec, FLOAT, PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:s")
	LASER_BIND(active_sec, FLOAT, PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s")
	LASER_BIND(fade_sec, FLOAT, PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:s")
	LASER_BIND(warning_width, FLOAT, PROPERTY_HINT_RANGE, "0,20,0.5,or_greater,suffix:px")
	LASER_BIND(warning_color, COLOR)
	LASER_BIND(hit_while_growing, BOOL)

	ADD_GROUP("Aim and Motion", "");
	LASER_BIND(aim, INT, PROPERTY_HINT_ENUM, "Pattern,Track,Lock On")
	LASER_BIND(turn_rate_degrees, FLOAT, PROPERTY_HINT_RANGE, "0,1440,1,or_greater,suffix:deg/s")
	LASER_BIND(keep_tracking_when_active, BOOL)
	LASER_BIND(follow_spawner, BOOL)
	LASER_BIND(sweep_speed_degrees, FLOAT, PROPERTY_HINT_RANGE, "-720,720,1,or_less,or_greater,suffix:deg/s")
	LASER_BIND(sweep_arc_degrees, FLOAT, PROPERTY_HINT_RANGE, "0,360,1,or_greater,suffix:deg")

	ADD_GROUP("Walls", "");
	LASER_BIND(wall_mask, INT, PROPERTY_HINT_LAYERS_2D_PHYSICS)
	LASER_BIND(reflections, INT, PROPERTY_HINT_RANGE, "0,16,1,or_greater")

	ADD_GROUP("Hits", "");
	LASER_BIND(collision_mask, INT, PROPERTY_HINT_LAYERS_2D_PHYSICS)
	LASER_BIND(collide_with_bodies, BOOL)
	LASER_BIND(collide_with_areas, BOOL)
	LASER_BIND(pierce_limit, INT, PROPERTY_HINT_RANGE, "0,16,1,or_greater")
	LASER_BIND(hit_interval_sec, FLOAT, PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:s")
	LASER_BIND(max_hits_per_beam, INT, PROPERTY_HINT_RANGE, "1,256,1,or_greater")

	ADD_GROUP("Looks", "");
	LASER_BIND(modulate, COLOR)
	LASER_BIND(color_ramp, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Gradient")
	LASER_BIND(body_frames, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames")
	LASER_BIND(body_mode, INT, PROPERTY_HINT_ENUM, "Stretch,Tile")
	LASER_BIND(body_scroll_speed, FLOAT, PROPERTY_HINT_RANGE, "-2000,2000,1,or_less,or_greater,suffix:px/s")
	LASER_BIND(charge_frames, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames")
	LASER_BIND(muzzle_frames, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames")
	LASER_BIND(tip_frames, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames")
	LASER_BIND(pierce_frames, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames")
	LASER_BIND(wall_impact_frames, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames")
	LASER_BIND(effects_scale, FLOAT, PROPERTY_HINT_RANGE, "0.05,8,0.01,or_greater")
	LASER_BIND(material, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "CanvasItemMaterial,ShaderMaterial")
	LASER_BIND(z_index, INT, PROPERTY_HINT_RANGE, "-4096,4096,1")

	BIND_ENUM_CONSTANT(LASER_BEAM);
	BIND_ENUM_CONSTANT(LASER_BOLT);
	BIND_ENUM_CONSTANT(LASER_CURVY);
	BIND_ENUM_CONSTANT(LASER_AIM_PATTERN);
	BIND_ENUM_CONSTANT(LASER_AIM_TRACK);
	BIND_ENUM_CONSTANT(LASER_AIM_LOCK_ON);
	BIND_ENUM_CONSTANT(BODY_STRETCH);
	BIND_ENUM_CONSTANT(BODY_TILE);
	BIND_ENUM_CONSTANT(PRESET_CUSTOM);
	BIND_ENUM_CONSTANT(PRESET_INSTANT_BEAM);
	BIND_ENUM_CONSTANT(PRESET_CHARGED_SWEEP);
	BIND_ENUM_CONSTANT(PRESET_WALL_BEAM);
	BIND_ENUM_CONSTANT(PRESET_REFLECTING_BEAM);
	BIND_ENUM_CONSTANT(PRESET_BOLT);
	BIND_ENUM_CONSTANT(PRESET_HOMING_CURVY);
	BIND_ENUM_CONSTANT(PRESET_LOCK_ON);
}

#undef LASER_BIND

} //namespace BlastBullets2D
