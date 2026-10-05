#pragma once

#include "core/warn_once2d.hpp"
#include "core/transform_math2d.hpp"
#include "factory/bullet_factory2d.hpp"
#include "attachments/bullet_attachment2d.hpp"
#include "attachments/bullet_attachment_object_pool2d.hpp"
#include "data/bullet_effect_layer_data2d.hpp"
#include "data/bullet_rotation_data2d.hpp"
#include "core/cached_string_names2d.hpp"
#include "core/reentrancy_guard2d.hpp"
#include "data/bullet_volley_data2d.hpp"
#include "godot_cpp/classes/curve.hpp"
#include "godot_cpp/classes/curve2d.hpp"
#include "godot_cpp/classes/path2d.hpp"
#include "godot_cpp/classes/ref.hpp"
#include "godot_cpp/classes/texture2d.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/defs.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/math_defs.hpp"
#include "godot_cpp/core/memory.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/core/print_string.hpp"
#include "godot_cpp/core/property_info.hpp"
#include "godot_cpp/variant/callable.hpp"
#include "godot_cpp/variant/callable_method_pointer.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/variant.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "data/bullet_curves_data2d.hpp"
#include "bullet_volley/bullet_movement_pattern_data2d.hpp"
#include "data/bullet_speed_data2d.hpp"
#include "core/collision_shape_helper2d.hpp"
#include "core/dynamic_sparse_set.hpp"
#include "pooling/volley_pool_key2d.hpp"
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/shape2d.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <iterator>
#include <vector>
#include "data/bullet_wobble_data2d.hpp"
#include "bullet_volley/homing_target_deque.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

namespace BlastBullets2D {
using namespace godot;

class VolleyPool;

// One volley: N bullets drawn by a single MultiMeshInstance2D, collided by
// one physics area (one shared shape) and moved by one per-bullet loop
// (move_bullets). Spawned and pooled by BulletFactory2D, steered through
// the returned instance or by the BulletSpawner2D that fired it.
//
// This header declares everything; the bodies live in one file per feature
// (src/bullet_volley/):
//   bullet_volley2d.cpp             lifecycle: spawn, pool reuse, per-bullet enable/disable, teardown
//   bullet_volley2d_setup.cpp       MultiMesh + render buffer setup
//   bullet_volley2d_tick.cpp        move_bullets, the per-bullet motion loop (hot path)
//   bullet_volley2d_render.cpp      physics interpolation pass
//   bullet_volley2d_collision.cpp   area/shape, collision intake, dedup, drain, hit counts
//   bullet_volley2d_bounce.cpp      bounce knobs + bounce decision
//   bullet_volley2d_homing.cpp      homing target queues + steering knobs
//   bullet_volley2d_orbit.cpp       orbiting
//   bullet_volley2d_motion.cpp      speed, direction, velocity, transforms, rotation
//   bullet_volley2d_curves.cpp      bullet curves + movement patterns
//   bullet_volley2d_wobble.cpp      wobble
//   bullet_volley2d_gravity.cpp     gravity + drag
//   bullet_volley2d_lifetime.cpp    lifetime + expiry
//   bullet_volley2d_timers.cpp      attach_time_based_function timers
//   bullet_volley2d_attachments.cpp attachments
//   bullet_volley2d_effects.cpp     sprite effect layers + trails
//   bullet_volley2d_animation.cpp   sprite animation, fade/tint, custom data
//   bullet_volley2d_teleport.cpp    teleporting
//   bullet_volley2d_debug.cpp       debug_* introspection
//   bullet_volley2d_bindings.cpp    Godot bindings (methods, properties, signals)
//   bullet_volley2d_internal.hpp    inline helpers shared by several of the files above
// Members marked _ALWAYS_INLINE_ are defined in the .cpp that uses them or in
// bullet_volley2d_internal.hpp: only the volley's own files may call them.
class BulletVolley2D : public MultiMeshInstance2D {
	GDCLASS(BulletVolley2D, MultiMeshInstance2D)
public:
	// Godot's memnew cannot forward constructor arguments, so instances are created
	// with memnew and then initialized through spawn(). Always call spawn() after memnew.

	// Whether all the bullets should be processed/moved/rotated etc.. or just skipped (basically this value should be equal to false only when ALL bullets are completely disabled)
	bool is_active = false;

	// The id of the multimesh inside the bullet factory's sparse set
	int sparse_set_id = -1;

	// Counts spawn/enable cycles. Deferred attachment disables carry the value they were
	// queued with so pool reuse in between can't misfire them onto a new owner.
	int volley_generation = 0;


	// DEFERRED-WORK CONTRACT (mandatory for every call_deferred addition):
	// pool reuse, frees, and re-homes can all land between queue and flush, so
	// 1. stamp the current generation (plus epoch/instance-id where they apply)
	//    at queue time;
	// 2. at flush, drop on generation mismatch FIRST, then re-resolve ids via
	//    ObjectDB::get_instance, then re-validate ownership/slot/active state;
	// 3. queue on the object whose lifetime covers the flush, and never carry
	//    raw pointers across the defer (Godot drops the call if that object
	//    died, which is the safe outcome);
	// 4. emits that must survive a pool hand-over resolve the emitter fresh and
	//    apply the re-ownership check - never trust a cached pointer.
	// Current inventory (all compliant): life_time_over, sprite_animation_
	// finished, deferred attachment disables, timer attach/detach/execute,
	// homing reached-emits/auto-pops (generation + per-bullet epoch), spawner
	// shoot_once_deferred (full revalidation at flush), debugger restore
	// (idempotent flag, needs no generation).

	bool marked_for_internal_deletion = false;

	// True while the factory runs this volley's tick (move_bullets, its
	// collision drain, animation, reduce_lifetime). User code fired from
	// inside (collision handlers, attachment callbacks) may spawn: the pool
	// skips a volley mid-tick, and enable_volley refuses it, so the
	// drain can never be pulled out from under itself. Every OTHER pooled
	// volley stays reusable mid-sweep (same-key spawns from handlers work).
	bool is_being_ticked = false;


	// Gets the total amount of bullets that the multimesh always holds
	_ALWAYS_INLINE_ int get_amount_bullets() const { return amount_bullets; };

	// Gets the total amount of attachments that are active
	int get_amount_active_attachments() const;

	// Used to spawn brand new bullets that are active in the scene tree.
	// spawner_id stamps signal ownership BEFORE any physics/tree activation
	// (configure-then-attach): a spawner passes its instance id so the volley
	// is never observable as factory-owned. 0 = factory-owned (default).
	void spawn(const BulletVolleyData2D &spawn_data, VolleyPool *pool, BulletFactory2D *factory, Node *bullets_container, const Vector2 &new_inherited_velocity_offset, int new_sparse_set_id, bool spawn_in_pool, uint64_t spawner_id = 0);

	// Native transform source for the NEXT spawn()/enable_volley(): when
	// set, the volley reads its bullet transforms from this span instead of
	// unboxing data.transforms (a Variant per bullet). The factory sets it
	// around the call; set_up_bullet_instances consumes and clears it.
	// Resource id of the data being applied (spawn/enable): keys the
	// once-per-resource configuration warnings (WarnOnce2D).
	uint64_t warn_data_id = 0;
	const Transform2D *spawn_transforms_ptr = nullptr;
	int spawn_transforms_count = 0;
	_ALWAYS_INLINE_ int spawn_transform_count(const BulletVolleyData2D &data) const {
		return spawn_transforms_ptr != nullptr ? spawn_transforms_count : (int)data.transforms.size();
	}

	// Activates the multimesh. Returns false (without leaving it factory-active)
	// when the spawn data is incompatible, so the pool owner can re-push it.
	// spawner_id works like spawn()'s: stamped before re-activation.
	bool enable_volley(const BulletVolleyData2D &data, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id = 0);
	// GDScript entry for enable_volley (Ref-based; native callers use the
	// reference overload directly). Returns false on null data without touching state.
	bool enable_volley_for_script(const Ref<BulletVolleyData2D> &data, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0), uint64_t spawner_id = 0);

	// Internal delete, C++ side only. Never call this from GDScript or from inside a
	// physics callback; factory free/reset methods already call it for you at a safe time.
	void force_delete();

	/// METHODS RESPONSIBLE FOR VARIOUS BULLET FEATURES

	// Caches hold GLOBAL-space transforms, but MultiMesh instance slots are LOCAL
	// to this MultiMeshInstance2D (rendered global = node_global * instance).
	// Convert on every visual write so a moved factory/multimesh doesn't offset
	// rendering away from physics (physics areas are node-independent).
	// A degenerate (zero-scale) node global has no inverse: fall back to the
	// raw global transform instead of writing a non-finite inverse into the
	// multimesh buffer.
	// Scoped cache of the node inverse: get_global_transform() crosses the
	// extension boundary, and loops that convert every bullet (tick trails,
	// spawn setup) would otherwise pay it per bullet. Only valid while a
	// NodeInverseScope is alive (no user code may move the node inside).
	mutable bool node_inverse_scope_active = false;
	mutable bool node_inverse_scope_valid = false;
	mutable Transform2D node_inverse_scope;

	struct NodeInverseScope {
		const BulletVolley2D *owner;
		bool saved_active;
		bool saved_valid;
		Transform2D saved_inverse;
		explicit NodeInverseScope(const BulletVolley2D *p_owner) :
				owner(p_owner), saved_active(p_owner->node_inverse_scope_active), saved_valid(p_owner->node_inverse_scope_valid), saved_inverse(p_owner->node_inverse_scope) {
			const Transform2D node_global = owner->get_global_transform();
			owner->node_inverse_scope_valid = is_transform_invertible_safe(node_global);
			owner->node_inverse_scope = owner->node_inverse_scope_valid ? node_global.affine_inverse() : Transform2D();
			owner->node_inverse_scope_active = true;
		}
		~NodeInverseScope() {
			owner->node_inverse_scope_active = saved_active;
			owner->node_inverse_scope_valid = saved_valid;
			owner->node_inverse_scope = saved_inverse;
		}
		NodeInverseScope(const NodeInverseScope &) = delete;
		NodeInverseScope &operator=(const NodeInverseScope &) = delete;
	};

	_ALWAYS_INLINE_ Transform2D to_local_for_multimesh(const Transform2D &global_transf) const;

	// Use this method when you want to use physics interpolation - smooth rendering of textures despite physics ticks per second
	void interpolate_bullet_visuals();

	_ALWAYS_INLINE_ void batch_flush_instance_transforms();

	_ALWAYS_INLINE_ void update_specific_previous_transforms_for_interpolation(int begin_bullet_index, int end_bullet_index_inclusive);

	void update_all_previous_transforms_for_interpolation();

	// Updates interpolation data for physics
	_ALWAYS_INLINE_ void update_bullet_previous_transform_for_interpolation(int bullet_index);

	_ALWAYS_INLINE_ Transform2D get_interpolated_transform(const Transform2D &curr_transf, const Transform2D &prev_transf, double fraction);

	// Advances the volley clock (curves, fade) and the lifetime countdown;
	// at zero runs expire_live_bullets().
	void reduce_lifetime(double delta);
	// Expiry: emits life_time_over LIVE (every listed bullet alive), then
	// disables the bullets the handler left untouched (On Lifetime Over each)
	// unless the handler vetoed (infinite lifetime or set_life_time_left).
	void expire_live_bullets();
	std::vector<int> expiry_indexes_scratch;
	std::vector<uint64_t> expiry_epochs_scratch;
	// Remaining lifetime in seconds (0 for infinite volleys). The setter
	// extends or shortens the countdown only (the curve clock keeps
	// running); rejects non-finite / <= 0 and infinite volleys.
	double get_life_time_left() const;
	void set_life_time_left(double seconds);

	// Advances the SpriteFrames animation baked in anim_frames/anim_frame_secs.
	// Hot path: plain countdown + index + one set_texture. No SpriteFrames calls here.
	// Texture swaps are interpolation-exempt (interpolation only lerps transforms).
	void advance_sprite_animation(double delta);
	///

	TypedArray<bool> get_all_bullets_status();

	bool is_bullet_status_enabled(int bullet_index);

	_ALWAYS_INLINE_ Ref<Resource> get_shared_bullets_custom_data() const {
		return shared_bullets_custom_data;
	}

	_ALWAYS_INLINE_ void set_shared_bullets_custom_data(const Ref<Resource> &new_shared_bullets_custom_data) {
		shared_bullets_custom_data = new_shared_bullets_custom_data;
	}

	// Per-bullet custom data (seeded from spawn data). Strictly separated from
	// shared_bullets_custom_data: a bullet with no per-bullet value reads as
	// null, never as the shared value, so the two can never be confused.
	Ref<Resource> bullet_get_custom_data(int bullet_index) const;
	void bullet_set_custom_data(int bullet_index, const Ref<Resource> &new_custom_data);

