extends BlastTest
## Spin off-vs-on + cold-vs-warm-pool benchmarks (heart, 1500 bullets).
## Prints timings for the report; asserts generous budgets that catch cliffs
## (10x regressions), never 10% drift (debug headless jitters).

const N := 1500


func _heart_spawner() -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(4, 250.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_HEART, N)
	sp.helper_heart_size = 150.0
	return sp


func test_collect_heart_spin_off_vs_on() -> void:
	var sp := _heart_spawner()
	await idle(1)
	var t0 := Time.get_ticks_usec()
	for i in 10:
		var tf: Array = sp.collect_spawn_transforms()
		assert_eq(tf.size(), N, "heart collects 1500")
	var off_ms := float(Time.get_ticks_usec() - t0) / 1000.0 / 10.0
	sp.set_spin_enabled(true)
	sp.set_spin_speed_deg_per_sec(90.0)
	await idle(3)
	t0 = Time.get_ticks_usec()
	for i in 10:
		var tf2: Array = sp.collect_spawn_transforms()
		assert_eq(tf2.size(), N, "heart collects 1500 with spin")
	var on_ms := float(Time.get_ticks_usec() - t0) / 1000.0 / 10.0
	print("BENCH heart1500 collect: off=%.3fms on=%.3fms" % [off_ms, on_ms])
	assert_lt(off_ms, 100.0, "no-spin collect budget")
	assert_lt(on_ms, 100.0, "spin collect budget")


func test_shoot_tick_heart_spin_off_vs_on() -> void:
	# NOTE: wall-clock over physics() frames measures real-time pacing, not
	# CPU (headless still paces physics at 60Hz). This test only pins that
	# repeated shots keep working with spin on; CPU is measured synchronously
	# in test_shoot_once_cpu_cold_vs_warm below.
	var sp := _heart_spawner()
	await idle(1)
	for round in 2:
		assert_true(sp.shoot_once(), "shot fires")
		await physics(5)
		await idle(1)
		factory.free_active_bullets()
		await idle(1)
		sp.set_spin_enabled(true)
		sp.set_spin_speed_deg_per_sec(90.0)
		await idle(2)
		assert_true(sp.shoot_once(), "spin shot fires")
		await physics(5)
		await idle(1)
		factory.free_active_bullets()
		await idle(1)
		sp.set_spin_enabled(false)
		await idle(1)


func test_shoot_once_cpu_cold_vs_warm() -> void:
	# Pure-CPU shoot_once timing (no frames inside the measurement) plus the
	# deterministic pin that matters: repeated spawner shots must REUSE the
	# pool (a miss-every-shot pattern reallocates 1500 areas per volley).
	var sp := _heart_spawner()
	await idle(1)
	var colds: Array = []
	for i in 3:
		factory.reset()
		await idle(1)
		var t0 := Time.get_ticks_usec()
		assert_true(sp.shoot_once(), "cold shot fires")
		colds.push_back(float(Time.get_ticks_usec() - t0) / 1000.0)
		await idle(1)
		factory.free_active_bullets()
		await idle(1)
	factory.reset()
	await idle(1)
	factory.debug_reset_pool_stats()
	var warms: Array = []
	for i in 3:
		var t1 := Time.get_ticks_usec()
		assert_true(sp.shoot_once(), "warm shot fires")
		warms.push_back(float(Time.get_ticks_usec() - t1) / 1000.0)
		await idle(1)
		# clear (not free): cleared volleys PARK pooled, freed ones are
		# destroyed. Warmth only exists via clear/expiry.
		factory.clear_active_bullets()
		await idle(1)
	var stats: Dictionary = factory.debug_get_pool_hit_stats()
	print("BENCH heart1500 shoot_once cold_ms=", colds, " warm_ms=", warms, " pool=", stats)
	assert_gte(int(stats.get("hits", 0)), 2, "repeated shots reuse the pool")


func test_first_spawn_cold_vs_warm_pool() -> void:
	# Times ONLY the shoot_once() call (no frames inside the measurement) and
	# parks the first volley with clear_active_bullets(): free_active_bullets()
	# DESTROYS volleys, so a shot after it is cold again (the old version of
	# this test measured two cold shots).
	var sp := _heart_spawner()
	await idle(1)
	var t0 := Time.get_ticks_usec()
	assert_true(sp.shoot_once(), "first-ever shot fires")
	var cold_ms := float(Time.get_ticks_usec() - t0) / 1000.0
	await idle(1)
	factory.clear_active_bullets()
	await idle(2)
	factory.debug_reset_pool_stats()
	t0 = Time.get_ticks_usec()
	assert_true(sp.shoot_once(), "pooled shot fires")
	var warm_ms := float(Time.get_ticks_usec() - t0) / 1000.0
	var stats: Dictionary = factory.debug_get_pool_hit_stats()
	assert_eq(int(stats.get("hits", 0)), 1, "second shot is a pool hit (warm)")
	assert_eq(int(stats.get("misses", 0)), 0, "no pool miss on the warm shot")
	print("BENCH heart1500 cold=%.3fms warm=%.3fms" % [cold_ms, warm_ms])
	# Catastrophe guards only (debug build, shared CI cores); the real
	# tracking lives in tools/run_benchmarks.py.
	assert_lt(cold_ms, 2000.0, "cold spawn budget")
	assert_lt(warm_ms, 2000.0, "warm spawn budget")
