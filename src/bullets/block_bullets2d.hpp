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
				bullet_accelerate_rotation_speed(0, delta); // accelerate only the first one once
				cache_first_rotation_result = all_rotation_speed[0] * delta;
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

		// Accelerate every entry (not just active ones): a re-enabled bullet
		// rejoins at the block's current speed, matching the old shared-entry
		// behavior exactly.
		for (int i = 0; i < amount_bullets && i < (int)all_cached_speed.size(); ++i) {
			bullet_accelerate_speed(i, delta);
		}

		// Swap into a local first: handle_bullet_collision can funnel into
		// disable_multimesh() (last bullet out), which clears the member vector.
		// Iterating the member directly would invalidate iterators mid-loop and
		// silently drop the remaining collisions of this frame.
		if (!all_collided_bullets.empty()) {
			std::vector<BulletCollisionData2D> pending_collisions;
			pending_collisions.swap(all_collided_bullets);
			for (auto &data : pending_collisions) {
				handle_bullet_collision(data.collision_type, data.bullet_index, data.collided_instance_id);
			}
		}
	}

protected:
	static void _bind_methods();
	void set_up_movement_data(const BulletSpeedData2D &new_speed_data);

	virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_enable_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_disable_logic() override final;
};
} //namespace BlastBullets2D
