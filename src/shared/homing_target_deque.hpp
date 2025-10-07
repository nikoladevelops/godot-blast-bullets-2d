#pragma once

#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include <deque>

using namespace godot;

namespace BlastBullets2D {

// The supported homing types
enum HomingType {
	NotHoming,
	GlobalPositionTarget,
	Node2DTarget,
	MousePositionTarget
};

// Stores a Node2D target and its instance ID for validation
struct Node2DTargetData {
	Node2D *target;
	uint64_t cached_valid_instance_id;

	Node2DTargetData(Node2D *node, uint64_t valid_instance_id) :
			target(node), cached_valid_instance_id(valid_instance_id) {}
};

// Represents a homing target
struct HomingTarget {
	HomingType type = HomingType::NotHoming;
	bool has_bullet_reached_target = false;

	union {
		Vector2 global_position_target;
		Node2DTargetData node2d_target_data;
	};

	HomingTarget() :
			type(HomingType::NotHoming), has_bullet_reached_target(false) {
		global_position_target = Vector2(0, 0); // Safe fallback
	}

	HomingTarget(Vector2 pos) :
			type(GlobalPositionTarget), global_position_target(pos) {}
	HomingTarget(Node2D *node, uint64_t id) :
			type(Node2DTarget), node2d_target_data(node, id) {}
};

class HomingTargetDeque {
public:
	void resize(int new_size) {
		homing_targets.resize(new_size);
	}

	HomingTarget &front() {
		return homing_targets.front();
	}

	const HomingTarget &front() const {
		return homing_targets.front();
	}

	HomingTarget &back() {
		return homing_targets.back();
	}

	const HomingTarget &back() const {
		return homing_targets.back();
	}

	bool empty() const noexcept{
		return homing_targets.empty();
	}

	_ALWAYS_INLINE_ int get_homing_targets_amount() const {
		return homing_targets.size();
	}

	_ALWAYS_INLINE_ bool has_homing_targets() const {
		return get_homing_targets_amount() > 0;
	}

	// Checks if a homing target is valid
	_ALWAYS_INLINE_ bool is_homing_target_valid(const Node *target, uint64_t cached_instance_id) const {
		return target != nullptr && UtilityFunctions::is_instance_id_valid(cached_instance_id);
	}

	_ALWAYS_INLINE_ void bullet_homing_trim_front_invalid_targets(const Vector2 &cached_mouse_global_position) {
		while (!homing_targets.empty()) {
			HomingTarget &target = homing_targets.front();

			switch (target.type) {
				case NotHoming:
				case GlobalPositionTarget:
				case MousePositionTarget:
					return; // valid, stop trimming

				case Node2DTarget: {
					auto &target_data = target.node2d_target_data;

					if (!is_homing_target_valid(target_data.target, target_data.cached_valid_instance_id)) {
						pop_front_target(cached_mouse_global_position);
						continue;
					}
					// As soon as you find an instance that is valid, stop looping
					return;
				}
				default:
					UtilityFunctions::push_error(
							"Unsupported HomingTarget type in bullet_homing_trim_front_invalid_targets");
					return;
			}
		}
	}

	// Checks whether the bullet is homing a valid target and updates the target_pos with the proper global_position of the target. If the target is invalid node2d it will pop it.
	_ALWAYS_INLINE_ Vector2 get_cached_front_target_global_position() const {
		return cached_front_target_global_position;
	}

	// Updates the cached_front_target_global_position. Note that the argument you pass is the CACHED MOUSE POSITION, NOT THE NEW VALUE
	_ALWAYS_INLINE_ void refresh_cached_front_target_global_position(const Vector2 &cached_mouse_global_position) {
		if (!homing_targets.empty()) {
			const HomingTarget &front = homing_targets.front();

			switch (front.type) {
				case HomingType::GlobalPositionTarget:
					// No need to refresh cache since the global position will never change
					break;
				case HomingType::Node2DTarget:
					// In this case the target is 100% valid since get_homing_target_global_position checks it
					cached_front_target_global_position = front.node2d_target_data.target->get_global_position();
					break;
				case HomingType::NotHoming: // This case should never happen but just in case..
					return;
				case MousePositionTarget:
					cached_front_target_global_position = cached_mouse_global_position;
					break;
			}
		}
	}

	////////////////////// POP METHODS

	_ALWAYS_INLINE_ Variant pop_front_target(const Vector2 &cached_mouse_global_position) {
		uint64_t queue_size = homing_targets.size();

		if (queue_size == 0) {
			return nullptr;
		}

		HomingTarget target = homing_targets.front();
		homing_targets.pop_front();

		const Vector2 cached_pos = cached_front_target_global_position; //cached_bullet_homing_deque_front_target_global_positions[bullet_index];

		if (queue_size > 1) { // If the old size was bigger than 1 it means there is an element that will now be the front of the queue
			HomingTarget next_target = homing_targets.front();

			switch (next_target.type) {
				case GlobalPositionTarget:
					cached_front_target_global_position = next_target.global_position_target;
					break;
				case Node2DTarget: {
					auto &next_target_data = next_target.node2d_target_data;

					// If its not valid no need to edit cache since it wont be used either way..
					if (!is_homing_target_valid(next_target_data.target, next_target_data.cached_valid_instance_id)) {
						break;
					}

					cached_front_target_global_position = next_target_data.target->get_global_position();
					break;
				}
				case NotHoming:
					break;
				case MousePositionTarget:
					cached_front_target_global_position = cached_mouse_global_position;
					break;
			}
		}

		switch (target.type) {
			case GlobalPositionTarget: {
				return target.global_position_target;
			}
			case Node2DTarget: {
				auto &target_data = target.node2d_target_data;

				if (!is_homing_target_valid(target_data.target, target_data.cached_valid_instance_id)) {
					return nullptr;
				}

				return target_data.target;
			}
			case NotHoming: {
				return nullptr;
			}
			case MousePositionTarget:
				--mouse_homing_targets_amount;

				return cached_pos;
		}
		return nullptr;
	}

