#pragma once

#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include <utility>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

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
	Vector2 global_position_target{ 0, 0 };
	Node2DTargetData node2d_target_data{ nullptr, 0 };

	HomingTarget() = default;

	HomingTarget(Vector2 pos) :
			type(GlobalPositionTarget), global_position_target(pos) {}
	HomingTarget(Node2D *node, uint64_t id) :
			type(Node2DTarget), node2d_target_data(node, id) {}
};

// Double-ended queue that allocates NOTHING while empty: every bullet owns
// one, and std::deque allocates its map and a first node on construction
// (two mallocs and ~0.5 KB per bullet on every cold spawn, homing or not).
// A ring over a vector that doubles when full; slots are value types.
template <typename T>
class RingDeque2D {
public:
	bool empty() const noexcept { return count == 0; }
	int size() const noexcept { return count; }
	T &front() { return buf[head]; }
	const T &front() const { return buf[head]; }
	T &back() { return buf[wrap(head + count - 1)]; }
	void emplace_back(const T &value) {
		grow_if_full();
		buf[wrap(head + count)] = value;
		++count;
	}
	void emplace_front(const T &value) {
		grow_if_full();
		head = wrap(head + capacity() - 1);
		buf[head] = value;
		++count;
	}
	void pop_front() {
		buf[head] = T();
		head = wrap(head + 1);
		--count;
	}
	void pop_back() {
		buf[wrap(head + count - 1)] = T();
		--count;
	}

private:
	std::vector<T> buf;
	int head = 0;
	int count = 0;

	int capacity() const { return (int)buf.size(); }
	int wrap(int index) const { return index % capacity(); }
	void grow_if_full() {
		if (count < capacity()) {
			return;
		}
		std::vector<T> grown(capacity() == 0 ? 4 : capacity() * 2);
		for (int k = 0; k < count; ++k) {
			grown[k] = std::move(buf[wrap(head + k)]);
		}
		buf.swap(grown);
		head = 0;
	}
};

class HomingTargetDeque {
public:
	// NOTE: no resize() on purpose - growth via std::deque::resize would insert
	// default HomingTargets (NotHoming) that block trimming and home toward a stale
	// cache. Deques are sized implicitly by push/pop only.
	// Per-deque cap: pushes are user-driven and unbounded by default (a
	// push-per-tick script would grow memory and per-tick trim cost forever).
	static constexpr int MAX_HOMING_TARGETS_PER_DEQUE = 256;

	// Rejects the push when full (warn + false/no-store) so callers that
	// track counters never count a target that was never stored.
	_ALWAYS_INLINE_ bool has_room_for_push() const {
		if ((int)homing_targets.size() >= MAX_HOMING_TARGETS_PER_DEQUE) {
			UtilityFunctions::push_error("HomingTargetDeque is full (256 targets). Pop or clear before pushing more.");
			return false;
		}
		return true;
	}

	HomingTarget &front() {
		return homing_targets.front();
	}

	const HomingTarget &front() const {
		return homing_targets.front();
	}

	bool empty() const noexcept {
		return homing_targets.empty();
	}

	_ALWAYS_INLINE_ int get_homing_targets_amount() const {
		return homing_targets.size();
	}

	_ALWAYS_INLINE_ bool has_homing_targets() const {
		return get_homing_targets_amount() > 0;
	}

	// Checks if a homing target is valid. Follows the same guard pattern as
	// BulletSpawner2D::is_tracked_node_alive(): the cached id must still be
	// registered and must still belong to this exact pointer. A failed check
	// means "gone": callers trim/pop instead of dereferencing.
	_ALWAYS_INLINE_ bool is_homing_target_valid(const Node *target, uint64_t cached_instance_id) const {
		if (target == nullptr || !UtilityFunctions::is_instance_id_valid(cached_instance_id)) {
			return false;
		}
		return target->get_instance_id() == cached_instance_id;
	}

