#pragma once

// Tiny RAII re-entrancy latch (WP-C/WP-D).
//
// Script callbacks (on_bullet_enable/disable/spawn, collision/lifetime signal
// handlers) run user code that may re-enter the function that fired them.
// Each guarded region owns an int depth counter: the first entry claims it,
// nested entries reject with an error instead of recursing unboundedly or
// corrupting counters. Replaces the hand-rolled per-function guard structs.
namespace BlastBullets2D {

class ReentrancyGuard {
public:
	explicit ReentrancyGuard(int &depth) :
			depth(depth) { ++depth; }
	~ReentrancyGuard() { --depth; }

	ReentrancyGuard(const ReentrancyGuard &) = delete;
	ReentrancyGuard &operator=(const ReentrancyGuard &) = delete;

private:
	int &depth;
};

} //namespace BlastBullets2D
