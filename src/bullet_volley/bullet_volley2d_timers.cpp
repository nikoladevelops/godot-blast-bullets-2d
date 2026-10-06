// Custom timers: callables attached with attach_time_based_function, run by
// BulletFactory2D once per volley per physics frame (run_custom_timers).

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletVolley2D::attach_time_based_function(double time, const Callable &callable, bool repeat, bool execute_only_if_volley_is_active) {
	if (!Math::is_finite(time) || time <= 0.0) {
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
	custom_timers.emplace_back(callable, time, repeat, execute_only_if_volley_is_active, ++custom_timer_id_counter);
}

void BulletVolley2D::detach_time_based_function(const Callable &callable) {
	for (auto it = custom_timers.begin(); it != custom_timers.end();) {
		if (it->_callback == callable) {
			it = custom_timers.erase(it); // Order-preserving
		} else {
			++it;
		}
	}
}

void BulletVolley2D::detach_all_time_based_functions() {
	custom_timers.clear();
}

void BulletVolley2D::run_custom_timers(double delta) {
	if (!Math::is_finite(delta) || delta < 0.0 || custom_timers.empty()) {
		return;
	}
	if (_timers_running_depth > 0) {
		return; // a callback re-entered the sweep: the outer pass owns this tick
	}
	ReentrancyGuard timers_guard(_timers_running_depth);
	// Pass 1: advance and collect. No user code runs here. A timer that only
	// runs for an active volley HOLDS while the volley is parked (every
	// bullet disabled, still owned): its remaining time resumes on wake, so
	// a one-shot is never consumed unseen and a repeating one keeps phase.
	due_timer_scratch.clear();
	for (CustomTimer &timer : custom_timers) {
		if (timer._execute_only_if_volley_is_active && !is_active) {
			continue;
		}
		timer._current_time -= delta;
		if (timer._current_time <= 0.0) {
			due_timer_scratch.push_back(timer._id);
		}
	}
	// Pass 2: fire each due timer that is still attached (an earlier
	// callback of this pass may have detached it, parked or freed the
	// volley). Re-arm or erase right before the call, so the callback sees
	// its own next state and may detach itself.
	const uint64_t self_id = get_instance_id();
	for (size_t d = 0; d < due_timer_scratch.size(); ++d) {
		const uint64_t id = due_timer_scratch[d];
		int index = -1;
		for (int k = 0; k < (int)custom_timers.size(); ++k) {
			if (custom_timers[k]._id == id) {
				index = k;
				break;
			}
		}
		if (index < 0) {
			continue;
		}
		CustomTimer &timer = custom_timers[index];
		if (timer._execute_only_if_volley_is_active && !is_active) {
			continue; // parked by an earlier callback: stays due, fires after the wake
		}
		const Callable callback = timer._callback;
		if (timer._repeating) {
			// Carry the overshoot so the average period stays exact
			// (resetting dropped up to one tick per period: a 0.1s timer at
			// 60 Hz fired every 7 ticks). Anti-spiral: a period shorter than
			// the tick fires once per tick and resyncs instead of banking an
			// ever-growing debt.
			timer._current_time += timer._initial_time;
			if (timer._current_time <= 0.0) {
				timer._current_time = timer._initial_time;
			}
		} else {
			custom_timers.erase(custom_timers.begin() + index);
		}
		// The bound object can be freed between attach and fire; calling an
		// invalid callable would spam engine errors every repeat.
		if (!callback.is_valid()) {
			continue;
		}
		note_user_code();
		callback.call();
		if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
			return; // the callback freed this volley
		}
	}
	due_timer_scratch.clear();
}

} // namespace BlastBullets2D
