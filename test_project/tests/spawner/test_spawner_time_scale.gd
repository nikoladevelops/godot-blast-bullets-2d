extends BlastTest
## Time scale on spawners: a spawner runs on factory.time_scale x its own
## time_scale (cadence, initial delay, bursts, telegraph, spin, movement),
## and its volleys follow the same product unless
## time_affects_volleys is off. Scale 0 freezes and resumes with the exact
## remaining time; huge scales stay bounded; orphans fall back to the factory
## scale; nothing leaks into pooled volleys. One idle frame = 1/60 s.

var fired: Array = []


func _on_fired(volley: BulletVolley2D, _count: int) -> void:
	fired.append(volley)


func before_each() -> void:
	await super()
	fired.clear()


## An auto-firing spawner: one still bullet per shot, every `interval` s.
func _shooter(interval: float = 0.1, speed: float = 0.0) -> BulletSpawner2D:
	var data := H.make_volley_data(1, speed, 30.0)
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.shoot_interval_sec = interval
	sp.shoot_initial_delay_sec = 0.0
	sp.volley_fired.connect(_on_fired)
	return sp


func _start(sp: BulletSpawner2D) -> void:
	sp.set_shooting_enabled(true)


func _shots_over(frames: int) -> int:
	var before := fired.size()
	await idle(frames)
	return fired.size() - before


func test_defaults_and_group() -> void:
	var sp := make_spawner()
	assert_eq(sp.time_scale, 1.0, "real time by default")
	assert_true(sp.time_scale_affects_volleys, "volleys follow by default")
	var group := ""
	var found := {}
	for p in sp.get_property_list():
		if p["usage"] & PROPERTY_USAGE_GROUP:
			group = p["name"]
		elif p["name"] in ["time_scale", "time_scale_affects_volleys"]:
			found[p["name"]] = group
	assert_eq(found, { "time_scale": "Time", "time_scale_affects_volleys": "Time" }, "both live in the Time group")


func test_setter_rejects_bad_values_and_keeps_the_old_one() -> void:
	var sp := make_spawner()
	sp.time_scale = 0.5
	for bad in [NAN, INF, -INF, -0.1]:
		sp.time_scale = bad
		expect_error_sequence(["BulletSpawner2D: time_scale must be finite and >= 0, keeping the old value."])
	assert_eq(sp.time_scale, 0.5, "old value kept")
	sp.time_scale = 0.0
	assert_eq(sp.time_scale, 0.0, "zero is valid (freeze)")


func test_spawner_scale_scales_the_cadence() -> void:
	var sp := _shooter(0.1)
	_start(sp)
	var full := await _shots_over(60) # 1 s at 10 shots/s
	assert_between(full, 9, 11, "baseline: about 10 shots per second")
	sp.time_scale = 0.5
	var half := await _shots_over(60)
	assert_between(half, 4, 6, "half speed: about 5 shots per second")
	sp.time_scale = 2.0
	var double := await _shots_over(60)
	assert_between(double, 19, 21, "double speed: about 20 shots per second")


func test_factory_scale_now_slows_spawners_too() -> void:
	var sp := _shooter(0.1)
	_start(sp)
	factory.time_scale = 0.5
	assert_between(await _shots_over(60), 4, 6, "the factory scale slows the spawner it serves")
	factory.time_scale = 0.0
	assert_eq(await _shots_over(60), 0, "factory scale 0 stops shooting")
	factory.time_scale = 1.0
	assert_gt(await _shots_over(30), 0, "and it resumes")


func test_scales_multiply() -> void:
	var sp := _shooter(0.1)
	_start(sp)
	sp.time_scale = 0.5
	factory.time_scale = 0.5
	assert_between(await _shots_over(120), 4, 6, "0.5 x 0.5 = 0.25: about 2.5 shots per second over 2 s")


func test_scale_zero_freezes_and_resumes_with_the_exact_remaining_time() -> void:
	var sp := _shooter(1.0)
	_start(sp)
	await idle(2)
	assert_eq(fired.size(), 1, "the first shot is immediate")
	await idle(30) # 0.5 s of the 1 s wait
	sp.time_scale = 0.0
	assert_eq(await _shots_over(180), 0, "frozen for 3 s: no shot")
	sp.time_scale = 1.0
	var frames := 0
	while fired.size() < 2 and frames < 120:
		await idle(1)
		frames += 1
	assert_between(frames, 26, 34, "about 0.5 s (30 frames) of the wait was left")


