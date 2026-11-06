#pragma once

#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/array.hpp>

#include <cstdint>

namespace BlastBullets2D {
using namespace godot;

// WHEN a spawner fires (the Shooting group): auto fire and its interval
// (ranges, reload jitter), budgets (max_volleys, fire_n_volleys), the
// per-frame repay cap, bursts with their telegraph, and the pattern-list
// sequencer; their runtime clocks and counters; the serials a shot handler
// takes the cadence over with; and the pure timing math. The host spawner
// runs the clocks in its process loop and fires (user code: it re-checks
// the serials after every emit). Settings are trusted here: the spawner
// validates them (its error texts name its class).
struct ShotCadence2D {
	// ---- Settings ------------------------------------------------------------
	// Master switch for automatic shooting.
	bool shooting_enabled = true;
	// Seconds between volleys (> 0), up to shoot_interval_max_sec when that
	// is larger (a roll per wait).
	double shoot_interval_sec = 1.0;
	double shoot_interval_max_sec = 0.0;
	// Delay before the first volley after (re)arming.
	double shoot_initial_delay_sec = 0.0;
	// Volleys after which auto-shooting stops. -1 = infinite, 0 = never.
	int max_volleys = -1;
	// The most shots ONE timer (auto-fire, a burst chain, a pattern list)
	// fires in one frame: sub-frame intervals and a late frame repay up to
	// this many, then the timer resyncs and drops the rest of the backlog.
	// 1 = never repay a hitch. Bounds an overloaded game's spiral.
	int max_shots_per_frame = 8;
	// Bursts (N shots per trigger, burst_interval_sec apart, up to
	// burst_interval_max_sec when larger).
	bool burst_enabled = false;
	int burst_count = 3;
	double burst_interval_sec = 0.15;
	double burst_interval_max_sec = 0.0;
	// Mirror every other burst volley: a mirrored shot negates both the spin
	// and the spiral-family winding, so the arms sweep the other way.
	bool burst_alternate_mirror = false;
	// Telegraph: warn telegraph_sec (up to telegraph_max_sec) before each
	// burst volley.
	bool telegraph_enabled = false;
	double telegraph_sec = 0.5;
	double telegraph_max_sec = 0.0;
	// Reload jitter: each auto wait is the interval +/- random *
	// reload_jitter_sec (0 = exact). The seed (0 = non-deterministic) also
	// seeds the timing ranges: hashed with the shot number per roll.
	double reload_jitter_sec = 0.0;
	int reload_jitter_seed = 0;

	// ---- Auto fire state -----------------------------------------------------
	// Countdown to the next volley.
	double shoot_time_left = 0.0;
	// True while shoot_time_left counts the initial delay (until the first
	// pull): an interval change never cuts it.
	bool shoot_initial_wait = false;
	// Volleys fired since (re)arming: the shot number every seeded roll uses.
	int volleys_fired = 0;
	// Pause latch (pause_shooting / resume_shooting): auto fire stays off
	// while set, but volleys_fired / max_volleys are kept so a resume
	// continues the current wave instead of restarting it.
	bool shooting_paused = false;
	// One-shot budget from fire_n_volleys(n): counts every fired volley
	// (auto or manual) down to 0, then pauses. -1 = no budget. Never touches
	// max_volleys, so unlimited (-1) stays unlimited.
	int oneshot_volleys_left = -1;
	// Set by the shot that spent the fire_n_volleys budget: an auto chain
	// belongs to that budget and ends with it.
	bool last_shot_spent_budget = false;
	// Bumped by reset_shooting and fire_n_volleys: a shot handler calling
	// them owns the cadence (the reporting shot neither spends the new budget
	// nor rearms the interval over the restarted delay).
	uint32_t serial = 0;

