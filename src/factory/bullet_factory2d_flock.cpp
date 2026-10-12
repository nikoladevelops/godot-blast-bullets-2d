// BulletFactory2D flocking: the per-sweep flock pass (FlockSolver2D) over the
// sweep's volley snapshot, and its debug readout. The per-bullet turn runs in
// the volley tick (step_flock, bullet_volley2d_tick.cpp).

#include "factory/bullet_factory2d_internal.hpp"

#include <godot_cpp/classes/time.hpp>

using namespace godot;

namespace BlastBullets2D {

// Kept out of the sweep loop on purpose (§15.3: inlining heavy work into the
// factory loop slowed every volley tick).
_NO_INLINE_ void BulletFactory2D::run_flock_pass() {
	const uint64_t started = Time::get_singleton()->get_ticks_usec();
	flock_solver.begin(sweep_counter);
	// A frozen bullet world turns nothing (every volley ticks with delta 0).
	if (time_scale > 0.0) {
		for (const VolleyIterationEntry &entry : iteration_scratch) {
			BulletVolley2D *volley = entry.volley;
			if (volley == nullptr || !volley->is_active || volley->flock_config == nullptr) {
				continue;
			}
			const FlockConfig2D &config = *volley->flock_config;
			if (!config.enabled || config.flock.is_null()) {
				continue;
			}
			flock_solver.add_volley(*volley, config, entry.id);
		}
		flock_solver.solve();
	}
	stats_last_flock_usec = Time::get_singleton()->get_ticks_usec() - started;
	flock_solver.set_pass_usec(stats_last_flock_usec);
}

Dictionary BulletFactory2D::debug_get_flock_stats() const {
	const FlockSolver2D::Stats &s = flock_solver.get_stats();
	Dictionary d;
	d["flocks"] = s.flocks;
	d["members"] = s.members;
	d["grid_members"] = s.grid_members;
	d["neighbor_checks"] = (int64_t)s.neighbor_checks;
	d["capped_members"] = s.capped_members;
	d["pass_usec"] = (int64_t)s.pass_usec;
	d["armed_volleys"] = flock_armed_volleys;
	return d;
}

} //namespace BlastBullets2D
