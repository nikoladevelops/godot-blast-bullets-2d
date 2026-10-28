extends BlastTest
## Shooting cadence and its controls: initial delay + interval timing,
## pause / resume / fire_n_volleys / volleys_remaining / reset_shooting
## (including calls made before the spawner enters the tree), deferred and
## nested shots, skip reasons for every failure, reload jitter, and
## shooting_* signals tracking AUTO-fire only.
## Time is simulated (60 fps): one idle frame = 1/60 s.
##
## Documented setter/signal split: with auto-fire configured, the shot that
## trips max_volleys emits shooting_finished exactly once; set_max_volleys()
## only reports start/stop transitions (lowering onto the count -> stopped,
## never a second finished). Manual-only spawners never report a finish
## (test_spawner_cadence).
##
## Shooting transitions report exactly once, and only inside the tree: the
## scene loader calls setters before _ready (which emits the start itself),
## so pre-tree setters must stay silent.




## Spawner wired to the factory, shooting still OFF (tests arm it).
func _spawner() -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(2, 50.0, 30.0))
	watch_signals(sp)
	return sp


## Frames until `sp` emitted `sig` `n` times (early break), -1 on timeout.
func _frames_until(sp: Object, sig: String, n: int, budget: int = 240) -> int:
	for i in budget:
		if get_signal_emit_count(sp, sig) >= n:
			return i
		await idle(1)
	return -1


func test_initial_delay_then_steady_interval() -> void:
	var sp := _spawner()
	sp.set_shoot_initial_delay_sec(0.5) # 30 frames
	sp.set_shoot_interval_sec(0.25) # 15 frames
	sp.reset_shooting() # re-arms the initial delay (it applies at _ready and here)
	sp.set_shooting_enabled(true)
	var first := await _frames_until(sp, "volley_fired", 1)
	# +-1: a frame boundary can land a hair before/after the exact delay.
	assert_between(first, 29, 31, "first shot after the initial delay (%d frames)" % first)
	var next := await _frames_until(sp, "volley_fired", 2)
	assert_between(next, 14, 16, "then one shot per interval (%d frames)" % next)
	sp.set_shooting_enabled(false)


func test_fire_n_volleys_spends_then_pauses_and_resume_shoots_normally() -> void:
	var sp := _spawner()
	sp.set_shoot_interval_sec(0.05)
	sp.fire_n_volleys(3)
	sp.set_shooting_enabled(true)
	await _frames_until(sp, "shooting_stopped", 1)
	await idle(10)
	assert_eq(sp.get_volleys_fired(), 3, "exactly the budget fired")
	assert_true(sp.is_shooting_paused(), "a spent budget pauses")
	assert_eq(sp.volleys_remaining(), -1, "a spent budget clears itself (unlimited again)")
	sp.resume_shooting()
	await _frames_until(sp, "volley_fired", 6)
	assert_gte(sp.get_volleys_fired(), 6, "resume shoots normally after a spent budget")
	assert_eq(sp.volleys_remaining(), -1, "still unlimited")
	sp.set_shooting_enabled(false)


func test_volleys_remaining_combines_cap_and_budget() -> void:
	var sp := _spawner()
	assert_eq(sp.volleys_remaining(), -1, "unlimited by default")
	sp.set_max_volleys(5)
	assert_eq(sp.volleys_remaining(), 5, "cap only")
	sp.fire_n_volleys(2)
	assert_eq(sp.volleys_remaining(), 2, "the smaller limit wins")
	assert_true(sp.shoot_once(), "manual shot")
	assert_eq(sp.volleys_remaining(), 1, "manual shots count against both")


func test_pre_tree_controls_survive_entering_the_tree() -> void:
	var sp := BulletSpawner2D.new()
	sp.set_homing_enabled(false)
	sp.set_spawn_data(H.make_volley_data(2, 50.0, 30.0))
	sp.set_shoot_interval_sec(0.05)
	sp.set_bullet_factory(factory)
	watch_signals(sp)
	sp.fire_n_volleys(2)
	sp.reset_shooting()
	sp.fire_n_volleys(2)
	assert_signal_emit_count(sp, "shooting_started", 0, "nothing reports before the tree")
	add(sp) # shooting_enabled defaults to true: starts on _ready
	assert_signal_emit_count(sp, "shooting_started", 1, "_ready reports the start once")
	await _frames_until(sp, "shooting_stopped", 1)
	await idle(10)
	assert_eq(sp.get_volleys_fired(), 2, "the pre-tree budget was honored")
	sp.set_shooting_enabled(false)


