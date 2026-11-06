#pragma once

#include <godot_cpp/core/object.hpp>

// Tiny RAII re-entrancy latch.
//
// Script callbacks (on_bullet_enable/disable/spawn, collision/lifetime signal
// handlers) run user code that may re-enter the function that fired them.
// Each guarded region owns an int depth counter: the first entry claims it,
// nested entries reject with an error instead of recursing unboundedly or
// corrupting counters. Replaces the hand-rolled per-function guard structs.
//
// The counter lives in `owner`, and user code inside the guarded scope may
// free the owner: the release then skips the counter, which died with it
// (decrementing it wrote into freed memory: AddressSanitizer,
// heap-use-after-free in ~ReentrancyGuard).
namespace BlastBullets2D {

class ReentrancyGuard {
public:
	ReentrancyGuard(int &depth, const godot::Object *owner) :
			depth(depth), owner(owner), owner_id(owner->get_instance_id()) { ++depth; }
	~ReentrancyGuard() {
		if (godot::ObjectDB::get_instance(godot::ObjectID(owner_id)) == owner) {
			--depth;
		}
	}

	ReentrancyGuard(const ReentrancyGuard &) = delete;
	ReentrancyGuard &operator=(const ReentrancyGuard &) = delete;

private:
	int &depth;
	const godot::Object *owner;
	uint64_t owner_id;
};

} //namespace BlastBullets2D
