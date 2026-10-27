#include "data/laser_data2d.hpp"

#include "core/transform_math2d.hpp"

#include "core/live_object2d.hpp"
#include "data/laser_effect_data2d.hpp"

#include "core/assign_if_changed2d.hpp"
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
	if (!assign_if_changed2d(field, value)) {
		return;
	}
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
LASER_DOUBLE_SETTER(body_width_scale, value >= 0.0, ">= 0")

#undef LASER_DOUBLE_SETTER

void LaserData2D::set_type(LaserType value) {
	if (value < LASER_BEAM || value > LASER_CURVY) {
		UtilityFunctions::push_error("LaserData2D: type must be 0 (Beam), 1 (Bolt) or 2 (Curvy), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(type, value)) {
		return;
	}
	notify_property_list_changed();
	emit_changed();
}

void LaserData2D::set_aim(LaserAim value) {
	if (value < LASER_AIM_PATTERN || value > LASER_AIM_LOCK_ON) {
		UtilityFunctions::push_error("LaserData2D: aim must be 0 (Pattern), 1 (Track) or 2 (Lock On), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(aim, value)) {
		return;
	}
	notify_property_list_changed();
	emit_changed();
}

void LaserData2D::set_body_mode(BodyMode value) {
	if (value < BODY_STRETCH || value > BODY_TILE) {
		UtilityFunctions::push_error("LaserData2D: body_mode must be 0 (Stretch) or 1 (Tile), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(body_mode, value)) {
		return;
	}
	notify_property_list_changed();
	emit_changed();
}

void LaserData2D::set_warning_color(const Color &value) {
	if (!color_ok2d(value)) {
		UtilityFunctions::push_error("LaserData2D: warning_color must be finite, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(warning_color, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_modulate(const Color &value) {
	if (!color_ok2d(value)) {
		UtilityFunctions::push_error("LaserData2D: modulate must be finite, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(modulate, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_hit_while_growing(bool value) {
	if (!assign_if_changed2d(hit_while_growing, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_keep_tracking_when_active(bool value) {
	if (!assign_if_changed2d(keep_tracking_when_active, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_follow_spawner(bool value) {
	if (!assign_if_changed2d(follow_spawner, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_wall_mask(int64_t value) {
	if (value < 0 || value > 0xFFFFFFFFll) {
		UtilityFunctions::push_error("LaserData2D: wall_mask must be a 32-bit layer mask (0 = no walls), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(wall_mask, value)) {
		return;
	}
	notify_property_list_changed(); // reflections gate on it
	emit_changed();
}

void LaserData2D::set_reflections(int value) {
	if (value < 0 || value > 256) {
		UtilityFunctions::push_error("LaserData2D: reflections must be between 0 and 256 (each one is a ray per tick), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(reflections, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_collision_mask(int64_t value) {
	if (value < 0 || value > 0xFFFFFFFFll) {
		UtilityFunctions::push_error("LaserData2D: collision_mask must be a 32-bit layer mask, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(collision_mask, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_ignore_despawn_bounds(bool value) {
	if (!assign_if_changed2d(ignore_despawn_bounds, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_collide_with_bodies(bool value) {
	if (!assign_if_changed2d(collide_with_bodies, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_collide_with_areas(bool value) {
	if (!assign_if_changed2d(collide_with_areas, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_pierce_limit(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("LaserData2D: pierce_limit must be >= 0 (0 = passes through every target), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(pierce_limit, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_max_hits_per_beam(int value) {
	if (value < 1 || value > 4096) {
		UtilityFunctions::push_error("LaserData2D: max_hits_per_beam must be between 1 and 4096, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(max_hits_per_beam, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_color_ramp(const Ref<Gradient> &value) {
	if (!assign_if_changed2d(color_ramp, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_body_frames(const Ref<SpriteFrames> &value) {
	if (!assign_if_changed2d(body_frames, value)) {
		return;
	}
	++looks_serial;
	emit_changed();
}

void LaserData2D::set_body_material(const Ref<Material> &value) {
	if (!assign_if_changed2d(body_material, value)) {
		return;
	}
	emit_changed();
}

void LaserData2D::set_effects(const Array &value) {
	for (int64_t i = 0; i < value.size(); ++i) {
		const Variant &entry = value[i];
		if (entry.get_type() != Variant::NIL && live_object2d<LaserEffectData2D>(entry) == nullptr) {
			UtilityFunctions::push_error("LaserData2D: effects entries must be LaserEffectData2D or null, keeping the old value.");
			return;
		}
	}
	if (!assign_if_changed2d(effects, value)) {
		return;
	}
	emit_changed();
}

const char *LaserData2D::look_property(int slot) {
	static const char *const names[LOOK_SLOT_COUNT] = { "charge_look", "muzzle_look", "tip_look", "pierce_look", "wall_impact_look" };
	return slot >= 0 && slot < LOOK_SLOT_COUNT ? names[slot] : "";
}

void LaserData2D::set_look(LookSlot slot, const Ref<LaserSpriteLook2D> &value) {
	if (slot < LOOK_CHARGE || slot >= LOOK_SLOT_COUNT) {
		UtilityFunctions::push_error("LaserData2D: set_look slot must be one of the LookSlot values, nothing changed.");
		return;
	}
	Ref<LaserSpriteLook2D> &field = looks[slot];
	if (field == value) {
		return;
	}
	// One look may sit in several slots (or several lasers): its `changed`
	// connects once and stays while any slot holds it.
	const Callable on_changed = callable_mp(this, &LaserData2D::_on_look_changed);
	const StringName changed("changed");
	const Ref<LaserSpriteLook2D> old = field;
	field = value;
	legacy_looks &= ~(1u << slot);
	if (old.is_valid()) {
		bool still_held = false;
		for (const Ref<LaserSpriteLook2D> &l : looks) {
			still_held = still_held || l == old;
		}
		if (!still_held && old->is_connected(changed, on_changed)) {
			old->disconnect(changed, on_changed);
		}
	}
	if (value.is_valid() && !value->is_connected(changed, on_changed)) {
		value->connect(changed, on_changed);
	}
	++looks_serial;
	emit_changed();
}

void LaserData2D::_on_look_changed() {
	++looks_serial;
	emit_changed();
}

bool LaserData2D::_set(const StringName &p_name, const Variant &p_value) {
	static const char *const legacy[LOOK_SLOT_COUNT] = { "charge_frames", "muzzle_frames", "tip_frames", "pierce_frames", "wall_impact_frames" };
	const String name = p_name;
	for (int slot = 0; slot < LOOK_SLOT_COUNT; ++slot) {
		if (name != legacy[slot]) {
			continue;
		}
		const Ref<SpriteFrames> frames = p_value;
		if (looks[slot].is_valid()) {
			looks[slot]->set_frames(frames);
			return true;
		}
		if (frames.is_null()) {
			return true;
		}
		Ref<LaserSpriteLook2D> look;
		look.instantiate();
		look->set_frames(frames);
		look->set_scale(Vector2(1, 1) * (real_t)legacy_effects_scale);
		set_look((LookSlot)slot, look);
		legacy_looks |= 1u << slot;
		return true;
	}
	if (name == "effects_scale") {
		const double value = p_value;
		if (!is_finite_real2d(value) || !(value > 0.0)) {
			return true; // the old setter refused it too
		}
		legacy_effects_scale = value;
		for (int slot = 0; slot < LOOK_SLOT_COUNT; ++slot) {
			if ((legacy_looks & (1u << slot)) != 0 && looks[slot].is_valid()) {
				looks[slot]->set_scale(Vector2(1, 1) * (real_t)value);
			}
		}
		return true;
	}
	if (name == "material") {
		set_body_material(p_value);
		return true;
	}
	return false;
}

bool LaserData2D::_get(const StringName &p_name, Variant &r_ret) const {
	static const char *const legacy[LOOK_SLOT_COUNT] = { "charge_frames", "muzzle_frames", "tip_frames", "pierce_frames", "wall_impact_frames" };
	const String name = p_name;
	for (int slot = 0; slot < LOOK_SLOT_COUNT; ++slot) {
		if (name == legacy[slot]) {
			r_ret = looks[slot].is_valid() ? Variant(looks[slot]->get_frames()) : Variant();
			return true;
		}
	}
	if (name == "effects_scale") {
		r_ret = legacy_effects_scale;
		return true;
	}
	if (name == "material") {
		r_ret = body_material;
		return true;
	}
	return false;
}

void LaserData2D::set_z_index(int value) {
	if (value < -4096 || value > 4096) {
		UtilityFunctions::push_error("LaserData2D: z_index must be between -4096 and 4096, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(z_index, value)) {
		return;
	}
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

#define LASER_BIND(NAME, VARIANT, HINT, HINT_STRING)                                 \
	ClassDB::bind_method(D_METHOD("get_" #NAME), &LaserData2D::get_##NAME);          \
	ClassDB::bind_method(D_METHOD("set_" #NAME, "value"), &LaserData2D::set_##NAME); \
	ADD_PROPERTY(PropertyInfo(Variant::VARIANT, #NAME, HINT, HINT_STRING), "set_" #NAME, "get_" #NAME);

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
	LASER_BIND(ignore_despawn_bounds, BOOL, PROPERTY_HINT_NONE, "")

	ADD_GROUP("Phases", "");
	LASER_BIND(charge_sec, FLOAT, PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:s")
	LASER_BIND(grow_sec, FLOAT, PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:s")
	LASER_BIND(active_sec, FLOAT, PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s")
	LASER_BIND(fade_sec, FLOAT, PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:s")
	LASER_BIND(warning_width, FLOAT, PROPERTY_HINT_RANGE, "0,20,0.5,or_greater,suffix:px")
	LASER_BIND(warning_color, COLOR, PROPERTY_HINT_NONE, "")
	LASER_BIND(hit_while_growing, BOOL, PROPERTY_HINT_NONE, "")

	ADD_GROUP("Aim and Motion", "");
	LASER_BIND(aim, INT, PROPERTY_HINT_ENUM, "Pattern,Track,Lock On")
	LASER_BIND(turn_rate_degrees, FLOAT, PROPERTY_HINT_RANGE, "0,1440,1,or_greater,suffix:deg/s")
	LASER_BIND(keep_tracking_when_active, BOOL, PROPERTY_HINT_NONE, "")
	LASER_BIND(follow_spawner, BOOL, PROPERTY_HINT_NONE, "")
	LASER_BIND(sweep_speed_degrees, FLOAT, PROPERTY_HINT_RANGE, "-720,720,1,or_less,or_greater,suffix:deg/s")
	LASER_BIND(sweep_arc_degrees, FLOAT, PROPERTY_HINT_RANGE, "0,360,1,or_greater,suffix:deg")

	ADD_GROUP("Walls", "");
	LASER_BIND(wall_mask, INT, PROPERTY_HINT_LAYERS_2D_PHYSICS, "")
	LASER_BIND(reflections, INT, PROPERTY_HINT_RANGE, "0,16,1,or_greater")

	ADD_GROUP("Hits", "");
	LASER_BIND(collision_mask, INT, PROPERTY_HINT_LAYERS_2D_PHYSICS, "")
	LASER_BIND(collide_with_bodies, BOOL, PROPERTY_HINT_NONE, "")
	LASER_BIND(collide_with_areas, BOOL, PROPERTY_HINT_NONE, "")
	LASER_BIND(pierce_limit, INT, PROPERTY_HINT_RANGE, "0,16,1,or_greater")
	LASER_BIND(hit_interval_sec, FLOAT, PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:s")
	LASER_BIND(max_hits_per_beam, INT, PROPERTY_HINT_RANGE, "1,256,1,or_greater")

	ADD_GROUP("Looks", "");
	LASER_BIND(modulate, COLOR, PROPERTY_HINT_NONE, "")
	LASER_BIND(color_ramp, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Gradient")
	LASER_BIND(body_frames, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames")
	LASER_BIND(body_mode, INT, PROPERTY_HINT_ENUM, "Stretch,Tile")
	LASER_BIND(body_scroll_speed, FLOAT, PROPERTY_HINT_RANGE, "-2000,2000,1,or_less,or_greater,suffix:px/s")
	LASER_BIND(body_width_scale, FLOAT, PROPERTY_HINT_RANGE, "0,4,0.01,or_greater")
	LASER_BIND(body_material, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "CanvasItemMaterial,ShaderMaterial")
	LASER_BIND(charge_look, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "LaserSpriteLook2D")
	LASER_BIND(muzzle_look, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "LaserSpriteLook2D")
	LASER_BIND(tip_look, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "LaserSpriteLook2D")
	LASER_BIND(pierce_look, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "LaserSpriteLook2D")
	LASER_BIND(wall_impact_look, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "LaserSpriteLook2D")
	LASER_BIND(z_index, INT, PROPERTY_HINT_RANGE, "-4096,4096,1")

	ADD_GROUP("Effects", "");
	LASER_BIND(effects, ARRAY, PROPERTY_HINT_ARRAY_TYPE, "LaserEffectData2D")
	ClassDB::bind_method(D_METHOD("get_look", "slot"), &LaserData2D::get_look);
	ClassDB::bind_method(D_METHOD("set_look", "slot", "look"), &LaserData2D::set_look);

	BIND_ENUM_CONSTANT(LASER_BEAM);
	BIND_ENUM_CONSTANT(LASER_BOLT);
	BIND_ENUM_CONSTANT(LASER_CURVY);
	BIND_ENUM_CONSTANT(LASER_AIM_PATTERN);
	BIND_ENUM_CONSTANT(LASER_AIM_TRACK);
	BIND_ENUM_CONSTANT(LASER_AIM_LOCK_ON);
	BIND_ENUM_CONSTANT(BODY_STRETCH);
	BIND_ENUM_CONSTANT(LOOK_CHARGE);
	BIND_ENUM_CONSTANT(LOOK_MUZZLE);
	BIND_ENUM_CONSTANT(LOOK_TIP);
	BIND_ENUM_CONSTANT(LOOK_PIERCE);
	BIND_ENUM_CONSTANT(LOOK_WALL_IMPACT);
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