func test_pause_before_the_tree_is_kept() -> void:
	var sp := BulletSpawner2D.new()
	sp.set_homing_enabled(false)
	sp.set_spawn_data(H.make_volley_data(2, 50.0, 30.0))
	sp.set_bullet_factory(factory)
	sp.pause_shooting()
	watch_signals(sp)
	add(sp)
	await idle(10)
	assert_eq(sp.get_volleys_fired(), 0, "paused before the tree: still paused")
	assert_signal_emit_count(sp, "shooting_started", 0, "a paused spawner never reports a start")
	sp.set_shooting_enabled(false)


func test_shoot_once_deferred_fires_once_after_the_frame() -> void:
	var sp := _spawner()
	assert_true(sp.shoot_once_deferred(), "queued")
	assert_eq(sp.get_volleys_fired(), 0, "not fired synchronously")
	await idle(1)
	assert_eq(sp.get_volleys_fired(), 1, "fired once on the next frame")
	var outside := BulletSpawner2D.new()
	assert_false(outside.shoot_once_deferred(), "outside the tree it refuses")
	expect_error_sequence(["shoot_once_deferred: spawner is not inside the tree"])
	outside.free()


func test_nested_shoot_once_is_rejected() -> void:
	var a := _spawner()
	var b := _spawner()
	a.volley_fired.connect(func(_v, _i): a.shoot_once(), CONNECT_ONE_SHOT)
	assert_true(a.shoot_once(), "outer shot")
	expect_error_sequence(["shoot_once: re-entrant call from inside a spawner signal handler is not allowed"])
	b.volley_fired.connect(func(_v, _i): a.shoot_once(), CONNECT_ONE_SHOT)
	assert_true(b.shoot_once(), "outer shot on another spawner")
	expect_error_sequence(["shoot_once: re-entrant call from inside a spawner signal handler is not allowed"])
	assert_eq(a.get_volleys_fired(), 1, "the nested shots did not fire")


func test_every_failure_reports_a_skip_reason() -> void:
	var sp := _spawner()
	sp.set_bullet_factory(null)
	move_to_own_world(sp) # no factory in its world to fall back on
	assert_false(sp.shoot_once(), "no factory")
	expect_error_sequence(["no BulletFactory2D assigned"])
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(null)
	assert_false(sp.shoot_once(), "no spawn data")
	expect_error_sequence(["no spawn_data assigned"])
	sp.set_spawn_data(H.make_volley_data(2, 50.0, 30.0))
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_AIMED # no target
	assert_false(sp.shoot_once(), "no transforms")
	expect_error_sequence(["no aimed target assigned", "produced no transforms"])
	var reasons: Array = []
	for i in get_signal_emit_count(sp, "volley_skipped"):
		reasons.append(str(get_signal_parameters(sp, "volley_skipped", i)[0]))
	assert_eq(reasons, ["no_factory", "no_spawn_data", "no_transforms"], "each failure names its reason")


func test_over_budget_shot_skips_before_generating_the_pattern() -> void:
	var sp := _spawner()
	sp.set_max_live_bullets(2)
	assert_true(sp.shoot_once(), "first shot fills the budget (2 bullets)")
	var before: Dictionary = sp.debug_get_pattern_cache_info()
	assert_false(sp.shoot_once(), "over budget")
	var after: Dictionary = sp.debug_get_pattern_cache_info()
	assert_eq(after["hits"] + after["misses"], before["hits"] + before["misses"], "no pattern work for a skipped shot")
	assert_eq(str(get_signal_parameters(sp, "volley_skipped", 0)[0]), "over_budget", "reported as over budget")


