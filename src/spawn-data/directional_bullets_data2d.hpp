#pragma once

#include "../shared/bullet_curves_data2d.hpp"
#include "../shared/bullet_rotation_data2d.hpp"
#include "../shared/bullet_speed_data2d.hpp"
#include "../shared/bullet_wobble_data2d.hpp"
#include "./multimesh_bullets_data2d.hpp"

#include "godot_cpp/variant/node_path.hpp"
#include "godot_cpp/variant/typed_array.hpp"

namespace BlastBullets2D {
using namespace godot;

class DirectionalBulletsData2D : public MultiMeshBulletsData2D {
	GDCLASS(DirectionalBulletsData2D, MultiMeshBulletsData2D)

public:
	// You are required to pass AT LEAST 1 BulletSpeedData2D in order for the bullets to work. If you want each bullet to have different data (different speed/max speed/ acceleration for each bullet), you would provide the same amount of BulletSpeedData2D as the .size() of the transforms. If you provide less than .size(), the bullets will use only the first BulletSpeedData2D. Note that BulletSpeedData2D has a helper static method that you can use to generate random speed data - BulletSpeedData2D.generate_random_data()
	TypedArray<BulletSpeedData2D> all_bullet_speed_data;

	// Whether each bullet's direction should be adjusted based on the rotation data provided by the user (bullet rotates, so it now moves in that direction)
	bool adjust_direction_based_on_rotation = false;

	// SHARED SPEED / ROTATION RELATED

	// Shared speed applied to every bullet at spawn/enable time, taking
	// precedence over all_bullet_speed_data when set. Null (default) disables
	// the feature and the array drives instead.
	Ref<BulletSpeedData2D> shared_bullet_speed_data;

	// Shared rotation applied to every bullet at spawn/enable time, taking
	// precedence over all_bullet_rotation_data when set. Null (default)
	// disables the feature and the array drives instead.
	Ref<BulletRotationData2D> shared_bullet_rotation_data;

	// Shared curves applied to every bullet at spawn/enable time, before any
	// per-bullet curves. Null (default) disables the feature. Tick precedence
	// (shared wins ties, per-bullet fills gaps) is unchanged.
	Ref<BulletCurvesData2D> shared_bullet_curves_data;

	// SHARED MOVEMENT PATTERN RELATED

	// Path to a Path2D in the scene tree whose Curve2D is applied as the
	// movement pattern of every bullet at spawn/enable time. A Path2D node
	// cannot live inside a Resource, so the path is stored instead and
	// resolved through the BulletFactory2D (absolute paths always work).
	// Empty (default) disables the feature.
	NodePath shared_movement_pattern_path;

	// Whether the shared movement pattern rotates bullets to face their direction of travel.
	bool shared_movement_pattern_face_movement_direction = false;

	// Whether the shared movement pattern repeats instead of stopping at the end of the curve.
	bool shared_movement_pattern_repeat = true;

	// PER-BULLET CURVES / PATTERNS (runtime-owned; this data seeds them here).
	// Same fallback rule as all_bullet_speed_data: empty = off, size ==
	// amount = per bullet, otherwise the first entry drives all bullets.
	// Null entries are skipped per bullet.

	// Per-bullet curves applied at spawn/enable time through the regular
	// per-bullet machinery (shared curves still win ties per tick).
	TypedArray<BulletCurvesData2D> all_bullet_curves_data;

	// PER-BULLET MOVEMENT PATTERN RELATED

	// Per-bullet movement pattern paths applied at spawn/enable time. Each
	// entry must point at a Path2D; its Curve2D is extracted when the volley
	// spawns. Same fallback rule as above. The face/repeat flags below
	// resolve per bullet when sized to the amount, otherwise the shared
	// flags drive. Bad entries are skipped per bullet.
	TypedArray<NodePath> all_bullet_movement_pattern_paths;

	// Per-bullet face flags for the curves above.
	TypedArray<bool> all_bullet_movement_pattern_face_movement_directions;

	// Per-bullet repeat flags for the curves above.
	TypedArray<bool> all_bullet_movement_pattern_repeats;

	// WOBBLE (sine/cos flight modulation; editor-friendly danmaku staple).
	// Shared wobble applied to every bullet at spawn/enable time, taking
	// precedence over all_bullet_wobble_data when set and enabled. Null
	// (default) disables the feature and the array drives instead.
	Ref<BulletWobbleData2D> shared_bullet_wobble_data;

	// Per-bullet wobble applied at spawn/enable time through the per-bullet
	// machinery (same fallback rule as speed data: empty = off, size ==
	// amount = per bullet, otherwise the first entry drives all bullets).
	// Null entries and entries with enabled = false are skipped per bullet.
	TypedArray<BulletWobbleData2D> all_bullet_wobble_data;

	// GRAVITY / DRAG (2D sideview + tower-defense shells).
	// Constant acceleration added to every bullet each tick (px/s^2).
	// (0, 0) disables. Must stay finite.
	Vector2 gravity = Vector2(0, 0);

	// Linear drag applied to speed each tick: speed -= speed * drag * delta.
	// 0 disables. Must stay finite and >= 0.
	double linear_drag = 0.0;

