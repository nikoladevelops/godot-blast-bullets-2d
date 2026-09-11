#pragma once

#include "../debugger/idebugger_data_provider2d.hpp"
#include "../factory/bullet_factory2d.hpp"
#include "../shared/bullet_attachment2d.hpp"
#include "../shared/bullet_attachment_object_pool2d.hpp"
#include "../shared/bullet_rotation_data2d.hpp"
#include "../spawn-data/multimesh_bullets_data2d.hpp"
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

namespace BlastBullets2D {
using namespace godot;

class MultiMeshObjectPool;

class MultiMeshBullets2D : public MultiMeshInstance2D, public IDebuggerDataProvider2D {
	GDCLASS(MultiMeshBullets2D, MultiMeshInstance2D)
public:
	// Godot's memnew cannot forward constructor arguments, so instances are created
	// with memnew and then initialized through spawn(). Always call spawn() after memnew.

	virtual ~MultiMeshBullets2D();

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

	bool marked_for_internal_deletion = false;

	// Gets the total amount of bullets that the multimesh always holds
	_ALWAYS_INLINE_ int get_amount_bullets() const { return amount_bullets; };

	// Gets the total amount of attachments that are active
	int get_amount_active_attachments() const;

	// Used to spawn brand new bullets that are active in the scene tree
	void spawn(const MultiMeshBulletsData2D &spawn_data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container, const Vector2 &new_inherited_velocity_offset, int new_sparse_set_id, bool spawn_in_pool);

	// Activates the multimesh. Returns false (without leaving it factory-active)
	// when the spawn data is incompatible, so the pool owner can re-push it.
	bool enable_multimesh(const MultiMeshBulletsData2D &data, const Vector2 &new_inherited_velocity_offset);

	// Clears homing state (target deques + counters) on teardown so global mouse-target
	// accounting can't leak. Base version is a no-op (only directional bullets home).
	virtual void clear_homing_state_for_teardown() {}

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
	_ALWAYS_INLINE_ Transform2D to_local_for_multimesh(const Transform2D &global_transf) const {
		return get_global_transform().affine_inverse() * global_transf;
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
		const Transform2D multimesh_inv = get_global_transform().affine_inverse();

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
	}

