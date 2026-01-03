#pragma once

#include "../shared/bullet_speed_data2d.hpp"
#include "../shared/homing_target_deque.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/defs.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/callable.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/variant.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "multimesh_bullets2d.hpp"
#include "shared/bullet_curves_data2d.hpp"
#include "spawn-data/multimesh_bullets_data2d.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class DirectionalBullets2D : public MultiMeshBullets2D {
	GDCLASS(DirectionalBullets2D, MultiMeshBullets2D)

public:
	enum OrbitingDirection {
		DontMove = 0,
		OrbitLeft,
		OrbitRight
	};

	enum OrbitingTextureRotation {
		FaceTarget = 0,
		FaceOppositeTarget,
		FaceOrbitingDirection,
		FaceOppositeOrbitingDirection
	};

	struct OrbitingData {
		real_t angle;
		real_t radius;
		OrbitingDirection direction;
		OrbitingTextureRotation texture_rotation;

		OrbitingData() = default;

		OrbitingData(real_t new_radius, OrbitingDirection new_direction, OrbitingTextureRotation new_texture_rotation) :
				angle(-10.0),
				radius(new_radius),
				direction(new_direction),
				texture_rotation(new_texture_rotation) {};

		bool check_is_already_locked_orbiting() const {
			return angle > -9.0f;
		}
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
	//

	// HOMING

	double homing_update_interval = 0.0;
	double homing_update_timer = 0.0;
	real_t homing_smoothing = 0.0;

	// Minimum distance (in pixels) from the homing target at which the bullet is considered to have reached it. Once within this distance, the bullet_homing_target_reached signal is emitted
	real_t distance_from_target_before_considering_as_reached = 5.0;

	// This tracks each bullet's homing deque - allows each bullet to have its own separate homing targets (per-bullet homing)
	std::vector<HomingTargetDeque> all_bullet_homing_targets;

	// For each bullet's homing deque, store the amount targets
	std::vector<int> all_homing_count;

	// Tracks how many bullets are currently homing in TOTAL (per-bullet homing, NOT shared) - basically determines whether the per-bullet homing feature is even turned on
	int active_homing_count = 0;

	// This is a shared homing deque - allows the bullets to share the same target
	HomingTargetDeque shared_homing_deque;

	//

public:
	// Updates all bullets' positions, rotations, and homing
	_ALWAYS_INLINE_ void move_bullets(double delta) {
		const bool is_using_physics_interpolation = bullet_factory->use_physics_interpolation;
		update_all_previous_transforms_for_interpolation();

		bool homing_interval_reached = false;

		bool shared_homing_deque_enabled = !shared_homing_deque.empty();
		const bool is_per_bullet_homing_enabled = (active_homing_count > 0);

		// If homing is enabled (either shared or per-bullet) update the timer and cache mouse position if needed
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
			shared_homing_deque_enabled = (targets_amount - trimmed) > 0;

			// If timer timed out, refresh the cached global position of the front target
			if (shared_homing_deque_enabled && homing_interval_reached) {
				shared_homing_deque.refresh_cached_front_target_global_position(cached_mouse_global_position);
			}
		}

		const bool is_orbiting_feature_enabled = (active_orbiting_count > 0);

		Vector2 homing_bullet_pos;
		Vector2 homing_target_pos;

		const bool shared_curves_data_enabled = shared_bullet_curves_data.is_valid();
		const BulletCurvesData2D *const shared_curves_ptr = shared_bullet_curves_data.ptr();
		bool is_per_bullet_curves_valid = false;
		const BulletCurvesData2D *per_bullet_curves_data = nullptr;

		// Loop only through ACTIVE bullets (skip the disabled ones)
		const auto &active_bullet_indexes = all_bullets_enabled_set.get_active_indexes();

		for (int i : active_bullet_indexes) {
			bool direction_got_updated = false;
			HomingTargetDeque *target_deque_used_for_orbiting = nullptr;

			// 1. STANDARD HOMING PHASE
			if (shared_homing_deque_enabled) { // Handle homing towards shared deque (takes precedence over per-bullet homing)
				update_homing(shared_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
				try_to_emit_bullet_homing_target_reached_signal(shared_homing_deque, shared_homing_deque_enabled, i, homing_bullet_pos, homing_target_pos);
				direction_got_updated = true;
				target_deque_used_for_orbiting = &shared_homing_deque;
			} else if (is_per_bullet_homing_enabled) { // Handle per-bullet homing
				// Whether the deque has any targets
				auto &curr_homing_count = all_homing_count[i];

				if (curr_homing_count > 0) {
					auto &curr_homing_deque = all_bullet_homing_targets[i];

					// Trim the invalid ones
					int trimmed_count = curr_homing_deque.bullet_homing_trim_front_invalid_targets(cached_mouse_global_position, curr_homing_count);

					// Very important to keep track of the amount of targets after trimming
					curr_homing_count -= trimmed_count; // count for the deque
					active_homing_count -= trimmed_count; // global count across all bullets that determines whether the per-bullet homing feature is even active

					if (curr_homing_count > 0) {
						// If per bullet homing is indeed active, then refresh the cache if interval has been reached
						if (homing_interval_reached) {
							curr_homing_deque.refresh_cached_front_target_global_position(cached_mouse_global_position);
						}

						update_homing(curr_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
						try_to_emit_bullet_homing_target_reached_signal(curr_homing_deque, shared_homing_deque_enabled, i, homing_bullet_pos, homing_target_pos);
						direction_got_updated = true;
						target_deque_used_for_orbiting = &curr_homing_deque;
					}
				}
			}

			auto &curr_bullet_transf = all_cached_instance_transforms[i];
			auto &curr_bullet_direction = all_cached_direction[i];

			// 2. CURVES & ROTATION DATA
			if (shared_curves_data_enabled) {
				apply_x_direction_curve(curr_bullet_direction, shared_curves_ptr);
				apply_y_direction_curve(curr_bullet_direction, shared_curves_ptr);
				apply_direction_curve_texture_rotation_if_needed(curr_bullet_direction, curr_bullet_transf, delta, shared_curves_ptr);
				direction_got_updated = true;
			} else {
				per_bullet_curves_data = find_bullet_curves_data(i);
				is_per_bullet_curves_valid = per_bullet_curves_data != nullptr;
				if (is_per_bullet_curves_valid) {
					per_bullet_curves_data = all_bullet_curves_data[i].ptr();
					apply_x_direction_curve(curr_bullet_direction, per_bullet_curves_data);
					apply_y_direction_curve(curr_bullet_direction, per_bullet_curves_data);
					apply_direction_curve_texture_rotation_if_needed(curr_bullet_direction, curr_bullet_transf, delta, per_bullet_curves_data);
					direction_got_updated = true;
				}
			}

			if (shared_curves_data_enabled && shared_curves_ptr->rotation_speed_curve.is_valid()) {
				update_rotation_using_curve(i, delta, shared_curves_ptr);
				bullet_accelerate_rotation_speed_using_curve(i, delta, shared_curves_ptr);
			} else if (is_per_bullet_curves_valid && per_bullet_curves_data->rotation_speed_curve.is_valid()) {
				update_rotation_using_curve(i, delta, per_bullet_curves_data);
				bullet_accelerate_rotation_speed_using_curve(i, delta, per_bullet_curves_data);
			} else if (is_rotation_data_active) {
				update_rotation(i, delta);
				bullet_accelerate_rotation_speed(i, delta);
			}

			if (adjust_direction_based_on_rotation) {
				curr_bullet_direction = all_cached_instance_transforms[i].columns[0].normalized();
				direction_got_updated = true;
			}

			// 3. VELOCITY CALCULATION
			Vector2 &velocity_delta = all_cached_velocity[i];
			if (direction_got_updated) {
				velocity_delta = curr_bullet_direction * all_cached_speed[i] + inherited_velocity_offset;
			}
			velocity_delta *= delta;

			// 4. MOVEMENT PATTERNS
			const bool use_pattern = check_exists_bullet_movement_pattern_data(i);
			if (use_pattern) {
				auto &pattern = all_movement_pattern_data[i];
				const Ref<Curve2D> &curve = pattern.path_curve;
				const real_t len = curve->get_baked_length();
				const real_t prev_dist = pattern.distance_traveled;
				const real_t advance_dist = velocity_delta.length();
				pattern.distance_traveled += advance_dist;
				const real_t s1 = Math::fmod(prev_dist, len);
				const real_t s2 = Math::fmod(pattern.distance_traveled, len);
				const int64_t l1 = (int64_t)(prev_dist / len);
				const int64_t l2 = (int64_t)(pattern.distance_traveled / len);
				const Vector2 start = curve->sample_baked(0.0);
				const Vector2 end = curve->sample_baked(len * 0.9999);
				const Vector2 disp = end - start;
				const Vector2 p1 = l1 * disp + (curve->sample_baked(s1) - start);
				const Vector2 p2 = l2 * disp + (curve->sample_baked(s2) - start);
				Vector2 local_delta = p2 - p1;
				Vector2 pattern_direction = local_delta.rotated(curr_bullet_direction.angle()).normalized();
				const real_t original_speed = velocity_delta.length();
				if (original_speed > 0.0001) {
					velocity_delta = pattern_direction * original_speed;
				}
				if (pattern.face_movement_direction && velocity_delta.length_squared() > 0.0001) {
					const Vector2 tangent = velocity_delta.normalized();
					curr_bullet_transf.columns[0] = tangent;
					curr_bullet_transf.columns[1] = Vector2(-tangent.y, tangent.x);
				}
				if (!pattern.repeat_pattern && pattern.distance_traveled >= len) {
					if (pattern.face_movement_direction) {
						const Vector2 logical_dir = curr_bullet_direction.normalized();
						curr_bullet_transf.columns[0] = logical_dir;
						curr_bullet_transf.columns[1] = Vector2(-logical_dir.y, logical_dir.x);
					}
					all_movement_pattern_data.erase(i);
				}
			}

			auto &curr_bullet_origin = all_cached_instance_origin[i];

			// 5. ORBITING LOGIC
			if (is_orbiting_feature_enabled && all_orbiting_status[i] && target_deque_used_for_orbiting != nullptr && !target_deque_used_for_orbiting->empty()) {
				OrbitingData *const orbiting_data = &all_orbiting_data[i];
				if (orbiting_data != nullptr) {
					const Vector2 to_target = curr_bullet_origin - homing_target_pos;
					const real_t current_dist = to_target.length();
					const bool already_locked = orbiting_data->check_is_already_locked_orbiting();

					// Track if we are ACTUALLY doing orbit movement this frame
					bool is_actually_orbiting = false;

					// Movement Logic (Locked or Boundary Arrival)
					if (already_locked) {
						real_t dir_multiplier = (orbiting_data->direction == OrbitRight) ? 1.0 : (orbiting_data->direction == OrbitLeft ? -1.0 : 0.0);
						if (dir_multiplier != 0.0) {
							real_t angular_speed = (all_cached_speed[i] / orbiting_data->radius) * dir_multiplier;
							orbiting_data->angle += angular_speed * delta;
						}
						Vector2 target_pos = homing_target_pos + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
						velocity_delta = target_pos - curr_bullet_origin;

						is_actually_orbiting = true;
					}
					// Check exact frame arrival:
					else if (Math::abs(current_dist - orbiting_data->radius) < (all_cached_speed[i] * delta)) {
						// REACHED RADIUS - LOCK NOW
						orbiting_data->angle = to_target.angle();
						Vector2 target_pos = homing_target_pos + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
						velocity_delta = target_pos - curr_bullet_origin;

						// We consider this frame as orbiting because we just snapped to the ring
						is_actually_orbiting = true;
					} else if (current_dist < orbiting_data->radius) {
						// SPAWNED INSIDE - PUSH OUT
						// This is technically NOT orbiting yet, it's just moving to the border
						Vector2 outward_dir = (current_dist > 0.1f) ? (to_target / current_dist) : Vector2(1, 0);
						real_t next_dist = current_dist + (all_cached_speed[i] * delta);
						Vector2 target_pos = homing_target_pos + (outward_dir * next_dist);
						velocity_delta = target_pos - curr_bullet_origin;
					}

					// 6. TEXTURE ROTATION
					// Only rotate if the bullet is PHYSICALLY orbiting (Locked or just snapped)
					if (is_actually_orbiting) {
						Vector2 look_dir;
						Vector2 radial_vec = (curr_bullet_origin - homing_target_pos).normalized();

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
						}

						if (look_dir != Vector2()) {
							rotate_to_target(i, look_dir, 0.0);
						}
					}
				}
			}

			// 7. TRANSFORM UPDATES
			curr_bullet_origin += velocity_delta;
			curr_bullet_transf.set_origin(curr_bullet_origin);

			auto &curr_shape_transf = all_cached_shape_transforms[i];
			auto &curr_shape_origin = all_cached_shape_origin[i];
			curr_shape_transf = curr_bullet_transf;
			Vector2 rotated_offset = cache_collision_shape_offset.rotated(curr_shape_transf.get_rotation());
			curr_shape_origin = curr_bullet_origin + rotated_offset;
			curr_shape_transf.set_origin(curr_shape_origin);

			physics_server->area_set_shape_transform(area, i, curr_shape_transf);
			move_bullet_attachment(velocity_delta, i);

			if (shared_curves_data_enabled && shared_curves_ptr->movement_speed_curve.is_valid()) {
				bullet_accelerate_speed_using_curve(i, delta, shared_curves_ptr);
			} else if (is_per_bullet_curves_valid && per_bullet_curves_data->movement_speed_curve.is_valid()) {
				bullet_accelerate_speed_using_curve(i, delta, per_bullet_curves_data);
			} else {
				bullet_accelerate_speed(i, delta);
			}

			if (!is_using_physics_interpolation) {
				multi->set_instance_transform_2d(i, all_cached_instance_transforms[i]);
			}
		}

		// Handle collisions safely after all physics processing logic is done
		for (auto &data : all_collided_bullets) {
			handle_bullet_collision(data.collision_type, data.bullet_index, data.collided_instance_id);
		}
		all_collided_bullets.clear();
	}

	///////////////// ORBITING DATA METHODS

	_ALWAYS_INLINE_ void bullet_enable_orbiting(int bullet_index, real_t orbiting_radius, OrbitingDirection orbiting_direction, OrbitingTextureRotation orbiting_texture_rotation) {
		if (!validate_bullet_index(bullet_index, "bullet_enable_orbiting")) {
			return;
		}

		// ONLY increment if it was previously disabled
		if (all_orbiting_status[bullet_index] == 0) {
			active_orbiting_count++;
		}

		all_orbiting_data[bullet_index] = OrbitingData(orbiting_radius, orbiting_direction, orbiting_texture_rotation);
		all_orbiting_status[bullet_index] = 1;
	}

	_ALWAYS_INLINE_ void bullet_disable_orbiting(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_disable_orbiting")) {
			return;
		}

		// ONLY decrement if it was previously enabled
		if (all_orbiting_status[bullet_index] == 1) {
			active_orbiting_count--;
		}

		all_orbiting_status[bullet_index] = 0;
	}

	/////////////////

	///////////// PER BULLET HOMING DEQUE POP METHODS

	_ALWAYS_INLINE_ Variant bullet_homing_pop_front_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_front_target") || !bullet_check_has_homing_targets(bullet_index)) {
			return nullptr;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		--all_homing_count[bullet_index];
		--active_homing_count;

		return queue.pop_front_target(cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ Variant bullet_homing_pop_back_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_pop_back_target") || !bullet_check_has_homing_targets(bullet_index)) {
			return nullptr;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		--all_homing_count[bullet_index];
		--active_homing_count;

		return queue.pop_back_target(cached_mouse_global_position);
	}
	/////////////////////

	//////////////// PER BULLET HOMING DEQUE PUSH METHODS

	_ALWAYS_INLINE_ bool bullet_homing_push_front_mouse_position_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_mouse_position_target")) {
			return false;
		}

		if (HomingTargetDeque::mouse_homing_targets_amount <= 0) {
			cached_mouse_global_position = get_global_mouse_position();
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_front_mouse_position_target(cached_mouse_global_position);

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_node2d_target")) {
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_front_node2d_target(new_homing_target);

		++all_homing_count[bullet_index];
		++active_homing_count;
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_front_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_front_global_position_target")) {
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_front_global_position_target(global_position);

		++all_homing_count[bullet_index];
		++active_homing_count;
		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_mouse_position_target(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_mouse_position_target")) {
			return false;
		}

		if (HomingTargetDeque::mouse_homing_targets_amount <= 0) {
			cached_mouse_global_position = get_global_mouse_position();
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_back_mouse_position_target(cached_mouse_global_position);

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_node2d_target(int bullet_index, Node2D *new_homing_target) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_node2d_target")) {
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_back_node2d_target(new_homing_target);

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}

	_ALWAYS_INLINE_ bool bullet_homing_push_back_global_position_target(int bullet_index, const Vector2 &global_position) {
		if (!validate_bullet_index(bullet_index, "bullet_homing_push_back_global_position_target")) {
			return false;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		queue.push_back_global_position_target(global_position);

		++all_homing_count[bullet_index];
		++active_homing_count;

		return true;
	}
	/////////////////////////////

	///  PER BULLET HOMING DEQUE HELPERS

	_ALWAYS_INLINE_ void bullet_clear_homing_targets(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_clear_homing_targets")) {
			return;
		}

		auto &queue = all_bullet_homing_targets[bullet_index];

		auto &count = all_homing_count[bullet_index];
		active_homing_count -= count;
		count = 0;

		queue.clear_homing_targets(cached_mouse_global_position);
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

	_ALWAYS_INLINE_ void all_bullets_push_back_mouse_position_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_mouse_position_target");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_homing_push_back_mouse_position_target(i);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_mouse_position_target(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_mouse_position_target");
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_homing_push_front_mouse_position_target(i);
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_back_homing_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_back_homing_target");
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				bullet_homing_push_back_node2d_target(i, node);
			}
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			Vector2 global_pos = node2d_or_global_position;
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				bullet_homing_push_back_global_position_target(i, global_pos);
			}
		} else {
			UtilityFunctions::push_error("Invalid homing target type in all_bullets_push_back_homing_target");
		}
	}

	_ALWAYS_INLINE_ void all_bullets_push_front_homing_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_push_front_homing_target");
		if (Node2D *node = Object::cast_to<Node2D>(node2d_or_global_position)) {
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
				bullet_homing_push_front_node2d_target(i, node);
			}
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			Vector2 global_pos = node2d_or_global_position;
			for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
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

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_new_target(const Variant &node2d_or_global_position, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_new_target");
		all_bullets_clear_homing_targets(bullet_index_start, bullet_index_end_inclusive);
		all_bullets_push_back_homing_target(node2d_or_global_position, bullet_index_start, bullet_index_end_inclusive);
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_new_target_array");
		all_bullets_clear_homing_targets(bullet_index_start, bullet_index_end_inclusive);
		all_bullets_push_back_homing_targets_array(node2ds_or_global_positions_array, bullet_index_start, bullet_index_end_inclusive);
	}

	_ALWAYS_INLINE_ void all_bullets_replace_homing_targets_with_mouse(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_replace_homing_targets_with_mouse");

		// Cache once for the whole loop
		cached_mouse_global_position = get_global_mouse_position();

		all_bullets_clear_homing_targets(bullet_index_start, bullet_index_end_inclusive);
		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			all_bullet_homing_targets[i].push_back_mouse_position_target(cached_mouse_global_position);
			++all_homing_count[i];
			++active_homing_count;
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

	_ALWAYS_INLINE_ Variant shared_homing_deque_pop_front_target() {
		return shared_homing_deque.pop_front_target(cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ Variant shared_homing_deque_pop_back_target() {
		return shared_homing_deque.pop_back_target(cached_mouse_global_position);
	}

	// SHARED BULLET HOMING DEQUE PUSH METHODS

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_mouse_position_target() {
		if (HomingTargetDeque::mouse_homing_targets_amount <= 0) {
			cached_mouse_global_position = get_global_mouse_position();
		}

		shared_homing_deque.push_front_mouse_position_target(cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_node2d_target(Node2D *new_homing_target) {
		shared_homing_deque.push_front_node2d_target(new_homing_target);
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_front_global_position_target(const Vector2 &global_position) {
		shared_homing_deque.push_front_global_position_target(global_position);
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_mouse_position_target() {
		if (HomingTargetDeque::mouse_homing_targets_amount <= 0) {
			cached_mouse_global_position = get_global_mouse_position();
		}

		shared_homing_deque.push_back_mouse_position_target(cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_node2d_target(Node2D *new_homing_target) {
		shared_homing_deque.push_back_node2d_target(new_homing_target);
	}

	_ALWAYS_INLINE_ void shared_homing_deque_push_back_global_position_target(const Vector2 &global_position) {
		shared_homing_deque.push_back_global_position_target(global_position);
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
			}
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_clear_homing_targets() {
		shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ void shared_homing_deque_replace_homing_targets_with_new_target(const Variant &node2d_or_global_position) {
		Node2D *node2d_target = Object::cast_to<Node2D>(node2d_or_global_position);

		if (node2d_target) {
			shared_homing_deque_clear_homing_targets();
			shared_homing_deque_push_back_node2d_target(node2d_target);
		} else if (node2d_or_global_position.get_type() == Variant::VECTOR2) {
			shared_homing_deque_clear_homing_targets();
			shared_homing_deque_push_back_global_position_target(node2d_or_global_position);
		} else {
			UtilityFunctions::push_error("Invalid type passed to shared_homing_deque_replace_homing_targets_with_new_target");
		}
	}

	_ALWAYS_INLINE_ void shared_homing_deque_replace_homing_targets_with_new_target_array(const Array &node2ds_or_global_positions_array) {
		shared_homing_deque_clear_homing_targets();

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

	// Teleports a bullet to a new global position
	_ALWAYS_INLINE_ void teleport_bullet(int bullet_index, const Vector2 &new_global_pos) {
		if (!validate_bullet_index(bullet_index, "teleport_bullet")) {
			return;
		}

		// Bullet texture related
		auto &curr_bullet_transf = all_cached_instance_transforms[bullet_index];
		auto &curr_bullet_origin = all_cached_instance_origin[bullet_index];

		// Bullet shape related
		auto &curr_shape_transf = all_cached_shape_transforms[bullet_index];
		auto &curr_shape_origin = all_cached_shape_origin[bullet_index];

		// Update the bullet origin and transform
		curr_bullet_origin = new_global_pos;
		curr_bullet_transf.set_origin(curr_bullet_origin);

		// Update the collision shape
		// The shape transform is based on the bullet transform plus an offset so it should always follow it no matter how the bullet moves
		curr_shape_transf = curr_bullet_transf;

		// The user had previously set a collision shape offset relative to the center of the texture, so it needs to be re-calculated by taking into account the new rotation of the bullet
		Vector2 rotated_offset = cache_collision_shape_offset.rotated(curr_shape_transf.get_rotation());

		// Update the shape origin
		curr_shape_origin = curr_bullet_origin + rotated_offset;

		// Update the shape transform origin with the rotated offset
		curr_shape_transf.set_origin(curr_shape_origin);

		// Instantly apply the updated transforms
		if (all_bullets_enabled_set.contains(bullet_index)) { // Apply to multi only if the bullet is enabled (if disabled the transform is zero which prevents the multimesh from rendering it)
			multi->set_instance_transform_2d(bullet_index, curr_bullet_transf);
		}

		physics_server->area_set_shape_transform(area, bullet_index, curr_shape_transf);

		// Reset physics interpolation data
		update_bullet_previous_transform_for_interpolation(bullet_index);
	}

	// Shifts a bullet's position by a certain amount
	_ALWAYS_INLINE_ void teleport_shift_bullet(int bullet_index, const Vector2 &shift_amount) {
		if (!validate_bullet_index(bullet_index, "teleport_shift_bullet")) {
			return;
		}

		// Bullet texture related
		auto &curr_bullet_transf = all_cached_instance_transforms[bullet_index];
		auto &curr_bullet_origin = all_cached_instance_origin[bullet_index];

		// Bullet shape related
		auto &curr_shape_transf = all_cached_shape_transforms[bullet_index];
		auto &curr_shape_origin = all_cached_shape_origin[bullet_index];

		// Update the bullet origin and transform
		curr_bullet_origin += shift_amount;
		curr_bullet_transf.set_origin(curr_bullet_origin);

		// Update the collision shape
		// The shape transform is based on the bullet transform plus an offset so it should always follow it no matter how the bullet moves
		curr_shape_transf = curr_bullet_transf;

		// The user had previously set a collision shape offset relative to the center of the texture, so it needs to be re-calculated by taking into account the new rotation of the bullet
		Vector2 rotated_offset = cache_collision_shape_offset.rotated(curr_shape_transf.get_rotation());

		// Update the shape origin
		curr_shape_origin = curr_bullet_origin + rotated_offset;

		// Update the shape transform origin with the rotated offset
		curr_shape_transf.set_origin(curr_shape_origin);

		// Instantly apply the updated transforms
		if (all_bullets_enabled_set.contains(bullet_index)) { // Apply to multi only if the bullet is enabled (if disabled the transform is zero which prevents the multimesh from rendering it)
			multi->set_instance_transform_2d(bullet_index, curr_bullet_transf);
		}

		physics_server->area_set_shape_transform(area, bullet_index, curr_shape_transf);

		// Reset physics interpolation data
		update_bullet_previous_transform_for_interpolation(bullet_index);
	}

	_ALWAYS_INLINE_ void teleport_shift_all_bullets(const Vector2 &shift_amount, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "teleport_shift_all_bullets");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			teleport_shift_bullet(i, shift_amount);
		}
	}

	// Property getters and setters
	real_t get_homing_smoothing() const { return homing_smoothing; }
	void set_homing_smoothing(real_t value) { homing_smoothing = value; }
	real_t get_homing_update_interval() const { return homing_update_interval; }
	void set_homing_update_interval(real_t value) { homing_update_interval = value; }
	bool get_homing_take_control_of_texture_rotation() const { return homing_take_control_of_texture_rotation; }
	void set_homing_take_control_of_texture_rotation(bool value) { homing_take_control_of_texture_rotation = value; }
	bool get_bullet_homing_auto_pop_after_target_reached() const { return bullet_homing_auto_pop_after_target_reached; }
	void set_bullet_homing_auto_pop_after_target_reached(bool value) { bullet_homing_auto_pop_after_target_reached = value; }
	real_t get_distance_from_target_before_considering_as_reached() const { return distance_from_target_before_considering_as_reached; }
	void set_distance_from_target_before_considering_as_reached(real_t value) { distance_from_target_before_considering_as_reached = value; }
	bool get_shared_homing_deque_auto_pop_after_target_reached() const { return shared_homing_deque_auto_pop_after_target_reached; }
	void set_shared_homing_deque_auto_pop_after_target_reached(bool value) { shared_homing_deque_auto_pop_after_target_reached = value; }

	// Virtual methods
	void set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data);
	virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) override final;
	virtual void custom_additional_enable_logic(const MultiMeshBulletsData2D &data) override final;
	virtual void custom_additional_disable_logic() override final;

protected:
	// Updates homing behavior for a bullet
	_ALWAYS_INLINE_ void update_homing(HomingTargetDeque &homing_deque, int bullet_index, double delta, Vector2 &bullet_pos, Vector2 &target_pos) {
		// Get the front target's cached position
		target_pos = homing_deque.get_cached_front_target_global_position();

		bullet_pos = all_cached_instance_origin[bullet_index];
		Vector2 diff = target_pos - bullet_pos;

		real_t dist_sq = diff.length_squared();
		if (dist_sq <= 0.0) {
			return;
		}

		real_t max_turn = homing_smoothing * delta;
		if (max_turn < 0.0) {
			max_turn = 0.0;
		}

		Vector2 &current_direction = all_cached_direction[bullet_index];

		auto &curr_transf = all_cached_instance_transforms[bullet_index];
		// If rotation is controlled via movement pattern or rotation data, just set direction directly toward target
		if (check_exists_bullet_movement_pattern_data(bullet_index) || is_rotation_data_active) {
			current_direction = diff.normalized();
		} else { // Otherwise use smoothing to rotate toward target
			// Rotate toward target with smoothing
			rotate_to_target(bullet_index, diff, max_turn);

			// Get the new direction based on the rotated transform
			current_direction = curr_transf[0].normalized();
		}
	}

	// Rotates bullet to face target with smoothing (boundary-agnostic version)
	_ALWAYS_INLINE_ void rotate_to_target(int bullet_index, const Vector2 &diff, real_t max_turn) {
		if (!homing_take_control_of_texture_rotation || diff.length_squared() <= 0.0) {
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

		bool use_smoothing = max_turn > 0.0; // Hoist for clamp

		// Apply smoothing clamp
		if (use_smoothing) {
			delta_rot = Math::clamp(delta_rot, -max_turn, max_turn);
		}

		// Rotate locally
		rotate_transform_locally(all_cached_instance_transforms[bullet_index], delta_rot);

		if (!use_smoothing) {
			update_bullet_previous_transform_for_interpolation(bullet_index);
		}
	}

	// Updates bullet rotation based on rotation speed
	_ALWAYS_INLINE_ void update_rotation(int bullet_index, double delta) {
		real_t cache_rotation_speed = all_rotation_speed[bullet_index];
		real_t rot_delta = cache_rotation_speed * (real_t)delta;

		// Apply rotation if active or speed > 0
		if (cache_rotation_speed != 0.0f) {
			bool max_reached = cache_rotation_speed >= all_max_rotation_speed[bullet_index];

			if (!(max_reached && stop_rotation_when_max_reached)) {
				rotate_transform_locally(all_cached_instance_transforms[bullet_index], rot_delta);
			}
		}
	}

	// Updates bullet rotation based on rotation speed using a curve
	_ALWAYS_INLINE_ void update_rotation_using_curve(int bullet_index, double delta, const BulletCurvesData2D *curves_data) {
		real_t cache_rotation_speed = all_rotation_speed[bullet_index];
		real_t rot_delta = cache_rotation_speed * (real_t)delta;

		// If the curves data is valid it means we are using it, so just apply it
		if (curves_data != nullptr && curves_data->rotation_speed_curve.is_valid()) {
			rotate_transform_locally(all_cached_instance_transforms[bullet_index], rot_delta);
		}
	}

	_ALWAYS_INLINE_ void try_to_emit_bullet_homing_target_reached_signal(HomingTargetDeque &homing_deque, bool is_using_shared_homing_deque, int bullet_index, const Vector2 &bullet_pos, const Vector2 &target_pos) {
		// Reached check: Post-move, direct to actual target
		Vector2 post_to_target = target_pos - bullet_pos;
		real_t post_dist_sq = post_to_target.length_squared();
		real_t threshold_sq = distance_from_target_before_considering_as_reached * distance_from_target_before_considering_as_reached;
		if (post_dist_sq <= threshold_sq) { // Fully squared for perf

			HomingTarget &target = homing_deque.front();

			// Ensure that the signal is emitted only ONCE when the target is reached by the bullet
			if (!target.has_bullet_reached_target) {
				target.has_bullet_reached_target = true;
				switch (target.type) {
					case GlobalPositionTarget:
						call_deferred("emit_signal", "bullet_homing_target_reached", this, bullet_index, nullptr, target_pos);
						break;
					case Node2DTarget: {
						auto &target_data = target.node2d_target_data;

						// In case the target instance is freed - will still emit the signal, but with a nullptr as the target
						if (!homing_deque.is_homing_target_valid(target_data.target, target_data.cached_valid_instance_id)) {
							call_deferred("emit_signal", "bullet_homing_target_reached", this, bullet_index, nullptr, target_pos);
							break;
						}

						call_deferred("emit_signal", "bullet_homing_target_reached", this, bullet_index, target_data.target, target_pos);
						break;
					}
					case NotHoming:
						break;
					case MousePositionTarget:
						call_deferred("emit_signal", "bullet_homing_target_reached", this, bullet_index, nullptr, target_pos);
						break;
				}

				// Pop the front target automatically if that's what the user wants
				if (is_using_shared_homing_deque) {
					if (shared_homing_deque_auto_pop_after_target_reached) {
						call_deferred("shared_homing_deque_pop_front_target");
					}
				} else {
					if (bullet_homing_auto_pop_after_target_reached) {
						call_deferred("bullet_homing_pop_front_target", bullet_index);
					}
				}
			}
		}
	}

	// Normalizes an angle to [-PI, PI]
	_ALWAYS_INLINE_ void normalize_angle(real_t &angle) const {
		angle = Math::wrapf(angle, -Math_PI, Math_PI);
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
