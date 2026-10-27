#pragma once

#include "core/variant_enum_cast2d.hpp"
#include "data/laser_sprite_look2d.hpp"
#include <godot_cpp/classes/gradient.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>

namespace BlastBullets2D {
using namespace godot;

// What a laser is: its type, size, phases, aim, walls, hits and looks.
// LaserSpawner2D fires one LaserVolley2D per shot (one beam per pattern
// slot); BulletFactory2D.spawn_laser fires one from a script. Lasers read the
// resource every tick, so edits reach lasers already firing. Every accepted
// change emits `changed`. Serialized enum ids: never renumber.
class LaserData2D : public Resource {
	GDCLASS(LaserData2D, Resource)

public:
	enum LaserType : int {
		LASER_BEAM = 0, // fixed at its muzzle, reaching `length` (or the first wall)
		LASER_BOLT = 1, // a `length` segment flying away at `speed`
		LASER_CURVY = 2 // a snake: the head flies (and tracks), the body follows its path
	};
	enum LaserAim : int {
		LASER_AIM_PATTERN = 0, // the pattern slot's direction (plus the sweep)
		LASER_AIM_TRACK = 1, // turns toward its target at turn_rate_degrees
		LASER_AIM_LOCK_ON = 2 // ends on its target (length = the distance, capped at `length`)
	};
	enum BodyMode : int {
		BODY_STRETCH = 0, // one body frame stretched over the whole beam
		BODY_TILE = 1 // the frame repeats every texture width (scrolls with body_scroll_speed)
	};
	enum Preset : int {
		PRESET_CUSTOM = 0,
		PRESET_INSTANT_BEAM = 1,
		PRESET_CHARGED_SWEEP = 2,
		PRESET_WALL_BEAM = 3,
		PRESET_REFLECTING_BEAM = 4,
		PRESET_BOLT = 5,
		PRESET_HOMING_CURVY = 6,
		PRESET_LOCK_ON = 7
	};

	// Shape.
	LaserType type = LASER_BEAM;
	double length = 600.0;
	double width = 12.0;
	double extend_speed = 0.0; // px/s (beams), 0 = full length at once
	// Phases: CHARGING (warning line, no hits) -> GROWING (width 0 -> full)
	// -> ACTIVE (active_sec, 0 = until ended) -> FADING -> finished.
	double charge_sec = 0.5;
	double grow_sec = 0.1;
	double active_sec = 1.0;
	double fade_sec = 0.2;
	double warning_width = 2.0;
	Color warning_color = Color(1.0, 0.3, 0.3, 0.6);
	bool hit_while_growing = true;
	// Aim and motion.
	LaserAim aim = LASER_AIM_PATTERN;
	double turn_rate_degrees = 90.0; // TRACK (and curvy heads); 0 = snaps
	bool keep_tracking_when_active = true;
	bool follow_spawner = true;
	double sweep_speed_degrees = 0.0; // beams: degrees per second around the muzzle
	double sweep_arc_degrees = 0.0; // 0 = keeps turning, > 0 = swings back and forth
	// Bolts and curvy lasers.
	double speed = 600.0;
	double wave_amplitude = 0.0; // curvy: px sideways
	double wave_frequency = 2.0; // curvy: waves per second
	// Walls (rays): the beam stops at the first wall of wall_mask, or
	// reflects off `reflections` walls first.
	int64_t wall_mask = 0;
	int reflections = 0;
	// Hits (shape queries on collision_mask, every tick of a hitting phase).
	int64_t collision_mask = 1;
	bool collide_with_bodies = true;
	bool collide_with_areas = true;
	int pierce_limit = 0; // targets hit at most per beam, it stops at the last (0 = all)
	double hit_interval_sec = 0.0; // 0 = one hit per contact, > 0 = repeats
	int max_hits_per_beam = 32; // physics query results per beam segment
	// Looks.
	Color modulate = Color(1, 1, 1, 1);
	Ref<Gradient> color_ramp; // along the length
	Ref<SpriteFrames> body_frames;
	BodyMode body_mode = BODY_STRETCH;
	double body_scroll_speed = 0.0;
	double body_width_scale = 1.0; // drawn width / `width` (hits keep `width`)
	Ref<Material> body_material; // the body and the warning line
	// The point animations, each on its own canvas item (z order relative
	// to the laser by default, own material). Null = not drawn.
	enum LookSlot : int {
		LOOK_CHARGE = 0, // at the muzzle while charging
		LOOK_MUZZLE = 1, // beams: at the muzzle while it fires
		LOOK_TIP = 2, // at the end of the beam (the head of a bolt)
		LOOK_PIERCE = 3, // on every target the beam pierces
		LOOK_WALL_IMPACT = 4, // where the beam meets a wall
		LOOK_SLOT_COUNT = 5
	};
	Ref<LaserSpriteLook2D> looks[LOOK_SLOT_COUNT];
	int z_index = 0;
	// Bumped by every look change (a frame property, a look assigned or
	// edited): lasers in flight re-bake and re-apply their looks once
	// (LaserVolley2D::apply_looks).
	uint64_t looks_serial = 0;