	_ALWAYS_INLINE_ Variant pop_back_target(const Vector2 &cached_mouse_global_position) {
		if (homing_targets.empty()) {
			return nullptr;
		}

		HomingTarget target = homing_targets.back();
		homing_targets.pop_back();

		switch (target.type) {
			case GlobalPositionTarget: {
				return target.global_position_target;
			}
			case Node2DTarget: {
				auto &target_data = target.node2d_target_data;

				if (!is_homing_target_valid(target_data.target, target_data.cached_valid_instance_id)) {
					return nullptr;
				}

				return target_data.target;
			}
			case NotHoming: {
				return nullptr;
			}
			case MousePositionTarget:
				--mouse_homing_targets_amount;

				// It is a bit weird since this isn't really the global mouse position owned by that particular MousePositionTarget (since obviously it is not yet active),
				// but it's fine we are returning the most recently cached mouse global position for the queue of homing targets
				// I'm doing this to avoid returning a nullptr while also the global position being garbage as well.. so best I can do is return this
				return cached_mouse_global_position;
		}
		return nullptr;
	}

	//////////////////////////////////////////////

	//// PUSH METHODS
	_ALWAYS_INLINE_ void push_front_mouse_position_target(const Vector2 &cached_mouse_global_position) {
		HomingTarget target;
		target.type = HomingType::MousePositionTarget;

		++mouse_homing_targets_amount;

		cached_front_target_global_position = cached_mouse_global_position;

		homing_targets.emplace_front(target);
	}

	_ALWAYS_INLINE_ void push_front_node2d_target(Node2D *new_homing_target) {
		homing_targets.emplace_front(new_homing_target, new_homing_target->get_instance_id());

		cached_front_target_global_position = new_homing_target->get_global_position();
	}

	_ALWAYS_INLINE_ void push_front_global_position_target(const Vector2 &global_position) {
		homing_targets.emplace_front(global_position);

		cached_front_target_global_position = global_position;
	}

	_ALWAYS_INLINE_ void push_back_mouse_position_target(const Vector2 &cached_mouse_global_position) {
		HomingTarget target;
		target.type = HomingType::MousePositionTarget;

		bool is_queue_empty = homing_targets.empty();

		++mouse_homing_targets_amount;

		homing_targets.emplace_back(target);

		if (is_queue_empty) {
			cached_front_target_global_position = cached_mouse_global_position;
		}
	}

	_ALWAYS_INLINE_ void push_back_node2d_target(Node2D *new_homing_target) {
		bool is_queue_empty = homing_targets.empty();

		homing_targets.emplace_back(new_homing_target, new_homing_target->get_instance_id());

		// Update the cached global position since it will be used - target is at the front of the queue
		if (is_queue_empty) {
			cached_front_target_global_position = new_homing_target->get_global_position();
		}
	}

	_ALWAYS_INLINE_ void push_back_global_position_target(const Vector2 &global_position) {
		bool is_queue_empty = homing_targets.empty();

		homing_targets.emplace_back(global_position);

		// Update the cached global position since it will be used - target is at the front of the queue
		if (is_queue_empty) {
			cached_front_target_global_position = global_position;
		}
	}

	///////////////////////////////////////

	///  OTHER HOMING HELPERS

	_ALWAYS_INLINE_ void clear_homing_targets(const Vector2 &cached_mouse_global_position) {
		while (!homing_targets.empty()) {
			// This is intentional because some homing targets have pop logic that needs to stay consistent (that's why using .clear is unsafe)
			// The mouse tracking logic relies on this currently
			pop_back_target(cached_mouse_global_position);
		}
	}

	_ALWAYS_INLINE_ HomingType get_current_target_type() const {
		if (homing_targets.empty()) {
			return HomingType::NotHoming;
		}

		return homing_targets.front().type;
	}

	_ALWAYS_INLINE_ Variant get_bullet_current_homing_target() const {
		if (homing_targets.empty()) {
			return nullptr;
		}

		const HomingTarget &target = homing_targets.front();

		switch (target.type) {
			case GlobalPositionTarget: {
				return target.global_position_target;
			}
			case Node2DTarget: {
				auto &target_data = target.node2d_target_data;

				if (!is_homing_target_valid(target_data.target, target_data.cached_valid_instance_id)) {
					return nullptr;
				}

				return target_data.target;
			}
			case NotHoming: {
				return nullptr;
			}
			case MousePositionTarget:
				return cached_front_target_global_position;
		}
		return nullptr;
	}
	//////////////////////////////////////

	// The idea behind this is to track whether the multimesh even has the need of tracking the mouse global position - enables caching behavior
	// Not safe for multithreading by default
	static inline int mouse_homing_targets_amount = 0;

private:
	std::deque<HomingTarget> homing_targets;
	mutable Vector2 cached_front_target_global_position{ 0, 0 };
};
} //namespace BlastBullets2D