	// Trims invalid targets from the front of the deque - returns the amount of targets trimmed
	_ALWAYS_INLINE_ int bullet_homing_trim_front_invalid_targets(const Vector2 &cached_mouse_global_position, int current_target_count) {
		int trimmed_count = 0;
		while (trimmed_count < current_target_count && !homing_targets.empty()) {
			HomingTarget &target = homing_targets.front();

			switch (target.type) {
				case NotHoming:
				case GlobalPositionTarget:
				case MousePositionTarget:
					return trimmed_count; // valid, stop trimming

				case Node2DTarget: {
					auto &target_data = target.node2d_target_data;

					if (!is_homing_target_valid(target_data.target, target_data.cached_valid_instance_id)) {
						pop_front_target(cached_mouse_global_position);
						++trimmed_count;
						continue;
					}
					// As soon as you find an instance that is valid, stop looping
					return trimmed_count;
				}
				default:
					UtilityFunctions::push_error(
							"Unsupported HomingTarget type in bullet_homing_trim_front_invalid_targets");
					return trimmed_count;
			}
		}
		return trimmed_count;
	}

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
				case HomingType::Node2DTarget: {
					auto &d = front.node2d_target_data;
					if (!is_homing_target_valid(d.target, d.cached_valid_instance_id)) {
						break;
					}
					cached_front_target_global_position = d.target->get_global_position();
					break;
				}
				case HomingType::NotHoming: // This case should never happen but just in case..
					return;
				case MousePositionTarget:
					cached_front_target_global_position = cached_mouse_global_position;
					break;
			}
		}
	}

	////////////////////// POP METHODS

	inline Variant pop_front_target(const Vector2 &cached_mouse_global_position) {
		uint64_t queue_size = homing_targets.size();

		if (queue_size == 0) {
			return nullptr;
		}

		HomingTarget target = homing_targets.front();
		homing_targets.pop_front();

		const Vector2 cached_pos = cached_front_target_global_position;

		if (queue_size > 1) { // If the old size was bigger than 1 it means there is an element that will now be the front of the queue
			HomingTarget next_target = homing_targets.front();

			switch (next_target.type) {
				case GlobalPositionTarget:
					cached_front_target_global_position = next_target.global_position_target;
					break;
				case Node2DTarget: {
					auto &next_target_data = next_target.node2d_target_data;

					if (!is_homing_target_valid(next_target_data.target, next_target_data.cached_valid_instance_id)) {
						// Next is invalid - will be trimmed next frame, keep cache stale-free
						cached_front_target_global_position = Vector2(0, 0);
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

		if (target.type == MousePositionTarget) {
			--mouse_homing_targets_amount;
		}
		return target_as_variant(target, cached_pos);
	}

	_ALWAYS_INLINE_ Variant pop_back_target(const Vector2 &cached_mouse_global_position) {
		if (homing_targets.empty()) {
			return nullptr;
		}

		HomingTarget target = homing_targets.back();
		homing_targets.pop_back();

		if (target.type == MousePositionTarget) {
			--mouse_homing_targets_amount;
		}
		// A mouse target has no position of its own: the last cached mouse
		// position (good for one frame until the cache refreshes).
		return target_as_variant(target, cached_mouse_global_position);
	}

	//////////////////////////////////////////////

	//// PUSH METHODS
	// Each returns false (nothing stored) when the push is rejected: a null
	// node, a non-finite position or a full deque. Callers that track
	// counters never count a target that was never stored.
	_ALWAYS_INLINE_ bool push_front_mouse_position_target(const Vector2 &cached_mouse_global_position) {
		HomingTarget target;
		target.type = HomingType::MousePositionTarget;
		return store(target, true, cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ bool push_front_node2d_target(Node2D *new_homing_target) {
		if (!new_homing_target) {
			UtilityFunctions::push_error("push_front_node2d_target: target is null");
			return false;
		}
		return store(HomingTarget(new_homing_target, new_homing_target->get_instance_id()), true, new_homing_target->get_global_position());
	}

	_ALWAYS_INLINE_ bool push_front_global_position_target(const Vector2 &global_position) {
		if (!global_position.is_finite()) {
			UtilityFunctions::push_error("push_front_global_position_target: position must be finite, nothing pushed.");
			return false;
		}
		return store(HomingTarget(global_position), true, global_position);
	}

	_ALWAYS_INLINE_ bool push_back_mouse_position_target(const Vector2 &cached_mouse_global_position) {
		HomingTarget target;
		target.type = HomingType::MousePositionTarget;
		return store(target, false, cached_mouse_global_position);
	}

	_ALWAYS_INLINE_ bool push_back_node2d_target(Node2D *new_homing_target) {
		if (!new_homing_target) {
			UtilityFunctions::push_error("push_back_node2d_target: target is null");
			return false;
		}
		return store(HomingTarget(new_homing_target, new_homing_target->get_instance_id()), false, new_homing_target->get_global_position());
	}

	_ALWAYS_INLINE_ bool push_back_global_position_target(const Vector2 &global_position) {
		if (!global_position.is_finite()) {
			UtilityFunctions::push_error("push_back_global_position_target: position must be finite, nothing pushed.");
			return false;
		}
		return store(HomingTarget(global_position), false, global_position);
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

	_ALWAYS_INLINE_ Variant get_current_homing_target() const {
		if (homing_targets.empty()) {
			return nullptr;
		}

		return target_as_variant(homing_targets.front(), cached_front_target_global_position);
	}
	// Re-arms the front target's reached flag (per-bullet reached semantics
	// live on the target itself, unlike the shared deque's per-bullet
	// states). Push-front paths call this so a popped-then-repushed target
	// fires again instead of staying latched from its previous exposure.
	_ALWAYS_INLINE_ void reset_front_reached_flag() {
		if (!homing_targets.empty()) {
			homing_targets.front().has_bullet_reached_target = false;
		}
	}

	//////////////////////////////////////

	// The idea behind this is to track whether the multimesh even has the need of tracking the mouse global position - enables caching behavior
	// Not safe for multithreading by default
	static inline int mouse_homing_targets_amount = 0;

private:
	// Stores a validated target at the front or the back; the cached front
	// position follows whenever the target became the front.
	_ALWAYS_INLINE_ bool store(const HomingTarget &target, bool at_front, const Vector2 &position) {
		if (!has_room_for_push()) {
			return false;
		}
		if (target.type == MousePositionTarget) {
			++mouse_homing_targets_amount;
		}
		const bool becomes_front = at_front || homing_targets.empty();
		if (at_front) {
			homing_targets.emplace_front(target);
		} else {
			homing_targets.emplace_back(target);
		}
		if (becomes_front) {
			cached_front_target_global_position = position;
		}
		return true;
	}

	// A target as GDScript sees it: the Node2D (null once freed), its
	// position, or `mouse_position` for a mouse target.
	_ALWAYS_INLINE_ Variant target_as_variant(const HomingTarget &target, const Vector2 &mouse_position) const {
		switch (target.type) {
			case GlobalPositionTarget:
				return target.global_position_target;
			case Node2DTarget:
				if (!is_homing_target_valid(target.node2d_target_data.target, target.node2d_target_data.cached_valid_instance_id)) {
					return nullptr;
				}
				return target.node2d_target_data.target;
			case MousePositionTarget:
				return mouse_position;
			case NotHoming:
				break;
		}
		return nullptr;
	}

	RingDeque2D<HomingTarget> homing_targets;
	mutable Vector2 cached_front_target_global_position{ 0, 0 };
};
} //namespace BlastBullets2D