func test_manual_shots_never_report_shooting_finished() -> void:
	var sp := _spawner() # auto-shooting off
	sp.set_max_volleys(1)
	assert_true(sp.shoot_once(), "manual shot reaches the cap")
	assert_signal_emit_count(sp, "shooting_finished", 0, "shooting_* signals track auto-fire only")


func test_reload_jitter_is_bounded_reproducible_and_seed_independent() -> void:
	# Gaps between shots in frames for a seeded spawner.
	var gaps_for := func(seed_value: int) -> Array:
		var sp := _spawner()
		sp.set_shoot_interval_sec(0.5)
		sp.set_reload_jitter_sec(0.2)
		sp.set_reload_jitter_seed(seed_value)
		sp.set_shooting_enabled(true)
		var stamps: Array = []
		var seen := 0
		for f in 400:
			await idle(1)
			var n: int = get_signal_emit_count(sp, "volley_fired")
			while seen < n:
				stamps.append(f)
				seen += 1
			if stamps.size() >= 7:
				break
		sp.set_shooting_enabled(false)
		var out: Array = []
		for i in range(1, stamps.size()):
			out.append(stamps[i] - stamps[i - 1])
		return out
	var a: Array = await gaps_for.call(7)
	var a2: Array = await gaps_for.call(7)
	var b: Array = await gaps_for.call(8)
	assert_eq(a.size(), 6, "six gaps measured")
	for g in a:
		assert_between(g, 17, 43, "gap within interval +- jitter (0.3..0.7 s)")
	assert_eq(a, a2, "same seed, same rhythm")
	# The old seeding (seed + volley index) made seed 8 replay seed 7 one shot
	# later; frame rounding hides that by at most one frame, so compare with
	# that tolerance: independent sequences differ by more somewhere.
	var lagged := true
	for i in 5:
		if absi(int(b[i]) - int(a[i + 1])) > 1:
			lagged = false
	assert_false(lagged, "consecutive seeds are not lagged copies of one sequence (a=%s b=%s)" % [a, b])


func test_cap_trip_by_shot_emits_finished_once() -> void:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.max_volleys = 2
	sp.set_shooting_enabled(true) # auto-fire configured: the cap trip is a finish
	watch_signals(sp)
	assert_true(sp.shoot_once())
	assert_true(sp.shoot_once())
	assert_signal_emit_count(sp, "shooting_finished", 1, "finished exactly once")
	assert_true(sp.shoot_once(), "manual shot past the cap still fires")
	assert_signal_emit_count(sp, "shooting_finished", 1, "no second finish past the cap")


func test_lowering_then_raising_the_cap() -> void:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.max_volleys = -1
	sp.set_shooting_enabled(true)
	assert_true(sp.shoot_once())
	assert_true(sp.shoot_once())
	watch_signals(sp)
	sp.max_volleys = sp.get_volleys_fired()
	assert_signal_emit_count(sp, "shooting_stopped", 1, "stopped on the crossing")
	assert_signal_emit_count(sp, "shooting_finished", 0, "no finish from the setter")
	assert_false(sp.is_shooting_active(), "inactive at the lowered cap")
	sp.max_volleys = sp.get_volleys_fired() + 3
	assert_signal_emit_count(sp, "shooting_started", 1, "started on raising")
	assert_true(sp.is_shooting_active(), "active again")


func _fresh() -> BulletSpawner2D:
	var sp := BulletSpawner2D.new()
	sp.set_spawn_data(H.make_volley_data(1, 0.0, 60.0))
	autofree(sp)
	watch_signals(sp)
	return sp


func test_pre_tree_setters_silent_then_load_emits_once() -> void:
	var sp := _fresh()
	sp.set_shooting_enabled(false)
	sp.set_shooting_enabled(true)
	sp.max_volleys = 0
	sp.max_volleys = -1
	assert_signal_emit_count(sp, "shooting_started", 0, "no started before the tree")
	assert_signal_emit_count(sp, "shooting_stopped", 0, "no stopped before the tree")
	sp.set_bullet_factory(factory)
	add_child(sp)
	await idle(5)
	assert_signal_emit_count(sp, "shooting_started", 1, "exactly one started across load")
	sp.set_shooting_enabled(false)
	assert_signal_emit_count(sp, "shooting_stopped", 1, "one stopped on disable")
	sp.set_shooting_enabled(true)
	assert_signal_emit_count(sp, "shooting_started", 2, "one more started on re-enable")


