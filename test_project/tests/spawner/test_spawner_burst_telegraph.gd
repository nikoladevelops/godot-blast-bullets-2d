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