func test_initial_delay_runs_on_the_scaled_clock() -> void:
	var sp := _shooter(5.0)
	sp.shoot_initial_delay_sec = 0.5
	sp.reset_shooting()
	sp.time_scale = 0.5
	_start(sp)
	assert_eq(await _shots_over(45), 0, "0.75 s of real time is only 0.375 s of spawner time")
	assert_gt(await _shots_over(45), 0, "the delay ends after about 1 s of real time")


func test_scale_change_mid_wait_keeps_the_elapsed_fraction() -> void:
	var sp := _shooter(1.0)
	_start(sp)
	await idle(2)
	await idle(30) # half the wait elapsed
	sp.time_scale = 0.5 # the other half now takes 1 s
	var frames := 0
	while fired.size() < 2 and frames < 200:
		await idle(1)
		frames += 1
	assert_between(frames, 56, 64, "the remaining 0.5 s runs at half speed")


func test_telegraph_countdown_follows_the_scale() -> void:
	var sp := _shooter(5.0)
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.5
	sp.time_scale = 0.5
	_start(sp)
	var frames := 0
	while fired.size() < 1 and frames < 200:
		await idle(1)
		frames += 1
	assert_between(frames, 56, 66, "a 0.5 s warning lasts about 1 s at half speed")


func test_burst_chain_follows_the_scale() -> void:
	var sp := _shooter(5.0)
	sp.burst_enabled = true
	sp.burst_count = 3
	sp.burst_interval_sec = 0.2
	sp.time_scale = 0.5
	_start(sp)
	var frames := 0
	while fired.size() < 3 and frames < 300:
		await idle(1)
		frames += 1
	assert_between(frames, 48, 60, "3 shots 0.2 s apart take 0.4 s of spawner time = about 0.8 s")


func test_spin_follows_the_scale() -> void:
	var sp := make_spawner()
	sp.spin_enabled = true
	sp.spin_mode = 0 # continuous
	sp.spin_speed_deg_per_sec = 90.0
	sp.reset_spin_angle()
	await idle(60)
	var full := sp.get_spin_angle_deg()
	assert_almost_eq(full, 90.0, 4.0, "baseline: 90 degrees in a second")
	sp.time_scale = 0.5
	sp.reset_spin_angle()
	await idle(60)
	assert_almost_eq(sp.get_spin_angle_deg(), 45.0, 3.0, "half speed: 45 degrees")
	factory.time_scale = 0.0
	sp.reset_spin_angle()
	await idle(30)
	assert_eq(sp.get_spin_angle_deg(), 0.0, "factory scale 0 freezes the spin")


func test_movement_follows_the_scale_and_reports_zero_velocity_when_frozen() -> void:
	var path := Path2D.new()
	var curve := Curve2D.new()
	curve.add_point(Vector2(0, 0))
	curve.add_point(Vector2(300, 0))
	path.curve = curve
	add(path)
	var sp := make_spawner()
	sp.movement_transition = 0
	sp.movement_ease = 0
	sp.movement_duration_sec = 1.0
	sp.set_movement_path_node(path)
	sp.time_scale = 0.5
	sp.movement_enabled = true
	await idle(60)
	assert_almost_eq(sp.global_position.x, 150.0, 8.0, "half the path after 1 s at half speed")
	sp.time_scale = 0.0
	await idle(10)
	var held := sp.global_position.x
	await idle(30)
	assert_eq(sp.global_position.x, held, "frozen: it does not move")
	assert_eq(sp.get_movement_velocity(), Vector2.ZERO, "and reports no velocity")