	// ---- Burst and telegraph state (never stored) ----------------------------
	// Shots left in the current chain.
	int burst_shots_left = 0;
	// The chain's last shot is being reported (burst_shot_fired): it still
	// runs for a handler that takes it over (that handler's cancel reports it).
	bool burst_chain_closing = false;
	// Bumped when a chain starts or ends: a burst_shot_fired / volley_fired
	// handler that cancels, restarts or resets the chain owns it (the running
	// shot touches no counter after its emits once this moved).
	uint32_t burst_serial = 0;
	// Shots fired in the current chain (shot index and mirror rhythm).
	int burst_shots_fired = 0;
	// The auto loop started the chain: it then follows the auto-fire rules
	// (clamped to volleys_remaining, cancelled when shooting_enabled turns
	// off, errors latched once). Same for a pending telegraph.
	bool burst_from_auto = false;
	bool telegraph_from_auto = false;
	double burst_time_left = 0.0;
	double telegraph_time_left = 0.0;
	bool burst_mirror_next = false;
	// Consecutive failed shots inside the current chain: a failed shot is
	// retried (not consumed), so transient hitches never eat burst shots;
	// after a full burst's worth of failures in a row the failure is
	// permanent and the chain ends with burst_finished. Reset on every
	// success and every new chain.
	int burst_consecutive_failures = 0;
	// The current chain has shown its telegraph: the expiry fires instead
	// of warning again.
	bool burst_telegraph_done = false;
	// A volley waits for its telegraph to end (transforms re-collect at fire
	// time, so markers that move during the warning still aim correctly).
	bool telegraph_pending = false;

	// ---- Pattern-list sequencer (runtime only) -------------------------------
	// Entries fired in order (interval apart) or all at once; each is a
	// Dictionary of the keys it overrides for its shot.
	Array pattern_list_entries;
	double pattern_list_interval_sec = 0.25;
	int pattern_list_cursor = 0;
	double pattern_list_time_left = 0.0;
	bool pattern_list_active = false;
	// Bumped by every spawn_pattern_list / stop_pattern_list call: a handler
	// that started or stopped a list inside an entry's shot owns what follows.
	uint32_t pattern_list_serial = 0;

	// ---- Timing rolls --------------------------------------------------------
	// Its own RNG (mutable: the rolls are const): separate from the homing and
	// placement streams, so a jitter reseed never shifts them. Seed 0
	// randomizes it once, not per roll.
	mutable Ref<RandomNumberGenerator> jitter_rng;
	mutable bool jitter_rng_randomized = false;

	// Auto fire runs: on, not paused, budget left.
	bool auto_active() const {
		return shooting_enabled && !shooting_paused && (max_volleys < 0 || volleys_fired < max_volleys);
	}
	// Volleys the budgets still allow (-1 = no limit).
	int volleys_remaining() const {
		int remaining = -1;
		if (max_volleys >= 0) {
			remaining = MAX(0, max_volleys - volleys_fired);
		}
		if (oneshot_volleys_left >= 0) {
			remaining = (remaining < 0) ? oneshot_volleys_left : MIN(remaining, oneshot_volleys_left);
		}
		return remaining;
	}
	// A burst volley waits for a telegraph.
	bool telegraph_wanted() const {
		return telegraph_enabled && (telegraph_sec > 0.0 || telegraph_max_sec > telegraph_sec);
	}
	// Shorter settings apply to the wait already running (otherwise the old,
	// longer one would still be waited out once), down to the longest wait
	// the settings can still roll (interval, its max, the reload jitter): the
	// same values never cut a rolled wait. The initial delay is not an interval.
	void clamp_wait_to_settings() {
		const double longest = Math::max(shoot_interval_sec, shoot_interval_max_sec) + Math::max(reload_jitter_sec, 0.0);
		if (!shoot_initial_wait && shoot_time_left > longest) {
			shoot_time_left = longest;
		}
	}

	// A shot was fired: its number (what its fired signal reports).
	int count_shot() { return ++volleys_fired; }
	// The number the next shot will report (seeds what it arms).
	uint64_t next_shot_number() const { return (uint64_t)volleys_fired + 1; }

	// base, or a roll in [base, max_value) when max_value is larger
	// (channel: 1 interval, 2 burst interval, 3 telegraph).
	double roll_range(double base, double max_value, uint64_t channel) const;
	// The next auto wait: the interval (range) +/- the reload jitter,
	// floored at 1 ms.
	double next_shoot_interval() const;
	double next_burst_interval() const { return roll_range(burst_interval_sec, burst_interval_max_sec, 2); }
	double next_telegraph() const { return roll_range(telegraph_sec, telegraph_max_sec, 3); }

private:
	RandomNumberGenerator &rng() const;
};

} // namespace BlastBullets2D
