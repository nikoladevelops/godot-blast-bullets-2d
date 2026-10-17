#pragma once

#include "core/variant_enum_cast2d.hpp"
#include "data/bullet_volley_data2d.hpp"
#include <godot_cpp/classes/resource.hpp>

namespace BlastBullets2D {
using namespace godot;

// One generation of a split chain (BulletSplitData2D.stages[d] splits the
// bullets of generation d: 0 = the bullets a spawner fired, 1 = their
// children, ...). A bullet that dies by one of the enabled triggers bursts
// into `count` children at its pose. The children carry the parent's spawn
// data (or child_data) with the direction and speed chosen here.
//
// The children of one parent are ONE new volley of exactly `count`
// bullets, spawned right after the factory sweep in which the parent died
// (the same physics frame). Volleys read the stage when a bullet dies, so
// edits reach bullets already in flight. Every accepted change emits
// `changed`.
class BulletSplitStage2D : public Resource {
	GDCLASS(BulletSplitStage2D, Resource)

public:
	// SERIALIZED ids: never renumber.
	enum DirectionMode : int {
		// Evenly across spread_degrees, centered on angle_offset_degrees.
		DIRECTION_FAN = 0,
		// 360 / count apart, the first child at angle_offset_degrees.
		DIRECTION_RING = 1,
		// A random angle per child within spread_degrees around
		// angle_offset_degrees.
		DIRECTION_RANDOM_IN_SPREAD = 2
	};
	// SERIALIZED ids: never renumber.
	enum SpeedMode : int {
		// The parent's current speed, top speed and acceleration, times the
		// child's multiplier.
		SPEED_MULTIPLY_PARENT = 0,
		// fixed_speed times the child's multiplier (top speed and
		// acceleration from the children's spawn data).
		SPEED_FIXED = 1,
		// The children's spawn data, times the child's multiplier.
		SPEED_FROM_SPAWN_DATA = 2
	};
	// SERIALIZED ids: never renumber.
	enum SpeedDistribution : int {
		// From speed_multiplier_min (first child) to _max (last child).
		SPEED_RAMP = 0,
		// A random multiplier per child between min and max.
		SPEED_RANDOM = 1
	};

	// Triggers.
	bool split_on_hit = true;
	bool split_on_lifetime_over = true;
	real_t split_after_sec = 0.0;
	bool split_on_despawn = false;
	bool split_on_clear = false;
	// Triggers that do not kill the bullet (parent_survives decides whether
	// the parent stays).
	bool split_on_bounce = false;
	bool split_on_non_lethal_hit = false;
	bool split_on_homing_reached = false;
	bool split_on_graze = false;

	// Children.
	int count = 3;
	real_t chance = 1.0;
	int64_t seed = 0;

	// Direction.
	DirectionMode direction_mode = DIRECTION_FAN;
	real_t spread_degrees = 60.0;
	real_t angle_offset_degrees = 0.0;
	bool relative_to_parent_heading = true;
	real_t angle_jitter_degrees = 0.0;

	// Speed.
	SpeedMode speed_mode = SPEED_MULTIPLY_PARENT;
	real_t speed_multiplier_min = 1.0;
	real_t speed_multiplier_max = 1.0;
	SpeedDistribution speed_distribution = SPEED_RAMP;
	real_t fixed_speed = 100.0;

	// Placement and look.
	real_t spawn_offset_px = 0.0;
	real_t scale_multiplier = 1.0;
	Ref<BulletVolleyData2D> child_data;

	// What the children keep.
	bool keep_custom_data = true;
	bool keep_collision_count = false;
	bool keep_homing = true;
	bool keep_graze = true;
	bool keep_sound = true;
	bool keep_flock = true;
	bool inherit_parent_velocity = false;

	// The parent.
	bool fire_death_effects = true;
	bool parent_survives = false;
	int max_splits_per_bullet = 1;

	bool get_split_on_hit() const { return split_on_hit; }
	void set_split_on_hit(bool value) { store_flag(split_on_hit, value); }
	bool get_split_on_lifetime_over() const { return split_on_lifetime_over; }
	void set_split_on_lifetime_over(bool value) { store_flag(split_on_lifetime_over, value); }
	real_t get_split_after_sec() const { return split_after_sec; }
	void set_split_after_sec(real_t value) { store_amount(split_after_sec, value, "split_after_sec", 0.0, -1.0); }
	bool get_split_on_despawn() const { return split_on_despawn; }
	void set_split_on_despawn(bool value) { store_flag(split_on_despawn, value); }
	bool get_split_on_clear() const { return split_on_clear; }
	void set_split_on_clear(bool value) { store_flag(split_on_clear, value); }
	bool get_split_on_bounce() const { return split_on_bounce; }
	void set_split_on_bounce(bool value) { store_flag(split_on_bounce, value); }
	bool get_split_on_non_lethal_hit() const { return split_on_non_lethal_hit; }
	void set_split_on_non_lethal_hit(bool value) { store_flag(split_on_non_lethal_hit, value); }
	bool get_split_on_homing_reached() const { return split_on_homing_reached; }
	void set_split_on_homing_reached(bool value) { store_flag(split_on_homing_reached, value); }
	bool get_split_on_graze() const { return split_on_graze; }
	void set_split_on_graze(bool value) { store_flag(split_on_graze, value); }

