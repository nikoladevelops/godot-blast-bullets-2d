extends BlastTest
## Spin-path perf: a 10k-bullet collect with advancing spin must stay far
## below frame budget. Spin is one folded matrix per volley (not per-bullet
## trig), so the collect cost is the generator's alone. Bound is generous
## on purpose (debug headless jitters): it catches pathological regressions
## (per-bullet extension round-trips, trig storms), not 10% drift.


func test_10k_spin_collect_budget() -> void:
	var sp := make_spawner(H.make_directional_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_SPIRAL, 10000)
	sp.set_spin_enabled(true)
	sp.set_spin_speed_deg_per_sec(90.0)
	await idle(2)
	var worst := 0
	for i in 5:
		var t0 := Time.get_ticks_usec()
		var tf: Array = sp.collect_spawn_transforms()
		var dt := int(Time.get_ticks_usec() - t0)
		worst = maxi(worst, dt)
		assert_eq(tf.size(), 10000, "10k spiral collects with spin on")
		await idle(1)
	print("worst 10k spin collect usec=", worst)
	assert_lt(worst, 25000, "10k spin collect stays in the millisecond class")