	_ALWAYS_INLINE_ void batch_flush_instance_transforms() {
		if (!multi.is_valid() || amount_bullets != multi->get_instance_count()){
		    return;
		}
		if ((int)all_cached_instance_transforms.size() != amount_bullets || (int)batch_buffer.size() != amount_bullets * 8) {
			return;
		}
#ifdef DEV_ENABLED
		ERR_FAIL_COND((int)batch_buffer.size() != amount_bullets * 8);
#endif
float *w = batch_buffer.ptrw();
		const Transform2D multimesh_inv = get_global_transform().affine_inverse();
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
	inline void reduce_lifetime(double delta) {
		if (!Math::is_finite(delta) || delta < 0.0) {
			return;
		}
		curves_elapsed_time += delta;

		// If the lifetime is infinite there is no lifetime timer
		if (is_life_time_infinite) {
			return;
		}

		// Life time timer logic
		current_life_time -= delta;

		// The bullets still have life time left, so don't do anything yet
		if (current_life_time > 0) {
			return;
		}

		std::vector<int> active_copy = all_bullets_enabled_set.get_active_indexes();

		// If the life_time_over signal is not enabled, we can just disable all bullets right away and skip the additional logic
		if (!is_life_time_over_signal_enabled) {
			for (int i : active_copy) {
				if (!all_bullets_enabled_set.contains(i)) {
					continue;
				}
				disable_bullet(i, true);
			}

			return;
		}

		// If the life_time_over signal is enabled - collect transforms, disable bullets immediately (consistent with collision path),
		// but keep attachment disable and signal deferred so handler can still access attachment.
		TypedArray<Transform2D> transfs;
		TypedArray<int> bullet_indexes;

		// Caches already hold global-space transforms (spawn data is global and all
		// movement/homing math is global), so store them directly. Composing with
		// get_global_transform() here would double-apply the node transform.
		for (int i : active_copy) {
			if (!all_bullets_enabled_set.contains(i)) {
				continue;
			}
			if (i < 0 || i >= (int)all_cached_instance_transforms.size()) {
				continue;
			}
			transfs.push_back(all_cached_instance_transforms[i]); // Store the transform before disabling
			bullet_indexes.push_back(i);
			disable_bullet(i, false); // immediate shape disable, keep attachment for signal
		}

		if (bullet_indexes.size() > 0) {
			// Emit signal deferred so user code runs outside physics step
			bullet_factory->call_deferred("emit_signal", "life_time_over", this, bullet_indexes, bullets_custom_data, transfs);

			// Disable attachments after signal (deferred keeps order)
			for (int i = 0; i < bullet_indexes.size(); ++i) {
				int idx = bullet_indexes[i];
				call_deferred("_do_deferred_bullet_disable_attachment", idx, multimesh_generation, attachments[idx]);
			}
		}
	}

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
						// Deferred like life_time_over: never emit directly from physics tick.
						// NOTE: the signal lives on the multimesh itself (not the factory),
						// so it must be emitted on `this`.
						call_deferred("emit_signal", "sprite_animation_finished", this);
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

	_ALWAYS_INLINE_ Ref<Resource> get_bullets_custom_data() const {
		return bullets_custom_data;
	}

	_ALWAYS_INLINE_ void set_bullets_custom_data(const Ref<Resource> &new_custom_data) {
		bullets_custom_data = new_custom_data;
	}

	Vector2 get_inherited_velocity_offset() const { return inherited_velocity_offset; }
	void set_inherited_velocity_offset(const Vector2 &new_offset) {
		if (!new_offset.is_finite()) {
			UtilityFunctions::push_error("set_inherited_velocity_offset: offset must be finite, keeping the old value.");
			return;
		}
		inherited_velocity_offset = new_offset;
	}

	bool get_is_multimesh_auto_pooling_enabled() const { return is_multimesh_auto_pooling_enabled; }
	void set_is_multimesh_auto_pooling_enabled(bool value) { is_multimesh_auto_pooling_enabled = value; }

	bool get_is_attachments_auto_pooling_enabled() const { return is_attachments_auto_pooling_enabled; }
	void set_is_attachments_auto_pooling_enabled(bool value) { is_attachments_auto_pooling_enabled = value; }

	Ref<BulletCurvesData2D> get_shared_bullet_curves_data() const { return shared_bullet_curves_data; }
	void set_shared_bullet_curves_data(const Ref<BulletCurvesData2D> &new_curves_data) {
		// Block bullets are spawned without an instance handle by design (spawn_block_bullets
		// returns void), so advanced per-instance features stay on DirectionalBullets2D.
		if (!new_curves_data.is_null() && is_class("BlockBullets2D")) {
			UtilityFunctions::push_error("BlockBullets2D does not support bullet curves - use DirectionalBullets2D for curves.");
			return;
		}
		populate_shared_curves_related_data(new_curves_data);
	}

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

	// Exact instantaneous velocity of a bullet, including movement patterns,
	// orbiting and curves. Use this for gameplay logic such as splitting bullets.
	Vector2 get_bullet_velocity(int bullet_index) const;

	TypedArray<Transform2D> all_bullets_get_transforms(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	void all_bullets_set_transforms(const Transform2D &new_transform, bool set_direction_based_on_transform = false, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	real_t get_curves_elapsed_time() const;
	void set_curves_elapsed_time(real_t new_time);

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
	// Explicit runtime shape change. Size-only changes apply immediately through the
	// PhysicsServer for every bullet (same bucket, no re-bucketing needed). Effective-type
	// changes additionally recreate the RIDs and re-bucket pooled instances. The visual
	// QuadMesh is texture-driven and intentionally untouched by shape edits.
	void set_collision_shape_runtime(const Ref<Shape2D> &new_shape);
	PoolKey get_pool_key() const { return PoolKey{ amount_bullets, cached_effective_shape_type }; }
	static void _bind_methods();

	void _notification(int p_what);

	bool is_multimesh_auto_pooling_enabled = true;

	bool is_attachments_auto_pooling_enabled = true;

	// Maintained by MultiMeshObjectPool (push/pop/try_remove_instance) so duplicate
	// pool pushes are an O(1) check instead of an O(n) bucket scan.
	bool is_pooled_in_pool = false;

	// Counts all active bullets
	int active_bullets_counter = 0;

	// Used to store all bullets active state and enable fast lookups and removals
	DynamicSparseSet all_bullets_enabled_set;

	BulletFactory2D *bullet_factory = nullptr;
	MultiMeshObjectPool *bullets_pool = nullptr;
	PhysicsServer2D *physics_server = nullptr;

	std::vector<RID> physics_shapes;

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

	// Important. Determines if there was valid rotation data passed, if its true it means the rotation logic will work
	bool is_rotation_data_active = false;

	// If true it means that only a single BulletRotationData2D was provided, so it will be used for each bullet. If false it means that we have BulletRotationData2D for each bullet. It is determined by the amount of BulletRotationData2D passed to spawn()
	bool use_only_first_rotation_data = false;

	// If set to true, it will stop the rotation when the max rotation speed is reached
	bool stop_rotation_when_max_reached = false;

	///

	/// BULLET ATTACHMENT RELATED

	// Stores each bullet's attachment pooling id
	std::vector<uint32_t> attachment_pooling_ids;

	// Stores pointers to all bullet attachments currently in the scene
	std::vector<BulletAttachment2D *> attachments;

	// Re-entrancy latch for bullet_set_attachment (see the guard there): the setup
	// path runs user script callbacks which must not nest another setup call.
	int _attachment_setup_depth = 0;

	// Stores each attachment's transform data
	std::vector<Transform2D> attachment_transforms;

	// Stores each attachment's offset relative to the bullet's texture center
	std::vector<Vector2> attachment_offsets;

	// Stores each attachment's local transform relative to the bullet's texture center
	std::vector<Transform2D> attachment_local_transforms;

	// Whether the attachment should stick while the bullet is rotating
	std::vector<uint8_t> attachment_stick_relative_to_bullet;

	///

	/// OTHER

	// Provides inertia to the bullets by adding an additional velocity offset to their movement every physics frame
	Vector2 inherited_velocity_offset = Vector2(0, 0);

	// The amount of bullets the multimesh has
	int amount_bullets = 0;

	// Pointer to the multimesh instead of always calling the get method
	Ref<MultiMesh> multi = nullptr;

	// Reusable buffer for batch uploads (avoid per-frame alloc)
	mutable PackedFloat32Array batch_buffer;

	// The user can pass any custom data they desire and have access to it in the area_entered and body_entered function callbacks
	Ref<Resource> bullets_custom_data;

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

	struct BulletCollisionData2D {
		int64_t collided_instance_id = -1;
		int bullet_index = -1;
		CollisionType collision_type = AREA;

		BulletCollisionData2D() = default;

		BulletCollisionData2D(int new_bullet_index, int64_t new_collided_instance_id, CollisionType new_collision_type) :
				collided_instance_id(new_collided_instance_id),
				bullet_index(new_bullet_index),
				collision_type(new_collision_type) {}
	};

	// All bullets that have collided this physics frame
	std::vector<BulletCollisionData2D> all_collided_bullets;

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
	_ALWAYS_INLINE_ bool validate_bullet_index(int bullet_index, const String &function_name) const {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			UtilityFunctions::push_error("Invalid bullet index in " + function_name);
			return false;
		}
		return true;
	}

	_ALWAYS_INLINE_ void ensure_indexes_match_amount_bullets_range(int &bullet_index_start, int &bullet_index_end_inclusive, const String &function_name) const {
		if (bullet_index_start < 0 || bullet_index_start >= amount_bullets) {
			bullet_index_start = 0;
		}
		if (bullet_index_end_inclusive < 0 || bullet_index_end_inclusive >= amount_bullets) {
			bullet_index_end_inclusive = amount_bullets - 1;
		}
		if (bullet_index_start > bullet_index_end_inclusive) {
			// Clamp the inverted range to empty instead of silently expanding to
			// "everything": start==end could no-op, but widening to the full
			// multimesh on a typo'd range is how users nuke state they didn't mean to.
			UtilityFunctions::push_error("Invalid index range in " + function_name + " (start > end). Nothing was applied.");
			bullet_index_end_inclusive = bullet_index_start - 1;
		}
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
	// Matches the tick convention (directional/block step 8): the instance rotation
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
		if ((int)all_rotation_speed.size() != amount_bullets) {
			all_rotation_speed.resize(amount_bullets, 0.0);
		}

		// Block has single speed/dir - handle separately to avoid OOB
		if (all_cached_speed.size() == 1) {
			if (is_movement_curve_valid) {
				all_cached_speed[0] = get_bullet_curves_movement_speed(shared_bullet_curves_data.ptr());
			}
			if (is_rotation_curve_valid) {
				all_rotation_speed[0] = get_bullet_curves_rotation_speed(shared_bullet_curves_data.ptr());
			}
			auto &current_direction = all_cached_direction[0];
			apply_x_direction_curve(current_direction, shared_bullet_curves_data.ptr());
			apply_y_direction_curve(current_direction, shared_bullet_curves_data.ptr());
			if (is_movement_curve_valid || is_x_direction_curve_valid || is_y_direction_curve_valid) {
				all_cached_velocity[0] = all_cached_direction[0] * all_cached_speed[0] + inherited_velocity_offset;
			}
		} else {
			for (int i = 0; i < amount_bullets; ++i) {
				if (is_movement_curve_valid) {
					all_cached_speed[i] = get_bullet_curves_movement_speed(shared_bullet_curves_data.ptr());
				}

				if (is_rotation_curve_valid) {
					all_rotation_speed[i] = get_bullet_curves_rotation_speed(shared_bullet_curves_data.ptr());
				}

				auto &current_direction = all_cached_direction[i];

				apply_x_direction_curve(current_direction, shared_bullet_curves_data.ptr());
				apply_y_direction_curve(current_direction, shared_bullet_curves_data.ptr());

				if (is_movement_curve_valid || is_x_direction_curve_valid || is_y_direction_curve_valid) {
					all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset;
				}
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

		// Same unconditional sizing as populate_shared_curves_related_data: a per-bullet
		// rotation curve can also appear on a vector that was never rotation-sized.
		if ((int)all_rotation_speed.size() != amount_bullets) {
			all_rotation_speed.resize(amount_bullets, 0.0);
		}

		if (is_rotation_curve_valid) {
			all_rotation_speed[bullet_index] = get_bullet_curves_rotation_speed(curr_curves.ptr());
		}

		int eff = (all_cached_speed.size() == 1) ? 0 : bullet_index;
		if (is_movement_curve_valid) {
			all_cached_speed[eff] = get_bullet_curves_movement_speed(curr_curves.ptr());
		}

		auto &current_direction = all_cached_direction[eff];

		if (is_x_direction_curve_valid) {
			apply_x_direction_curve(current_direction, curr_curves.ptr());
		}

		if (is_y_direction_curve_valid) {
			apply_y_direction_curve(current_direction, curr_curves.ptr());
		}

		if (is_movement_curve_valid || is_x_direction_curve_valid || is_y_direction_curve_valid) {
			all_cached_velocity[eff] = all_cached_direction[eff] * all_cached_speed[eff] + inherited_velocity_offset;
		}
	}

	// Applies the x direction curve offset to the provided direction vector and normalizes it
	_ALWAYS_INLINE_ void apply_x_direction_curve(Vector2 &direction_vector, const BulletCurvesData2D *curves_data) const {
		const bool is_x_direction_curve_valid = curves_data != nullptr && curves_data->get_x_direction_curve().is_valid();

		if (!is_x_direction_curve_valid) {
			return;
		}

		const real_t x_dir_offset = get_bullet_curves_x_direction_offset(curves_data);
		const real_t x_direction_curve_strength = curves_data->x_direction_curve_strength;
		auto x_curve_mode = curves_data->x_direction_curve_mode;

		if (x_curve_mode == DirectionCurveMode::Additive) {
			direction_vector.x += x_dir_offset * x_direction_curve_strength;
		} else {
			direction_vector.x = x_dir_offset * x_direction_curve_strength;
		}

		direction_vector = direction_vector.normalized();
	}

	// Applies the y direction curve offset to the provided direction vector and normalizes it
	_ALWAYS_INLINE_ void apply_y_direction_curve(Vector2 &direction_vector, const BulletCurvesData2D *curves_data) const {
		const bool is_y_direction_curve_valid = curves_data != nullptr && curves_data->get_y_direction_curve().is_valid();

		if (!is_y_direction_curve_valid) {
			return;
		}

		const real_t y_dir_offset = get_bullet_curves_y_direction_offset(curves_data);
		const real_t y_direction_curve_strength = curves_data->y_direction_curve_strength;
		auto y_curve_mode = curves_data->y_direction_curve_mode;

		if (y_curve_mode == DirectionCurveMode::Additive) {
			direction_vector.y += y_dir_offset * y_direction_curve_strength;
		} else {
			direction_vector.y = y_dir_offset * y_direction_curve_strength;
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

		if (is_class("BlockBullets2D")) {
			UtilityFunctions::push_error("BlockBullets2D does not support bullet curves - use DirectionalBullets2D for curves.");
			return;
		}

		populate_individual_bullet_curves_related_data(bullet_index, curves_data);
	}

	_ALWAYS_INLINE_ void all_bullets_set_curves_data(const Ref<BulletCurvesData2D> &curves_data, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_curves_data");

		// Single error for the whole range instead of one per bullet below.
		if (!curves_data.is_null() && is_class("BlockBullets2D")) {
			UtilityFunctions::push_error("BlockBullets2D does not support bullet curves - use DirectionalBullets2D for curves.");
			return;
		}

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

	////////////

	// Accelerates bullet speed
	_ALWAYS_INLINE_ void bullet_accelerate_speed(int bullet_index, double delta) {
		real_t &curr_bullet_speed = all_cached_speed[bullet_index];
		real_t curr_max_bullet_speed = all_cached_max_speed[bullet_index];

		real_t acceleration = all_cached_acceleration[bullet_index] * delta;
		real_t new_speed = curr_bullet_speed + acceleration;
		if (acceleration >= 0.0) {
			new_speed = Math::min(new_speed, curr_max_bullet_speed);
		} else {
			new_speed = Math::max(new_speed, (real_t)0.0);
		}
		curr_bullet_speed = new_speed;

		all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * curr_bullet_speed + inherited_velocity_offset;
	}

	// Accelerates bullet speed using a curve
	_ALWAYS_INLINE_ void bullet_accelerate_speed_using_curve(int bullet_index, double delta, const BulletCurvesData2D *curves_data) {
		real_t &curr_bullet_speed = all_cached_speed[bullet_index];
		curr_bullet_speed = get_bullet_curves_movement_speed(curves_data);

		all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * curr_bullet_speed + inherited_velocity_offset;
	}

	// Accelerates bullet rotation speed
	_ALWAYS_INLINE_ void bullet_accelerate_rotation_speed(int bullet_index, double delta) {
		real_t &curr_bullet_rotation_speed = all_rotation_speed[bullet_index];
		real_t curr_max_rotation_speed = all_max_rotation_speed[bullet_index];

		// Symmetric with bullet_accelerate_speed: clamp both directions so a
		// sustained negative acceleration can't run to -inf and an overspeed
		// above max decays back instead of freezing there forever.
		real_t acceleration = all_rotation_acceleration[bullet_index] * delta;
		real_t new_speed = curr_bullet_rotation_speed + acceleration;
		if (acceleration >= 0.0) {
			new_speed = Math::min(new_speed, curr_max_rotation_speed);
		} else {
			new_speed = Math::max(new_speed, -curr_max_rotation_speed);
		}
		curr_bullet_rotation_speed = new_speed;
	}

	// Accelerates bullet rotation speed using a curve
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

		return attachments[bullet_index];
	}

	_ALWAYS_INLINE_ BulletAttachment2D *bullet_set_attachment_to_null(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_set_attachment_to_null")) {
			return nullptr;
		}

		auto &curr_attachment = attachments[bullet_index];
		auto temp = curr_attachment;

		// Detached without being disabled: clear owner tracking so its PREDELETE
		// doesn't try to drop this (now stale) slot.
		if (temp != nullptr) {
			temp->owner_multimesh_id = 0;
			temp->owner_bullet_index = -1;
		}

		curr_attachment = nullptr;
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
	// below and by the spawn-time shared-attachment application, so both types
	// (including BlockBullets2D) share one implementation. Returns false when
	// nothing was attached (error already printed).
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
		struct _AttachmentSetupGuard {
			int &depth;
			_AttachmentSetupGuard(int &d) :
					depth(d) { ++depth; }
			~_AttachmentSetupGuard() { --depth; }
		} attachment_setup_guard(_attachment_setup_depth);

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

		attachment_pooling_ids[bullet_index] = pooling_key;
		attachment_stick_relative_to_bullet[bullet_index] = stick_relative_to_bullet;

		attachment_offsets[bullet_index] = bullet_attachment_offset;

		auto &local_transf = attachment_local_transforms[bullet_index];

		local_transf = Transform2D();
		local_transf.set_origin(bullet_attachment_offset);
		local_transf.set_rotation(0.0);

		auto &global_transf = attachment_transforms[bullet_index];
		global_transf = calculate_attachment_global_transf(bullet_index, cache_texture_transforms[bullet_index]);

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

		if (is_class("BlockBullets2D")) {
			UtilityFunctions::push_error("BlockBullets2D does not support attachments - use DirectionalBullets2D for bullet_set_attachment");
			return;
		}

		attach_bullet_attachment_internal(bullet_index, attachment_scene, bullet_attachment_offset, stick_relative_to_bullet);
	}

	// Applies the spawn data's shared attachment to every bullet. Called from
	// spawn()/enable_multimesh() so both bullet types behave identically; the
	// public setters stay Directional-only (BlockBullets2D has no controllable
	// spawn, so its users configure attachments here). Stops after the first
	// failure so a bad scene prints one error instead of one per bullet.
	_ALWAYS_INLINE_ void apply_shared_bullet_attachment_from_data(const MultiMeshBulletsData2D &data) {
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

		// Being freed outright: clear owner tracking first so its PREDELETE skip
		// path can't race with this deletion.
		temp->owner_multimesh_id = 0;
		temp->owner_bullet_index = -1;

		if (temp->get_parent()) {
			temp->get_parent()->remove_child(temp);
		}
		memdelete(temp);
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

		// Owner tracking cleared before the callback: if the handler frees the
		// attachment itself, its PREDELETE hook then finds nothing left to do.
		detaching->owner_multimesh_id = 0;
		detaching->owner_bullet_index = -1;

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

	// Deferred attachment disable carrying the spawn generation AND the exact
	// attachment pointer. Lifetime expiry queues disables that flush after the
	// tick; if the slot was detached or re-assigned in a handler, we must not
	// pool the new owner's attachment by index alone.
	void _do_deferred_bullet_disable_attachment(int bullet_index, int expected_generation, BulletAttachment2D *expected_attachment) {
		if (expected_generation != multimesh_generation) {
			return;
		}
		if (bullet_index < 0 || bullet_index >= (int)attachments.size() || attachments[bullet_index] != expected_attachment) {
			return;
		}
		bullet_disable_attachment(bullet_index);
	}

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
		active_bullets_counter = 0;
		is_active = false;
		curves_elapsed_time = 0.0;
		// No path should reach here with live bits: every drain goes through
		// disable_bullet() first. Clear anyway so a future direct call can't
		// pool an instance whose sparse set claims live bullets at counter 0.
		all_bullets_enabled_set.clear();
		// Drop pending collision records: they belong to the expiring lifetime and must
		// never be processed after a pool reuse as phantom hits on the new owner.
		all_collided_bullets.clear();
		anim_frame_index = 0;
		anim_paused = false;
		anim_finished = false;
		if (!anim_frame_secs.empty()) {
			anim_frame_time_left = anim_frame_secs[0];
		}
		// Deferred attachment disables can be dropped by a generation bump, so the
		// pool push below must never inherit live slots: sweep every survivor now
		// (auto-pool returns them to the pool, otherwise they are queue_freed).
		for (int i = 0; i < (int)attachments.size(); ++i) {
			if (attachments[i] != nullptr) {
				bullet_disable_attachment(i);
			}
		}
		shared_bullet_curves_data = Ref<BulletCurvesData2D>();
		for (auto &r : all_bullet_curves_data) {
			r.unref();
		}
		for (auto &p : all_movement_pattern_data) {
			p = BulletMovementPatternData2D();
		}
		// Drop homing targets here too (the override is a no-op for block bullets).
		// Otherwise a pooled instance carries stale deques into its next owner and
		// keeps the global mouse-target counter inflated while sitting idle.
		clear_homing_state_for_teardown();

		set_visible(false); // Hide the multimesh node itself

		custom_additional_disable_logic();

		// Timers belong to the expiring lifetime: detach on EVERY path, not just
		// the pooled one. Otherwise a pooling-opted-out instance keeps ticking
		// stale callbacks, and enable_bullet() wake inherits them.
		_do_detach_all_time_based_functions(multimesh_timers_generation);

		if (!is_multimesh_auto_pooling_enabled) {
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

	_ALWAYS_INLINE_ void enable_bullet(int bullet_index, int collision_amount = 0, bool should_enable_attachment = true) {
		if (!validate_bullet_index(bullet_index, "enable_bullet")) {
			return;
		}
		if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)bullets_current_collision_count.size()) {
			return;
		}
		if (multi.is_null() || !multi.is_valid()) {
			return;
		}
		if (physics_server == nullptr || !area.is_valid()) {
			return;
		}

		const bool curr_bullet_status = all_bullets_enabled_set.contains(bullet_index);

		// If the bullet is already enabled, just return
		if (curr_bullet_status) {
			return;
		}

		++active_bullets_counter;
		if (active_bullets_counter > amount_bullets) {
			active_bullets_counter = amount_bullets;
		}

		multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(all_cached_instance_transforms[bullet_index])); // Start rendering the instance

		physics_server->area_set_shape_disabled(area, bullet_index, false);

		auto &current_bullet_collision_amount = bullets_current_collision_count[bullet_index];

		// collision_amount is how many hits the bullet has already taken: 0 means fresh
		// (full hits remaining). Clamp into range so re-enabling can't grant extra hits
		// or kill the bullet one hit early.
		if (collision_amount < 0) {
			current_bullet_collision_amount = 0;
		} else if (bullet_max_collision_count > 0 && collision_amount >= bullet_max_collision_count) {
			current_bullet_collision_amount = bullet_max_collision_count - 1;
		} else {
			current_bullet_collision_amount = collision_amount;
		}

		if (should_enable_attachment) {
			bullet_enable_attachment(bullet_index);
		}

		all_bullets_enabled_set.activate_data(bullet_index);

		if (!is_active) {
			// Waking a fully pooled multimesh outside the factory pop path: drop it from
			// the pool first, otherwise the next pop() would hand out this live instance
			// to a second owner while the first still drives it. The factory also resumes
			// processing it so woken bullets actually move.
			if (bullets_pool != nullptr) {
				bullets_pool->try_remove_instance(this, get_pool_key());
			}
			if (bullet_factory != nullptr) {
				bullet_factory->reactivate_multimesh_instance(*this);
			}
			// A pooled instance carries the previous owner's signal connections; they
			// must not fire for this wake (same cleanup the pool-pop enable does).
			disconnect_sprite_animation_connections();
			// An expiry-pooled wake would otherwise die again on the next tick with an
			// exhausted timer. Only top it up when expired; manual-disable wakes keep
			// their remaining lifetime untouched.
			if (!is_life_time_infinite && current_life_time <= 0.0) {
				current_life_time = max_life_time;
			}
			is_active = true;
			set_visible(true);
		}
	}

	// Disables a single bullet: removes it from the live set, hides the visual,
	// disables its physics shape, and (unless told otherwise) returns its
	// attachment to the attachment pool. When the last bullet goes out, the
	// whole instance is pooled via disable_multimesh() below.
	// Virtual hook so DirectionalBullets2D can drop per-bullet homing/orbit
	// state alongside the sparse-set removal (base version only handles core).
	virtual void on_bullet_disabled(int bullet_index) {}
	_ALWAYS_INLINE_ void disable_bullet(int bullet_index, bool should_disable_attachment = true) {
		if (!validate_bullet_index(bullet_index, "disable_bullet")) {
			return;
		}

		const bool curr_bullet_status = all_bullets_enabled_set.contains(bullet_index);

		// If the bullet is already disabled, just return
		if (!curr_bullet_status) {
			return;
		}

		all_bullets_enabled_set.disable_data(bullet_index);

		--active_bullets_counter;
		if (active_bullets_counter < 0) {
			active_bullets_counter = 0;
		}

		// Drop per-bullet homing/orbit state now (directional override): the tick
		// only trims active bullets, so without this a disabled bullet's invalid
		// targets leak counters until the whole multimesh dies.
		on_bullet_disabled(bullet_index);

		multi->set_instance_transform_2d(bullet_index, zero_transform); // Stops rendering the instance

		physics_server->area_set_shape_disabled(area, bullet_index, true);

		if (should_disable_attachment) {
			bullet_disable_attachment(bullet_index);
		}

		if (active_bullets_counter <= 0) {
			disable_multimesh();
		}
	}

	_ALWAYS_INLINE_ void handle_bullet_collision(CollisionType collision_type, int bullet_index, int64_t entered_instance_id) {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			return;
		}
		if (bullet_index >= (int)bullets_current_collision_count.size() || bullet_index >= (int)attachments.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
			return;
		}
		const bool curr_bullet_status = all_bullets_enabled_set.contains(bullet_index);

		// If the bullet is already disabled, just return
		if (!curr_bullet_status) {
			return;
		}

		int &current_bullet_collision_amount = bullets_current_collision_count[bullet_index];

		// Always keep track of how many collisions this bullet had (yes even if the user set bullet_max_collision_count to 0, I just want consistent behavior)
		++current_bullet_collision_amount;

		const bool bullet_reached_max_collisions = bullet_max_collision_count > 0 && current_bullet_collision_amount >= bullet_max_collision_count;

		// Only disable the bullet if the max collision count is greater than 0, otherwise the bullet should never be disabled due to collisions
		if (bullet_reached_max_collisions) {
			disable_bullet(bullet_index, false); // Don't disable the attachment yet, first emit the signal for collision so user has access to the attachment and CAN detach it himself inside GDScript
		}

		// Capture the slot before the signal: the handler runs user code that may
		// detach, replace, or - through a re-entrant spawn that pops this instance
		// from the pool - hand the whole multimesh to a new owner.
		BulletAttachment2D *attachment_at_signal_time = attachments[bullet_index];

		Object *hit_target = ObjectDB::get_instance(entered_instance_id);

		// Caches already hold global-space transforms (spawn data is global, all
		// movement/homing math is global), so pass the cache straight through.
		// Composing with get_global_transform() here would double-apply the node.
		const Transform2D bullet_global_transf = all_cached_instance_transforms[bullet_index];

		if (collision_type == CollisionType::AREA) {
			bullet_factory->emit_signal("area_entered", hit_target, this, bullet_index, bullets_custom_data, bullet_global_transf);
		} else if (collision_type == CollisionType::BODY) {
			bullet_factory->emit_signal("body_entered", hit_target, this, bullet_index, bullets_custom_data, bullet_global_transf);
		}

		// Disable the bullet attachment if the bullet reached its max collision count and the attachment is still enabled
		if (bullet_reached_max_collisions) {
			// The signal above runs user code that may have detached this slot
			// already (bullet_set_attachment_to_null / bullet_free_attachment /
			// bullet_set_attachment), or re-assigned it. Only disable the slot if
			// it still holds what we captured - anything else belongs to whoever
			// changed it (possibly a new pool owner), and disable_multimesh()'s
			// sweep catches any survivor that would otherwise leak.
			if (attachment_at_signal_time != nullptr && attachments[bullet_index] == attachment_at_signal_time) {
				bullet_disable_attachment(bullet_index);
			}
		}
	}

	/// COLLISION DETECTION METHODS

	_ALWAYS_INLINE_ void area_entered_func(PhysicsServer2D::AreaBodyStatus status, RID entered_rid, int64_t entered_instance_id, int entered_shape_index, int bullet_shape_index) {
		if (status == PhysicsServer2D::AREA_BODY_ADDED) {
			all_collided_bullets.emplace_back(bullet_shape_index, entered_instance_id, CollisionType::AREA);
		}
	}
	_ALWAYS_INLINE_ void body_entered_func(PhysicsServer2D::AreaBodyStatus status, RID entered_rid, int64_t entered_instance_id, int entered_shape_index, int bullet_shape_index) {
		if (status == PhysicsServer2D::AREA_BODY_ADDED) {
			all_collided_bullets.emplace_back(bullet_shape_index, entered_instance_id, CollisionType::BODY);
		}
	}

	// Moves a single bullet attachment
	_ALWAYS_INLINE_ void move_bullet_attachment(const Vector2 &translate_by, int bullet_index) {
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
		if (!bullet_factory->use_physics_interpolation) {
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
	void set_bullet_max_collision_count(int value) { bullet_max_collision_count = value; }

	// Single-bullet collision counter read. See set_bullet_collision_count for writing.
	int get_bullet_collision_count(int bullet_index) const;

	// Single-bullet collision counter write. Clamped like set_bullets_current_collision_count:
	// negatives become 0, values above max become max (when max > 0).
	void set_bullet_collision_count(int bullet_index, int value);

	TypedArray<int> get_bullets_current_collision_count() const {
		TypedArray<int> arr;

		for (auto &collision_count : bullets_current_collision_count) {
			arr.push_back(collision_count);
		}

		return arr;
	}

	bool set_bullets_current_collision_count(const TypedArray<int> &arr) {
		int arr_size = arr.size();

		if (arr_size != amount_bullets) {
			UtilityFunctions::push_error("You need to provide collisions amount for each bullet (same amount as the transforms array amount) when calling set_bullets_current_collision_count. Make sure the amount is not less/more than the amount of bullets available");
			return false;
		}

		bullets_current_collision_count.clear();
		bullets_current_collision_count.reserve(amount_bullets);

		for (int collision_count : arr) {
			if (collision_count < 0) {
				bullets_current_collision_count.push_back(0);
				continue;
			} else if (bullet_max_collision_count > 0 && collision_count > bullet_max_collision_count) {
				bullets_current_collision_count.push_back(bullet_max_collision_count);
				continue;
			}

			bullets_current_collision_count.push_back(collision_count);
		}

		return true;
	}

	// Void wrapper for the editor property (property setters must return void).
	void set_bullets_current_collision_count_no_return(const TypedArray<int> &arr) {
		(void)set_bullets_current_collision_count(arr);
	}

	// Holds custom logic that runs before the spawn function finalizes. Note that the multimesh is not yet added to the scene tree here
	virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) {}

	// Holds custom logic that runs before activating this multimesh when retrieved from the object pool
	virtual void custom_additional_enable_logic(const MultiMeshBulletsData2D &data) {}

	// Holds custom logic that runs before disabling and pushing this multimesh inside an object pool
	virtual void custom_additional_disable_logic() {}
	///
private:
	// Reserves enough memory and populates all needed data structures keeping track of rotation data
	void set_rotation_data(const TypedArray<BulletRotationData2D> &rotation_data, bool new_rotate_only_textures);

	// Guarantees no attachment slot survives into a new owner: force-disables any
	// live slot and blanks all five attachment arrays plus the interpolation cache.
	// Used by spawn() and enable_multimesh() so pooled reuse can't inherit stale
	// pointers when a deferred disable was dropped by a generation bump.
	void reset_attachment_state_for_reuse();

	// Generates texture transform with correct rotation and sets it to the correct bullet on the multimesh
	Transform2D generate_texture_transform(Transform2D transf, bool is_texture_rotation_permanent, real_t texture_rotation_radians, int bullet_index);

	// Generates a collision shape transform for a particular bullet and attaches it to the area
	Transform2D generate_collision_shape_transform_for_area(Transform2D transf, const RID &shape, const Vector2 &collision_shape_offset, int bullet_index);

	// Sets up the area correctly with collision related data
	void set_up_area(const int collision_layer, const int collision_mask, bool new_monitorable, const RID &physics_space);

	void generate_physics_shapes_for_area(int amount);

	void set_all_physics_shapes_enabled_for_area(bool enable);

	void generate_multimesh();

	void set_up_multimesh(int new_instance_count, const Ref<Mesh> &new_mesh, Vector2 new_texture_size);

	void set_up_bullet_instances(const MultiMeshBulletsData2D &data);

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
			const Ref<Resource> &new_bullets_custom_data,
			const Ref<Material> &new_material,
			int new_z_index,
			int new_light_mask,
			int new_visibility_layer,
			const Dictionary &new_instance_shader_parameters);

	///

	/// METHODS COMING FROM THE IDebuggerDataProvider2D INTERFACE

	PhysicsServer2D::ShapeType get_collision_shape_type_for_debugging() const override {
		return cached_effective_shape_type;
	}

	const Vector2 get_collision_shape_size_for_debugging() const override {
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

	const std::vector<Transform2D> &get_all_collision_shape_transforms_for_debugging() const override {
		return all_cached_shape_transforms;
	}

	bool get_skip_debugging() const override {
		// NEVER skip: the debugger always inspects multimesh shapes, including pooled
		// (inactive) instances - their frozen cached shape transforms keep rendering.
		// The debugger's null-provider guard still protects against dangling entries.
		return false;
	}

	bool is_active_for_debugging() const override {
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

		CustomTimer(const godot::Callable &callback, double initial_time, bool repeating, bool execute_only_if_multimesh_is_active) :
				_callback(callback), _current_time(initial_time), _initial_time(initial_time), _repeating(repeating), _execute_only_if_multimesh_is_active(execute_only_if_multimesh_is_active) {};
	};

	void execute_stored_callable_safely(const Callable &_callback, bool execute_only_if_multimesh_is_active) {
		// Stamp the timers generation: between this deferred queue and its execution the
		// multimesh can be disabled, pooled and re-enabled for a NEW owner - the stale
		// owner's callback must not fire then (execute_only_if_multimesh_is_active alone
		// can't catch it, since the new owner is active too).
		call_deferred("_do_execute_stored_callable_safely", _callback, execute_only_if_multimesh_is_active, multimesh_timers_generation);
	}

	void _do_execute_stored_callable_safely(const Callable &_callback, bool execute_only_if_multimesh_is_active, int expected_timers_generation) {
		if (expected_timers_generation != multimesh_timers_generation) {
			return;
		}

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
		call_deferred("_do_attach_time_based_function", time, callable, repeat, execute_only_if_multimesh_is_active, multimesh_timers_generation);
	}

	_ALWAYS_INLINE_ void _do_attach_time_based_function(double time, const Callable &callable, bool repeat, bool execute_only_if_multimesh_is_active, int expected_timers_generation) {
		if (expected_timers_generation != multimesh_timers_generation) {
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

		multimesh_custom_timers.emplace_back(callable, time, repeat, execute_only_if_multimesh_is_active);
	}

	_ALWAYS_INLINE_ void multimesh_detach_time_based_function(const Callable &callable) {
		// Stamp the generation so a full-disable landing before this deferred
		// call can't erase the next owner's timers.
		call_deferred("_do_detach_time_based_function", callable, multimesh_timers_generation);
	}

	_ALWAYS_INLINE_ void _do_detach_time_based_function(const Callable &callable, int expected_timers_generation) {
		if (expected_timers_generation != multimesh_timers_generation) {
			return;
		}
		for (auto it = multimesh_custom_timers.begin(); it != multimesh_custom_timers.end();) {
			if (it->_callback == callable) {
				it = multimesh_custom_timers.erase(it); // Order-preserving
			} else {
				++it;
			}
		}
	}

	_ALWAYS_INLINE_ void multimesh_detach_all_time_based_functions() {
		call_deferred("_do_detach_all_time_based_functions", multimesh_timers_generation);
	}

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
				execute_stored_callable_safely(it->_callback, it->_execute_only_if_multimesh_is_active);

				if (it->_repeating) {
					it->_current_time = it->_initial_time;
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
};
} //namespace BlastBullets2D
