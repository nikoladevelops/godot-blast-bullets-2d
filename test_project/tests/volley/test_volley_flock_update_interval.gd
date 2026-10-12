extends BlastTest
## BulletFlock2D.update_interval (the opt-in cost knob): 0 recomputes every
## bullet's steering every tick (the default, unchanged behavior); a positive
## interval recomputes each bullet once per interval, staggered across the
## flock so the work spreads evenly over the ticks, while every bullet keeps
## turning toward its last steering in between and still counts as a
## flockmate. A bullet with no steering yet (new volley, pool reuse, woken
## from a freeze) computes at once. The work drops by the interval's factor.

const DT := 1.0 / 60.0


func _stack(count: int) -> Array:
	var tr: Array = []
	for k in count:
		tr.append(Transform2D(0.0, Vector2(100, 100)))
	return tr


func _volley(tr: Array, flock: BulletFlock2D, speed := 0.0) -> BulletVolley2D:
	var v: BulletVolley2D = factory.spawn_volley(H.make_flight_data(tr, speed, 1000.0))
	v.flock_join(flock)
	return v


func _step(n: int) -> void:
	for k in n:
		assert_true(factory.debug_advance_time(DT), "tick %d runs" % k)


func test_the_default_recomputes_every_bullet_every_tick() -> void:
	var f := BulletFlock2D.new()
	assert_eq(f.update_interval, 0.0, "every tick by default")
	_volley(_stack(20), f)
	for k in 5:
		_step(1)
		var stats: Dictionary = factory.debug_get_flock_stats()
		assert_eq(stats["queried_members"], 20, "tick %d: every bullet recomputed" % k)


func test_an_interval_spreads_the_work_evenly() -> void:
	var f := BulletFlock2D.new()
	f.update_interval = 3.0 * DT
	_volley(_stack(60), f)
	_step(1) # first tick: nobody has steering yet, everyone computes
	assert_eq(factory.debug_get_flock_stats()["queried_members"], 60, "the first tick computes everyone")
	var total := 0
	var per_tick: Array = []
	for k in 30:
		_step(1)
		var q: int = factory.debug_get_flock_stats()["queried_members"]
		per_tick.append(q)
		total += q
	assert_almost_eq(float(total), 600.0, 30.0, "30 ticks at one refresh per 3 ticks: ~10 per bullet (%d)" % total)
	assert_lt(per_tick.max() - per_tick.min(), 12, "spread evenly over the ticks: %s" % [per_tick])


func test_the_work_drops_by_the_interval_factor() -> void:
	var f := BulletFlock2D.new()
	f.update_interval = 4.0 * DT
	_volley(_stack(100), f)
	_step(1)
	var checks := 0
	for k in 40:
		_step(1)
		checks += int(factory.debug_get_flock_stats()["neighbor_checks"])
	# Stacked: each recomputed bullet costs exactly the cap (32) checks.
	assert_almost_eq(float(checks), 40.0 * 100.0 * 32.0 / 4.0, 40.0 * 100.0 * 32.0 / 4.0 * 0.1, "a quarter of the every-tick work (%d checks)" % checks)


func test_a_bullet_keeps_its_steering_between_refreshes() -> void:
	var f := H.make_flock(0.0, 0.0, 1.0)
	f.update_interval = 5.0 * DT
	var v := _volley([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 40))], f, 100.0)
	_step(1)
	var changes := 0
	var last: Vector2 = v.debug_get_bullet_flock_steering(0)
	var turned := 0
	for k in 30:
		var before: Vector2 = v.get_bullet_direction(0)
		_step(1)
		var s: Vector2 = v.debug_get_bullet_flock_steering(0)
		if s != last:
			changes += 1
			last = s
		if v.get_bullet_direction(0) != before:
			turned += 1
	assert_between(changes, 5, 7, "30 ticks at one refresh per 5: ~6 updates (%d)" % changes)
	assert_eq(turned, 30, "it turns every tick toward its last steering")


func test_the_flock_still_flocks_with_an_interval() -> void:
	var f := H.make_flock(0.0, 0.0, 1.0)
	f.update_interval = 0.1
	var v := _volley([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 30))], f, 100.0)
	_step(30)
	assert_lt(v.get_bullet_transform(0).origin.distance_to(v.get_bullet_transform(1).origin), 25.0, "cohesion still closes the gap")


func test_bullets_without_steering_compute_at_once() -> void:
	var f := H.make_flock(0.0, 0.0, 1.0)
	f.update_interval = 100.0 # one refresh per 100 s: only the forced ones happen
	var a := _volley([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 30))], f, 100.0)
	_step(1)
	assert_ne(a.debug_get_bullet_flock_steering(0), Vector2(), "a new volley steers on its first tick")
	var b := _volley([Transform2D(0.0, Vector2(500, 0)), Transform2D(0.0, Vector2(500, 30))], f, 100.0)
	_step(1)
	assert_ne(b.debug_get_bullet_flock_steering(0), Vector2(), "a volley joining later too")
	a.set_is_auto_pooling_enabled(false)
	a.disable_bullet(1)
	_step(3)
	a.teleport_shift_all_bullets(Vector2(0, 0))
	a.enable_bullet(1)
	_step(1)
	assert_ne(a.debug_get_bullet_flock_steering(1), Vector2(), "a woken bullet computes at once")
	assert_eq(a.debug_get_bullet_flock_neighbors(1), 1, "with its current flockmate")


func test_pool_reuse_never_reads_an_old_life_steering() -> void:
	var f := H.make_flock(0.0, 0.0, 1.0)
	f.update_interval = 100.0
	var old := _volley([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 30))], f, 100.0)
	_step(2)
	assert_ne(old.debug_get_bullet_flock_steering(0), Vector2(), "the old life steered")
	old.disable_bullet(0)
	old.disable_bullet(1)
	var reused: BulletVolley2D = factory.spawn_volley(H.make_flight_data([Transform2D(0.0, Vector2(400, 0)), Transform2D(0.0, Vector2(400, 300))], 100.0, 1000.0))
	assert_eq(reused, old, "the pool handed it back")
	reused.flock_join(f)
	_step(1)
	assert_eq(reused.debug_get_bullet_flock_steering(0), Vector2(), "300 px apart: no steering, never the old life's")
	assert_eq(reused.debug_get_bullet_flock_neighbors(0), 0, "no flockmate in range")


func test_update_interval_rejects_bad_values() -> void:
	var f := BulletFlock2D.new()
	f.update_interval = -0.1
	f.update_interval = NAN
	f.update_interval = INF
	expect_error_sequence([
		"BulletFlock2D: update_interval must be finite and >= 0, keeping the old value.",
		"BulletFlock2D: update_interval must be finite and >= 0, keeping the old value.",
		"BulletFlock2D: update_interval must be finite and >= 0, keeping the old value.",
	])
	assert_eq(f.update_interval, 0.0, "kept")