	// HOMING STEERING (spawn-time seed; every value below also exists as a
	// live DirectionalBullets2D setter for runtime tuning). Pool reuse
	// re-seeds these on every enable, so direct BulletFactory2D.spawn_* users
	// no longer lose steering on the first reuse. BulletSpawner2D users can
	// ignore them: apply_steering_to_volley overwrites them per volley.

	// Shared homing turn agility (0 snaps instantly). Must stay finite and >= 0.
	double homing_smoothing = 0.0;

	// Seconds between homing target position refreshes (0 = every tick).
	// Must stay finite and >= 0.
	double homing_update_interval = 0.0;

	// Distance in pixels at which a bullet counts as having reached its
	// target. Must stay finite and >= 0.
	double homing_distance_before_reached = 5.0;

	// Whether homing steers the bullet facing (leave off when a movement
	// pattern, rotation data, or orbiting texture mode already owns it).
	bool homing_take_control_of_texture_rotation = false;

	// Per-bullet queue: pop the front target when this bullet reaches it.
	bool bullet_homing_auto_pop_after_target_reached = false;

	// Shared queue: one deferred pop when any bullet reaches the front target.
	bool shared_homing_deque_auto_pop_after_target_reached = false;

	// When > 0, homing steering only begins after this many seconds of
	// straight flight (classic aimed-then-homing). Must stay finite and >= 0.
	double homing_delay_sec = 0.0;

	// When > 0, homing steering stops after this many seconds (lets fast
	// players escape). 0 = infinite. Must stay finite and >= 0.
	double homing_duration_sec = 0.0;

	// When > 0, homing steering pauses while the bullet is farther than this
	// from its target (BLAST-style homing range). 0 = unlimited.
	// Must stay finite and >= 0.
	double homing_lose_range_px = 0.0;

	double get_homing_smoothing() const;
	void set_homing_smoothing(double value);

	double get_homing_update_interval() const;
	void set_homing_update_interval(double value);

	double get_homing_distance_before_reached() const;
	void set_homing_distance_before_reached(double value);

	bool get_homing_take_control_of_texture_rotation() const;
	void set_homing_take_control_of_texture_rotation(bool value);

	bool get_bullet_homing_auto_pop_after_target_reached() const;
	void set_bullet_homing_auto_pop_after_target_reached(bool value);

	bool get_shared_homing_deque_auto_pop_after_target_reached() const;
	void set_shared_homing_deque_auto_pop_after_target_reached(bool value);

	TypedArray<BulletSpeedData2D> get_all_bullet_speed_data() const;
	void set_all_bullet_speed_data(const TypedArray<BulletSpeedData2D> &new_data);

	bool get_adjust_direction_based_on_rotation() const;
	void set_adjust_direction_based_on_rotation(bool new_adjust_direction_based_on_rotation);

	Ref<BulletSpeedData2D> get_shared_bullet_speed_data() const;
	void set_shared_bullet_speed_data(const Ref<BulletSpeedData2D> &new_speed_data);

	Ref<BulletRotationData2D> get_shared_bullet_rotation_data() const;
	void set_shared_bullet_rotation_data(const Ref<BulletRotationData2D> &new_rotation_data);

	Ref<BulletCurvesData2D> get_shared_bullet_curves_data() const;
	void set_shared_bullet_curves_data(const Ref<BulletCurvesData2D> &new_curves_data);

	NodePath get_shared_movement_pattern_path() const;
	void set_shared_movement_pattern_path(const NodePath &new_path);

	bool get_shared_movement_pattern_face_movement_direction() const;
	void set_shared_movement_pattern_face_movement_direction(bool value);

	bool get_shared_movement_pattern_repeat() const;
	void set_shared_movement_pattern_repeat(bool value);

	TypedArray<BulletCurvesData2D> get_all_bullet_curves_data() const;
	void set_all_bullet_curves_data(const TypedArray<BulletCurvesData2D> &new_data);

	TypedArray<NodePath> get_all_bullet_movement_pattern_paths() const;
	void set_all_bullet_movement_pattern_paths(const TypedArray<NodePath> &new_paths);

	TypedArray<bool> get_all_bullet_movement_pattern_face_movement_directions() const;
	void set_all_bullet_movement_pattern_face_movement_directions(const TypedArray<bool> &new_flags);

	TypedArray<bool> get_all_bullet_movement_pattern_repeats() const;
	void set_all_bullet_movement_pattern_repeats(const TypedArray<bool> &new_flags);

	Ref<BulletWobbleData2D> get_shared_bullet_wobble_data() const;
	void set_shared_bullet_wobble_data(const Ref<BulletWobbleData2D> &new_wobble_data);

	TypedArray<BulletWobbleData2D> get_all_bullet_wobble_data() const;
	void set_all_bullet_wobble_data(const TypedArray<BulletWobbleData2D> &new_data);

	Vector2 get_gravity() const;
	void set_gravity(const Vector2 &value);

	double get_linear_drag() const;
	void set_linear_drag(double value);

	double get_homing_delay_sec() const;
	void set_homing_delay_sec(double value);

	double get_homing_duration_sec() const;
	void set_homing_duration_sec(double value);

	double get_homing_lose_range_px() const;
	void set_homing_lose_range_px(double value);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D