	LaserType get_type() const { return type; }
	void set_type(LaserType value);
	double get_length() const { return length; }
	void set_length(double value);
	double get_width() const { return width; }
	void set_width(double value);
	double get_extend_speed() const { return extend_speed; }
	void set_extend_speed(double value);
	double get_charge_sec() const { return charge_sec; }
	void set_charge_sec(double value);
	double get_grow_sec() const { return grow_sec; }
	void set_grow_sec(double value);
	double get_active_sec() const { return active_sec; }
	void set_active_sec(double value);
	double get_fade_sec() const { return fade_sec; }
	void set_fade_sec(double value);
	double get_warning_width() const { return warning_width; }
	void set_warning_width(double value);
	Color get_warning_color() const { return warning_color; }
	void set_warning_color(const Color &value);
	bool get_hit_while_growing() const { return hit_while_growing; }
	void set_hit_while_growing(bool value);
	LaserAim get_aim() const { return aim; }
	void set_aim(LaserAim value);
	double get_turn_rate_degrees() const { return turn_rate_degrees; }
	void set_turn_rate_degrees(double value);
	bool get_keep_tracking_when_active() const { return keep_tracking_when_active; }
	void set_keep_tracking_when_active(bool value);
	bool get_follow_spawner() const { return follow_spawner; }
	void set_follow_spawner(bool value);
	double get_sweep_speed_degrees() const { return sweep_speed_degrees; }
	void set_sweep_speed_degrees(double value);
	double get_sweep_arc_degrees() const { return sweep_arc_degrees; }
	void set_sweep_arc_degrees(double value);
	double get_speed() const { return speed; }
	void set_speed(double value);
	double get_wave_amplitude() const { return wave_amplitude; }
	void set_wave_amplitude(double value);
	double get_wave_frequency() const { return wave_frequency; }
	void set_wave_frequency(double value);
	int64_t get_wall_mask() const { return wall_mask; }
	void set_wall_mask(int64_t value);
	int get_reflections() const { return reflections; }
	void set_reflections(int value);
	int64_t get_collision_mask() const { return collision_mask; }
	void set_collision_mask(int64_t value);
	bool get_collide_with_bodies() const { return collide_with_bodies; }
	void set_collide_with_bodies(bool value);
	bool get_collide_with_areas() const { return collide_with_areas; }
	void set_collide_with_areas(bool value);
	int get_pierce_limit() const { return pierce_limit; }
	void set_pierce_limit(int value);
	double get_hit_interval_sec() const { return hit_interval_sec; }
	void set_hit_interval_sec(double value);
	int get_max_hits_per_beam() const { return max_hits_per_beam; }
	void set_max_hits_per_beam(int value);
	Color get_modulate() const { return modulate; }
	void set_modulate(const Color &value);
	Ref<Gradient> get_color_ramp() const { return color_ramp; }
	void set_color_ramp(const Ref<Gradient> &value);
	Ref<SpriteFrames> get_body_frames() const { return body_frames; }
	void set_body_frames(const Ref<SpriteFrames> &value);
	BodyMode get_body_mode() const { return body_mode; }
	void set_body_mode(BodyMode value);
	double get_body_scroll_speed() const { return body_scroll_speed; }
	void set_body_scroll_speed(double value);
	double get_body_width_scale() const { return body_width_scale; }
	void set_body_width_scale(double value);
	Ref<Material> get_body_material() const { return body_material; }
	void set_body_material(const Ref<Material> &value);
	Ref<LaserSpriteLook2D> get_look(LookSlot slot) const { return looks[slot]; }
	void set_look(LookSlot slot, const Ref<LaserSpriteLook2D> &value);
	Ref<LaserSpriteLook2D> get_charge_look() const { return looks[LOOK_CHARGE]; }
	void set_charge_look(const Ref<LaserSpriteLook2D> &value) { set_look(LOOK_CHARGE, value); }
	Ref<LaserSpriteLook2D> get_muzzle_look() const { return looks[LOOK_MUZZLE]; }
	void set_muzzle_look(const Ref<LaserSpriteLook2D> &value) { set_look(LOOK_MUZZLE, value); }
	Ref<LaserSpriteLook2D> get_tip_look() const { return looks[LOOK_TIP]; }
	void set_tip_look(const Ref<LaserSpriteLook2D> &value) { set_look(LOOK_TIP, value); }
	Ref<LaserSpriteLook2D> get_pierce_look() const { return looks[LOOK_PIERCE]; }
	void set_pierce_look(const Ref<LaserSpriteLook2D> &value) { set_look(LOOK_PIERCE, value); }
	Ref<LaserSpriteLook2D> get_wall_impact_look() const { return looks[LOOK_WALL_IMPACT]; }
	void set_wall_impact_look(const Ref<LaserSpriteLook2D> &value) { set_look(LOOK_WALL_IMPACT, value); }
	// The property name of each slot ("charge_look", ...).
	static const char *look_property(int slot);
	int get_z_index() const { return z_index; }
	void set_z_index(int value);

	// Ready-made lasers (apply_preset writes the behavior knobs, never the
	// looks; the editor-only `preset` property is never saved).
	void apply_preset(Preset preset);
	static Ref<LaserData2D> create_preset(Preset preset);
	Preset get_matching_preset() const;
	Preset get_preset() const { return get_matching_preset(); }

	void _validate_property(PropertyInfo &p_property) const;
	// Scenes saved before the looks keep loading: charge_frames ..
	// wall_impact_frames become the frames of the matching look (created as
	// needed), effects_scale its scale, material the body_material. Hidden
	// (never listed, so never saved again).
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;

protected:
	static void _bind_methods();

private:
	void store(double &field, double value);
	void _on_look_changed();
	// Old effects_scale, applied to the looks the migration created.
	double legacy_effects_scale = 1.0;
	uint32_t legacy_looks = 0; // bit per slot: created by the migration
};
} //namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserData2D::LaserType);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserData2D::LaserAim);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserData2D::BodyMode);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserData2D::Preset);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserData2D::LookSlot);