func test_volleys_follow_the_spawner_scale_unless_opted_out() -> void:
	var sp := _shooter(100.0, 100.0)
	sp.shoot_initial_delay_sec = 0.0
	_start(sp)
	await idle(2)
	var v: BulletVolley2D = fired.back()
	var x0: float = v.get_bullet_transform(0).origin.x
	sp.time_scale = 0.5
	await idle(60)
	var half: float = v.get_bullet_transform(0).origin.x - x0
	assert_almost_eq(half, 50.0, 3.0, "100 px/s at half speed: 50 px in a second")
	sp.time_scale_affects_volleys = false
	var x1: float = v.get_bullet_transform(0).origin.x
	await idle(60)
	assert_almost_eq(v.get_bullet_transform(0).origin.x - x1, 100.0, 3.0, "opted out: the bullets run at the factory scale")


func test_volley_lifetime_runs_on_the_scaled_clock() -> void:
	var data := H.make_volley_data(1, 0.0, 1.0)
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.time_scale = 0.5
	assert_true(sp.shoot_once(), "shot")
	await idle(90) # 1.5 s real = 0.75 s of bullet time
	assert_eq(factory.get_active_bullet_count(), 1, "still alive")
	await idle(60)
	assert_eq(factory.get_active_bullet_count(), 0, "expired after 2 s real time")


func test_orphans_fall_back_to_the_factory_scale() -> void:
	var sp := _shooter(100.0, 100.0)
	sp.orphaned_volleys = 0 # keep flying
	_start(sp)
	await idle(2)
	var v: BulletVolley2D = fired.back()
	sp.time_scale = 0.5
	await idle(10)
	sp.get_parent().remove_child(sp)
	sp.free()
	var x0: float = v.get_bullet_transform(0).origin.x
	await idle(60)
	assert_almost_eq(v.get_bullet_transform(0).origin.x - x0, 100.0, 3.0, "no spawner left: full factory-scale speed")


func test_huge_scale_stays_bounded() -> void:
	var sp := _shooter(0.001)
	sp.time_scale = 100000.0
	_start(sp)
	await idle(5)
	assert_lte(fired.size(), 8 * 5 + 8, "at most 8 pulls per frame, however large the scale")
	assert_gt(fired.size(), 0, "and it still shoots")


func test_scale_survives_reparenting() -> void:
	var sp := make_spawner()
	sp.time_scale = 0.25
	sp.time_scale_affects_volleys = false
	var holder := Node2D.new()
	add(holder)
	sp.get_parent().remove_child(sp)
	holder.add_child(sp)
	assert_eq(sp.time_scale, 0.25, "scale kept")
	assert_false(sp.time_scale_affects_volleys, "toggle kept")


func test_spawner_without_a_factory_uses_its_own_scale() -> void:
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.set_homing_enabled(false)
	sp.time_scale = 0.5
	sp.spin_enabled = true
	sp.spin_speed_deg_per_sec = 90.0
	add(sp)
	await idle(60)
	assert_almost_eq(sp.get_spin_angle_deg(), 45.0, 3.0, "no factory: only the spawner scale applies")


func test_scale_does_not_leak_into_pooled_volleys() -> void:
	var data := H.make_volley_data(1, 100.0, 30.0)
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.time_scale = 0.25
	assert_true(sp.shoot_once(), "shot")
	await idle(2)
	factory.clear_active_bullets()
	await idle(1)
	var fresh: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 100.0, 30.0))
	await idle(1)
	var x0: float = fresh.get_bullet_transform(0).origin.x
	await idle(60)
	assert_almost_eq(fresh.get_bullet_transform(0).origin.x - x0, 100.0, 3.0, "a reused volley with no owner runs at full speed")


## Any finite time_scale is accepted, so the spin step can overflow (a huge
## scale times the spin speed, or the oscillation clock): the spin angle stays
## finite (a NaN angle once stuck forever and broke every later shot).
func test_spin_angle_stays_finite_at_a_huge_time_scale() -> void:
	for mode in [BulletSpawner2D.SPIN_CONTINUOUS]:
		var sp := make_spawner()
		sp.spin_enabled = true
		sp.spin_mode = mode
		sp.spin_speed_deg_per_sec = 1e30 # x (delta x 1e300) overflows a double
		sp.time_scale = 1e300
		await idle(3)
		assert_true(is_finite(sp.get_spin_angle_deg()), "mode %d: the spin angle stays finite" % mode)
		sp.time_scale = 1.0
		assert_true(sp.shoot_once(), "mode %d: shots still work" % mode)