func test_pre_tree_cap_silent_tree_cap_reports() -> void:
	var sp := _fresh()
	sp.max_volleys = 0
	assert_signal_emit_count(sp, "shooting_stopped", 0, "pre-tree cap silent")
	sp.set_bullet_factory(factory)
	add_child(sp)
	await idle(1)
	var started_before: int = get_signal_emit_count(sp, "shooting_started")
	sp.max_volleys = -1
	assert_eq(get_signal_emit_count(sp, "shooting_started"), started_before + 1, "tree raise reports started")
	sp.max_volleys = 0
	assert_signal_emit_count(sp, "shooting_stopped", 1, "tree cap reports stopped")


## shooting_started / shooting_stopped report auto-fire TRANSITIONS only:
## reset_shooting() on a spawner that is already firing restarts its cadence
## silently (no second shooting_started without a stop between), while a
## reset that really restarts a finished or paused spawner reports once.
func test_reset_shooting_reports_only_a_real_restart() -> void:
	var sp := _spawner()
	sp.shoot_interval_sec = 0.05
	sp.set_shooting_enabled(true)
	assert_signal_emit_count(sp, "shooting_started", 1, "auto-fire started")
	sp.reset_shooting()
	assert_signal_emit_count(sp, "shooting_started", 1, "already firing: the restart is silent")
	sp.max_volleys = 1
	assert_gte(await _frames_until(sp, "shooting_finished", 1), 0, "the cap ends auto-fire")
	sp.reset_shooting()
	assert_signal_emit_count(sp, "shooting_started", 2, "a finished spawner restarting reports once")
	sp.max_volleys = -1
	sp.pause_shooting()
	sp.reset_shooting()
	assert_signal_emit_count(sp, "shooting_started", 3, "a paused spawner restarting reports once")
	assert_signal_emit_count(sp, "shooting_stopped", 1, "one stop: the pause")
	sp.set_shooting_enabled(false)


## Burst steps and pattern-list entries keep their average rate at any
## interval, like the main shooting timer: overshoot carries into the next
## wait instead of rounding every step up to whole frames (0.02 s at 60 fps
## once took 2 frames = 0.033 s per step). A pattern-list interval of 0
## stays one entry per frame.
func test_burst_and_pattern_list_steps_keep_their_rate() -> void:
	var sp := _spawner()
	sp.shoot_interval_sec = 100.0
	sp.burst_enabled = true
	sp.burst_count = 31
	sp.burst_interval_sec = 0.02
	sp.begin_burst()
	var frames := await _frames_until(sp, "burst_finished", 1)
	# 30 waits of 0.02 s = 0.6 s = 36 frames at 60 fps (+1 frame of slack).
	assert_between(frames, 35, 37, "a 31-shot chain at 0.02 s takes about 0.6 s")
	var entries: Array = []
	for i in 31:
		entries.append({"helper_bullets_amount": 2})
	var fired_before: int = get_signal_emit_count(sp, "volley_fired")
	sp.spawn_pattern_list(entries, false, 0.02)
	frames = await _frames_until(sp, "pattern_list_finished", 1)
	assert_between(frames, 35, 37, "31 entries at 0.02 s take about 0.6 s")
	assert_eq(get_signal_emit_count(sp, "volley_fired") - fired_before, 31, "every entry fired once")
	fired_before = get_signal_emit_count(sp, "volley_fired")
	sp.spawn_pattern_list([{"helper_bullets_amount": 2}, {"helper_bullets_amount": 2}, {"helper_bullets_amount": 2}], false, 0.0)
	await idle(1)
	assert_lte(get_signal_emit_count(sp, "volley_fired") - fired_before, 2, "interval 0: one entry per frame")
	await _frames_until(sp, "pattern_list_finished", 2)


