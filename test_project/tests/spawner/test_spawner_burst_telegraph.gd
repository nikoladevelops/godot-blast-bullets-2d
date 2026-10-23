extends BlastTest
## Burst chains and telegraphs under the shooting rules: auto-started bursts
## never fire past max_volleys or a fire_n_volleys budget, disabling auto-fire
## cancels them (burst_finished still reports), pause freezes them, the mirror
## rhythm is "first shot plain, then alternate" for any count, burst_count
## changes mid-chain stay consistent, a misconfigured burst reports its error
## once, and the telegraph payload is the shot the player will get.
## Time is simulated (60 fps).


func _spawner(count: int, interval: float = 0.05) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(1, 50.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.burst_enabled = true
	sp.burst_count = count
	sp.burst_interval_sec = interval
	sp.shoot_interval_sec = 10.0 # one trigger pull per test window
	watch_signals(sp)
	return sp


func _until(sp: Object, sig: String, n: int, frames: int = 240) -> void:
	for i in frames:
		if get_signal_emit_count(sp, sig) >= n:
			return
		await idle(1)


func test_auto_burst_never_fires_past_max_volleys() -> void:
	var sp := _spawner(3)
	sp.max_volleys = 2
	sp.set_shooting_enabled(true)
	await _until(sp, "burst_finished", 1)
	await idle(10)
	assert_eq(sp.get_volleys_fired(), 2, "the chain was clamped to the cap")
	assert_signal_emit_count(sp, "shooting_finished", 1, "finished once at the cap")
	assert_signal_emit_count(sp, "burst_finished", 1, "the clamped chain still finishes")


func test_auto_burst_respects_a_fire_n_volleys_budget() -> void:
	var sp := _spawner(3)
	sp.fire_n_volleys(1)
	sp.set_shooting_enabled(true)
	await _until(sp, "burst_finished", 1)
	await idle(10)
	assert_eq(sp.get_volleys_fired(), 1, "the chain was clamped to the budget")
	assert_false(sp.is_processing(), "nothing left to do: _process sleeps")
	sp.set_shooting_enabled(false)


func test_disabling_auto_fire_cancels_the_running_chain() -> void:
	var sp := _spawner(5)
	sp.set_shooting_enabled(true)
	await _until(sp, "burst_shot_fired", 2)
	sp.set_shooting_enabled(false)
	await idle(20)
	assert_eq(sp.get_volleys_fired(), 2, "no burst shot after auto-fire was switched off")
	assert_signal_emit_count(sp, "burst_finished", 1, "the cancelled chain reports its end")
	assert_eq(sp.get_burst_shots_left(), 0, "chain drained")


func test_disabling_burst_mode_cancels_and_reports() -> void:
	var sp := _spawner(5)
	sp.begin_burst()
	await _until(sp, "burst_shot_fired", 1)
	sp.burst_enabled = false
	assert_signal_emit_count(sp, "burst_finished", 1, "switching bursts off ends the chain with a report")
	await idle(20)
	assert_eq(sp.get_volleys_fired(), 1, "no shot after the chain ended")


func test_pause_freezes_and_resume_continues_the_chain() -> void:
	var sp := _spawner(4)
	sp.set_shooting_enabled(true)
	await _until(sp, "burst_shot_fired", 2)
	sp.pause_shooting()
	await idle(20)
	assert_eq(sp.get_volleys_fired(), 2, "paused chains do not fire")
	sp.resume_shooting()
	await _until(sp, "burst_finished", 1)
	assert_eq(sp.get_volleys_fired(), 4, "the chain completes after resuming")
	sp.set_shooting_enabled(false)


func test_mirror_rhythm_starts_plain_for_any_count() -> void:
	for count in [3, 4]:
		var sp := _spawner(count)
		sp.burst_alternate_mirror = true
		sp.begin_burst()
		await _until(sp, "burst_finished", 1)
		var flags: Array = []
		var indexes: Array = []
		for i in get_signal_emit_count(sp, "burst_shot_fired"):
			var params: Array = get_signal_parameters(sp, "burst_shot_fired", i)
			indexes.append(params[0])
			flags.append(params[1])
		var want: Array = []
		for i in count:
			want.append(i % 2 == 1)
		assert_eq(flags, want, "count %d: plain, mirrored, plain, ..." % count)
		assert_eq(indexes, range(1, count + 1), "count %d: shot indexes 1..n" % count)


func test_shrinking_burst_count_mid_chain_stays_consistent() -> void:
	var sp := _spawner(5)
	sp.begin_burst()
	await _until(sp, "burst_shot_fired", 2)
	sp.burst_count = 3
	await _until(sp, "burst_finished", 1)
	assert_eq(get_signal_emit_count(sp, "burst_shot_fired"), 3, "the chain ends at the new count")
	var indexes: Array = []
	for i in 3:
		indexes.append(get_signal_parameters(sp, "burst_shot_fired", i)[0])
	assert_eq(indexes, [1, 2, 3], "shot indexes stay 1..n")


func test_misconfigured_auto_burst_reports_once() -> void:
	var sp := _spawner(3, 0.02)
	sp.set_bullet_factory(null)
	sp.shoot_interval_sec = 0.1
	sp.set_shooting_enabled(true)
	await _until(sp, "burst_finished", 3)
	expect_errors_containing("no BulletFactory2D assigned", 1, "one report for many failed chain shots")
	sp.set_shooting_enabled(false)


func test_telegraph_payload_matches_the_shot_and_warns_once_per_chain() -> void:
	var sp := make_spawner(H.make_volley_data(5, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 5) # still bullets: positions stay readable
	sp.burst_enabled = true
	sp.burst_count = 3
	sp.burst_interval_sec = 0.05
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.05
	sp.shoot_interval_sec = 10.0
	watch_signals(sp)
	sp.set_shooting_enabled(true)
	await _until(sp, "burst_finished", 1)
	assert_signal_emit_count(sp, "volley_telegraphed", 1, "one warning per chain, before the first shot")
	var aim: Array = get_signal_parameters(sp, "volley_telegraphed", 0)[0]
	var shot: BulletVolley2D = get_signal_parameters(sp, "volley_fired", 0)[0]
	assert_eq(aim.size(), shot.get_amount_bullets(), "the warning shows every bullet of the shot")
	for i in aim.size():
		assert_almost_eq((aim[i] as Transform2D).origin.distance_to(shot.get_bullet_transform(i).origin), 0.0, 0.01, "bullet %d spawns where it was telegraphed" % i)
	sp.set_shooting_enabled(false)


## Every way a running chain ends reports burst_finished exactly once:
## burst_count shrunk to the shots already fired, reset_shooting(), and a
## fresh begin_burst() over a running chain (which then runs its own chain
## and reports that one too). The next chain starts clean (indexes 1..n).
func test_every_chain_end_reports_once(how: String = use_parameters(["shrink_to_fired", "reset_shooting", "begin_over_running"])) -> void:
	var sp := _spawner(5)
	sp.begin_burst()
	await _until(sp, "burst_shot_fired", 2)
	assert_eq(get_signal_emit_count(sp, "burst_finished"), 0, "still running")
	match how:
		"shrink_to_fired":
			sp.burst_count = 2
		"reset_shooting":
			sp.reset_shooting()
		"begin_over_running":
			sp.begin_burst()
	assert_eq(get_signal_emit_count(sp, "burst_finished"), 1, "%s: the running chain reports its end at once" % how)
	if how == "begin_over_running":
		await _until(sp, "burst_finished", 2)
		assert_eq(get_signal_emit_count(sp, "burst_finished"), 2, "the new chain reports its own end")
		assert_eq(get_signal_parameters(sp, "burst_shot_fired", 2)[0], 1, "the new chain counts from 1")
	else:
		assert_eq(sp.get_burst_shots_left(), 0, "%s: nothing left" % how)
		await idle(20)
		assert_eq(get_signal_emit_count(sp, "burst_finished"), 1, "%s: no late report" % how)
		sp.burst_count = 2
		sp.begin_burst()
		await _until(sp, "burst_finished", 2)
		var last: int = get_signal_emit_count(sp, "burst_shot_fired") - 1
		assert_eq(get_signal_parameters(sp, "burst_shot_fired", last)[0], 2, "%s: the next chain counts 1..n" % how)


## A handler of volley_fired or burst_shot_fired (inside the running burst
## shot) that cancels, restarts or resets the chain owns it: the counters
## never go negative, every chain reports burst_finished exactly once, no
## burst_shot_fired follows a cancel, and a chain started there fires all
## its shots.
func test_handlers_that_cancel_or_restart_a_burst_own_the_chain(p: Array = use_parameters([
		["volley_fired", "cancel"], ["volley_fired", "reset_shooting"], ["volley_fired", "begin_burst"],
		["burst_shot_fired", "cancel"], ["burst_shot_fired", "reset_shooting"], ["burst_shot_fired", "begin_burst"]])) -> void:
	var sig: String = p[0]
	var action: String = p[1]
	var sp := _spawner(3, 0.02)
	var log: Array = []
	var acted: Array = []
	sp.burst_shot_fired.connect(func(_n, _m) -> void: log.append("shot"))
	sp.burst_finished.connect(func() -> void: log.append("finished"))
	var handler := func() -> void:
		if acted.is_empty():
			acted.append(true)
			match action:
				"cancel":
					sp.burst_enabled = false
					sp.burst_enabled = true
				"reset_shooting":
					sp.reset_shooting()
				"begin_burst":
					sp.begin_burst()
	if sig == "volley_fired":
		sp.volley_fired.connect(func(_v, _n) -> void: handler.call())
	else:
		sp.burst_shot_fired.connect(func(_n, _m) -> void: handler.call())
	sp.begin_burst()
	var min_left := 0
	for k in 60:
		await idle(1)
		min_left = mini(min_left, sp.get_burst_shots_left())
	assert_eq(min_left, 0, "burst_shots_left never goes negative")
	var expected: Array
	match [sig, action]:
		["volley_fired", "begin_burst"]:
			expected = ["finished", "shot", "shot", "shot", "finished"] # the new chain fires all 3
		["burst_shot_fired", "begin_burst"]:
			expected = ["shot", "finished", "shot", "shot", "shot", "finished"]
		["volley_fired", _]:
			expected = ["finished"] # cancelled inside the shot: no shot report after it
		_:
			expected = ["shot", "finished"]
	assert_eq(log, expected, "%s from %s" % [action, sig])


## Reports a velocity and runs `action` on its first call.
const ACT_IN_VELOCITY := """
extends Node2D
var action: Callable
var calls := 0
func get_velocity() -> Vector2:
	calls += 1
	if calls == 1 and action.is_valid():
		action.call()
	return Vector2.ZERO
"""


## The warning's aim snapshot runs the aimed target's get_velocity (user
## code). A script there that turns telegraphs off, ends burst mode or resets
## the wave owns the cadence: the warning it interrupted never starts, so no
## stale telegraph fires a shot into the new state.
func test_a_velocity_script_that_cancels_the_warning_wins(action: String = use_parameters(["telegraph_off", "burst_off", "reset_shooting"])) -> void:
	var s := GDScript.new()
	s.source_code = ACT_IN_VELOCITY
	assert_eq(s.reload(), OK, "the script compiles")
	var target := Node2D.new()
	target.set_script(s)
	target.position = Vector2(200, 0)
	add(target)
	var sp := make_spawner(H.make_volley_data(1, 50.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_AIMED, 1)
	sp.helper_aimed_prediction = 1.0
	sp.helper_aimed_prediction_time = 0.5
	sp.set_helper_aimed_target(target)
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.1
	sp.shoot_interval_sec = 10.0
	if action == "burst_off":
		sp.burst_enabled = true
		sp.burst_count = 3
		sp.burst_interval_sec = 0.05
	watch_signals(sp)
	target.action = func() -> void:
		match action:
			"telegraph_off":
				sp.telegraph_enabled = false
			"burst_off":
				sp.burst_enabled = false
			"reset_shooting":
				sp.reset_shooting()
	sp.set_shooting_enabled(true)
	await idle(30)
	var warned: int = get_signal_emit_count(sp, "volley_telegraphed")
	if action == "reset_shooting":
		# The reset wave pulls again at once: one clean warning, one shot.
		assert_eq([warned, sp.get_volleys_fired()], [1, 1], "only the reset wave's own warning and shot")
	else:
		assert_eq([warned, sp.get_volleys_fired()], [0, 0], "the interrupted warning never starts and fires nothing")


## telegraph_sec 0 with telegraph_max_sec above it: every warning lasts a
## random time between the two (documented), plain shots and bursts alike.
func test_a_zero_telegraph_with_a_max_still_warns(burst: bool = use_parameters([false, true])) -> void:
	var sp := _spawner(3 if burst else 1)
	sp.burst_enabled = burst
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.0
	sp.telegraph_max_sec = 0.3
	sp.reload_jitter_seed = 7
	sp.set_shooting_enabled(true)
	await _until(sp, "volley_fired", 1, 60)
	assert_signal_emit_count(sp, "volley_telegraphed", 1, "warned before the first shot")
	assert_eq(sp.get_volleys_fired(), 1, "then it fires")


## Switching bursts off ends a running chain and its first-shot warning,
## never a plain telegraphed shot that is not part of one.
func test_burst_off_keeps_a_plain_telegraph() -> void:
	var sp := _spawner(1)
	sp.burst_enabled = false
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.1
	sp.set_shooting_enabled(true)
	await _until(sp, "volley_telegraphed", 1, 30)
	assert_true(sp.is_telegraph_pending(), "warning")
	sp.burst_enabled = false
	assert_true(sp.is_telegraph_pending(), "a plain warning is not a burst's")
	await _until(sp, "volley_fired", 1, 30)
	assert_eq(sp.get_volleys_fired(), 1, "the warned shot fires")


## An auto chain stops where the cap or the budget stops, also when either is
## set mid-chain (the clamp at the chain's start alone fired past it).
func test_an_auto_burst_stops_at_a_cap_set_mid_chain(kind: String = use_parameters(["max_volleys", "max_volleys_reached", "fire_n_volleys"])) -> void:
	var sp := _spawner(5)
	sp.set_shooting_enabled(true)
	await _until(sp, "burst_shot_fired", 1)
	var want := 2
	match kind:
		"max_volleys":
			sp.max_volleys = 2
		"max_volleys_reached":
			sp.max_volleys = 1 # already reached: no further chain shot
			want = 1
		_:
			sp.fire_n_volleys(1)
	await _until(sp, "burst_finished", 1)
	await idle(10)
	assert_eq(sp.get_volleys_fired(), want, "the chain ends at the new limit")
	assert_eq(sp.get_burst_shots_left(), 0, "nothing left to resume")
	sp.resume_shooting()
	await idle(10)
	if kind == "fire_n_volleys":
		assert_signal_emit_count(sp, "burst_finished", 1, "a resume never finishes the old chain")


## begin_burst warns first when telegraph_enabled (documented), a one-shot
## chain too (burst_count 1, or burst mode off): it used to fire at once.
func test_a_one_shot_manual_burst_is_warned_first(burst_on: bool = use_parameters([true, false])) -> void:
	var sp := _spawner(1)
	sp.burst_enabled = burst_on
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.1
	sp.begin_burst()
	assert_eq(sp.get_volleys_fired(), 0, "nothing fired at once")
	assert_true(sp.is_telegraph_pending(), "warning first")
	await _until(sp, "volley_fired", 1, 30)
	assert_signal_emit_count(sp, "volley_telegraphed", 1, "one warning")
	assert_eq(sp.get_volleys_fired(), 1, "then the one shot")


## A handler of the chain's LAST burst_shot_fired that takes the chain over
## (cancels it, resets shooting or starts a new chain): the old chain still
## reports burst_finished exactly once, before anything of the new one.
func test_a_handler_of_the_last_burst_shot_still_finishes_the_chain(action: String = use_parameters(["cancel", "reset_shooting", "begin_burst"])) -> void:
	var sp := _spawner(3, 0.02)
	var log: Array = []
	var acted: Array = []
	sp.burst_finished.connect(func() -> void: log.append("finished"))
	sp.burst_shot_fired.connect(func(n: int, _m) -> void:
		log.append("shot %d" % n)
		if n == 3 and acted.is_empty():
			acted.append(true)
			if action == "cancel":
				sp.burst_enabled = false
				sp.burst_enabled = true
			elif action == "reset_shooting":
				sp.reset_shooting()
			else:
				sp.begin_burst())
	sp.begin_burst()
	for k in 60:
		await idle(1)
	var expected := ["shot 1", "shot 2", "shot 3", "finished"]
	if action == "begin_burst":
		expected += ["shot 1", "shot 2", "shot 3", "finished"]
	assert_eq(log, expected, "%s from the last shot" % action)
