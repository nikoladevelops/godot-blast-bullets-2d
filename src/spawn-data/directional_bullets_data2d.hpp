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
	// How per-bullet arrays resolve. Entry i belongs to bullet i and nobody
	// else. Each bullet falls back independently: a valid per-bullet entry
	// wins for that bullet, otherwise the shared value fills in when it is
	// set, otherwise the feature default (off, zero, null — see each array).
	// An array shorter than the volley only covers its own indices; the rest
	// fall back. Longer arrays ignore the extras. Mismatches warn once per
	// spawn. Each array has its own tile_* checkbox (off by default) that
	// restores the old wrap-around (slot i reads entry i % size) for that
	// array only.

	// Speed for each bullet. The array MUST have one entry per bullet
	// (same size as transforms) unless you use shared_bullet_speed_data or
	// tile_all_bullet_speed_data. Entry i drives bullet i only.
	// Note that BulletSpeedData2D has a helper static method that you can use to generate random speed data - BulletSpeedData2D.generate_random_data(). Negative speed flies backwards on purpose.
	TypedArray<BulletSpeedData2D> all_bullet_speed_data;

	// Wrap short speed arrays around the volley (slot i reads entry
	// i % size). Off by default. Check it to fan 2 entries across 10
	// bullets as A,B,A,B... instead of covering only bullets 0 and 1.
	bool tile_all_bullet_speed_data = false;

	// Whether each bullet's direction should be adjusted based on the rotation data provided by the user (bullet rotates, so it now moves in that direction)
	bool adjust_direction_based_on_rotation = false;

	// SHARED SPEED / ROTATION RELATED

	// Fallback speed for bullets whose own entry is missing or invalid
	// (null entry, or NaN/Inf values). A valid per-bullet entry always wins
	// for its bullet; this only fills the gaps. Null (default) means no
	// fallback — uncovered bullets fly at speed 0.
	// Example: volley of 3, speeds [200, null, 200], shared 150 → bullets
	// fly at 200, 150, 200.
	Ref<BulletSpeedData2D> shared_bullet_speed_data;

	// Same fallback deal as shared speed, for spin. Null (default) means no
	// fallback — uncovered bullets don't rotate.
	Ref<BulletRotationData2D> shared_bullet_rotation_data;

	// Fallback curves, resolved per channel. A bullet that carries its own
	// x-curve uses it even when this also defines x; this only covers the
	// channels the bullet lacks. Null (default) means no fallback.
	// Example: shared defines x + speed, bullet 2 carries only a y-curve →
	// bullet 2 steers with its own y plus the shared x and speed.
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
	// Entry i seeds bullet i only. A null entry means "no curves for this
	// bullet" — it falls back to shared_bullet_curves_data per channel.

	// Curves for each bullet, applied at spawn/enable time. Entry i affects
	// bullet i only. Short arrays leave the tail bullets on the shared
	// fallback (or plain ballistics when that is unset too).
	TypedArray<BulletCurvesData2D> all_bullet_curves_data;

	// Wrap short curve arrays around the volley. Off by default.
	bool tile_all_bullet_curves_data = false;

	// PER-BULLET MOVEMENT PATTERN RELATED

	// Pattern path for each bullet, applied at spawn/enable time. Each entry
	// must point at a Path2D; its Curve2D is extracted when the volley
	// spawns. Entry i drives bullet i only. A bullet with its own valid
	// pattern always uses it; the shared path only covers bullets without
	// one. Empty or bad entries fall back per bullet.
	// The face/repeat flags below follow their own arrays (same rule).
	TypedArray<NodePath> all_bullet_movement_pattern_paths;

	// Wrap short pattern-path arrays around the volley. Off by default.
	bool tile_all_bullet_movement_pattern_paths = false;

	// Face flag for each bullet's own pattern. Entry i belongs to bullet i;
	// bullets without an entry use shared_movement_pattern_face_movement_direction.
	TypedArray<bool> all_bullet_movement_pattern_face_movement_directions;

	// Wrap short face-flag arrays around the volley. Off by default.
	bool tile_all_bullet_movement_pattern_face_movement_directions = false;

	// Repeat flag for each bullet's own pattern. Entry i belongs to bullet i;
	// bullets without an entry use shared_movement_pattern_repeat.
	TypedArray<bool> all_bullet_movement_pattern_repeats;

	// Wrap short repeat-flag arrays around the volley. Off by default.
	bool tile_all_bullet_movement_pattern_repeats = false;

	// WOBBLE (sine/cos flight modulation; editor-friendly danmaku staple).
	// Fallback wobble for bullets whose own entry is missing, null, or
	// disabled. A bullet with an active per-bullet seed always uses it.
	// Null (default) means no fallback — uncovered bullets fly straight.
	// Example: shared gentle weave + one strong per-bullet seed on bullet 2
	// → bullet 2 snakes hard, the rest weave gently.
	Ref<BulletWobbleData2D> shared_bullet_wobble_data;

	// Wobble seed for each bullet, applied at spawn/enable time. Entry i
	// affects bullet i only. Null entries and entries with enabled = false
	// fall back to shared_bullet_wobble_data for that bullet.
	TypedArray<BulletWobbleData2D> all_bullet_wobble_data;

	// Wrap short wobble arrays around the volley. Off by default.
	bool tile_all_bullet_wobble_data = false;

	// GRAVITY / DRAG (2D sideview + tower-defense shells).
	// Constant acceleration added to every bullet each tick (px/s^2).
	// (0, 0) disables. Must stay finite.
	Vector2 gravity = Vector2(0, 0);

	// Per-bullet gravity for each bullet. Entry i pulls bullet i only.
	// Empty array (default) means every bullet uses gravity above.
	// A non-finite entry is treated as (0, 0) for that bullet only.
	// Example: gravity (0, 1000) + entries [(0,0), (800,0)] on 3 bullets →
	// bullet 0 falls, bullet 1 drifts right, bullet 2 falls (no entry 2).
	TypedArray<Vector2> all_bullet_gravity;

	// Wrap short gravity arrays around the volley. Off by default.
	bool tile_all_bullet_gravity = false;

	// Gravity time window over volley life (seconds since spawn, measured on
	// curves_elapsed_time): gravity only integrates inside
	// [delay, delay + duration]. delay 0 = immediate; duration 0 = infinite.
	// Lets shells fly straight first, then drop (or drop, then glide).
	// Both must stay finite and >= 0.
	double gravity_delay_sec = 0.0;
	double gravity_duration_sec = 0.0;

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

	// BOUNCE / RICOCHET (spawn-time seed; every value below also exists as a
	// live DirectionalBullets2D property for runtime tuning). Pool reuse
	// re-seeds these on every enable, so direct BulletFactory2D.spawn_* users
	// keep bouncing across reuses without a spawner.

	// Bounce behavior kinds. SIMPLE_RADIAL reflects across the radial axis
	// (bullet pos minus target pos): cheap, robust, looks right on round
	// targets. PRECISE_SHAPE resolves the target's CollisionShape2D and
	// reflects across the analytic surface normal (flat walls bounce flat):
	// slightly costlier, runs only on bounce ticks, falls back to radial
	// when the target has no usable shape.
	enum BounceMode {
		BOUNCE_SIMPLE_RADIAL = 0,
		BOUNCE_PRECISE_SHAPE = 1
	};

	// Bitmask of target collision layers that trigger a bounce. Checked
	// against the collided object's collision_layer and takes precedence
	// over the normal collision path. 0 (default) disables bouncing.
	// NOTE: these bits must overlap collision_mask, otherwise the bullet
	// never detects those targets at all (warned once per spawn).
	int bounce_mask = 0;

	// Speed multiplier applied on every bounce. 1.0 (default) is perfectly
	// elastic, < 1 damps (0.5 halves the speed), 0 dead-stops the bullet in
	// place (still alive), > 1 is super-elastic. Clamped to [0, 2].
	double bounce_strength = 1.0;

	// Whether a bounce also counts as a normal hit (increments the collision
	// counter and can disable the bullet at bullet_max_collision_count).
	// False (default) means free bounces that never kill the bullet.
	bool bounce_hit_consumed = false;

	// How many times a single bullet may bounce before bounces stop working
	// for it (further hits take the normal collision path). 0 (default) =
	// unlimited (lifetime still bounds it). Must stay >= 0.
	int bounce_max_count = 0;

	// Which normal estimator to use (see BounceMode). Default radial.
	int bounce_mode = BOUNCE_SIMPLE_RADIAL;

	// Whether a bounce re-aims the bullet visual at the reflected heading.
	// Turn off when something else owns the facing (rotation data, patterns).
	bool bounce_rotate_texture = true;

	// Max visual turn rate in radians/sec toward the reflected heading.
	// 0 (default) snaps instantly (right for bullets); > 0 slews smoothly
	// like homing_smoothing. Ballistics always reflect instantly, only the
	// visual lags. Ignored while adjust_direction_based_on_rotation is on
	// (the visual owns ballistics there, so it must snap).
	double bounce_rotation_smooth = 0.0;

	// Random scatter in degrees applied symmetrically around the reflected
	// heading (+/- randomness per bounce). 0 (default) = exact reflection.
	// Must stay finite in [0, 180].
	double bounce_randomness_deg = 0.0;

	// Quiet period in seconds after a bounce during which further bounce
	// records for that bullet are swallowed (lets it escape the overlap).
	// Prevents double-flip when a target carries both a body and an area.
	// Must stay finite in [0, 1]. Default 0.05.
	double bounce_cooldown_sec = 0.05;

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

	bool get_tile_all_bullet_speed_data() const;
	void set_tile_all_bullet_speed_data(bool value);

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

	bool get_tile_all_bullet_curves_data() const;
	void set_tile_all_bullet_curves_data(bool value);

	TypedArray<NodePath> get_all_bullet_movement_pattern_paths() const;
	void set_all_bullet_movement_pattern_paths(const TypedArray<NodePath> &new_paths);

	bool get_tile_all_bullet_movement_pattern_paths() const;
	void set_tile_all_bullet_movement_pattern_paths(bool value);

	TypedArray<bool> get_all_bullet_movement_pattern_face_movement_directions() const;
	void set_all_bullet_movement_pattern_face_movement_directions(const TypedArray<bool> &new_flags);

	bool get_tile_all_bullet_movement_pattern_face_movement_directions() const;
	void set_tile_all_bullet_movement_pattern_face_movement_directions(bool value);

	TypedArray<bool> get_all_bullet_movement_pattern_repeats() const;
	void set_all_bullet_movement_pattern_repeats(const TypedArray<bool> &new_flags);

	bool get_tile_all_bullet_movement_pattern_repeats() const;
	void set_tile_all_bullet_movement_pattern_repeats(bool value);

	Ref<BulletWobbleData2D> get_shared_bullet_wobble_data() const;
	void set_shared_bullet_wobble_data(const Ref<BulletWobbleData2D> &new_wobble_data);

	TypedArray<BulletWobbleData2D> get_all_bullet_wobble_data() const;
	void set_all_bullet_wobble_data(const TypedArray<BulletWobbleData2D> &new_data);

	bool get_tile_all_bullet_wobble_data() const;
	void set_tile_all_bullet_wobble_data(bool value);

	Vector2 get_gravity() const;
	void set_gravity(const Vector2 &value);

	TypedArray<Vector2> get_all_bullet_gravity() const;
	void set_all_bullet_gravity(const TypedArray<Vector2> &new_data);

	bool get_tile_all_bullet_gravity() const;
	void set_tile_all_bullet_gravity(bool value);

	double get_gravity_delay_sec() const;
	void set_gravity_delay_sec(double value);

	double get_gravity_duration_sec() const;
	void set_gravity_duration_sec(double value);

	double get_linear_drag() const;
	void set_linear_drag(double value);

	double get_homing_delay_sec() const;
	void set_homing_delay_sec(double value);

	double get_homing_duration_sec() const;
	void set_homing_duration_sec(double value);

	double get_homing_lose_range_px() const;
	void set_homing_lose_range_px(double value);

	int get_bounce_mask() const;
	void set_bounce_mask(int value);
	void set_bounce_mask_from_array(const TypedArray<int> &numbers);

	double get_bounce_strength() const;
	void set_bounce_strength(double value);

	bool get_bounce_hit_consumed() const;
	void set_bounce_hit_consumed(bool value);

	int get_bounce_max_count() const;
	void set_bounce_max_count(int value);

	int get_bounce_mode() const;
	void set_bounce_mode(int value);

	bool get_bounce_rotate_texture() const;
	void set_bounce_rotate_texture(bool value);

	double get_bounce_rotation_smooth() const;
	void set_bounce_rotation_smooth(double value);

	double get_bounce_randomness_deg() const;
	void set_bounce_randomness_deg(double value);

	double get_bounce_cooldown_sec() const;
	void set_bounce_cooldown_sec(double value);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBulletsData2D::BounceMode);
