#pragma once

#include "./multimesh_bullets2d.hpp"

#include "../shared/bullet_rotation_data2d.hpp"
#include "../shared/bullet_speed_data2d.hpp"

namespace BlastBullets2D {
using namespace godot;

class BlockBullets2D : public MultiMeshBullets2D {
	GDCLASS(BlockBullets2D, MultiMeshBullets2D)

public:
	// The block rotation. The direction of the bullets is determined by it.
	real_t block_rotation_radians = 0.0;

	// Rigid whole-volley shift: blocks move as one, so per-bullet teleport
	// makes no sense, but the factory/spawner "shift all" contract must move
	// them too. Mirrors DirectionalBullets2D::teleport_shift_bullet per bullet
	// (finite check, shape sync, attachment carry, interpolation sync).
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
		if (all_bullets_enabled_set.contains(bullet_index)) {
			multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(curr_bullet_transf));
		}
		if (bullet_index < (int)attachments.size() && bullet_index < (int)attachment_transforms.size() && bullet_index < (int)attachment_stick_relative_to_bullet.size() && attachments[bullet_index]) {
			BulletAttachment2D *attachment_instance = attachments[bullet_index];
			Transform2D att_global_transf;
			if (attachment_stick_relative_to_bullet[bullet_index]) {
				att_global_transf = calculate_attachment_global_transf(bullet_index, curr_bullet_transf);
			} else {
				att_global_transf = attachment_transforms[bullet_index].translated(shift_amount);
			}
			attachment_transforms[bullet_index] = att_global_transf;
			attachment_instance->set_global_transform(att_global_transf);
			attachment_instance->reset_physics_interpolation();
		}
		update_bullet_previous_transform_for_interpolation(bullet_index);
	}

	_ALWAYS_INLINE_ void teleport_shift_all_bullets(const Vector2 &shift_amount, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "teleport_shift_all_bullets");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			teleport_shift_bullet(i, shift_amount);
		}
	}

	inline void move_bullets(double delta) {
		if (amount_bullets <= 0 || all_cached_velocity.empty() || physics_server == nullptr || !area.is_valid()) {
			return;
		}
		if (!Math::is_finite(delta) || delta < 0.0) {
			return;
		}
		real_t cache_first_rotation_result = 0.0;
		// Accelerate only the first bullet rotation speed
		if (is_rotation_data_active) {
			if (use_only_first_rotation_data) {
				// Frozen while its slot is disabled: the cached sweep below
				// must come from a live bullet, or a disabled bullet 0 drives
				// the whole volley's spin from stale state.
				if (all_bullets_enabled_set.contains(0)) {
					bullet_accelerate_rotation_speed(0, delta); // accelerate only the first one once
					cache_first_rotation_result = all_rotation_speed[0] * delta;
				}
			}
		}

		bool is_using_physics_interpolation = bullet_factory != nullptr && bullet_factory->use_physics_interpolation;

		if (is_using_physics_interpolation) {
			update_all_previous_transforms_for_interpolation();
		}

		const auto &active_bullet_indexes = all_bullets_enabled_set.get_active_indexes();

		for (int i : active_bullet_indexes) {
			if (i < 0 || i >= amount_bullets || i >= (int)all_cached_instance_transforms.size() || i >= (int)all_cached_shape_transforms.size() || i >= (int)all_cached_velocity.size()) {
				continue;
			}
			// Per-bullet velocity (same shared values by default, so block motion
			// is unchanged, but per-bullet overrides via the setters are honored).
			const Vector2 velocity_delta = all_cached_velocity[i] * (real_t)delta;
			Transform2D &curr_instance_transf = all_cached_instance_transforms[i];
			Transform2D &curr_shape_transf = all_cached_shape_transforms[i];

			Vector2 &curr_instance_origin = all_cached_instance_origin[i];
			Vector2 &curr_shape_origin = all_cached_shape_origin[i];

			curr_instance_origin += velocity_delta;

			// Handle bullet rotation and bullet rotation speed acceleration
			real_t rotation_angle = 0.0;
			if (is_rotation_data_active) {
				if (!use_only_first_rotation_data) {
					bullet_accelerate_rotation_speed(i, delta);
					rotation_angle = all_rotation_speed[i] * delta;
				} else {
					rotation_angle = cache_first_rotation_result;
				}

				rotate_transform_locally(curr_instance_transf, rotation_angle);

				if (!rotate_only_textures) {
					rotate_transform_locally(curr_shape_transf, rotation_angle);
				}
			}

			curr_instance_transf.set_origin(curr_instance_origin);
			// Rotation changes the offset basis, so re-derive the shape origin
			// instead of translating the stale one.
			if (cache_collision_shape_offset != Vector2(0, 0)) {
				curr_shape_origin = curr_instance_origin + cache_collision_shape_offset.rotated(curr_shape_transf.get_rotation());
			} else {
				curr_shape_origin = curr_instance_origin;
			}
			curr_shape_transf.set_origin(curr_shape_origin);

			physics_server->area_set_shape_transform(area, i, curr_shape_transf);

			move_bullet_attachment(velocity_delta, i);
		}
		if (!is_using_physics_interpolation) {
			batch_flush_instance_transforms();
		}

		// Accelerate only live entries (matching DirectionalBullets2D, which
		// freezes disabled bullets at their disable-time speed): accelerating
		// disabled entries made a re-enabled bullet rejoin at the volley's
		// current speed instead of resuming where it left off (see
		// enable_bullet's "resume, not respawn" contract).
		for (int i = 0; i < amount_bullets && i < (int)all_cached_speed.size(); ++i) {
			if (!all_bullets_enabled_set.contains(i)) {
				continue;
			}
			bullet_accelerate_speed(i, delta);
		}

		// Swap into a local first: handle_bullet_collision can funnel into
		// disable_multimesh() (last bullet out), which clears the member vector.
		// Iterating the member directly would invalidate iterators mid-loop and
		// silently drop the remaining collisions of this frame.
		if (!all_collided_bullets.empty()) {
			collision_scratch.clear();
			collision_scratch.swap(all_collided_bullets);
			for (auto &data : collision_scratch) {
				handle_bullet_collision(data.collision_type, data.bullet_index, data.collided_instance_id, data.queue_bullet_epoch);
				if (is_queued_for_deletion()) {
					collision_scratch.clear();
					break;
				}
			}
		}
	}

protected:
	static void _bind_methods();
	void set_up_movement_data(const BulletSpeedData2D &new_speed_data);

	virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) override final;
	virtual bool custom_additional_enable_logic(const MultiMeshBulletsData2D &data) override final;
	virtual bool is_data_type_compatible(const MultiMeshBulletsData2D &data) const override final;
	virtual void reset_transient_subclass_state(bool drop_stale_work) override final;
	virtual void custom_additional_disable_logic() override final;
};
} //namespace BlastBullets2D