## fire_n_volleys(n) from a volley_fired handler is a fresh budget: the shot
## that is reporting never counts against it (n more volleys fire).
func test_fire_n_volleys_from_volley_fired_fires_n_more() -> void:
	var sp := make_spawner(H.make_volley_data(1, 50.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.shoot_interval_sec = 0.05
	var armed: Array = []
	sp.volley_fired.connect(func(_v, _n) -> void:
		if armed.is_empty():
			armed.append(true)
			sp.fire_n_volleys(3))
	sp.set_shooting_enabled(true)
	for k in 90:
		await idle(1)
	assert_eq(sp.get_volleys_fired(), 4, "the first volley, then exactly 3 from the new budget")
	assert_true(sp.is_shooting_paused(), "the spent budget paused the wave")


## reset_shooting() from a volley_fired handler restarts the cadence from
## shoot_initial_delay_sec: the reporting pull never adds its interval on top.
func test_reset_shooting_from_volley_fired_restarts_the_delay() -> void:
	var sp := make_spawner(H.make_volley_data(1, 50.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.shoot_interval_sec = 0.05
	var frames: Array = []
	var frame := [0]
	sp.volley_fired.connect(func(_v, n) -> void:
		frames.append(frame[0])
		if n == 1:
			sp.shoot_initial_delay_sec = 0.5
			sp.reset_shooting())
	sp.set_shooting_enabled(true)
	for k in 60:
		await idle(1)
		frame[0] += 1
		if frames.size() >= 2:
			break
	assert_eq(frames.size(), 2, "the cadence resumed")
	assert_almost_eq(frames[1] - frames[0], 30, 1, "the next volley comes shoot_initial_delay_sec (30 frames) later")


func test_countdowns_read_the_spawners_clock() -> void:
	var sp := _spawner()
	assert_eq(sp.get_shoot_time_left(), 0.0, "idle: nothing counts down")
	sp.set_shoot_interval_sec(1.0)
	sp.set_shooting_enabled(true)
	await idle(2) # the first shot is immediate, then the 1 s wait
	assert_almost_eq(sp.get_shoot_time_left(), 1.0 - 1.0 / 60.0, 0.02, "the wait right after a shot")
	await idle(30)
	assert_almost_eq(sp.get_shoot_time_left(), 0.5 - 1.0 / 60.0, 0.02, "half of it left")
	sp.time_scale = 0.5
	await idle(30) # 0.25 s of spawner time
	assert_almost_eq(sp.get_shoot_time_left(), 0.25 - 1.0 / 60.0, 0.02, "on the scaled clock")
	sp.pause_shooting()
	assert_eq(sp.get_shoot_time_left(), 0.0, "paused: no automatic pull ahead")


func test_burst_telegraph_and_pattern_list_countdowns() -> void:
	var sp := _spawner()
	sp.set_shoot_interval_sec(10.0)
	sp.burst_count = 3
	sp.burst_interval_sec = 0.5
	sp.burst_enabled = true
	sp.telegraph_sec = 0.4
	sp.telegraph_enabled = true
	sp.set_shooting_enabled(true)
	await idle(2)
	assert_true(sp.is_telegraph_pending(), "the first burst shot warns")
	assert_almost_eq(sp.get_telegraph_time_left(), 0.4 - 1.0 / 60.0, 0.03, "the warning counts down")
	for f in 40:
		await idle(1)
		if not sp.is_telegraph_pending():
			break
	assert_false(sp.is_telegraph_pending(), "warned and fired")
	assert_eq(sp.get_telegraph_time_left(), 0.0, "no warning left")
	assert_gt(sp.get_burst_shots_left(), 0, "the chain runs")
	assert_almost_eq(sp.get_burst_time_left(), 0.5, 0.05, "the next burst shot in about 0.5 s")
	sp.set_shooting_enabled(false)
	await idle(2)
	assert_eq(sp.get_burst_time_left(), 0.0, "an auto chain ends with auto-fire")
	sp.spawn_pattern_list([{}, {}], false, 0.3)
	await idle(2)
	assert_almost_eq(sp.get_pattern_list_time_left(), 0.3 - 1.0 / 60.0, 0.03, "the next entry")
	sp.stop_pattern_list()
	assert_eq(sp.get_pattern_list_time_left(), 0.0, "stopped")


## process_callback: the loop (cadence, movement, spin, shots) runs in idle
## frames (Idle) or in physics ticks (Physics); Auto (the default) picks one.
func test_process_callback_runs_the_loop_in_the_chosen_callback() -> void:
	assert_eq([BulletSpawner2D.PROCESS_IDLE, BulletSpawner2D.PROCESS_PHYSICS, BulletSpawner2D.PROCESS_AUTO], [0, 1, 2], "serialized ids")
	for mode in [BulletSpawner2D.PROCESS_IDLE, BulletSpawner2D.PROCESS_PHYSICS]:
		var sp := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
		assert_eq(sp.process_callback, BulletSpawner2D.PROCESS_AUTO, "auto by default")
		sp.process_callback = mode
		sp.shoot_interval_sec = 0.1
		sp.shoot_initial_delay_sec = 0.0
		var in_physics: Array = []
		sp.volley_fired.connect(func(_v, _n) -> void: in_physics.append(Engine.is_in_physics_frame()))
		sp.set_shooting_enabled(true)
		await idle(31)
		var physics_mode: bool = mode == BulletSpawner2D.PROCESS_PHYSICS
		assert_between(in_physics.size(), 5, 6, "mode %d: 10 shots a second" % mode)
		assert_true(in_physics.all(func(x): return x == physics_mode), "mode %d: every shot in the chosen callback" % mode)
		assert_eq(sp.is_physics_processing(), physics_mode, "mode %d: physics processing" % mode)
		assert_eq(sp.is_processing(), not physics_mode, "mode %d: idle processing" % mode)


func test_switching_process_callback_keeps_the_cadence() -> void:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.shoot_interval_sec = 0.1
	sp.shoot_initial_delay_sec = 0.0
	var shots := [0]
	sp.volley_fired.connect(func(_v, _n) -> void: shots[0] += 1)
	sp.set_shooting_enabled(true)
	await idle(30)
	sp.process_callback = BulletSpawner2D.PROCESS_PHYSICS
	await idle(30)
	sp.process_callback = BulletSpawner2D.PROCESS_IDLE
	await idle(30)
	assert_between(shots[0], 14, 16, "a second and a half at 10 shots a second, across two switches")
	sp.process_callback = 5
	expect_error_sequence(["BulletSpawner2D: process_callback must be 0 (Idle), 1 (Physics) or 2 (Auto), keeping the old value."])


## Auto (the default): physics ticks while the spawner's factory interpolates
## (a moving spawner must move on ticks then, or it jitters), idle frames
## otherwise (smoother without interpolation); it follows a live switch of
## the factory's interpolation without losing the cadence.
func test_auto_follows_the_factorys_interpolation() -> void:
	factory.physics_interpolation = BulletFactory2D.INTERPOLATION_OFF
	var sp := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.shoot_interval_sec = 0.1
	sp.shoot_initial_delay_sec = 0.0
	var in_physics: Array = []
	sp.volley_fired.connect(func(_v, _n) -> void: in_physics.append(Engine.is_in_physics_frame()))
	sp.set_shooting_enabled(true)
	await idle(30)
	assert_true(not in_physics.is_empty() and in_physics.all(func(x): return x == false), "not interpolating: idle frames %s" % [in_physics])
	assert_eq([sp.is_processing(), sp.is_physics_processing()], [true, false], "the idle loop")
	var before := in_physics.size()
	factory.physics_interpolation = BulletFactory2D.INTERPOLATION_ON
	await idle(30)
	var after: Array = in_physics.slice(before + 1) # the shot of the switch frame may land in either
	assert_true(not after.is_empty() and after.all(func(x): return x == true), "interpolating: physics ticks %s" % [after])
	assert_eq([sp.is_processing(), sp.is_physics_processing()], [false, true], "the physics loop")
	factory.physics_interpolation = BulletFactory2D.INTERPOLATION_OFF
	await idle(30)
	assert_between(in_physics.size(), 14, 16, "a second and a half at 10 shots a second, across two switches")


func test_physics_streams_are_evenly_spaced() -> void:
	# Physics mode: a moving stream's bullets sit exactly speed x interval apart.
	var sp := make_spawner(H.make_volley_data(1, 300.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.process_callback = BulletSpawner2D.PROCESS_PHYSICS
	sp.shoot_interval_sec = 0.05
	sp.shoot_initial_delay_sec = 0.0
	var gaps: Array = await _stream_gaps(sp, 20, sp.set_shooting_enabled.bind(true))
	assert_gte(gaps.size(), 4, "a stream")
	for g in gaps:
		assert_almost_eq(g, 15.0, 0.01, "300 px/s x 0.05 s apart: %s" % [gaps])


## Shot timing: a shot's bullets move from when it was due, not from the tick
## that happened to fire it, so a stream sits exactly speed x interval apart
## at any interval (not only whole multiples of the tick), in both process
## modes, for plain auto fire, bursts and pattern lists alike, and several
## shots due inside one tick never stack.
func _stream_gaps(sp: BulletSpawner2D, frames: int, start := Callable()) -> Array:
	var volleys: Array = []
	sp.volley_fired.connect(func(v, _n) -> void: volleys.append(v))
	if start.is_valid():
		start.call()
	for i in frames:
		await physics(1)
	# Measured when every volley has had its first tick (a shot fired after
	# this frame's sweep moves on the next one), all at the same moment.
	sp.set_shooting_enabled(false)
	sp.stop_pattern_list()
	await idle(2)
	var gaps: Array = []
	for k in range(1, volleys.size()):
		gaps.append(snappedf((volleys[k - 1] as BulletVolley2D).get_bullet_transform(0).origin.x - (volleys[k] as BulletVolley2D).get_bullet_transform(0).origin.x, 0.01))
	return gaps


func test_streams_are_evenly_spaced_at_any_interval(mode: int = use_parameters([BulletSpawner2D.PROCESS_PHYSICS, BulletSpawner2D.PROCESS_IDLE])) -> void:
	for interval in [0.04, 0.025, 0.007]: # 2.4, 1.5 and 0.42 ticks (several shots in one tick)
		var sp := make_spawner(H.make_volley_data(1, 300.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
		sp.process_callback = mode
		sp.shoot_interval_sec = interval
		sp.max_shots_per_frame = 8
		var gaps: Array = await _stream_gaps(sp, 30, sp.set_shooting_enabled.bind(true))
		assert_gte(gaps.size(), 5, "a stream at %s s" % interval)
		var ideal: float = 300.0 * interval
		assert_true(gaps.all(func(g): return absf(g - ideal) < 0.05), "mode %d, %s s: every gap %.2f px: %s" % [mode, interval, ideal, gaps])
		sp.set_shooting_enabled(false)
		await idle(1)
		factory.free_active_bullets()


func test_burst_and_list_streams_are_evenly_spaced() -> void:
	var sp := make_spawner(H.make_volley_data(1, 300.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.burst_enabled = true
	sp.burst_count = 12
	sp.burst_interval_sec = 0.04
	var gaps: Array = await _stream_gaps(sp, 40, sp.begin_burst)
	assert_gte(gaps.size(), 8, "a burst stream")
	assert_true(gaps.all(func(g): return absf(g - 12.0) < 0.05), "burst: every gap 12 px: %s" % [gaps])
	await idle(1)
	factory.free_active_bullets()
	var lister := make_spawner(H.make_volley_data(1, 300.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	var entries: Array = []
	for i in 12:
		entries.append({})
	var list_gaps: Array = await _stream_gaps(lister, 40, lister.spawn_pattern_list.bind(entries, false, 0.04))
	assert_gte(list_gaps.size(), 8, "a list stream")
	assert_true(list_gaps.all(func(g): return absf(g - 12.0) < 0.05), "list: every gap 12 px: %s" % [list_gaps])


## Lowering what a wait was rolled from (shoot_interval_max_sec, the reload
## jitter) cuts a running wait down to the longest the new settings can roll,
## like a shorter shoot_interval_sec does.
func test_lowering_the_interval_range_cuts_the_running_wait(knob: String = use_parameters(["shoot_interval_max_sec", "reload_jitter_sec"])) -> void:
	var sp := make_spawner(H.make_volley_data(1, 50.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.shoot_interval_sec = 0.1
	sp.bullet_random_seed = 7
	sp.reload_jitter_seed = 7
	sp.set(knob, 9.0)
	assert_eq(sp.get(knob), 9.0, "%s set" % knob)
	sp.set_shooting_enabled(true)
	for i in 30:
		await idle(1)
		if sp.get_volleys_fired() > 0:
			break
	await idle(1)
	assert_gt(sp.get_shoot_time_left(), 0.5, "a long wait was rolled")
	sp.set(knob, 0.0)
	assert_lte(sp.get_shoot_time_left(), 0.1 + 0.0001, "%s lowered: the wait is cut to the interval" % knob)


## A handler inside the spawner's physics frame (its own volley_fired) that
## switches process_callback to Idle never makes the spawner step twice that
## frame (its idle step of the same frame is skipped).
func test_switching_to_idle_inside_a_physics_frame_steps_once_that_frame() -> void:
	var sp := make_spawner(H.make_volley_data(1, 50.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.process_callback = BulletSpawner2D.PROCESS_PHYSICS
	sp.shoot_interval_sec = 0.05
	var switched := [false]
	sp.volley_fired.connect(func(_v, _n) -> void:
		if not switched[0]:
			switched[0] = true
			sp.process_callback = BulletSpawner2D.PROCESS_IDLE)
	await idle(1)
	var f0 := Engine.get_process_frames()
	var r0 := sp.debug_get_run_frame_count()
	sp.set_shooting_enabled(true)
	for i in 30:
		await idle(1)
		if switched[0]:
			break
	await idle(5)
	assert_true(switched[0], "the handler switched to Idle")
	var frames := Engine.get_process_frames() - f0
	var runs := sp.debug_get_run_frame_count() - r0
	# Window: from this frame's idle step to the last frame's physics step
	# (Idle by then): one step per frame in between.
	assert_eq(runs, frames - 1, "one step per frame across the switch (%d steps, %d frames)" % [runs, frames])


## max_shots_per_frame bounds every timer (auto-fire, a burst chain, a
## pattern list): a 0.002 s interval is ~8.3 shots per 60 fps frame, so each
## frame fires exactly min(cap, due) and the timer resyncs (no growing
## backlog). 1 = never repay a hitch; 8 is the default.
func _shots_per_frame(sp: BulletSpawner2D, frames: int) -> Array:
	var out: Array = []
	for i in frames:
		var before: int = get_signal_emit_count(sp, "volley_fired")
		await idle(1)
		out.append(get_signal_emit_count(sp, "volley_fired") - before)
	return out


func test_max_shots_per_frame_bounds_every_timer(cap: int = use_parameters([1, 3, 8])) -> void:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 0.02))
	watch_signals(sp)
	assert_eq(sp.max_shots_per_frame, 8, "default")
	sp.max_shots_per_frame = cap
	sp.shoot_interval_sec = 0.002
	sp.set_shooting_enabled(true)
	await idle(2)
	var auto: Array = await _shots_per_frame(sp, 6)
	assert_eq(auto, [cap, cap, cap, cap, cap, cap], "auto-fire: %d per frame" % cap)
	sp.set_shooting_enabled(false)
	sp.shoot_interval_sec = 100.0
	sp.burst_enabled = true
	sp.burst_count = 200
	sp.burst_interval_sec = 0.002
	sp.begin_burst()
	await idle(1)
	var burst: Array = await _shots_per_frame(sp, 4)
	assert_eq(burst, [cap, cap, cap, cap], "a burst chain: %d per frame" % cap)
	sp.burst_enabled = false
	await idle(1)
	var entries: Array = []
	for i in 100:
		entries.append({"helper_bullets_amount": 1})
	sp.spawn_pattern_list(entries, false, 0.002)
	await idle(1)
	var listed: Array = await _shots_per_frame(sp, 4)
	assert_eq(listed, [cap, cap, cap, cap], "a pattern list: %d per frame" % cap)
	sp.stop_pattern_list()


func test_max_shots_per_frame_rejects_below_one() -> void:
	var sp := make_spawner()
	sp.max_shots_per_frame = 0
	expect_error_sequence(["BulletSpawner2D: max_shots_per_frame must be >= 1, keeping the old value."])
	assert_eq(sp.max_shots_per_frame, 8, "kept")
