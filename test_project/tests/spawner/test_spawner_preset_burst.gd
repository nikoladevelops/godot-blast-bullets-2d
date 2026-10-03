extends BlastTest
## Spin presets advance from idle (apply_pattern_preset refreshes process
## state); failed burst shots are retried, never silently consumed; the chain
## resumes with a stable rhythm; a permanent failure aborts with
## burst_finished; out-of-range presets are rejected loudly. Bursts are
## driven only through the public begin_burst() and real frames.


func _burst_spawner() -> BulletSpawner2D:
	var sp := make_spawner(H.make_directional_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.burst_enabled = true
	watch_signals(sp)
	return sp


func test_spin_preset_advances_from_idle() -> void:
	var sp := make_spawner(H.make_directional_data(1, 0.0, 60.0))
	sp.apply_pattern_preset(BulletFactory2D.PATTERN_PRESET_TWIN_SPIRAL_COUNTER)
	assert_true(sp.spin_enabled, "preset arms spin")
	var a0: float = sp.get_spin_angle_deg()
	await idle(30)
	assert_gt(absf(sp.get_spin_angle_deg() - a0), 5.0, "spin advanced from idle")


## Idles until `sp` emitted `sig` at least `n` times (early break; the
## frame budget is generous: burst intervals are a few frames each).
func _until(sp: Object, sig: String, n: int, frames: int = 120) -> void:
	for i in frames:
		if get_signal_emit_count(sp, sig) >= n:
			return
		await idle(1)


func test_failed_shots_retry_and_chain_resumes() -> void:
	var sp := _burst_spawner()
	sp.burst_count = 3
	sp.burst_interval_sec = 0.05 # 3 frames at the fixed 60 fps
	sp.max_live_bullets = 1 # over-budget fails deterministically, recovers via free
	sp.begin_burst()
	await _until(sp, "burst_shot_fired", 1)
	assert_signal_emit_count(sp, "burst_shot_fired", 1, "first burst shot fired")
	await _until(sp, "volley_skipped", 2)
	assert_eq(sp.get_burst_shots_left(), 2, "failed shots NOT consumed")
	assert_signal_not_emitted(sp, "burst_finished", "burst still open during a transient failure")
	assert_signal_emit_count(sp, "volley_skipped", 2, "each failed attempt reported")
	factory.free_active_bullets()
	await _until(sp, "burst_shot_fired", 2)
	assert_eq(sp.get_burst_shots_left(), 1, "second success leaves one")
	factory.free_active_bullets()
	await _until(sp, "burst_finished", 1)
	assert_signal_emit_count(sp, "burst_shot_fired", 3, "all three shots fired after recovery")
	assert_signal_emit_count(sp, "burst_finished", 1, "burst_finished exactly once")
	for i in 3:
		assert_false(get_signal_parameters(sp, "burst_shot_fired", i)[1], "shot %d not mirrored (alternate mirror off)" % i)


func test_permanent_failure_aborts_chain() -> void:
	var sp := _burst_spawner()
	sp.burst_count = 2
	sp.burst_interval_sec = 0.02
	sp.set_bullet_factory(null)
	sp.begin_burst()
	await idle(1)
	await idle(1)
	expect_error("no BulletFactory2D assigned")
	assert_signal_not_emitted(sp, "burst_finished", "single failure does not abort yet")
	assert_eq(sp.get_burst_shots_left(), 2, "shot retried, not consumed")
	await _until(sp, "burst_finished", 1)
	expect_error("no BulletFactory2D assigned")
	assert_signal_emit_count(sp, "burst_finished", 1, "permanent failure aborts with burst_finished")
	assert_eq(sp.get_burst_shots_left(), 0, "chain drained on abort")
	assert_signal_emit_count(sp, "volley_skipped", 2, "each attempt reports a skip")
	assert_eq(str(get_signal_parameters(sp, "volley_skipped", 0)[0]), "no_factory", "with the config reason")


func test_out_of_range_preset_rejected() -> void:
	var sp := make_spawner()
	sp.apply_pattern_preset(999)
	expect_error("preset out of range")
	assert_eq(sp.pattern_source, BulletSpawner2D.PATTERN_FROM_HELPER_RING, "bad preset keeps the old source")
	sp.apply_pattern_preset(-5)
	expect_error("preset out of range")
	assert_eq(sp.pattern_source, BulletSpawner2D.PATTERN_FROM_HELPER_RING, "negative preset keeps the old source")
