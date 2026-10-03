#pragma once

#include "../shared/warn_once2d.hpp"
#include "../factory/bullet_factory2d.hpp"
#include "../shared/bullet_attachment2d.hpp"
#include "../shared/bullet_attachment_object_pool2d.hpp"
#include "../shared/bullet_effect_layer_data2d.hpp"
#include "../shared/bullet_rotation_data2d.hpp"
#include "../shared/cached_string_names2d.hpp"
#include "../shared/reentrancy_guard2d.hpp"
#include "../spawn-data/directional_bullets_data2d.hpp"
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
#include "shared/bullet_curves_data2d.hpp"
#include "shared/bullet_movement_pattern_data2d.hpp"
#include "shared/bullet_speed_data2d.hpp"
#include "shared/collision_shape_helper2d.hpp"
#include "shared/dynamic_sparse_set.hpp"
#include "shared/multimesh_pool_key2d.hpp"
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
#include "../shared/bullet_speed_data2d.hpp"
#include "../shared/bullet_wobble_data2d.hpp"
#include "../shared/homing_target_deque.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "spawn-data/directional_bullets_data2d.hpp"

namespace BlastBullets2D {
using namespace godot;

class MultiMeshObjectPool;

// One volley: N bullets drawn by a single MultiMeshInstance2D, collided by
// one physics area (one shared shape) and moved by one per-bullet loop
// (move_bullets). Spawned and pooled by BulletFactory2D, steered through
// the returned instance or by the BulletSpawner2D that fired it.
class DirectionalBullets2D : public MultiMeshInstance2D {
	GDCLASS(DirectionalBullets2D, MultiMeshInstance2D)
public:
	// Godot's memnew cannot forward constructor arguments, so instances are created
	// with memnew and then initialized through spawn(). Always call spawn() after memnew.

	// Whether all the bullets should be processed/moved/rotated etc.. or just skipped (basically this value should be equal to false only when ALL bullets are completely disabled)
	bool is_active = false;

	// The id of the multimesh inside the bullet factory's sparse set
	int sparse_set_id = -1;

	// Counts spawn/enable cycles. Deferred attachment disables carry the value they were
	// queued with so pool reuse in between can't misfire them onto a new owner.
	int multimesh_generation = 0;

	// Same idea for time-based functions: a deferred attach stamped with a stale
	// generation is dropped, so a full-disable landing first can't leak it onward.
	int multimesh_timers_generation = 0;

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
	// skips a volley mid-tick, and enable_multimesh refuses it, so the
	// drain can never be pulled out from under itself. Every OTHER pooled
	// volley stays reusable mid-sweep (same-key spawns from handlers work).
	bool is_being_ticked = false;

	// Set when an expiry with life_time_over signals drains the volley:
	// the volley keeps its attachment slots and stays OUT of the pool until
	// the deferred signal (and its attachment releases) have flushed. Pooling
	// at once let a same-frame spawn pop the volley, bump the generation and
	// silently drop the signal, and the disable sweep released attachments
	// before the handler could see them. _do_finish_lifetime_hold() pools.
	bool lifetime_flush_pending = false;

	// Gets the total amount of bullets that the multimesh always holds
	_ALWAYS_INLINE_ int get_amount_bullets() const { return amount_bullets; };

	// Gets the total amount of attachments that are active
	int get_amount_active_attachments() const;

	// Used to spawn brand new bullets that are active in the scene tree.
	// spawner_id stamps signal ownership BEFORE any physics/tree activation
	// (configure-then-attach): a spawner passes its instance id so the volley
	// is never observable as factory-owned. 0 = factory-owned (default).
	void spawn(const DirectionalBulletsData2D &spawn_data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container, const Vector2 &new_inherited_velocity_offset, int new_sparse_set_id, bool spawn_in_pool, uint64_t spawner_id = 0);

	// Native transform source for the NEXT spawn()/enable_multimesh(): when
	// set, the volley reads its bullet transforms from this span instead of
	// unboxing data.transforms (a Variant per bullet). The factory sets it
	// around the call; set_up_bullet_instances consumes and clears it.
	// Resource id of the data being applied (spawn/enable): keys the
	// once-per-resource configuration warnings (WarnOnce2D).
	uint64_t warn_data_id = 0;
	const Transform2D *spawn_transforms_ptr = nullptr;
	int spawn_transforms_count = 0;
	_ALWAYS_INLINE_ int spawn_transform_count(const DirectionalBulletsData2D &data) const {
		return spawn_transforms_ptr != nullptr ? spawn_transforms_count : (int)data.transforms.size();
	}

	// Activates the multimesh. Returns false (without leaving it factory-active)
	// when the spawn data is incompatible, so the pool owner can re-push it.
	// spawner_id works like spawn()'s: stamped before re-activation.
	bool enable_multimesh(const DirectionalBulletsData2D &data, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id = 0);
	// GDScript entry for enable_multimesh (Ref-based; native callers use the
	// reference overload directly). Returns false on null data without touching state.
	bool enable_multimesh_for_script(const Ref<DirectionalBulletsData2D> &data, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0), uint64_t spawner_id = 0) {
		if (data.is_null()) {
			UtilityFunctions::push_error("enable_multimesh: spawn data is null.");
			return false;
		}
		return enable_multimesh(*data.ptr(), new_inherited_velocity_offset, spawner_id);
	}

	// Internal delete, C++ side only. Never call this from GDScript or from inside a
	// physics callback; factory free/reset methods already call it for you at a safe time.
	void force_delete() {
		marked_for_internal_deletion = true;
		Node *parent = get_parent();
		if (parent) {
			parent->remove_child(this);
		}
		memdelete(this); // Immediate deletion after removal from tree
	}

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
		const DirectionalBullets2D *owner;
		bool saved_active;
		bool saved_valid;
		Transform2D saved_inverse;
		explicit NodeInverseScope(const DirectionalBullets2D *p_owner) :
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

	_ALWAYS_INLINE_ Transform2D to_local_for_multimesh(const Transform2D &global_transf) const {
		if (node_inverse_scope_active) {
			return node_inverse_scope_valid ? node_inverse_scope * global_transf : global_transf;
		}
		const Transform2D node_global = get_global_transform();
		if (!is_transform_invertible_safe(node_global)) {
			return global_transf;
		}
		return node_global.affine_inverse() * global_transf;
	}

	// Use this method when you want to use physics interpolation - smooth rendering of textures despite physics ticks per second
	_ALWAYS_INLINE_ void interpolate_bullet_visuals() {
		if (!bullet_factory || !bullet_factory->use_physics_interpolation) {
			return;
		}
		if (!multi.is_valid()) {
			return;
		}
		if ((int)all_cached_instance_transforms.size() != amount_bullets || (int)all_previous_instance_transf.size() != amount_bullets) {
			return;
		}
		if ((int)batch_buffer.size() != amount_bullets * 8) {
			return;
		}
		double fraction = Engine::get_singleton()->get_physics_interpolation_fraction();
		// Degenerate node global (zero scale) has no inverse: skip the frame
		// instead of writing a non-finite buffer (see to_local_for_multimesh).
		const Transform2D node_global = get_global_transform();
		if (!is_transform_invertible_safe(node_global)) {
			return;
		}
		const Transform2D multimesh_inv = node_global.affine_inverse();

		// batch_buffer sized in spawn/set_up_multimesh (amount never changes on reuse)
#ifdef DEV_ENABLED
		ERR_FAIL_COND((int)batch_buffer.size() != amount_bullets * 8);
#endif
		float *w = batch_buffer.ptrw();
		for (int i = 0; i < amount_bullets; ++i) {
			Transform2D t;
			if (all_bullets_enabled_set.contains(i)) {
				t = multimesh_inv * get_interpolated_transform(all_cached_instance_transforms[i], all_previous_instance_transf[i], fraction);
			} else {
				t = zero_transform;
			}
			w[i * 8 + 0] = t.columns[0][0];
			w[i * 8 + 1] = t.columns[1][0];
			w[i * 8 + 2] = 0;
			w[i * 8 + 3] = t.columns[2][0];
			w[i * 8 + 4] = t.columns[0][1];
			w[i * 8 + 5] = t.columns[1][1];
			w[i * 8 + 6] = 0;
			w[i * 8 + 7] = t.columns[2][1];
		}
		multi->set_buffer(batch_buffer);
		const auto &active_bullet_indexes = all_bullets_enabled_set.get_active_indexes();
		for (int i : active_bullet_indexes) {
			if (i < 0 || i >= amount_bullets || i >= (int)attachments.size() || i >= (int)attachment_transforms.size() || i >= (int)all_previous_attachment_transf.size()) {
				continue;
			}
			if (!attachments[i])
				continue;
			const Transform2D &at = get_interpolated_transform(attachment_transforms[i], all_previous_attachment_transf[i], fraction);
			attachments[i]->set_global_transform(at);
		}
		// Trail shards follow the lerped bullet pose (same derivation as the
		// physics tick, so trails never lag the rendered bullets by a step).
		// Frame routing still keys off the physics clock (discrete, no lerp
		// needed); one-shot shards are static and need nothing here.
		if (!fx_trail_bakes.empty() && (int)all_cached_instance_transforms.size() == amount_bullets && (int)all_previous_instance_transf.size() == amount_bullets) {
			NodeInverseScope trail_inverse_scope(this);
			for (int i : active_bullet_indexes) {
				if (i < 0 || i >= amount_bullets) {
					continue;
				}
				const Transform2D lerped_global = get_interpolated_transform(all_cached_instance_transforms[i], all_previous_instance_transf[i], fraction);
				write_trail_from_global(i, lerped_global);
			}
		}
	}

	_ALWAYS_INLINE_ void batch_flush_instance_transforms() {
		if (!multi.is_valid() || amount_bullets != multi->get_instance_count()) {
			return;
		}
		if ((int)all_cached_instance_transforms.size() != amount_bullets || (int)batch_buffer.size() != amount_bullets * 8) {
			return;
		}
#ifdef DEV_ENABLED
		ERR_FAIL_COND((int)batch_buffer.size() != amount_bullets * 8);
#endif
		float *w = batch_buffer.ptrw();
		// Same degenerate-global guard as interpolate_bullet_visuals: never
		// write a non-finite inverse into the buffer.
		const Transform2D node_global = get_global_transform();
		if (!is_transform_invertible_safe(node_global)) {
			return;
		}
		const Transform2D multimesh_inv = node_global.affine_inverse();
		for (int i = 0; i < amount_bullets; ++i) {
			Transform2D t = zero_transform;
			if (all_bullets_enabled_set.contains(i)) {
				t = multimesh_inv * all_cached_instance_transforms[i];
			}
			w[i * 8 + 0] = t.columns[0][0];
			w[i * 8 + 1] = t.columns[1][0];
			w[i * 8 + 2] = 0;
			w[i * 8 + 3] = t.columns[2][0];
			w[i * 8 + 4] = t.columns[0][1];
			w[i * 8 + 5] = t.columns[1][1];
			w[i * 8 + 6] = 0;
			w[i * 8 + 7] = t.columns[2][1];
		}

		multi->set_buffer(batch_buffer);
	}

	_ALWAYS_INLINE_ void update_specific_previous_transforms_for_interpolation(int begin_bullet_index, int end_bullet_index_inclusive) {
		if (!bullet_factory || !bullet_factory->use_physics_interpolation) {
			return;
		}
		if (amount_bullets <= 0) {
			return;
		}
		if ((int)all_previous_instance_transf.size() != amount_bullets || (int)all_previous_attachment_transf.size() != amount_bullets) {
			return;
		}
		if ((int)all_cached_instance_transforms.size() != amount_bullets || (int)attachment_transforms.size() != amount_bullets) {
			return;
		}
		// Clamp instead of trusting the caller: this writes into fixed-size caches,
		// so an unvalidated range would be an OOB write.
		if (begin_bullet_index < 0) {
			begin_bullet_index = 0;
		}
		if (begin_bullet_index >= amount_bullets) {
			return;
		}
		if (end_bullet_index_inclusive >= amount_bullets) {
			end_bullet_index_inclusive = amount_bullets - 1;
		}
		if (begin_bullet_index > end_bullet_index_inclusive) {
			return;
		}

		for (int i = begin_bullet_index; i <= end_bullet_index_inclusive; ++i) {
			all_previous_instance_transf[i] = all_cached_instance_transforms[i];
			all_previous_attachment_transf[i] = attachment_transforms[i];
		}
	}

	_ALWAYS_INLINE_ void update_all_previous_transforms_for_interpolation() {
		if (!bullet_factory || !bullet_factory->use_physics_interpolation) {
			return;
		}
		if ((int)all_previous_instance_transf.size() != amount_bullets || (int)all_previous_attachment_transf.size() != amount_bullets) {
			return;
		}

		for (int i = 0; i < amount_bullets; ++i) {
			all_previous_instance_transf[i] = all_cached_instance_transforms[i];
			all_previous_attachment_transf[i] = attachment_transforms[i];
		}
	}

