extends BlastTest
## Shooting cadence and its controls: initial delay + interval timing,
## pause / resume / fire_n_volleys / volleys_remaining / reset_shooting
## (including calls made before the spawner enters the tree), deferred and
## nested shots, skip reasons for every failure, reload jitter, and
## shooting_* signals tracking AUTO-fire only.
## Time is simulated (60 fps): one idle frame = 1/60 s.


## Spawner wired to the factory, shooting still OFF (tests arm it).
func _spawner() -> BulletSpawner2D:
	var sp := make_spawner(H.make_directional_data(2, 50.0, 30.0))
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
	sp.set_spawn_data(H.make_directional_data(2, 50.0, 30.0))
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
	sp.set_spawn_data(H.make_directional_data(2, 50.0, 30.0))
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
	assert_false(sp.shoot_once(), "no factory")
	expect_error_sequence(["no BulletFactory2D assigned"])
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(null)
	assert_false(sp.shoot_once(), "no spawn data")
	expect_error_sequence(["no spawn_data assigned"])
	sp.set_spawn_data(H.make_directional_data(2, 50.0, 30.0))
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
