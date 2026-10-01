extends SceneTree
## Preset process-state + burst failure-policy suite.
##
## Bug 1: apply_pattern_preset() wrote spin_enabled raw, skipping the process
## state refresh, so spin presets on an idle spawner never advanced.
## Bug 2: fire_burst_volley() consumed a burst shot even when shoot_once()
## failed (busy factory, over budget, no transforms), silently dropping shots.
## Failed shots are now retried (mirror rhythm and indexes stable); after a
## full burst's worth of consecutive failures the chain aborts with
## burst_finished (each attempt reports through volley_skipped).
##
## Covers: T1 spin preset advances from idle, T2 failed shots are retried not
## consumed, T3 firing resumes the same chain with stable mirror rhythm,
## T4 permanent failure aborts with burst_finished, T5 no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_preset_burst.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0
var _shot_mirror: Array = []
var _finished := 0
var _skipped := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_burst_shot(_idx: int, mirrored: bool) -> void:
	_shot_mirror.append(mirrored)

func _on_burst_finished() -> void:
	_finished += 1

func _on_skipped(_reason: StringName) -> void:
	_skipped += 1

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	s.speed = 0.0
	s.max_speed = 3000.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = 60.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	return d

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("PRESETBURST T1 spin preset advances from idle")
	var sp := BulletSpawner2D.new()
	get_root().add_child(sp)
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(_data())
	sp.set_shooting_enabled(false)
	sp.apply_pattern_preset(BulletFactory2D.PATTERN_PRESET_TWIN_SPIRAL_COUNTER)
	_check(sp.spin_enabled, "T1 preset arms spin")
	var a0: float = sp.get_spin_angle_deg()
	for i in 30:
		await process_frame
	var a1: float = sp.get_spin_angle_deg()
	_check(absf(a1 - a0) > 5.0, "T1 spin advanced from idle (%.1f -> %.1f)" % [a0, a1])
	sp.queue_free()
	await process_frame

	# ---------------------------------------------------------------
	printerr("PRESETBURST T2 failed burst shots retry instead of dropping")
	var sp2 := BulletSpawner2D.new()
	get_root().add_child(sp2)
	sp2.set_bullet_factory(factory)
	sp2.set_spawn_data(_data())
	sp2.set_shooting_enabled(false)
	sp2.burst_enabled = true
	sp2.burst_count = 3
	sp2.burst_interval_sec = 0.05
	sp2.volley_skipped.connect(_on_skipped)
	sp2.burst_shot_fired.connect(_on_burst_shot)
	sp2.burst_finished.connect(_on_burst_finished)
	# No factory would fail everything, but then recovery needs reattach;
	# over-budget fails deterministically and recovers via free.
	# Manual fire_burst_volley() calls (not wall timing): fully deterministic.
	sp2.max_live_bullets = 1
	_shot_mirror.clear()
	_finished = 0
	_skipped = 0
	sp2.begin_burst()
	sp2.fire_burst_volley()
	_check(_shot_mirror.size() == 1, "T2 first burst shot fired (got %d)" % _shot_mirror.size())
	sp2.fire_burst_volley()
	sp2.fire_burst_volley()
	_check(sp2.get_burst_shots_left() == 2, "T2 failed shots NOT consumed (left=%d)" % sp2.get_burst_shots_left())
	_check(_finished == 0, "T2 burst still open during transient failure")
	_check(_skipped == 2, "T2 each failed attempt reported volley_skipped (got %d)" % _skipped)
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("PRESETBURST T3 chain resumes with stable rhythm after recovery")
	sp2.fire_burst_volley()
	_check(sp2.get_burst_shots_left() == 1, "T3 second success leaves one (left=%d)" % sp2.get_burst_shots_left())
	factory.free_active_bullets()
	await process_frame
	sp2.fire_burst_volley()
	_check(_shot_mirror.size() == 3, "T3 all three shots fired after recovery (got %d)" % _shot_mirror.size())
	_check(_finished == 1, "T3 burst_finished exactly once")
	# Mirror alternates per remaining count: shots at left=3,2,1 -> mirrored
	# flags [false, true, false] for alternate mode off... burst_alternate_mirror
	# defaults off, so every shot is plain.
	_check(_shot_mirror == [false, false, false], "T3 non-mirrored rhythm stable (%s)" % str(_shot_mirror))
	sp2.queue_free()
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("PRESETBURST T4 permanent failure aborts the chain")
	var sp4 := BulletSpawner2D.new()
	get_root().add_child(sp4)
	sp4.set_bullet_factory(factory)
	sp4.set_spawn_data(_data())
	sp4.set_shooting_enabled(false)
	sp4.burst_enabled = true
	sp4.burst_count = 2
	sp4.burst_interval_sec = 0.02
	sp4.burst_finished.connect(_on_burst_finished)
	sp4.volley_skipped.connect(_on_skipped)
	_finished = 0
	_skipped = 0
	sp4.set_bullet_factory(null)
	sp4.begin_burst()
	sp4.fire_burst_volley()
	_check(_finished == 0, "T4 single failure does not abort yet")
	_check(sp4.get_burst_shots_left() == 2, "T4 shot retried, not consumed (left=%d)" % sp4.get_burst_shots_left())
	sp4.fire_burst_volley()
	_check(_finished == 1, "T4 permanent failure aborts with burst_finished")
	_check(sp4.get_burst_shots_left() == 0, "T4 chain drained on abort (left=%d)" % sp4.get_burst_shots_left())
	_check(_skipped == 0, "T4 config failure reports no volley_skipped spam (got %d)" % _skipped)
	sp4.queue_free()
	await process_frame

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL PRESET-BURST TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