	// Updates interpolation data for physics
	_ALWAYS_INLINE_ void update_bullet_previous_transform_for_interpolation(int bullet_index) {
		if (!bullet_factory || !bullet_factory->use_physics_interpolation) {
			return;
		}
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return;
		}
		if (bullet_index >= (int)all_previous_instance_transf.size() || bullet_index >= (int)all_previous_attachment_transf.size()) {
			return;
		}
		if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)attachment_transforms.size()) {
			return;
		}

		all_previous_instance_transf[bullet_index] = all_cached_instance_transforms[bullet_index];
		all_previous_attachment_transf[bullet_index] = attachment_transforms[bullet_index];
	}

	_ALWAYS_INLINE_ Transform2D get_interpolated_transform(const Transform2D &curr_transf, const Transform2D &prev_transf, double fraction) {
		// Fast path: an unchanged basis (no spin/steer this tick - most
		// bullets) only needs the origin lerped. Exact, and skips 2 atan2,
		// ~6 sqrt and a sincos per bullet per rendered frame.
		if (curr_transf.columns[0] == prev_transf.columns[0] && curr_transf.columns[1] == prev_transf.columns[1]) {
			Transform2D out = curr_transf;
			out.columns[2] = prev_transf.columns[2].lerp(curr_transf.columns[2], fraction);
			return out;
		}
		// Interpolate position
		Vector2 prev_pos = prev_transf.get_origin();
		Vector2 curr_pos = curr_transf.get_origin();
		Vector2 interpolated_pos = prev_pos.lerp(curr_pos, fraction);

		// Interpolate rotation
		double prev_rot = prev_transf.get_rotation();
		double curr_rot = curr_transf.get_rotation();
		double interpolated_rot = godot::Math::lerp_angle(prev_rot, curr_rot, fraction);

		// Preserve scale (lerped): the old Transform2D(rot, pos) form reset
		// scaled bullets to scale 1 every interpolated frame (flicker).
		Vector2 interpolated_scale = prev_transf.get_scale().lerp(curr_transf.get_scale(), fraction);
		Transform2D out(interpolated_rot, interpolated_pos);
		out.set_scale(interpolated_scale);
		return out;
	}

	// Reduces the lifetime of the multimesh so it can eventually get disabled entirely
	void reduce_lifetime(double delta);

	// Advances the SpriteFrames animation baked in anim_frames/anim_frame_secs.
	// Hot path: plain countdown + index + one set_texture. No SpriteFrames calls here.
	// Texture swaps are interpolation-exempt (interpolation only lerps transforms).
	_ALWAYS_INLINE_ void advance_sprite_animation(double delta) {
		const int64_t frame_count = (int64_t)anim_frames.size();
		if (frame_count <= 1 || !is_active || anim_paused || anim_finished) {
			return;
		}
		if (!Math::is_finite(delta) || delta <= 0.0) {
			return;
		}
		if (delta > 0.5) {
			delta = 0.5; // clamp hitch spikes so one tick can't fast-forward whole anims
		}
		anim_frame_time_left -= delta;
		int strides = 0;
		while (anim_frame_time_left <= 0.0) {
			if (++strides > 8) {
				// Anti-spiral: resync timer to current frame instead of looping forever.
				anim_frame_time_left = anim_frame_secs[anim_frame_index] > 0.0 ? anim_frame_secs[anim_frame_index] : 0.0;
				break;
			}
			int next = anim_frame_index + 1;
			if (next >= frame_count) {
				if (anim_loop) {
					next = 0;
				} else {
					anim_frame_index = (int)frame_count - 1;
					anim_frame_time_left = 0.0;
				if (!anim_finished) {
					anim_finished = true;
					// Deferred like the life_time_over signals: never emit directly from physics tick.
					// NOTE: the signal lives on the multimesh itself (not the factory),
					// so it must be emitted on `this`. Generation-guarded: a pool
					// reuse before the flush must not emit for the new life.
					call_deferred(CachedStringNames2D::get().m_do_emit_sprite_animation_finished, multimesh_generation);
				}
					return;
				}
			}
			anim_frame_index = next;
			set_texture(anim_frames[anim_frame_index]);
			anim_frame_time_left += anim_frame_secs[anim_frame_index];
			// Guard against zero-length frames looping forever in one tick.
			if (anim_frame_secs[anim_frame_index] <= 0.0) {
				break;
			}
		}
	}
	///

	_ALWAYS_INLINE_ TypedArray<bool> get_all_bullets_status() {
		TypedArray<bool> status_array;
		status_array.resize(amount_bullets);

		for (int i = 0; i < amount_bullets; i++) {
			bool status = all_bullets_enabled_set.contains(i);
			status_array[i] = status;
		}

		return status_array;
	}

	_ALWAYS_INLINE_ bool is_bullet_status_enabled(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "is_bullet_status_enabled")) {
			return false;
		}

		return all_bullets_enabled_set.contains(bullet_index);
	}

	_ALWAYS_INLINE_ Ref<Resource> get_shared_bullets_custom_data() const {
		return shared_bullets_custom_data;
	}

	_ALWAYS_INLINE_ void set_shared_bullets_custom_data(const Ref<Resource> &new_shared_bullets_custom_data) {
		shared_bullets_custom_data = new_shared_bullets_custom_data;
	}

	// Per-bullet custom data (seeded from spawn data). Strictly separated from
	// shared_bullets_custom_data: a bullet with no per-bullet value reads as
	// null, never as the shared value, so the two can never be confused.
	_ALWAYS_INLINE_ Ref<Resource> bullet_get_custom_data(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_custom_data")) {
			return Ref<Resource>();
		}
		if (bullet_index >= 0 && bullet_index < (int)all_bullets_custom_data.size() && all_bullets_custom_data[bullet_index].is_valid()) {
			return all_bullets_custom_data[bullet_index];
		}
		return Ref<Resource>();
	}
	_ALWAYS_INLINE_ void bullet_set_custom_data(int bullet_index, const Ref<Resource> &new_custom_data) {
		if (!validate_bullet_index(bullet_index, "bullet_set_custom_data")) {
			return;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_bullets_custom_data.size()) {
			return;
		}
		all_bullets_custom_data[bullet_index] = new_custom_data;
	}

	_ALWAYS_INLINE_ TypedArray<Resource> all_bullets_get_custom_data(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_custom_data");
		TypedArray<Resource> arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_get_custom_data(i));
		}
		return arr;
	}
	_ALWAYS_INLINE_ void all_bullets_set_custom_data(const Ref<Resource> &new_custom_data, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_custom_data");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_custom_data(i, new_custom_data);
		}
	}

	Vector2 get_inherited_velocity_offset() const { return inherited_velocity_offset; }
	void set_inherited_velocity_offset(const Vector2 &new_offset) {
		if (!new_offset.is_finite()) {
			UtilityFunctions::push_error("set_inherited_velocity_offset: offset must be finite, keeping the old value.");
			return;
		}
		inherited_velocity_offset = new_offset;
		// Recompose live velocities now: the tick only recomputes when the
		// direction changes, so without this the getter stays stale until
		// the next steer (forever while paused or fully disabled).
		for (size_t k = 0; k < all_cached_velocity.size() && k < all_cached_direction.size() && k < all_cached_speed.size(); ++k) {
			all_cached_velocity[k] = all_cached_direction[k] * all_cached_speed[k] + inherited_velocity_offset;
		}
	}

	bool get_is_multimesh_auto_pooling_enabled() const { return is_multimesh_auto_pooling_enabled; }
	void set_is_multimesh_auto_pooling_enabled(bool value) { is_multimesh_auto_pooling_enabled = value; }

	bool get_is_attachments_auto_pooling_enabled() const { return is_attachments_auto_pooling_enabled; }
	void set_is_attachments_auto_pooling_enabled(bool value) { is_attachments_auto_pooling_enabled = value; }

	// Pooling flags reset to defaults on every new life (spawn/enable via
	// reset_transient_volley_state): a pooled reuse never inherits "don't pool"
	// from a previous manual owner. Set the flags explicitly after every
	// spawn/enable when you want manual ownership. Same-owner enable_bullet()
	// wakes keep flags by design (no new life starts).
	void reset_pooling_flags_to_default() {
		is_multimesh_auto_pooling_enabled = true;
		is_attachments_auto_pooling_enabled = true;
	}

	Ref<BulletCurvesData2D> get_shared_bullet_curves_data() const { return shared_bullet_curves_data; }
	void set_shared_bullet_curves_data(const Ref<BulletCurvesData2D> &new_curves_data) {
		populate_shared_curves_related_data(new_curves_data);
	}
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
	void snapshot_appearance_from_data(const DirectionalBulletsData2D &data);
	// One fade step, driven by curves_elapsed_time (the volley age clock).
	// No-op unless a fade is configured. Per-volley O(1): one CanvasItem
	// write at most, only while the value actually changes.
	void tick_volley_fade();

	// Bullet movement pattern

	_ALWAYS_INLINE_ bool check_exists_bullet_movement_pattern_data(int bullet_index) const {
		if (bullet_index < 0 || bullet_index >= (int)all_movement_pattern_data.size()) {
			return false;
		}
		return all_movement_pattern_data[bullet_index].path_curve.is_valid();
	}

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
	PoolKey get_pool_key() const { return PoolKey{ amount_bullets, cached_effective_shape_type }; }

	void _notification(int p_what);

	bool is_multimesh_auto_pooling_enabled = true;

	bool is_attachments_auto_pooling_enabled = true;

	// Set by the pool itself - tells us at a glance whether this volley is sitting in a bucket right now.
	bool is_pooled_in_pool = false;

	// How many bullets are currently flying
	int active_bullets_counter = 0;

	// Which bullets are still alive (fast add/remove/lookup while the tick runs)
	DynamicSparseSet all_bullets_enabled_set;

	BulletFactory2D *bullet_factory = nullptr;
	// Optional spawner owner (ObjectID, 0 = none), mirroring owner_multimesh_id
	// on attachments. When a live BulletSpawner2D is tagged, collision and
	// lifetime signals are possessed by it; otherwise they stay on the factory.
	// Plain integer: no ownership, nothing to clean up on free. Reset in
	// spawn()/enable_multimesh() so pooled reuse never inherits a stale owner;
	// stamped by BulletSpawner2D::shoot_once() after every spawn it performs.
	uint64_t owner_spawner_id = 0;
	MultiMeshObjectPool *bullets_pool = nullptr;
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
	real_t bullet_get_rotation_speed(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_rotation_speed")) {
			return 0.0;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_rotation_speed.size()) {
			return 0.0;
		}
		return all_rotation_speed[bullet_index];
	}
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

	_ALWAYS_INLINE_ void bump_attachment_epoch(int bullet_index) {
		if (bullet_index >= 0 && bullet_index < (int)attachment_assignment_epochs.size()) {
			++attachment_assignment_epochs[bullet_index];
		}
	}

	_ALWAYS_INLINE_ uint64_t attachment_epoch_for(int bullet_index) const {
		if (bullet_index < 0 || bullet_index >= (int)attachment_assignment_epochs.size()) {
			return 0;
		}
		return attachment_assignment_epochs[bullet_index];
	}

	// Slot guarded across the collision-signal emit on a killing blow: the
	// final disable funnels into disable_multimesh(), whose sweep would pool
	// every attachment BEFORE the signal fires (handler would see nullptr).
	// Set to the dying slot before disable_bullet(); the sweep skips it, the
	// post-signal cleanup disables it normally. -1 = no guard.
	int signal_protected_attachment_slot = -1;

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
	static uint64_t collision_dedup_key(int bullet_index, int64_t instance_id) {
		uint64_t h = 14695981039346656037ull;
		const uint32_t a = (uint32_t)bullet_index;
		for (int i = 0; i < 4; ++i) {
			h ^= (uint64_t)((a >> (i * 8)) & 0xFFu);
			h *= 1099511628211ull;
		}
		const uint64_t b = (uint64_t)instance_id;
		for (int i = 0; i < 8; ++i) {
			h ^= (b >> (i * 8)) & 0xFFull;
			h *= 1099511628211ull;
		}
		return h == 0 ? 1 : h;
	}

	_ALWAYS_INLINE_ bool collision_already_queued(int bullet_index, int64_t entered_instance_id) {
		if (!collision_dedup_by_object || collision_dedup_slots.empty()) {
			return false;
		}
		const uint64_t key = collision_dedup_key(bullet_index, entered_instance_id);
		uint32_t idx = (uint32_t)key & collision_dedup_slot_mask;
		for (uint32_t probe = 0; probe <= collision_dedup_slot_mask; ++probe) {
			const uint64_t slot = collision_dedup_slots[idx];
			if (slot == 0) {
				return false; // empty run: key is absent
			}
			if (slot == key) {
				return true;
			}
			idx = (idx + 1) & collision_dedup_slot_mask;
		}
		return false;
	}

	_ALWAYS_INLINE_ void mark_collision_queued(int bullet_index, int64_t entered_instance_id) {
		if (!collision_dedup_by_object) {
			return;
		}
		// Grow at >= 50% load so probe runs stay short.
		if (collision_dedup_slot_mask == 0 || (collision_dedup_slot_used + 1) * 2 > (uint32_t)collision_dedup_slots.size()) {
			// Power-of-two sizing keeps the (size - 1) mask trick valid.
			size_t new_size = 64;
			while (new_size < (size_t)(collision_dedup_slot_used + 1) * 4) {
				new_size *= 2;
			}
			// Rehash, never drop: assigning a fresh vector here used to
			// forget every pair queued so far in the window (the first 32
			// pairs stopped being deduped the moment the 33rd arrived, so
			// multi-shape targets double-counted again at scale).
			std::vector<uint64_t> old_keys;
			old_keys.swap(collision_dedup_slots);
			collision_dedup_slots.assign(new_size, 0);
			collision_dedup_slot_mask = (uint32_t)(new_size - 1);
			collision_dedup_slot_used = 0;
			for (uint64_t old_key : old_keys) {
				if (old_key == 0) {
					continue;
				}
				uint32_t idx = (uint32_t)old_key & collision_dedup_slot_mask;
				// Terminates: new_size >= (old_used + 1) * 4 keeps load < 50%.
				for (uint32_t probe = 0; probe <= collision_dedup_slot_mask; ++probe) {
					if (collision_dedup_slots[idx] == 0) {
						collision_dedup_slots[idx] = old_key;
						++collision_dedup_slot_used;
						break;
					}
					idx = (idx + 1) & collision_dedup_slot_mask;
				}
			}
		}
		const uint64_t key = collision_dedup_key(bullet_index, entered_instance_id);
		uint32_t idx = (uint32_t)key & collision_dedup_slot_mask;
		for (uint32_t probe = 0; probe <= collision_dedup_slot_mask; ++probe) {
			const uint64_t slot = collision_dedup_slots[idx];
			if (slot == 0) {
				collision_dedup_slots[idx] = key;
				++collision_dedup_slot_used;
				return;
			}
			if (slot == key) {
				return;
			}
			idx = (idx + 1) & collision_dedup_slot_mask;
		}
	}

	void clear_collision_dedup_keys() {
		// A cold window must not pin a huge table from a one-off spike: when
		// almost nothing was queued, drop back to 64 slots. A hot window
		// keeps its capacity so a sustained spike does not regrow+rehash on
		// every drain.
		const size_t table_size = collision_dedup_slots.size();
		if (table_size > 1024 && (size_t)collision_dedup_slot_used * 16 < table_size) {
			collision_dedup_slots.assign(64, 0);
			collision_dedup_slot_mask = 63;
			collision_dedup_slot_used = 0;
			return;
		}
		for (uint64_t &slot : collision_dedup_slots) {
			slot = 0;
		}
		collision_dedup_slot_used = 0;
	}

	// Bound so both dedup modes are reachable and testable. Default true (one
	// logical hit per bullet/target per frame). Set false to restore the legacy
	// per-SHAPE delivery, where a 3-shape target counts three hits.
	bool get_collision_dedup_by_object() const { return collision_dedup_by_object; }
	void set_collision_dedup_by_object(bool value) {
		if (collision_dedup_by_object == value) {
			return;
		}
		collision_dedup_by_object = value;
		// Keys from the old mode must not gate the new one (or vice versa).
		clear_collision_dedup_keys();
	}

	// Table-level introspection for tests (test_volley_dedup_table.gd). These
	// operate on the LIVE dedup table, not a shadow copy, so call them on an
	// idle volley with no overlaps in flight.
	void debug_dedup_reset() { clear_collision_dedup_keys(); }
	bool debug_dedup_probe(int bullet_index, int64_t target_instance_id) {
		return collision_already_queued(bullet_index, target_instance_id);
	}
	void debug_dedup_mark(int bullet_index, int64_t target_instance_id) {
		mark_collision_queued(bullet_index, target_instance_id);
	}
	Dictionary debug_dedup_stats() const {
		Dictionary d;
		d["used"] = (int64_t)collision_dedup_slot_used;
		d["capacity"] = (int64_t)collision_dedup_slots.size();
		d["dedup_by_object"] = collision_dedup_by_object;
		return d;
	}
	// Brute-force key-collision search over deterministic distinct pairs.
	// Returns {collided: bool, probes: int}. A 32-bit key finds a collision
	// within a few hundred thousand probes with near certainty; a 64-bit
	// key will not. Cold path: only tests call this.
	Dictionary debug_dedup_find_collision(int probe_count) const {
		Dictionary d;
		d["collided"] = false;
		d["probes"] = 0;
		if (probe_count <= 1) {
			return d;
		}
		std::vector<uint64_t> keys;
		keys.reserve((size_t)probe_count);
		for (int i = 0; i < probe_count; ++i) {
			keys.push_back(collision_dedup_key(i, (int64_t)1000003 + (int64_t)i * (int64_t)7919));
		}
		std::sort(keys.begin(), keys.end());
		for (size_t i = 1; i < keys.size(); ++i) {
			if (keys[i] == keys[i - 1]) {
				d["collided"] = true;
				d["probes"] = (int64_t)(i + 1);
				return d;
			}
		}
		d["probes"] = (int64_t)probe_count;
		return d;
	}

	// Central transform-invertibility check. get_scale().length_squared()
	// alone accepts singular bases like scale (0,1) (det == 0) whose
	// affine_inverse() is garbage. All conversion/validation paths use this.
	static bool is_transform_invertible_safe(const Transform2D &t) {
		if (!t.is_finite()) {
			return false;
		}
		const Vector2 x = t.columns[0];
		const Vector2 y = t.columns[1];
		if (!x.is_finite() || !y.is_finite()) {
			return false;
		}
		const real_t det = x.x * y.y - x.y * y.x;
		return Math::is_finite(det) && Math::abs(det) > (real_t)1e-8;
	}

	// Per-instance shader keys applied via set_instance_shader_parameter().
	// Clearing the C++ Dictionary does NOT clear the CanvasItem overrides,
	// so pooled reuse (A with params -> B without) would leak A's visuals.
	// finalize_set_up() resets every previously applied key on the empty path.
	std::vector<String> applied_instance_shader_keys;

	void clear_applied_instance_shader_overrides() {
		for (const String &key : applied_instance_shader_keys) {
			if (!key.is_empty()) {
				set_instance_shader_parameter(key, Variant());
			}
		}
		applied_instance_shader_keys.clear();
	}

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
		// short, e.g. a shard list rebuilt elsewhere).
		_ALWAYS_INLINE_ Ref<MultiMesh> shard_multimesh(int shard_index) const {
			if (shard_index >= 0 && shard_index < (int)shard_multimeshes.size() && shard_multimeshes[shard_index].is_valid()) {
				return shard_multimeshes[shard_index];
			}
			return shards[shard_index]->get_multimesh();
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
	static FXLayerSnapshot fx_snapshot_layer(const Ref<BulletEffectLayerData2D> &layer) {
		FXLayerSnapshot snap;
		if (layer.is_null() || !layer->enabled) {
			return snap;
		}
		snap.present = true;
		snap.bake_version = layer->get_bake_version();
		snap.trigger = layer->trigger;
		snap.material_id = layer->material.is_valid() ? (uint64_t)layer->material->get_instance_id() : 0;
		snap.self_modulate = layer->self_modulate;
		snap.z_index = layer->z_index;
		snap.z_as_relative = layer->z_as_relative;
		snap.visibility_layer = layer->visibility_layer;
		snap.light_mask = layer->light_mask;
		snap.max_instances = layer->max_instances;
		return snap;
	}
	std::vector<FXLayerSnapshot> fx_seeded_snapshots;
	bool fx_layers_match_seeded(const TypedArray<BulletEffectLayerData2D> &layers) const;
	// Hides every live trail instance and rewinds per-bullet trail state,
	// keeping the shard nodes (pooled-reuse fast path).
	void fx_soft_reset_trail_layers();

	// Hot trail write for one bullet: picks the frame from the volley clock
	// plus the bullet's random phase, composes the follow transform, moves
	// shards when the frame changes. Called from both move_bullets loops
	// after the instance transform is final, and from enable_bullet wakes.
	_ALWAYS_INLINE_ void write_trail_instances(int bullet_index) {
		if (fx_trail_bakes.empty()) {
			return;
		}
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return;
		}
		if (bullet_index >= (int)all_cached_instance_transforms.size()) {
			return;
		}
		if (!all_bullets_enabled_set.contains(bullet_index)) {
			return;
		}
		write_trail_from_global(bullet_index, all_cached_instance_transforms[bullet_index]);
	}

	// Core trail write from an explicit global pose (physics cache above,
	// lerped pose from the interpolation pass below). Frame, shard routing
	// and per-instance ramp sampling live here so both paths stay identical.
	_ALWAYS_INLINE_ void write_trail_from_global(int bullet_index, const Transform2D &bullet_transf) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return;
		}
		if (!bullet_transf.get_origin().is_finite() || !Math::is_finite(bullet_transf.get_rotation())) {
			return;
		}
		for (auto &bake : fx_trail_bakes) {
			if (bake.layer.is_null() || bake.frames.empty() || bake.shards.empty()) {
				continue;
			}
			if (bullet_index >= (int)bake.bullet_on.size() || bullet_index >= (int)bake.phase.size() || bullet_index >= (int)bake.bullet_shard.size()) {
				continue;
			}
			if (!bake.bullet_on[bullet_index]) {
				continue;
			}
			if (!(bake.total > 0.0) || !Math::is_finite(curves_elapsed_time)) {
				continue;
			}
			double age = curves_elapsed_time + bake.phase[bullet_index];
			age = age - Math::floor(age / bake.total) * bake.total;
			// First frame whose end boundary passes age (prefix sums make this
			// O(log F) instead of re-accumulating per bullet per tick).
			int frame = 0;
			if (!bake.frame_starts.empty()) {
				size_t lo = 0;
				size_t hi = bake.frame_starts.size();
				while (lo < hi) {
					const size_t mid = lo + (hi - lo) / 2;
					if (age < bake.frame_starts[mid]) {
						hi = mid;
					} else {
						lo = mid + 1;
					}
				}
				frame = (lo < bake.frame_starts.size()) ? (int)lo : (int)bake.frame_starts.size() - 1;
			}
			if (frame < 0 || frame >= (int)bake.shards.size()) {
				continue;
			}
			Transform2D trail_transf(bullet_transf.get_rotation(), bullet_transf.get_origin() + bake.layer->offset.rotated(bullet_transf.get_rotation()));
			if (bake.layer->scale != Vector2(1, 1) && bake.layer->scale.is_finite()) {
				trail_transf = trail_transf.scaled_local(bake.layer->scale);
			}
			// Static rotation offset plus continuous spin. Spin reads the
			// unwrapped clock (rotation is periodic, so fposmod keeps it
			// precise over long sessions); pooled reuse restarts the clock,
			// hence the spin, with it.
			double extra_rot = 0.0;
			if (Math::is_finite(bake.layer->rotation_degrees)) {
				extra_rot += bake.layer->rotation_degrees * Math::PI / 180.0;
			}
			const double raw_age = curves_elapsed_time + bake.phase[bullet_index];
			if (bake.layer->spin_degrees_per_sec != 0.0 && Math::is_finite(bake.layer->spin_degrees_per_sec) && Math::is_finite(raw_age)) {
				extra_rot += bake.layer->spin_degrees_per_sec * Math::PI / 180.0 * raw_age;
			}
			if (extra_rot != 0.0) {
				trail_transf = trail_transf.rotated_local(Math::fposmod(extra_rot, Math::TAU));
			}
			const Transform2D local = to_local_for_multimesh(trail_transf);
			if (!local.get_origin().is_finite()) {
				continue;
			}
			if (bake.bullet_shard[bullet_index] != frame) {
				const int prev = bake.bullet_shard[bullet_index];
				if (prev >= 0 && prev < (int)bake.shards.size() && bake.shards[prev] != nullptr) {
					bake.shard_multimesh(prev)->set_instance_transform_2d(bullet_index, zero_transform);
					// Leaving shard: hide it now when nobody else rides it
					// (previously it stayed flagged visible until an
					// unrelated hide scan happened to notice).
					if (prev < (int)bake.shard_refcount.size() && bake.shard_refcount[prev] > 0) {
						if (--bake.shard_refcount[prev] == 0 && prev < (int)bake.shard_visible.size()) {
							bake.shard_visible[prev] = 0;
							bake.shards[prev]->set_visible(false);
						}
					}
				}
				bake.bullet_shard[bullet_index] = frame;
				if (frame >= 0 && frame < (int)bake.shard_refcount.size()) {
					++bake.shard_refcount[frame];
				}
			}
			MultiMeshInstance2D *shard = bake.shards[frame];
			if (shard == nullptr) {
				continue;
			}
			bake.shard_multimesh(frame)->set_instance_transform_2d(bullet_index, local);
			if (bullet_index < (int)bake.bullet_trail_transf.size()) {
				bake.bullet_trail_transf[bullet_index] = local;
			}
			// Trail tint: ramp sample when set, plus the layer fade envelope
			// when configured. Trails key the envelope off volley age and
			// remaining lifetime (per-bullet birth is untracked by design):
			// fade-in covers spawn, fade-out the volley end. Infinite
			// lifetimes skip fade-out like the volley tick does. Costs one
			// color write per bullet per tick, so it runs only when a fade
			// is actually configured.
			const bool trail_has_fade = (bake.layer->fade_in_sec > 0.0 || bake.layer->fade_out_sec > 0.0) && Math::is_finite(curves_elapsed_time);
			if (bake.layer->color_ramp.is_valid() || trail_has_fade) {
				Color tint(1, 1, 1, 1);
				if (bake.layer->color_ramp.is_valid()) {
					tint = bake.layer->color_ramp->sample((float)(age / bake.total));
				}
				if (trail_has_fade) {
					double alpha = 1.0;
					if (bake.layer->fade_in_sec > 0.0 && curves_elapsed_time < bake.layer->fade_in_sec) {
						alpha = curves_elapsed_time / bake.layer->fade_in_sec;
					}
					if (!is_life_time_infinite && bake.layer->fade_out_sec > 0.0 && Math::is_finite(current_life_time) && current_life_time < bake.layer->fade_out_sec) {
						const double out_alpha = current_life_time / bake.layer->fade_out_sec;
						if (out_alpha < alpha) {
							alpha = out_alpha;
						}
					}
					tint.a *= (float)Math::clamp(alpha, 0.0, 1.0);
				}
				bake.shard_multimesh(frame)->set_instance_color(bullet_index, tint);
				if (bullet_index < (int)bake.bullet_trail_tint.size()) {
					bake.bullet_trail_tint[bullet_index] = tint;
				}
			}
			if (frame >= (int)bake.shard_visible.size() || !bake.shard_visible[frame]) {
				if (frame < (int)bake.shard_visible.size()) {
					bake.shard_visible[frame] = 1;
				}
				shard->set_visible(true);
			}
		}
	}

	// Hides one bullet across every trail shard (disable path). Retires a
	// shard's visibility when nothing tracks it anymore, so a fully
	// hidden trail costs zero draw calls (empty shards must not stay
	// visible). The next write re-shows it.
	_ALWAYS_INLINE_ void hide_trail_instances(int bullet_index) {
		if (fx_trail_bakes.empty() || bullet_index < 0 || bullet_index >= amount_bullets) {
			return;
		}
		for (auto &bake : fx_trail_bakes) {
			if (bullet_index < 0 || bullet_index >= (int)bake.bullet_shard.size()) {
				continue;
			}
			const int prev = bake.bullet_shard[bullet_index];
			if (prev >= 0 && prev < (int)bake.shards.size() && bake.shards[prev] != nullptr) {
				bake.shard_multimesh(prev)->set_instance_transform_2d(bullet_index, zero_transform);
			}
			bake.bullet_shard[bullet_index] = -1;
			// O(1) hide decision via the live-bullet count (same semantics as
			// the old full-slot scan: hide exactly when nobody rides it).
			if (prev >= 0 && prev < (int)bake.shard_refcount.size() && bake.shard_refcount[prev] > 0) {
				if (--bake.shard_refcount[prev] == 0 && prev < (int)bake.shard_visible.size()) {
					bake.shard_visible[prev] = 0;
					if (prev < (int)bake.shards.size() && bake.shards[prev] != nullptr) {
						bake.shards[prev]->set_visible(false);
					}
				}
			}
		}
	}

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

	// Whether the directional_life_time_over signal will be emitted when the life time of the bullets is over. Tracked by BulletFactory2D
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
	// amount_bullets at spawn; the directional homing epochs stay separate
	// (they guard deferred homing work, this guards the collision drain).
	std::vector<uint64_t> bullet_collision_epochs;

	_ALWAYS_INLINE_ uint64_t collision_epoch_for_bullet(int bullet_index) const {
		if (bullet_index < 0 || bullet_index >= (int)bullet_collision_epochs.size()) {
			return 0;
		}
		return bullet_collision_epochs[bullet_index];
	}

	_ALWAYS_INLINE_ void bump_collision_epoch_for_bullet(int bullet_index) {
		if (bullet_index < 0 || bullet_index >= (int)bullet_collision_epochs.size()) {
			return;
		}
		++bullet_collision_epochs[bullet_index];
	}

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
	_ALWAYS_INLINE_ bool ensure_indexes_match_amount_bullets_range(int &bullet_index_start, int &bullet_index_end_inclusive, const char *function_name) const {
		// Unspawned / empty volley: an empty range, not a user error.
		if (amount_bullets <= 0) {
			bullet_index_start = 0;
			bullet_index_end_inclusive = -1;
			return false;
		}
		if (bullet_index_end_inclusive == -1) {
			bullet_index_end_inclusive = amount_bullets - 1;
		}
		if (bullet_index_start < 0 || bullet_index_start >= amount_bullets || bullet_index_end_inclusive < 0 || bullet_index_end_inclusive >= amount_bullets) {
			UtilityFunctions::push_error(String("Invalid index range in ") + function_name + " (" + String::num_int64(bullet_index_start) + ".." + String::num_int64(bullet_index_end_inclusive) + " outside 0.." + String::num_int64(amount_bullets - 1) + "; use -1 as the end for \"through the last bullet\"). Nothing was applied.");
			bullet_index_start = 0;
			bullet_index_end_inclusive = -1;
			return false;
		}
		if (bullet_index_start > bullet_index_end_inclusive) {
			UtilityFunctions::push_error(String("Invalid index range in ") + function_name + " (start > end). Nothing was applied.");
			bullet_index_start = 0;
			bullet_index_end_inclusive = -1;
			return false;
		}
		return true;
	}

	// Resolve Ref<Shape2D> once via casting into typed cache. Single error per spawn/enable, then quiet.
	// Null/unsupported/invalid => circle r16 (consistent default, cheapest physics).
	_ALWAYS_INLINE_ void cache_collision_shape_typed(const Ref<Shape2D> &shape) {
		cached_collision_shape = shape;
		cached_effective_shape_type = PhysicsServer2D::SHAPE_CIRCLE;
		cached_rect_size = CollisionShapeHelper2D::DEFAULT_RECT_SIZE;
		cached_circle_radius = CollisionShapeHelper2D::DEFAULT_CIRCLE_RADIUS;
		cached_capsule_radius = 8.0f;
		cached_capsule_height = 24.0f;
		if (shape.is_null()) {
			return;
		}
		if (auto *rect = Object::cast_to<RectangleShape2D>(shape.ptr())) {
			Vector2 s = rect->get_size();
			if (s.x <= 0.0f || s.y <= 0.0f) {
				UtilityFunctions::push_error("RectangleShape2D size must be > 0. Falling back to circle r16.");
				return;
			}
			cached_effective_shape_type = PhysicsServer2D::SHAPE_RECTANGLE;
			cached_rect_size = s;
			return;
		}
		if (auto *circle = Object::cast_to<CircleShape2D>(shape.ptr())) {
			float r = circle->get_radius();
			if (r <= 0.0f) {
				UtilityFunctions::push_error("CircleShape2D radius must be > 0. Falling back to circle r16.");
				return;
			}
			cached_effective_shape_type = PhysicsServer2D::SHAPE_CIRCLE;
			cached_circle_radius = r;
			return;
		}
		if (auto *capsule = Object::cast_to<CapsuleShape2D>(shape.ptr())) {
			float r = capsule->get_radius();
			float h = capsule->get_height();
			if (r <= 0.0f || h <= 0.0f) {
				UtilityFunctions::push_error("CapsuleShape2D radius/height must be > 0. Falling back to circle r16.");
				return;
			}
			cached_effective_shape_type = PhysicsServer2D::SHAPE_CAPSULE;
			cached_capsule_radius = r;
			cached_capsule_height = h;
			return;
		}
		UtilityFunctions::push_error("Unsupported collision shape type: " + shape->get_class() + " - only RectangleShape2D/CircleShape2D/CapsuleShape2D supported. Falling back to circle r16.");
	}

	// Sync shape transform from instance transform (shared logic for teleport/set_transform)
	// Matches the movement-tick convention: the instance rotation
	// includes the texture rotation for rendering, but the physics shape must use the
	// logical (un-textured) rotation when shapes follow rotation. With
	// rotate_only_textures=true the shape keeps its previous orientation (origin-only sync).
	_ALWAYS_INLINE_ void sync_shape_transform_from_instance(int bullet_index, const Transform2D &instance_transf) {
		auto &shape_transf = all_cached_shape_transforms[bullet_index];
		auto &shape_origin = all_cached_shape_origin[bullet_index];
		auto &instance_origin = all_cached_instance_origin[bullet_index];
		if (!rotate_only_textures) {
			shape_transf = instance_transf;
			if (cache_texture_rotation_radians != 0.0) {
				shape_transf = shape_transf.rotated_local(-cache_texture_rotation_radians);
			}
		}
		Vector2 rotated_offset = Vector2(0, 0);
		if (cache_collision_shape_offset != Vector2(0, 0)) {
			rotated_offset = cache_collision_shape_offset.rotated(shape_transf.get_rotation());
		}
		shape_origin = instance_origin + rotated_offset;
		shape_transf.set_origin(shape_origin);
		if (physics_server) {
			physics_server->area_set_shape_transform(area, bullet_index, shape_transf);
		}
	}

	// Carry the attachment along a transform edit: stick-relative slots
	// recompute from the new transform, non-stick slots shift by the jump
	// delta (they never heal otherwise). Shared by set_bullet_transform,
	// the texture-rotation setters, and the teleport paths so paused
	// factories never leave attachments behind.
	_ALWAYS_INLINE_ void carry_attachment_with_transform(int bullet_index, const Transform2D &new_transform, const Vector2 &origin_delta) {
		if (bullet_factory == nullptr || bullet_index < 0 || bullet_index >= (int)attachments.size() || attachments[bullet_index] == nullptr) {
			return;
		}
		if (attachment_stick_relative_to_bullet[bullet_index]) {
			attachment_transforms[bullet_index] = calculate_attachment_global_transf(bullet_index, new_transform);
		} else {
			attachment_transforms[bullet_index] = attachment_transforms[bullet_index].translated(origin_delta);
		}
		if (!bullet_factory->use_physics_interpolation) {
			attachments[bullet_index]->set_global_transform(attachment_transforms[bullet_index]);
		}
	}

	//////////////////// CURVES RELATED
	inline void populate_shared_curves_related_data(const Ref<BulletCurvesData2D> &new_curves_data) {
		if (new_curves_data.is_null()) {
			shared_bullet_curves_data.unref();
			return;
		}

		shared_bullet_curves_data = new_curves_data;

		const bool is_movement_curve_valid = shared_bullet_curves_data->movement_speed_curve.is_valid();
		const bool is_rotation_curve_valid = shared_bullet_curves_data->rotation_speed_curve.is_valid();
		const bool is_x_direction_curve_valid = shared_bullet_curves_data->x_direction_curve.is_valid();
		const bool is_y_direction_curve_valid = shared_bullet_curves_data->y_direction_curve.is_valid();

		// Size unconditionally: BulletCurvesData2D is a mutable shared Resource, so a user
		// can gain a rotation curve AFTER this call. move_bullets then reads all_rotation_speed
		// by bullet index whenever the curve is valid - an undersized/empty vector here would
		// become an OOB write in the tick. Reset to 0.0 so no previous owner's speeds leak.
		// All three rotation vectors must match: set_shared_bullet_rotation_data
		// indexes max/accel beside speed, and a curves-seeded resize of speed
		// alone would leave them short -> OOB read on the next shared write.
		if ((int)all_rotation_speed.size() != amount_bullets) {
			all_rotation_speed.assign(amount_bullets, 0.0);
			all_max_rotation_speed.assign(amount_bullets, 0.0);
			all_rotation_acceleration.assign(amount_bullets, 0.0);
		}

		for (int i = 0; i < amount_bullets; ++i) {
			// Skip bullets carrying their own curves: shared channels only
			// cover channels the bullet lacks (same rule as the tick).
			// Without this, spawning with both set would show shared
			// values in get_bullet_speed_data/direction until the first
			// tick corrected them.
			const bool has_own = (i >= 0 && i < (int)all_bullet_curves_data.size() && all_bullet_curves_data[i].is_valid());
			const BulletCurvesData2D *own = has_own ? all_bullet_curves_data[i].ptr() : nullptr;
			if (is_movement_curve_valid && (own == nullptr || !own->movement_speed_curve.is_valid())) {
				all_cached_speed[i] = get_bullet_curves_movement_speed(shared_bullet_curves_data.ptr());
			}

			if (is_rotation_curve_valid && (own == nullptr || !own->rotation_speed_curve.is_valid())) {
				all_rotation_speed[i] = get_bullet_curves_rotation_speed(shared_bullet_curves_data.ptr());
			}

			auto &current_direction = all_cached_direction[i];

			if (is_x_direction_curve_valid && (own == nullptr || !own->x_direction_curve.is_valid())) {
				apply_x_direction_curve(current_direction, shared_bullet_curves_data.ptr());
			}
			if (is_y_direction_curve_valid && (own == nullptr || !own->y_direction_curve.is_valid())) {
				apply_y_direction_curve(current_direction, shared_bullet_curves_data.ptr());
			}

			if (is_movement_curve_valid || is_x_direction_curve_valid || is_y_direction_curve_valid) {
				all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset;
			}
		}
	}

	_ALWAYS_INLINE_ void populate_individual_bullet_curves_related_data(int bullet_index, const Ref<BulletCurvesData2D> &new_curves_data) {
		if (new_curves_data.is_null()) {
			if (bullet_index >= 0 && bullet_index < (int)all_bullet_curves_data.size() && all_bullet_curves_data[bullet_index].is_valid()) {
				all_bullet_curves_data[bullet_index].unref();
			}
			return;
		}

		// Vector is pre-sized to amount_bullets, direct index
		all_bullet_curves_data[bullet_index] = new_curves_data;
		Ref<BulletCurvesData2D> &curr_curves = all_bullet_curves_data[bullet_index];

		const bool is_movement_curve_valid = curr_curves->movement_speed_curve.is_valid();
		const bool is_rotation_curve_valid = curr_curves->rotation_speed_curve.is_valid();
		const bool is_x_direction_curve_valid = curr_curves->x_direction_curve.is_valid();
		const bool is_y_direction_curve_valid = curr_curves->y_direction_curve.is_valid();

		// Size this even when there's no rotation curve yet - someone can add one later and the tick reads it every frame.
		// All three rotation vectors must match (see populate_shared above).
		if ((int)all_rotation_speed.size() != amount_bullets) {
			all_rotation_speed.assign(amount_bullets, 0.0);
			all_max_rotation_speed.assign(amount_bullets, 0.0);
			all_rotation_acceleration.assign(amount_bullets, 0.0);
		}

		if (is_rotation_curve_valid) {
			all_rotation_speed[bullet_index] = get_bullet_curves_rotation_speed(curr_curves.ptr());
		}

		if (is_movement_curve_valid) {
			all_cached_speed[bullet_index] = get_bullet_curves_movement_speed(curr_curves.ptr());
		}

		auto &current_direction = all_cached_direction[bullet_index];

		if (is_x_direction_curve_valid) {
			apply_x_direction_curve(current_direction, curr_curves.ptr());
		}

		if (is_y_direction_curve_valid) {
			apply_y_direction_curve(current_direction, curr_curves.ptr());
		}

		if (is_movement_curve_valid || is_x_direction_curve_valid || is_y_direction_curve_valid) {
			all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * all_cached_speed[bullet_index] + inherited_velocity_offset;
		}
	}

	// Applies the x direction curve offset to the provided direction vector and normalizes it.
	// A degenerate result (Override mode writing 0/near-0 into both axes)
	// keeps the incoming direction: normalizing a ~zero vector would
	// silently stall the bullet at the inherited offset, and the tick's
	// adjust_direction path already guards the same way.
	_ALWAYS_INLINE_ void apply_x_direction_curve(Vector2 &direction_vector, const BulletCurvesData2D *curves_data) const {
		const bool is_x_direction_curve_valid = curves_data != nullptr && curves_data->get_x_direction_curve().is_valid();

		if (!is_x_direction_curve_valid) {
			return;
		}

		const real_t x_dir_offset = get_bullet_curves_x_direction_offset(curves_data);
		const real_t x_direction_curve_strength = curves_data->x_direction_curve_strength;
		auto x_curve_mode = curves_data->x_direction_curve_mode;

		const Vector2 before = direction_vector;
		if (x_curve_mode == DirectionCurveMode::Additive) {
			direction_vector.x += x_dir_offset * x_direction_curve_strength;
		} else {
			direction_vector.x = x_dir_offset * x_direction_curve_strength;
		}

		if (direction_vector.length_squared() < 0.00000001) {
			direction_vector = before;
			return;
		}
		direction_vector = direction_vector.normalized();
	}

	// Applies the y direction curve offset to the provided direction vector and normalizes it.
	// Same degenerate-result guard as the x variant above.
	_ALWAYS_INLINE_ void apply_y_direction_curve(Vector2 &direction_vector, const BulletCurvesData2D *curves_data) const {
		const bool is_y_direction_curve_valid = curves_data != nullptr && curves_data->get_y_direction_curve().is_valid();

		if (!is_y_direction_curve_valid) {
			return;
		}

		const real_t y_dir_offset = get_bullet_curves_y_direction_offset(curves_data);
		const real_t y_direction_curve_strength = curves_data->y_direction_curve_strength;
		auto y_curve_mode = curves_data->y_direction_curve_mode;

		const Vector2 before = direction_vector;
		if (y_curve_mode == DirectionCurveMode::Additive) {
			direction_vector.y += y_dir_offset * y_direction_curve_strength;
		} else {
			direction_vector.y = y_dir_offset * y_direction_curve_strength;
		}

		if (direction_vector.length_squared() < 0.00000001) {
			direction_vector = before;
			return;
		}
		direction_vector = direction_vector.normalized();
	}

	// A NaN baked into a Curve resource would otherwise flow straight into
	// speed/direction caches with no recovery, so fail each sample to 0.
	_ALWAYS_INLINE_ real_t get_bullet_curves_movement_speed(const BulletCurvesData2D *curves_data) const {
		const bool use_unit_curve = curves_data->movement_use_unit_curve && !is_life_time_infinite;

		real_t input_x = curve_get_input_value(use_unit_curve);

		const real_t sampled = curves_data->movement_speed_curve->sample_baked(input_x);
		return Math::is_finite(sampled) ? sampled : 0.0;
	}

	_ALWAYS_INLINE_ real_t get_bullet_curves_rotation_speed(const BulletCurvesData2D *curves_data) const {
		const bool use_unit_curve = curves_data->rotation_use_unit_curve && !is_life_time_infinite;

		real_t input_x = curve_get_input_value(use_unit_curve);

		const real_t sampled = curves_data->rotation_speed_curve->sample_baked(input_x);
		return Math::is_finite(sampled) ? sampled : 0.0;
	}

	_ALWAYS_INLINE_ real_t get_bullet_curves_x_direction_offset(const BulletCurvesData2D *curves_data) const {
		const bool use_unit_curve = curves_data->x_direction_use_unit_curve && !is_life_time_infinite;

		real_t input_x = curve_get_input_value(use_unit_curve);

		const real_t sampled = curves_data->x_direction_curve->sample_baked(input_x);
		return Math::is_finite(sampled) ? sampled : 0.0;
	}

	_ALWAYS_INLINE_ real_t get_bullet_curves_y_direction_offset(const BulletCurvesData2D *curves_data) const {
		const bool use_unit_curve = curves_data->y_direction_use_unit_curve && !is_life_time_infinite;

		real_t input_x = curve_get_input_value(use_unit_curve);

		const real_t sampled = curves_data->y_direction_curve->sample_baked(input_x);
		return Math::is_finite(sampled) ? sampled : 0.0;
	}

	_ALWAYS_INLINE_ void apply_direction_curve_texture_rotation_if_needed(Vector2 &curr_bullet_direction, Transform2D &curr_bullet_transf, double delta, const BulletCurvesData2D *curves_data) const {
		bool should_apply = curves_data->rotate_towards_adjusted_direction && !is_rotation_data_active;

		if (!should_apply) {
			return;
		}

		real_t target = curr_bullet_direction.angle();
		real_t current = curr_bullet_transf.get_rotation();

		real_t diff = Math::fposmod(target - current + static_cast<real_t>(Math::PI), static_cast<real_t>(Math::TAU)) - static_cast<real_t>(Math::PI);
		// Math::abs: a negative rotation speed would make Math::clamp's min > max,
		// collapsing the clamp to a single bound (full-rate rotation, wrong direction).
		real_t step = Math::abs(curves_data->direction_curve_rotation_speed) * (real_t)delta;

		rotate_transform_locally(curr_bullet_transf, Math::clamp(diff, -step, step));
	}

	// Calculates the input x value for curves based on whether unit curve is used or not (basically whether to treat the input as percentages or raw elapsed time)
	_ALWAYS_INLINE_ real_t curve_get_input_value(bool use_unit_curve) const {
		real_t input_x;

		if (use_unit_curve && !is_life_time_infinite) {
			// max_life_time can legitimately be 0 (infinite lifetime flipped off at
			// runtime before any lifetime was configured): 0/0 = NaN would poison the
			// whole tick, so treat the (empty) lifetime as fully elapsed instead.
			if (max_life_time <= 0.0) {
				return 1.0;
			}
			real_t progress = Math::clamp(curves_elapsed_time / max_life_time, 0.0, 1.0);
			input_x = progress;
		} else {
			input_x = curves_elapsed_time;
		}

		return input_x;
	}

	// Borrows raw pointer - valid until next reassignment (no refcount inc). Keep scope transient.
	_ALWAYS_INLINE_ BulletCurvesData2D *find_bullet_curves_data_ptr(int bullet_index) const {
		if (bullet_index < 0 || bullet_index >= (int)all_bullet_curves_data.size()) {
			return nullptr;
		}
		const Ref<BulletCurvesData2D> &r = all_bullet_curves_data[bullet_index];
		if (r.is_null()) {
			return nullptr;
		}
		return r.ptr();
	}

	Ref<BulletCurvesData2D> bullet_get_curves_data(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_curves_data")) {
			return Ref<BulletCurvesData2D>();
		}

		if (bullet_index < 0 || bullet_index >= (int)all_bullet_curves_data.size() || all_bullet_curves_data[bullet_index].is_null()) {
			UtilityFunctions::push_error("Invalid bullet_index at bullet_get_curves_data(). This bullet has no individual curves data, did you mean to access shared_bullet_curves_data?");
			return Ref<BulletCurvesData2D>();
		}

		return all_bullet_curves_data[bullet_index];
	}

	void bullet_set_curves_data(int bullet_index, const Ref<BulletCurvesData2D> &curves_data) {
		if (!validate_bullet_index(bullet_index, "bullet_set_curves_data")) {
			return;
		}

		if (curves_data.is_null()) {
			if (bullet_index >= 0 && bullet_index < (int)all_bullet_curves_data.size()) {
				all_bullet_curves_data[bullet_index].unref();
			}
			return;
		}

		populate_individual_bullet_curves_related_data(bullet_index, curves_data);
	}

	_ALWAYS_INLINE_ void all_bullets_set_curves_data(const Ref<BulletCurvesData2D> &curves_data, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_curves_data");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_curves_data(i, curves_data);
		}
	}

	_ALWAYS_INLINE_ TypedArray<BulletCurvesData2D> all_bullets_get_curves_data(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_curves_data");

		TypedArray<BulletCurvesData2D> arr;

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_get_curves_data(i));
		}

		return arr;
	}

	// Clear one bullet's per-bullet curves back to the shared fallback (or
	// no curves when unset). Mirrors bullet_set_wobble_data(null): the slot
	// stops steering on its own curves and the tick resolves shared again.
	_ALWAYS_INLINE_ void clear_per_bullet_curves_data(int bullet_index) {
		bullet_set_curves_data(bullet_index, Ref<BulletCurvesData2D>());
	}

	// Clear a range of per-bullet curves (default all) back to shared/none.
	_ALWAYS_INLINE_ void all_bullets_clear_curves_data(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_clear_curves_data");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_curves_data(i, Ref<BulletCurvesData2D>());
		}
	}

	// Speed up (or slow down) one bullet for this tick. Negative speed is
	// legal and flies backwards along the heading (velocity = direction *
	// speed). max_speed <= 0 means unlimited; otherwise speed clamps
	// symmetrically to [-max, max] so reverse flight survives instead of
	// snapping to 0 the way the old deceleration floor did.
	_ALWAYS_INLINE_ void bullet_accelerate_speed(int bullet_index, double delta) {
		real_t &curr_bullet_speed = all_cached_speed[bullet_index];
		real_t curr_max_bullet_speed = all_cached_max_speed[bullet_index];

		real_t acceleration = all_cached_acceleration[bullet_index] * delta;
		real_t new_speed = curr_bullet_speed + acceleration;
		// max_speed <= 0 means unlimited (matches max_collision_count = 0 and
		// the resource default of 0): a default-constructed BulletSpeedData2D
		// must fly at constant speed, not freeze after one tick.
		if (curr_max_bullet_speed > 0.0) {
			new_speed = Math::clamp(new_speed, -curr_max_bullet_speed, curr_max_bullet_speed);
		}
		curr_bullet_speed = new_speed;

		all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * curr_bullet_speed + inherited_velocity_offset;
	}

	// Same, but the speed comes straight from this bullet's curve sample
	// (negative samples fly backwards, same as negative speed above).
	_ALWAYS_INLINE_ void bullet_accelerate_speed_using_curve(int bullet_index, double delta, const BulletCurvesData2D *curves_data) {
		real_t &curr_bullet_speed = all_cached_speed[bullet_index];
		curr_bullet_speed = get_bullet_curves_movement_speed(curves_data);

		all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * curr_bullet_speed + inherited_velocity_offset;
	}

	// Spin one bullet faster/slower for this tick, clamped to its max
	_ALWAYS_INLINE_ void bullet_accelerate_rotation_speed(int bullet_index, double delta) {
		real_t &curr_bullet_rotation_speed = all_rotation_speed[bullet_index];
		real_t curr_max_rotation_speed = all_max_rotation_speed[bullet_index];

		// max <= 0 means unlimited (same convention as linear speed).
		// Otherwise clamp symmetrically to [-max, max]: once the speed is
		// already outside the band (e.g. seeded by a curve), acceleration in
		// either direction recovers toward the band instead of freezing.
		real_t acceleration = all_rotation_acceleration[bullet_index] * delta;
		real_t new_speed = curr_bullet_rotation_speed + acceleration;
		if (curr_max_rotation_speed > 0.0) {
			new_speed = Math::clamp(new_speed, -curr_max_rotation_speed, curr_max_rotation_speed);
		}
		curr_bullet_rotation_speed = new_speed;
	}

	// Same as above, but the target speed is sampled from this bullet's curve
	_ALWAYS_INLINE_ void bullet_accelerate_rotation_speed_using_curve(int bullet_index, double delta, const BulletCurvesData2D *curves_data) {
		real_t &curr_bullet_rotation_speed = all_rotation_speed[bullet_index];

		curr_bullet_rotation_speed = get_bullet_curves_rotation_speed(curves_data);
	}

	// Custom rotation function (I am doing this for performance reasons since Godot's rotated_local returns a brand new Transform2D, but I want to modify a reference without making copies)
	_ALWAYS_INLINE_ void rotate_transform_locally(Transform2D &transform, real_t angle) const {
		// Precompute sin and cos of the angle
		const real_t sin_angle = Math::sin(angle);
		const real_t cos_angle = Math::cos(angle);

		// Extract the basis vectors by reference
		Vector2 &x_axis = transform.columns[0];
		Vector2 &y_axis = transform.columns[1];

		// Apply the rotation to the basis vectors
		x_axis = Vector2(
				x_axis.x * cos_angle - x_axis.y * sin_angle,
				x_axis.x * sin_angle + x_axis.y * cos_angle);
		y_axis = Vector2(
				y_axis.x * cos_angle - y_axis.y * sin_angle,
				y_axis.x * sin_angle + y_axis.y * cos_angle);

		// The origin (columns[2]) remains unchanged
	}

	_ALWAYS_INLINE_ BulletAttachment2D *bullet_get_attachment(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_attachment")) {
			return nullptr;
		}
		if (bullet_index >= (int)attachments.size()) {
			return nullptr;
		}
		return attachments[bullet_index];
	}

	// Owner tracking: detaching an attachment always
	// clears both fields together, which is what makes its later PREDELETE a
	// no-op for the slot it left behind.
	static void clear_attachment_owner_fields(BulletAttachment2D *attachment) {
		if (attachment != nullptr) {
			attachment->owner_multimesh_id = 0;
			attachment->owner_bullet_index = -1;
		}
	}

	// Slot liveness: the "is this slot still mine" check runs liveness first:
	// BEFORE the pointer compare: comparing a dangling pointer first would
	// touch freed memory when the id was recycled (memdelete + allocator ABA).
	bool slot_still_holds_attachment(int bullet_index, BulletAttachment2D *expected_attachment, uint64_t expected_attachment_id, uint64_t expected_attachment_epoch) const {
		if (expected_attachment == nullptr || expected_attachment_id == 0) {
			return false;
		}
		if (ObjectDB::get_instance(ObjectID(expected_attachment_id)) != expected_attachment) {
			return false;
		}
		if (bullet_index < 0 || bullet_index >= (int)attachments.size() || attachments[bullet_index] != expected_attachment) {
			return false;
		}
		return attachment_epoch_for(bullet_index) == expected_attachment_epoch;
	}

	// Id-only form for deferred work: the queued request carries NO pointer
	// (a raw Object* in a deferred call's Variant args is converted by the
	// binder BEFORE this body runs, which reads freed memory when a handler
	// freed the attachment in between). The live slot pointer is compared
	// against the ObjectDB resolution of the id - never dereferenced first.
	bool slot_still_holds_attachment_id(int bullet_index, uint64_t expected_attachment_id, uint64_t expected_attachment_epoch) const {
		if (expected_attachment_id == 0 || bullet_index < 0 || bullet_index >= (int)attachments.size()) {
			return false;
		}
		BulletAttachment2D *slot = attachments[bullet_index];
		if (slot == nullptr) {
			return false;
		}
		return slot_still_holds_attachment(bullet_index, slot, expected_attachment_id, expected_attachment_epoch);
	}

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
	static bool is_popped_attachment_from_scene(BulletAttachment2D *candidate, const Ref<PackedScene> &expected_scene, uint32_t expected_pooling_id) {
		if (candidate == nullptr) {
			return false;
		}
		if (expected_scene.is_valid()) {
			return candidate->source_scene == expected_scene ||
					(candidate->source_scene.is_valid() &&
							!candidate->source_scene->get_path().is_empty() && !expected_scene->get_path().is_empty() &&
							candidate->source_scene->get_path() == expected_scene->get_path());
		}
		return candidate->source_scene.is_valid() &&
				BulletAttachmentObjectPool2D::make_pooling_key_for_scene(candidate->source_scene) == expected_pooling_id;
	}

	_ALWAYS_INLINE_ BulletAttachment2D *bullet_set_attachment_to_null(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_set_attachment_to_null")) {
			return nullptr;
		}
		if (bullet_index >= (int)attachments.size()) {
			return nullptr;
		}

		auto &curr_attachment = attachments[bullet_index];
		auto temp = curr_attachment;

		// Detached without being disabled: clear owner tracking so its PREDELETE
		// doesn't try to drop this (now stale) slot.
		clear_attachment_owner_fields(temp);

		curr_attachment = nullptr;
		bump_attachment_epoch(bullet_index);
		return temp;
	}

	// Called by BulletAttachment2D's PREDELETE when an ACTIVE attachment is freed
	// manually: drops the slot only if it still holds this exact pointer. Safe to
	// call on any state (bounds- and identity-checked).
	void _do_drop_attachment_slot_if_matches(int bullet_index, BulletAttachment2D *attachment) {
		if (bullet_index < 0 || bullet_index >= (int)attachments.size()) {
			return;
		}
		if (attachments[bullet_index] == attachment) {
			attachments[bullet_index] = nullptr;
			bump_attachment_epoch(bullet_index);
		}
	}

	_ALWAYS_INLINE_ TypedArray<BulletAttachment2D> all_bullets_get_attachments(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_attachments");

		TypedArray<BulletAttachment2D> arr;

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_get_attachment(i));
		}

		return arr;
	}

	_ALWAYS_INLINE_ TypedArray<BulletAttachment2D> all_bullets_set_attachment_to_null(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_attachment_to_null");

		TypedArray<BulletAttachment2D> arr;

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_set_attachment_to_null(i));
		}

		return arr;
	}

	_ALWAYS_INLINE_ void all_bullets_set_attachment(const Ref<PackedScene> &attachment_scene, const Vector2 &bullet_attachment_offset, bool stick_relative_to_bullet = true, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_attachment");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_attachment(i, attachment_scene, bullet_attachment_offset, stick_relative_to_bullet);
		}
	}

	// Guard-free core of bullet_set_attachment. Used by the public wrapper
	// below and by the spawn-time shared-attachment application, so both share
	// one implementation. Returns false when
	// nothing was attached (error already printed).
	// Rejected while the factory holds its internal busy flag (disable
	// sweeps, reset/free loops): attaching into a sweep would either be
	// wiped by the trailing blank (reset path) or pooled as a live slot
	// (disable path). Defer with call_deferred instead.
	_ALWAYS_INLINE_ bool attach_bullet_attachment_internal(int bullet_index, const Ref<PackedScene> &attachment_scene, const Vector2 &bullet_attachment_offset, bool stick_relative_to_bullet) {
		if (!validate_bullet_index(bullet_index, "bullet_set_attachment")) {
			return false;
		}

		if (!attachment_scene.is_valid()) {
			UtilityFunctions::push_error("Tried to set an invalid attachment scene to bullet index: " + String::num_int64(bullet_index));
			return false;
		}

		if (bullet_factory == nullptr) {
			UtilityFunctions::push_error("bullet_set_attachment: multimesh was never spawned through BulletFactory2D.");
			return false;
		}

		if (bullet_factory->get_is_tearing_down()) {
			UtilityFunctions::push_error("bullet_set_attachment: cannot attach while the factory is tearing down. The scene is being freed.");
			return false;
		}

		if (bullet_factory->get_is_factory_busy()) {
			UtilityFunctions::push_error("bullet_set_attachment: cannot attach while the factory is busy (disable sweep or reset/free in progress). Use call_deferred to attach after it.");
			return false;
		}

		// Re-entrancy guard: the script callbacks below (on_bullet_spawn /
		// on_bullet_enable) run user code; a handler calling bullet_set_attachment on
		// the same index here would recurse unboundedly or leave an orphaned,
		// untracked node when the outer call overwrites the slot. Nested calls reject
		// with this error; defer them with call_deferred instead. Held here (not
		// just in the public wrapper) so spawn/enable-time application is covered too.
		if (_attachment_setup_depth > 0) {
			UtilityFunctions::push_error("bullet_set_attachment: re-entrant call from inside on_bullet_spawn/on_bullet_enable/on_bullet_disable is not allowed. Use call_deferred to replace attachments from those callbacks.");
			return false;
		}
		ReentrancyGuard attachment_setup_guard(_attachment_setup_depth);

		// Pooling is keyed by the scene itself: every loader of the same scene
		// shares one bucket, no ids needed. A remembered (recognized) key pops
		// with zero scene interaction; a miss instantiates, which doubles as
		// the type check, and the key is only remembered after a successful
		// check. The derived key is stored per slot so disable-time push-back
		// returns the instance to the right bucket.
		auto &pool = bullet_factory->bullet_attachments_pool;
		const uint32_t pooling_key = BulletAttachmentObjectPool2D::make_pooling_key_for_scene(attachment_scene);
		const bool key_recognized = pool.is_key_recognized(pooling_key);

		bullet_disable_attachment(bullet_index);

		BulletAttachment2D *attachment_instance = pool.pop(pooling_key);
		bool created_brand_new_instance = false;

		if (attachment_instance != nullptr && !is_popped_attachment_from_scene(attachment_instance, attachment_scene, pooling_key)) {
			// Key collision guard: on mismatch, return it to its own bucket
			// and fall through to a fresh instantiate instead of handing out
			// a foreign node.
			pool.push(attachment_instance, attachment_instance->home_pooling_id != 0 ? attachment_instance->home_pooling_id : pooling_key);
			attachment_instance = nullptr;
		}

		if (!attachment_instance) {
			Node *fresh_inst = attachment_scene->instantiate();
			attachment_instance = Object::cast_to<BulletAttachment2D>(fresh_inst);

			if (!attachment_instance) {
				if (key_recognized) {
					UtilityFunctions::push_error("bullet_set_attachment: scene stopped producing BulletAttachment2D at bullet index: " + String::num_int64(bullet_index));
				} else {
					UtilityFunctions::push_error("Tried to instantiate an attachment scene that is not of type BulletAttachment2D at bullet index: " + String::num_int64(bullet_index));
				}
				if (fresh_inst) {
					fresh_inst->queue_free();
				}
				return false;
			}

			pool.note_key_label(pooling_key, BulletAttachmentObjectPool2D::make_key_label_for_scene(attachment_scene));
			created_brand_new_instance = true;
		}

		// Own the slot immediately: the script callbacks below run user code, and
		// holding the slot keeps re-entrant API reads and the owner-tracking PREDELETE
		// hook consistent from the first moment.
		attachments[bullet_index] = attachment_instance;
		bump_attachment_epoch(bullet_index);

		// Stamp the source scene alongside the pooling key: the key is only a
		// 32-bit hash, so the pop path verifies identity against this exact
		// Ref on every reuse.
		attachment_instance->source_scene = attachment_scene;
		attachment_pooling_ids[bullet_index] = pooling_key;
		attachment_stick_relative_to_bullet[bullet_index] = stick_relative_to_bullet;

		attachment_offsets[bullet_index] = bullet_attachment_offset;

		auto &local_transf = attachment_local_transforms[bullet_index];

		local_transf = Transform2D();
		local_transf.set_origin(bullet_attachment_offset);
		local_transf.set_rotation(0.0);

		auto &global_transf = attachment_transforms[bullet_index];
		// Live pose, not the spawn-time snapshot: cache_texture_transforms is
		// only written at setup, so teleports/shifts/ticks since then would
		// otherwise spawn the attachment at a stale pose (stuck forever when
		// the factory is paused and no tick heals it).
		Transform2D live_pose = Transform2D();
		if (bullet_index >= 0 && bullet_index < (int)all_cached_instance_transforms.size()) {
			live_pose = all_cached_instance_transforms[bullet_index];
		} else if (bullet_index >= 0 && bullet_index < (int)cache_texture_transforms.size()) {
			live_pose = cache_texture_transforms[bullet_index];
		}
		global_transf = calculate_attachment_global_transf(bullet_index, live_pose);

		attachment_instance->set_transform(Transform2D());
		attachment_instance->set_global_transform(global_transf);

		attachment_instance->reset_physics_interpolation(); // Even when using custom interpolation, reset the interpolation state because Godot might try to interpolate.. Fixes a bug where the attachment would appear in the wrong place for a frame

		// Track ownership so a manually freed ACTIVE attachment can drop this slot
		// from its own PREDELETE instead of leaving a dangling pointer here.
		attachment_instance->owner_multimesh_id = get_instance_id();
		attachment_instance->owner_bullet_index = bullet_index;

		// Handle physics interpolation nicely if enabled
		if (bullet_factory->use_physics_interpolation) {
			all_previous_attachment_transf[bullet_index] = attachment_transforms[bullet_index];
		}

		const uint64_t setup_instance_id = attachment_instance->get_instance_id();

		if (created_brand_new_instance) {
			attachment_instance->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF); // I have custom physics interpolation logic, so disable the Godot one
			attachment_instance->call_on_bullet_spawn(); // Call GDScript custom virtual method to ensure the proper state before adding to the scene tree
			bullet_factory->bullet_attachments_container->add_child(attachment_instance);
		} else {
			attachment_instance->call_on_bullet_enable(); // Call GDScript custom virtual method so that it gets enabled properly
		}

		// The callback may have freed the attachment itself (immediate free()); the
		// PREDELETE owner hook already nulled the slot in that case.
		if (ObjectDB::get_instance(ObjectID(setup_instance_id)) == nullptr) {
			return false;
		}

		// The slot was claimed right after instance selection; nothing left to assign.
		return true;
	}

	_ALWAYS_INLINE_ void bullet_set_attachment(int bullet_index, const Ref<PackedScene> &attachment_scene, const Vector2 &bullet_attachment_offset, bool stick_relative_to_bullet = true) {
		if (!validate_bullet_index(bullet_index, "bullet_set_attachment")) {
			return;
		}

		attach_bullet_attachment_internal(bullet_index, attachment_scene, bullet_attachment_offset, stick_relative_to_bullet);
	}

	// Applies the spawn data's shared attachment to every bullet. Called from
	// spawn()/enable_multimesh() so both bullet types behave identically.
	// Stops after the first failure so a bad scene prints one error instead of
	// one per bullet.
	_ALWAYS_INLINE_ void apply_shared_bullet_attachment_from_data(const DirectionalBulletsData2D &data) {
		if (data.shared_bullet_attachment.is_null()) {
			return;
		}
		for (int i = 0; i < amount_bullets; ++i) {
			if (!attach_bullet_attachment_internal(i, data.shared_bullet_attachment, data.shared_bullet_attachment_offset, data.shared_bullet_attachment_stick_relative_to_bullet)) {
				break;
			}
		}
	}

	_ALWAYS_INLINE_ void bullet_free_attachment(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_free_attachment")) {
			return;
		}

		BulletAttachment2D *&attachment_ptr = attachments[bullet_index];

		if (attachment_ptr == nullptr) {
			return;
		}
		auto temp = attachment_ptr;
		attachment_ptr = nullptr;
		bump_attachment_epoch(bullet_index);

		// Being freed outright: clear owner tracking first so its PREDELETE skip
		// path can't race with this deletion.
		clear_attachment_owner_fields(temp);

		// queue_free, never memdelete: this is script-callable, and an
		// immediate delete from inside the attachment's own _process or a
		// signal it emitted would free the node under the caller's feet.
		// The slot is already empty and the owner fields cleared, so the
		// node is fully detached from this volley; hide it so the frame
		// until the flush shows nothing.
		if (!temp->is_queued_for_deletion()) {
			temp->set_visible(false);
			temp->queue_free();
		}
	}

	_ALWAYS_INLINE_ void bullet_disable_attachment(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_disable_attachment")) {
			return;
		}

		BulletAttachment2D *&attachment_ptr = attachments[bullet_index];

		if (attachment_ptr == nullptr) {
			return;
		}

		// Detach FIRST, then run the script callback: on_bullet_disable runs user
		// code, and with the slot still holding the pointer a handler that re-enters
		// (bullet_set_attachment / bullet_disable_attachment / disable_bullet on the
		// same index) would recurse infinitely or double-pool the attachment. After
		// the null, any re-entrant call on this slot sees a clean, empty slot.
		BulletAttachment2D *detaching = attachment_ptr;
		attachment_ptr = nullptr;
		bump_attachment_epoch(bullet_index);

		// Owner tracking cleared before the callback: if the handler frees the
		// attachment itself, its PREDELETE hook then finds nothing left to do.
		clear_attachment_owner_fields(detaching);

		const uint64_t detaching_id = detaching->get_instance_id();
		detaching->call_on_bullet_disable();

		// The callback may have freed the attachment itself (immediate free()).
		// Everything below touches detaching, so revalidate first; the slot is
		// already null, so there is nothing left to clean up when it is gone.
		if (ObjectDB::get_instance(ObjectID(detaching_id)) == nullptr) {
			return;
		}

		if (bullet_factory == nullptr) {
			UtilityFunctions::push_error("bullet_disable_attachment: multimesh was never spawned through BulletFactory2D.");
			return;
		}

		if (is_attachments_auto_pooling_enabled) {
			bullet_factory->bullet_attachments_pool.push(detaching, attachment_pooling_ids[bullet_index]);
		} else {
			// If the user has selected to not use auto pooling,
			// he most likely expects for the attachments to get freed by themselves when necessary
			// so do that, otherwise the scene will be spammed with hundreds of disabled attachments that never get freed (and user might not even notice this)

			detaching->queue_free();
		}
	}

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
	// Cold path: defined in multimesh_bullets2d.cpp.
	// Batched, id-only deferred release of the attachments held by slots that
	// expired this tick. `requests` packs (bullet_index, attachment_id, epoch)
	// triples; only slots that HELD an attachment at expiry are queued, so a
	// 10k-bullet volley death without attachments queues nothing.
	void _do_deferred_bullet_disable_attachments(int expected_generation, const PackedInt64Array &requests);

	// Generation-guarded deferred life_time_over emit (see schedule site in
	// reduce_lifetime): drops stale emissions when the instance was pooled
	// and re-enabled for a new owner before the flush. The emitter is
	// re-resolved by id so a freed factory/spawner also drops cleanly.
	// Cold path: defined in multimesh_bullets2d.cpp.
	void _do_emit_life_time_over(int expected_generation, uint64_t emitter_instance_id, const StringName &signal_name, const TypedArray<int> &bullet_indexes);

	// Generation-guarded deferred sprite_animation_finished emit: a restart
	// or pool reuse before the flush must not emit for the wrong life. The
	// anim_finished re-check covers restart-in-place (no generation change).
	// Cold path: defined in multimesh_bullets2d.cpp.
	void _do_emit_sprite_animation_finished(int expected_generation);

	_ALWAYS_INLINE_ void bullet_enable_attachment(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_enable_attachment")) {
			return;
		}

		BulletAttachment2D *&attachment_ptr = attachments[bullet_index];

		if (attachment_ptr != nullptr) {
			attachment_ptr->call_on_bullet_enable();
		}
	}

	// Drops user connections to sprite_animation_finished: pooled instances carry them
	// across owners. Used by enable_multimesh AND the enable_bullet wake path (a wake
	// re-activates a pooled instance outside the pool pop, so it needs the same cleanup).
	void disconnect_sprite_animation_connections() {
		for (const Dictionary &connection : get_signal_connection_list("sprite_animation_finished")) {
			const Callable callable = connection["callable"];
			disconnect("sprite_animation_finished", callable);
		}
	}

	// Called when all bullets have been disabled. Clears per-frame state, then
	// hands this instance back to the multimesh pool (unless the user opted out).
	// Pooling is NOT just a memory optimization: pooled instances MUST return here
	// with zero live bullets, zero pending hits, zero timers and detached homing,
	// or the next pop() inherits stale state (phantom hits, leaked counters).
	// The curve clock rewinds together with the lifetime remaining below: unit
	// curves sample curves_elapsed_time/max_life_time, so rewinding one without
	// the other would pin curves at their end sample after a wake. Manual
	// full-disable + wake therefore resumes both clocks from zero remaining,
	// matching enable_multimesh (expiry wakes top up only when expired).
	_ALWAYS_INLINE_ void disable_multimesh() {
		// Re-entrancy guard: the sweep below fires user script callbacks
		// (attachment on_bullet_disable), and a handler calling factory.reset() /
		// free_* there would force_delete this multimesh mid-sweep (use-after-free).
		// Holding the factory's busy flag makes those paths reject with the standard
		// busy error until the sweep and the pool push are done.
		const bool saved_factory_busy = bullet_factory != nullptr ? bullet_factory->get_is_factory_busy() : false;
		if (bullet_factory != nullptr) {
			bullet_factory->_set_internal_operation_busy(true);
		}

		_disable_multimesh_internal();

		if (bullet_factory != nullptr) {
			bullet_factory->_set_internal_operation_busy(saved_factory_busy);
		}
	}

	_ALWAYS_INLINE_ void _disable_multimesh_internal() {
		// Pre-sweep drain markers: every drain goes through disable_bullet()
		// first, so no live bits should remain; clear anyway so a future direct
		// call can't pool an instance whose sparse set claims live bullets at
		// counter 0. Cleared BEFORE the sweep so a wake during the sweep
		// (is_active/counter set by enable_bullet) aborts below instead of
		// being buried.
		active_bullets_counter = 0;
		is_active = false;
		all_bullets_enabled_set.clear();
		// Both clocks rewind together (see disable_multimesh): unit curves
		// sample elapsed/max, so a split rewind pins curves at 1.0 while
		// lifetime still ticks.
		curves_elapsed_time = 0.0;
		if (!is_life_time_infinite) {
			current_life_time = 0.0;
		}
		// Deferred attachment disables can be dropped by a generation bump, so the
		// pool push below must never inherit live slots: sweep every survivor now
		// (auto-pool returns them to the pool, otherwise they are queue_freed).
		// The sweep runs user script callbacks (on_bullet_disable), and a handler
		// may wake a bullet again via enable_bullet(). The wake sets is_active and
		// bumps the counter/generations, so the trailing steps below (which belong
		// to the dying life) must not run over the fresh life - abort instead.
		// A held expiry keeps every slot for its deferred handler: the
		// releases are queued behind the signal (see reduce_lifetime).
		const bool hold_for_lifetime_flush = lifetime_flush_pending;
		for (int i = 0; i < (int)attachments.size() && !hold_for_lifetime_flush; ++i) {
			// The collision killing-blow path guards one slot across the
			// signal emit: its attachment must survive the sweep so the
			// handler can inspect/detach it. Post-signal cleanup disables it.
			if (i == signal_protected_attachment_slot) {
				continue;
			}
			if (attachments[i] != nullptr) {
				bullet_disable_attachment(i);
			}
			if (is_active || active_bullets_counter > 0) {
				return;
			}
		}
		// One reset owns the rest of the clean-disabled state (owner 0
		// for pool neutrality, curves/patterns/motion state, attachment blanks,
		// collided hits, timers, clocks, animation cursor). No generation bump
		// and no connection scrub: the dying life's deferred emits must still
		// flush, and same-owner wakes keep their connections.
		// Pooling flags are PRESERVED here (not reset): reset_transient_volley_state
		// defaults them for new lives, but this is a dying life — the owner's
		// explicit "don't pool" must survive to the gate below, otherwise a
		// pooling-off volley would resurrect pooling mid-sweep and pool itself.
		const bool saved_auto_pool = is_multimesh_auto_pooling_enabled;
		const bool saved_auto_pool_attachments = is_attachments_auto_pooling_enabled;
		// A collision killing blow on the LAST live bullet funnels here with
		// its slot guarded (signal_protected_attachment_slot): the sweep above
		// skipped it, and the reset must keep it too, otherwise the handler of
		// the volley's final bullet sees a null attachment while every earlier
		// bullet's handler saw its own (the post-signal path releases it, and a
		// re-entrant pool pop sweeps it via the new life's reset).
		const bool keep_slots_for_signal = hold_for_lifetime_flush || signal_protected_attachment_slot >= 0;
		reset_transient_volley_state(0, false, keep_slots_for_signal);
		is_multimesh_auto_pooling_enabled = saved_auto_pool;
		is_attachments_auto_pooling_enabled = saved_auto_pool_attachments;

		on_volley_deactivated();

		deactivate_volley();

		if (!is_multimesh_auto_pooling_enabled) {
			return;
		}

		// Held expiry: _do_finish_lifetime_hold() pools after the flush.
		if (hold_for_lifetime_flush) {
			return;
		}

		// A script callback inside the sweep can legally wake a bullet again
		// (enable_bullet). Pooling an instance with live bullets would hand an ACTIVE
		// multimesh to the next pop() as if it were disabled - only pool when the
		// instance is truly drained.
		if (bullets_pool != nullptr && active_bullets_counter == 0) {
			bullets_pool->push(this, get_pool_key());
		}
	}

	void enable_bullet(int bullet_index, int collision_amount = 0, bool should_enable_attachment = true);

	// Clearly-named alias: wake_bullet() revives ONE pooled or
	// manually-disabled slot with its current appearance/ballistics intact
	// (same-owner re-enable). Cross-owner reuse must go through
	// spawn_*()/enable_multimesh() which reseed appearance, custom data,
	// speeds and patterns. Calling wake on a foreign pooled volley warns (see
	// enable_bullet) instead of silently driving stale state.
	void wake_bullet(int bullet_index, int collision_amount = 0, bool should_enable_attachment = true) {
		enable_bullet(bullet_index, collision_amount, should_enable_attachment);
	}

	// Stability introspection for tests and support (bound below).
	// Keys: amount_bullets, active_bullets, generation, owner_spawner_id,
	// is_active, is_pooled, pool_amount, pool_shape, auto_pool_multimesh,
	// auto_pool_attachments, self_modulate.
	Dictionary debug_get_volley_info() const;
	// Attached timer count (0 = no per-tick timer cost). For tests asserting
	// the 64-timer cap and detach-during-fire behavior.
	int debug_get_timer_count() const { return (int)multimesh_custom_timers.size(); }
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
	void debug_run_interpolation_pass() { interpolate_bullet_visuals(); }

	// Disables a single bullet: removes it from the live set, hides the visual,
	// disables its physics shape, and (unless told otherwise) returns its
	// attachment to the attachment pool. When the last bullet goes out, the
	// whole instance is pooled via disable_multimesh() below.
	// A wake does NOT restore the attachment: re-attach explicitly (or via
	// the shared spawn-data attachment on the next enable). Kept simple on
	// purpose — silently re-popping a pooled slot here could hand a foreign
	// scene's node to a volley whose pooling id changed since.
	void disable_bullet(int bullet_index, bool should_disable_attachment = true);
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
	_ALWAYS_INLINE_ Object *resolve_signal_emitter() const {
		if (owner_spawner_id != 0) {
			return ObjectDB::get_instance(ObjectID(owner_spawner_id));
		}
		return bullet_factory;
	}

	void handle_bullet_collision(CollisionType collision_type, int bullet_index, int64_t entered_instance_id, uint64_t queued_bullet_epoch, Vector2 queued_target_velocity, bool queued_velocity_valid, Vector2 queued_target_position, bool queue_position_valid);

	/// COLLISION DETECTION METHODS

	// Queue-time target velocity for the bounce math (see the record
	// above): same read as the drain path (RigidBody2D linear_velocity,
	// CharacterBody2D velocity, AnimatableBody2D constant_linear_velocity),
	// false when the target exposes none or is already gone (the drain
	// then falls back to its live read).
	static bool read_queued_target_velocity(int64_t entered_instance_id, Vector2 &out_velocity) {
		// Cached once (same rationale as the bounce drain below).
		const StringName &prop_linear_velocity = CachedStringNames2D::get().linear_velocity;
		const StringName &prop_velocity = CachedStringNames2D::get().velocity;
		const StringName &prop_constant_linear_velocity = CachedStringNames2D::get().constant_linear_velocity;
		out_velocity = Vector2(0, 0);
		Object *hit_target = ObjectDB::get_instance(entered_instance_id);
		if (hit_target == nullptr) {
			return false;
		}
		const Variant linear_v = hit_target->get(prop_linear_velocity);
		if (linear_v.get_type() == Variant::VECTOR2) {
			const Vector2 v = (Vector2)linear_v;
			if (v.is_finite()) {
				out_velocity = v;
				return true;
			}
			return false;
		}
		const Variant vel_v = hit_target->get(prop_velocity);
		if (vel_v.get_type() == Variant::VECTOR2) {
			const Vector2 v = (Vector2)vel_v;
			if (v.is_finite()) {
				out_velocity = v;
				return true;
			}
			return false;
		}
		// AnimatableBody2D platforms expose neither of the above: their
		// motion lives in constant_linear_velocity (with sync_to_physics).
		// Without this a ramming crusher reads as standing still.
		const Variant const_v = hit_target->get(prop_constant_linear_velocity);
		if (const_v.get_type() == Variant::VECTOR2) {
			const Vector2 v = (Vector2)const_v;
			if (v.is_finite()) {
				out_velocity = v;
				return true;
			}
		}
		return false;
	}

	// Queue-time target pose for the velocity-less estimate. Unconditional
	// (cheap: one cast + one position read); the drain uses it only when
	// no velocity property exists.
	static void read_queued_target_pose(int64_t entered_instance_id, Vector2 &out_position, bool &out_valid) {
		out_position = Vector2(0, 0);
		out_valid = false;
		Object *hit_target = ObjectDB::get_instance(entered_instance_id);
		if (hit_target == nullptr) {
			return;
		}
		Node2D *target_n2d = Object::cast_to<Node2D>(hit_target);
		if (target_n2d == nullptr) {
			return;
		}
		const Vector2 pos = target_n2d->get_global_position();
		if (!pos.is_finite()) {
			return;
		}
		out_position = pos;
		out_valid = true;
	}

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

	_ALWAYS_INLINE_ void park_paused_overlap(PhysicsServer2D::AreaBodyStatus status, int64_t target_id, int bullet_index, CollisionType type) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return;
		}
		for (size_t i = 0; i < paused_overlaps.size(); ++i) {
			PausedOverlap2D &p = paused_overlaps[i];
			if (p.bullet_index == bullet_index && p.target_id == target_id && p.type == type) {
				if (status == PhysicsServer2D::AREA_BODY_REMOVED) {
					p = paused_overlaps.back();
					paused_overlaps.pop_back();
				}
				return;
			}
		}
		if (status == PhysicsServer2D::AREA_BODY_ADDED && paused_overlaps.size() < kMaxPausedOverlaps) {
			PausedOverlap2D p;
			p.bullet_index = bullet_index;
			p.target_id = target_id;
			p.type = type;
			p.epoch = collision_epoch_for_bullet(bullet_index);
			paused_overlaps.push_back(p);
		}
	}

	// Called by the factory when processing resumes: queue every parked
	// overlap whose bullet is still the same live bullet, through the normal
	// dedup + record path. Returns the number of records queued.
	int replay_paused_overlaps() {
		int queued = 0;
		std::vector<PausedOverlap2D> pending;
		pending.swap(paused_overlaps);
		for (const PausedOverlap2D &p : pending) {
			if (p.bullet_index < 0 || p.bullet_index >= amount_bullets || !all_bullets_enabled_set.contains(p.bullet_index)) {
				continue;
			}
			if (p.epoch != collision_epoch_for_bullet(p.bullet_index)) {
				continue;
			}
			if (collision_dedup_by_object) {
				if (collision_already_queued(p.bullet_index, p.target_id)) {
					continue;
				}
				mark_collision_queued(p.bullet_index, p.target_id);
			}
			BulletCollisionData2D record(p.bullet_index, p.target_id, p.type);
			record.queue_bullet_epoch = p.epoch;
			if (wants_queued_target_motion()) {
				record.queue_target_velocity_valid = read_queued_target_velocity(p.target_id, record.queue_target_velocity);
				read_queued_target_pose(p.target_id, record.queue_target_position, record.queue_target_position_valid);
			}
			all_collided_bullets.push_back(record);
			++queued;
		}
		return queued;
	}

	_ALWAYS_INLINE_ void area_entered_func(PhysicsServer2D::AreaBodyStatus status, RID entered_rid, int64_t entered_instance_id, int entered_shape_index, int bullet_shape_index) {
		(void)entered_rid;
		(void)entered_shape_index;
		// Paused factory stops draining (no _physics_process) but the physics
		// server keeps firing: park (bounded) instead of queueing.
		if (bullet_factory != nullptr && bullet_factory->is_bullet_processing_paused()) {
			park_paused_overlap(status, entered_instance_id, bullet_shape_index, CollisionType::AREA);
			return;
		}
		if (status == PhysicsServer2D::AREA_BODY_ADDED) {
			if (bullet_shape_index < 0 || bullet_shape_index >= amount_bullets) {
				return;
			}
			// Object-level dedup: a multi-shape target (or body+area pair on
			// one node) queues one record per shape for a single overlap.
			// Default collapses to one logical hit per (bullet, target) per
			// drain window; shape-level opt-out preserves legacy behavior.
			if (collision_dedup_by_object) {
				// O(1) hash lookup instead of a linear scan over every queued
				// record: at 10k bullets the scan was ~50M comparisons per frame.
				if (collision_already_queued(bullet_shape_index, entered_instance_id)) {
					return;
				}
				mark_collision_queued(bullet_shape_index, entered_instance_id);
			}
			BulletCollisionData2D record(bullet_shape_index, entered_instance_id, CollisionType::AREA);
			record.queue_bullet_epoch = collision_epoch_for_bullet(bullet_shape_index);
			// Target motion only feeds the bounce math: skip the property
			// lookups (up to 3 Object::get per record, which can run user
			// _get) on volleys that never bounce.
			if (wants_queued_target_motion()) {
				record.queue_target_velocity_valid = read_queued_target_velocity(entered_instance_id, record.queue_target_velocity);
				read_queued_target_pose(entered_instance_id, record.queue_target_position, record.queue_target_position_valid);
			}
			all_collided_bullets.push_back(record);
		}
	}
	_ALWAYS_INLINE_ void body_entered_func(PhysicsServer2D::AreaBodyStatus status, RID entered_rid, int64_t entered_instance_id, int entered_shape_index, int bullet_shape_index) {
		(void)entered_rid;
		(void)entered_shape_index;
		if (bullet_factory != nullptr && bullet_factory->is_bullet_processing_paused()) {
			park_paused_overlap(status, entered_instance_id, bullet_shape_index, CollisionType::BODY);
			return;
		}
		if (status == PhysicsServer2D::AREA_BODY_ADDED) {
			if (bullet_shape_index < 0 || bullet_shape_index >= amount_bullets) {
				return;
			}
			// Same object-level dedup as the area path above.
			if (collision_dedup_by_object) {
				if (collision_already_queued(bullet_shape_index, entered_instance_id)) {
					return;
				}
				mark_collision_queued(bullet_shape_index, entered_instance_id);
			}
			BulletCollisionData2D record(bullet_shape_index, entered_instance_id, CollisionType::BODY);
			record.queue_bullet_epoch = collision_epoch_for_bullet(bullet_shape_index);
			// Target motion only feeds the bounce math: skip the property
			// lookups (up to 3 Object::get per record, which can run user
			// _get) on volleys that never bounce.
			if (wants_queued_target_motion()) {
				record.queue_target_velocity_valid = read_queued_target_velocity(entered_instance_id, record.queue_target_velocity);
				read_queued_target_pose(entered_instance_id, record.queue_target_position, record.queue_target_position_valid);
			}
			all_collided_bullets.push_back(record);
		}
	}

	// Moves a single bullet attachment. Indices come from the tick loop, but
	// every vector here is indexed bare - a desync would be an OOB write per
	// tick, so validate sizes instead of trusting the reset invariant.
	// A null factory can only happen on a not-yet-spawned instance driven
	// through debug helpers; without the guard the interpolation check below
	// would null-deref.
	_ALWAYS_INLINE_ void move_bullet_attachment(const Vector2 &translate_by, int bullet_index) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return;
		}
		if (bullet_index >= (int)attachments.size() || bullet_index >= (int)attachment_stick_relative_to_bullet.size() || bullet_index >= (int)attachment_transforms.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
			return;
		}
		auto &curr_attachment = attachments[bullet_index];

		if (!curr_attachment) {
			return;
		}

		Transform2D new_attachment_transf;
		if (attachment_stick_relative_to_bullet[bullet_index]) {
			const Transform2D &bullet_global_transf = all_cached_instance_transforms[bullet_index];
			new_attachment_transf = calculate_attachment_global_transf(bullet_index, bullet_global_transf);
		} else {
			new_attachment_transf = attachment_transforms[bullet_index];
			new_attachment_transf = new_attachment_transf.translated(translate_by);
		}

		// Store the new transform as the current one
		attachment_transforms[bullet_index] = new_attachment_transf;

		// Apply immediately only if not using interpolation
		if (bullet_factory == nullptr || !bullet_factory->use_physics_interpolation) {
			curr_attachment->set_global_transform(new_attachment_transf);
		}
	}

	// Calculates the global transform of the bullet attachment. Note that this function relies on bullet_attachment_local_transform being set already
	_ALWAYS_INLINE_ Transform2D calculate_attachment_global_transf(int bullet_index, const Transform2D &original_data_transf) {
		// If there was additional texture rotation applied, this should not affect the bullet attachments
		if (cache_texture_rotation_radians != 0.0) {
			// So just remove that rotation and then calculate the actual global transform of the bullet attachment
			return original_data_transf.rotated_local(-cache_texture_rotation_radians) * attachment_local_transforms[bullet_index];
		}

		return original_data_transf * attachment_local_transforms[bullet_index];
	}

	bool get_is_life_time_infinite() const { return is_life_time_infinite; }
	void set_is_life_time_infinite(bool value) {
		if (is_life_time_infinite == value) {
			return;
		}
		// A finite lifetime needs a positive max_life_time: with 0 the multimesh dies
		// on the first tick and unit curves divide 0/0 = NaN (see curve_get_input_value).
		if (!value && !(max_life_time > 0.0)) {
			UtilityFunctions::push_error("set_is_life_time_infinite(false) requires a max_life_time > 0, but this multimesh has none (it was spawned with an infinite lifetime). Keep the lifetime infinite or spawn with a finite max_life_time.");
			return;
		}
		is_life_time_infinite = value;
		curves_elapsed_time = 0.0;
		if (!value) {
			current_life_time = max_life_time;
		}
	}

	int get_bullet_max_collision_count() const { return bullet_max_collision_count; }
	void set_bullet_max_collision_count(int value) {
		if (value < 0) {
			UtilityFunctions::push_error("set_bullet_max_collision_count: value must be >= 0 (0 = infinite collisions). Keeping previous value.");
			return;
		}
		bullet_max_collision_count = value;
		// Lowering the max must not leave stored counts above it (count > max
		// would stay observable until the next hit). Clamp live counts down,
		// mirroring set_bullet(s)_collision_count. max == 0 means infinite.
		if (value > 0) {
			for (auto &count : bullets_current_collision_count) {
				if (count >= value) {
					count = value - 1;
				}
				if (count < 0) {
					count = 0;
				}
			}
		}
	}

	// Single-bullet collision counter read. See set_bullet_collision_count for writing.
	int get_bullet_collision_count(int bullet_index) const;

	// Single-bullet collision counter write. Clamped like enable_bullet()'s
	// wake top-up: negatives become 0, values at/above max become max - 1
	// (exactly one hit remaining). Storing the threshold itself would kill on
	// the next hit, unlike an equivalent wake.
	void set_bullet_collision_count(int bullet_index, int value);

	TypedArray<int> get_bullets_current_collision_count() const {
		TypedArray<int> arr;

		for (auto &collision_count : bullets_current_collision_count) {
			arr.push_back(collision_count);
		}

		return arr;
	}

	bool set_bullets_current_collision_count(const TypedArray<int> &arr, bool tile_short_arrays = false) {
		int arr_size = arr.size();

		if (arr_size <= 0) {
			bullets_current_collision_count.clear();
			bullets_current_collision_count.resize(amount_bullets, 0);
			return true;
		}
		if (arr_size != amount_bullets) {
			WarnOnce2D::warn(warn_data_id, 1u, arr_size, amount_bullets, "DirectionalBullets2D: bullets_current_collision_count size (" + String::num_int64(arr_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets start at 0" + String(tile_short_arrays ? " (tiling on: wrapping short array)." : " (check tile_bullets_current_collision_count to wrap, or provide one entry per bullet)."));
		}

		bullets_current_collision_count.clear();
		bullets_current_collision_count.reserve(amount_bullets);

		// Same clamp as enable_bullet()/set_bullet_collision_count(): at/above max
		// leaves exactly one hit remaining (max - 1), never a pinned kill.
		// Strict: slot i reads entry i. With the tile checkbox, wraps (i % size).
		for (int i = 0; i < amount_bullets; ++i) {
			const int src = tile_short_arrays ? (i % arr_size) : i;
			const int collision_count = (src >= 0 && src < arr_size) ? (int)arr[src] : 0;
			if (collision_count < 0) {
				bullets_current_collision_count.push_back(0);
				continue;
			} else if (bullet_max_collision_count > 0 && collision_count >= bullet_max_collision_count) {
				bullets_current_collision_count.push_back(bullet_max_collision_count - 1);
				continue;
			}

			bullets_current_collision_count.push_back(collision_count);
		}

		return true;
	}

	// Void wrapper for the editor property (property setters must return void).
	// Strict indexing (uncovered bullets start at 0); the data resource
	// threads its own tile box through the spawn path instead.
	void set_bullets_current_collision_count_no_return(const TypedArray<int> &arr) {
		(void)set_bullets_current_collision_count(arr);
	}

	// Single owner of the clean-disabled state. Clears ALL transient
	// volley state (owner stamp, curves/patterns, motion ballistics via
	// reset_motion_feature_state, attachment slots, collided hits, timers, clocks, baked
	// animation) so pooled reuse can never inherit a previous owner's state.
	// drop_stale_work additionally bumps the generation and scrubs volley-wide
	// signal connections: pass true when starting a NEW life (enable), false
	// when the volley is dying but its deferred emits must still flush
	// (disable) or when same-owner wakes must keep their connections.
	// keep_attachment_slots: the lifetime hold (see lifetime_flush_pending)
	// keeps slots alive for the deferred handler; everything else resets.
	void reset_transient_volley_state(uint64_t new_owner_spawner_id, bool drop_stale_work, bool keep_attachment_slots = false);

	// Deferred tail of a held expiry (queued after the signal and the
	// per-slot attachment releases): releases any slot still held, then
	// pools the volley exactly like a normal full disable would have.
	void _do_finish_lifetime_hold(int expected_generation);

	// Ends a lifetime hold early (wake before the flush): releases held
	// attachment slots so the new life starts with blank slots.
	void release_lifetime_hold_attachments();

	// Shared deactivation tail: enabled set, counter, active flag, visibility,
	// physics shapes, animation cursor. No pool decision here - the caller
	// (disable path, failed enable) decides what happens next.
	void deactivate_volley();
	///
protected:
	// Internal setup helpers (spawn/enable seed shared spawn data through
	// set_rotation_data). Not bound to scripts.
	// Reserves enough memory and populates all needed data structures keeping track of rotation data
	void set_rotation_data(const TypedArray<BulletRotationData2D> &rotation_data, bool new_rotate_only_textures, bool tile_short_arrays = false);

	// Guarantees no attachment slot survives into a new owner: force-disables any
	// live slot and blanks all five attachment arrays plus the interpolation cache.
	// Used by spawn() and enable_multimesh() so pooled reuse can't inherit stale
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
	void mark_shape_data_applied() {
		shape_data_applied = true;
		applied_shape_type = cached_effective_shape_type;
		applied_rect_size = cached_rect_size;
		applied_circle_radius = cached_circle_radius;
		applied_capsule_radius = cached_capsule_radius;
		applied_capsule_height = cached_capsule_height;
	}

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

	void set_up_bullet_instances(const DirectionalBulletsData2D &data);

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
	/// COLLISION-SHAPE DEBUGGER ACCESSORS (read by MultiMeshBulletsDebugger2D once per volley per tick)

	PhysicsServer2D::ShapeType get_collision_shape_type_for_debugging() const {
		return cached_effective_shape_type;
	}

	const Vector2 get_collision_shape_size_for_debugging() const {
		// Full size from typed cache so math is exact per shape. No cast per tick.
		switch (cached_effective_shape_type) {
			case PhysicsServer2D::SHAPE_CIRCLE:
				return Vector2(cached_circle_radius * 2.0f, cached_circle_radius * 2.0f);
			case PhysicsServer2D::SHAPE_CAPSULE:
				return Vector2(cached_capsule_radius * 2.0f, cached_capsule_height);
			case PhysicsServer2D::SHAPE_RECTANGLE:
			default:
				return cached_rect_size;
		}
	}

	const std::vector<Transform2D> &get_all_collision_shape_transforms_for_debugging() const {
		return all_cached_shape_transforms;
	}

	bool get_skip_debugging() const {
		// NEVER skip: the debugger always inspects multimesh shapes, including pooled
		// (inactive) instances - their frozen cached shape transforms keep rendering.
		// The debugger's null-provider guard still protects against dangling entries.
		return false;
	}

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
		bool _execute_only_if_multimesh_is_active;
		// Unique per attach, never reused within a life. The deferred execute
		// carries it so a detach landing between the queue and the flush
		// cancels that exact request. The volley-wide timers generation
		// cannot do this: it is bumped only by a full detach/wake, so a
		// single detach_time_based_function(callable) used to leave an
		// already-queued callback live and it still fired.
		uint64_t _id = 0;

		CustomTimer(const godot::Callable &callback, double initial_time, bool repeating, bool execute_only_if_multimesh_is_active, uint64_t id) :
				_callback(callback), _current_time(initial_time), _initial_time(initial_time), _repeating(repeating), _execute_only_if_multimesh_is_active(execute_only_if_multimesh_is_active), _id(id) {};
	};

	// Monotonic, never reset: a stale id must not match a later attach.
	uint64_t next_custom_timer_id() { return ++custom_timer_id_counter; }

	// True while any live timer still owns this id.
	bool has_custom_timer_with_id(uint64_t timer_id) const {
		if (timer_id == 0) {
			return false;
		}
		for (const CustomTimer &timer : multimesh_custom_timers) {
			if (timer._id == timer_id) {
				return true;
			}
		}
		return false;
	}

	// A deferred fire that has been queued but not yet flushed. The callable is
	// stored alongside the id because a non-repeating timer is erased from the
	// timer vector on the very tick it fires, so a detach that has to cancel
	// this request can no longer find it there.
	struct PendingCustomTimerFire {
		uint64_t id = 0;
		Callable callback;
	};

	// True while the id is still attached or its fire is still queued.
	bool custom_timer_request_still_valid(uint64_t timer_id) const {
		if (timer_id == 0) {
			return false;
		}
		if (has_custom_timer_with_id(timer_id)) {
			return true;
		}
		for (const PendingCustomTimerFire &pending : pending_custom_timer_fires) {
			if (pending.id == timer_id) {
				return true;
			}
		}
		return false;
	}

	void retire_pending_custom_timer_fire(uint64_t timer_id) {
		for (auto it = pending_custom_timer_fires.begin(); it != pending_custom_timer_fires.end();) {
			if (it->id == timer_id) {
				it = pending_custom_timer_fires.erase(it);
			} else {
				++it;
			}
		}
	}

	// Cancel every queued fire belonging to this callable, plus its timers.
	void cancel_pending_custom_timer_fires_for(const Callable &callback) {
		for (auto it = pending_custom_timer_fires.begin(); it != pending_custom_timer_fires.end();) {
			if (it->callback == callback) {
				it = pending_custom_timer_fires.erase(it);
			} else {
				++it;
			}
		}
	}

	void clear_pending_custom_timer_fires() { pending_custom_timer_fires.clear(); }

	void execute_stored_callable_safely(const Callable &_callback, bool execute_only_if_multimesh_is_active, uint64_t timer_id) {
		// Stamp the timers generation: between this deferred queue and its execution the
		// multimesh can be disabled, pooled and re-enabled for a NEW owner - the stale
		// owner's callback must not fire then (execute_only_if_multimesh_is_active alone
		// can't catch it, since the new owner is active too). The per-timer id
		// additionally catches a single detach, which does NOT bump the
		// generation.
		call_deferred(CachedStringNames2D::get().m_do_execute_stored_callable_safely, _callback, execute_only_if_multimesh_is_active, multimesh_timers_generation, timer_id);
	}

	void _do_execute_stored_callable_safely(const Callable &_callback, bool execute_only_if_multimesh_is_active, int expected_timers_generation, uint64_t expected_timer_id) {
		// Retire the id on every exit path: whether we run, bail on generation,
		// bail on detach, or bail on a dead callable, this request is consumed
		// and must not stay valid for a later flush.
		if (expected_timers_generation != multimesh_timers_generation) {
			retire_pending_custom_timer_fire(expected_timer_id);
			return;
		}
		// The timer this request came from must still be valid. Without this,
		// detaching a callable between the queue and this flush left the
		// callback live: the generation is only bumped by a full detach/wake,
		// so a targeted multimesh_detach_time_based_function() could not stop
		// an already-scheduled fire.
		if (!custom_timer_request_still_valid(expected_timer_id)) {
			return;
		}
		retire_pending_custom_timer_fire(expected_timer_id);

		// If the user wants to execute the callable only if the multimesh is active, check for that
		if (execute_only_if_multimesh_is_active && !is_active) {
			return;
		}

		// The bound object can be freed between attach and fire; calling an invalid
		// callable would spam engine errors every repeat.
		if (!_callback.is_valid()) {
			return;
		}

		_callback.call();
	}

	_ALWAYS_INLINE_ void multimesh_attach_time_based_function(double time, const Callable &callable, bool repeat = false, bool execute_only_if_multimesh_is_active = true) {
		// Stamp the timers generation so a full-disable (which detaches directly)
		// landing before this deferred call can't leak the timer into the next owner.
		// Outside physics processing the timer applies immediately (no frame of
		// delay); inside a physics frame it defers, since
		// run_multimesh_custom_timers() may be iterating the vector.
		if (Engine::get_singleton()->is_in_physics_frame()) {
			call_deferred(CachedStringNames2D::get().m_do_attach_time_based_function, time, callable, repeat, execute_only_if_multimesh_is_active, multimesh_timers_generation);
			return;
		}
		_do_attach_time_based_function(time, callable, repeat, execute_only_if_multimesh_is_active, multimesh_timers_generation);
	}

	// Deferred implementation of multimesh_attach_time_based_function above.
	// Advanced: calling directly runs synchronously, which is only safe
	// outside physics processing (the timer vector may be iterated then).
	_ALWAYS_INLINE_ void _do_attach_time_based_function(double time, const Callable &callable, bool repeat, bool execute_only_if_multimesh_is_active, int expected_timers_generation) {
		if (expected_timers_generation != multimesh_timers_generation) {
			return;
		}
		// Direct script calls to this _do_* impl bypass the phase check in the
		// public wrapper. run_multimesh_custom_timers() may be iterating the
		// vector right now (factory holds the iterating flag during the timer
		// sweep).
		if (bullet_factory != nullptr && bullet_factory->is_bullets_iterating()) {
			UtilityFunctions::push_error("Cannot modify attached timers while bullets are being processed (e.g. inside a timer callback or collision handler). Use multimesh_attach_time_based_function() instead of the _do_* implementation.");
			return;
		}
		if (time <= 0.0) {
			UtilityFunctions::push_error("When calling multimesh_attach_time_based_function(), you need to provide a time value that is above 0");
			return;
		}

		if (!callable.is_valid()) {
			UtilityFunctions::push_error("Invalid callable was passed to multimesh_attach_time_based_function()");
			return;
		}

		// Uncapped user attaches would grow memory and per-tick iteration cost
		// without bound (an attach-per-tick script degrades every future tick).
		if (multimesh_custom_timers.size() >= 64) {
			UtilityFunctions::push_error("multimesh_attach_time_based_function: timer limit (64 per multimesh) reached, detach some first.");
			return;
		}

		multimesh_custom_timers.emplace_back(callable, time, repeat, execute_only_if_multimesh_is_active, next_custom_timer_id());
	}

	_ALWAYS_INLINE_ void multimesh_detach_time_based_function(const Callable &callable) {
		// Stamp the generation so a full-disable landing before this deferred
		// call can't erase the next owner's timers. Immediate outside physics
		// processing, deferred within it (same rationale as attach above).
		if (Engine::get_singleton()->is_in_physics_frame()) {
			call_deferred(CachedStringNames2D::get().m_do_detach_time_based_function, callable, multimesh_timers_generation);
			return;
		}
		_do_detach_time_based_function(callable, multimesh_timers_generation);
	}

	// Deferred implementation of multimesh_detach_time_based_function above.
	// Advanced: calling directly runs synchronously, which is only safe
	// outside physics processing (the timer vector may be iterated then).
	_ALWAYS_INLINE_ void _do_detach_time_based_function(const Callable &callable, int expected_timers_generation) {
		if (expected_timers_generation != multimesh_timers_generation) {
			return;
		}
		if (bullet_factory != nullptr && bullet_factory->is_bullets_iterating()) {
			UtilityFunctions::push_error("Cannot modify attached timers while bullets are being processed (e.g. inside a timer callback or collision handler). Use multimesh_detach_time_based_function() instead of the _do_* implementation.");
			return;
		}
		// Cancel queued fires for this callable FIRST and unconditionally. A
		// one-shot is erased from the vector on the tick it fires, so by the
		// time a detach runs its fire may no longer be in the vector at all -
		// matching inside the loop below would miss it and the detached
		// callable would still run once. That is the bug the per-timer id
		// exists to prevent.
		cancel_pending_custom_timer_fires_for(callable);
		for (auto it = multimesh_custom_timers.begin(); it != multimesh_custom_timers.end();) {
			if (it->_callback == callable) {
				it = multimesh_custom_timers.erase(it); // Order-preserving
			} else {
				++it;
			}
		}
	}

	_ALWAYS_INLINE_ void multimesh_detach_all_time_based_functions() {
		// Immediate outside physics processing, deferred within it (same
		// rationale as attach above).
		if (Engine::get_singleton()->is_in_physics_frame()) {
			call_deferred(CachedStringNames2D::get().m_do_detach_all_time_based_functions, multimesh_timers_generation);
			return;
		}
		_do_detach_all_time_based_functions(multimesh_timers_generation);
	}

	// Deferred implementation of multimesh_detach_all_time_based_functions above.
	// Advanced: calling directly runs synchronously, which is only safe
	// outside physics processing (the timer vector may be iterated then).
	// NOTE: no is_bullets_iterating() guard here on purpose: the internal
	// disable path (_disable_multimesh_internal) must clear timers even when
	// it runs inside the physics sweep. Direct script calls during iteration
	// are still unsafe - use the public wrapper instead.
	_ALWAYS_INLINE_ void _do_detach_all_time_based_functions(int expected_timers_generation) {
		if (expected_timers_generation != multimesh_timers_generation) {
			return;
		}
		++multimesh_timers_generation;
		multimesh_custom_timers.clear();
	}

	_ALWAYS_INLINE_ void run_multimesh_custom_timers(double delta) {
		if (!Math::is_finite(delta) || delta < 0.0) {
			return;
		}
		for (auto it = multimesh_custom_timers.begin(); it != multimesh_custom_timers.end();) {
			it->_current_time -= delta;
			if (it->_current_time <= 0.0) {
				// Record BEFORE the (deferred) call: a one-shot is erased from
				// the vector on this same line, so the flush could not find it
				// there. Bounded by the 64-timer cap.
				PendingCustomTimerFire pending;
				pending.id = it->_id;
				pending.callback = it->_callback;
				pending_custom_timer_fires.push_back(pending);
				execute_stored_callable_safely(it->_callback, it->_execute_only_if_multimesh_is_active, it->_id);

				if (it->_repeating) {
					// Carry the overshoot so the average period stays exact
					// (resetting dropped up to one tick per period: a 0.1s
					// timer at 60 Hz fired every 7 ticks). Anti-spiral: a
					// period shorter than the tick fires once per tick and
					// resyncs instead of banking an ever-growing debt.
					it->_current_time += it->_initial_time;
					if (it->_current_time <= 0.0) {
						it->_current_time = it->_initial_time;
					}
					++it;
				} else {
					it = multimesh_custom_timers.erase(it);
				}
			} else {
				++it;
			}
		}
	}

	// Stores a bunch of timers for the multimesh that should execute
	std::vector<CustomTimer> multimesh_custom_timers;
	// Source of the per-timer ids above. Deliberately never reset: reusing an
	// id would let a stale deferred request match a brand new timer.
	uint64_t custom_timer_id_counter = 0;
	// Ids of one-shot fires whose deferred request is still in flight. Bounded
	// by the 64-timer cap; each entry is retired when its flush runs.
	std::vector<PendingCustomTimerFire> pending_custom_timer_fires;

public:
	// Clears both homing deques through the pop loop so the global
	// mouse-target counter stays exact: force_delete() memdeletes without
	// running disable logic, and a leaked counter would query the mouse every
	// homing tick forever. Pop paths never touch the tree, so this is safe.
	~DirectionalBullets2D();

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

	// Stamp so homing callbacks scheduled by a dead volley no-op instead of firing into the new owner.
	// The volley-wide generation is bumped on every spawn/enable/disable: a
	// deferred call scheduled by a previous life carries a stale generation
	// and no-ops instead of eating the new life's targets or emitting ghost
	// signals. The per-bullet epoch below covers the single-bullet path the
	// volley generation can't: reach -> disable_bullet(i) -> enable_bullet(i)
	// -> push-new-target(i) before the flush. Without it the stale deferred
	// pop/emit (same volley generation) would eat the fresh front target and
	// fire a ghost reached signal for the dead life's target.
	uint64_t homing_operation_generation = 0;
	std::vector<uint64_t> bullet_homing_epochs;

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
	void refresh_gravity_active() {
		gravity_active = false;
		for (const Vector2 &g : all_gravity) {
			if (g.length_squared() > 0.0) {
				gravity_active = true;
				break;
			}
		}
	}

	// HOMING GATING (seeded from spawn data; editable live below).
	// delay = straight-flight seconds before steering starts; duration =
	// seconds of steering before it stops (0 = infinite); lose_range =
	// steering pauses beyond this distance from the target (0 = unlimited).
	real_t homing_delay_sec = 0.0;
	real_t homing_duration_sec = 0.0;
	real_t homing_lose_range_px = 0.0;

	// BOUNCE / RICOCHET (seeded from DirectionalBulletsData2D; editable live
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
	int bounce_mode = 0; // DirectionalBulletsData2D::BounceMode
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
	void ensure_bounce_vectors() {
		if (!bounce_enabled()) {
			all_bounce_count.clear();
			all_bounce_cooldown.clear();
			all_bounce_last_tick.clear();
			all_bounce_last_target.clear();
			all_bounce_last_time.clear();
			all_bounce_last_normal.clear();
			all_bounce_last_target_velocity.clear();
			bounce_visual_pending.clear();
			bounce_visual_target.clear();
			all_bounce_speed_multiplier.clear();
			bounce_speed_scaled = false;
			return;
		}
		if ((int)all_bounce_count.size() != amount_bullets) {
			all_bounce_count.assign(amount_bullets, 0);
		}
		if ((int)all_bounce_cooldown.size() != amount_bullets) {
			all_bounce_cooldown.assign(amount_bullets, 0.0);
		}
		if ((int)all_bounce_last_tick.size() != amount_bullets) {
			all_bounce_last_tick.assign(amount_bullets, 0);
		}
		if ((int)all_bounce_last_target.size() != amount_bullets) {
			all_bounce_last_target.assign(amount_bullets, 0);
		}
		if ((int)all_bounce_last_time.size() != amount_bullets) {
			all_bounce_last_time.assign(amount_bullets, 0.0);
		}
		if ((int)all_bounce_last_normal.size() != amount_bullets) {
			all_bounce_last_normal.assign(amount_bullets, Vector2(0, 0));
		}
		if ((int)all_bounce_last_target_velocity.size() != amount_bullets) {
			all_bounce_last_target_velocity.assign(amount_bullets, Vector2(0, 0));
		}
		if ((int)bounce_visual_pending.size() != amount_bullets) {
			bounce_visual_pending.assign(amount_bullets, 0);
		}
		if ((int)bounce_visual_target.size() != amount_bullets) {
			bounce_visual_target.assign(amount_bullets, Vector2(1, 0));
		}
		if ((int)all_bounce_speed_multiplier.size() != amount_bullets) {
			all_bounce_speed_multiplier.assign(amount_bullets, 1.0);
		}
	}

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
	_ALWAYS_INLINE_ bool advance_movement_pattern(const Ref<Curve2D> &curve, bool face_movement_direction, bool repeat_pattern, real_t &distance_traveled, Vector2 &velocity_delta, Vector2 &curr_bullet_direction, Transform2D &curr_bullet_transf, real_t known_len = -1.0) {
		const real_t len = (known_len >= 0.001) ? known_len : curve->get_baked_length();
		// NaN baked length (corrupt Curve2D points) fails the < comparison, so
		// check finiteness explicitly: without it fmod/sample_baked propagate
		// NaN into velocity/origin and brick the volley permanently.
		if (!Math::is_finite(len) || len < 0.001) {
			return false;
		}
		const real_t prev_dist = distance_traveled;
		const real_t advance_dist = velocity_delta.length();
		distance_traveled += advance_dist;
		// Non-repeating patterns stop exactly at the end: clamp into the
		// final tile instead of wrapping once past it and snapping back.
		const real_t clamped_dist = (!repeat_pattern && distance_traveled >= len) ? len : distance_traveled;
		const real_t s1 = Math::fmod(prev_dist, len);
		const real_t s2 = Math::fmod(clamped_dist, len);
		const int64_t l1 = (int64_t)(prev_dist / len);
		const int64_t l2 = (int64_t)(clamped_dist / len);
		// p2 - p1 = (l2 - l1) * disp + sample(s2) - sample(s1): the curve
		// start cancels, and the lap displacement only matters on the tick a
		// lap boundary is crossed. Two sample_baked calls per bullet per tick
		// instead of four.
		Vector2 local_delta = curve->sample_baked(s2) - curve->sample_baked(s1);
		if (l2 != l1) {
			const Vector2 disp = curve->sample_baked(len * 0.9999) - curve->sample_baked(0.0);
			local_delta += (real_t)(l2 - l1) * disp;
		}
		// Reuse advance_dist (== |velocity_delta|, unchanged since entry) instead
		// of a second length() sqrt per bullet per tick.
		if (advance_dist > 0.0001 && local_delta.length_squared() > 0.00000001) {
			Vector2 pattern_direction = local_delta.rotated(curr_bullet_direction.angle()).normalized();
			velocity_delta = pattern_direction * advance_dist;
		}
		if (face_movement_direction && velocity_delta.length_squared() > 0.0001) {
			const Vector2 tangent = velocity_delta.normalized();
			// Preserve scale like set_bullet_texture_rotation_towards_position does.
			const Vector2 pattern_scale = curr_bullet_transf.get_scale();
			curr_bullet_transf.set_rotation_and_scale(tangent.angle(), pattern_scale);
		}
		if (!repeat_pattern && distance_traveled >= len) {
			if (face_movement_direction) {
				const Vector2 logical_dir = curr_bullet_direction.normalized();
				const Vector2 pattern_scale = curr_bullet_transf.get_scale();
				curr_bullet_transf.set_rotation_and_scale(logical_dir.angle(), pattern_scale);
			}
			return false;
		}
		return true;
	}

	// Updates all bullets' positions, rotations, and homing.
	// Never inlined into the caller: the factory calls this once per volley,
	// and inlining the whole per-bullet loop into BulletFactory2D::tick_volleys
	// measured 20-25% slower on trails_fx_2k (register pressure in the loop).
	_NO_INLINE_ void move_bullets(double delta) {
		if (amount_bullets <= 0 || physics_server == nullptr || !area.is_valid()) {
			return;
		}
		if (!Math::is_finite(delta) || delta < 0.0) {
			return;
		}
		// Bounce bookkeeping runs only while armed; plain volleys skip it.
		if (bounce_enabled()) {
			++bounce_tick_counter;
			bounce_last_delta = delta;
		}
		const bool is_using_physics_interpolation = bullet_factory != nullptr && bullet_factory->use_physics_interpolation;
		if (is_using_physics_interpolation) {
			update_all_previous_transforms_for_interpolation();
		}

		bool homing_interval_reached = false;

		// Unified precedence: per-bullet deque wins for its bullet. The shared
	// deque is the broadcast fallback, used only when the bullet's own
	// deque is empty. Either deque steering the bullet marks homing active.
	bool shared_homing_deque_enabled = !shared_homing_deque.empty();
		const bool is_per_bullet_homing_enabled = (active_homing_count > 0);

		// If homing is enabled (either shared or per-bullet) update the timer and cache mouse position if needed
		// The mouse query is hoisted here (once per tick, not once per push):
		// get_global_mouse_position() walks to the viewport each call, and a
		// volley with per-bullet mouse pushes would otherwise pay it N times.
		if (shared_homing_deque_enabled || is_per_bullet_homing_enabled) {
			// Update homing timer / how often to update the homing target position
			homing_interval_reached = update_homing_timer(delta);

			// In case we have the mouse as a homing target, make sure to cache its global position
			if (homing_interval_reached && HomingTargetDeque::mouse_homing_targets_amount > 0) {
				cached_mouse_global_position = get_global_mouse_position();
			}
		}

		// Since shared homing deque is used for all bullets, do this once
		if (shared_homing_deque_enabled) {
			// Delete any invalid (freed) targets
			auto targets_amount = shared_homing_deque.get_homing_targets_amount();
			int trimmed = shared_homing_deque.bullet_homing_trim_front_invalid_targets(cached_mouse_global_position, targets_amount);
			if (trimmed > 0) {
				// Front changed: route the orbit lock per policy (a new front
				// target never inherits a stale center) and give every bullet
				// a clean reached slate for the new target.
				orbit_route_shared_front_change();
			}
			shared_homing_deque_enabled = (targets_amount - trimmed) > 0;

			// If timer timed out, refresh the cached global position of the front target
			if (shared_homing_deque_enabled && homing_interval_reached) {
				shared_homing_deque.refresh_cached_front_target_global_position(cached_mouse_global_position);
			}
		}

		// While the shared deque holds targets, per-bullet caches go stale
		// (their branch never runs). Refresh them on the interval too, so a
		// shared drain hands over live positions instead of push-time ones.
		if (shared_homing_deque_enabled && homing_interval_reached && active_homing_count > 0) {
			for (size_t qi = 0; qi < all_bullet_homing_targets.size() && qi < all_homing_count.size(); ++qi) {
				if (all_homing_count[qi] > 0 && !all_bullet_homing_targets[qi].empty()) {
					all_bullet_homing_targets[qi].refresh_cached_front_target_global_position(cached_mouse_global_position);
				}
			}
		}

		const bool is_orbiting_feature_enabled = (active_orbiting_count > 0);

		Vector2 homing_bullet_pos;
		Vector2 homing_target_pos;

		const bool shared_curves_data_enabled = shared_bullet_curves_data.is_valid();
		const BulletCurvesData2D *const shared_curves_ptr = shared_bullet_curves_data.ptr();

		const bool shared_curves_x_direction_curve_valid = shared_curves_data_enabled && shared_curves_ptr->x_direction_curve.is_valid();
		const bool shared_curves_y_direction_curve_valid = shared_curves_data_enabled && shared_curves_ptr->y_direction_curve.is_valid();

		const bool shared_curves_rotation_curve_valid = shared_curves_data_enabled && shared_curves_ptr->rotation_speed_curve.is_valid();
		const bool shared_curves_acceleration_curve_valid = shared_curves_data_enabled && shared_curves_ptr->movement_speed_curve.is_valid();

		// Hoist shared curve samples outside loop (same for all bullets in this multimesh)
		real_t shared_x_offset = 0, shared_y_offset = 0;
		real_t shared_x_strength = 0, shared_y_strength = 0;
		DirectionCurveMode shared_x_mode = DirectionCurveMode::Additive, shared_y_mode = DirectionCurveMode::Additive;
		real_t shared_movement_speed_val = 0, shared_rotation_speed_val = 0;
		if (shared_curves_data_enabled) {
			if (shared_curves_x_direction_curve_valid) {
				shared_x_offset = get_bullet_curves_x_direction_offset(shared_curves_ptr);
				shared_x_strength = shared_curves_ptr->x_direction_curve_strength;
				shared_x_mode = shared_curves_ptr->x_direction_curve_mode;
			}
			if (shared_curves_y_direction_curve_valid) {
				shared_y_offset = get_bullet_curves_y_direction_offset(shared_curves_ptr);
				shared_y_strength = shared_curves_ptr->y_direction_curve_strength;
				shared_y_mode = shared_curves_ptr->y_direction_curve_mode;
			}
			if (shared_curves_acceleration_curve_valid) {
				shared_movement_speed_val = get_bullet_curves_movement_speed(shared_curves_ptr);
			}
			if (shared_curves_rotation_curve_valid) {
				shared_rotation_speed_val = get_bullet_curves_rotation_speed(shared_curves_ptr);
			}
		}

		bool is_per_bullet_curves_valid = false;
		const BulletCurvesData2D *per_bullet_curves_data = nullptr;

		// Shared movement pattern curve sampled once (same for all bullets).
		const bool shared_pattern_curve_valid = shared_movement_pattern_curve.is_valid();
		real_t shared_pattern_len = 0.0;
		if (shared_pattern_curve_valid) {
			shared_pattern_len = shared_movement_pattern_curve->get_baked_length();
			// NaN baked length (corrupt Curve2D points) would flow into the
			// per-bullet advance as known_len; normalize to 0 here so the
			// use_shared_pattern gate below stays shut (advance also guards).
			if (!Math::is_finite(shared_pattern_len)) {
				shared_pattern_len = 0.0;
			}
		}

		// Gravity time window sampled once (same for all bullets). Combined
		// with gravity_active below, a volley with no pull skips the gravity
		// block for every bullet.
		const bool gravity_window = gravity_window_open();

		// Locked-orbit snapshot for the pattern gate below: the orbit lock
		// state read per bullet must match what the orbit section below sees, or the pattern
		// gate and the orbit displacement disagree for one frame.
		// Reuses the member scratch (no per-tick allocation when sized).
		std::vector<uint8_t> &orbit_locked_snapshot = orbit_locked_scratch;
		if (is_orbiting_feature_enabled) {
			if ((int)orbit_locked_snapshot.size() != amount_bullets) {
				orbit_locked_snapshot.assign(amount_bullets, 0);
			} else {
				std::fill(orbit_locked_snapshot.begin(), orbit_locked_snapshot.end(), (uint8_t)0);
			}
			for (int li : all_bullets_enabled_set.get_active_indexes()) {
				if (li >= 0 && li < amount_bullets && li < (int)all_orbiting_status.size() && li < (int)all_orbiting_data.size()) {
					if (all_orbiting_status[li] && all_orbiting_data[li].is_locked_orbiting) {
						orbit_locked_snapshot[li] = 1;
					}
				}
			}
		}

		// Shout when homing can't visibly do anything (no steering, no pattern, no spin, no ring) - usually a forgotten take-control flag.
		// changes via rotate_to_target, movement patterns, rotation data, or orbiting).
		// Warn once.
		if (!homing_inert_warning_issued && (shared_homing_deque_enabled || is_per_bullet_homing_enabled) && !homing_take_control_of_texture_rotation && !is_rotation_data_active && active_orbiting_count == 0) {
			bool any_pattern = false;
			for (int pi : all_bullets_enabled_set.get_active_indexes()) {
				if (check_exists_bullet_movement_pattern_data(pi)) {
					any_pattern = true;
					break;
				}
			}
			// The spawn-data shared pattern counts too (per-bullet entries are
			// only half the story now).
			any_pattern = any_pattern || shared_movement_pattern_curve.is_valid();
			if (!any_pattern) {
				UtilityFunctions::push_warning("DirectionalBullets2D has homing targets but homing_take_control_of_texture_rotation is false (and no movement pattern/rotation data), so homing will not steer bullets. Set it to true.");
				homing_inert_warning_issued = true;
			}
		}

		// Loop only through ACTIVE bullets (skip the disabled ones)
		const auto &active_bullet_indexes = all_bullets_enabled_set.get_active_indexes();

		// BulletCurvesData2D is a mutable shared Resource: a user can gain a rotation
		// curve AFTER the multimesh was spawned/enabled, when populate_* had no reason
		// to size all_rotation_speed. Enforce the invariant once per tick so the
		// rotation branches below can never index out of bounds. All three
		// vectors travel together (max/accel are indexed beside speed), so a
		// future path that desyncs one must not leave the others short.
		// resize (not assign): existing speeds survive, only gaps fill with 0.
		if ((int)all_rotation_speed.size() != amount_bullets || (int)all_max_rotation_speed.size() != amount_bullets || (int)all_rotation_acceleration.size() != amount_bullets) {
			all_rotation_speed.resize(amount_bullets, 0.0);
			all_max_rotation_speed.resize(amount_bullets, 0.0);
			all_rotation_acceleration.resize(amount_bullets, 0.0);
		}
		// Same invariant for the shared-deque reached states (H5 fix storage).
		if ((int)all_shared_homing_reached.size() != amount_bullets) {
			all_shared_homing_reached.resize(amount_bullets);
		}

		// Texture-rotation strip for the physics shape, hoisted: rotated_local
		// recomputed the same sin/cos for every bullet every tick.
		const bool strip_texture_rotation = cache_texture_rotation_radians != 0.0;
		const real_t strip_cos = strip_texture_rotation ? (real_t)Math::cos(-cache_texture_rotation_radians) : (real_t)1.0;
		const real_t strip_sin = strip_texture_rotation ? (real_t)Math::sin(-cache_texture_rotation_radians) : (real_t)0.0;
		const bool has_shape_offset = cache_collision_shape_offset != Vector2(0, 0);

		{ // One node inverse for the whole movement loop (trail writes convert per bullet); no user code runs inside.
		NodeInverseScope tick_inverse_scope(this);
		for (int i : active_bullet_indexes) {
			if (i < 0 || i >= amount_bullets) {
				continue;
			}
			// Bounce cooldown ticks down so the bullet can escape the overlap
			// it just bounced out of (a body+area pair on one target would
			// otherwise double-flip it in place). Skipped entirely unless
			// bouncing is armed.
			if (bounce_enabled() && i >= 0 && i < (int)all_bounce_cooldown.size() && all_bounce_cooldown[i] > 0.0) {
				all_bounce_cooldown[i] -= (real_t)delta;
				if (all_bounce_cooldown[i] < 0.0) {
					all_bounce_cooldown[i] = 0.0;
				}
			}
			if (i >= (int)all_cached_instance_transforms.size() || i >= (int)all_cached_direction.size() || i >= (int)all_cached_velocity.size()) {
				continue;
			}
			// The speed arrays should always fit by now, but double-check in the hot loop - a crash here would take the whole game down.
			// but every consumer below indexes it - never trust the invariant in a hot loop.
			if (i >= (int)all_cached_speed.size() || i >= (int)all_cached_max_speed.size() || i >= (int)all_cached_acceleration.size()) {
				continue;
			}
			bool direction_got_updated = false;
			HomingTargetDeque *target_deque_used_for_orbiting = nullptr;

			// 1. STANDARD HOMING PHASE
			// Reached-signal timing runs AFTER steering below (see the reached-signal section):
			// orbit, pattern and curve steering rewrite the velocity, so the
			// reached test must predict with the final velocity_delta, not
			// the pre-steer cached velocity.
			if (is_per_bullet_homing_enabled || shared_homing_deque_enabled) { // Per-bullet deque wins per bullet; shared is the broadcast fallback
				// Whether this bullet has its own targets (non-empty per-bullet deque)
				bool bullet_has_own_targets = false;
				if (i >= 0 && i < (int)all_homing_count.size() && i < (int)all_bullet_homing_targets.size()) {
					bullet_has_own_targets = all_homing_count[i] > 0 && !all_bullet_homing_targets[i].empty();
				}
				if (bullet_has_own_targets) { // Handle per-bullet homing (wins over shared)
					auto &curr_homing_count = all_homing_count[i];

					if (curr_homing_count > 0) {
						auto &curr_homing_deque = all_bullet_homing_targets[i];

						// Drop freed targets off the front
						int trimmed_count = curr_homing_deque.bullet_homing_trim_front_invalid_targets(cached_mouse_global_position, curr_homing_count);

						// Keep the counters honest after trimming so the tick below sees the real queue
						curr_homing_count -= trimmed_count; // this deque lost some
						active_homing_count -= trimmed_count; // ...and so did the volley-wide total
						if (curr_homing_count < 0) {
							curr_homing_count = 0;
						}
						if (active_homing_count < 0) {
							active_homing_count = 0;
						}
						// If the counter somehow runs ahead of the real queue, pull it back - otherwise bullets would home on ghosts.
						const int live_after_trim = curr_homing_deque.get_homing_targets_amount();
						if (curr_homing_count > live_after_trim) {
							active_homing_count -= (curr_homing_count - live_after_trim);
							if (active_homing_count < 0) {
								active_homing_count = 0;
							}
							curr_homing_count = live_after_trim;
						}

						// Trimming exposed a new front target - treat it like a pop so orbit rings re-lock cleanly.
						if (trimmed_count > 0) {
							orbit_route_front_change_for_bullet(i, curr_homing_deque);
						}

						if (curr_homing_count > 0 && !curr_homing_deque.empty()) {
							// Refresh the cached target position on the interval so moving targets don't leave stale positions behind
							if (homing_interval_reached) {
								curr_homing_deque.refresh_cached_front_target_global_position(cached_mouse_global_position);
							}

							update_homing(curr_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
							direction_got_updated = true;
							target_deque_used_for_orbiting = &curr_homing_deque;
						} else if (shared_homing_deque_enabled) {
							// Per-bullet deque drained this tick: fall back to
							// the shared broadcast deque for this bullet.
							update_homing(shared_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
							direction_got_updated = true;
							target_deque_used_for_orbiting = &shared_homing_deque;
						}
					} else if (shared_homing_deque_enabled) {
						update_homing(shared_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
						direction_got_updated = true;
						target_deque_used_for_orbiting = &shared_homing_deque;
					}
				} else if (shared_homing_deque_enabled) { // Shared broadcast fallback (only when the bullet has no own targets)
					update_homing(shared_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
					direction_got_updated = true;
					target_deque_used_for_orbiting = &shared_homing_deque;
				}
			}

			auto &curr_bullet_transf = all_cached_instance_transforms[i];
			auto &curr_bullet_direction = all_cached_direction[i];

			// 2. DIRECTION CURVES - shared sampled once before loop
		// Unified precedence: per-bullet channels win over shared per
		// channel. A bullet with its own x curve uses it even when shared
		// also defines x; shared only covers channels the bullet lacks.
		is_per_bullet_curves_valid = (i >= 0 && i < (int)all_bullet_curves_data.size() && all_bullet_curves_data[i].is_valid());
		per_bullet_curves_data = is_per_bullet_curves_valid ? all_bullet_curves_data[i].ptr() : nullptr; // O(1) vector index, borrows - valid until vector reassigned
		const bool per_bullet_x_curve_valid = is_per_bullet_curves_valid && per_bullet_curves_data->x_direction_curve.is_valid();
		const bool per_bullet_y_curve_valid = is_per_bullet_curves_valid && per_bullet_curves_data->y_direction_curve.is_valid();

		const BulletCurvesData2D *direction_curves_for_texture = nullptr;
		if (per_bullet_x_curve_valid) {
			apply_x_direction_curve(curr_bullet_direction, per_bullet_curves_data);
			direction_curves_for_texture = per_bullet_curves_data;
		} else if (shared_curves_x_direction_curve_valid) {
			const Vector2 before_x = curr_bullet_direction;
			if (shared_x_mode == DirectionCurveMode::Additive) {
				curr_bullet_direction.x += shared_x_offset * shared_x_strength;
			} else {
				curr_bullet_direction.x = shared_x_offset * shared_x_strength;
			}
			// Same degenerate guard as the per-bullet appliers: an Override
			// result near zero keeps the incoming direction instead of
			// stalling the bullet (and snapping its texture to angle 0).
			if (curr_bullet_direction.length_squared() < 0.00000001) {
				curr_bullet_direction = before_x;
			} else {
				curr_bullet_direction = curr_bullet_direction.normalized();
			}
			direction_curves_for_texture = shared_curves_ptr;
		}

		if (per_bullet_y_curve_valid) {
			apply_y_direction_curve(curr_bullet_direction, per_bullet_curves_data);
			if (direction_curves_for_texture == nullptr) {
				direction_curves_for_texture = per_bullet_curves_data;
			}
		} else if (shared_curves_y_direction_curve_valid) {
			const Vector2 before_y = curr_bullet_direction;
			if (shared_y_mode == DirectionCurveMode::Additive) {
				curr_bullet_direction.y += shared_y_offset * shared_y_strength;
			} else {
				curr_bullet_direction.y = shared_y_offset * shared_y_strength;
			}
			if (curr_bullet_direction.length_squared() < 0.00000001) {
				curr_bullet_direction = before_y;
			} else {
				curr_bullet_direction = curr_bullet_direction.normalized();
			}
			direction_curves_for_texture = shared_curves_ptr;
		}

		if (direction_curves_for_texture != nullptr) {
			apply_direction_curve_texture_rotation_if_needed(curr_bullet_direction, curr_bullet_transf, delta, direction_curves_for_texture);
			direction_got_updated = true;
		}

		// 2b. WOBBLE (waveform flight modulation). Lateral displaces the
		// heading perpendicular to flight (snakes, weaves, curtains);
		// angular oscillates the heading itself (corkscrews, petals).
		// Runs on the analytic offset DELTA between frames (not the absolute
		// offset) so pausing/teleporting never jumps the bullet. Disabled
		// entries cost one bool check per bullet; fully disabled volleys
		// skip the loop via the hoisted flag below.
		// Texture follow: with face_movement_direction the visual slews
		// toward the steered heading (same contract as direction curves:
		// skipped while rotation data drives the visual). Angular mode
		// already rotates the heading; the follow keeps the sprite glued
		// to it. Lateral mode steers the heading too, so the same follow
		// points snakes along their path.
		if (is_wobble_feature_enabled && i >= 0 && i < (int)all_bullet_wobble.size() && all_bullet_wobble[i].active) {
			WobbleSeed &w = all_bullet_wobble[i];
			const real_t t = (real_t)curves_elapsed_time;
			bool in_window = t >= w.delay_sec && (w.duration_sec <= 0.0 || t < w.delay_sec + w.duration_sec);
			if (!in_window) {
				// Leaving (or not yet in) the window: the next entry starts
				// from the analytic baseline, exactly as before.
				w.has_last = false;
			}
			if (in_window && w.frequency_hz >= 0.0 && w.amplitude >= 0.0) {
				const real_t phase_base = w.distance_phased && i >= 0 && i < (int)wobble_distance_traveled.size()
						? wobble_distance_traveled[i] * 0.02
						: t;
				const real_t damp = (w.damping_per_sec > 0.0 && t > w.delay_sec) ? (real_t)Math::exp(-(double)(w.damping_per_sec * (t - w.delay_sec))) : 1.0;
				const real_t now_off = w.amplitude * damp * evaluate_wobble_waveform(w.waveform, Math::TAU * w.frequency_hz * phase_base + w.phase);
				const real_t prev_t = t - (real_t)delta;
				const bool prev_in = prev_t >= w.delay_sec && (w.duration_sec <= 0.0 || prev_t < w.delay_sec + w.duration_sec);
				real_t prev_off = 0.0;
				const bool clock_continuous = w.has_last && Math::abs((curves_elapsed_time - delta) - w.last_time) < 1e-6;
				if (clock_continuous) {
					// Exact: the offset actually applied last tick.
					prev_off = w.last_offset;
				} else if (prev_in && delta > 0.0) {
					const real_t prev_base = w.distance_phased && i >= 0 && i < (int)wobble_distance_traveled.size()
							? (wobble_distance_traveled[i] - (curr_bullet_direction * all_cached_speed[i]).length() * (real_t)delta) * 0.02
							: prev_t;
					const real_t prev_damp = (w.damping_per_sec > 0.0 && prev_t > w.delay_sec) ? (real_t)Math::exp(-(double)(w.damping_per_sec * (prev_t - w.delay_sec))) : 1.0;
					prev_off = w.amplitude * prev_damp * evaluate_wobble_waveform(w.waveform, Math::TAU * w.frequency_hz * prev_base + w.phase);
				}
				const real_t frame_delta = now_off - prev_off;
				if (Math::is_finite(now_off)) {
					w.has_last = true;
					w.last_offset = now_off;
					w.last_time = curves_elapsed_time;
				} else {
					w.has_last = false;
				}
				if (Math::is_finite(frame_delta) && Math::abs(frame_delta) > 0.00001) {
					if (w.mode == 1) {
						curr_bullet_direction = curr_bullet_direction.rotated(Math::deg_to_rad(frame_delta));
						if (curr_bullet_direction.length_squared() < 0.00000001) {
							curr_bullet_direction = Vector2(1, 0);
						} else {
							curr_bullet_direction = curr_bullet_direction.normalized();
						}
					} else {
						if (curr_bullet_direction.length_squared() > 0.00000001) {
							const Vector2 perp = Vector2(-curr_bullet_direction.y, curr_bullet_direction.x).normalized();
							const Vector2 steered = curr_bullet_direction + perp * (frame_delta * 0.01);
							// A pathological frame_delta (~100x amplitude)
							// could cancel the heading to zero; hold the old
							// heading instead of snapping to angle 0.
							if (steered.length_squared() > 0.00000001) {
								curr_bullet_direction = steered.normalized();
							}
						}
					}
					direction_got_updated = true;
					// Texture follow (see 2b header): slew the visual toward
					// the steered heading so snakes point along their path.
					// Same skip rule as direction curves: rotation data owns
					// the visual then. Snap when face_rotation_speed <= 0.
					if (w.face_movement_direction && !is_rotation_data_active && delta > 0.0) {
						const real_t target = curr_bullet_direction.angle();
						const real_t current = curr_bullet_transf.get_rotation();
						const real_t diff = Math::fposmod(target - current + static_cast<real_t>(Math::PI), static_cast<real_t>(Math::TAU)) - static_cast<real_t>(Math::PI);
						if (Math::is_finite(diff) && Math::abs(diff) > 0.00001) {
							if (w.face_rotation_speed <= 0.0) {
								rotate_transform_locally(curr_bullet_transf, diff);
							} else {
								const real_t step = Math::abs(w.face_rotation_speed) * (real_t)delta;
								rotate_transform_locally(curr_bullet_transf, Math::clamp(diff, -step, step));
							}
						}
					}
				}
			}
		}

			// 3. ROTATION - per-bullet curve wins per bullet, shared is fallback
			if (is_per_bullet_curves_valid && per_bullet_curves_data->rotation_speed_curve.is_valid()) {
				bullet_accelerate_rotation_speed_using_curve(i, delta, per_bullet_curves_data);
				update_rotation_using_curve(i, delta);
			} else if (shared_curves_rotation_curve_valid) {
				all_rotation_speed[i] = shared_rotation_speed_val;
				update_rotation_using_curve(i, delta);
			} else if (is_rotation_data_active) {
				bullet_accelerate_rotation_speed(i, delta);
				update_rotation(i, delta);
			}

			// 4. ADJUST DIRECTION BASED ON THE NEW ROTATION (OPTIONALLY)
			// Visual-only spin must not leak into ballistics: with
			// rotate_only_textures the instance rotation is rendering-only (the
			// shape path freezes too), so re-deriving direction from it would
			// steer bullets with texture spin. Skipped the same way.
			if (adjust_direction_based_on_rotation && !rotate_only_textures) {
				// columns[0] includes the texture rotation used for rendering;
				// strip it to recover the logical movement direction.
				// Degenerate basis (zero-scale) keeps the last good direction
				// instead of normalizing a ~zero vector into a stall.
				const Vector2 stripped_basis = all_cached_instance_transforms[i].columns[0].rotated(-cache_texture_rotation_radians);
				if (stripped_basis.length_squared() > 0.00000001) {
					curr_bullet_direction = stripped_basis.normalized();
				}
				direction_got_updated = true;
			}

		// 4b. BOUNCE VISUAL PURSUIT (smooth ricochet facing). Ballistics
		// always reflect instantly in the collision drain; with
		// bounce_rotation_smooth > 0 only the sprite slews toward the
		// reflected heading over several ticks. Visual-only: the logical
		// direction is never touched here. Skipped while
		// adjust_direction_based_on_rotation owns ballistics (the visual
		// owns movement there, so it snapped at bounce time instead).
		if (bounce_enabled() && bounce_rotate_texture && bounce_rotation_smooth > 0.0 && !adjust_direction_based_on_rotation && i >= 0 && i < (int)bounce_visual_pending.size() && bounce_visual_pending[i] && i < (int)bounce_visual_target.size()) {
			const Vector2 want = bounce_visual_target[i];
			if (want.length_squared() > 0.00000001 && Math::is_finite((double)bounce_rotation_smooth) && delta > 0.0) {
				rotate_to_target(i, want, bounce_rotation_smooth * (real_t)delta, false);
				const Vector2 fwd = all_cached_instance_transforms[i][0].rotated(-cache_texture_rotation_radians);
				if (fwd.length_squared() > 0.00000001 && fwd.normalized().dot(want.normalized()) > 0.9999) {
					bounce_visual_pending[i] = 0;
				}
			} else {
				bounce_visual_pending[i] = 0;
			}
		}

			// 5. VELOCITY CALCULATION (ONLY IF DIRECTION GOT UPDATED) - use temp to avoid mutating cached velocity
			if (direction_got_updated) {
				all_cached_velocity[i] = curr_bullet_direction * all_cached_speed[i] + inherited_velocity_offset + ((i >= 0 && i < (int)all_gravity_velocity.size()) ? all_gravity_velocity[i] : Vector2(0, 0));
			}
			// Wind-free base: the inherited offset rides along AFTER pattern
			// steering below. advance_movement_pattern measures advance_dist =
			// |velocity_delta|, so leaving the offset in would stretch the
			// pattern step along the pattern direction instead of adding
			// world-space drift. Re-added after the pattern runs.
			Vector2 velocity_delta = (curr_bullet_direction * all_cached_speed[i]) * (real_t)delta;

		// 6. MOVEMENT PATTERNS (RELYING ON CURVES AND PATH2D)
		// Unified precedence: per-bullet entries win per bullet; the shared
		// slot is the broadcast fallback for bullets without their own
		// pattern. A locked orbit owns the displacement: the pattern advance
		// (and its distance ledger) is skipped so a non-repeating pattern
		// cannot finish invisibly while the ring drives the bullet. The gate
		// reads the pre-tick snapshot above so it agrees with the orbit section even
		// when the deque empties mid-tick.
		const bool orbit_locked_this_bullet = is_orbiting_feature_enabled && i >= 0 && i < amount_bullets && i < (int)orbit_locked_snapshot.size() && orbit_locked_snapshot[i] && target_deque_used_for_orbiting != nullptr && !target_deque_used_for_orbiting->empty();
		const bool has_per_bullet_pattern = !orbit_locked_this_bullet && check_exists_bullet_movement_pattern_data(i);
		bool use_shared_pattern = false;
		if (!orbit_locked_this_bullet && !has_per_bullet_pattern) {
			if (shared_pattern_curve_valid && shared_pattern_len >= 0.001 && i >= 0 && i < (int)shared_movement_pattern_distances.size()) {
				use_shared_pattern = shared_movement_pattern_repeat || shared_movement_pattern_distances[i] < shared_pattern_len;
			}
		}
		const bool use_per_bullet_pattern = has_per_bullet_pattern;
		if (use_shared_pattern || use_per_bullet_pattern) {
			if (use_shared_pattern) {
				if (!advance_movement_pattern(shared_movement_pattern_curve, shared_movement_pattern_face_movement_direction, shared_movement_pattern_repeat, shared_movement_pattern_distances[i], velocity_delta, curr_bullet_direction, curr_bullet_transf, shared_pattern_len)) {
					// Park the ledger at the end (degenerate curve or completed
					// run) so finished bullets skip without an extra flag.
					shared_movement_pattern_distances[i] = shared_pattern_len;
				}
			} else {
				auto &pattern = all_movement_pattern_data[i];
				const Ref<Curve2D> &curve = pattern.path_curve;
				if (curve.is_null()) {
					all_movement_pattern_data[i] = BulletMovementPatternData2D();
				} else if (!advance_movement_pattern(curve, pattern.face_movement_direction, pattern.repeat_pattern, pattern.distance_traveled, velocity_delta, curr_bullet_direction, curr_bullet_transf)) {
					all_movement_pattern_data[i] = BulletMovementPatternData2D();
				}
			}
		}
		// World-space wind: added after pattern steering so patterns shape
		// the ballistic step and the offset drifts the result (matches the
		// documented velocity composition direction * speed + offset).
		velocity_delta += inherited_velocity_offset * (real_t)delta;
		// Gravity: per-bullet acceleration inside its time window, scaled by the
		// gravity strength curve when present. Default (zero vectors, zero
		// windows beyond immediate) reproduces straight top-down flight:
		// all_gravity reads zero and the window short-circuits below.
		// Zeroed on every new life and on set_gravity (new regime).
		// Both gates hoist out of the loop (gravity_active, gravity_window):
		// a volley with no pull anywhere skips the block for every bullet.
		if (gravity_active && gravity_window && i >= 0 && i < (int)all_gravity.size() && i < (int)all_gravity_velocity.size()) {
			const Vector2 base_g = all_gravity[i];
			if (base_g.length_squared() > 0.0) {
				const BulletCurvesData2D *grav_shared = shared_bullet_curves_data.is_valid() ? shared_bullet_curves_data.ptr() : nullptr;
				const BulletCurvesData2D *grav_per = (is_per_bullet_curves_valid && per_bullet_curves_data != nullptr) ? per_bullet_curves_data : nullptr;
				const real_t scale = gravity_strength_scale_for_bullet(grav_shared, grav_per);
				const Vector2 g = base_g * scale;
				if (g.is_finite()) {
					Vector2 &gv = all_gravity_velocity[i];
					gv += g * (real_t)delta;
					velocity_delta += gv * (real_t)delta;
				}
			}
		}
		if (is_wobble_feature_enabled && i >= 0 && i < (int)wobble_distance_traveled.size()) {
			wobble_distance_traveled[i] += velocity_delta.length();
		}

			auto &curr_bullet_origin = all_cached_instance_origin[i];

			// 7. ORBITING LOGIC (RELYING ON HOMING TARGETS)
			// A deque that ran dry unlocks the orbit under RelockAlways and
			// RelockOnTargetChange; StayLocked rides out the gap, keeping
			// angle/center/identity so the ring re-pins silently when a target
			// returns. Helpers below keep this policy-aware everywhere.
			const bool orbit_vectors_ready = i >= 0 && i < (int)all_orbiting_data.size() && i < (int)all_orbiting_status.size();
			if (is_orbiting_feature_enabled && (target_deque_used_for_orbiting == nullptr || target_deque_used_for_orbiting->empty())) {
				if (orbit_vectors_ready && all_orbiting_data[i].lock_policy != StayLocked) {
					all_orbiting_data[i].is_locked_orbiting = false;
				}
			}
			if (is_orbiting_feature_enabled && orbit_vectors_ready && all_orbiting_status[i] && target_deque_used_for_orbiting != nullptr && !target_deque_used_for_orbiting->empty()) {
				OrbitingData *const orbiting_data = &all_orbiting_data[i];
				if (orbiting_data != nullptr) {
					// RelockOnTargetChange watches identity, not distance: a new
					// front target (retarget, round-robin, reached-pop) unlocks
					// so the bullet re-acquires the ring around the new target;
					// the same target moving keeps the lock. StayLocked never
					// unlocks here; RelockAlways unlocked via the no-op write
					// and replace paths, not per-frame distance.
					if (orbiting_data->is_locked_orbiting && orbiting_data->lock_policy == RelockOnTargetChange && orbit_should_unlock_for_front_change(*orbiting_data, *target_deque_used_for_orbiting)) {
						orbiting_data->is_locked_orbiting = false;
					}
					// Ring center for this frame: FollowTarget tracks the target,
					// FollowDeadzone pins until the target walks out of the
					// deadzone, Anchored freezes at the lock point. Unlocked
					// bullets always fly toward the live target so they can
					// still reach the ring. A non-finite deque cache (freed
					// Node2D target between trim and tick) holds the bullet
					// still this frame instead of poisoning ballistics.
					if (!homing_target_pos.is_finite()) {
						velocity_delta = Vector2(0, 0);
					} else {
					const Vector2 orbit_center = orbiting_data->is_locked_orbiting ? orbit_effective_center(*orbiting_data, homing_target_pos) : homing_target_pos;
					const Vector2 to_center = curr_bullet_origin - orbit_center;
					const real_t current_dist = to_center.length();
					const bool already_locked = orbiting_data->is_locked_orbiting;

					// Track if we are ACTUALLY doing orbit movement this frame
					bool is_physically_orbiting_this_frame = false;

					// Movement Logic (Locked or Boundary Arrival)
					// DontMove = escort: holds a fixed ring slot (angle set at
					// lock time, never advanced) and translates with the target.
					// Zero-delta ticks and zero-speed bullets hold still: with
					// no time passing (or no speed to advance the sweep) the
					// ring slot is already correct, so any snap would be a
					// teleport, not motion.
					const real_t orbit_speed = all_cached_speed[i];
					const bool orbit_can_move = delta > 0.0 && orbit_speed > 0.0;
					if (already_locked && orbiting_data->direction == DontMove) {
						if (orbiting_data->rigid_follow || orbit_can_move) {
							Vector2 target_pos = orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
							Vector2 snap_delta = target_pos - curr_bullet_origin;
							if (orbiting_data->rigid_follow) {
								// Rigid follow: the formation holds regardless
								// of bullet speed, so fast targets can't drag
								// escorts behind.
								velocity_delta = snap_delta;
							} else if (orbit_can_move) {
								real_t max_step = orbit_speed * (real_t)delta;
								if (snap_delta.length_squared() > max_step * max_step) {
									snap_delta = snap_delta.normalized() * max_step;
								}
								velocity_delta = snap_delta;
							} else {
								velocity_delta = Vector2(0, 0);
							}
						} else {
							velocity_delta = Vector2(0, 0);
						}

						is_physically_orbiting_this_frame = true;
					} else if (already_locked) {
						real_t dir_multiplier = (orbiting_data->direction == OrbitRight) ? 1.0 : (orbiting_data->direction == OrbitLeft ? -1.0 : 0.0);

						if (dir_multiplier != 0.0) {
							// Guard against tiny radius (division by zero -> inf/NaN)
							real_t safe_radius = orbiting_data->radius;
							if (safe_radius < 0.01) {
								safe_radius = 0.01;
							}
							// Rigid follow moves the whole ring slot with the
							// target first (same as DontMove escorts), then
							// advances the angle: circling never lags behind a
							// moving target, no matter how slow the bullet is.
							// With rigid follow off, the angle advances but the
							// ring center lags: cheap drift look, bullets fall
							// behind fast targets (clamped to speed * delta).
							// Zero-delta ticks hold both angle and position: with
							// no time passing any snap would be a teleport.
							if (orbiting_data->rigid_follow) {
								if (delta > 0.0) {
									orbiting_data->angle += (orbit_speed / safe_radius) * dir_multiplier * (real_t)delta;
								}
								// Long-session guard: the angle feeds rotated()
								// only (periodic), so wrap it once float
								// precision could degrade. Threshold form keeps
								// every sane-session read bit-identical.
								if (orbiting_data->angle > 1000000.0 || orbiting_data->angle < -1000000.0) {
									orbiting_data->angle = Math::fposmod(orbiting_data->angle, (real_t)Math::TAU);
								}
								velocity_delta = (orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle)) - curr_bullet_origin;
							} else if (orbit_can_move) {
								real_t angular_speed = (orbit_speed / safe_radius) * dir_multiplier;
								orbiting_data->angle += angular_speed * delta;
								if (orbiting_data->angle > 1000000.0 || orbiting_data->angle < -1000000.0) {
									orbiting_data->angle = Math::fposmod(orbiting_data->angle, (real_t)Math::TAU);
								}
								Vector2 target_pos = orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
								Vector2 snap_delta = target_pos - curr_bullet_origin;
								real_t max_step = orbit_speed * (real_t)delta;
								if (snap_delta.length_squared() > max_step * max_step) {
									snap_delta = snap_delta.normalized() * max_step;
								}
								velocity_delta = snap_delta;
							} else {
								velocity_delta = Vector2(0, 0);
							}
						} else {
							velocity_delta = (orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle)) - curr_bullet_origin;
						}

						is_physically_orbiting_this_frame = true;
					}
					// Check exact frame arrival: use epsilon so low-speed / high-FPS bullets still lock (speed*delta can be <0.2px)
					// DontMove locks here too: the angle stored is the escort's
					// fixed slot, held (never advanced) by the branch above.
					// Zero-delta ticks never lock: with no motion the distance
					// test is meaningless and the snap below would teleport.
					else if (delta > 0.0 && Math::abs(current_dist - orbiting_data->radius) < Math::max((real_t)(orbit_speed * delta), (real_t)2.0)) {
						// REACHED RADIUS - LOCK NOW
						orbiting_data->angle = to_center.angle();
						orbit_stamp_lock(*orbiting_data, orbit_center, *target_deque_used_for_orbiting);

						Vector2 target_pos = orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
						Vector2 snap_delta = target_pos - curr_bullet_origin;
						real_t max_step = orbit_speed * (real_t)delta;
						if (snap_delta.length_squared() > max_step * max_step) {
							snap_delta = snap_delta.normalized() * max_step;
						}
						velocity_delta = snap_delta;

						// We consider this frame as orbiting because we just snapped to the ring
						is_physically_orbiting_this_frame = true;
					} else if (current_dist < orbiting_data->radius) {
						// SPAWNED INSIDE - PUSH OUT
						// This is technically NOT orbiting yet, it's just moving to the border.
						// Zero-delta ticks hold still: advancing next_dist by
						// speed * 0 keeps the bullet exactly where it is.
						Vector2 outward_dir = (current_dist > 0.1f) ? (to_center / current_dist) : Vector2(1, 0);
						real_t next_dist = current_dist + (orbit_speed * (real_t)delta);
						Vector2 target_pos = orbit_center + (outward_dir * next_dist);
						velocity_delta = target_pos - curr_bullet_origin;
					}

					// TEXTURE ROTATION WHEN ORBITING
					// Only rotate if the bullet is PHYSICALLY orbiting (Locked or just snapped).
					// DontMove escorts still face the target (FaceTarget /
					// FaceOppositeTarget); tangential modes are skipped - an
					// escort has no direction of travel.
					if (is_physically_orbiting_this_frame && (orbiting_data->direction != DontMove || (orbiting_data->texture_rotation != FaceOrbitingDirection && orbiting_data->texture_rotation != FaceOppositeOrbitingDirection))) {
						Vector2 look_dir = Vector2();
						// Zero-radius guard: a bullet sitting exactly on the orbit
						// center has no defined facing; keep the old yaw instead
						// of normalizing a zero vector into a stall (angle 0 snap).
						if ((curr_bullet_origin - orbit_center).length_squared() >= 0.000001) {
							Vector2 radial_vec = (curr_bullet_origin - orbit_center).normalized();

							switch (orbiting_data->texture_rotation) {
								case FaceTarget:
									look_dir = -radial_vec;
									break;
								case FaceOppositeTarget:
									look_dir = radial_vec;
									break;
								case FaceOrbitingDirection:
									look_dir = (orbiting_data->direction == OrbitRight) ? Vector2(-radial_vec.y, radial_vec.x) : Vector2(radial_vec.y, -radial_vec.x);
									break;
								case FaceOppositeOrbitingDirection:
									look_dir = (orbiting_data->direction == OrbitRight) ? Vector2(radial_vec.y, -radial_vec.x) : Vector2(-radial_vec.y, radial_vec.x);
									break;
								default:
									break;
							}

							if (look_dir != Vector2()) {
								// Orbiting owns its texture rotation: it must not require the
								// homing_take_control_of_texture_rotation flag (an undocumented
								// cross-feature dependency that left Face* modes silently dead).
								rotate_to_target_preserve_interpolation(i, look_dir, false);
							}
						}
					}
					} // end non-finite orbit-center guard
				}
			}

			// 8. TRANSFORM UPDATES
			curr_bullet_origin += velocity_delta;
			curr_bullet_transf.set_origin(curr_bullet_origin);

			auto &curr_shape_transf = all_cached_shape_transforms[i];
			auto &curr_shape_origin = all_cached_shape_origin[i];
			// Instance carries the texture rotation for rendering; physics must
			// use the logical (un-textured) rotation. When rotate_only_textures
			// is true, keep the shape at its previous logical orientation (it
			// does not follow bullet rotation), otherwise follow the bullet and
			// strip the texture-only offset.
			if (!rotate_only_textures) {
				curr_shape_transf = curr_bullet_transf;
				if (strip_texture_rotation) {
					// Same as rotated_local(-texture_rotation): basis * R.
					const Vector2 c0 = curr_shape_transf.columns[0];
					const Vector2 c1 = curr_shape_transf.columns[1];
					curr_shape_transf.columns[0] = c0 * strip_cos + c1 * strip_sin;
					curr_shape_transf.columns[1] = c1 * strip_cos - c0 * strip_sin;
				}
			}
			Vector2 rotated_offset = Vector2(0, 0);
			if (has_shape_offset) {
				// Rotate by the shape's facing (column 0 direction) without
				// atan2 + sin/cos: same result as rotated(get_rotation()).
				const Vector2 facing = curr_shape_transf.columns[0];
				const real_t facing_len = facing.length();
				if (facing_len > (real_t)0.0) {
					const Vector2 f = facing / facing_len;
					rotated_offset = Vector2(f.x * cache_collision_shape_offset.x - f.y * cache_collision_shape_offset.y, f.y * cache_collision_shape_offset.x + f.x * cache_collision_shape_offset.y);
				} else {
					rotated_offset = cache_collision_shape_offset;
				}
			}
			curr_shape_origin = curr_bullet_origin + rotated_offset;
			curr_shape_transf.set_origin(curr_shape_origin);

			physics_server->area_set_shape_transform(area, i, curr_shape_transf);
			move_bullet_attachment(velocity_delta, i);
			write_trail_instances(i);

			// 7b. REACHED SIGNAL (after all steering): tests the post-move
			// position directly. The transform was already advanced above, so
			// predicting again with velocity_delta would test two ticks
			// ahead. A bullet that followed the shared deque this tick has no per-bullet signal data - skip it.
			// ran: homing_target_pos below is only valid when this bullet
			// actually homed this tick.
			if (target_deque_used_for_orbiting != nullptr && !target_deque_used_for_orbiting->empty() && homing_target_pos.is_finite()) {
				try_to_emit_bullet_homing_target_reached_signal(*target_deque_used_for_orbiting, shared_homing_deque_enabled, i, curr_bullet_origin, homing_target_pos, Vector2(0, 0));
			}

			// 9. MOVEMENT SPEED ACCELERATION - per-bullet curve wins per
			// bullet, shared is the fallback; plain ballistics otherwise.
			// NOTE: disabled bullets freeze at their disable-time speed:
			// per-bullet ballistics are individually owned here, so a wake
			// resumes where that bullet left off (see enable_bullet).
			// Gravity steers velocity directly (no uphill slowdown model here:
			// speed magnitude stays ballistic, the step already curved above).
			// Linear drag trims speed after curves/accel so TD shells decay.
			if (is_per_bullet_curves_valid && per_bullet_curves_data->movement_speed_curve.is_valid()) {
				bullet_accelerate_speed_using_curve(i, delta, per_bullet_curves_data);
			} else if (shared_curves_acceleration_curve_valid) {
				all_cached_speed[i] = shared_movement_speed_val;
				all_cached_velocity[i] = all_cached_direction[i] * shared_movement_speed_val + inherited_velocity_offset + ((i >= 0 && i < (int)all_gravity_velocity.size()) ? all_gravity_velocity[i] : Vector2(0, 0));
			} else {
				bullet_accelerate_speed(i, delta);
			}
			// Bounce boost vs curve overwrite: curves rewrite speed every
			// tick, which would erase a bounce boost on the next frame, so
			// re-apply this bullet's cumulative multiplier on curve-driven
			// ticks. Plain accel needs no rescale (the boost rides inside
			// the cached speed and the bounce-time ceiling raise keeps its
			// clamp away); drag below then decays the scaled total.
			if ((is_per_bullet_curves_valid && per_bullet_curves_data != nullptr && per_bullet_curves_data->movement_speed_curve.is_valid()) || shared_curves_acceleration_curve_valid) {
				if (bounce_speed_scaled && i >= 0 && i < (int)all_bounce_speed_multiplier.size()) {
					const real_t bmult = all_bounce_speed_multiplier[i];
					if (Math::is_finite((double)bmult) && bmult != (real_t)1.0) {
						all_cached_speed[i] *= bmult;
						all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset + ((i >= 0 && i < (int)all_gravity_velocity.size()) ? all_gravity_velocity[i] : Vector2(0, 0));
					}
				}
			}
			if (linear_drag > 0.0 && Math::is_finite(linear_drag)) {
				const real_t keep = Math::max((real_t)0.0, (real_t)1.0 - linear_drag * (real_t)delta);
				all_cached_speed[i] *= keep;
				all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset + ((i >= 0 && i < (int)all_gravity_velocity.size()) ? all_gravity_velocity[i] : Vector2(0, 0));
			} else if (gravity.length_squared() > 0.0) {
				all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset + ((i >= 0 && i < (int)all_gravity_velocity.size()) ? all_gravity_velocity[i] : Vector2(0, 0));
			}
		}
		} // tick_inverse_scope
		if (!is_using_physics_interpolation) {
			batch_flush_instance_transforms();
		}

		// Collisions last: a handler can kill the whole volley mid-drain, so work on a copy - the live list may vanish under us.
		// Self-liveness token (same pattern as handle_bullet_collision): a
		// handler that immediately frees this volley leaves every member
		// access below as use-after-free. ObjectDB validates the id without
		// touching the object, so a freed volley breaks safely instead of
		// crashing (misuse is still prohibited by the handler contract).
		const uint64_t drain_self_id = get_instance_id();
		if (!all_collided_bullets.empty()) {
			collision_scratch.clear();
			collision_scratch.swap(all_collided_bullets);
			// The dedup keys describe exactly this drain window. Clearing them
			// here (not at the end) is safe: the physics server only queues from
			// callbacks that run outside the drain, and any overlap that starts
			// re-filling mid-drain must be able to queue again next frame.
			clear_collision_dedup_keys();
			for (auto &data : collision_scratch) {
				handle_bullet_collision(data.collision_type, data.bullet_index, data.collided_instance_id, data.queue_bullet_epoch, data.queue_target_velocity, data.queue_target_velocity_valid, data.queue_target_position, data.queue_target_position_valid);
				// handle_bullet_collision calls straight into user code, and that code may free this very volley, so
				// check we're still alive before touching anything below (queue_free is caught by the second check).
				// reject via the factory guards).
				if (ObjectDB::get_instance(ObjectID(drain_self_id)) != this) {
					// Freed: every member (collision_scratch included) is gone,
					// so touching anything here would be use-after-free.
					return;
				}
				if (is_queued_for_deletion()) {
					collision_scratch.clear();
					break;
				}
			}
		}
	}

	// Fingerprint of the current front target, so the ring knows when the target actually changed.
	// bullet locked onto last. Node2D = instance id, Vector2 = bit hash of
	// the snapshot namespaced away from the mouse sentinel, mouse = 1 (it
	// has no stable address).
	_ALWAYS_INLINE_ uint64_t orbit_target_identity(const HomingTargetDeque &deque) const {
		if (deque.empty()) {
			return 0;
		}
		const HomingTarget &front = deque.front();
		switch (front.type) {
			case Node2DTarget:
				if (deque.is_homing_target_valid(front.node2d_target_data.target, front.node2d_target_data.cached_valid_instance_id)) {
					return front.node2d_target_data.cached_valid_instance_id;
				}
				return 0;
			case GlobalPositionTarget: {
				const Vector2 p = front.global_position_target;
				if (!p.is_finite()) {
					return 0;
				}
				// Upper bits set: a hashed position can never equal the mouse
				// sentinel (3) or an empty deque (0), and sign information
				// survives (abs() collapsed +p/-p onto one identity before).
				int32_t bx = (int32_t)Math::round(p.x * 16.0);
				int32_t by = (int32_t)Math::round(p.y * 16.0);
				uint64_t h = ((uint64_t)(uint32_t)bx * 0x9E3779B1ULL) ^ ((uint64_t)(uint32_t)by * 0x85EBCA77ULL) ^ ((uint64_t)GlobalPositionTarget * 0xC2B2AE35ULL);
				return h | 0x4000000000000000ULL;
			}
			case MousePositionTarget:
				return (uint64_t)MousePositionTarget;
			default:
				return 0;
		}
	}

	// Locked bullet's live deque (per-bullet wins, same precedence as the
	// tick): the center getter needs the unlocked fallback without
	// duplicating it. A bullet with its own non-empty deque reports that
	// deque; otherwise it reports the shared broadcast deque.
	_ALWAYS_INLINE_ bool orbit_live_deque_for_bullet(int bullet_index, const HomingTargetDeque *&r_deque) const {
		if (bullet_index >= 0 && bullet_index < (int)all_bullet_homing_targets.size() && bullet_index < (int)all_homing_count.size()) {
			if (all_homing_count[bullet_index] > 0 && !all_bullet_homing_targets[bullet_index].empty()) {
				r_deque = &all_bullet_homing_targets[bullet_index];
				return true;
			}
		}
		if (!shared_homing_deque.empty()) {
			r_deque = &shared_homing_deque;
			return true;
		}
		return false;
	}

	_ALWAYS_INLINE_ HomingType orbit_target_type(const HomingTargetDeque &deque) const {
		if (deque.empty()) {
			return NotHoming;
		}
		return deque.front().type;
	}

	// Locked-ring center for this tick. FollowTarget tracks the target.
	// FollowDeadzone pins locked_center until the target walks farther than
	// follow_deadzone from it, then re-pins. Anchored ignores the target
	// entirely: the ring freezes where it locked.
	_ALWAYS_INLINE_ Vector2 orbit_effective_center(OrbitingData &orbiting_data, const Vector2 &live_target_pos) {
		switch (orbiting_data.follow_mode) {
			case Anchored:
				if (!orbiting_data.locked_center.is_finite()) {
					return live_target_pos;
				}
				return orbiting_data.locked_center;
			case FollowDeadzone: {
				const real_t deadzone = (orbiting_data.follow_deadzone > 0.0f) ? orbiting_data.follow_deadzone : 0.0f;
				if (!orbiting_data.locked_center.is_finite() || !live_target_pos.is_finite()) {
					return live_target_pos;
				}
				if ((live_target_pos - orbiting_data.locked_center).length() > deadzone) {
					orbiting_data.locked_center = live_target_pos;
				}
				return orbiting_data.locked_center;
			}
			case FollowTarget:
			default:
				return live_target_pos;
		}
	}

	// Stamp the lock bookkeeping shared by every lock site.
	_ALWAYS_INLINE_ void orbit_stamp_lock(OrbitingData &orbiting_data, const Vector2 &center, const HomingTargetDeque &deque) {
		orbiting_data.is_locked_orbiting = true;
		orbiting_data.locked_center = center;
		orbiting_data.locked_target_type = orbit_target_type(deque);
		orbiting_data.locked_target_identity = orbit_target_identity(deque);
	}

	// Policy gate for every event that would drop the lock.
	// Explicit user action (disable, clear, freed target) always unlocks.
	// Otherwise: RelockAlways unlocks, StayLocked keeps everything
	// (angle + center + identity) so the ring re-pins silently when the
	// target returns, and RelockOnTargetChange unlocks only when the front
	// target is a different identity than the one the bullet locked onto.
	_ALWAYS_INLINE_ bool orbit_should_unlock_for_front_change(OrbitingData &orbiting_data, const HomingTargetDeque &deque) {
		switch (orbiting_data.lock_policy) {
			case StayLocked:
				return false;
			case RelockOnTargetChange: {
				const uint64_t current = orbit_target_identity(deque);
				if (current == 0 || current != orbiting_data.locked_target_identity) {
					return true;
				}
				return false;
			}
			case RelockAlways:
			default:
				return true;
		}
	}

	///////////////// ORBITING DATA METHODS

	// Disabled bullets own no homing/orbit state (disable_bullet clears it
	// and the tick never moves them): pushing targets or arming orbit there
	// inflates the homing/orbiting counters for slots that never drain.
	// Retarget passes skip disabled slots; direct script calls get a loud
	// error here instead of a silent counter leak.
	_ALWAYS_INLINE_ bool orbit_reject_disabled_bullet(int bullet_index, const char *function_name) const {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return false;
		}
		if (!all_bullets_enabled_set.contains(bullet_index)) {
			UtilityFunctions::push_error(String(function_name) + ": bullet index " + String::num_int64(bullet_index) + " is disabled. Wake it with enable_bullet() first, then push targets or enable orbiting.");
			return true;
		}
		return false;
	}

	_ALWAYS_INLINE_ void bullet_enable_orbiting(int bullet_index, real_t orbiting_radius, OrbitingDirection orbiting_direction, OrbitingTextureRotation orbiting_texture_rotation, OrbitingFollowMode orbiting_follow_mode = FollowTarget, real_t orbiting_follow_deadzone = 0.0f, OrbitingLockPolicy orbiting_lock_policy = RelockAlways, bool orbiting_rigid_follow = true) {
		if (!validate_bullet_index(bullet_index, "bullet_enable_orbiting")) {
			return;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_enable_orbiting")) {
			return;
		}

		// NaN comparisons are always false, so a bare `< 0.01` check would
		// store NaN and brick the volley (tick math propagates it into the
		// origin forever). Reject non-finite outright like the setters do.
		if (!Math::is_finite(orbiting_radius) || orbiting_radius < 0.01) {
			if (!Math::is_finite(orbiting_radius)) {
				UtilityFunctions::push_error("Orbiting radius must be finite and >= 0.01, got " + String::num(orbiting_radius) + ". Orbiting stays disabled.");
				return;
			}
			UtilityFunctions::push_error("Orbiting radius must be >= 0.01, got " + String::num(orbiting_radius) + ". Clamping to 0.01 to avoid division by zero.");
			orbiting_radius = 0.01;
		}

		// OrbitRandom resolves per bullet at enable time: each bullet rolls
		// OrbitLeft or OrbitRight (never DontMove) so one call fans a mixed
		// ring. Stored as the rolled value, so getters and re-applies see a
		// concrete direction and the roll never changes mid-flight.
		if (orbiting_direction == OrbitRandom) {
			orbiting_direction = (UtilityFunctions::randi() % 2 == 0) ? OrbitLeft : OrbitRight;
		}

		if (orbiting_direction < DontMove || orbiting_direction > OrbitRight) {
			UtilityFunctions::push_error("Invalid orbiting direction " + String::num_int64(orbiting_direction) + ". Use DontMove, OrbitLeft, OrbitRight or OrbitRandom.");
			return;
		}

		if (orbiting_texture_rotation < FaceTarget || orbiting_texture_rotation > FaceOppositeOrbitingDirection) {
			UtilityFunctions::push_error("Invalid orbiting texture rotation " + String::num_int64(orbiting_texture_rotation) + ". Use FaceTarget, FaceOppositeTarget, FaceOrbitingDirection or FaceOppositeOrbitingDirection.");
			return;
		}

		if (orbiting_follow_mode < FollowTarget || orbiting_follow_mode > Anchored) {
			UtilityFunctions::push_error("Invalid orbiting follow mode " + String::num_int64(orbiting_follow_mode) + ". Use FollowTarget, FollowDeadzone or Anchored.");
			return;
		}

		if (!Math::is_finite(orbiting_follow_deadzone) || orbiting_follow_deadzone < 0.0) {
			UtilityFunctions::push_error("Orbiting follow deadzone must be finite and >= 0, got " + String::num(orbiting_follow_deadzone) + ".");
			return;
		}

		if (orbiting_lock_policy < RelockAlways || orbiting_lock_policy > RelockOnTargetChange) {
			UtilityFunctions::push_error("Invalid orbiting lock policy " + String::num_int64(orbiting_lock_policy) + ". Use RelockAlways, StayLocked or RelockOnTargetChange.");
			return;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 1) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " already has orbiting enabled.");
			return;
		}

		all_orbiting_data[bullet_index] = OrbitingData(orbiting_radius, orbiting_direction, orbiting_texture_rotation, orbiting_follow_mode, orbiting_follow_deadzone, orbiting_lock_policy, orbiting_rigid_follow);
		active_orbiting_count++; // Important because it tracks whether orbiting is even used at all
		orbiting_status = 1;
	}

	_ALWAYS_INLINE_ void bullet_disable_orbiting(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_disable_orbiting")) {
			return;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " already has orbiting disabled.");
			return;
		}

		all_orbiting_data[bullet_index].is_locked_orbiting = false;
		active_orbiting_count--;
		orbiting_status = 0;
	}

	_ALWAYS_INLINE_ void bullet_set_orbiting_radius(int bullet_index, real_t new_radius) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_radius")) {
			return;
		}

		if (!Math::is_finite(new_radius)) {
			UtilityFunctions::push_error("Orbiting radius must be finite, got " + String::num(new_radius) + ". Radius unchanged (a NaN radius would brick the volley).");
			return;
		}
		if (new_radius < 0.01) {
			UtilityFunctions::push_error("Orbiting radius must be >= 0.01, got " + String::num(new_radius) + ". Clamping to 0.01.");
			new_radius = 0.01;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting radius.");
			return;
		}

		auto &orbiting_data = all_orbiting_data[bullet_index];
		// No-op writes keep the lock: the spawner re-applies identical
		// tuning every retarget pass, and unlocking there caused the
		// 1-frame fly-to-rim flicker on moving targets.
		if (Math::is_equal_approx(orbiting_data.radius, new_radius)) {
			orbiting_data.radius = new_radius;
			return;
		}

		orbiting_data.radius = new_radius;
		// StayLocked survives retarget tuning: keep the angle, re-seat the
		// slot on the new radius next tick instead of flying back out to
		// re-acquire it. Every other policy re-locks from scratch.
		if (orbiting_data.lock_policy != StayLocked) {
			orbiting_data.is_locked_orbiting = false;
		}
	}

	_ALWAYS_INLINE_ real_t bullet_get_orbiting_radius(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_radius")) {
			return 0.0;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting radius.");
			return 0.0;
		}

		auto &orbiting_data = all_orbiting_data[bullet_index];
		return orbiting_data.radius;
	}

	_ALWAYS_INLINE_ bool bullet_is_orbiting_enabled(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_is_orbiting_enabled")) {
			return false;
		}

		return all_orbiting_status[bullet_index] == 1;
	}

	_ALWAYS_INLINE_ void bullet_set_orbiting_texture_rotation(int bullet_index, OrbitingTextureRotation new_texture_rotation) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_texture_rotation")) {
			return;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting texture rotation.");
			return;
		}

		auto &orbiting_data = all_orbiting_data[bullet_index];
		if (new_texture_rotation < FaceTarget || new_texture_rotation > FaceOppositeOrbitingDirection) {
			UtilityFunctions::push_error("Invalid orbiting texture rotation " + String::num_int64(new_texture_rotation) + ". Use FaceTarget, FaceOppositeTarget, FaceOrbitingDirection or FaceOppositeOrbitingDirection.");
			return;
		}
		orbiting_data.texture_rotation = new_texture_rotation;
	}

	_ALWAYS_INLINE_ OrbitingTextureRotation bullet_get_orbiting_texture_rotation(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_texture_rotation")) {
			return FaceTarget;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting texture rotation.");
			return FaceTarget;
		}

		auto &orbiting_data = all_orbiting_data[bullet_index];
		return orbiting_data.texture_rotation;
	}

	_ALWAYS_INLINE_ void bullet_set_orbiting_direction(int bullet_index, OrbitingDirection new_direction) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_direction")) {
			return;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting direction.");
			return;
		}

		auto &orbiting_data = all_orbiting_data[bullet_index];
		// OrbitRandom rolls a concrete direction per call (never DontMove):
		// setting it re-rolls instead of storing the sentinel, so the stored
		// direction is always a real sweep.
		if (new_direction == OrbitRandom) {
			new_direction = (UtilityFunctions::randi() % 2 == 0) ? OrbitLeft : OrbitRight;
		}
		if (new_direction < DontMove || new_direction > OrbitRight) {
			UtilityFunctions::push_error("Invalid orbiting direction " + String::num_int64(new_direction) + ". Use DontMove, OrbitLeft, OrbitRight or OrbitRandom.");
			return;
		}
		if (orbiting_data.direction == new_direction) {
			return;
		}
		orbiting_data.direction = new_direction;
		// Same StayLocked rule as the radius setter: a retarget carrying a
		// changed sweep keeps the slot instead of dropping it.
		if (orbiting_data.lock_policy != StayLocked) {
			orbiting_data.is_locked_orbiting = false;
		}
	}

	_ALWAYS_INLINE_ OrbitingDirection bullet_get_orbiting_direction(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_direction")) {
			return DontMove;
		}

		auto &orbiting_status = all_orbiting_status[bullet_index];

		if (orbiting_status == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting direction.");
			return DontMove;
		}

		auto &orbiting_data = all_orbiting_data[bullet_index];
		return orbiting_data.direction;
	}

	/////////////////

	///////////////// ORBITING DATA HELPERS

	// Re-lock every locked bullet onto a fresh deque without the fly-to-rim
	// flicker: same front identity = keep angle + center, new identity =
	// stamp the new target (angle preserved, center re-pinned) under
	// StayLocked/RelockOnTargetChange, full relock under RelockAlways.
	// Explicit clears still go through bullet_clear_homing_targets (unlock).
	_ALWAYS_INLINE_ void orbit_keep_lock_across_replace(HomingTargetDeque &deque) {
		for (size_t k = 0; k < all_orbiting_data.size(); ++k) {
			if (k >= all_orbiting_status.size() || all_orbiting_status[k] == 0) {
				continue;
			}
			OrbitingData &o = all_orbiting_data[k];
			if (!o.is_locked_orbiting) {
				continue;
			}
			if (deque.empty()) {
				if (o.lock_policy == StayLocked) {
					continue;
				}
				o.is_locked_orbiting = false;
				continue;
			}
			if (o.lock_policy == RelockAlways) {
				o.is_locked_orbiting = false;
				continue;
			}
			if (!orbit_should_unlock_for_front_change(o, deque)) {
				o.locked_center = deque.get_cached_front_target_global_position();
				o.locked_target_type = orbit_target_type(deque);
				o.locked_target_identity = orbit_target_identity(deque);
			} else {
				o.is_locked_orbiting = false;
			}
		}
	}

	// Policy-aware unlock for a deque that ran dry: StayLocked rides out the
	// gap (keeps angle/center/identity so the ring re-pins silently when a
	// target returns), every other policy unlocks.
	_ALWAYS_INLINE_ void orbit_unlock_on_empty_deque() {
		for (size_t k = 0; k < all_orbiting_data.size(); ++k) {
			if (k >= all_orbiting_status.size() || all_orbiting_status[k] == 0) {
				continue;
			}
			OrbitingData &o = all_orbiting_data[k];
			if (!o.is_locked_orbiting || o.lock_policy == StayLocked) {
				continue;
			}
			o.is_locked_orbiting = false;
		}
	}

	// Same, scoped to one per-bullet deque's owner.
	_ALWAYS_INLINE_ void orbit_unlock_on_empty_deque_for_bullet(int bullet_index) {
		if (bullet_index < 0 || bullet_index >= (int)all_orbiting_data.size() || bullet_index >= (int)all_orbiting_status.size()) {
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			return;
		}
		OrbitingData &o = all_orbiting_data[bullet_index];
		if (!o.is_locked_orbiting || o.lock_policy == StayLocked) {
			return;
		}
		o.is_locked_orbiting = false;
	}

	_ALWAYS_INLINE_ void all_bullets_enable_orbiting(real_t orbiting_radius, OrbitingDirection orbiting_direction, OrbitingTextureRotation orbiting_texture_rotation, int bullet_index_start = 0, int bullet_index_end_inclusive = -1, OrbitingFollowMode orbiting_follow_mode = FollowTarget, real_t orbiting_follow_deadzone = 0.0f, OrbitingLockPolicy orbiting_lock_policy = RelockAlways, bool orbiting_rigid_follow = true) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_enable_orbiting");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_enable_orbiting(i, orbiting_radius, orbiting_direction, orbiting_texture_rotation, orbiting_follow_mode, orbiting_follow_deadzone, orbiting_lock_policy, orbiting_rigid_follow);
		}
	}

	// Concentric-ring enable: bullet (start + k) orbits at radius_start + radius_step * k.
	// Re-arming an already-orbiting bullet updates every passed param (not
	// just the radius): ranges like all_bullets_enable_orbiting_linear must
	// be re-runnable on armed volleys. OrbitRandom keeps each bullet's rolled
	// direction (re-setting it would re-roll mid-flight); the rest applies.
	// Invalid enums are still rejected per bullet by the individual setters.
	_ALWAYS_INLINE_ void all_bullets_enable_orbiting_linear(real_t radius_start, real_t radius_step, OrbitingDirection orbiting_direction = OrbitRight, OrbitingTextureRotation orbiting_texture_rotation = FaceTarget, int bullet_index_start = 0, int bullet_index_end_inclusive = -1, OrbitingFollowMode orbiting_follow_mode = FollowTarget, real_t orbiting_follow_deadzone = 0.0f, OrbitingLockPolicy orbiting_lock_policy = RelockAlways, bool orbiting_rigid_follow = true) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_enable_orbiting_linear");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			const real_t want_radius = radius_start + radius_step * (real_t)(i - bullet_index_start);
			if (i >= 0 && i < (int)all_orbiting_status.size() && all_orbiting_status[i] == 1) {
				bullet_set_orbiting_radius(i, want_radius);
				if (orbiting_direction != OrbitRandom) {
					bullet_set_orbiting_direction(i, orbiting_direction);
				}
				bullet_set_orbiting_texture_rotation(i, orbiting_texture_rotation);
				bullet_set_orbiting_follow_mode(i, orbiting_follow_mode);
				bullet_set_orbiting_follow_deadzone(i, orbiting_follow_deadzone);
				bullet_set_orbiting_lock_policy(i, orbiting_lock_policy);
				bullet_set_orbiting_rigid_follow(i, orbiting_rigid_follow);
				continue;
			}
			bullet_enable_orbiting(i, want_radius, orbiting_direction, orbiting_texture_rotation, orbiting_follow_mode, orbiting_follow_deadzone, orbiting_lock_policy, orbiting_rigid_follow);
		}
	}

	_ALWAYS_INLINE_ bool bullet_is_orbiting_locked(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_is_orbiting_locked")) {
			return false;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			return false;
		}
		return all_orbiting_data[bullet_index].is_locked_orbiting;
	}

	_ALWAYS_INLINE_ Vector2 bullet_get_orbiting_center(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_center")) {
			return Vector2(0, 0);
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting center.");
			return Vector2(0, 0);
		}
		auto &orbiting_data = all_orbiting_data[bullet_index];
		if (orbiting_data.is_locked_orbiting) {
			return orbiting_data.locked_center;
		}
		const HomingTargetDeque *live_deque = nullptr;
		if (orbit_live_deque_for_bullet(bullet_index, live_deque) && live_deque != nullptr) {
			return live_deque->get_cached_front_target_global_position();
		}
		return Vector2(0, 0);
	}

	// Locked angle in radians: the ring slot the bullet holds (or is flying to).
	_ALWAYS_INLINE_ real_t bullet_get_orbiting_angle(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_angle")) {
			return 0.0;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting angle.");
			return 0.0;
		}
		return all_orbiting_data[bullet_index].angle;
	}

	// Re-pin the locked ring center without unlocking: Anchored rings follow a
	// scripted point, Deadzone rings skip ahead, StayLocked rings jump to a
	// teleported target. Rejected (no unlock) when orbiting is off or the
	// bullet never locked.
	_ALWAYS_INLINE_ void bullet_set_orbiting_center(int bullet_index, const Vector2 &new_center) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_center")) {
			return;
		}
		if (!new_center.is_finite()) {
			UtilityFunctions::push_error("bullet_set_orbiting_center: new_center must be finite (NaN/Inf is rejected).");
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting center.");
			return;
		}
		auto &orbiting_data = all_orbiting_data[bullet_index];
		if (!orbiting_data.is_locked_orbiting) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " is not locked onto its ring yet. Cannot set orbiting center.");
			return;
		}
		orbiting_data.locked_center = new_center;
	}

	_ALWAYS_INLINE_ void bullet_set_orbiting_follow_mode(int bullet_index, OrbitingFollowMode new_follow_mode) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_follow_mode")) {
			return;
		}
		if (new_follow_mode < FollowTarget || new_follow_mode > Anchored) {
			UtilityFunctions::push_error("Invalid orbiting follow mode " + String::num_int64(new_follow_mode) + ". Use FollowTarget, FollowDeadzone or Anchored.");
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting follow mode.");
			return;
		}
		all_orbiting_data[bullet_index].follow_mode = new_follow_mode;
	}

	_ALWAYS_INLINE_ OrbitingFollowMode bullet_get_orbiting_follow_mode(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_follow_mode")) {
			return FollowTarget;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting follow mode.");
			return FollowTarget;
		}
		return all_orbiting_data[bullet_index].follow_mode;
	}

	_ALWAYS_INLINE_ void bullet_set_orbiting_follow_deadzone(int bullet_index, real_t new_deadzone) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_follow_deadzone")) {
			return;
		}
		if (!Math::is_finite(new_deadzone) || new_deadzone < 0.0) {
			UtilityFunctions::push_error("Orbiting follow deadzone must be finite and >= 0, got " + String::num(new_deadzone) + ".");
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting follow deadzone.");
			return;
		}
		all_orbiting_data[bullet_index].follow_deadzone = new_deadzone;
	}

	_ALWAYS_INLINE_ real_t bullet_get_orbiting_follow_deadzone(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_follow_deadzone")) {
			return 0.0;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting follow deadzone.");
			return 0.0;
		}
		return all_orbiting_data[bullet_index].follow_deadzone;
	}

	_ALWAYS_INLINE_ void bullet_set_orbiting_lock_policy(int bullet_index, OrbitingLockPolicy new_lock_policy) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_lock_policy")) {
			return;
		}
		if (new_lock_policy < RelockAlways || new_lock_policy > RelockOnTargetChange) {
			UtilityFunctions::push_error("Invalid orbiting lock policy " + String::num_int64(new_lock_policy) + ". Use RelockAlways, StayLocked or RelockOnTargetChange.");
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting lock policy.");
			return;
		}
		all_orbiting_data[bullet_index].lock_policy = new_lock_policy;
	}

	_ALWAYS_INLINE_ OrbitingLockPolicy bullet_get_orbiting_lock_policy(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_lock_policy")) {
			return RelockAlways;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting lock policy.");
			return RelockAlways;
		}
		return all_orbiting_data[bullet_index].lock_policy;
	}

	// When on, locked OrbitLeft/OrbitRight bullets translate 1:1 with the
	// target (same rigid snap DontMove escorts use) and keep circling: the
	// ring never lags, stretches, or re-locks when the target moves. When
	// off, locked bullets chase the ring clamped to speed * delta, so slow
	// bullets trail behind fast targets. Never drops the lock.
	_ALWAYS_INLINE_ void bullet_set_orbiting_rigid_follow(int bullet_index, bool new_rigid_follow) {
		if (!validate_bullet_index(bullet_index, "bullet_set_orbiting_rigid_follow")) {
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot set orbiting rigid follow.");
			return;
		}
		all_orbiting_data[bullet_index].rigid_follow = new_rigid_follow;
	}

	_ALWAYS_INLINE_ bool bullet_get_orbiting_rigid_follow(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_orbiting_rigid_follow")) {
			return true;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			UtilityFunctions::push_warning("Bullet index " + String::num_int64(bullet_index) + " has orbiting disabled. Cannot get orbiting rigid follow.");
			return true;
		}
		return all_orbiting_data[bullet_index].rigid_follow;
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_rigid_follow(bool new_rigid_follow, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_rigid_follow");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_rigid_follow(i, new_rigid_follow);
		}
	}

	_ALWAYS_INLINE_ PackedFloat32Array all_bullets_get_orbiting_radius(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_orbiting_radius");

		PackedFloat32Array arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_get_orbiting_radius(i));
		}

		return arr;
	}

	// Bulk center reads (same per-bullet warnings as bullet_get_orbiting_center).
	// Example: var centers = bullets.all_bullets_get_orbiting_center().
	_ALWAYS_INLINE_ PackedVector2Array all_bullets_get_orbiting_center(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_orbiting_center");

		PackedVector2Array arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_get_orbiting_center(i));
		}

		return arr;
	}

	// Bulk ring-slot reads (same per-bullet warnings as bullet_get_orbiting_angle).
	_ALWAYS_INLINE_ PackedFloat32Array all_bullets_get_orbiting_angle(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_orbiting_angle");

		PackedFloat32Array arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_get_orbiting_angle(i));
		}

		return arr;
	}

	// Which deque feeds each bullet ("per", "shared", or "none"): per-bullet
	// wins, shared is the fallback, matching the steering tick.
	// Example: var info = bullets.debug_get_orbiting_info(0); print(info["deque_src"]).
	Dictionary debug_get_orbiting_info(int bullet_index) const {
		Dictionary d;
		d["valid"] = false;
		d["enabled"] = false;
		d["locked"] = false;
		d["center"] = Vector2(0, 0);
		d["angle"] = 0.0;
		d["radius"] = 0.0;
		d["direction"] = 0;
		d["texture_rotation"] = 0;
		d["follow_mode"] = 0;
		d["deadzone"] = 0.0;
		d["lock_policy"] = 0;
		d["rigid_follow"] = false;
		d["deque_src"] = "none";
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return d;
		}
		d["valid"] = true;
		if (bullet_index >= 0 && bullet_index < (int)all_orbiting_status.size()) {
			d["enabled"] = all_orbiting_status[bullet_index] != 0;
		}
		if (bullet_index >= 0 && bullet_index < (int)all_orbiting_data.size()) {
			const OrbitingData &o = all_orbiting_data[bullet_index];
			d["locked"] = o.is_locked_orbiting;
			d["center"] = o.locked_center;
			d["angle"] = o.angle;
			d["radius"] = o.radius;
			d["direction"] = (int)o.direction;
			d["texture_rotation"] = (int)o.texture_rotation;
			d["follow_mode"] = (int)o.follow_mode;
			d["deadzone"] = o.follow_deadzone;
			d["lock_policy"] = (int)o.lock_policy;
			d["rigid_follow"] = o.rigid_follow;
			if (o.is_locked_orbiting) {
				d["deque_src"] = "locked";
				return d;
			}
		}
		const HomingTargetDeque *live_deque = nullptr;
		bool is_per = false;
		if (bullet_index >= 0 && bullet_index < (int)all_bullet_homing_targets.size() && bullet_index < (int)all_homing_count.size()) {
			is_per = all_homing_count[bullet_index] > 0 && !all_bullet_homing_targets[bullet_index].empty();
		}
		if (orbit_live_deque_for_bullet(bullet_index, live_deque) && live_deque != nullptr) {
			d["deque_src"] = is_per ? "per" : "shared";
			d["center"] = live_deque->get_cached_front_target_global_position();
		}
		return d;
	}

	// Bulk homing-queue depths. Example: var n = bullets.all_bullets_get_homing_targets_amount().
	_ALWAYS_INLINE_ PackedInt32Array all_bullets_get_homing_targets_amount(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_homing_targets_amount");

		PackedInt32Array arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_homing_check_targets_amount(i));
		}

		return arr;
	}

	_ALWAYS_INLINE_ TypedArray<bool> all_bullets_is_orbiting_enabled(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_is_orbiting_enabled");

		TypedArray<bool> arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_is_orbiting_enabled(i));
		}

		return arr;
	}

	_ALWAYS_INLINE_ void all_bullets_disable_orbiting(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_disable_orbiting");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_disable_orbiting(i);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_radius(real_t new_radius, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_radius");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_radius(i, new_radius);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_direction(OrbitingDirection new_direction, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_direction");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_direction(i, new_direction);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_texture_rotation(OrbitingTextureRotation new_rotation, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_texture_rotation");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_texture_rotation(i, new_rotation);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_follow_mode(OrbitingFollowMode new_follow_mode, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_follow_mode");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_follow_mode(i, new_follow_mode);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_follow_deadzone(real_t new_deadzone, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_follow_deadzone");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_follow_deadzone(i, new_deadzone);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_lock_policy(OrbitingLockPolicy new_lock_policy, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_lock_policy");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_lock_policy(i, new_lock_policy);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_set_orbiting_center(const Vector2 &new_center, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_orbiting_center");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_orbiting_center(i, new_center);
		}
	}

	_ALWAYS_INLINE_ TypedArray<bool> all_bullets_is_orbiting_locked(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_is_orbiting_locked");

		TypedArray<bool> arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_is_orbiting_locked(i));
		}

		return arr;
	}

	////////////////

	///////////// PER BULLET HOMING DEQUE POP METHODS

	// NOTE: manual pop/clear + push in the same frame as a reach races the
	// deferred auto-pop/emit queued for the old front (same bullet epoch):
	// the flush still pops the fresh front and fires a ghost reached signal.
	// Keep manual edits and auto-pop apart in one frame, or re-push after
	// the flush.
	_ALWAYS_INLINE_ Variant bullet_homing_pop_front_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_front_target") || !bullet_check_has_homing_targets(bullet_index)) {
			return nullptr;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		if (all_homing_count[bullet_index] > 0) {
			--all_homing_count[bullet_index];
		}
		if (active_homing_count > 0) {
			--active_homing_count;
		}

		Variant popped = queue.pop_front_target(cached_mouse_global_position);
		// Resync hardening: the tick trim path and direct deque edits can
		// leave the counter above the live deque size (phantom-homing an
		// empty deque). Clamp down so counters always reflect reality.
		const int live = queue.get_homing_targets_amount();
		if (all_homing_count[bullet_index] > live) {
			active_homing_count -= (all_homing_count[bullet_index] - live);
			if (active_homing_count < 0) {
				active_homing_count = 0;
			}
			all_homing_count[bullet_index] = live;
		}
		orbit_route_front_change_for_bullet(bullet_index, queue);
		return popped;
	}

	// NOTE: manual pop/clear + push in the same frame as a reach races the
	// deferred auto-pop/emit queued for the old front (same bullet epoch):
	// the flush still pops the fresh front and fires a ghost reached signal.
	// Keep manual edits and auto-pop apart in one frame, or re-push after
	// the flush.
	_ALWAYS_INLINE_ Variant bullet_homing_pop_back_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_back_target") || !bullet_check_has_homing_targets(bullet_index)) {
			return nullptr;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		if (all_homing_count[bullet_index] > 0) {
			--all_homing_count[bullet_index];
		}
		if (active_homing_count > 0) {
			--active_homing_count;
		}

		Variant popped = queue.pop_back_target(cached_mouse_global_position);
		// Same resync as the front-pop above.
		const int live = queue.get_homing_targets_amount();
		if (all_homing_count[bullet_index] > live) {
			active_homing_count -= (all_homing_count[bullet_index] - live);
			if (active_homing_count < 0) {
				active_homing_count = 0;
			}
			all_homing_count[bullet_index] = live;
		}
		// Back-pop leaves the front target untouched: only an emptied deque
		// unlocks here, the lock itself is never disturbed by a tail edit.
		if (bullet_index >= 0 && bullet_index < (int)all_orbiting_data.size() && bullet_index < (int)all_orbiting_status.size() && all_orbiting_status[bullet_index]) {
			orbit_keep_lock_across_replace_for_bullet_front_only(bullet_index, queue, false);
		}
		return popped;
	}
	/////////////////////

	//////////////// PER BULLET HOMING DEQUE PUSH METHODS

	_ALWAYS_INLINE_ bool bullet_homing_push_front_mouse_position_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_mouse_position_target")) {
			return false;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_homing_push_front_mouse_position_target")) {
			return false;
		}

		// Always refresh on push: keying freshness off the GLOBAL mouse-target counter
		// made a fresh target inherit this node's stale cache whenever any OTHER
		// multimesh held mouse targets.
		cached_mouse_global_position = get_global_mouse_position();

		auto &queue = all_bullet_homing_targets[bullet_index];

		// Only count the target if the deque actually stored it (rejected
		// pushes - full deque - must not desync the counters into
		// phantom-homing an empty deque forever).
		if (!queue.push_front_mouse_position_target(cached_mouse_global_position)) {
			return false;
		}

		++all_homing_count[bullet_index];
		++active_homing_count;

		// A re-exposed front target re-arms like the shared deque does: the
		// per-bullet reached flag below is per-target, so without this a
		// popped-then-repushed target never fires again.
		queue.reset_front_reached_flag();

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_node2d_target")) {
			return false;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_homing_push_front_node2d_target")) {
			return false;
		}

		if (new_homing_target == nullptr) {
			UtilityFunctions::push_error("bullet_homing_push_front_node2d_target: target is null, nothing pushed.");
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		if (!queue.push_front_node2d_target(new_homing_target)) {
			return false;
		}

		++all_homing_count[bullet_index];
		++active_homing_count;
		queue.reset_front_reached_flag();
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_global_position_target")) {
			return false;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_homing_push_front_global_position_target")) {
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		// Only count the target if the deque actually stored it (a non-finite position
		// is rejected inside the deque; counting it would desync the counters and leave
		// this bullet phantom-homing an empty deque forever).
		if (!queue.push_front_global_position_target(global_position)) {
			return false;
		}

		++all_homing_count[bullet_index];
		++active_homing_count;
		queue.reset_front_reached_flag();
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_mouse_position_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_mouse_position_target")) {
			return false;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_homing_push_back_mouse_position_target")) {
			return false;
		}

		// Always refresh on push (see push_front variant for the rationale).
		cached_mouse_global_position = get_global_mouse_position();

		auto &queue = all_bullet_homing_targets[bullet_index];

		// Only count the target if the deque actually stored it (see front variant).
		if (!queue.push_back_mouse_position_target(cached_mouse_global_position)) {
			return false;
		}

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_node2d_target")) {
			return false;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_homing_push_back_node2d_target")) {
			return false;
		}

		if (new_homing_target == nullptr) {
			UtilityFunctions::push_error("bullet_homing_push_back_node2d_target: target is null, nothing pushed.");
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		if (!queue.push_back_node2d_target(new_homing_target)) {
			return false;
		}

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_global_position_target")) {
			return false;
		}
		if (orbit_reject_disabled_bullet(bullet_index, "bullet_homing_push_back_global_position_target")) {
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		// Only count the target if the deque actually stored it (see push_front variant).
		if (!queue.push_back_global_position_target(global_position)) {
			return false;
		}

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}

	// Single-bullet Variant push (Node2D or Vector2), mirroring the
	// all_bullets_*_homing_target type branch. Returns false with an error on
	// invalid index or target type, pushing nothing.
	_ALWAYS_INLINE_ bool bullet_homing_push_back_homing_target(int bullet_index, const Variant &node2d_or_global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_homing_target")) {
			return false;
		}
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			return bullet_homing_push_back_node2d_target(bullet_index, node);
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			return bullet_homing_push_back_global_position_target(bullet_index, node2d_or_global_position);
		}
		UtilityFunctions::push_error("Invalid homing target type in bullet_homing_push_back_homing_target. Use a Node2D or Vector2.");
		return false;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_homing_target(int bullet_index, const Variant &node2d_or_global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_homing_target")) {
			return false;
		}
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			return bullet_homing_push_front_node2d_target(bullet_index, node);
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			return bullet_homing_push_front_global_position_target(bullet_index, node2d_or_global_position);
		}
		UtilityFunctions::push_error("Invalid homing target type in bullet_homing_push_front_homing_target. Use a Node2D or Vector2.");
		return false;
	}
	/////////////////////////////

	///  PER BULLET HOMING DEQUE HELPERS

	// NOTE: manual pop/clear + push in the same frame as a reach races the
	// deferred auto-pop/emit queued for the old front (same bullet epoch):
	// the flush still pops the fresh front and fires a ghost reached signal.
	// Keep manual edits and auto-pop apart in one frame, or re-push after
	// the flush.
	_ALWAYS_INLINE_ void bullet_clear_homing_targets(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_clear_homing_targets")) {
			return;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		auto &count = all_homing_count[bullet_index];
		active_homing_count -= count;
		if (active_homing_count < 0) {
			active_homing_count = 0;
		}
		count = 0;

		queue.clear_homing_targets(cached_mouse_global_position);
		orbit_unlock_on_empty_deque_for_bullet(bullet_index);
	}

	_ALWAYS_INLINE_ Array all_bullets_pop_front_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_pop_front_target");
		Array popped_targets;

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			popped_targets.push_back(bullet_homing_pop_front_target(i)); // could push nullptr but that's expected
		}

		return popped_targets;
	}

	_ALWAYS_INLINE_ Array all_bullets_pop_back_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_pop_back_target");
		Array popped_targets;

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			popped_targets.push_back(bullet_homing_pop_back_target(i));
		}

		return popped_targets;
	}

	// Bulk push paths skip disabled slots silently: the per-bullet push
	// rejects them loudly, and a retarget pass over a partially-disabled
	// volley must not spam one error per bullet per interval. Declared
	// before every bulk path that uses it (including the mouse pushes).
	_ALWAYS_INLINE_ bool orbit_skip_disabled_in_bulk(int bullet_index) const {
		return bullet_index < 0 || bullet_index >= amount_bullets || !all_bullets_enabled_set.contains(bullet_index);
	}

	_ALWAYS_INLINE_ void all_bullets_push_back_mouse_position_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_mouse_position_target");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (orbit_skip_disabled_in_bulk(i)) {
				continue;
			}
			bullet_homing_push_back_mouse_position_target(i);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_mouse_position_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_mouse_position_target");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (orbit_skip_disabled_in_bulk(i)) {
				continue;
			}
			bullet_homing_push_front_mouse_position_target(i);
		}
	}

	// Bulk push paths skip disabled slots silently (see the helper above the
	// mouse pushes): the per-bullet push rejects them loudly, and a retarget
	// pass over a partially-disabled volley must not spam one error per
	// bullet per interval.
	_ALWAYS_INLINE_ void all_bullets_push_back_homing_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_homing_target");
		if (node2d_or_global_position.get_type() == Variant::VECTOR2 && !Vector2(node2d_or_global_position).is_finite()) {
			UtilityFunctions::push_error("Non-finite homing target in all_bullets_push_back_homing_target. Nothing was pushed.");
			return;
		}
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (orbit_skip_disabled_in_bulk(i)) {
					continue;
				}
				bullet_homing_push_back_node2d_target(i, node);
			}
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			Vector2 global_pos = node2d_or_global_position;
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (orbit_skip_disabled_in_bulk(i)) {
					continue;
				}
				bullet_homing_push_back_global_position_target(i, global_pos);
			}
		} else {
			UtilityFunctions::push_error("Invalid homing target type in all_bullets_push_back_homing_target");
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_homing_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_homing_target");
		if (node2d_or_global_position.get_type() == Variant::VECTOR2 && !Vector2(node2d_or_global_position).is_finite()) {
			UtilityFunctions::push_error("Non-finite homing target in all_bullets_push_front_homing_target. Nothing was pushed.");
			return;
		}
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (orbit_skip_disabled_in_bulk(i)) {
					continue;
				}
				bullet_homing_push_front_node2d_target(i, node);
			}
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			Vector2 global_pos = node2d_or_global_position;
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				if (orbit_skip_disabled_in_bulk(i)) {
					continue;
				}
				bullet_homing_push_front_global_position_target(i, global_pos);
			}
		} else {
			UtilityFunctions::push_error("Invalid homing target type in all_bullets_push_front_homing_target");
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_back_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_homing_targets_array");
		for (const Variant &target : node2ds_or_global_positions_array) {
			all_bullets_push_back_homing_target(target, bullet_index_start, bullet_index_end_inclusive);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_homing_targets_array");
		for (const Variant &target : node2ds_or_global_positions_array) {
			all_bullets_push_front_homing_target(target, bullet_index_start, bullet_index_end_inclusive);
		}
	}

	// Per-bullet variant of the shared replace above: instead of clear (which
	// unlocks) + push, clear the raw deque inline and re-pin surviving locks
	// onto the fresh front. Returns false when the slot is disabled or the
	// target invalid (nothing touched): bulk callers skip disabled slots
	// silently, direct script calls with a bad target still get the loud
	// error below. A direct call on a disabled slot stays silent (bulk
	// parity) — wake the bullet first, then replace.
	_ALWAYS_INLINE_ bool bullet_replace_homing_targets_with_new_target(int bullet_index, const Variant &node2d_or_global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_replace_homing_targets_with_new_target")) {
			return false;
		}
		if (bullet_index < 0 || bullet_index >= amount_bullets || !all_bullets_enabled_set.contains(bullet_index)) {
			return false;
		}
		Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position);
		const bool is_vec = node2d_or_global_position.get_type() == Variant::VECTOR2;
		if (node == nullptr && !is_vec) {
			UtilityFunctions::push_error("Invalid homing target type in bullet_replace_homing_targets_with_new_target. Use a Node2D or Vector2. Nothing was changed.");
			return false;
		}
		if (is_vec && !Vector2(node2d_or_global_position).is_finite()) {
			UtilityFunctions::push_error("Non-finite homing target in bullet_replace_homing_targets_with_new_target. Nothing was changed.");
			return false;
		}
		auto &queue = all_bullet_homing_targets[bullet_index];
		auto &count = all_homing_count[bullet_index];
		active_homing_count -= count;
		if (active_homing_count < 0) {
			active_homing_count = 0;
		}
		count = 0;
		queue.clear_homing_targets(cached_mouse_global_position);
		bullet_homing_push_back_homing_target(bullet_index, node2d_or_global_position);
		orbit_keep_lock_across_replace_for_bullet(bullet_index, queue);
		return true;
	}

	_ALWAYS_INLINE_ void orbit_keep_lock_across_replace_for_bullet(int bullet_index, HomingTargetDeque &deque) {
		if (bullet_index < 0 || bullet_index >= (int)all_orbiting_data.size() || bullet_index >= (int)all_orbiting_status.size()) {
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			return;
		}
		orbit_keep_lock_across_replace_for_bullet_front_only(bullet_index, deque);
	}

	// Front-change core: shared by pops (any position) and replaces.
	// front_changed tells whether the front target is a different target
	// than before: a back-pop leaves the front untouched, so only an empty
	// deque unlocks there, while RelockAlways still re-locks on a real
	// front swap.
	_ALWAYS_INLINE_ void orbit_keep_lock_across_replace_for_bullet_front_only(int bullet_index, HomingTargetDeque &deque, bool front_changed = true) {
		OrbitingData &o = all_orbiting_data[bullet_index];
		if (!o.is_locked_orbiting) {
			return;
		}
		if (deque.empty()) {
			if (o.lock_policy != StayLocked) {
				o.is_locked_orbiting = false;
			}
			return;
		}
		if (!front_changed) {
			return;
		}
		if (o.lock_policy == RelockAlways) {
			o.is_locked_orbiting = false;
			return;
		}
		if (!orbit_should_unlock_for_front_change(o, deque)) {
			o.locked_center = deque.get_cached_front_target_global_position();
			o.locked_target_type = orbit_target_type(deque);
			o.locked_target_identity = orbit_target_identity(deque);
		} else {
			o.is_locked_orbiting = false;
		}
	}

	// Single routing point for every per-bullet front change that is not an
	// explicit clear: manual pops (front/back) and the deferred auto-pop
	// flush funnel here. Empty deque = policy-aware unlock (StayLocked rides
	// out the gap); non-empty deque = keep or unlock per lock policy, so
	// RelockAlways never holds a stale lock on a new target and
	// StayLocked/RelockOnTargetChange never flicker on the same target.
	_ALWAYS_INLINE_ void orbit_route_front_change_for_bullet(int bullet_index, HomingTargetDeque &deque) {
		orbit_keep_lock_across_replace_for_bullet(bullet_index, deque);
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_new_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_new_target");

		// Validate BEFORE clearing: an invalid target must not wipe the user's targets.
		const bool is_node2d = Object::cast_to<Node2D>(node2d_or_global_position) != nullptr;
		if (!is_node2d && node2d_or_global_position.get_type() != Variant::VECTOR2) {
			UtilityFunctions::push_error("Invalid homing target type in all_bullets_replace_homing_targets_with_new_target. Use a Node2D or Vector2. Nothing was changed.");
			return;
		}
		if (node2d_or_global_position.get_type() == Variant::VECTOR2 && !Vector2(node2d_or_global_position).is_finite()) {
			UtilityFunctions::push_error("Non-finite homing target in all_bullets_replace_homing_targets_with_new_target. Nothing was changed.");
			return;
		}

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_replace_homing_targets_with_new_target(i, node2d_or_global_position);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_new_target_array");

		// Validate every entry BEFORE clearing so a bad entry can't wipe the user's
		// targets (all-or-nothing replace).
		for (int k = 0; k < node2ds_or_global_positions_array.size(); ++k) {
			const Variant &target = node2ds_or_global_positions_array[k];
			if (Object::cast_to<Node2D>(target) == nullptr && target.get_type() != Variant::VECTOR2) {
				UtilityFunctions::push_error("Invalid homing target type in all_bullets_replace_homing_targets_with_new_target_array at array index " + String::num_int64(k) + ". Use Node2D or Vector2 entries. Nothing was changed.");
				return;
			}
			if (target.get_type() == Variant::VECTOR2 && !Vector2(target).is_finite()) {
				UtilityFunctions::push_error("Non-finite homing target in all_bullets_replace_homing_targets_with_new_target_array at array index " + String::num_int64(k) + ". Nothing was changed.");
				return;
			}
		}

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (i < 0 || i >= amount_bullets || !all_bullets_enabled_set.contains(i)) {
				continue;
			}
			auto &queue = all_bullet_homing_targets[i];
			auto &count = all_homing_count[i];
			active_homing_count -= count;
			if (active_homing_count < 0) {
				active_homing_count = 0;
			}
			count = 0;
			queue.clear_homing_targets(cached_mouse_global_position);
		}
		all_bullets_push_back_homing_targets_array(node2ds_or_global_positions_array, bullet_index_start, bullet_index_end_inclusive);
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			orbit_keep_lock_across_replace_for_bullet(i, all_bullet_homing_targets[i]);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_mouse(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_mouse");

		// Cache once for the whole loop
		cached_mouse_global_position = get_global_mouse_position();

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (i < 0 || i >= amount_bullets || !all_bullets_enabled_set.contains(i)) {
				continue;
			}
			auto &queue = all_bullet_homing_targets[i];
			auto &count = all_homing_count[i];
			active_homing_count -= count;
			if (active_homing_count < 0) {
				active_homing_count = 0;
			}
			count = 0;
			queue.clear_homing_targets(cached_mouse_global_position);
			if (all_bullet_homing_targets[i].push_back_mouse_position_target(cached_mouse_global_position)) {
				++all_homing_count[i];
				++active_homing_count;
			}
			orbit_keep_lock_across_replace_for_bullet(i, all_bullet_homing_targets[i]);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_assign_homing_targets_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_assign_homing_targets_array");

		const int range_size = bullet_index_end_inclusive - bullet_index_start + 1;
		if (node2ds_or_global_positions_array.size() != range_size) {
			UtilityFunctions::push_error("all_bullets_assign_homing_targets_array: targets array size must match the bullet range size. Nothing pushed.");
			return;
		}

		// Validate every element first so a bad entry can't leave a half-assigned range behind.
		for (int k = 0; k < range_size; ++k) {
			const Variant &target = node2ds_or_global_positions_array[k];
			if (Object::cast_to<Node2D>(target) == nullptr && target.get_type() != Variant::VECTOR2) {
				UtilityFunctions::push_error("Invalid homing target type in all_bullets_assign_homing_targets_array at array index " + String::num_int64(k) + ". Use Node2D or Vector2 entries. Nothing pushed.");
				return;
			}
			if (target.get_type() == Variant::VECTOR2 && !Vector2(target).is_finite()) {
				UtilityFunctions::push_error("Non-finite homing target in all_bullets_assign_homing_targets_array at array index " + String::num_int64(k) + ". Nothing pushed.");
				return;
			}
		}

		// Element i of the array goes to bullet (start + i), pushed to the back.
		// Disabled slots are skipped silently (bulk path): the push would
		// otherwise error per bullet on every retarget pass.
		for (int k = 0; k < range_size; ++k) {
			const int bi = bullet_index_start + k;
			if (bi < 0 || bi >= amount_bullets || !all_bullets_enabled_set.contains(bi)) {
				continue;
			}
			bullet_homing_push_back_homing_target(bi, node2ds_or_global_positions_array[k]);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_clear_homing_targets(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_clear_homing_targets");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_clear_homing_targets(i);
		}
	}

	_ALWAYS_INLINE_ int bullet_homing_check_targets_amount(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_homing_check_targets_amount")) {
			return 0;
		}

		return all_bullet_homing_targets[bullet_index].get_homing_targets_amount();
	}

	_ALWAYS_INLINE_ bool bullet_check_has_homing_targets(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_check_has_homing_targets")) {
			return false;
		}

		return all_bullet_homing_targets[bullet_index].has_homing_targets();
	}

	_ALWAYS_INLINE_ HomingType bullet_homing_check_current_target_type(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_homing_check_current_target_type")) {
			return HomingType::NotHoming;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		return queue.get_current_target_type();
	}

	_ALWAYS_INLINE_ Variant bullet_get_current_homing_target(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_current_homing_target") || !bullet_check_has_homing_targets(bullet_index)) {
			return nullptr;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		return queue.get_current_homing_target();
	}

	//////////////////////////////

	// SHARED BULLET HOMING DEQUE POP METHODS
	// NOTE: the deferred coalesced auto-pop (_do_shared_auto_pop_front_target)
	// is stamped with the volley generation only. A manual clear/pop/push
	// between the queue and the flush therefore races it: keep manual edits and
	// auto-pop apart in the same frame (or re-push after the flush), otherwise
	// the stale pop can eat the fresh front target.

	_ALWAYS_INLINE_ Variant shared_homing_deque_pop_front_target() {
		Variant popped = shared_homing_deque.pop_front_target(cached_mouse_global_position);
		orbit_route_shared_front_change();
		return popped;
	}

	_ALWAYS_INLINE_ Variant shared_homing_deque_pop_back_target() {
		Variant popped = shared_homing_deque.pop_back_target(cached_mouse_global_position);
		if (shared_homing_deque.empty()) {
			// The sole (= front) element is gone: the routing below resets
			// the dangling front pointers (via reset) and unlocks per
			// policy, so the next push starts every bullet fresh.
			orbit_route_shared_front_change();
		}
		// Non-empty back-pop leaves the front untouched: reached flags and
		// locks both stay, nothing to route.
		return popped;
	}

	// SHARED BULLET HOMING DEQUE PUSH METHODS
	// Push-front always swaps the front target, so every bullet is re-armed for
	// it. Push-back only re-arms when the deque was empty (that push creates
	// the front); otherwise the front is unchanged and fired flags must stay.
	// A push onto a volley with zero enabled bullets is rejected: the tick
	// never moves disabled slots, so queuing there only inflates the shared
	// state (and the global mouse counter) with targets that never drain.

	_ALWAYS_INLINE_ bool orbit_reject_fully_disabled_volley(const char *function_name) const {
		for (int k = 0; k < amount_bullets; ++k) {
			if (all_bullets_enabled_set.contains(k)) {
				return false;
			}
		}
		UtilityFunctions::push_error(String(function_name) + ": volley has no enabled bullets. Wake a bullet with enable_bullet() first.");
		return true;
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_mouse_position_target() {
		// Always refresh on push (see bullet_homing_push_front_mouse_position_target).
		cached_mouse_global_position = get_global_mouse_position();

		if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_front_mouse_position_target")) {
			return;
		}
		if (shared_homing_deque.push_front_mouse_position_target(cached_mouse_global_position)) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_node2d_target(Node2D *new_homing_target) {
		if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_front_node2d_target")) {
			return;
		}
		const int before = shared_homing_deque.get_homing_targets_amount();
		shared_homing_deque.push_front_node2d_target(new_homing_target);
		if (shared_homing_deque.get_homing_targets_amount() != before) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_global_position_target(const Vector2 &global_position) {
		if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_front_global_position_target")) {
			return;
		}
		if (shared_homing_deque.push_front_global_position_target(global_position)) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_mouse_position_target() {
		// Always refresh on push (see bullet_homing_push_front_mouse_position_target).
		cached_mouse_global_position = get_global_mouse_position();

		if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_back_mouse_position_target")) {
			return;
		}
		const bool was_empty = shared_homing_deque.empty();
		if (shared_homing_deque.push_back_mouse_position_target(cached_mouse_global_position) && was_empty) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_node2d_target(Node2D *new_homing_target) {
		if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_back_node2d_target")) {
			return;
		}
		const bool was_empty = shared_homing_deque.empty();
		if (shared_homing_deque.push_back_node2d_target(new_homing_target) && was_empty && !shared_homing_deque.empty()) {
			reset_shared_homing_reached_state();
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_global_position_target(const Vector2 &global_position) {
		if (orbit_reject_fully_disabled_volley("shared_homing_deque_push_back_global_position_target")) {
			return;
		}
		const bool was_empty = shared_homing_deque.empty();
		if (shared_homing_deque.push_back_global_position_target(global_position) && was_empty) {
			reset_shared_homing_reached_state();
		}
	}

	////////////////////////////////////

	/// SHARED BULLET HOMING DEQUE HELPER METHODS

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_homing_targets_array(const Array &node2ds_or_global_positions_array) {
		for (const Variant &target : node2ds_or_global_positions_array) {
			Node2D *node2d_target = Object::cast_to<Node2D>(target);

			if (node2d_target) {
				shared_homing_deque_push_back_node2d_target(node2d_target);
			} else if (target.get_type() == Variant::VECTOR2) {
				shared_homing_deque_push_back_global_position_target(target);
			} else {
				UtilityFunctions::push_error("Invalid homing target type in shared_homing_deque_push_back_homing_targets_array. Use Node2D or Vector2 entries.");
			}
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_homing_targets_array(const Array &node2ds_or_global_positions_array) {
		for (const Variant &target : node2ds_or_global_positions_array) {
			Node2D *node2d_target = Object::cast_to<Node2D>(target);

			if (node2d_target) {
				shared_homing_deque_push_front_node2d_target(node2d_target);
			} else if (target.get_type() == Variant::VECTOR2) {
				shared_homing_deque_push_front_global_position_target(target);
			} else {
				UtilityFunctions::push_error("Invalid homing target type in shared_homing_deque_push_front_homing_targets_array. Use Node2D or Vector2 entries.");
			}
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_clear_homing_targets() {
		shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
		reset_shared_homing_reached_state();
		// Unlatch a queued-then-cleared auto-pop: without this the stale
		// flush pops whatever is pushed next, eating a fresh front target.
		shared_auto_pop_queued = false;
		orbit_unlock_on_empty_deque();
	}

	// Replace = clear + push, but locks must survive the intermediate empty
	// deque under StayLocked/RelockOnTargetChange: validate first (invalid
	// input keeps the old queue AND the old lock), then re-pin surviving
	// locks onto the fresh front instead of dropping them.
	_ALWAYS_INLINE_ void shared_homing_deque_replace_homing_targets_with_new_target(const Variant &node2d_or_global_position) {
		Node2D *node2d_target = Object::cast_to<Node2D>(node2d_or_global_position);

		if (node2d_target) {
			shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
			reset_shared_homing_reached_state();
			shared_homing_deque_push_back_node2d_target(node2d_target);
			orbit_keep_lock_across_replace(shared_homing_deque);
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			if (!Vector2(node2d_or_global_position).is_finite()) {
				UtilityFunctions::push_error("Non-finite homing target passed to shared_homing_deque_replace_homing_targets_with_new_target. Nothing was changed.");
				return;
			}
			shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
			reset_shared_homing_reached_state();
			shared_homing_deque_push_back_global_position_target(node2d_or_global_position);
			orbit_keep_lock_across_replace(shared_homing_deque);
		} else {
			UtilityFunctions::push_error("Invalid type passed to shared_homing_deque_replace_homing_targets_with_new_target");
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array) {
		for (auto &target : node2ds_or_global_positions_array) {
			if (Object::cast_to<Node2D>(target) == nullptr && target.get_type() != Variant::VECTOR2) {
				UtilityFunctions::push_error("Invalid type passed to shared_homing_deque_replace_homing_targets_with_new_target_array. Nothing was changed.");
				return;
			}
			if (target.get_type() == Variant::VECTOR2 && !Vector2(target).is_finite()) {
				UtilityFunctions::push_error("Non-finite homing target passed to shared_homing_deque_replace_homing_targets_with_new_target_array. Nothing was changed.");
				return;
			}
		}
		shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
		reset_shared_homing_reached_state();

		for (auto &target : node2ds_or_global_positions_array) {
			Node2D *node2d_target = Object::cast_to<Node2D>(target);

			if (node2d_target) {
				shared_homing_deque_push_back_node2d_target(node2d_target);
			} else if (target.get_type() == Variant::VECTOR2) {
				shared_homing_deque_push_back_global_position_target(target);
			} else {
				UtilityFunctions::push_error("Invalid type passed to shared_homing_deque_replace_homing_targets_with_new_target_array");
			}
		}
		orbit_keep_lock_across_replace(shared_homing_deque);
	}

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
	_ALWAYS_INLINE_ void orbit_reflect_teleport(int bullet_index, const Vector2 &p_shift_delta) {
		(void)p_shift_delta;
		if (bullet_index < 0 || bullet_index >= (int)all_orbiting_data.size() || bullet_index >= (int)all_orbiting_status.size()) {
			return;
		}
		if (all_orbiting_status[bullet_index] == 0) {
			return;
		}
		OrbitingData &o = all_orbiting_data[bullet_index];
		if (!o.is_locked_orbiting) {
			return;
		}
		if (bullet_index >= (int)all_cached_instance_origin.size()) {
			return;
		}
		// Ring center from the same policy the tick uses: unlocked fallback
		// is live target, locked uses the effective (possibly pinned) center.
		const HomingTargetDeque *live_deque = nullptr;
		Vector2 center = o.locked_center;
		if (orbit_live_deque_for_bullet(bullet_index, live_deque) && live_deque != nullptr && !live_deque->empty()) {
			center = orbit_effective_center(o, live_deque->get_cached_front_target_global_position());
		}
		const Vector2 offset = all_cached_instance_origin[bullet_index] - center;
		if (offset.length_squared() < 0.00000001 || !offset.is_finite()) {
			return;
		}
		o.angle = offset.angle();
		// Radius follows an explicit user move: without this a teleport far
		// off-ring pulls the bullet back on the next tick. The stored radius
		// setting is left alone; only the live slot re-aims.
		o.locked_center = center;
	}

	// Teleports a bullet to a new global position. A locked orbit re-aims
	// its ring slot from the new offset (same angle convention as the lock
	// moment) instead of snapping the bullet back next tick: teleporting is
	// an explicit user move, not target motion.
	_ALWAYS_INLINE_ void teleport_bullet(int bullet_index, const Vector2 &new_global_pos) {
		if (!validate_bullet_index(bullet_index, "teleport_bullet")) {
			return;
		}

		// Non-finite input would permanently poison the cached transform, the multimesh
		// instance and the physics shape with no recovery API - reject like bullet_set_velocity does.
		if (!new_global_pos.is_finite()) {
			UtilityFunctions::push_error("teleport_bullet: new_global_pos must be finite (NaN/Inf is rejected).");
			return;
		}

		if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
			return;
		}

		auto &curr_bullet_transf = all_cached_instance_transforms[bullet_index];
		auto &curr_bullet_origin = all_cached_instance_origin[bullet_index];

		const Vector2 origin_delta = new_global_pos - curr_bullet_origin;
		curr_bullet_origin = new_global_pos;
		curr_bullet_transf.set_origin(curr_bullet_origin);

		sync_shape_transform_from_instance(bullet_index, curr_bullet_transf);

		// Instantly apply the updated transforms
		if (all_bullets_enabled_set.contains(bullet_index)) {
			multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(curr_bullet_transf));
		}

		if (bullet_index < (int)attachments.size() && bullet_index < (int)attachment_transforms.size() && bullet_index < (int)attachment_stick_relative_to_bullet.size() && attachments[bullet_index]) {
			BulletAttachment2D *attachment_instance = attachments[bullet_index];

			// Same carry policy as set_bullet_transform: stick-relative attachments
			// recompute from the new transform, non-stick ones shift by the jump.
			Transform2D att_global_transf;
			if (attachment_stick_relative_to_bullet[bullet_index]) {
				att_global_transf = calculate_attachment_global_transf(bullet_index, curr_bullet_transf);
			} else {
				att_global_transf = attachment_transforms[bullet_index].translated(origin_delta);
			}

			// Update the cache
			attachment_transforms[bullet_index] = att_global_transf;

			// Move the actual Node
			attachment_instance->set_global_transform(att_global_transf);

			// Reset Godot's internal engine interpolation
			attachment_instance->reset_physics_interpolation();
		}

		// Reset physics interpolation data
		update_bullet_previous_transform_for_interpolation(bullet_index);

		// A teleport moves the bullet, not the target: re-aim the locked
		// ring slot from the new offset so the next tick holds the new
		// position instead of pulling the bullet back to the old slot.
		orbit_reflect_teleport(bullet_index, origin_delta);
	}

	// Shifts a bullet's position by a certain amount. Same locked-ring
	// re-aim as teleport_bullet (see above).
	_ALWAYS_INLINE_ void teleport_shift_bullet(int bullet_index, const Vector2 &shift_amount) {
		if (!validate_bullet_index(bullet_index, "teleport_shift_bullet")) {
			return;
		}

		if (!shift_amount.is_finite()) {
			UtilityFunctions::push_error("teleport_shift_bullet: shift_amount must be finite (NaN/Inf is rejected).");
			return;
		}

		if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
			return;
		}

		auto &curr_bullet_transf = all_cached_instance_transforms[bullet_index];
		auto &curr_bullet_origin = all_cached_instance_origin[bullet_index];

		curr_bullet_origin += shift_amount;
		curr_bullet_transf.set_origin(curr_bullet_origin);

		sync_shape_transform_from_instance(bullet_index, curr_bullet_transf);

		// Instantly apply the updated transforms
		if (all_bullets_enabled_set.contains(bullet_index)) {
			multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(curr_bullet_transf));
		}

		if (bullet_index < (int)attachments.size() && bullet_index < (int)attachment_transforms.size() && bullet_index < (int)attachment_stick_relative_to_bullet.size() && attachments[bullet_index]) {
			BulletAttachment2D *attachment_instance = attachments[bullet_index];

			// Same carry policy as set_bullet_transform: stick-relative attachments
			// recompute from the new transform, non-stick ones shift by the jump.
			Transform2D att_global_transf;
			if (attachment_stick_relative_to_bullet[bullet_index]) {
				att_global_transf = calculate_attachment_global_transf(bullet_index, curr_bullet_transf);
			} else {
				att_global_transf = attachment_transforms[bullet_index].translated(shift_amount);
			}

			// Update the cache
			attachment_transforms[bullet_index] = att_global_transf;

			// Move the actual Node
			attachment_instance->set_global_transform(att_global_transf);

			// Reset Godot's internal engine interpolation
			attachment_instance->reset_physics_interpolation();
		}

		// Reset physics interpolation data
		update_bullet_previous_transform_for_interpolation(bullet_index);

		orbit_reflect_teleport(bullet_index, shift_amount);
	}

	_ALWAYS_INLINE_ void teleport_shift_all_bullets(const Vector2 &shift_amount, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "teleport_shift_all_bullets");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			teleport_shift_bullet(i, shift_amount);
		}
	}

	// Sets a bullet's velocity directly (wind, knockback, split inheritance).
	// Decomposes into direction + speed so the per-tick integrator
	// (velocity = direction * speed + inherited offset) keeps producing exactly
	// this velocity. max_speed is raised when below the new speed so the next
	// tick doesn't snap it back down. Non-finite input is rejected.
	_ALWAYS_INLINE_ void bullet_set_velocity(int bullet_index, const Vector2 &new_velocity) {
		if (!validate_bullet_index(bullet_index, "bullet_set_velocity")) {
			return;
		}

		if (!new_velocity.is_finite()) {
			UtilityFunctions::push_error("bullet_set_velocity: new_velocity must be finite.");
			return;
		}
		// Near-zero or singular basis = degenerate transform (e.g. zero-scale
		// teleport): normalizing it would silently stall the bullet at the
		// inherited offset. A (0, 1) scale passes a columns[0] length check
		// but has determinant 0, so centralise on the invertibility check.
		// Reject like set_bullet_transform does instead.
		if (!is_transform_invertible_safe(all_cached_instance_transforms[bullet_index])) {
			UtilityFunctions::push_error("bullet_set_velocity: bullet transform is degenerate (zero or singular scale), direction is undefined. Fix the transform first (set_bullet_transform).");
			return;
		}

		if (shared_bullet_curves_data.is_valid() && shared_bullet_curves_data->movement_speed_curve.is_valid()) {
			UtilityFunctions::push_warning("You are trying to set bullet velocity directly while having a movement speed curve assigned to the shared curves data. The curve will override any direct velocity changes. Set the curve to null first if you want to set velocity directly.");
			return;
		}

		BulletCurvesData2D *curves_data = find_bullet_curves_data_ptr(bullet_index);

		if (curves_data != nullptr && curves_data->movement_speed_curve.is_valid()) {
			UtilityFunctions::push_warning("You are trying to set bullet velocity directly while having a movement speed curve assigned as an individual bullet curves data. The curve will override any direct velocity changes. Set the curve to null first if you want to set velocity directly.");
			return;
		}

		const Vector2 without_offset = new_velocity - inherited_velocity_offset;
		const real_t new_speed = without_offset.length();

		if (bullet_index < 0 || bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_speed.size() || bullet_index >= (int)all_cached_max_speed.size() || bullet_index >= (int)all_cached_velocity.size()) {
			return;
		}
		if (new_speed > 0.0001) {
			all_cached_direction[bullet_index] = without_offset / new_speed;
		}

		all_cached_speed[bullet_index] = new_speed;
		if (all_cached_max_speed[bullet_index] < new_speed) {
			all_cached_max_speed[bullet_index] = new_speed;
		}
		all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * new_speed + inherited_velocity_offset;
	}

	_ALWAYS_INLINE_ void all_bullets_set_velocity(const Vector2 &new_velocity, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_velocity");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_velocity(i, new_velocity);
		}
	}

	_ALWAYS_INLINE_ TypedArray<Vector2> all_bullets_get_velocity(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_velocity");

		TypedArray<Vector2> arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (i < 0 || i >= (int)all_cached_velocity.size()) {
				arr.push_back(Vector2());
				continue;
			}
			arr.push_back(all_cached_velocity[i]);
		}

		return arr;
	}

	// Property getters and setters
	real_t get_homing_smoothing() const { return homing_smoothing; }
	void set_homing_smoothing(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("homing_smoothing must be a finite value >= 0 (0 snaps instantly).");
			return;
		}
		homing_smoothing = value;
	}
	real_t get_homing_update_interval() const { return homing_update_interval; }
	void set_homing_update_interval(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("homing_update_interval must be a finite value >= 0 (0 refreshes every tick).");
			return;
		}
		homing_update_interval = value;
	}
	bool get_homing_take_control_of_texture_rotation() const { return homing_take_control_of_texture_rotation; }
	void set_homing_take_control_of_texture_rotation(bool value) { homing_take_control_of_texture_rotation = value; }
	bool get_bullet_homing_auto_pop_after_target_reached() const { return bullet_homing_auto_pop_after_target_reached; }
	void set_bullet_homing_auto_pop_after_target_reached(bool value) { bullet_homing_auto_pop_after_target_reached = value; }
	real_t get_homing_distance_before_reached() const { return homing_distance_before_reached; }
	void set_homing_distance_before_reached(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("homing_distance_before_reached must be a finite value >= 0.");
			return;
		}
		homing_distance_before_reached = value;
	}
	bool get_shared_homing_deque_auto_pop_after_target_reached() const { return shared_homing_deque_auto_pop_after_target_reached; }
	void set_shared_homing_deque_auto_pop_after_target_reached(bool value) { shared_homing_deque_auto_pop_after_target_reached = value; }

	// Per-bullet turn agility. Setting any value enables per-bullet mode, after
	// which update_homing ignores the shared homing_smoothing. Same validation
	// as the shared setter. Reading returns the effective value per bullet.
	real_t bullet_get_homing_smoothing(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_homing_smoothing")) {
			return 0.0;
		}
		if (use_per_bullet_homing_smoothing && bullet_index >= 0 && bullet_index < (int)all_bullet_homing_smoothing.size()) {
			return all_bullet_homing_smoothing[bullet_index];
		}
		return homing_smoothing;
	}
	void bullet_set_homing_smoothing(int bullet_index, real_t value) {
		if (!validate_bullet_index(bullet_index, "bullet_set_homing_smoothing")) {
			return;
		}
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("bullet_set_homing_smoothing: value must be finite and >= 0 (0 snaps instantly).");
			return;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_bullet_homing_smoothing.size()) {
			UtilityFunctions::push_error("bullet_set_homing_smoothing: per-bullet smoothing storage is not set up for this multimesh.");
			return;
		}
		// First per-bullet write seeds every bullet with the shared value so
		// untouched bullets keep steering exactly as before.
		if (!use_per_bullet_homing_smoothing) {
			all_bullet_homing_smoothing.assign(all_bullet_homing_smoothing.size(), homing_smoothing);
		}
		all_bullet_homing_smoothing[bullet_index] = value;
		use_per_bullet_homing_smoothing = true;
	}
	void all_bullets_set_homing_smoothing(real_t value, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_homing_smoothing");
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("all_bullets_set_homing_smoothing: value must be finite and >= 0 (0 snaps instantly).");
			return;
		}
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			if (i < 0 || i >= (int)all_bullet_homing_smoothing.size()) {
				UtilityFunctions::push_error("all_bullets_set_homing_smoothing: per-bullet smoothing storage is not set up for this multimesh.");
				return;
			}
		}
		// First per-bullet write seeds every bullet with the shared value so
		// untouched bullets keep steering exactly as before.
		if (!use_per_bullet_homing_smoothing) {
			all_bullet_homing_smoothing.assign(all_bullet_homing_smoothing.size(), homing_smoothing);
		}
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			all_bullet_homing_smoothing[i] = value;
		}
		use_per_bullet_homing_smoothing = true;
	}
	// Effective per-bullet smoothing over a range (mirrors all_bullets_get_velocity).
	TypedArray<real_t> all_bullets_get_homing_smoothing(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_homing_smoothing");
		TypedArray<real_t> arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_get_homing_smoothing(i));
		}
		return arr;
	}
	// Clears per-bullet smoothing mode: the shared homing_smoothing drives
	// every bullet again. No-op when per-bullet mode was never enabled.
	void clear_per_bullet_homing_smoothing() {
		use_per_bullet_homing_smoothing = false;
	}

	// BOUNCE / RICOCHET RUNTIME API (spawn-data equivalents, editable live).
	int get_bounce_mask() const { return bounce_mask; }
	void set_bounce_mask(int value) {
		if (value < 0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_bounce_mask: value must be >= 0 (0 = bouncing disabled), keeping the old value.");
			return;
		}
		bounce_mask = value;
		bounce_mask_warning_issued = false;
		ensure_bounce_vectors();
	}
	void set_bounce_mask_from_array(const TypedArray<int> &numbers) {
		bounce_mask = DirectionalBulletsData2D::calculate_bitmask(numbers);
		bounce_mask_warning_issued = false;
		ensure_bounce_vectors();
	}
	bool get_bounce_tilemap_layers() const { return bounce_tilemap_layers; }
	void set_bounce_tilemap_layers(bool value) {
		bounce_tilemap_layers = value;
	}
	real_t get_bounce_strength() const { return bounce_strength; }
	void set_bounce_strength(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_bounce_strength: value must be finite and >= 0 (1 = elastic), keeping the old value.");
			return;
		}
		bounce_strength = value;
	}
	bool get_bounce_push_assist() const { return bounce_push_assist; }
	void set_bounce_push_assist(bool value) { bounce_push_assist = value; }
	bool get_bounce_charge_amplify() const { return bounce_charge_amplify; }
	void set_bounce_charge_amplify(bool value) { bounce_charge_amplify = value; }
	bool get_bounce_hit_consumed() const { return bounce_hit_consumed; }
	void set_bounce_hit_consumed(bool value) { bounce_hit_consumed = value; }
	int get_bounce_max_count() const { return bounce_max_count; }
	void set_bounce_max_count(int value) {
		if (value < 0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_bounce_max_count: value must be >= 0 (0 = unlimited), keeping the old value.");
			return;
		}
		bounce_max_count = value;
	}
	int get_bounce_mode() const { return bounce_mode; }
	void set_bounce_mode(int value) {
		if (value != 0 && value != 1) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_bounce_mode: value must be 0 (radial) or 1 (precise shape), keeping the old value.");
			return;
		}
		bounce_mode = value;
	}
	bool get_bounce_rotate_texture() const { return bounce_rotate_texture; }
	void set_bounce_rotate_texture(bool value) { bounce_rotate_texture = value; }
	real_t get_bounce_rotation_smooth() const { return bounce_rotation_smooth; }
	void set_bounce_rotation_smooth(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_bounce_rotation_smooth: value must be finite and >= 0 (0 = instant snap), keeping the old value.");
			return;
		}
		bounce_rotation_smooth = value;
	}
	real_t get_bounce_randomness_deg() const { return bounce_randomness_deg; }
	void set_bounce_randomness_deg(real_t value) {
		if (!Math::is_finite(value) || value < 0.0 || value > 180.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_bounce_randomness_deg: value must be finite in [0, 180], keeping the old value.");
			return;
		}
		bounce_randomness_deg = value;
	}
	real_t get_bounce_cooldown_sec() const { return bounce_cooldown_sec; }
	void set_bounce_cooldown_sec(real_t value) {
		if (!Math::is_finite(value) || value < 0.0 || value > 1.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_bounce_cooldown_sec: value must be finite in [0, 1], keeping the old value.");
			return;
		}
		bounce_cooldown_sec = value;
	}
	real_t get_bounce_debounce_sec() const { return bounce_debounce_sec; }
	void set_bounce_debounce_sec(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_bounce_debounce_sec: value must be finite and >= 0 (0 = off), keeping the old value.");
			return;
		}
		bounce_debounce_sec = value;
	}
	// How many times one bullet has bounced in its current life.
	int bullet_get_bounce_count(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_bounce_count")) {
			return 0;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_bounce_count.size()) {
			return 0;
		}
		return all_bounce_count[bullet_index];
	}
	// Bounce counts over a range (mirrors all_bullets_get_velocity).
	TypedArray<int> all_bullets_get_bounce_count(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_bounce_count");
		TypedArray<int> arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_get_bounce_count(i));
		}
		return arr;
	}
	// Previous-tick origin used by physics interpolation. Lets tests prove
	// the render path stays continuous across teleports and bounces.
	Vector2 debug_get_previous_origin(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "debug_get_previous_origin")) {
			return Vector2(0, 0);
		}
		if (bullet_index < 0 || bullet_index >= (int)all_previous_instance_transf.size()) {
			return Vector2(0, 0);
		}
		return all_previous_instance_transf[bullet_index].get_origin();
	}
	// Bounce introspection for tests/support: counts, config echo, pending
	// visual state. Never mutates.
	Dictionary debug_get_bounce_info(int bullet_index) const {
		Dictionary d;
		d["valid"] = false;
		d["bounce_count"] = 0;
		d["bounce_mask"] = bounce_mask;
		d["bounce_strength"] = bounce_strength;
		d["bounce_hit_consumed"] = bounce_hit_consumed;
		d["bounce_max_count"] = bounce_max_count;
		d["bounce_mode"] = bounce_mode;
		d["bounce_tilemap_layers"] = bounce_tilemap_layers;
		d["bounce_enabled"] = bounce_mask != 0;
		d["visual_pending"] = false;
		d["cooldown"] = 0.0;
		d["last_normal"] = Vector2(0, 0);
		d["last_target_velocity"] = Vector2(0, 0);
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return d;
		}
		d["valid"] = true;
		if (bullet_index >= 0 && bullet_index < (int)all_bounce_count.size()) {
			d["bounce_count"] = all_bounce_count[bullet_index];
		}
		if (bullet_index >= 0 && bullet_index < (int)all_bounce_last_normal.size()) {
			d["last_normal"] = all_bounce_last_normal[bullet_index];
		}
		if (bullet_index >= 0 && bullet_index < (int)all_bounce_last_target_velocity.size()) {
			d["last_target_velocity"] = all_bounce_last_target_velocity[bullet_index];
		}
		if (bullet_index >= 0 && bullet_index < (int)bounce_visual_pending.size()) {
			d["visual_pending"] = bounce_visual_pending[bullet_index] != 0;
		}
		if (bullet_index >= 0 && bullet_index < (int)all_bounce_cooldown.size()) {
			d["cooldown"] = all_bounce_cooldown[bullet_index];
		}
		// Seconds left before this bullet may bounce off the SAME target
		// again (0 when the window is over, disarmed, or never bounced).
		double debounce_left = 0.0;
		if (bounce_debounce_sec > 0.0 && Math::is_finite((double)bounce_debounce_sec)
				&& bullet_index >= 0 && bullet_index < (int)all_bounce_last_target.size()
				&& bullet_index < (int)all_bounce_last_time.size()
				&& all_bounce_last_target[bullet_index] != 0 && Math::is_finite(curves_elapsed_time)) {
			const double anchor = all_bounce_last_time[bullet_index];
			if (Math::is_finite(anchor)) {
				debounce_left = Math::max(0.0, (double)bounce_debounce_sec - (curves_elapsed_time - anchor));
			}
		}
		d["debounce"] = debounce_left;
		return d;
	}
	// Seeds bounce config + zeroes the per-bullet ledger from spawn data.
	// Called from the custom spawn/enable logic alongside wobble/gravity.
	void apply_bounce_from_data(const DirectionalBulletsData2D &directional_data, int data_collision_mask) {
		bounce_mask = directional_data.bounce_mask;
		bounce_tilemap_layers = directional_data.bounce_tilemap_layers;
		bounce_strength = (real_t)directional_data.bounce_strength;
		bounce_push_assist = directional_data.bounce_push_assist;
		bounce_charge_amplify = directional_data.bounce_charge_amplify;
		bounce_hit_consumed = directional_data.bounce_hit_consumed;
		bounce_max_count = directional_data.bounce_max_count;
		bounce_mode = directional_data.bounce_mode;
		bounce_rotate_texture = directional_data.bounce_rotate_texture;
		bounce_rotation_smooth = (real_t)directional_data.bounce_rotation_smooth;
		bounce_randomness_deg = (real_t)directional_data.bounce_randomness_deg;
		bounce_cooldown_sec = (real_t)directional_data.bounce_cooldown_sec;
		bounce_debounce_sec = (real_t)directional_data.bounce_debounce_sec;
		// Fresh life, fresh ledger. assign() both sizes and zeroes when
		// armed; clear() drops the vectors when disarmed so plain volleys
		// carry no bounce state at all.
		if (bounce_enabled()) {
			all_bounce_count.assign(amount_bullets, 0);
			all_bounce_cooldown.assign(amount_bullets, 0.0);
			all_bounce_last_tick.assign(amount_bullets, 0);
			all_bounce_last_target.assign(amount_bullets, 0);
			all_bounce_last_time.assign(amount_bullets, 0.0);
			all_bounce_last_normal.assign(amount_bullets, Vector2(0, 0));
			all_bounce_last_target_velocity.assign(amount_bullets, Vector2(0, 0));
			bounce_visual_pending.assign(amount_bullets, 0);
			bounce_visual_target.assign(amount_bullets, Vector2(1, 0));
			all_bounce_speed_multiplier.assign(amount_bullets, 1.0);
		} else {
			all_bounce_count.clear();
			all_bounce_cooldown.clear();
			all_bounce_last_tick.clear();
			all_bounce_last_target.clear();
			all_bounce_last_time.clear();
			all_bounce_last_normal.clear();
			all_bounce_last_target_velocity.clear();
			bounce_visual_pending.clear();
			bounce_visual_target.clear();
			all_bounce_speed_multiplier.clear();
		}
		bounce_speed_scaled = false;
		bounce_mask_warning_issued = false;
		// The #1 silent misconfiguration: bounce layers the bullet can never
		// detect because its collision_mask does not cover them. Warn once
		// per life instead of bouncing nothing forever.
		if (!bounce_mask_warning_issued && bounce_mask != 0 && (data_collision_mask & bounce_mask) != bounce_mask) {
			UtilityFunctions::push_warning("DirectionalBullets2D: bounce_mask has bits outside collision_mask, those targets will never be detected (no bounce). Add the bounce layers to collision_mask.");
			bounce_mask_warning_issued = true;
		}
	}
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
	void mark_per_bullet_rotation_presence(int bullet_index, bool present) {
		if (bullet_index < 0 || bullet_index >= (int)has_per_bullet_rotation_data.size()) {
			// Grow on demand: the seed loop marks before anything sized the
			// vector, and a late mark must not silently vanish.
			if (bullet_index >= 0 && bullet_index < amount_bullets) {
				has_per_bullet_rotation_data.resize(amount_bullets, 0);
			} else {
				return;
			}
		}
		has_per_bullet_rotation_data[bullet_index] = present ? 1 : 0;
	}
	// Same contract for the linear ballistics SoA (all_cached_speed /
	// max_speed / acceleration): set_bullet_speed_data claims presence for the
	// slot it writes so a later set_shared_bullet_speed_data cannot undo it.
	void mark_per_bullet_speed_presence(int bullet_index, bool present) {
		if (bullet_index < 0 || bullet_index >= (int)has_per_bullet_speed_data.size()) {
			if (bullet_index >= 0 && bullet_index < amount_bullets) {
				has_per_bullet_speed_data.resize(amount_bullets, 0);
			} else {
				return;
			}
		}
		has_per_bullet_speed_data[bullet_index] = present ? 1 : 0;
	}
	// Motion-feature lifecycle (speed SoA, rotation fallback, wobble,
	// gravity/drag, movement patterns, homing/orbit, bounce):
	// seed on a fresh spawn (the volley is not in the tree yet), reseed on a
	// pool reuse (false = refused, the caller rolls back), neutralize without
	// reseeding (drop_stale_work = a new life: scrub connections and
	// invalidate deferred work), and the tail of a full deactivation.
	void seed_motion_features_on_spawn(const DirectionalBulletsData2D &data);
	bool reseed_motion_features_on_enable(const DirectionalBulletsData2D &data);
	void reset_motion_feature_state(bool drop_stale_work);
	void on_volley_deactivated();

	// Resolves the spawn data's shared movement pattern Path2D and applies its
	// Curve2D to every bullet through the existing helpers. Empty path = off.
	void apply_shared_movement_pattern_from_data(const DirectionalBulletsData2D &directional_data);

	// Strict per-bullet indexing (used by every shared-vs-per-bullet
	// spawn-data array: speed, rotation, curves, wobble, gravity, movement
	// pattern paths, custom data, collision counts). Entry i belongs to
	// bullet i and nobody else:
	// size <= 0 → -1 (feature off for the per-bullet side / shared drives),
	// i < size → i, otherwise -1 (fall back to shared, then the default).
	// Longer arrays ignore the extras. Callers warn once per spawn call on
	// size != N (and != 0). Opt-in tiling lives in resolve_tiled_data_index.
	int resolve_strict_data_index(int array_size, int bullet_index) const {
		if (array_size <= 0) {
			return -1;
		}
		if (bullet_index >= 0 && bullet_index < array_size) {
			return bullet_index;
		}
		return -1;
	}
	// Opt-in wrap-around for one array when its tile_* checkbox is checked:
	// slot i reads entry (i % size), so 2 entries fan across 10 bullets as
	// A,B,A,B... Invalid entries still fall back per slot. Empty → -1.
	int resolve_tiled_data_index(int array_size, int bullet_index) const {
		if (array_size <= 0) {
			return -1;
		}
		if (bullet_index < 0) {
			return -1;
		}
		return bullet_index % array_size;
	}
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
	void apply_per_bullet_curves_from_data(const DirectionalBulletsData2D &directional_data);
	void apply_per_bullet_movement_patterns_from_data(const DirectionalBulletsData2D &directional_data);
	void apply_wobble_from_data(const DirectionalBulletsData2D &directional_data);
	void apply_gravity_from_data(const DirectionalBulletsData2D &directional_data);
	// Shared-as-fallback gap fillers (unified precedence: per-bullet wins).
	// Only slots holding invalid ballistics (non-finite speed triple or
	// inactive rotation) are overwritten from shared; valid per-bullet
	// slots are never touched.
	void apply_shared_speed_fallback(const Ref<BulletSpeedData2D> &shared);
	void apply_shared_rotation_fallback(const Ref<BulletRotationData2D> &shared, bool new_rotate_only_textures);

	// Gravity time window over volley life (seconds since spawn, read on
	// curves_elapsed_time): integrates only inside [delay, delay + duration].
	bool gravity_window_open() const {
		if (curves_elapsed_time < gravity_delay_sec) {
			return false;
		}
		if (gravity_duration_sec > 0.0 && curves_elapsed_time >= gravity_delay_sec + gravity_duration_sec) {
			return false;
		}
		return true;
	}

	// Gravity strength scale for one bullet from shared/per-bullet curves
	// (per-bullet wins when valid, like direction curves). Null curves read
	// 1.0; a non-finite sample reads 1.0 (neutral) so a broken curve can
	// never brick or invert the fall.
	real_t gravity_strength_scale_for_bullet(const BulletCurvesData2D *shared, const BulletCurvesData2D *per_bullet) const {
		const BulletCurvesData2D *src = nullptr;
		if (per_bullet != nullptr && per_bullet->gravity_strength_curve.is_valid()) {
			src = per_bullet;
		} else if (shared != nullptr && shared->gravity_strength_curve.is_valid()) {
			src = shared;
		} else {
			return 1.0;
		}
		const bool use_unit = src->gravity_use_unit_curve && !is_life_time_infinite;
		const real_t sampled = src->gravity_strength_curve->sample_baked(curve_get_input_value(use_unit));
		return Math::is_finite(sampled) ? sampled : 1.0;
	}

	// WOBBLE / GRAVITY / DRAG / HOMING-GATE RUNTIME API (spawn-data
	// equivalents, editable live on the instance).
	WobbleSeed make_wobble_seed(const Ref<BulletWobbleData2D> &wobble, int bullet_index) const {
		WobbleSeed seed;
		BulletWobbleData2D *w = wobble.ptr();
		if (w == nullptr || !w->enabled) {
			return seed;
		}
		if (!Math::is_finite(w->amplitude) || w->amplitude < 0.0 || !Math::is_finite(w->frequency_hz) || w->frequency_hz < 0.0) {
			return seed;
		}
		if (!Math::is_finite(w->phase_rad) || !Math::is_finite(w->phase_step_per_bullet) || !Math::is_finite(w->damping_per_sec) || w->damping_per_sec < 0.0) {
			return seed;
		}
		if (!Math::is_finite(w->delay_sec) || w->delay_sec < 0.0 || !Math::is_finite(w->duration_sec) || w->duration_sec < 0.0) {
			return seed;
		}
		if (!Math::is_finite(w->face_rotation_speed) || w->face_rotation_speed < 0.0) {
			return seed;
		}
		seed.active = true;
		seed.mode = (w->mode == BulletWobbleData2D::WOBBLE_ANGULAR) ? 1 : 0;
		seed.waveform = (w->waveform == BulletWobbleData2D::WOBBLE_COSINE) ? 1 : 0;
		seed.amplitude = w->amplitude;
		seed.frequency_hz = w->frequency_hz;
		seed.phase = w->phase_rad + w->phase_step_per_bullet * (real_t)bullet_index;
		seed.distance_phased = w->distance_phased;
		seed.damping_per_sec = w->damping_per_sec;
		seed.delay_sec = w->delay_sec;
		seed.duration_sec = w->duration_sec;
		seed.face_movement_direction = w->face_movement_direction;
		seed.face_rotation_speed = w->face_rotation_speed;
		return seed;
	}
	void refresh_wobble_feature_flag() {
		is_wobble_feature_enabled = false;
		for (const auto &w : all_bullet_wobble) {
			if (w.active) {
				is_wobble_feature_enabled = true;
				break;
			}
		}
		if (!is_wobble_feature_enabled) {
			wobble_distance_traveled.assign(amount_bullets, 0.0);
		}
	}
	bool get_is_wobble_enabled() const { return is_wobble_feature_enabled; }
	// Effective wobble amplitude of one bullet (seeded amplitude, 0 when inactive).
	real_t bullet_get_wobble_amplitude(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_wobble_amplitude")) {
			return 0.0;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_bullet_wobble.size()) {
			return 0.0;
		}
		const WobbleSeed &w = all_bullet_wobble[bullet_index];
		return w.active ? w.amplitude : 0.0;
	}
	// Effective per-channel curves winner for one bullet: "per", "shared",
	// or "none". Lets you prove which resource drives x / y / rotation /
	// speed / gravity-strength without reading the tick.
	Dictionary debug_get_curves_info(int bullet_index) const {
		Dictionary d;
		d["valid"] = false;
		d["x_src"] = "none";
		d["y_src"] = "none";
		d["rot_src"] = "none";
		d["speed_src"] = "none";
		d["gravity_src"] = "none";
		d["has_per_bullet_resource"] = false;
		d["has_shared_fallback"] = shared_bullet_curves_data.is_valid();
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return d;
		}
		d["valid"] = true;
		const BulletCurvesData2D *per = (bullet_index >= 0 && bullet_index < (int)all_bullet_curves_data.size() && all_bullet_curves_data[bullet_index].is_valid()) ? all_bullet_curves_data[bullet_index].ptr() : nullptr;
		const BulletCurvesData2D *shared = shared_bullet_curves_data.is_valid() ? shared_bullet_curves_data.ptr() : nullptr;
		d["has_per_bullet_resource"] = per != nullptr;
		auto pick = [](const BulletCurvesData2D *p, const BulletCurvesData2D *s, bool p_has, bool s_has) -> String {
			if (p_has) {
				return "per";
			}
			if (s_has) {
				return "shared";
			}
			(void)p;
			(void)s;
			return "none";
		};
		d["x_src"] = pick(per, shared, per != nullptr && per->x_direction_curve.is_valid(), shared != nullptr && shared->x_direction_curve.is_valid());
		d["y_src"] = pick(per, shared, per != nullptr && per->y_direction_curve.is_valid(), shared != nullptr && shared->y_direction_curve.is_valid());
		d["rot_src"] = pick(per, shared, per != nullptr && per->rotation_speed_curve.is_valid(), shared != nullptr && shared->rotation_speed_curve.is_valid());
		d["speed_src"] = pick(per, shared, per != nullptr && per->movement_speed_curve.is_valid(), shared != nullptr && shared->movement_speed_curve.is_valid());
		d["gravity_src"] = pick(per, shared, per != nullptr && per->gravity_strength_curve.is_valid(), shared != nullptr && shared->gravity_strength_curve.is_valid());
		return d;
	}
	// Effective movement-pattern state for one bullet: which side drives it,
	// its flags, and how far along it is. Finished shared parks at length;
	// finished per-bullet clears to no-pattern.
	Dictionary debug_get_pattern_info(int bullet_index) const {
		Dictionary d;
		d["valid"] = false;
		d["src"] = "none";
		d["face"] = false;
		d["repeat"] = true;
		d["distance"] = 0.0;
		d["length"] = 0.0;
		d["finished"] = false;
		d["has_shared_fallback"] = shared_movement_pattern_curve.is_valid();
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return d;
		}
		d["valid"] = true;
		if (bullet_index >= 0 && bullet_index < (int)all_movement_pattern_data.size() && all_movement_pattern_data[bullet_index].path_curve.is_valid()) {
			const auto &p = all_movement_pattern_data[bullet_index];
			d["src"] = "per";
			d["face"] = p.face_movement_direction;
			d["repeat"] = p.repeat_pattern;
			d["distance"] = p.distance_traveled;
			const real_t len = p.path_curve->get_baked_length();
			d["length"] = Math::is_finite(len) ? len : 0.0;
			d["finished"] = false;
			return d;
		}
		if (shared_movement_pattern_curve.is_valid() && bullet_index >= 0 && bullet_index < (int)shared_movement_pattern_distances.size()) {
			const real_t len = shared_movement_pattern_curve->get_baked_length();
			const real_t safe_len = Math::is_finite(len) ? len : 0.0;
			const real_t dist = shared_movement_pattern_distances[bullet_index];
			d["src"] = "shared";
			d["face"] = shared_movement_pattern_face_movement_direction;
			d["repeat"] = shared_movement_pattern_repeat;
			d["distance"] = dist;
			d["length"] = safe_len;
			d["finished"] = !shared_movement_pattern_repeat && safe_len >= 0.001 && dist >= safe_len;
			return d;
		}
		return d;
	}
	// Effective wobble face flag of one bullet (seeded value; false when
	// inactive). Use debug_get_wobble_info for the full seed incl. mode,
	// waveform, phase, damping, delay and duration.
	bool bullet_get_wobble_face_movement_direction(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_wobble_face_movement_direction")) {
			return false;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_bullet_wobble.size()) {
			return false;
		}
		return all_bullet_wobble[bullet_index].active && all_bullet_wobble[bullet_index].face_movement_direction;
	}
	real_t bullet_get_wobble_face_rotation_speed(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_wobble_face_rotation_speed")) {
			return 0.0;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_bullet_wobble.size()) {
			return 0.0;
		}
		const WobbleSeed &w = all_bullet_wobble[bullet_index];
		return w.active ? w.face_rotation_speed : 0.0;
	}
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
	void set_gravity(const Vector2 &value) {
		if (!value.is_finite()) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_gravity: value must be finite, keeping the old value.");
			return;
		}
		gravity = value;
		// Fill-gaps semantics (same contract as the speed/rotation shared
		// setters): slots with a user-authored per-bullet entry keep it;
		// only genuine gaps take the shared value. Changed slots restart
		// their integrated fall speed (new regime); untouched slots keep
		// integrating (no regime change for them). A slot that took the
		// shared value stays a gap (bit clear): the NEXT shared write must
		// still reach it, otherwise only the first set_gravity() works.
		// Same-value writes only refresh the member, never the fall speed.
		if ((int)has_per_bullet_gravity.size() != amount_bullets) {
			has_per_bullet_gravity.assign(amount_bullets, 0);
		}
		if ((int)all_gravity.size() != amount_bullets || (int)all_gravity_velocity.size() != amount_bullets) {
			all_gravity.assign(amount_bullets, Vector2(0, 0));
			all_gravity_velocity.assign(amount_bullets, Vector2(0, 0));
		}
		for (int i = 0; i < amount_bullets; ++i) {
			if (has_per_bullet_gravity[i]) {
				continue;
			}
			if (all_gravity[i] != value) {
				all_gravity_velocity[i] = Vector2(0, 0);
			}
			all_gravity[i] = value;
		}
		refresh_gravity_active();
	}
	Vector2 bullet_get_gravity(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_gravity")) {
			return Vector2(0, 0);
		}
		if (bullet_index < 0 || bullet_index >= (int)all_gravity.size()) {
			return Vector2(0, 0);
		}
		return all_gravity[bullet_index];
	}
	void bullet_set_gravity(int bullet_index, const Vector2 &value) {
		if (!validate_bullet_index(bullet_index, "bullet_set_gravity")) {
			return;
		}
		if (!value.is_finite()) {
			UtilityFunctions::push_error("DirectionalBullets2D.bullet_set_gravity: value must be finite, keeping the old value.");
			return;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_gravity.size() || bullet_index >= (int)all_gravity_velocity.size()) {
			return;
		}
		all_gravity[bullet_index] = value;
		all_gravity_velocity[bullet_index] = Vector2(0, 0);
		// A direct per-bullet write claims presence like a seeded entry, so
		// a later set_gravity() cannot silently undo it.
		if ((int)has_per_bullet_gravity.size() != amount_bullets) {
			has_per_bullet_gravity.assign(amount_bullets, 0);
		}
		has_per_bullet_gravity[bullet_index] = 1;
		refresh_gravity_active();
	}
	void all_bullets_set_gravity(const Vector2 &value, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_gravity");
		if (!value.is_finite()) {
			UtilityFunctions::push_error("DirectionalBullets2D.all_bullets_set_gravity: value must be finite, keeping old values.");
			return;
		}
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_gravity(i, value);
		}
	}
	// Per-bullet gravity vectors over a range (mirrors all_bullets_get_velocity).
	TypedArray<Vector2> all_bullets_get_gravity(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_gravity");
		TypedArray<Vector2> arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_get_gravity(i));
		}
		return arr;
	}
	double get_gravity_delay_sec() const { return gravity_delay_sec; }
	void set_gravity_delay_sec(double value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_gravity_delay_sec: value must be finite and >= 0, keeping the old value.");
			return;
		}
		gravity_delay_sec = value;
	}
	double get_gravity_duration_sec() const { return gravity_duration_sec; }
	void set_gravity_duration_sec(double value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_gravity_duration_sec: value must be finite and >= 0 (0 = infinite), keeping the old value.");
			return;
		}
		gravity_duration_sec = value;
	}
	// Fall speed magnitude of one bullet (integrated fall velocity length).
	// 0 when gravity never integrated for that slot.
	real_t bullet_get_fall_speed(int bullet_index) const {
		if (!validate_bullet_index(bullet_index, "bullet_get_fall_speed")) {
			return 0.0;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_gravity_velocity.size()) {
			return 0.0;
		}
		return all_gravity_velocity[bullet_index].length();
	}
	// Fall speeds over a range (mirrors all_bullets_get_velocity).
	TypedArray<real_t> all_bullets_get_fall_speed(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_fall_speed");
		TypedArray<real_t> arr;
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_get_fall_speed(i));
		}
		return arr;
	}
	// Full per-bullet steering snapshot for tests/support: {index, active,
	// direction, velocity, speed, gravity, fall_speed, wobble_active,
	// wobble_amplitude, has_homing_targets, homing_targets_amount,
	// homing_smoothing, orbiting_enabled, orbiting_locked, pattern,
	// shared_pattern_active, rotation_speed}. Never mutates.
	Dictionary debug_get_bullet_info(int bullet_index) const;
	real_t get_linear_drag() const { return linear_drag; }
	void set_linear_drag(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_linear_drag: value must be finite and >= 0, keeping the old value.");
			return;
		}
		linear_drag = value;
	}
	real_t get_homing_delay_sec() const { return homing_delay_sec; }
	void set_homing_delay_sec(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_homing_delay_sec: value must be finite and >= 0, keeping the old value.");
			return;
		}
		homing_delay_sec = value;
	}
	real_t get_homing_duration_sec() const { return homing_duration_sec; }
	void set_homing_duration_sec(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_homing_duration_sec: value must be finite and >= 0 (0 = infinite), keeping the old value.");
			return;
		}
		homing_duration_sec = value;
	}
	real_t get_homing_lose_range_px() const { return homing_lose_range_px; }
	void set_homing_lose_range_px(real_t value) {
		if (!Math::is_finite(value) || value < 0.0) {
			UtilityFunctions::push_error("DirectionalBullets2D.set_homing_lose_range_px: value must be finite and >= 0 (0 = unlimited), keeping the old value.");
			return;
		}
		homing_lose_range_px = value;
	}

	// SHARED MOVEMENT PATTERN RUNTIME API (spawn-data equivalent, editable live
	// on the instance; per-bullet helpers live with the other pattern API). Unified
	// precedence: per-bullet patterns win per bullet; the shared curve is
	// the broadcast fallback for bullets without their own pattern.
	// Clearing it (null curve) removes the fallback.
	Ref<Curve2D> get_shared_movement_pattern_curve() const { return shared_movement_pattern_curve; }
	void set_shared_movement_pattern_curve(const Ref<Curve2D> &new_curve) {
		shared_movement_pattern_curve = new_curve;
		// A fresh pattern starts every bullet at distance 0. A null curve
		// removes the feature: flags go back to defaults, matching the
		// spawn-data null handling.
		if (new_curve.is_null()) {
			shared_movement_pattern_face_movement_direction = false;
			shared_movement_pattern_repeat = true;
		}
		shared_movement_pattern_distances.assign(amount_bullets, 0.0);
	}

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
	void set_shared_bullet_speed_data(const Ref<BulletSpeedData2D> &new_speed_data) {
		shared_bullet_speed_data = new_speed_data;
		if (new_speed_data.is_null()) {
			return;
		}
		apply_shared_speed_fallback(new_speed_data);
	}
	bool has_shared_bullet_speed_data() const { return shared_bullet_speed_data.is_valid(); }
	void remove_shared_bullet_speed_data() { set_shared_bullet_speed_data(Ref<BulletSpeedData2D>()); }

	Ref<BulletRotationData2D> get_shared_bullet_rotation_data() const { return shared_bullet_rotation_data; }
	void set_shared_bullet_rotation_data(const Ref<BulletRotationData2D> &new_rotation_data) {
		shared_bullet_rotation_data = new_rotation_data;
		if (new_rotation_data.is_null()) {
			return;
		}
		apply_shared_rotation_fallback(new_rotation_data, rotate_only_textures);
	}
	bool has_shared_bullet_rotation_data() const { return shared_bullet_rotation_data.is_valid(); }
	void remove_shared_bullet_rotation_data() { set_shared_bullet_rotation_data(Ref<BulletRotationData2D>()); }

	bool get_adjust_direction_based_on_rotation() const { return adjust_direction_based_on_rotation; }
	void set_adjust_direction_based_on_rotation(bool value) { adjust_direction_based_on_rotation = value; }

	// Teardown hook: drop every homing target (per-bullet + shared) through the same
	// clear helpers the enable path uses, so the global mouse-target counter can't leak
	// when a multimesh dies holding mouse targets.
	void clear_homing_state_for_teardown() {
		for (auto &queue : all_bullet_homing_targets) {
			queue.clear_homing_targets(cached_mouse_global_position);
		}
		all_homing_count.assign(all_homing_count.size(), 0);
		active_homing_count = 0;
		shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
		reset_shared_homing_reached_state();
		// A pooled instance must not carry runtime homing/orbit setup into the
		// next owner. Mirrors custom_additional_enable_logic so an enable_bullet()
		// wake (which skips that path) starts from the same blank state.
		all_bullet_homing_smoothing.assign(all_bullet_homing_smoothing.size(), 0.0);
		use_per_bullet_homing_smoothing = false;
		// Value-reset the whole orbit payload, not just the lock: a stale
		// radius/direction/angle must never ride into the next life.
		for (auto &o : all_orbiting_data) {
			o = OrbitingData();
		}
		all_orbiting_status.assign(all_orbiting_status.size(), 0);
		active_orbiting_count = 0;
		homing_update_interval = 0.0;
		homing_update_timer = 0.0;
		homing_smoothing = 0.0;
		homing_take_control_of_texture_rotation = false;
		homing_distance_before_reached = 5.0;
		bullet_homing_auto_pop_after_target_reached = false;
		shared_homing_deque_auto_pop_after_target_reached = false;
		adjust_direction_based_on_rotation = false;
		homing_inert_warning_issued = false;
		cached_mouse_global_position = Vector2(0, 0);
		// A queued-then-invalidated shared auto-pop must not stay latched:
		// the drain bumps homing_operation_generation so the pop no-ops, and
		// a later manual wake (which skips the enable-path reset) would
		// otherwise never queue another one.
		shared_auto_pop_queued = false;
		// Shared movement/speed/rotation are per-owner runtime state like the
		// homing deques: a pooled instance must not steer the next owner along
		// the previous owner's pattern or speed. enable_multimesh() re-seeds
		// these from spawn data; an enable_bullet() wake has no data, so blank
		// them here to make the pool neutral on every reuse path.
		shared_movement_pattern_curve.unref();
		shared_movement_pattern_face_movement_direction = false;
		shared_movement_pattern_repeat = true;
		shared_movement_pattern_distances.assign(shared_movement_pattern_distances.size(), 0.0);
		shared_bullet_speed_data.unref();
		shared_bullet_rotation_data.unref();
		// Bounce is per-owner runtime state like homing: a pooled instance
		// must not ricochet the next owner off stale layers. Vectors drop
		// entirely while disarmed (see ensure_bounce_vectors).
		bounce_mask = 0;
		bounce_strength = 1.0;
		bounce_tilemap_layers = false;
		bounce_push_assist = true;
		bounce_charge_amplify = true;
		bounce_hit_consumed = false;
		bounce_max_count = 0;
		bounce_mode = 0;
		bounce_rotate_texture = true;
		bounce_rotation_smooth = 0.0;
		bounce_randomness_deg = 0.0;
		bounce_cooldown_sec = 0.05;
		bounce_debounce_sec = 0.15;
		all_bounce_count.clear();
		all_bounce_cooldown.clear();
		all_bounce_last_tick.clear();
		all_bounce_last_target.clear();
		all_bounce_last_time.clear();
		bounce_visual_pending.clear();
		bounce_visual_target.clear();
		all_bounce_speed_multiplier.clear();
		bounce_speed_scaled = false;
		bounce_mask_warning_issued = false;
	}

	// Single-bullet hook called from disable_bullet(): the tick only trims
	// active bullets, so a partially disabled multimesh would otherwise leak
	// this bullet's targets (and the global mouse counter) until full teardown.
	// Also bumps this bullet's homing epoch: deferred reached-emits/auto-pops
	// queued before the disable carry the old epoch and no-op, so a
	// disable -> enable -> push-new-target sequence before the flush can
	// neither eat the fresh front target nor fire a ghost signal. The
	// volley-wide generation is intentionally NOT bumped here - that would
	// invalidate every sibling's legitimately queued work.
	void on_bullet_disabled(int bullet_index) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return;
		}
		// Bounce cleanup first and independent: a disabled bullet owns no
		// bounce state (counts, cooldown, per-tick guard, smooth-visual
		// pursuit all restart on wake), and this must run even if the
		// homing vectors below are ever sized differently.
		if (bullet_index < (int)all_bounce_count.size()) {
			all_bounce_count[bullet_index] = 0;
		}
		if (bullet_index < (int)all_bounce_cooldown.size()) {
			all_bounce_cooldown[bullet_index] = 0.0;
		}
		if (bullet_index < (int)all_bounce_last_tick.size()) {
			all_bounce_last_tick[bullet_index] = 0;
		}
		// A wake forgets who it bounced off last, same as counts above.
		if (bullet_index < (int)all_bounce_last_target.size()) {
			all_bounce_last_target[bullet_index] = 0;
		}
		if (bullet_index < (int)all_bounce_last_time.size()) {
			all_bounce_last_time[bullet_index] = 0.0;
		}
		if (bullet_index < (int)all_bounce_last_normal.size()) {
			all_bounce_last_normal[bullet_index] = Vector2(0, 0);
		}
		if (bullet_index < (int)all_bounce_last_target_velocity.size()) {
			all_bounce_last_target_velocity[bullet_index] = Vector2(0, 0);
		}
		if (bullet_index < (int)bounce_visual_pending.size()) {
			bounce_visual_pending[bullet_index] = 0;
		}
		if (bullet_index < (int)bounce_visual_target.size()) {
			bounce_visual_target[bullet_index] = Vector2(1, 0);
		}
		// A wake starts fresh like every other bounce ledger entry: without
		// this a re-enabled bullet would keep scaling curve/accel ticks by
		// a stale boost from its previous life.
		if (bullet_index < (int)all_bounce_speed_multiplier.size()) {
			all_bounce_speed_multiplier[bullet_index] = 1.0;
		}
		if (bullet_index >= (int)all_bullet_homing_targets.size()) {
			return;
		}
		if (bullet_index >= 0 && bullet_index < (int)bullet_homing_epochs.size()) {
			++bullet_homing_epochs[bullet_index];
		}
		auto &queue = all_bullet_homing_targets[bullet_index];
		queue.clear_homing_targets(cached_mouse_global_position);
		if (bullet_index >= 0 && bullet_index < (int)all_homing_count.size()) {
			active_homing_count -= all_homing_count[bullet_index];
			if (active_homing_count < 0) {
				active_homing_count = 0;
			}
			all_homing_count[bullet_index] = 0;
		}
		// Drop this bullet's shared-deque reached state so a re-enabled bullet can
		// emit again for the current front target.
		if (bullet_index >= 0 && bullet_index < (int)all_shared_homing_reached.size()) {
			all_shared_homing_reached[bullet_index] = SharedHomingReachedState();
		}
		if (bullet_index >= 0 && bullet_index < (int)all_orbiting_status.size() && all_orbiting_status[bullet_index]) {
			all_orbiting_status[bullet_index] = 0;
			--active_orbiting_count;
			if (active_orbiting_count < 0) {
				active_orbiting_count = 0;
			}
		}
		if (bullet_index >= 0 && bullet_index < (int)all_orbiting_data.size()) {
			all_orbiting_data[bullet_index] = OrbitingData();
		}
	}

 protected:
 	// Updates homing behavior for a bullet. Zero-delta ticks steer nothing:
 	// with no time passing any direction or texture change would be motion
 	// without movement, so the bullet holds its pose. A zero heading
 	// (unseeded ballistics) also holds: steering it would snap to angle 0.
 	_ALWAYS_INLINE_ void update_homing(HomingTargetDeque &homing_deque, int bullet_index, double delta, Vector2 &bullet_pos, Vector2 &target_pos) {
		if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_origin.size() || bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
			return;
		}
		if (!Math::is_finite(delta) || delta <= 0.0) {
			return;
		}
		// Get the front target's cached position
		target_pos = homing_deque.get_cached_front_target_global_position();
		if (!target_pos.is_finite()) {
			return;
		}

		bullet_pos = all_cached_instance_origin[bullet_index];
		Vector2 diff = target_pos - bullet_pos;

		real_t dist_sq = diff.length_squared();
		if (dist_sq <= 0.0) {
			return;
		}

		// Homing gating: delay (straight flight first), duration (escape
		// window), lose-range (pause while too far). All cheap float
		// compares; curves_elapsed_time is the volley clock.
		if (homing_delay_sec > 0.0 && curves_elapsed_time < homing_delay_sec) {
			return;
		}
		if (homing_duration_sec > 0.0 && curves_elapsed_time >= homing_delay_sec + homing_duration_sec) {
			return;
		}
		if (homing_lose_range_px > 0.0 && dist_sq > homing_lose_range_px * homing_lose_range_px) {
			return;
		}

		// Steering gains. A zero heading (unseeded ballistics) holds: any
		// steering below would normalize it into an angle-0 snap.
		real_t max_turn = homing_smoothing * delta;
		if (use_per_bullet_homing_smoothing && bullet_index >= 0 && bullet_index < (int)all_bullet_homing_smoothing.size()) {
			max_turn = all_bullet_homing_smoothing[bullet_index] * delta;
		}
		if (max_turn < 0.0) {
			max_turn = 0.0;
		}

		Vector2 &current_direction = all_cached_direction[bullet_index];
		if (current_direction.length_squared() < 0.00000001) {
			return;
		}

		auto &curr_transf = all_cached_instance_transforms[bullet_index];
		// If rotation is controlled via movement pattern (per-bullet or shared)
		// or rotation data, just set direction directly toward target
		if (check_exists_bullet_movement_pattern_data(bullet_index) || shared_movement_pattern_curve.is_valid() || is_rotation_data_active) {
			current_direction = diff.normalized();
		} else { // Otherwise use smoothing to rotate toward target
			// Rotate toward target with smoothing
			rotate_to_target(bullet_index, diff, max_turn);

			// Logical direction strips the texture rotation used for rendering
			// (rotate_to_target aims the visual forward; movement must not
			// inherit that offset or bullets head off-target every tick).
			current_direction = curr_transf[0].rotated(-cache_texture_rotation_radians).normalized();
		}
	}

	// Rotates bullet to face target with smoothing (boundary-agnostic version).
	// require_homing_flag: the homing feature only rotates the texture when the user
	// opted in via homing_take_control_of_texture_rotation; orbiting's Face* modes
	// own their texture rotation unconditionally and pass false.
	_ALWAYS_INLINE_ void rotate_to_target(int bullet_index, const Vector2 &diff, real_t max_turn, bool require_homing_flag = true) {
		if ((require_homing_flag && !homing_take_control_of_texture_rotation) || diff.length_squared() <= 0.0) {
			return;
		}
		if (!diff.is_finite() || !Math::is_finite(max_turn)) {
			return;
		}
		if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
			return;
		}

		// Normalize diff once for facing direction
		real_t dist_to_target = diff.length();
		Vector2 face_dir = diff / dist_to_target;

		// Adjust for texture offset: target_forward is the transform's [0] dir that makes visual face target
		Vector2 target_forward = face_dir.rotated(-cache_texture_rotation_radians);

		// Current forward from transform
		Vector2 current_forward = all_cached_instance_transforms[bullet_index][0].normalized();

		// Direct delta_rot via cross/dot (one atan2, no get_rotation())
		real_t dot = current_forward.dot(target_forward);
		real_t cross = current_forward.x * target_forward.y - current_forward.y * target_forward.x;
		real_t delta_rot = Math::atan2(cross, dot);
		normalize_angle(delta_rot);

		bool use_smoothing = max_turn > 0.0; // Snap when there's no turn budget

		// Apply smoothing clamp
		if (use_smoothing) {
			delta_rot = Math::clamp(delta_rot, -max_turn, max_turn);
		}

		// Rotate locally
		rotate_transform_locally(all_cached_instance_transforms[bullet_index], delta_rot);

	// No smoothing means snap: reset the interpolation cache so the sprite doesn't lag a frame behind. Orbiting preserves it separately to stay smooth.
		if (!use_smoothing) {
			update_bullet_previous_transform_for_interpolation(bullet_index);
		}
	}

	// Rotate without touching the interpolation cache, so orbiters stay smooth instead of jittering every frame.
	_ALWAYS_INLINE_ void rotate_to_target_preserve_interpolation(int bullet_index, const Vector2 &diff, bool require_homing_flag = true) {
		if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_previous_instance_transf.size()) {
			return;
		}
		const Transform2D prev = all_cached_instance_transforms[bullet_index];
		rotate_to_target(bullet_index, diff, 0.0, require_homing_flag);
		all_previous_instance_transf[bullet_index] = prev;
	}

	// Spin one bullet's sprite by its rotation speed for this tick
	_ALWAYS_INLINE_ void update_rotation(int bullet_index, double delta) {
		real_t cache_rotation_speed = all_rotation_speed[bullet_index];
		real_t rot_delta = cache_rotation_speed * (real_t)delta;

		// Skip standing still - rotating by 0 would just add float noise
		if (cache_rotation_speed != 0.0f) {
			// max <= 0 means unlimited (same convention as linear speed):
			// without the gate, max == 0 makes abs(speed) >= 0 always true
			// and the stop flag freezes default-constructed rotation data.
			const real_t max_speed = (bullet_index >= 0 && bullet_index < (int)all_max_rotation_speed.size()) ? all_max_rotation_speed[bullet_index] : 0.0;
			const bool max_reached = max_speed > 0.0 && Math::abs(cache_rotation_speed) >= max_speed;

			if (!(max_reached && stop_rotation_when_max_reached)) {
				rotate_transform_locally(all_cached_instance_transforms[bullet_index], rot_delta);
			}
		}
	}

	// Spin one bullet's sprite by its rotation speed for this tick using a curve.
	// Curve spin bypasses the max/stop gate by design (the curve IS the speed
	// program); documented so a stop flag under curves never surprises.
	_ALWAYS_INLINE_ void update_rotation_using_curve(int bullet_index, double delta) {
		real_t cache_rotation_speed = all_rotation_speed[bullet_index];
		real_t rot_delta = cache_rotation_speed * (real_t)delta;

		rotate_transform_locally(all_cached_instance_transforms[bullet_index], rot_delta);
	}

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

	// Bumped by EVERY mutation of the shared deque's FRONT (push_front /
	// pop_front / clear / replace). The deferred auto-pop stamps the value it
	// saw, so a manual edit landing between the queue and the flush cancels
	// the stale pop instead of eating whatever the user pushed in the meantime.
	// This is the shared-deque counterpart of bullet_homing_epochs: without it
	// the only guard is the volley generation, which a manual edit on a live
	// volley does not bump.
	uint64_t shared_homing_front_epoch = 0;

	_ALWAYS_INLINE_ void bump_shared_homing_front_epoch() { ++shared_homing_front_epoch; }

	// Re-arms every bullet for the new front target. Called by EVERY shared
	// front mutation (push_front, pop_front, clear, replace), so bumping the
	// front epoch here gives one choke point that cannot be missed by a future
	// mutator - unlike a bump per call site.
	_ALWAYS_INLINE_ void reset_shared_homing_reached_state() {
		for (SharedHomingReachedState &state : all_shared_homing_reached) {
			state.front_target = nullptr;
			state.fired = false;
		}
		bump_shared_homing_front_epoch();
	}

	// One shared pop per tick no matter how many bullets arrive at once - otherwise a full volley would eat the whole queue in a frame.
	bool shared_auto_pop_queued = false;

	// The signal fires a frame later, so carry the target as an id and look it up then - the raw pointer may be dead by now.
	_ALWAYS_INLINE_ void _do_emit_homing_target_reached(uint64_t p_generation, int p_bullet_index, uint64_t p_bullet_epoch, uint64_t p_target_instance_id, const Vector2 &p_target_global_position) {
		if (p_generation != homing_operation_generation) {
			return; // Scheduled by a previous life (pool reuse before the flush).
		}
		// No signal for a bullet whose life ended after the queue: single-bullet
		// disable/enable bumps the per-bullet epoch (but not the volley
		// generation), and a disabled bullet must stay silent even when its
		// epoch still matches (e.g. reach -> disable with no re-enable).
		if (p_bullet_index < 0 || p_bullet_index >= (int)bullet_homing_epochs.size() || bullet_homing_epochs[p_bullet_index] != p_bullet_epoch || !all_bullets_enabled_set.contains(p_bullet_index)) {
			return;
		}
		Node2D *target = nullptr;
		if (p_target_instance_id != 0) {
			target = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(p_target_instance_id)));
		}
		emit_signal(CachedStringNames2D::get().bullet_homing_target_reached, this, p_bullet_index, target, p_target_global_position);
	}

	_ALWAYS_INLINE_ void _do_shared_auto_pop_front_target(uint64_t p_generation, uint64_t p_front_epoch) {
		if (p_generation != homing_operation_generation) {
			return; // Never touch the flag: it belongs to the new life now.
		}
		// The deque's front changed since this pop was queued, so a manual
		// push/pop/clear already superseded it. Unlatch (so a later real reach
		// can queue again) but do NOT pop: popping here would eat the target
		// the user just queued deliberately.
		if (p_front_epoch != shared_homing_front_epoch) {
			shared_auto_pop_queued = false;
			return;
		}
		shared_auto_pop_queued = false;
		shared_homing_deque.pop_front_target(cached_mouse_global_position);
		// orbit_route_shared_front_change() calls reset_shared_homing_reached_state(),
		// which is the single place that bumps shared_homing_front_epoch - so the
		// pop itself does not need (and must not add) a second bump.
		orbit_route_shared_front_change();
	}

	// Same trick for per-bullet pops: stale calls from a dead volley just no-op.
	// of eating the new life's front target. Guards both the volley
	// generation (pool reuse) and the per-bullet epoch (single-bullet
	// disable/enable + fresh push before the flush).
	_ALWAYS_INLINE_ void _do_auto_pop_front_target(uint64_t p_generation, int p_bullet_index, uint64_t p_bullet_epoch) {
		if (p_generation != homing_operation_generation) {
			return;
		}
		if (p_bullet_index < 0 || p_bullet_index >= (int)bullet_homing_epochs.size() || bullet_homing_epochs[p_bullet_index] != p_bullet_epoch) {
			return;
		}
		bullet_homing_pop_front_target(p_bullet_index);
	}

	// Single routing point for every shared-deque front change: the deferred
	// shared auto-pop flush and the manual shared pops funnel here. Resets
	// the reached state (a new front target re-arms every bullet) and routes
	// the orbit lock per policy, so RelockAlways re-acquires on the new
	// target instead of holding a stale center.
	_ALWAYS_INLINE_ void orbit_route_shared_front_change() {
		reset_shared_homing_reached_state();
		orbit_keep_lock_across_replace(shared_homing_deque);
	}

	// Reached check against the post-move position: the caller passes the
	// already-advanced origin with a zero delta, so the test runs exactly
	// where the bullet landed this tick (orbit/pattern/curve output). A
	// point test cannot sweep-catch tunneling: when speed * delta exceeds
	// twice the threshold a bullet can jump clean over it, so size
	// homing_distance_before_reached for the fastest volleys.
	_ALWAYS_INLINE_ void try_to_emit_bullet_homing_target_reached_signal(HomingTargetDeque &homing_deque, bool is_using_shared_homing_deque, int bullet_index, const Vector2 &bullet_pos, const Vector2 &target_pos, const Vector2 &post_velocity_delta) {
		if (homing_deque.empty()) {
			return;
		}
		// Reached check against the predicted post-move position: at high speed a bullet
		// can tunnel past the threshold within one tick and never fire on pre-move pos.
		const Vector2 check_pos = bullet_pos + post_velocity_delta;
		Vector2 post_to_target = target_pos - check_pos;
		real_t post_dist_sq = post_to_target.length_squared();
		real_t threshold_sq = homing_distance_before_reached * homing_distance_before_reached;
		if (post_dist_sq <= threshold_sq) { // Fully squared for perf

			HomingTarget &target = homing_deque.front();

			// Decide whether THIS bullet may fire:
			// - shared deque: per-bullet state keyed on the current front target
			// - per-bullet deque: the target's own flag (each bullet owns its targets)
			bool fire_for_this_bullet = false;
			if (is_using_shared_homing_deque) {
				if (bullet_index >= 0 && bullet_index < (int)all_shared_homing_reached.size()) {
					SharedHomingReachedState &state = all_shared_homing_reached[bullet_index];
					fire_for_this_bullet = (state.front_target != (const void *)&target) || !state.fired;
					state.front_target = &target;
					state.fired = true;
				}
			} else {
				fire_for_this_bullet = !target.has_bullet_reached_target;
				target.has_bullet_reached_target = true;
			}

			// Ensure that the signal is emitted only ONCE per bullet per target
			if (fire_for_this_bullet) {
				const uint64_t bullet_epoch = (bullet_index >= 0 && bullet_index < (int)bullet_homing_epochs.size()) ? bullet_homing_epochs[bullet_index] : 0;
				switch (target.type) {
					case GlobalPositionTarget:
						// Deferred through the generation-guarded emitter: the target travels
						// as an instance id (resolved at fire time, null when freed) and a
						// stale generation no-ops, so pool reuse before the flush can neither
						// crash on a dangling pointer nor emit ghosts.
						call_deferred(CachedStringNames2D::get().m_do_emit_homing_target_reached, homing_operation_generation, bullet_index, bullet_epoch, (uint64_t)0, target_pos);
						break;
					case Node2DTarget: {
						auto &target_data = target.node2d_target_data;

						// In case the target instance is freed - will still emit the signal, but with a nullptr as the target
						uint64_t target_id = 0;
						if (homing_deque.is_homing_target_valid(target_data.target, target_data.cached_valid_instance_id)) {
							target_id = target_data.cached_valid_instance_id;
						}
						call_deferred(CachedStringNames2D::get().m_do_emit_homing_target_reached, homing_operation_generation, bullet_index, bullet_epoch, target_id, target_pos);
						break;
					}
					case NotHoming:
						break;
					case MousePositionTarget:
						call_deferred(CachedStringNames2D::get().m_do_emit_homing_target_reached, homing_operation_generation, bullet_index, bullet_epoch, (uint64_t)0, target_pos);
						break;
				}

				// Pop the front target automatically if that's what the user wants.
				// Shared deque: N bullets reaching in the same tick must queue exactly
				// ONE deferred pop, otherwise the storm would drain every target the
				// user pushed. Per-bullet deques pop their own deque per bullet - no
				// storm there.
				if (is_using_shared_homing_deque) {
					// Both the generation and the shared front epoch travel with
					// the pop: a pool reuse no-ops, and a manual shared-deque edit
					// landing before the flush cancels this stale pop.
					if (shared_homing_deque_auto_pop_after_target_reached && !shared_auto_pop_queued) {
						shared_auto_pop_queued = true;
						call_deferred(CachedStringNames2D::get().m_do_shared_auto_pop_front_target, homing_operation_generation, shared_homing_front_epoch);
					}
				} else {
					if (bullet_homing_auto_pop_after_target_reached) {
						const uint64_t pop_epoch = (bullet_index >= 0 && bullet_index < (int)bullet_homing_epochs.size()) ? bullet_homing_epochs[bullet_index] : 0;
						call_deferred(CachedStringNames2D::get().m_do_auto_pop_front_target, homing_operation_generation, bullet_index, pop_epoch);
					}
				}
			}
		}
	}

	// Normalizes an angle to [-PI, PI]
	_ALWAYS_INLINE_ void normalize_angle(real_t &angle) const {
		angle = Math::wrapf(angle, -static_cast<real_t>(Math::PI), static_cast<real_t>(Math::PI));
	}

	// Updates the homing timer and checks if interval is reached
	_ALWAYS_INLINE_ bool update_homing_timer(double delta) {
		homing_update_timer -= delta;
		if (homing_update_timer <= 0.0) {
			homing_update_timer = homing_update_interval;
			return true;
		}
		return false;
	}

	static void _bind_methods();
};
} // namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::HomingType);
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::OrbitingDirection);
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::OrbitingTextureRotation);
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::OrbitingFollowMode);
VARIANT_ENUM_CAST(BlastBullets2D::DirectionalBullets2D::OrbitingLockPolicy);