	int get_count() const { return count; }
	void set_count(int value);
	real_t get_chance() const { return chance; }
	void set_chance(real_t value) { store_amount(chance, value, "chance", 0.0, 1.0); }
	int64_t get_seed() const { return seed; }
	void set_seed(int64_t value);

	DirectionMode get_direction_mode() const { return direction_mode; }
	void set_direction_mode(DirectionMode value);
	real_t get_spread_degrees() const { return spread_degrees; }
	void set_spread_degrees(real_t value) { store_amount(spread_degrees, value, "spread_degrees", 0.0, 360.0); }
	real_t get_angle_offset_degrees() const { return angle_offset_degrees; }
	void set_angle_offset_degrees(real_t value);
	bool get_relative_to_parent_heading() const { return relative_to_parent_heading; }
	void set_relative_to_parent_heading(bool value) { store_flag(relative_to_parent_heading, value); }
	real_t get_angle_jitter_degrees() const { return angle_jitter_degrees; }
	void set_angle_jitter_degrees(real_t value) { store_amount(angle_jitter_degrees, value, "angle_jitter_degrees", 0.0, 360.0); }

	SpeedMode get_speed_mode() const { return speed_mode; }
	void set_speed_mode(SpeedMode value);
	real_t get_speed_multiplier_min() const { return speed_multiplier_min; }
	void set_speed_multiplier_min(real_t value) { store_amount(speed_multiplier_min, value, "speed_multiplier_min", 0.0, -1.0); }
	real_t get_speed_multiplier_max() const { return speed_multiplier_max; }
	void set_speed_multiplier_max(real_t value) { store_amount(speed_multiplier_max, value, "speed_multiplier_max", 0.0, -1.0); }
	SpeedDistribution get_speed_distribution() const { return speed_distribution; }
	void set_speed_distribution(SpeedDistribution value);
	real_t get_fixed_speed() const { return fixed_speed; }
	void set_fixed_speed(real_t value) { store_amount(fixed_speed, value, "fixed_speed", 0.0, -1.0); }

	real_t get_spawn_offset_px() const { return spawn_offset_px; }
	void set_spawn_offset_px(real_t value) { store_amount(spawn_offset_px, value, "spawn_offset_px", 0.0, -1.0); }
	real_t get_scale_multiplier() const { return scale_multiplier; }
	void set_scale_multiplier(real_t value);
	Ref<BulletVolleyData2D> get_child_data() const { return child_data; }
	void set_child_data(const Ref<BulletVolleyData2D> &value);

	bool get_keep_custom_data() const { return keep_custom_data; }
	void set_keep_custom_data(bool value) { store_flag(keep_custom_data, value); }
	bool get_keep_collision_count() const { return keep_collision_count; }
	void set_keep_collision_count(bool value) { store_flag(keep_collision_count, value); }
	bool get_keep_homing() const { return keep_homing; }
	void set_keep_homing(bool value) { store_flag(keep_homing, value); }
	bool get_keep_graze() const { return keep_graze; }
	void set_keep_graze(bool value) { store_flag(keep_graze, value); }
	bool get_keep_sound() const { return keep_sound; }
	void set_keep_sound(bool value) { store_flag(keep_sound, value); }
	bool get_keep_flock() const { return keep_flock; }
	void set_keep_flock(bool value) { store_flag(keep_flock, value); }
	bool get_inherit_parent_velocity() const { return inherit_parent_velocity; }
	void set_inherit_parent_velocity(bool value) { store_flag(inherit_parent_velocity, value); }

	bool get_fire_death_effects() const { return fire_death_effects; }
	void set_fire_death_effects(bool value) { store_flag(fire_death_effects, value); }
	bool get_parent_survives() const { return parent_survives; }
	void set_parent_survives(bool value);
	int get_max_splits_per_bullet() const { return max_splits_per_bullet; }
	void set_max_splits_per_bullet(int value);

	// The angle (radians, relative to the reference direction) and speed
	// multiplier of child k of `children`, given unit rolls in [0, 1].
	// Pure: the factory uses them, scripts may preview a split with them.
	real_t child_angle_radians(int k, int children, double random_roll, double jitter_roll) const;
	real_t child_speed_multiplier(int k, int children, double random_roll) const;

	// spread_degrees hides in Ring mode, fixed_speed outside Fixed,
	// max_splits_per_bullet while the parent never survives a split.
	void _validate_property(PropertyInfo &p_property) const;

protected:
	static void _bind_methods();

private:
	void store_flag(bool &field, bool value);
	// max < 0 = no upper bound.
	void store_amount(real_t &field, real_t value, const char *property_name, real_t min, real_t max);
};
} //namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSplitStage2D::DirectionMode);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSplitStage2D::SpeedMode);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSplitStage2D::SpeedDistribution);
