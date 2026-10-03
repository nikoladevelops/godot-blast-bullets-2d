// Custom timers: callables attached with attach_time_based_function, run by
// BulletFactory2D once per volley per physics frame (run_custom_timers).

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

bool BulletVolley2D::has_custom_timer_with_id(uint64_t timer_id) const {
	if (timer_id == 0) {
		return false;
	}
	for (const CustomTimer &timer : custom_timers) {
		if (timer._id == timer_id) {
			return true;
		}
	}
	return false;
}

bool BulletVolley2D::custom_timer_request_still_valid(uint64_t timer_id) const {
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

void BulletVolley2D::retire_pending_custom_timer_fire(uint64_t timer_id) {
	for (auto it = pending_custom_timer_fires.begin(); it != pending_custom_timer_fires.end();) {
		if (it->id == timer_id) {
			it = pending_custom_timer_fires.erase(it);
		} else {
			++it;
		}
	}
}

void BulletVolley2D::cancel_pending_custom_timer_fires_for(const Callable &callback) {
	for (auto it = pending_custom_timer_fires.begin(); it != pending_custom_timer_fires.end();) {
		if (it->callback == callback) {
			it = pending_custom_timer_fires.erase(it);
		} else {
			++it;
		}
	}
}

void BulletVolley2D::execute_stored_callable_safely(const Callable &_callback, bool execute_only_if_volley_is_active, uint64_t timer_id) {
	// Stamp the timers generation: between this deferred queue and its execution the
	// multimesh can be disabled, pooled and re-enabled for a NEW owner - the stale
	// owner's callback must not fire then (execute_only_if_volley_is_active alone
	// can't catch it, since the new owner is active too). The per-timer id
	// additionally catches a single detach, which does NOT bump the
	// generation.
	call_deferred(CachedStringNames2D::get().m_do_execute_stored_callable_safely, _callback, execute_only_if_volley_is_active, timers_generation, timer_id);
}

void BulletVolley2D::_do_execute_stored_callable_safely(const Callable &_callback, bool execute_only_if_volley_is_active, int expected_timers_generation, uint64_t expected_timer_id) {
	// Retire the id on every exit path: whether we run, bail on generation,
	// bail on detach, or bail on a dead callable, this request is consumed
	// and must not stay valid for a later flush.
	if (expected_timers_generation != timers_generation) {
		retire_pending_custom_timer_fire(expected_timer_id);
		return;
	}
	// The timer this request came from must still be valid. Without this,
	// detaching a callable between the queue and this flush left the
	// callback live: the generation is only bumped by a full detach/wake,
	// so a targeted detach_time_based_function() could not stop
	// an already-scheduled fire.
	if (!custom_timer_request_still_valid(expected_timer_id)) {
		return;
	}
	retire_pending_custom_timer_fire(expected_timer_id);

	// If the user wants to execute the callable only if the multimesh is active, check for that
	if (execute_only_if_volley_is_active && !is_active) {
		return;
	}

	// The bound object can be freed between attach and fire; calling an invalid
	// callable would spam engine errors every repeat.
	if (!_callback.is_valid()) {
		return;
	}

	_callback.call();
}

void BulletVolley2D::attach_time_based_function(double time, const Callable &callable, bool repeat, bool execute_only_if_volley_is_active) {
	// Stamp the timers generation so a full-disable (which detaches directly)
	// landing before this deferred call can't leak the timer into the next owner.
	// Outside physics processing the timer applies immediately (no frame of
	// delay); inside a physics frame it defers, since
	// run_custom_timers() may be iterating the vector.
	if (Engine::get_singleton()->is_in_physics_frame()) {
		call_deferred(CachedStringNames2D::get().m_do_attach_time_based_function, time, callable, repeat, execute_only_if_volley_is_active, timers_generation);
		return;
	}
	_do_attach_time_based_function(time, callable, repeat, execute_only_if_volley_is_active, timers_generation);
}

void BulletVolley2D::_do_attach_time_based_function(double time, const Callable &callable, bool repeat, bool execute_only_if_volley_is_active, int expected_timers_generation) {
	if (expected_timers_generation != timers_generation) {
		return;
	}
	// Direct script calls to this _do_* impl bypass the phase check in the
	// public wrapper. run_custom_timers() may be iterating the
	// vector right now (factory holds the iterating flag during the timer
	// sweep).
	if (bullet_factory != nullptr && bullet_factory->is_bullets_iterating()) {
		UtilityFunctions::push_error("Cannot modify attached timers while bullets are being processed (e.g. inside a timer callback or collision handler). Use attach_time_based_function() instead of the _do_* implementation.");
		return;
	}
	if (time <= 0.0) {
		UtilityFunctions::push_error("When calling attach_time_based_function(), you need to provide a time value that is above 0");
		return;
	}

	if (!callable.is_valid()) {
		UtilityFunctions::push_error("Invalid callable was passed to attach_time_based_function()");
		return;
	}

	// Uncapped user attaches would grow memory and per-tick iteration cost
	// without bound (an attach-per-tick script degrades every future tick).
	if (custom_timers.size() >= 64) {
		UtilityFunctions::push_error("attach_time_based_function: timer limit (64 per multimesh) reached, detach some first.");
		return;
	}

	custom_timers.emplace_back(callable, time, repeat, execute_only_if_volley_is_active, next_custom_timer_id());
}

void BulletVolley2D::detach_time_based_function(const Callable &callable) {
	// Stamp the generation so a full-disable landing before this deferred
	// call can't erase the next owner's timers. Immediate outside physics
	// processing, deferred within it (same rationale as attach above).
	if (Engine::get_singleton()->is_in_physics_frame()) {
		call_deferred(CachedStringNames2D::get().m_do_detach_time_based_function, callable, timers_generation);
		return;
	}
	_do_detach_time_based_function(callable, timers_generation);
}

void BulletVolley2D::_do_detach_time_based_function(const Callable &callable, int expected_timers_generation) {
	if (expected_timers_generation != timers_generation) {
		return;
	}
	if (bullet_factory != nullptr && bullet_factory->is_bullets_iterating()) {
		UtilityFunctions::push_error("Cannot modify attached timers while bullets are being processed (e.g. inside a timer callback or collision handler). Use detach_time_based_function() instead of the _do_* implementation.");
		return;
	}
	// Cancel queued fires for this callable FIRST and unconditionally. A
	// one-shot is erased from the vector on the tick it fires, so by the
	// time a detach runs its fire may no longer be in the vector at all -
	// matching inside the loop below would miss it and the detached
	// callable would still run once. That is the bug the per-timer id
	// exists to prevent.
	cancel_pending_custom_timer_fires_for(callable);
	for (auto it = custom_timers.begin(); it != custom_timers.end();) {
		if (it->_callback == callable) {
			it = custom_timers.erase(it); // Order-preserving
		} else {
			++it;
		}
	}
}

void BulletVolley2D::detach_all_time_based_functions() {
	// Immediate outside physics processing, deferred within it (same
	// rationale as attach above).
	if (Engine::get_singleton()->is_in_physics_frame()) {
		call_deferred(CachedStringNames2D::get().m_do_detach_all_time_based_functions, timers_generation);
		return;
	}
	_do_detach_all_time_based_functions(timers_generation);
}

void BulletVolley2D::_do_detach_all_time_based_functions(int expected_timers_generation) {
	if (expected_timers_generation != timers_generation) {
		return;
	}
	++timers_generation;
	custom_timers.clear();
}

void BulletVolley2D::run_custom_timers(double delta) {
	if (!Math::is_finite(delta) || delta < 0.0) {
		return;
	}
	for (auto it = custom_timers.begin(); it != custom_timers.end();) {
		it->_current_time -= delta;
		if (it->_current_time <= 0.0) {
			// Record BEFORE the (deferred) call: a one-shot is erased from
			// the vector on this same line, so the flush could not find it
			// there. Bounded by the 64-timer cap.
			PendingCustomTimerFire pending;
			pending.id = it->_id;
			pending.callback = it->_callback;
			pending_custom_timer_fires.push_back(pending);
			execute_stored_callable_safely(it->_callback, it->_execute_only_if_volley_is_active, it->_id);

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
				it = custom_timers.erase(it);
			}
		} else {
			++it;
		}
	}
}

} // namespace BlastBullets2D