	TypedArray<Resource> all_bullets_get_custom_data(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);
	void all_bullets_set_custom_data(const Ref<Resource> &new_custom_data, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	Vector2 get_inherited_velocity_offset() const { return inherited_velocity_offset; }
	void set_inherited_velocity_offset(const Vector2 &new_offset);

	bool get_is_auto_pooling_enabled() const { return is_auto_pooling_enabled; }
	// Turning pooling back ON for a parked volley pools it at once.
	void set_is_auto_pooling_enabled(bool value);

	bool get_is_attachments_auto_pooling_enabled() const { return is_attachments_auto_pooling_enabled; }
	void set_is_attachments_auto_pooling_enabled(bool value) { is_attachments_auto_pooling_enabled = value; }

	// Pooling flags reset to defaults on every new life (spawn/enable via
	// begin_life): a pooled reuse never inherits "don't pool"
	// from a previous manual owner. Set the flags explicitly after every
	// spawn/enable when you want manual ownership. Same-owner enable_bullet()
	// wakes keep flags by design (no new life starts).
	void reset_pooling_flags_to_default();

	Ref<BulletCurvesData2D> get_shared_bullet_curves_data() const { return shared_bullet_curves_data; }
	void set_shared_bullet_curves_data(const Ref<BulletCurvesData2D> &new_curves_data);
	bool has_shared_bullet_curves_data() const { return shared_bullet_curves_data.is_valid(); }
	void remove_shared_bullet_curves_data() { set_shared_bullet_curves_data(Ref<BulletCurvesData2D>()); }

	// Re-bakes the animation cache from a SpriteFrames resource and switches to it.
	// An empty animation name is rejected with an error (returns false, previous
	// animation untouched). The default "default" animation name auto-resolves via
	// the same rules as spawn: "default" if present, else the first animation.
	bool play_sprite_animation(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_animation = StringName("default"));
	// Reuses the cached SpriteFrames source. Same resolve rules as play_sprite_animation.
	bool play_sprite_animation_name(const StringName &p_animation);
	bool restart_sprite_animation();
	void stop_sprite_animation() { anim_paused = true; }
	void resume_sprite_animation() { anim_paused = false; }
	bool is_sprite_animation_playing() const { return is_active && !anim_paused && !anim_finished && anim_frames.size() > 1; }
	bool is_sprite_animation_finished() const { return anim_finished; }

	StringName get_sprite_animation() const { return anim_name; }
	Ref<SpriteFrames> get_sprite_frames() const { return anim_source; }
	int get_sprite_frame() const { return anim_frame_index; }
	int get_sprite_frame_count() const { return (int)anim_frames.size(); }

	// Bullet Speed Data

	Ref<BulletSpeedData2D> get_bullet_speed_data(int bullet_index) const;
	void set_bullet_speed_data(int bullet_index, const Ref<BulletSpeedData2D> &new_bullet_speed_data);

	TypedArray<BulletSpeedData2D> all_bullets_get_speed_data(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	void all_bullets_set_speed_data(const Ref<BulletSpeedData2D> &new_bullet_speed_data, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Bullet Direction

	Vector2 get_bullet_direction(int bullet_index) const;
	void set_bullet_direction(int bullet_index, const Vector2 &new_direction);

	TypedArray<Vector2> all_bullets_get_direction(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	void all_bullets_set_direction(const Vector2 &new_direction, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void set_bullet_direction_towards_position(int bullet_index, const Vector2 &target_position);
	void all_bullets_set_direction_towards_position(const Vector2 &target_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void set_bullet_direction_towards_node2d(int bullet_index, const Node2D *target_node);
	void all_bullets_set_direction_towards_node2d(const Node2D *target_node, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Bullet Texture Rotation (Radians)

	real_t get_bullet_texture_rotation_radians(int bullet_index) const;
	void set_bullet_texture_rotation_radians(int bullet_index, real_t new_rotation_radians);

	TypedArray<real_t> all_bullets_get_texture_rotation_radians(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	void all_bullets_set_texture_rotation_radians(real_t new_rotation_radians, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void set_bullet_texture_rotation_towards_position(int bullet_index, const Vector2 &target_position);
	void all_bullets_set_texture_rotation_towards_position(const Vector2 &target_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void set_bullet_texture_rotation_towards_node2d(int bullet_index, const Node2D *target_node);
	void all_bullets_set_texture_rotation_towards_node2d(const Node2D *target_node, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Bullet Texture Rotation (Degrees)

	real_t get_bullet_texture_rotation_degrees(int bullet_index) const;
	void set_bullet_texture_rotation_degrees(int bullet_index, real_t new_rotation_degrees);

	TypedArray<real_t> all_bullets_get_texture_rotation_degrees(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	void all_bullets_set_texture_rotation_degrees(real_t new_rotation_degrees, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Bullet Transforms

	// Bullet transform in the same global space as the movement caches.
	// get_bullet_global_transform() is the explicit-world alias; set_bullet_transform
	// accepts the same space so get()/set() round-trip without a node-transform shift.
	Transform2D get_bullet_transform(int bullet_index) const;
	void set_bullet_transform(int bullet_index, const Transform2D &new_transform, bool set_direction_based_on_transform = false);

	// Instance transform in global space. The caches already hold global-space
	// transforms, so this returns the cache directly.
	// Use this for gameplay logic such as spawning child bullets at a bullet's position.
	Transform2D get_bullet_global_transform(int bullet_index) const;

	// Logical velocity of a bullet (direction x speed + inherited offset).
	// Excludes per-tick pattern/orbit displacement: those steer the movement
	// delta after this value is computed, so this stays the stable gameplay
	// read (splitting bullets, speed checks) rather than the rendered delta.
	Vector2 get_bullet_velocity(int bullet_index) const;

	TypedArray<Transform2D> all_bullets_get_transforms(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	void all_bullets_set_transforms(const Transform2D &new_transform, bool set_direction_based_on_transform = false, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	real_t get_curves_elapsed_time() const;
	void set_curves_elapsed_time(real_t new_time);

	// Volley fade state (spawn-data fade_in_sec/fade_out_sec/modulate_ramp,
	// snapshotted per life so pool reuse never leaks the previous tint).
	// The tick owns CanvasItem self_modulate while any fade is configured;
	// change the base through set_fade_base_modulate(), not the CanvasItem
	// setter (which the next tick would overwrite).
	Color fade_base_modulate = Color(1, 1, 1, 1);
	double fade_in_sec = 0.0;
	double fade_out_sec = 0.0;
	Ref<Gradient> fade_modulate_ramp;
	// Bullet whiten override, snapshotted per life like the fade knobs
	// above. Live toggle rebuilds through set_override_frame_color().
	bool anim_override_frame_color = false;
	bool get_override_frame_color() const;
	void set_override_frame_color(bool value);
	// Last modulate written by the fade tick: compared before writing so
	// settled volleys skip the CanvasItem call entirely.
	Color fade_applied = Color(1, 1, 1, 1);
	double get_fade_in_sec() const;
	void set_fade_in_sec(double value);
	double get_fade_out_sec() const;
	void set_fade_out_sec(double value);
	Ref<Gradient> get_modulate_ramp() const;
	void set_modulate_ramp(const Ref<Gradient> &value);
	Color get_fade_base_modulate() const;
	void set_fade_base_modulate(const Color &value);
	// Copies base tint + fade knobs + whiten flag from spawn data (called on
	// every spawn/enable, before the animation rebuild so whitening applies
	// to the fresh frames). Starts transparent immediately when fade-in is
	// on so no full-alpha frame flashes before the first tick.
	void snapshot_appearance_from_data(const BulletVolleyData2D &data);
	// One fade step, driven by curves_elapsed_time (the volley age clock).
	// No-op unless a fade is configured. Per-volley O(1): one CanvasItem
	// write at most, only while the value actually changes.
	void tick_volley_fade();

	// Bullet movement pattern

	bool check_exists_bullet_movement_pattern_data(int bullet_index) const;

	_ALWAYS_INLINE_ BulletMovementPatternData2D find_bullet_movement_pattern_data(int bullet_index) const {
		return all_movement_pattern_data[bullet_index];
	}

	Ref<Curve2D> get_bullet_movement_pattern_curve(int bullet_index) const;

	void set_bullet_movement_pattern_from_path(int bullet_index, Path2D *path_holding_pattern, bool face_movement_direction = false, bool repeat_pattern = true);
	void all_bullets_set_movement_pattern_from_path(Path2D *path_holding_pattern, bool face_movement_direction = false, bool repeat_pattern = true, int start_index = 0, int end_index_inclusive = -1);

	void set_bullet_movement_pattern_from_curve(int bullet_index, const Ref<Curve2D> &curve_pattern, bool face_movement_direction = false, bool repeat_pattern = true);
	void all_bullets_set_movement_pattern_from_curve(const Ref<Curve2D> &curve_pattern, bool face_movement_direction = false, bool repeat_pattern = true, int start_index = 0, int end_index_inclusive = -1);

	void remove_bullet_movement_pattern(int bullet_index);
	void all_bullets_remove_movement_pattern(int start_index = 0, int end_index_inclusive = -1);

	int get_collision_layer() const;
	void set_collision_layer(int new_collision_layer);
	void set_collision_layer_from_array(const TypedArray<int> &numbers);

	int get_collision_mask() const;
	void set_collision_mask(int new_collision_mask);
	void set_collision_mask_from_array(const TypedArray<int> &numbers);

	bool get_monitorable() const;
	void set_monitorable(bool value);

	Ref<Shape2D> get_collision_shape() const { return cached_collision_shape; }
	// Swap the collision shape mid-flight. Resizing the same shape type is instant; switching shape types rebuilds physics and can move pool buckets. The sprite quad never changes here - it only cares about textures.
	void set_collision_shape_runtime(const Ref<Shape2D> &new_shape);
	// Idle-frame twin for handlers: queues set_collision_shape_runtime on the
	// factory's structural queue (next process frame, outside physics).
	void set_collision_shape_runtime_deferred(const Ref<Shape2D> &new_shape);
	PoolKey get_pool_key() const { return PoolKey{ amount_bullets, cached_effective_shape_type }; }

	void _notification(int p_what);

	bool is_auto_pooling_enabled = true;

	bool is_attachments_auto_pooling_enabled = true;

	// Set by the pool itself - tells us at a glance whether this volley is sitting in a bucket right now.
	bool is_pooled_in_pool = false;

	// How many bullets are currently flying
	int active_bullets_counter = 0;

	// Which bullets are still alive (fast add/remove/lookup while the tick runs)
	DynamicSparseSet all_bullets_enabled_set;

	BulletFactory2D *bullet_factory = nullptr;
	// Optional spawner owner (ObjectID, 0 = none), mirroring owner_volley_id
	// on attachments. When a live BulletSpawner2D is tagged, collision and
	// lifetime signals are possessed by it; otherwise they stay on the factory.
	// Plain integer: no ownership, nothing to clean up on free. Reset in
	// spawn()/enable_volley() so pooled reuse never inherits a stale owner;
	// stamped by BulletSpawner2D::shoot_once() after every spawn it performs.
	uint64_t owner_spawner_id = 0;
	VolleyPool *bullets_pool = nullptr;
	PhysicsServer2D *physics_server = nullptr;

	// ONE server shape per volley, added to the area once per bullet (area
	// shape index == bullet index, each with its own transform). Bullets of a
	// volley always share the same shape data, so N RIDs were pure overhead:
	// worse, every shape_set_data() made the physics server re-update EVERY
	// shape of the owning area, so a cold N-bullet spawn cost O(N^2)
	// (8k bullets: 1.5 s). Data is pushed once per volley: O(N) total.
	RID volley_shape;
	int area_shape_count = 0;

	// This is used to effectively hide a single bullet instance from being rendered by the multimesh
	static inline const Transform2D zero_transform = Transform2D().scaled(Vector2(0, 0));
	// Shared debugger can also read the zero transform to hide disabled shapes.
	// (Exposed as static so it's reachable without an instance.)

	///

	/// ROTATION RELATED

	std::vector<real_t> all_rotation_speed;
	std::vector<real_t> all_max_rotation_speed;
	std::vector<real_t> all_rotation_acceleration;

	// If set to false it will also rotate the collision shapes
	bool rotate_only_textures = false;

	// True once any rotation data was seeded - the tick only spins bullets while this is on
	bool is_rotation_data_active = false;
	bool get_is_rotation_data_active() const { return is_rotation_data_active; }
	// Effective per-bullet rotation speed (seeded value, live-updated by the tick).
	real_t bullet_get_rotation_speed(int bullet_index) const;
	// Full per-bullet rotation triple, mirroring get_bullet_speed_data: a
	// fresh resource carrying this bullet's live rotation/max/accel values.
	// Lets you read one bullet's spin, tweak it, and write it back with
	// set_bullet_rotation_data. Out-of-range reads return a zeroed resource.
	Ref<BulletRotationData2D> get_bullet_rotation_data(int bullet_index) const;
	// Live per-bullet rotation write (mirrors set_bullet_speed_data). Null
	// entries are rejected; non-finite values are rejected; max/accel follow
	// the same unlimited-when-max<=0 convention as the tick.
	void set_bullet_rotation_data(int bullet_index, const Ref<BulletRotationData2D> &new_bullet_rotation_data);
	// Rotation triples over a range (mirrors all_bullets_get_speed_data).
	TypedArray<BulletRotationData2D> all_bullets_get_rotation_data(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	// One rotation triple fanned over a range (mirrors all_bullets_set_speed_data).
	void all_bullets_set_rotation_data(const Ref<BulletRotationData2D> &new_bullet_rotation_data, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);
	// Turns rotation off for the whole volley (same as seeding an empty
	// array). Re-seed with spawn/enable or set_bullet_rotation_data to spin
	// again.
	void clear_bullet_rotation_data();

	// If set to true, it will stop the rotation when the max rotation speed is reached
	bool stop_rotation_when_max_reached = false;

	///

	/// BULLET ATTACHMENT RELATED

	// Per-slot attachment assignment epoch: bumped on EVERY ownership change
	// (attach, detach, disable, free, reclaim, reuse-blank). The deferred
	// disable carries (generation + attachment id + pointer + epoch); the
	// flush must match all four. Generation alone cannot catch same-life
	// reuse: the pool can return the SAME node to the SAME slot in the SAME
	// generation (classic ABA), and pointer+id then match a stale request.
	// Sized to amount_bullets at spawn; reset on reuse.
	std::vector<uint64_t> attachment_assignment_epochs;

	_ALWAYS_INLINE_ void bump_attachment_epoch(int bullet_index);

	_ALWAYS_INLINE_ uint64_t attachment_epoch_for(int bullet_index) const;

	// Object-level collision dedup (default ON): one logical hit per
	// (bullet, target) per drain window. The physics server reports per
	// SHAPE (body+area pair or multi-shape target queues N records for one
	// overlap); without this the counter/signals/bounce fire N times. When
	// false, every shape record is delivered (legacy shape-level).
	bool collision_dedup_by_object = true;

	// Packed (bullet_index, instance_id) keys of everything already queued this
	// drain window. The dedup used to scan all_collided_bullets linearly, which
	// is O(n^2) per physics step: 10k bullets overlapping one target cost ~50M
	// comparisons in a single frame. This open-addressed set makes each insert
	// O(1). Cleared wherever all_collided_bullets is drained or reset.
	std::vector<uint64_t> collision_dedup_slots;
	uint32_t collision_dedup_slot_mask = 0;
	uint32_t collision_dedup_slot_used = 0;

	// 64-bit FNV-1a over both fields; the result is never 0, which is what
	// lets 0 mean "empty slot" in the open-addressed table below. 64 bits,
	// not 32: at 10k entries a 32-bit key collides with ~1% probability and
	// silently merges two unrelated pairs (a real hit gets dropped); at
	// 64 bits that probability is ~5e-12.
	static uint64_t collision_dedup_key(int bullet_index, int64_t instance_id);

	_ALWAYS_INLINE_ bool collision_already_queued(int bullet_index, int64_t entered_instance_id);

	_ALWAYS_INLINE_ void mark_collision_queued(int bullet_index, int64_t entered_instance_id);

	void clear_collision_dedup_keys();

	// Bound so both dedup modes are reachable and testable. Default true (one
	// logical hit per bullet/target per frame). Set false to restore the legacy
	// per-SHAPE delivery, where a 3-shape target counts three hits.
	bool get_collision_dedup_by_object() const { return collision_dedup_by_object; }
	void set_collision_dedup_by_object(bool value);

	// Runtime twin of BulletVolleyData2D.emit_collision_signals (seeded every
	// life): false = hits/bounces still count, kill and reflect, but no
	// collision signal fires and no unhandled-hit warning is printed.
	bool emit_collision_signals = true;
	bool get_emit_collision_signals() const { return emit_collision_signals; }
	void set_emit_collision_signals(bool value) { emit_collision_signals = value; }

	// Unhandled-hit diagnostics (once per emitter, see warn_unhandled_*).
	// The emitter check runs once per drain, not per record.
	bool drain_handlers_checked = false;
	// Resolves the emitter of a hit/bounce/lifetime signal and warns once
	// when nothing can handle it: the owning spawner was freed (every
	// connection died with it) or the live emitter has no hit handler
	// connected. Returns the emitter (null when gone).
	Object *resolve_hit_emitter_checked();
	Object *resolve_lifetime_emitter_checked();
	// Path of the owning spawner, recorded by the spawner when it is freed
	// with orphaned_volleys = Keep Flying (zero per-shot cost); only read by
	// the orphan warnings. Cleared on every new life.
	String orphaned_spawner_path;

	// Table-level introspection for tests (test_volley_dedup_table.gd). These
	// operate on the LIVE dedup table, not a shadow copy, so call them on an
	// idle volley with no overlaps in flight.
	void debug_dedup_reset() { clear_collision_dedup_keys(); }
	bool debug_dedup_probe(int bullet_index, int64_t target_instance_id);
	void debug_dedup_mark(int bullet_index, int64_t target_instance_id);
	Dictionary debug_dedup_stats() const;
	// Brute-force key-collision search over deterministic distinct pairs.
	// Returns {collided: bool, probes: int}. A 32-bit key finds a collision
	// within a few hundred thousand probes with near certainty; a 64-bit
	// key will not. Cold path: only tests call this.
	Dictionary debug_dedup_find_collision(int probe_count) const;

	// Central transform-invertibility check. get_scale().length_squared()
	// alone accepts singular bases like scale (0,1) (det == 0) whose
	// affine_inverse() is garbage. All conversion/validation paths use this.
	static bool is_transform_invertible_safe(const Transform2D &t) {
		return is_transform_invertible_safe2d(t);
	}

	// Per-instance shader keys applied via set_instance_shader_parameter().
	// Clearing the C++ Dictionary does NOT clear the CanvasItem overrides,
	// so pooled reuse (A with params -> B without) would leak A's visuals.
	// finalize_set_up() resets every previously applied key on the empty path.
	std::vector<String> applied_instance_shader_keys;

	void clear_applied_instance_shader_overrides();

	// Stores each bullet's attachment pooling id
	std::vector<uint32_t> attachment_pooling_ids;

	// Stores pointers to all bullet attachments currently in the scene
	std::vector<BulletAttachment2D *> attachments;

	// Re-entrancy latch for bullet_set_attachment (see the guard there): the setup
	// path runs user script callbacks which must not nest another setup call.
	int _attachment_setup_depth = 0;

	// Set while enable_bullet()/disable_bullet() run user script callbacks
	// (on_bullet_enable/on_bullet_disable): nested enable/disable calls on this
	// volley would drift active_bullets_counter vs the sparse set
	// (activate_data dedups, the counter does not) and break pool accounting.
	// Nested calls reject with an error; use call_deferred from those callbacks.
	int _bullet_enable_depth = 0;

	// Stores each attachment's transform data
	std::vector<Transform2D> attachment_transforms;

	// Stores each attachment's offset relative to the bullet's texture center
	std::vector<Vector2> attachment_offsets;

	// Stores each attachment's local transform relative to the bullet's texture center
	std::vector<Transform2D> attachment_local_transforms;

	// Whether the attachment should stick while the bullet is rotating
	std::vector<uint8_t> attachment_stick_relative_to_bullet;

	// 1 while the slot's attachment is suspended (its bullet was disabled
	// with release_attachment = false): it already heard on_bullet_disable,
	// so a later release pools it without a second callback, and a wake
	// resumes it with on_bullet_enable.
	std::vector<uint8_t> attachment_suspended;

	///

	/// SPRITE EFFECT (FX LAYER) RELATED

	// One baked trail layer: per-frame shard nodes (children of the volley,
	// so pooling hides them, freeing is automatic, and relative z tracks the
	// volley), baked textures, and per-bullet phase/toggle state. Shards are
	// rebuilt on spawn/enable reseed and freed on reset/teardown, never per
	// frame. One-shot layers bake at the factory instead (they outlive the
	// volley); this volley only keeps the layer list for trigger routing.
	struct FXTrailBake {
		Ref<BulletEffectLayerData2D> layer;
		int layer_index = -1;
		// Set by play_effect_animation: the shards hold another animation's
		// frames, so a reuse must rebuild from the layer instead.
		bool animation_override = false;
		std::vector<Ref<Texture2D>> frames;
		std::vector<double> secs;
		// Prefix sums of secs (see FXOneShotBake::frame_starts): the per-tick
		// frame lookup binary searches this instead of re-accumulating.
		// Rebuilt wherever secs is assigned.
		std::vector<double> frame_starts;
		double total = 0.0;
		std::vector<MultiMeshInstance2D *> shards;
		// Parallel to shards: each shard's MultiMesh, cached so the per-bullet
		// trail writes skip a get_multimesh() extension call (and Ref churn).
		std::vector<Ref<MultiMesh>> shard_multimeshes;
		std::vector<uint8_t> shard_visible;
		// Live bullets per shard. hide_trail_instances() used to rescan every
		// slot to decide whether a shard could hide (O(N) per disabled bullet,
		// O(N^2) for a full-volley expiry); the count makes it O(1) with
		// identical hide semantics. Maintained at the two assignment sites
		// (frame transition in write_trail_from_global, release in
		// hide_trail_instances) and reset wherever bullet_shard is.
		std::vector<int> shard_refcount;
		std::vector<double> phase;
		std::vector<uint8_t> bullet_on;
		std::vector<int> bullet_shard;
		// Last local transform written per bullet (MultiMesh instance
		// readback is unreliable headless: set_ marks dirty without updating
		// the readable store, so debug reads this mirror instead).
		std::vector<Transform2D> bullet_trail_transf;
		// Last written per-bullet tint (same readback caveat as above).
		std::vector<Color> bullet_trail_tint;

		// Cached shard MultiMesh (falls back to the node when the cache is
		// short, e.g. a shard list rebuilt elsewhere). Raw pointer on purpose:
		// returning a Ref by value costs a reference()/unreference() engine
		// call pair per bullet per tick on the trail write path. The shard
		// node owns its MultiMesh, so the pointer stays valid.
		_ALWAYS_INLINE_ MultiMesh *shard_multimesh(int shard_index) const {
			if (shard_index >= 0 && shard_index < (int)shard_multimeshes.size() && shard_multimeshes[shard_index].is_valid()) {
				return shard_multimeshes[shard_index].ptr();
			}
			const Ref<MultiMesh> owned_by_node = shards[shard_index]->get_multimesh();
			return owned_by_node.ptr();
		}
	};
	std::vector<FXTrailBake> fx_trail_bakes;

	// Layer list retained from the last reseed (spawn/enable/live set), used
	// to route trigger events to the factory one-shot bakes.
	TypedArray<BulletEffectLayerData2D> fx_data_layers;
	// Bit per trigger with at least one enabled one-shot layer (see fx_reseed_from_data).
	uint32_t fx_oneshot_trigger_mask = 0;
	// Bake version per layer index at the last full reseed (0 = null/disabled
	// slot). A pooled reuse with the same layer resources at the same
	// versions soft-resets instead of rebuilding shard nodes and factory
	// bakes (see fx_reseed_from_data).
	// Everything a shard node or factory bake freezes at build time, per
	// layer: a mismatch in any field forces the full rebuild.
	struct FXLayerSnapshot {
		bool present = false;
		uint64_t bake_version = 0;
		int trigger = 0;
		uint64_t material_id = 0;
		Color self_modulate;
		int z_index = 0;
		bool z_as_relative = true;
		int visibility_layer = 0;
		int light_mask = 0;
		int max_instances = 0;
		bool operator==(const FXLayerSnapshot &o) const {
			return present == o.present && bake_version == o.bake_version && trigger == o.trigger && material_id == o.material_id && self_modulate == o.self_modulate && z_index == o.z_index && z_as_relative == o.z_as_relative && visibility_layer == o.visibility_layer && light_mask == o.light_mask && max_instances == o.max_instances;
		}
	};
	static FXLayerSnapshot fx_snapshot_layer(const Ref<BulletEffectLayerData2D> &layer);
	std::vector<FXLayerSnapshot> fx_seeded_snapshots;
	bool fx_layers_match_seeded(const TypedArray<BulletEffectLayerData2D> &layers) const;
	// Hides every live trail instance and rewinds per-bullet trail state,
	// keeping the shard nodes (pooled-reuse fast path).
	void fx_soft_reset_trail_layers();

	// Hot trail write for one bullet: picks the frame from the volley clock
	// plus the bullet's random phase, composes the follow transform, moves
	// shards when the frame changes. Called from both move_bullets loops
	// after the instance transform is final, and from enable_bullet wakes.
	_ALWAYS_INLINE_ void write_trail_instances(int bullet_index);

	// Core trail write from an explicit global pose (physics cache above,
	// lerped pose from the interpolation pass below). Frame, shard routing
	// and per-instance ramp sampling live here so both paths stay identical.
	_ALWAYS_INLINE_ void write_trail_from_global(int bullet_index, const Transform2D &bullet_transf);

	// Hides one bullet across every trail shard (disable path). Retires a
	// shard's visibility when nothing tracks it anymore, so a fully
	// hidden trail costs zero draw calls (empty shards must not stay
	// visible). The next write re-shows it.
	_ALWAYS_INLINE_ void hide_trail_instances(int bullet_index);

	// Rebuilds trail shards from a layer list (spawn/enable/live set).
	// One-shot layers in the same list register at the factory instead.
	void fx_rebuild_trail_layers(const TypedArray<BulletEffectLayerData2D> &layers);
	// Frees trail shard nodes and drops trail state (reset/teardown).
	void fx_clear_trail_layers();
	// Routes one trigger event for one bullet to the factory one-shot bakes.
	void fx_fire_oneshot(int trigger, int bullet_index, const Transform2D &at);
	// Fires ON_SPAWN layers for every bullet (spawn/enable activation).
	void fx_fire_spawn_layers();

	bool has_trail_effects() const;
	// Layer-index guard shared by the per-bullet toggles (single error for
	// range calls instead of one per bullet).
	bool fx_has_trail_layer(int layer_index) const;
	void bullet_set_trail_enabled(int layer_index, int bullet_index, bool trail_on);
	void all_bullets_set_trail_enabled(int layer_index, bool trail_on, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);
	bool play_effect_animation(int layer_index, const StringName &animation);
	// Full reseed from a layer list (spawn/enable/live set): retains it for
	// trigger routing, rebuilds trail shards, re-registers factory one-shot
	// bakes. fire_spawn flashes ON_SPAWN layers (activation only, never on
	// live edits or single-bullet wakes).
	void fx_reseed_from_data(const TypedArray<BulletEffectLayerData2D> &layers, bool fire_spawn);
	TypedArray<BulletEffectLayerData2D> get_effect_layers() const;
	void set_effect_layers(const TypedArray<BulletEffectLayerData2D> &new_layers);
	Dictionary debug_get_effect_layers_info() const;
	// Local-space trail instance transform for tests (zero Transform2D when
	// the bullet has no visible trail instance on that layer).
	Transform2D debug_get_trail_transform(int layer_index, int bullet_index) const;
	///

	/// OTHER

	// Provides inertia to the bullets by adding an additional velocity offset to their movement every physics frame
	Vector2 inherited_velocity_offset = Vector2(0, 0);

	// The amount of bullets the multimesh has
	int amount_bullets = 0;

	// Cached handle so the hot loop doesn't call get_multimesh() per bullet
	Ref<MultiMesh> multi = nullptr;

	// The volley's own quad (used when spawn data has no custom mesh):
	// kept across pool reuse and resized in place instead of reallocated.
	Ref<QuadMesh> owned_quad_mesh;

	// Scratch space for uploading all transforms at once (no per-frame allocations)
	mutable PackedFloat32Array batch_buffer;

	// The user can pass any custom data they desire and have access to it in the area_entered and body_entered function callbacks.
	// Per-bullet overrides via all_bullets_custom_data; strictly separate from
	// this shared value (bullet_get_custom_data returns null when no
	// per-bullet value is set, never this shared value).
	Ref<Resource> shared_bullets_custom_data;

	// Per-bullet custom data, seeded from spawn data. Kept strictly separate
	// from shared_bullets_custom_data: unset entries stay null and never read
	// as the shared value.
	std::vector<Ref<Resource>> all_bullets_custom_data;

	// The max life time before the multimesh gets disabled
	double max_life_time = 0.0;

	// Whether the life_time_over signal will be emitted when the life time of the bullets is over. Tracked by BulletFactory2D
	bool is_life_time_over_signal_enabled = false;

	// The current life time being processed
	double current_life_time = 0.0;

	// Elapsed time from multimesh activation, used for curves
	double curves_elapsed_time = 0.0;

	// Whether the lifetime is infinite - will ignore any lifetime timers
	bool is_life_time_infinite = false;

	// If a ShaderMaterial was provided and it has instance shader parameters, then they should get cached here
	Dictionary instance_shader_parameters;

	/// TEXTURE / ANIMATION RELATED

	// Baked SpriteFrames animation. Rebuilt at spawn/enable/play only; the per-tick
	// advance_sprite_animation() touches just these vectors + set_texture.
	// Texture swaps are interpolation-exempt: physics interpolation only lerps transform
	// buffers, so play/rebuild must never touch all_previous_* caches (and doesn't).
	Ref<SpriteFrames> anim_source;
	StringName anim_name = "default";
	std::vector<Ref<Texture2D>> anim_frames;
	std::vector<double> anim_frame_secs;
	bool anim_loop = true;
	bool anim_paused = false;
	bool anim_finished = false;
	// Set by advance_sprite_animation when the last frame finished; the tick
	// emits sprite_animation_finished right after (live) and clears it.
	bool anim_finished_event_pending = false;
	int anim_frame_index = 0;
	double anim_frame_time_left = 0.0;

	// This is the texture size of the bullets
	Vector2 texture_size = Vector2(0, 0);

	real_t cache_texture_rotation_radians = 0.0;

	Vector2 cache_collision_shape_offset = Vector2(0, 0);

	TypedArray<Transform2D> cache_texture_transforms;

	///

	/// BULLET SPEED RELATED

	std::vector<real_t> all_cached_speed;
	std::vector<real_t> all_cached_max_speed;
	std::vector<real_t> all_cached_acceleration;

	Ref<BulletCurvesData2D> shared_bullet_curves_data = nullptr;
	std::vector<Ref<BulletCurvesData2D>> all_bullet_curves_data;

	///

	/// CACHED CALCULATIONS FOR IMPROVED PERFORMANCE

	// Holds all multimesh instance transforms. I am doing this so I don't have to call multi->get_instance_transform_2d() every frame
	std::vector<Transform2D> all_cached_instance_transforms;

	// Holds all collision shape transforms. I am doing this so I don't have to call physics_server->area_get_shape_transform() every frame
	std::vector<Transform2D> all_cached_shape_transforms;

	// Holds all multimesh instance transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
	std::vector<Vector2> all_cached_instance_origin;

	// Holds all collision shape transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
	std::vector<Vector2> all_cached_shape_origin;

	// Holds all calculated velocities for the bullets. I am doing this to avoid unnecessary calculations. If I know the direction -> calculate the velocity. Update the values only when the velocity changes, otherwise it's just unnecessary to always do Vector2(cos, sin) every frame..
	std::vector<Vector2> all_cached_velocity;

	// Holds all cached directions of the bullets
	std::vector<Vector2> all_cached_direction;

	///

	/// BULLET MOVEMENT PATTERN RELATED

	std::vector<BulletMovementPatternData2D> all_movement_pattern_data;

	///

	// PHYSICS INTERPOLATION RELATED

	// Stores previous bullets transforms for interpolation
	std::vector<Transform2D> all_previous_instance_transf;

	// Stores previous attachment transforms for interpolation
	std::vector<Transform2D> all_previous_attachment_transf;

	//

	/// COLLISION RELATED

	enum CollisionType : uint8_t {
		AREA = 0,
		BODY
	};

	// Per-bullet disable epoch: bumped on every disable AND every wake, so a
	// collision record queued before a mid-drain re-enable mismatches at
	// emit time instead of double-firing for the same overlap. Sized to
	// amount_bullets at spawn; the homing epochs stay separate
	// (they guard deferred homing work, this guards the collision drain).
	std::vector<uint64_t> bullet_collision_epochs;

	_ALWAYS_INLINE_ uint64_t collision_epoch_for_bullet(int bullet_index) const;

	_ALWAYS_INLINE_ void bump_collision_epoch_for_bullet(int bullet_index);

	struct BulletCollisionData2D {
		int64_t collided_instance_id = -1;
		int bullet_index = -1;
		CollisionType collision_type = AREA;
		// Drain stamp: the per-bullet disable epoch observed when the record
		// was queued. A handler that re-enables the same bullet mid-drain
		// bumps its epoch, so the stale second record for the same overlap
		// mismatches and is skipped at emit time instead of double-firing.
		uint64_t queue_bullet_epoch = 0;
		// Queue-time target velocity for the bounce math: the overlap is
		// detected by the physics server but drained later in the volley
		// tick, and scripts can change the target's velocity in between
		// (a charger backing off reads as a pusher at drain and flips the
		// bounce forward through the target). Sampling here keeps the
		// impact-time motion. Invalid when the target exposes no velocity
		// (static/area targets read live as zero either way).
		Vector2 queue_target_velocity = Vector2(0, 0);
		bool queue_target_velocity_valid = false;
		// Queue-time target pose for the velocity-less estimate (see the
		// drain): Area2D hurtboxes, tweened bosses and position-moved
		// statics expose no velocity property, so the drain compares this
		// pose against the live one over the queue-to-drain window.
		// Stamped in PHYSICS FRAME counts, not seconds: the drain runs at
		// the tail of move_bullets while reduce_lifetime advances the age
		// clock afterwards, so a clock stamp would read identical at queue
		// and drain (zero window, estimate dead). Frame counts are global
		// and monotonic, so the queue functions (which never see the tick
		// counter) still stamp a usable window. Pooled
		// reuse clears the record vector, so lifetimes can never mix.
		Vector2 queue_target_position = Vector2(0, 0);
		bool queue_target_position_valid = false;

		BulletCollisionData2D() = default;

		BulletCollisionData2D(int new_bullet_index, int64_t new_collided_instance_id, CollisionType new_collision_type) :
				collided_instance_id(new_collided_instance_id),
				bullet_index(new_bullet_index),
				collision_type(new_collision_type) {}
	};

	// All bullets that have collided this physics frame
	std::vector<BulletCollisionData2D> all_collided_bullets;

	// Reusable drain buffer: swapping into a fresh local every hit-frame would
	// heap-allocate per collision frame. Single-threaded tick, same rationale
	// as the factory iteration_scratch.
	std::vector<BulletCollisionData2D> collision_scratch;

	// How many times a single bullet can collide before being disabled. If you set to 0 the bullet will never be disabled due to collisions.
	int bullet_max_collision_count = 1;

	// The area that holds all collision shapes
	RID area;

	// Saves whether the bullets can detect bodies or not
	bool monitorable = false;

	Ref<Shape2D> cached_collision_shape;

	// Typed cache resolved once at spawn/enable via casting. Physics + debugger branch on this, no per-bullet cast.
	// Default null => circle r16 (diameter 32, same coverage as rect 32, faster physics).
	PhysicsServer2D::ShapeType cached_effective_shape_type = PhysicsServer2D::SHAPE_CIRCLE;
	Vector2 cached_rect_size = Vector2(32, 32);
	float cached_circle_radius = 16.0f;
	float cached_capsule_radius = 8.0f;
	float cached_capsule_height = 24.0f;

	// Holds current collision count for each bullet
	std::vector<int> bullets_current_collision_count;

	//

	/// HELPER METHODS

	// Note: If you wish to debug these functions with the debugger, remove the _ALWAYS_INLINE_ temporarily

	// Validates bullet index and logs error if invalid
	// const char*, not String: this runs on every disable/hit and most
	// per-bullet API calls, and a String parameter heap-allocated a copy of
	// the name on every call. The String is built only on the error path.
	_ALWAYS_INLINE_ bool validate_bullet_index(int bullet_index, const char *function_name) const {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			UtilityFunctions::push_error(String("Invalid bullet index in ") + function_name + ": " + String::num_int64(bullet_index) + " (amount_bullets: " + String::num_int64(amount_bullets) + ")");
			return false;
		}
		return true;
	}

	// Range contract for every all_bullets_* API: end == -1 means "through the
	// last bullet" (the documented default); any other out-of-range or
	// inverted index FAILS LOUD and applies NOTHING. (It used to clamp silently
	// to the whole volley, so a typo like (99, 99) on a 10-bullet volley
	// rewrote every bullet.) On rejection the range is left empty
	// (start > end) so callers' loops do nothing and getters return [].
	_ALWAYS_INLINE_ bool ensure_indexes_match_amount_bullets_range(int &bullet_index_start, int &bullet_index_end_inclusive, const char *function_name) const;

	// Resolve Ref<Shape2D> once via casting into typed cache. Single error per spawn/enable, then quiet.
	// Null/unsupported/invalid => circle r16 (consistent default, cheapest physics).
	_ALWAYS_INLINE_ void cache_collision_shape_typed(const Ref<Shape2D> &shape);

	// Sync shape transform from instance transform (shared logic for teleport/set_transform)
	// Matches the movement-tick convention: the instance rotation
	// includes the texture rotation for rendering, but the physics shape must use the
	// logical (un-textured) rotation when shapes follow rotation. With
	// rotate_only_textures=true the shape keeps its previous orientation (origin-only sync).
	_ALWAYS_INLINE_ void sync_shape_transform_from_instance(int bullet_index, const Transform2D &instance_transf);

	// Carry the attachment along a transform edit: stick-relative slots
	// recompute from the new transform, non-stick slots shift by the jump
	// delta (they never heal otherwise). Shared by set_bullet_transform,
	// the texture-rotation setters, and the teleport paths so paused
	// factories never leave attachments behind.
	_ALWAYS_INLINE_ void carry_attachment_with_transform(int bullet_index, const Transform2D &new_transform, const Vector2 &origin_delta);

	//////////////////// CURVES RELATED
	inline void populate_shared_curves_related_data(const Ref<BulletCurvesData2D> &new_curves_data);

	_ALWAYS_INLINE_ void populate_individual_bullet_curves_related_data(int bullet_index, const Ref<BulletCurvesData2D> &new_curves_data);

	// Applies the x direction curve offset to the provided direction vector and normalizes it.
	// A degenerate result (Override mode writing 0/near-0 into both axes)
	// keeps the incoming direction: normalizing a ~zero vector would
	// silently stall the bullet at the inherited offset, and the tick's
	// adjust_direction path already guards the same way.
	_ALWAYS_INLINE_ void apply_x_direction_curve(Vector2 &direction_vector, const BulletCurvesData2D *curves_data) const;

	// Applies the y direction curve offset to the provided direction vector and normalizes it.
	// Same degenerate-result guard as the x variant above.
	_ALWAYS_INLINE_ void apply_y_direction_curve(Vector2 &direction_vector, const BulletCurvesData2D *curves_data) const;

	// A NaN baked into a Curve resource would otherwise flow straight into
	// speed/direction caches with no recovery, so fail each sample to 0.
	_ALWAYS_INLINE_ real_t get_bullet_curves_movement_speed(const BulletCurvesData2D *curves_data) const;

	_ALWAYS_INLINE_ real_t get_bullet_curves_rotation_speed(const BulletCurvesData2D *curves_data) const;

	_ALWAYS_INLINE_ real_t get_bullet_curves_x_direction_offset(const BulletCurvesData2D *curves_data) const;

	_ALWAYS_INLINE_ real_t get_bullet_curves_y_direction_offset(const BulletCurvesData2D *curves_data) const;

	_ALWAYS_INLINE_ void apply_direction_curve_texture_rotation_if_needed(Vector2 &curr_bullet_direction, Transform2D &curr_bullet_transf, double delta, const BulletCurvesData2D *curves_data) const;

	// Calculates the input x value for curves based on whether unit curve is used or not (basically whether to treat the input as percentages or raw elapsed time)
	_ALWAYS_INLINE_ real_t curve_get_input_value(bool use_unit_curve) const;

	// Borrows raw pointer - valid until next reassignment (no refcount inc). Keep scope transient.
	_ALWAYS_INLINE_ BulletCurvesData2D *find_bullet_curves_data_ptr(int bullet_index) const;

	Ref<BulletCurvesData2D> bullet_get_curves_data(int bullet_index) const;

	void bullet_set_curves_data(int bullet_index, const Ref<BulletCurvesData2D> &curves_data);

	void all_bullets_set_curves_data(const Ref<BulletCurvesData2D> &curves_data, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	TypedArray<BulletCurvesData2D> all_bullets_get_curves_data(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Clear one bullet's per-bullet curves back to the shared fallback (or
	// no curves when unset). Mirrors bullet_set_wobble_data(null): the slot
	// stops steering on its own curves and the tick resolves shared again.
	_ALWAYS_INLINE_ void clear_per_bullet_curves_data(int bullet_index) {
		bullet_set_curves_data(bullet_index, Ref<BulletCurvesData2D>());
	}

	// Clear a range of per-bullet curves (default all) back to shared/none.
	void all_bullets_clear_curves_data(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Speed up (or slow down) one bullet for this tick. Negative speed is
	// legal and flies backwards along the heading (velocity = direction *
	// speed). max_speed <= 0 means unlimited; otherwise speed clamps
	// symmetrically to [-max, max] so reverse flight survives instead of
	// snapping to 0 the way the old deceleration floor did.
	_ALWAYS_INLINE_ void bullet_accelerate_speed(int bullet_index, double delta);

	// Same, but the speed comes straight from this bullet's curve sample
	// (negative samples fly backwards, same as negative speed above).
	_ALWAYS_INLINE_ void bullet_accelerate_speed_using_curve(int bullet_index, double delta, const BulletCurvesData2D *curves_data);

	// Spin one bullet faster/slower for this tick, clamped to its max
	_ALWAYS_INLINE_ void bullet_accelerate_rotation_speed(int bullet_index, double delta);

	// Same as above, but the target speed is sampled from this bullet's curve
	_ALWAYS_INLINE_ void bullet_accelerate_rotation_speed_using_curve(int bullet_index, double delta, const BulletCurvesData2D *curves_data);

	// Custom rotation function (I am doing this for performance reasons since Godot's rotated_local returns a brand new Transform2D, but I want to modify a reference without making copies)
	_ALWAYS_INLINE_ void rotate_transform_locally(Transform2D &transform, real_t angle) const;

	BulletAttachment2D *bullet_get_attachment(int bullet_index);

	// Owner tracking: detaching an attachment always
	// clears both fields together, which is what makes its later PREDELETE a
	// no-op for the slot it left behind.
	static void clear_attachment_owner_fields(BulletAttachment2D *attachment);

	// Slot liveness: the "is this slot still mine" check runs liveness first:
	// BEFORE the pointer compare: comparing a dangling pointer first would
	// touch freed memory when the id was recycled (memdelete + allocator ABA).
	bool slot_still_holds_attachment(int bullet_index, BulletAttachment2D *expected_attachment, uint64_t expected_attachment_id, uint64_t expected_attachment_epoch) const;

	// Id-only form for deferred work: the queued request carries NO pointer
	// (a raw Object* in a deferred call's Variant args is converted by the
	// binder BEFORE this body runs, which reads freed memory when a handler
	// freed the attachment in between). The live slot pointer is compared
	// against the ObjectDB resolution of the id - never dereferenced first.
	bool slot_still_holds_attachment_id(int bullet_index, uint64_t expected_attachment_id, uint64_t expected_attachment_epoch) const;

	// NOTE: there is deliberately NO 3-argument (no-epoch) overload of this
	// check. It would silently accept a slot that was re-assigned to the same
	// node within the same generation - the classic ABA case the epoch exists
	// to catch. Callers that cannot supply an epoch must not use this at all.

	// Pooled-attachment check: pooling is keyed by a
	// 32-bit scene hash that can theoretically collide across two different
	// scenes, so every pop is verified. With a known expected scene (attach
	// path) the candidate must come from that exact scene; without one
	// (lifetime-expiry reclaim, which holds no scene Ref) the candidate must
	// at least genuinely belong to the slot's bucket. The residual risk (two
	// different path-scenes sharing one hash reaching reclaim) is accepted
	// and documented: the sweep just pushed these exact instances, so the
	// bucket top is ours unless a re-entrant handler stole it.
	static bool is_popped_attachment_from_scene(BulletAttachment2D *candidate, const Ref<PackedScene> &expected_scene, uint32_t expected_pooling_id);

	BulletAttachment2D *bullet_set_attachment_to_null(int bullet_index);

	// Called by BulletAttachment2D's PREDELETE when an ACTIVE attachment is freed
	// manually: drops the slot only if it still holds this exact pointer. Safe to
	// call on any state (bounds- and identity-checked).
	void _do_drop_attachment_slot_if_matches(int bullet_index, BulletAttachment2D *attachment);

	TypedArray<BulletAttachment2D> all_bullets_get_attachments(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	TypedArray<BulletAttachment2D> all_bullets_set_attachment_to_null(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_set_attachment(const Ref<PackedScene> &attachment_scene, const Vector2 &bullet_attachment_offset, bool stick_relative_to_bullet = true, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Guard-free core of bullet_set_attachment. Used by the public wrapper
	// below and by the spawn-time shared-attachment application, so both share
	// one implementation. Returns false when
	// nothing was attached (error already printed).
	// Rejected while the factory holds its internal busy flag (disable
	// sweeps, reset/free loops): attaching into a sweep would either be
	// wiped by the trailing blank (reset path) or pooled as a live slot
	// (disable path). Defer with call_deferred instead.
	_ALWAYS_INLINE_ bool attach_bullet_attachment_internal(int bullet_index, const Ref<PackedScene> &attachment_scene, const Vector2 &bullet_attachment_offset, bool stick_relative_to_bullet);

	void bullet_set_attachment(int bullet_index, const Ref<PackedScene> &attachment_scene, const Vector2 &bullet_attachment_offset, bool stick_relative_to_bullet = true);

	// Applies the spawn data's shared attachment to every bullet. Called from
	// spawn()/enable_volley() so both bullet types behave identically.
	// Stops after the first failure so a bad scene prints one error instead of
	// one per bullet.
	_ALWAYS_INLINE_ void apply_shared_bullet_attachment_from_data(const BulletVolleyData2D &data);

	void bullet_free_attachment(int bullet_index);

	void bullet_disable_attachment(int bullet_index);

	// Deferred attachment disable carrying the spawn generation, the slot's
	// attachment id, the exact pointer AND the per-slot assignment epoch.
	// Lifetime expiry queues disables that flush after the tick; if the slot
	// was detached or re-assigned in a handler, we must not pool the new
	// owner's attachment by index alone. The id re-resolves at flush so a
	// memdelete + allocator reuse at the same address (ABA) cannot falsely
	// match; the epoch catches the subtler same-life ABA where the pool
	// returns the SAME node to the SAME slot in the SAME generation.
	// Liveness is checked BEFORE the pointer compare: comparing a dangling
	// pointer first would touch freed memory when the id was recycled.



	void bullet_enable_attachment(int bullet_index);

	// Drops user connections to sprite_animation_finished: pooled instances carry them
	// across owners. Used by enable_volley AND the enable_bullet wake path (a wake
	// re-activates a pooled instance outside the pool pop, so it needs the same cleanup).
	void disconnect_sprite_animation_connections();

	// LIFE STATES (see bullet_volley2d.cpp): ACTIVE / PARKED (drained with
	// auto-pooling off: frozen, owned, wakeable) / POOLED (drained with
	// auto-pooling on: released, only a new life revives it).
	bool is_pooled() const;
	bool is_parked() const;
	// "active", "parked", "pooled", "fresh" or "dying" (tests/support).
	String debug_get_life_state() const;
	// Life id: bumped exactly once per life (spawn / enable_volley). Store it
	// next to a volley you keep across frames and compare before using the
	// volley again: a different id means the pool handed it to a new owner.
	int get_life_id() const { return volley_generation; }
	// True while release_life() has run and begin_life() has not (pooled or
	// pre-populated instances): begin_life skips the release then.
	bool life_released = true;
	// The last live bullet went out: park (auto-pooling off) or release +
	// pool (auto-pooling on).
	void on_volley_drained();
	// Pool time: drops every external reference (attachments, homing
	// targets, timers, records, volley-level listeners, owner, user
	// resources, user groups/metadata/modulate). Values are reseeded by
	// begin_life, never reset twice.
	void release_life();
	// Restores the Node/CanvasItem/Node2D settings spawn data never seeds
	// (modulate, transform, top_level, filtering, sorting, process mode...).
	void restore_node_baseline();
	// The single new-life path shared by spawn() and enable_volley().
	void begin_life(const BulletVolleyData2D &data, uint64_t new_owner_spawner_id, const Vector2 &new_inherited_velocity_offset);

	// Wake = resume (freeze contract). collision_amount -1 keeps the frozen
	// hit count; 0+ sets it. Refused on a pooled volley (stale handle).
	void enable_bullet(int bullet_index, int collision_amount = -1, bool should_enable_attachment = true);

	// Clearly-named alias: wake_bullet() revives ONE pooled or
	// manually-disabled slot with its current appearance/ballistics intact
	// (same-owner re-enable). Cross-owner reuse must go through
	// spawn_*()/enable_volley() which reseed appearance, custom data,
	// speeds and patterns. Calling wake on a foreign pooled volley warns (see
	// enable_bullet) instead of silently driving stale state.
	void wake_bullet(int bullet_index, int collision_amount = -1, bool should_enable_attachment = true) {
		enable_bullet(bullet_index, collision_amount, should_enable_attachment);
	}

	// Stability introspection for tests and support (bound below).
	// Keys: amount_bullets, active_bullets, generation, owner_spawner_id,
	// is_active, is_pooled, pool_amount, pool_shape, auto_pool_volley,
	// auto_pool_attachments, self_modulate.
	Dictionary debug_get_volley_info() const;
	// Every volley clock in one snapshot: curve clock, lifetime left, homing
	// interval timer, animation cursor, fade alpha, attached timers (time
	// left, period, repeating) and per-bullet bounce cooldowns.
	Dictionary debug_get_clocks() const;
	// Attached timer count (0 = no per-tick timer cost). For tests asserting
	// the 64-timer cap and detach-during-fire behavior.
	int debug_get_timer_count() const { return (int)custom_timers.size(); }
	// Collision shape state: {valid, type, circle_radius, rect_size,
	// capsule_radius, capsule_height, rid_count}. For tests asserting
	// set_collision_shape_runtime same-type vs type-change paths.
	Dictionary debug_get_shape_state() const;
	// Attachment slot introspection: {has_attachment, pooling_id,
	// owner_match}. owner_match verifies the attachment's owner ids point
	// back at this volley + index (stale ownership after reuse fails here).
	Dictionary debug_get_attachment_info(int bullet_index) const;
	// Runs the render interpolation pass on demand (same code the factory
	// _process drives): lets tests prove trail shards follow the lerped
	// bullet pose with zero physics ticks in between. No-op unless the
	// factory has use_physics_interpolation on.
	void debug_run_interpolation_pass();

	// Disables a single bullet: removes it from the live set, hides the visual,
	// disables its physics shape, and (unless told otherwise) returns its
	// attachment to the attachment pool. When the last bullet goes out, the
	// volley parks or pools via on_volley_drained().
	// A wake does NOT restore the attachment: re-attach explicitly (or via
	// the shared spawn-data attachment on the next enable). Kept simple on
	// purpose — silently re-popping a pooled slot here could hand a foreign
	// scene's node to a volley whose pooling id changed since.
	void disable_bullet(int bullet_index, bool release_attachment = true, bool reset_state = false);
	// Clears one bullet's runtime ledgers (hit count, bounce ledger, homing
	// queue, orbit, fall speed, wobble/pattern progress); kinematics,
	// configuration, custom data and the attachment stay. Works on live and
	// frozen bullets alike.
	void bullet_reset_state(int bullet_index);
	void all_bullets_reset_state(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);
	void reset_bullet_runtime_state(int bullet_index);
	// Keeps the attachment in its slot but tells it the bullet is gone
	// (on_bullet_disable); enable_bullet resumes it (on_bullet_enable).
	void suspend_bullet_attachment(int bullet_index);
	// Batched disable (expiry, clear_all_bullets): disable_bullet(i, true)
	// semantics per bullet with one render upload for a drained volley.
	// Returns how many bullets it disabled.
	int disable_bullets_bulk(const std::vector<int> &indexes, int fx_trigger = -1, const std::vector<uint64_t> *expected_epochs = nullptr);
	// Manual clear with visuals: captures the death pose, runs the silent
	// disable_bullet() above, then fires EFFECT_ON_CLEAR one-shots. Use
	// this instead of disable_bullet() when the disappearance should read
	// on screen (dismissals, wave clears, boss deaths). Collision kills
	// fire On Destroy and timeouts fire On Lifetime Over through their own
	// paths, so those never double with a clear layer; teardown
	// (factory reset/free) stays silent and never calls here. Already-dead
	// slots are a no-op false (no double fire). Returns true when a live
	// bullet was cleared.
	bool clear_bullet(int bullet_index);
	// Clears every live bullet through clear_bullet() above (snapshot the
	// live set first: each disable mutates it). Mass clears recycle the
	// oldest one-shot slots past the layer ring, so 500 bullets never
	// spawn 500 live effects. Returns how many bullets were cleared.
	int clear_all_bullets();

	// Resolves who owns the collision/lifetime signals for this multimesh: the
	// tagged BulletSpawner2D while it is alive, else the BulletFactory2D.
	// A spawner-owned volley NEVER falls back to the factory: if the owning
	// spawner is gone the event is dropped (return null) instead of firing
	// factory signals for another node's bullets. May also return null
	// during teardown (both gone) - the caller must skip emission then
	// (this also fixes a latent null-factory crash in the old code path).
	_ALWAYS_INLINE_ Object *resolve_signal_emitter() const;

	// One queued overlap: validate (alive, epoch, target alive), bounce,
	// count, emit LIVE, then decide the kill with the post-handler state.
	void handle_bullet_collision(const BulletCollisionData2D &record);
	// Drains every record queued since the last tick (start of the volley
	// tick, before the move: records describe the pose the server tested).
	void drain_collisions();
	// Moves one undrained record into the paused-overlap list (a handler
	// paused the factory mid-drain).
	void park_collision_record(const BulletCollisionData2D &record);

	/// COLLISION DETECTION METHODS

	// Queue-time target velocity for the bounce math (see the record
	// above): same read as the drain path (RigidBody2D linear_velocity,
	// CharacterBody2D velocity, AnimatableBody2D constant_linear_velocity),
	// false when the target exposes none or is already gone (the drain
	// then falls back to its live read).
	static bool read_queued_target_velocity(int64_t entered_instance_id, Vector2 &out_velocity);

	// Queue-time target pose for the velocity-less estimate. Unconditional
	// (cheap: one cast + one position read); the drain uses it only when
	// no velocity property exists.
	static void read_queued_target_pose(int64_t entered_instance_id, Vector2 &out_position, bool &out_valid);

	// ---- Overlaps that START while the factory is paused ----------------
	// The physics server keeps reporting while the factory is paused, but
	// the drain does not run. Dropping those ADDED events meant a bullet that
	// came to rest inside an enemy during a pause never registered the hit
	// (no new ADDED ever comes for a steady overlap). Instead they are parked
	// here (bounded, deduped), cancelled by a REMOVED event if the overlap
	// ends during the pause, and replayed once on resume: exactly-once, no
	// physics rescan, and overlaps already counted before the pause are never
	// re-reported.
	struct PausedOverlap2D {
		int bullet_index = -1;
		int64_t target_id = 0;
		CollisionType type = CollisionType::AREA;
		uint64_t epoch = 0;
	};
	std::vector<PausedOverlap2D> paused_overlaps;
	static constexpr size_t kMaxPausedOverlaps = 4096;

	_ALWAYS_INLINE_ void park_paused_overlap(PhysicsServer2D::AreaBodyStatus status, int64_t target_id, int bullet_index, CollisionType type);

	// Called by the factory when processing resumes: queue every parked
	// overlap whose bullet is still the same live bullet, through the normal
	// dedup + record path. Returns the number of records queued.
	int replay_paused_overlaps();

	void area_entered_func(PhysicsServer2D::AreaBodyStatus status, RID entered_rid, int64_t entered_instance_id, int entered_shape_index, int bullet_shape_index);
	void body_entered_func(PhysicsServer2D::AreaBodyStatus status, RID entered_rid, int64_t entered_instance_id, int entered_shape_index, int bullet_shape_index);

	// Moves a single bullet attachment. Indices come from the tick loop, but
	// every vector here is indexed bare - a desync would be an OOB write per
	// tick, so validate sizes instead of trusting the reset invariant.
	// A null factory can only happen on a not-yet-spawned instance driven
	// through debug helpers; without the guard the interpolation check below
	// would null-deref.
	_ALWAYS_INLINE_ void move_bullet_attachment(const Vector2 &translate_by, int bullet_index);

	// Calculates the global transform of the bullet attachment. Note that this function relies on bullet_attachment_local_transform being set already
	_ALWAYS_INLINE_ Transform2D calculate_attachment_global_transf(int bullet_index, const Transform2D &original_data_transf);

	bool get_is_life_time_infinite() const { return is_life_time_infinite; }
	void set_is_life_time_infinite(bool value);

	int get_bullet_max_collision_count() const { return bullet_max_collision_count; }
	void set_bullet_max_collision_count(int value);

	// Single-bullet collision counter read. See set_bullet_collision_count for writing.
	int get_bullet_collision_count(int bullet_index) const;

	// Single-bullet collision counter write. Clamped like enable_bullet()'s
	// wake top-up: negatives become 0, values at/above max become max - 1
	// (exactly one hit remaining). Storing the threshold itself would kill on
	// the next hit, unlike an equivalent wake.
	void set_bullet_collision_count(int bullet_index, int value);

	TypedArray<int> get_bullets_current_collision_count() const;

	bool set_bullets_current_collision_count(const TypedArray<int> &arr, bool tile_short_arrays = false);

	// Void wrapper for the editor property (property setters must return void).
	// Strict indexing (uncovered bullets start at 0); the data resource
	// threads its own tile box through the spawn path instead.
	void set_bullets_current_collision_count_no_return(const TypedArray<int> &arr) {
		(void)set_bullets_current_collision_count(arr);
	}




	///
protected:
	// Internal setup helpers (spawn/enable seed shared spawn data through
	// set_rotation_data). Not bound to scripts.
	// Reserves enough memory and populates all needed data structures keeping track of rotation data
	void set_rotation_data(const TypedArray<BulletRotationData2D> &rotation_data, bool new_rotate_only_textures, bool tile_short_arrays = false);

	// Guarantees no attachment slot survives into a new owner: force-disables any
	// live slot and blanks all five attachment arrays plus the interpolation cache.
	// Used by spawn() and enable_volley() so pooled reuse can't inherit stale
	// pointers when a deferred disable was dropped by a generation bump.
	void reset_attachment_state_for_reuse();

	// Generates texture transform with correct rotation and sets it to the correct bullet on the multimesh
	Transform2D generate_texture_transform(Transform2D transf, bool is_texture_rotation_permanent, real_t texture_rotation_radians, int bullet_index);

	// Generates a collision shape transform for a particular bullet and attaches it to the area
	Transform2D generate_collision_shape_transform_for_area(Transform2D transf, const Vector2 &collision_shape_offset, int bullet_index);

	// Shape data last pushed to volley_shape. Pool reuse with an identical
	// shape skips the push (and the area-wide shape update it triggers).
	// Invalidated whenever the RID is (re)created.
	bool shape_data_applied = false;
	PhysicsServer2D::ShapeType applied_shape_type = PhysicsServer2D::SHAPE_CIRCLE;
	Vector2 applied_rect_size;
	float applied_circle_radius = 0.0f;
	float applied_capsule_radius = 0.0f;
	float applied_capsule_height = 0.0f;
	bool shape_data_matches_applied() const {
		return shape_data_applied && applied_shape_type == cached_effective_shape_type && applied_rect_size == cached_rect_size && applied_circle_radius == cached_circle_radius && applied_capsule_radius == cached_capsule_radius && applied_capsule_height == cached_capsule_height;
	}
	void mark_shape_data_applied();

	// Sets up the area correctly with collision related data
	void set_up_area(const int collision_layer, const int collision_mask, bool new_monitorable, const RID &physics_space);

	void generate_physics_shapes_for_area(int amount);
	// Pushes the cached shape data into volley_shape once (no-op when it
	// already matches) and marks it applied.
	void apply_volley_shape_data();
	// Detaches every shape from the area and frees volley_shape.
	void release_volley_shape();

	void set_all_physics_shapes_enabled_for_area(bool enable);

	void generate_multimesh();

	void set_up_multimesh(int new_instance_count, const Ref<Mesh> &new_mesh, Vector2 new_texture_size);

	void set_up_bullet_instances(const BulletVolleyData2D &data);

	void set_up_life_time_timer(double new_max_life_time, double new_current_life_time);

	// Bakes frames + per-frame seconds from SpriteFrames (fps-relative durations) and
	// applies frame 0. Returns false on null/empty/missing (previous animation kept).
	bool rebuild_sprite_animation(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_animation);

	// Silent auto-resolution shared by rebuild + quad sizing: empty or missing "default"
	// resolves to first animation without error; explicit wrong names error once here.
	// Returns true with out_anim set, false when unusable (caller must not alter state).
	static bool resolve_sprite_animation(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_requested, StringName &out_anim);
	// Same resolution without any error output, for sizing-only paths where the
	// subsequent rebuild owns reporting.
	static bool resolve_sprite_animation_quiet(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_requested, StringName &out_anim);

	// Resolves QuadMesh size: manual override wins, else first-frame size
	// (AtlasTexture region, else texture size), else 32x32 fallback.
	static Vector2 resolve_quad_size(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_animation, Vector2 override_size);

	// Always called last
	void finalize_set_up(
			const Ref<Resource> &new_shared_bullets_custom_data,
			const Ref<Material> &new_material,
			int new_z_index,
			int new_light_mask,
			int new_visibility_layer,
			const Color &new_self_modulate,
			const Dictionary &new_instance_shader_parameters);

	///

public:
	/// COLLISION-SHAPE DEBUGGER ACCESSORS (read by BulletVolleyDebugger2D once per volley per tick)

	PhysicsServer2D::ShapeType get_collision_shape_type_for_debugging() const {
		return cached_effective_shape_type;
	}

	const Vector2 get_collision_shape_size_for_debugging() const;

	const std::vector<Transform2D> &get_all_collision_shape_transforms_for_debugging() const {
		return all_cached_shape_transforms;
	}

	bool get_skip_debugging() const;

	bool is_active_for_debugging() const {
		return is_active;
	}

public:
	// Timer logic
	struct CustomTimer {
		godot::Callable _callback;
		double _current_time;
		double _initial_time;
		bool _repeating;
		bool _execute_only_if_volley_is_active;
		// Unique per attach (monotonic, never reused): the live sweep calls
		// due timers by id, so a timer detached by an earlier callback of the
		// same tick is simply not found and never fires.
		uint64_t _id = 0;

		CustomTimer(const godot::Callable &callback, double initial_time, bool repeating, bool execute_only_if_volley_is_active, uint64_t id) :
				_callback(callback), _current_time(initial_time), _initial_time(initial_time), _repeating(repeating), _execute_only_if_volley_is_active(execute_only_if_volley_is_active), _id(id) {};
	};

	// Attach/detach apply IMMEDIATELY from anywhere (handlers, timer
	// callbacks, idle code): the only iteration of custom_timers is the
	// first pass of run_custom_timers, which runs no user code.
	void attach_time_based_function(double time, const Callable &callable, bool repeat = false, bool execute_only_if_volley_is_active = true);
	void detach_time_based_function(const Callable &callable);
	void detach_all_time_based_functions();

	// Called by the factory once per physics tick for volleys holding
	// timers. Pass 1 (no user code) advances every timer, re-arms repeating
	// ones and records the due ids; pass 2 calls each due timer that is
	// still attached, LIVE (inside the physics frame). One-shots are removed
	// before their call. Re-entrant calls are ignored.
	void run_custom_timers(double delta);

	// Stores a bunch of timers for the multimesh that should execute
	std::vector<CustomTimer> custom_timers;
	uint64_t custom_timer_id_counter = 0;
	std::vector<uint64_t> due_timer_scratch;
	int _timers_running_depth = 0;

public:
	// Clears both homing deques through the pop loop so the global
	// mouse-target counter stays exact: force_delete() memdeletes without
	// running disable logic, and a leaked counter would query the mouse every
	// homing tick forever. Pop paths never touch the tree, so this is safe.
	~BulletVolley2D();

	enum OrbitingDirection {
		DontMove = 0,
		OrbitLeft,
		OrbitRight,
		OrbitRandom
	};

	enum OrbitingTextureRotation {
		FaceTarget = 0,
		FaceOppositeTarget,
		FaceOrbitingDirection,
		FaceOppositeOrbitingDirection
	};

	enum OrbitingFollowMode {
		FollowTarget = 0,
		FollowDeadzone,
		Anchored
	};

	enum OrbitingLockPolicy {
		RelockAlways = 0,
		StayLocked,
		RelockOnTargetChange
	};

	struct OrbitingData {
		real_t angle = 0.0f;
		real_t radius = 0.0f;
		OrbitingDirection direction = OrbitRight;
		OrbitingTextureRotation texture_rotation = FaceTarget;
		bool is_locked_orbiting = false;
		OrbitingFollowMode follow_mode = FollowTarget;
		real_t follow_deadzone = 0.0f;
		OrbitingLockPolicy lock_policy = RelockAlways;
		bool rigid_follow = true;
		Vector2 locked_center{ 0, 0 };
		HomingType locked_target_type = NotHoming;
		uint64_t locked_target_identity = 0;

		OrbitingData() = default;

		OrbitingData(real_t new_radius, OrbitingDirection new_direction, OrbitingTextureRotation new_texture_rotation) :
				angle(0.0),
				radius(new_radius),
				direction(new_direction),
				texture_rotation(new_texture_rotation),
				is_locked_orbiting(false) {};

		OrbitingData(real_t new_radius, OrbitingDirection new_direction, OrbitingTextureRotation new_texture_rotation, OrbitingFollowMode new_follow_mode, real_t new_follow_deadzone, OrbitingLockPolicy new_lock_policy, bool new_rigid_follow = true) :
				angle(0.0),
				radius(new_radius),
				direction(new_direction),
				texture_rotation(new_texture_rotation),
				is_locked_orbiting(false),
				follow_mode(new_follow_mode),
				follow_deadzone(new_follow_deadzone),
				lock_policy(new_lock_policy),
				rigid_follow(new_rigid_follow) {};
	};

protected:
	// Configuration flags
	bool adjust_direction_based_on_rotation = false;
	bool homing_take_control_of_texture_rotation = false;
	bool bullet_homing_auto_pop_after_target_reached = false;
	bool shared_homing_deque_auto_pop_after_target_reached = false;

	Vector2 cached_mouse_global_position{ 0, 0 };

	// ORBITING

	// For each bullet containing its orbiting data
	std::vector<OrbitingData> all_orbiting_data;

	// For each bullet whether the orbiting is enabled or not
	std::vector<uint8_t> all_orbiting_status;

	int active_orbiting_count = 0;
	// Reused per-tick scratch: the orbit-lock snapshot used to keep read to avoid per-tick std::vector
	// assignment churn in move_bullets.
	std::vector<uint8_t> orbit_locked_scratch;
	//

	// HOMING

	double homing_update_interval = 0.0;
	double homing_update_timer = 0.0;
	real_t homing_smoothing = 0.0;

	// Minimum distance (in pixels) from the homing target at which the bullet is considered to have reached it. Once within this distance, the bullet_homing_target_reached signal is emitted
	real_t homing_distance_before_reached = 5.0;

	// This tracks each bullet's homing deque - allows each bullet to have its own separate homing targets (per-bullet homing)
	std::vector<HomingTargetDeque> all_bullet_homing_targets;

	// For each bullet's homing deque, store the amount targets
	std::vector<int> all_homing_count;

	// Per-bullet turn agility. Empty/unused unless any per-bullet smoothing was
	// set (use_per_bullet_homing_smoothing), in which case update_homing reads
	// the per-bullet value and the shared homing_smoothing is ignored.
	std::vector<real_t> all_bullet_homing_smoothing;
	// Per-bullet seed PRESENCE, not values. all_cached_speed / all_rotation_speed
	// are a single effective store the tick integrates and curves overwrite in
	// place; there is no separate "base" copy to re-derive from. These bits are
	// what let the shared fallbacks tell the two zero-cases apart:
	//   bit 1 -> a VALID per-bullet entry seeded this slot (including a
	//            deliberate all-zero "don't move" / "no spin"). Shared must not
	//            touch it, or an intentional freeze silently starts moving.
	//   bit 0 -> the slot was seeded from an invalid entry (null, wrong type, or
	//            non-finite), so it is a genuine gap and shared may fill it.
	// Without the bit the fallback compared the triple against 0, which cannot
	// distinguish those two cases.
	// INVARIANT: bit 0 must also mean "not yet filled by shared", so a slot the
	// fallback fills gets its bit set to 1 (fill-once). That keeps a later
	// set_shared_* from re-filling, which is the documented fill-once order
	// rule, while a deliberate zero seeded by the user still wins.
	std::vector<uint8_t> has_per_bullet_speed_data;
	std::vector<uint8_t> has_per_bullet_rotation_data;
	bool use_per_bullet_homing_smoothing = false;

	// Tracks how many bullets are currently homing in TOTAL (per-bullet homing, NOT shared) - basically determines whether the per-bullet homing feature is even turned on
	int active_homing_count = 0;

	// Once-flag so the silent-homing footgun warns exactly once per multimesh lifetime
	// segment (reset on spawn/enable). See move_bullets homing branch.
	bool homing_inert_warning_issued = false;


	// This is a shared homing deque - allows the bullets to share the same target
	HomingTargetDeque shared_homing_deque;

	//

	// SHARED MOVEMENT PATTERN (from spawn data; per-bullet entries in
	// all_movement_pattern_data stay exclusively runtime-owned). The slot holds
	// only curve+flags - distance traveled is per bullet (see below), so one
	// shared pattern costs one curve instead of N copies.
	Ref<Curve2D> shared_movement_pattern_curve;
	bool shared_movement_pattern_face_movement_direction = false;
	bool shared_movement_pattern_repeat = true;
	// Per-bullet distance ledger for the shared pattern. Sized to
	// amount_bullets at spawn/enable; a finished non-repeating bullet is one
	// whose distance reached the curve length (no extra flag needed).
	std::vector<real_t> shared_movement_pattern_distances;

	// SHARED SPEED / ROTATION / WOBBLE (from spawn data or the runtime API
	// below; mirrors the data so getters stay truthful across pool reuse).
	// Unified precedence: shared is the fallback default, per-bullet wins
	// per slot; null disables the fallback (current ballistics persist).
	Ref<BulletSpeedData2D> shared_bullet_speed_data;
	Ref<BulletRotationData2D> shared_bullet_rotation_data;
	Ref<BulletWobbleData2D> shared_bullet_wobble_data;
	// Live per-bullet wobble resources (mirrors the curves runtime API:
	// bullet_get_wobble_data returns the effective per-bullet resource, or
	// null when the slot runs on the shared fallback / inactive).
	std::vector<Ref<BulletWobbleData2D>> all_bullet_wobble_data;

	// WOBBLE (sine/cos flight modulation, seeded from spawn data; editable
	// live below). Unified precedence: per-bullet seeds win per slot, shared
	// is the fallback for slots without a valid per-bullet seed. Null
	// entries and disabled entries fall back to shared per bullet. Phase
	// seeds fan out per bullet
	// (phase + step * i) so one resource makes snakes/petals, not sync waves.
	struct WobbleSeed {
		bool active = false;
		int mode = 0;
		int waveform = 0; // mirrors BulletWobbleData2D::WobbleWaveform (0 = Sine, 1 = Cosine)
		real_t amplitude = 0.0;
		real_t frequency_hz = 0.0;
		real_t phase = 0.0;
		bool distance_phased = false;
		real_t damping_per_sec = 0.0;
		real_t delay_sec = 0.0;
		real_t duration_sec = 0.0;
		bool face_movement_direction = true;
		real_t face_rotation_speed = 18.0;
		// Last applied offset + the volley clock it was sampled at. The tick
		// applies (now - last) so the per-frame deltas telescope exactly; a
		// reconstructed "previous" sample drifted for distance-phased waves
		// (the ledger also grows by wind/gravity/acceleration). Any reseed
		// resets has_last, and a clock jump (wake rewind, manual
		// set_curves_elapsed_time) falls back to the analytic previous.
		bool has_last = false;
		real_t last_offset = 0.0;
		double last_time = 0.0;
	};

	// Single evaluation point for the wobble waveform so the now/prev tick
	// samples can never drift apart (pause/teleport-safe delta relies on
	// both halves using the same shape). Out-of-range seeds fail open to
	// sine instead of stalling the bullet.
	_ALWAYS_INLINE_ real_t evaluate_wobble_waveform(int waveform, real_t angle) const {
		return (waveform == 1) ? Math::cos(angle) : Math::sin(angle);
	}
	std::vector<WobbleSeed> all_bullet_wobble;
	bool is_wobble_feature_enabled = false;
	// Per-bullet distance ledger for distance-phased wobble (same idea as
	// the shared pattern ledger; time-phased wobble uses curves_elapsed_time).
	std::vector<real_t> wobble_distance_traveled;

	// GRAVITY / DRAG (seeded from spawn data; editable live below).
	// Gravity is a constant acceleration in px/s^2; drag is a linear
	// coefficient (speed -= speed * drag * delta). Both default to off.
	Vector2 gravity = Vector2(0, 0);
	real_t linear_drag = 0.0;
	// Per-bullet gravity vectors (resolved at spawn/enable from shared gravity
	// + all_bullet_gravity spawn data under the unified tiling rule).
	// The tick integrates these (see all_gravity_velocity); all zero = off.
	std::vector<Vector2> all_gravity;
	// Per-bullet gravity PRESENCE, same contract as the speed/rotation bits:
	// 1 = user-authored per-bullet entry (seed or bullet_set_gravity, even a
	// deliberate zero), so set_gravity() fills only genuine gaps instead of
	// silently destroying per-bullet tuning. Reset with the vectors.
	std::vector<uint8_t> has_per_bullet_gravity;
	// Gravity time window over volley life (seconds since spawn, read on
	// curves_elapsed_time): integrates only inside [delay, delay + duration].
	// delay 0 = immediate; duration 0 = infinite. Zeroed with the vectors.
	double gravity_delay_sec = 0.0;
	double gravity_duration_sec = 0.0;
	// Per-bullet integrated gravity velocity (semi-implicit Euler: v += g*dt
	// each tick, p += v*dt). Zeroed on every new life (spawn/enable/reset)
	// and whenever set_gravity starts a new regime, so pooled reuse never
	// inherits fall speed. Sized with the movement SoA below.
	std::vector<Vector2> all_gravity_velocity;
	// Volley-level gravity switch, refreshed by refresh_gravity_active()
	// after every seed/write/reset. Lets the tick skip the whole gravity
	// block (window math included) when no bullet pulls anywhere.
	bool gravity_active = false;
	void refresh_gravity_active();

	// HOMING GATING (seeded from spawn data; editable live below).
	// delay = straight-flight seconds before steering starts; duration =
	// seconds of steering before it stops (0 = infinite); lose_range =
	// steering pauses beyond this distance from the target (0 = unlimited).
	real_t homing_delay_sec = 0.0;
	real_t homing_duration_sec = 0.0;
	real_t homing_lose_range_px = 0.0;

	// BOUNCE / RICOCHET (seeded from BulletVolleyData2D; editable live
	// below). bounce_mask == 0 disables the whole feature (zero tick cost:
	// only the collision drain checks it). On a bounce-eligible hit the
	// bullet reflects across the surface normal and keeps flying; the hit
	// only consumes collision budget when bounce_hit_consumed is true.
	int bounce_mask = 0;
	real_t bounce_strength = 1.0;
	bool bounce_tilemap_layers = false;
	bool bounce_push_assist = true;
	bool bounce_charge_amplify = true;
	bool bounce_hit_consumed = false;
	int bounce_max_count = 0;
	int bounce_mode = 0; // BulletVolleyData2D::BounceMode
	bool bounce_rotate_texture = true;
	real_t bounce_rotation_smooth = 0.0;
	real_t bounce_randomness_deg = 0.0;
	real_t bounce_cooldown_sec = 0.05;
	// Same-target debounce window in seconds. Cooldown only buys escape
	// time, so a bullet still touching the same object when it expires
	// (deep overlap, sliding contact, steered straight back in) would
	// bounce again and again. Debounce keys on the target instance: a
	// re-hit against the just-bounced object inside this window never
	// re-bounces. 0 (allowed) disables it. Default 0.15.
	real_t bounce_debounce_sec = 0.15;
	// Per-bullet bounce ledger. Sized to amount_bullets only while bouncing
	// is armed (see ensure_bounce_vectors); empty otherwise so a plain
	// volley pays no per-tick bounce work. Zeroed on every new life and on
	// single-bullet disable (same rule as homing/orbit: a wake starts fresh).
	std::vector<int> all_bounce_count;
	std::vector<real_t> all_bounce_cooldown;
	std::vector<uint64_t> all_bounce_last_tick;
	// Same-target debounce ledger: which object each bullet last bounced
	// off, and when (volley clock). Sized with the rest of the ledger;
	// target id 0 means none yet (instance ids are never 0).
	std::vector<int64_t> all_bounce_last_target;
	std::vector<double> all_bounce_last_time;
	// Last-bounce forensics for debug_get_bounce_info (and the tests that
	// pin it): the contact normal and target velocity the last bounce
	// committed with. Zeroed everywhere the rest of the ledger is.
	std::vector<Vector2> all_bounce_last_normal;
	std::vector<Vector2> all_bounce_last_target_velocity;
	// Smooth visual pursuit: ballistics always reflect instantly, but with
	// bounce_rotation_smooth > 0 the sprite slews toward the reflected
	// heading over several ticks (same contract as homing smoothing).
	std::vector<uint8_t> bounce_visual_pending;
	std::vector<Vector2> bounce_visual_target;
	// Monotonic tick id for the one-bounce-per-bullet-per-tick guard (a
	// target carrying both a body and an area would otherwise double-flip).
	uint64_t bounce_tick_counter = 0;
	// Per-bullet bounce speed multiplier. Curves, acceleration and drag
	// rewrite all_cached_speed every tick, which would erase a bounce boost
	// on the very next frame. The multiplier re-applies below so an uncapped
	// strength persists (1.0 = untouched). Reset on every new life/wake.
	std::vector<real_t> all_bounce_speed_multiplier;
	// Volley latch: true once any bullet carries a multiplier != 1. Sticky
	// on purpose (per-bullet exact check does the real work); cleared on
	// new life, teardown and disarm.
	bool bounce_speed_scaled = false;
	// Last physics delta, used to size the first smooth step at bounce time.
	double bounce_last_delta = 0.016;
	// Once-flag so the mask-mismatch footgun warns once per life instead of
	// spamming (bounce bits outside collision_mask can never be detected).
	bool bounce_mask_warning_issued = false;
	// Volley-level bounce switch. True exactly when bounce_mask != 0; the
	// tick gates cooldown/visual work behind it.
	bool bounce_enabled() const { return bounce_mask != 0; }
	// Sizes (or clears) the bounce ledger to match amount_bullets. Sized only
	// while bouncing is armed; cleared when disarmed so disabled features
	// cost nothing per tick. Every reader bounds-checks, so the empty state
	// is always safe.
	void ensure_bounce_vectors();

public:
	// Advances one bullet along a movement-pattern curve for this tick.
	// distance_traveled is updated in place. Returns false when the pattern is
	// finished (degenerate curve or completed non-repeating run); the caller
	// then clears per-bullet entries or parks the shared ledger at the end.
	// Single implementation shared by the per-bullet and shared patterns.
	// known_len lets the shared-path caller pass its hoisted baked length
	// instead of re-querying it per bullet per tick (< 0 = query inside).
	// The inherited offset is carried in world space AFTER the pattern
	// steering: folding it into advance_dist would only stretch the pattern
	// step along the pattern direction instead of adding true wind.
	_ALWAYS_INLINE_ bool advance_movement_pattern(const Ref<Curve2D> &curve, bool face_movement_direction, bool repeat_pattern, real_t &distance_traveled, Vector2 &velocity_delta, Vector2 &curr_bullet_direction, Transform2D &curr_bullet_transf, real_t known_len = -1.0);

	// One volley tick, driven by BulletFactory2D::tick_volleys while
	// is_being_ticked is set. Fixed phases, every signal LIVE (synchronous):
	//   1. drain_collisions  - hits/bounces at the impact pose (pre-move)
	//   2. move_bullets      - pure integration (collects homing events)
	//   3. homing events     - bullet_homing_target_reached + auto-pop
	//   4. sprite animation  - sprite_animation_finished
	//   5. reduce_lifetime   - life_time_over, then expiry
	// Each phase that ran user code re-checks liveness, deletion and pause.
	void tick(double delta);
	// True while this volley may keep running its tick after user code ran
	// (not freed, not queued for deletion, factory not paused).
	bool tick_may_continue(uint64_t self_id) const;

	// Updates all bullets' positions, rotations, and homing.
	// Never inlined into the caller: the factory calls this once per volley,
	// and inlining the whole per-bullet loop into BulletFactory2D::tick_volleys
	// measured 20-25% slower on trails_fx_2k (register pressure in the loop).
	void move_bullets(double delta);

	// Fingerprint of the current front target, so the ring knows when the target actually changed.
	// bullet locked onto last. Node2D = instance id, Vector2 = bit hash of
	// the snapshot namespaced away from the mouse sentinel, mouse = 1 (it
	// has no stable address).
	_ALWAYS_INLINE_ uint64_t orbit_target_identity(const HomingTargetDeque &deque) const;

	// Locked bullet's live deque (per-bullet wins, same precedence as the
	// tick): the center getter needs the unlocked fallback without
	// duplicating it. A bullet with its own non-empty deque reports that
	// deque; otherwise it reports the shared broadcast deque.
	_ALWAYS_INLINE_ bool orbit_live_deque_for_bullet(int bullet_index, const HomingTargetDeque *&r_deque) const;

	_ALWAYS_INLINE_ HomingType orbit_target_type(const HomingTargetDeque &deque) const;

	// Locked-ring center for this tick. FollowTarget tracks the target.
	// FollowDeadzone pins locked_center until the target walks farther than
	// follow_deadzone from it, then re-pins. Anchored ignores the target
	// entirely: the ring freezes where it locked.
	_ALWAYS_INLINE_ Vector2 orbit_effective_center(OrbitingData &orbiting_data, const Vector2 &live_target_pos);

	// Stamp the lock bookkeeping shared by every lock site.
	_ALWAYS_INLINE_ void orbit_stamp_lock(OrbitingData &orbiting_data, const Vector2 &center, const HomingTargetDeque &deque);

	// Policy gate for every event that would drop the lock.
	// Explicit user action (disable, clear, freed target) always unlocks.
	// Otherwise: RelockAlways unlocks, StayLocked keeps everything
	// (angle + center + identity) so the ring re-pins silently when the
	// target returns, and RelockOnTargetChange unlocks only when the front
	// target is a different identity than the one the bullet locked onto.
	_ALWAYS_INLINE_ bool orbit_should_unlock_for_front_change(OrbitingData &orbiting_data, const HomingTargetDeque &deque);

	///////////////// ORBITING DATA METHODS

	// Disabled bullets own no homing/orbit state (disable_bullet clears it
	// and the tick never moves them): pushing targets or arming orbit there
	// inflates the homing/orbiting counters for slots that never drain.
	// Retarget passes skip disabled slots; direct script calls get a loud
	// error here instead of a silent counter leak.
	_ALWAYS_INLINE_ bool orbit_reject_disabled_bullet(int bullet_index, const char *function_name) const;

	void bullet_enable_orbiting(int bullet_index, real_t orbiting_radius, OrbitingDirection orbiting_direction, OrbitingTextureRotation orbiting_texture_rotation, OrbitingFollowMode orbiting_follow_mode = FollowTarget, real_t orbiting_follow_deadzone = 0.0f, OrbitingLockPolicy orbiting_lock_policy = RelockAlways, bool orbiting_rigid_follow = true);

	void bullet_disable_orbiting(int bullet_index);

	void bullet_set_orbiting_radius(int bullet_index, real_t new_radius);

	real_t bullet_get_orbiting_radius(int bullet_index);

	bool bullet_is_orbiting_enabled(int bullet_index);

	void bullet_set_orbiting_texture_rotation(int bullet_index, OrbitingTextureRotation new_texture_rotation);

	OrbitingTextureRotation bullet_get_orbiting_texture_rotation(int bullet_index);

	void bullet_set_orbiting_direction(int bullet_index, OrbitingDirection new_direction);

	OrbitingDirection bullet_get_orbiting_direction(int bullet_index);

	/////////////////

	///////////////// ORBITING DATA HELPERS

	// Re-lock every locked bullet onto a fresh deque without the fly-to-rim
	// flicker: same front identity = keep angle + center, new identity =
	// stamp the new target (angle preserved, center re-pinned) under
	// StayLocked/RelockOnTargetChange, full relock under RelockAlways.
	// Explicit clears still go through bullet_clear_homing_targets (unlock).
	_ALWAYS_INLINE_ void orbit_keep_lock_across_replace(HomingTargetDeque &deque);

	// Policy-aware unlock for a deque that ran dry: StayLocked rides out the
	// gap (keeps angle/center/identity so the ring re-pins silently when a
	// target returns), every other policy unlocks.
	_ALWAYS_INLINE_ void orbit_unlock_on_empty_deque();

	// Same, scoped to one per-bullet deque's owner.
	_ALWAYS_INLINE_ void orbit_unlock_on_empty_deque_for_bullet(int bullet_index);

	void all_bullets_enable_orbiting(real_t orbiting_radius, OrbitingDirection orbiting_direction, OrbitingTextureRotation orbiting_texture_rotation, int bullet_index_start = 0, int bullet_index_end_inclusive = -1, OrbitingFollowMode orbiting_follow_mode = FollowTarget, real_t orbiting_follow_deadzone = 0.0f, OrbitingLockPolicy orbiting_lock_policy = RelockAlways, bool orbiting_rigid_follow = true);

	// Concentric-ring enable: bullet (start + k) orbits at radius_start + radius_step * k.
	// Re-arming an already-orbiting bullet updates every passed param (not
	// just the radius): ranges like all_bullets_enable_orbiting_linear must
	// be re-runnable on armed volleys. OrbitRandom keeps each bullet's rolled
	// direction (re-setting it would re-roll mid-flight); the rest applies.
	// Invalid enums are still rejected per bullet by the individual setters.
	void all_bullets_enable_orbiting_linear(real_t radius_start, real_t radius_step, OrbitingDirection orbiting_direction = OrbitRight, OrbitingTextureRotation orbiting_texture_rotation = FaceTarget, int bullet_index_start = 0, int bullet_index_end_inclusive = -1, OrbitingFollowMode orbiting_follow_mode = FollowTarget, real_t orbiting_follow_deadzone = 0.0f, OrbitingLockPolicy orbiting_lock_policy = RelockAlways, bool orbiting_rigid_follow = true);

	bool bullet_is_orbiting_locked(int bullet_index);

	Vector2 bullet_get_orbiting_center(int bullet_index);

	// Locked angle in radians: the ring slot the bullet holds (or is flying to).
	real_t bullet_get_orbiting_angle(int bullet_index);

	// Re-pin the locked ring center without unlocking: Anchored rings follow a
	// scripted point, Deadzone rings skip ahead, StayLocked rings jump to a
	// teleported target. Rejected (no unlock) when orbiting is off or the
	// bullet never locked.
	void bullet_set_orbiting_center(int bullet_index, const Vector2 &new_center);

	void bullet_set_orbiting_follow_mode(int bullet_index, OrbitingFollowMode new_follow_mode);

	OrbitingFollowMode bullet_get_orbiting_follow_mode(int bullet_index);

	void bullet_set_orbiting_follow_deadzone(int bullet_index, real_t new_deadzone);

	real_t bullet_get_orbiting_follow_deadzone(int bullet_index);

	void bullet_set_orbiting_lock_policy(int bullet_index, OrbitingLockPolicy new_lock_policy);

	OrbitingLockPolicy bullet_get_orbiting_lock_policy(int bullet_index);

	// When on, locked OrbitLeft/OrbitRight bullets translate 1:1 with the
	// target (same rigid snap DontMove escorts use) and keep circling: the
	// ring never lags, stretches, or re-locks when the target moves. When
	// off, locked bullets chase the ring clamped to speed * delta, so slow
	// bullets trail behind fast targets. Never drops the lock.
	void bullet_set_orbiting_rigid_follow(int bullet_index, bool new_rigid_follow);

	bool bullet_get_orbiting_rigid_follow(int bullet_index);

	void all_bullets_set_orbiting_rigid_follow(bool new_rigid_follow, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	PackedFloat32Array all_bullets_get_orbiting_radius(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Bulk center reads (same per-bullet warnings as bullet_get_orbiting_center).
	// Example: var centers = bullets.all_bullets_get_orbiting_center().
	PackedVector2Array all_bullets_get_orbiting_center(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Bulk ring-slot reads (same per-bullet warnings as bullet_get_orbiting_angle).
	PackedFloat32Array all_bullets_get_orbiting_angle(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Which deque feeds each bullet ("per", "shared", or "none"): per-bullet
	// wins, shared is the fallback, matching the steering tick.
	// Example: var info = bullets.debug_get_orbiting_info(0); print(info["deque_src"]).
	Dictionary debug_get_orbiting_info(int bullet_index) const;

	// Bulk homing-queue depths. Example: var n = bullets.all_bullets_get_homing_targets_amount().
	PackedInt32Array all_bullets_get_homing_targets_amount(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	TypedArray<bool> all_bullets_is_orbiting_enabled(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_disable_orbiting(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_set_orbiting_radius(real_t new_radius, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_set_orbiting_direction(OrbitingDirection new_direction, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_set_orbiting_texture_rotation(OrbitingTextureRotation new_rotation, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_set_orbiting_follow_mode(OrbitingFollowMode new_follow_mode, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_set_orbiting_follow_deadzone(real_t new_deadzone, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_set_orbiting_lock_policy(OrbitingLockPolicy new_lock_policy, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_set_orbiting_center(const Vector2 &new_center, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	TypedArray<bool> all_bullets_is_orbiting_locked(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	////////////////

	///////////// PER BULLET HOMING DEQUE POP METHODS

	Variant bullet_homing_pop_front_target(int bullet_index);

	Variant bullet_homing_pop_back_target(int bullet_index);
	/////////////////////

	//////////////// PER BULLET HOMING DEQUE PUSH METHODS

	bool bullet_homing_push_front_mouse_position_target(int bullet_index);

	bool bullet_homing_push_front_node2d_target(int bullet_index, Node2D *new_homing_target);

	bool bullet_homing_push_front_global_position_target(int bullet_index, const Vector2 &global_position);

	bool bullet_homing_push_back_mouse_position_target(int bullet_index);

	bool bullet_homing_push_back_node2d_target(int bullet_index, Node2D *new_homing_target);

	bool bullet_homing_push_back_global_position_target(int bullet_index, const Vector2 &global_position);

	// Single-bullet Variant push (Node2D or Vector2), mirroring the
	// all_bullets_*_homing_target type branch. Returns false with an error on
	// invalid index or target type, pushing nothing.
	bool bullet_homing_push_back_homing_target(int bullet_index, const Variant &node2d_or_global_position);

	bool bullet_homing_push_front_homing_target(int bullet_index, const Variant &node2d_or_global_position);
	/////////////////////////////

	///  PER BULLET HOMING DEQUE HELPERS

	void bullet_clear_homing_targets(int bullet_index);

	Array all_bullets_pop_front_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	Array all_bullets_pop_back_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Bulk push paths skip disabled slots silently: the per-bullet push
	// rejects them loudly, and a retarget pass over a partially-disabled
	// volley must not spam one error per bullet per interval. Declared
	// before every bulk path that uses it (including the mouse pushes).
	_ALWAYS_INLINE_ bool orbit_skip_disabled_in_bulk(int bullet_index) const {
		return bullet_index < 0 || bullet_index >= amount_bullets || !all_bullets_enabled_set.contains(bullet_index);
	}

	void all_bullets_push_back_mouse_position_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_push_front_mouse_position_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Bulk push paths skip disabled slots silently (see the helper above the
	// mouse pushes): the per-bullet push rejects them loudly, and a retarget
	// pass over a partially-disabled volley must not spam one error per
	// bullet per interval.
	void all_bullets_push_back_homing_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_push_front_homing_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_push_back_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_push_front_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Per-bullet variant of the shared replace above: instead of clear (which
	// unlocks) + push, clear the raw deque inline and re-pin surviving locks
	// onto the fresh front. Returns false when the slot is disabled or the
	// target invalid (nothing touched): bulk callers skip disabled slots
	// silently, direct script calls with a bad target still get the loud
	// error below. A direct call on a disabled slot stays silent (bulk
	// parity) — wake the bullet first, then replace.
	bool bullet_replace_homing_targets_with_new_target(int bullet_index, const Variant &node2d_or_global_position);

	_ALWAYS_INLINE_ void orbit_keep_lock_across_replace_for_bullet(int bullet_index, HomingTargetDeque &deque);

	// Front-change core: shared by pops (any position) and replaces.
	// front_changed tells whether the front target is a different target
	// than before: a back-pop leaves the front untouched, so only an empty
	// deque unlocks there, while RelockAlways still re-locks on a real
	// front swap.
	_ALWAYS_INLINE_ void orbit_keep_lock_across_replace_for_bullet_front_only(int bullet_index, HomingTargetDeque &deque, bool front_changed = true);

	// Single routing point for every per-bullet front change that is not an
	// explicit clear: manual pops (front/back) and the deferred auto-pop
	// flush funnel here. Empty deque = policy-aware unlock (StayLocked rides
	// out the gap); non-empty deque = keep or unlock per lock policy, so
	// RelockAlways never holds a stale lock on a new target and
	// StayLocked/RelockOnTargetChange never flicker on the same target.
	_ALWAYS_INLINE_ void orbit_route_front_change_for_bullet(int bullet_index, HomingTargetDeque &deque);

	void all_bullets_replace_homing_targets_with_new_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_replace_homing_targets_with_mouse(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_assign_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	void all_bullets_clear_homing_targets(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	int bullet_homing_check_targets_amount(int bullet_index) const;

	bool bullet_check_has_homing_targets(int bullet_index) const;

	HomingType bullet_homing_check_current_target_type(int bullet_index) const;

	Variant bullet_get_current_homing_target(int bullet_index) const;

	//////////////////////////////

	// SHARED BULLET HOMING DEQUE POP METHODS

	Variant shared_homing_deque_pop_front_target();

	Variant shared_homing_deque_pop_back_target();

	// SHARED BULLET HOMING DEQUE PUSH METHODS
	// Push-front always swaps the front target, so every bullet is re-armed for
	// it. Push-back only re-arms when the deque was empty (that push creates
	// the front); otherwise the front is unchanged and fired flags must stay.
	// A push onto a volley with zero enabled bullets is rejected: the tick
	// never moves disabled slots, so queuing there only inflates the shared
	// state (and the global mouse counter) with targets that never drain.

	_ALWAYS_INLINE_ bool orbit_reject_fully_disabled_volley(const char *function_name) const;

	void shared_homing_deque_push_front_mouse_position_target();

	void shared_homing_deque_push_front_node2d_target(Node2D *new_homing_target);

	void shared_homing_deque_push_front_global_position_target(const Vector2 &global_position);

	void shared_homing_deque_push_back_mouse_position_target();

	void shared_homing_deque_push_back_node2d_target(Node2D *new_homing_target);

	void shared_homing_deque_push_back_global_position_target(const Vector2 &global_position);

	////////////////////////////////////

	/// SHARED BULLET HOMING DEQUE HELPER METHODS

	void shared_homing_deque_push_back_homing_targets_array(const Array &node2ds_or_global_positions_array);

	void shared_homing_deque_push_front_homing_targets_array(const Array &node2ds_or_global_positions_array);

	void shared_homing_deque_clear_homing_targets();

	// Replace = clear + push, but locks must survive the intermediate empty
	// deque under StayLocked/RelockOnTargetChange: validate first (invalid
	// input keeps the old queue AND the old lock), then re-pin surviving
	// locks onto the fresh front instead of dropping them.
	void shared_homing_deque_replace_homing_targets_with_new_target(const Variant &node2d_or_global_position);

	void shared_homing_deque_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array);

	_ALWAYS_INLINE_ int shared_homing_deque_check_homing_targets_amount() const {
		return shared_homing_deque.get_homing_targets_amount();
	}

	_ALWAYS_INLINE_ bool shared_homing_deque_check_has_homing_targets() const {
		return shared_homing_deque.has_homing_targets();
	}

	_ALWAYS_INLINE_ HomingType shared_homing_deque_check_current_target_type() const {
		return shared_homing_deque.get_current_target_type();
	}

	_ALWAYS_INLINE_ Variant shared_homing_deque_get_current_homing_target() const {
		return shared_homing_deque.get_current_homing_target();
	}

	/////////////////////////

	// Teleport bookkeeping for locked orbits: re-aim the ring slot from the
	// bullet's new offset around the current ring center, so the next tick
	// holds the teleported position instead of snapping back. Unlocked or
	// non-orbiting bullets are untouched. The shift delta is unused for the
	// angle itself (the new absolute offset decides it) but keeps the
	// signature symmetric with the teleport paths.
	_ALWAYS_INLINE_ void orbit_reflect_teleport(int bullet_index, const Vector2 &p_shift_delta);

	// Teleports a bullet to a new global position. A locked orbit re-aims
	// its ring slot from the new offset (same angle convention as the lock
	// moment) instead of snapping the bullet back next tick: teleporting is
	// an explicit user move, not target motion.
	void teleport_bullet(int bullet_index, const Vector2 &new_global_pos);

	// Shifts a bullet's position by a certain amount. Same locked-ring
	// re-aim as teleport_bullet (see above).
	void teleport_shift_bullet(int bullet_index, const Vector2 &shift_amount);

	void teleport_shift_all_bullets(const Vector2 &shift_amount, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Sets a bullet's velocity directly (wind, knockback, split inheritance).
	// Decomposes into direction + speed so the per-tick integrator
	// (velocity = direction * speed + inherited offset) keeps producing exactly
	// this velocity. max_speed is raised when below the new speed so the next
	// tick doesn't snap it back down. Non-finite input is rejected.
	void bullet_set_velocity(int bullet_index, const Vector2 &new_velocity);

	void all_bullets_set_velocity(const Vector2 &new_velocity, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	TypedArray<Vector2> all_bullets_get_velocity(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;

	// Property getters and setters
	real_t get_homing_smoothing() const { return homing_smoothing; }
	void set_homing_smoothing(real_t value);
	real_t get_homing_update_interval() const { return homing_update_interval; }
	void set_homing_update_interval(real_t value);
	bool get_homing_take_control_of_texture_rotation() const { return homing_take_control_of_texture_rotation; }
	void set_homing_take_control_of_texture_rotation(bool value) { homing_take_control_of_texture_rotation = value; }
	bool get_bullet_homing_auto_pop_after_target_reached() const { return bullet_homing_auto_pop_after_target_reached; }
	void set_bullet_homing_auto_pop_after_target_reached(bool value) { bullet_homing_auto_pop_after_target_reached = value; }
	real_t get_homing_distance_before_reached() const { return homing_distance_before_reached; }
	void set_homing_distance_before_reached(real_t value);
	bool get_shared_homing_deque_auto_pop_after_target_reached() const { return shared_homing_deque_auto_pop_after_target_reached; }
	void set_shared_homing_deque_auto_pop_after_target_reached(bool value) { shared_homing_deque_auto_pop_after_target_reached = value; }

	// Per-bullet turn agility. Setting any value enables per-bullet mode, after
	// which update_homing ignores the shared homing_smoothing. Same validation
	// as the shared setter. Reading returns the effective value per bullet.
	real_t bullet_get_homing_smoothing(int bullet_index) const;
	void bullet_set_homing_smoothing(int bullet_index, real_t value);
	void all_bullets_set_homing_smoothing(real_t value, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);
	// Effective per-bullet smoothing over a range (mirrors all_bullets_get_velocity).
	TypedArray<real_t> all_bullets_get_homing_smoothing(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	// Clears per-bullet smoothing mode: the shared homing_smoothing drives
	// every bullet again. No-op when per-bullet mode was never enabled.
	void clear_per_bullet_homing_smoothing() {
		use_per_bullet_homing_smoothing = false;
	}

	// BOUNCE / RICOCHET RUNTIME API (spawn-data equivalents, editable live).
	int get_bounce_mask() const { return bounce_mask; }
	void set_bounce_mask(int value);
	void set_bounce_mask_from_array(const TypedArray<int> &numbers);
	bool get_bounce_tilemap_layers() const { return bounce_tilemap_layers; }
	void set_bounce_tilemap_layers(bool value) {
		bounce_tilemap_layers = value;
	}
	real_t get_bounce_strength() const { return bounce_strength; }
	void set_bounce_strength(real_t value);
	bool get_bounce_push_assist() const { return bounce_push_assist; }
	void set_bounce_push_assist(bool value) { bounce_push_assist = value; }
	bool get_bounce_charge_amplify() const { return bounce_charge_amplify; }
	void set_bounce_charge_amplify(bool value) { bounce_charge_amplify = value; }
	bool get_bounce_hit_consumed() const { return bounce_hit_consumed; }
	void set_bounce_hit_consumed(bool value) { bounce_hit_consumed = value; }
	int get_bounce_max_count() const { return bounce_max_count; }
	void set_bounce_max_count(int value);
	int get_bounce_mode() const { return bounce_mode; }
	void set_bounce_mode(int value);
	bool get_bounce_rotate_texture() const { return bounce_rotate_texture; }
	void set_bounce_rotate_texture(bool value) { bounce_rotate_texture = value; }
	real_t get_bounce_rotation_smooth() const { return bounce_rotation_smooth; }
	void set_bounce_rotation_smooth(real_t value);
	real_t get_bounce_randomness_deg() const { return bounce_randomness_deg; }
	void set_bounce_randomness_deg(real_t value);
	real_t get_bounce_cooldown_sec() const { return bounce_cooldown_sec; }
	void set_bounce_cooldown_sec(real_t value);
	real_t get_bounce_debounce_sec() const { return bounce_debounce_sec; }
	void set_bounce_debounce_sec(real_t value);
	// How many times one bullet has bounced in its current life.
	int bullet_get_bounce_count(int bullet_index) const;
	// Bounce counts over a range (mirrors all_bullets_get_velocity).
	TypedArray<int> all_bullets_get_bounce_count(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	// Previous-tick origin used by physics interpolation. Lets tests prove
	// the render path stays continuous across teleports and bounces.
	Vector2 debug_get_previous_origin(int bullet_index) const;
	// Bounce introspection for tests/support: counts, config echo, pending
	// visual state. Never mutates.
	Dictionary debug_get_bounce_info(int bullet_index) const;
	// Seeds bounce config + zeroes the per-bullet ledger from spawn data.
	// Called from the custom spawn/enable logic alongside wobble/gravity.
	void apply_bounce_from_data(const BulletVolleyData2D &volley_data, int data_collision_mask);
	// Bounce decision for one queued collision record. Called from
	// handle_bullet_collision() BEFORE the hit counter increments.
	// Returns 0 = not a bounce (take the normal path), 1 = bounced and the
	// record is fully handled (return), 2 = bounced but the hit is consumed
	// too (fall through into normal counting/signals). Implemented in the
	// .cpp (needs scene-tree + shape classes).
	// Queue-time target motion is only needed while bouncing is armed (a
	// later arm falls back to the drain's live read).
	bool wants_queued_target_motion() const { return bounce_enabled(); }
	int try_handle_bounce(CollisionType collision_type, int bullet_index, int64_t entered_instance_id, Vector2 queued_target_velocity, bool queued_velocity_valid, Vector2 queued_target_position, bool queue_position_valid);

	// Virtual methods
	void set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data, bool tile_short_arrays = false);
	// Per-bullet rotation PRESENCE. set_rotation_data seeds the rotation SoA
	// from a user array, and the seed knows something the values cannot
	// express: whether a slot came from a valid entry (possibly a deliberate
	// all-zero "no spin") or from an invalid/absent one (a gap the shared
	// fallback may fill). These record it in has_per_bullet_rotation_data.
	void reset_per_bullet_rotation_presence() {
		has_per_bullet_rotation_data.assign(amount_bullets, 0);
	}
	void mark_per_bullet_rotation_presence(int bullet_index, bool present);
	// Same contract for the linear ballistics SoA (all_cached_speed /
	// max_speed / acceleration): set_bullet_speed_data claims presence for the
	// slot it writes so a later set_shared_bullet_speed_data cannot undo it.
	void mark_per_bullet_speed_presence(int bullet_index, bool present);
	// Motion-feature lifecycle (speed SoA, rotation fallback, wobble,
	// gravity/drag, movement patterns, homing/orbit, bounce):
	// seed on a fresh spawn (the volley is not in the tree yet), reseed on a
	// pool reuse (false = refused, the caller rolls back), neutralize without
	// reseeding (drop_stale_work = a new life: scrub connections and
	// invalidate deferred work), and the tail of a full deactivation.
	void seed_motion_features(const BulletVolleyData2D &data);
	void on_volley_deactivated();

	// Resolves the spawn data's shared movement pattern Path2D and applies its
	// Curve2D to every bullet through the existing helpers. Empty path = off.
	void apply_shared_movement_pattern_from_data(const BulletVolleyData2D &volley_data);

	// Strict per-bullet indexing (used by every shared-vs-per-bullet
	// spawn-data array: speed, rotation, curves, wobble, gravity, movement
	// pattern paths, custom data, collision counts). Entry i belongs to
	// bullet i and nobody else:
	// size <= 0 → -1 (feature off for the per-bullet side / shared drives),
	// i < size → i, otherwise -1 (fall back to shared, then the default).
	// Longer arrays ignore the extras. Callers warn once per spawn call on
	// size != N (and != 0). Opt-in tiling lives in resolve_tiled_data_index.
	int resolve_strict_data_index(int array_size, int bullet_index) const;
	// Opt-in wrap-around for one array when its tile_* checkbox is checked:
	// slot i reads entry (i % size), so 2 entries fan across 10 bullets as
	// A,B,A,B... Invalid entries still fall back per slot. Empty → -1.
	int resolve_tiled_data_index(int array_size, int bullet_index) const;
	// Legacy alias kept for internal call sites not yet migrated.
	int resolve_unified_data_index(int array_size, int bullet_index) const {
		return resolve_strict_data_index(array_size, bullet_index);
	}
	// Legacy alias kept for internal call sites not yet migrated.
	int resolve_per_bullet_data_index(int array_size, int bullet_index) const {
		return resolve_strict_data_index(array_size, bullet_index);
	}

	// Gravity introspection for tests/support: {vector, fall_speed,
	// window_active, curve_scale}. Never mutates.
	Dictionary debug_get_gravity_info(int bullet_index) const;

	// Seeds per-bullet curves/patterns from spawn data through the regular
	// per-bullet helpers (null entries skipped). Called from the custom
	// spawn/enable logic alongside the shared application; storage is
	// separate so ordering between them is irrelevant.
	void apply_per_bullet_curves_from_data(const BulletVolleyData2D &volley_data);
	void apply_per_bullet_movement_patterns_from_data(const BulletVolleyData2D &volley_data);
	void apply_wobble_from_data(const BulletVolleyData2D &volley_data);
	void apply_gravity_from_data(const BulletVolleyData2D &volley_data);
	// Shared-as-fallback gap fillers (unified precedence: per-bullet wins).
	// Only slots holding invalid ballistics (non-finite speed triple or
	// inactive rotation) are overwritten from shared; valid per-bullet
	// slots are never touched.
	void apply_shared_speed_fallback(const Ref<BulletSpeedData2D> &shared);
	void apply_shared_rotation_fallback(const Ref<BulletRotationData2D> &shared, bool new_rotate_only_textures);

	// Gravity time window over volley life (seconds since spawn, read on
	// curves_elapsed_time): integrates only inside [delay, delay + duration].
	bool gravity_window_open() const;

	// Gravity strength scale for one bullet from shared/per-bullet curves
	// (per-bullet wins when valid, like direction curves). Null curves read
	// 1.0; a non-finite sample reads 1.0 (neutral) so a broken curve can
	// never brick or invert the fall.
	real_t gravity_strength_scale_for_bullet(const BulletCurvesData2D *shared, const BulletCurvesData2D *per_bullet) const;

	// WOBBLE / GRAVITY / DRAG / HOMING-GATE RUNTIME API (spawn-data
	// equivalents, editable live on the instance).
	WobbleSeed make_wobble_seed(const Ref<BulletWobbleData2D> &wobble, int bullet_index) const;
	void refresh_wobble_feature_flag();
	bool get_is_wobble_enabled() const { return is_wobble_feature_enabled; }
	// Effective wobble amplitude of one bullet (seeded amplitude, 0 when inactive).
	real_t bullet_get_wobble_amplitude(int bullet_index) const;
	// Effective per-channel curves winner for one bullet: "per", "shared",
	// or "none". Lets you prove which resource drives x / y / rotation /
	// speed / gravity-strength without reading the tick.
	Dictionary debug_get_curves_info(int bullet_index) const;
	// Effective movement-pattern state for one bullet: which side drives it,
	// its flags, and how far along it is. Finished shared parks at length;
	// finished per-bullet clears to no-pattern.
	Dictionary debug_get_pattern_info(int bullet_index) const;
	// Effective wobble face flag of one bullet (seeded value; false when
	// inactive). Use debug_get_wobble_info for the full seed incl. mode,
	// waveform, phase, damping, delay and duration.
	bool bullet_get_wobble_face_movement_direction(int bullet_index) const;
	real_t bullet_get_wobble_face_rotation_speed(int bullet_index) const;
	// Live wobble seeding (mirrors the curves runtime API). Per-bullet wins
	// over shared at seed time; null clears that slot back to the shared
	// fallback (or inactive when no shared fallback is set).
	void bullet_set_wobble_data(int bullet_index, const Ref<BulletWobbleData2D> &wobble_data);
	void all_bullets_set_wobble_data(const Ref<BulletWobbleData2D> &wobble_data, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);
	Ref<BulletWobbleData2D> bullet_get_wobble_data(int bullet_index) const;
	TypedArray<BulletWobbleData2D> all_bullets_get_wobble_data(int bullet_index_start = 0, int bullet_index_end_inclusive = -1);
	void set_shared_bullet_wobble_data(const Ref<BulletWobbleData2D> &new_wobble_data);
	Ref<BulletWobbleData2D> get_shared_bullet_wobble_data() const { return shared_bullet_wobble_data; }
	bool has_shared_bullet_wobble_data() const { return shared_bullet_wobble_data.is_valid(); }
	void remove_shared_bullet_wobble_data();
	Dictionary debug_get_wobble_info(int bullet_index) const;
	Vector2 get_gravity() const { return gravity; }
	void set_gravity(const Vector2 &value);
	Vector2 bullet_get_gravity(int bullet_index) const;
	void bullet_set_gravity(int bullet_index, const Vector2 &value);
	void all_bullets_set_gravity(const Vector2 &value, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);
	// Per-bullet gravity vectors over a range (mirrors all_bullets_get_velocity).
	TypedArray<Vector2> all_bullets_get_gravity(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	double get_gravity_delay_sec() const { return gravity_delay_sec; }
	void set_gravity_delay_sec(double value);
	double get_gravity_duration_sec() const { return gravity_duration_sec; }
	void set_gravity_duration_sec(double value);
	// Fall speed magnitude of one bullet (integrated fall velocity length).
	// 0 when gravity never integrated for that slot.
	real_t bullet_get_fall_speed(int bullet_index) const;
	// Fall speeds over a range (mirrors all_bullets_get_velocity).
	TypedArray<real_t> all_bullets_get_fall_speed(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	// Full per-bullet steering snapshot for tests/support: {index, active,
	// direction, velocity, speed, gravity, fall_speed, wobble_active,
	// wobble_amplitude, has_homing_targets, homing_targets_amount,
	// homing_smoothing, orbiting_enabled, orbiting_locked, pattern,
	// shared_pattern_active, rotation_speed}. Never mutates.
	Dictionary debug_get_bullet_info(int bullet_index) const;
	real_t get_linear_drag() const { return linear_drag; }
	void set_linear_drag(real_t value);
	real_t get_homing_delay_sec() const { return homing_delay_sec; }
	void set_homing_delay_sec(real_t value);
	real_t get_homing_duration_sec() const { return homing_duration_sec; }
	void set_homing_duration_sec(real_t value);
	real_t get_homing_lose_range_px() const { return homing_lose_range_px; }
	void set_homing_lose_range_px(real_t value);

	// SHARED MOVEMENT PATTERN RUNTIME API (spawn-data equivalent, editable live
	// on the instance; per-bullet helpers live with the other pattern API). Unified
	// precedence: per-bullet patterns win per bullet; the shared curve is
	// the broadcast fallback for bullets without their own pattern.
	// Clearing it (null curve) removes the fallback.
	Ref<Curve2D> get_shared_movement_pattern_curve() const { return shared_movement_pattern_curve; }
	void set_shared_movement_pattern_curve(const Ref<Curve2D> &new_curve);

	bool get_shared_movement_pattern_face_movement_direction() const { return shared_movement_pattern_face_movement_direction; }
	void set_shared_movement_pattern_face_movement_direction(bool value) { shared_movement_pattern_face_movement_direction = value; }

	bool get_shared_movement_pattern_repeat() const { return shared_movement_pattern_repeat; }
	void set_shared_movement_pattern_repeat(bool value) { shared_movement_pattern_repeat = value; }

	bool has_shared_movement_pattern() const { return shared_movement_pattern_curve.is_valid(); }
	void remove_shared_movement_pattern() { set_shared_movement_pattern_curve(Ref<Curve2D>()); }

	// SHARED SPEED / ROTATION RUNTIME API (spawn-data equivalent, editable live
	// on the instance). Unified precedence: shared is the fallback default,
	// per-bullet ballistics win per slot. Setting shared fills only the
	// invalid-entry gaps (fully-zero triples); clearing it (null) keeps the
	// current ballistics (already resolved per bullet at seed time).
	Ref<BulletSpeedData2D> get_shared_bullet_speed_data() const { return shared_bullet_speed_data; }
	void set_shared_bullet_speed_data(const Ref<BulletSpeedData2D> &new_speed_data);
	bool has_shared_bullet_speed_data() const { return shared_bullet_speed_data.is_valid(); }
	void remove_shared_bullet_speed_data() { set_shared_bullet_speed_data(Ref<BulletSpeedData2D>()); }

	Ref<BulletRotationData2D> get_shared_bullet_rotation_data() const { return shared_bullet_rotation_data; }
	void set_shared_bullet_rotation_data(const Ref<BulletRotationData2D> &new_rotation_data);
	bool has_shared_bullet_rotation_data() const { return shared_bullet_rotation_data.is_valid(); }
	void remove_shared_bullet_rotation_data() { set_shared_bullet_rotation_data(Ref<BulletRotationData2D>()); }

	bool get_adjust_direction_based_on_rotation() const { return adjust_direction_based_on_rotation; }
	void set_adjust_direction_based_on_rotation(bool value) { adjust_direction_based_on_rotation = value; }

	// Teardown hook: drop every homing target (per-bullet + shared) through the same
	// clear helpers the enable path uses, so the global mouse-target counter can't leak
	// when a multimesh dies holding mouse targets.
	void clear_homing_state_for_teardown();


 protected:
 	// Updates homing behavior for a bullet. Zero-delta ticks steer nothing:
 	// with no time passing any direction or texture change would be motion
 	// without movement, so the bullet holds its pose. A zero heading
 	// (unseeded ballistics) also holds: steering it would snap to angle 0.
 	_ALWAYS_INLINE_ void update_homing(HomingTargetDeque &homing_deque, int bullet_index, double delta, Vector2 &bullet_pos, Vector2 &target_pos);

	// Rotates bullet to face target with smoothing (boundary-agnostic version).
	// require_homing_flag: the homing feature only rotates the texture when the user
	// opted in via homing_take_control_of_texture_rotation; orbiting's Face* modes
	// own their texture rotation unconditionally and pass false.
	_ALWAYS_INLINE_ void rotate_to_target(int bullet_index, const Vector2 &diff, real_t max_turn, bool require_homing_flag = true);

	// Rotate without touching the interpolation cache, so orbiters stay smooth instead of jittering every frame.
	_ALWAYS_INLINE_ void rotate_to_target_preserve_interpolation(int bullet_index, const Vector2 &diff, bool require_homing_flag = true);

	// Spin one bullet's sprite by its rotation speed for this tick
	_ALWAYS_INLINE_ void update_rotation(int bullet_index, double delta);

	// Spin one bullet's sprite by its rotation speed for this tick using a curve.
	// Curve spin bypasses the max/stop gate by design (the curve IS the speed
	// program); documented so a stop flag under curves never surprises.
	_ALWAYS_INLINE_ void update_rotation_using_curve(int bullet_index, double delta);

	// Per-bullet reached tracking for the SHARED homing deque. HomingTarget's own
	// has_bullet_reached_target flag would let only the FIRST bullet in range emit
	// bullet_homing_target_reached, but the signal carries bullet_index and auto-pop
	// is a per-bullet feature. Each bullet tracks which front target it already fired
	// for; popping the front re-arms everyone (reset_shared_homing_reached_state).
	// Pointer is only COMPARED (deque references stay valid while the element is
	// stored), never dereferenced.
	struct SharedHomingReachedState {
		const void *front_target = nullptr;
		bool fired = false;
	};
	std::vector<SharedHomingReachedState> all_shared_homing_reached;

	// Re-arms every bullet for the new front target. Called by EVERY shared
	// front mutation (push_front, pop_front, clear, replace).
	_ALWAYS_INLINE_ void reset_shared_homing_reached_state();

	// Reached events collected by the move loop (no user code may run
	// there), dispatched LIVE right after it (dispatch_homing_events). Each
	// event carries the bullet's collision epoch (a handler that disabled or
	// woke the bullet ends it) and the front identity it reached (auto-pop
	// only pops a front that is still that target, so a target the handler
	// pushed is never eaten). Reused buffers: no per-tick allocation.
	struct HomingReachedEvent {
		int bullet_index = -1;
		uint64_t epoch = 0;
		uint64_t target_instance_id = 0;
		Vector2 target_position;
		uint64_t front_identity = 0;
		bool auto_pop = false;
	};
	std::vector<HomingReachedEvent> homing_reached_events;
	std::vector<HomingReachedEvent> homing_dispatch_scratch;
	// One shared pop per tick no matter how many bullets arrive at once
	// (otherwise a full volley would eat the whole queue in a frame).
	bool shared_pop_requested = false;
	uint64_t shared_pop_identity = 0;
	// Emits every collected reached event (volley signal + owner spawner
	// forward), then the auto-pops. Returns false when the volley was freed.
	bool dispatch_homing_events();

	// Single routing point for every shared-deque front change: the deferred
	// shared auto-pop flush and the manual shared pops funnel here. Resets
	// the reached state (a new front target re-arms every bullet) and routes
	// the orbit lock per policy, so RelockAlways re-acquires on the new
	// target instead of holding a stale center.
	_ALWAYS_INLINE_ void orbit_route_shared_front_change();

	// Reached check against the post-move position: the caller passes the
	// already-advanced origin with a zero delta, so the test runs exactly
	// where the bullet landed this tick (orbit/pattern/curve output). A
	// point test cannot sweep-catch tunneling: when speed * delta exceeds
	// twice the threshold a bullet can jump clean over it, so size
	// homing_distance_before_reached for the fastest volleys.
	_ALWAYS_INLINE_ void try_to_emit_bullet_homing_target_reached_signal(HomingTargetDeque &homing_deque, bool is_using_shared_homing_deque, int bullet_index, const Vector2 &bullet_pos, const Vector2 &target_pos, const Vector2 &post_velocity_delta);

	// Normalizes an angle to [-PI, PI]
	_ALWAYS_INLINE_ void normalize_angle(real_t &angle) const {
		angle = Math::wrapf(angle, -static_cast<real_t>(Math::PI), static_cast<real_t>(Math::PI));
	}

	// Updates the homing timer and checks if interval is reached
	_ALWAYS_INLINE_ bool update_homing_timer(double delta);

	static void _bind_methods();
};
} // namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::HomingType);
VARIANT_ENUM_CAST(BlastBullets2D::BulletVolley2D::OrbitingDirection);
VARIANT_ENUM_CAST(BlastBullets2D::BulletVolley2D::OrbitingTextureRotation);
VARIANT_ENUM_CAST(BlastBullets2D::BulletVolley2D::OrbitingFollowMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletVolley2D::OrbitingLockPolicy);
