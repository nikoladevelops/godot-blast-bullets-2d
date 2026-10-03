#pragma once

// RAII guard for BulletFactory2D structural operations.
//
// Every structural factory op (reset/free_*/populate_*, interpolation toggles,
// manual-deletion fixup) used to hand-roll the same prologue/epilogue:
//   - hold the internal busy flag (user callbacks during sweeps must reject)
//   - pause bullet processing, resume it afterwards if it was on
//   - optionally power the debuggers down and back up (they rebuild from
//     indices, so they must not observe a half-mutated vector)
// Hand-rolled copies drifted (populate_* replicated the restore on every early
// return). This guard does it once, exception/early-return safe.
//
// Usage:
//   void BulletFactory2D::reset(...) {
//       ... pre-gates (busy/iterating/ready) ...
//       FactoryOperationGuard op(this); // or (this, /*manage_debuggers=*/false)
//       ... body with plain returns ...
//       emit_signal("reset_finished"); // before op destructs!
//   }
//
// NOT for the spawn fast path: spawning only appends and stays allowed while
// iterating, so it must not pause processing or hold busy.

namespace BlastBullets2D {

class BulletFactory2D;

class FactoryOperationGuard {
public:
	explicit FactoryOperationGuard(BulletFactory2D *factory, bool manage_debuggers = true, bool defer_debugger_restore = false);
	~FactoryOperationGuard();

	FactoryOperationGuard(const FactoryOperationGuard &) = delete;
	FactoryOperationGuard &operator=(const FactoryOperationGuard &) = delete;

private:
	BulletFactory2D *factory = nullptr;
	bool saved_busy = false;
	bool resume_processing = false;
	bool saved_debuggers = false;
	bool manage_debuggers = true;
	// Force call_deferred for the debugger restore (PREDELETE context: an
	// immediate rebuild races node teardown and crashes).
	bool defer_debugger_restore = false;
};

} //namespace BlastBullets2D
