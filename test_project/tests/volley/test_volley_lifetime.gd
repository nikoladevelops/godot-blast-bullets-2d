extends BlastTest
## Lifetimes: a short finite lifetime expires, signals (deferred) and pools;
## infinite never expires while the curve clock advances; invalid lifetimes
## are rejected at the resource setter; max collisions 0 = infinite; the
## curve clock rejects NaN; the live infinite toggle; counts readable before
## an expiry.


func test_short_lifetime_expires_signals_and_pools() -> void:
	var quick := H.make_volley_data(2, 300.0, 0.15)
	quick.is_life_time_over_signal_enabled = true
	watch_signals(factory)
	var q: BulletVolley2D = factory.spawn_volley(quick)
	for i in 30:
		await physics()
		if not q.is_bullet_status_enabled(0) and not q.is_bullet_status_enabled(1):
			break
	assert_false(q.is_bullet_status_enabled(0), "bullet 0 expired")
	await idle(1)
	assert_signal_emit_count(factory, "life_time_over", 1, "life_time_over emitted once (live, inside the tick)")
	assert_eq(factory.debug_get_bullets_pool_amount(), 1, "expired volley pooled after the signal")


func test_infinite_never_expires() -> void:
	var inf := H.make_volley_data(2, 300.0, 5.0)
	inf.is_life_time_infinite = true
	var w: BulletVolley2D = factory.spawn_volley(inf)
	await physics(10)
	assert_true(w.is_bullet_status_enabled(0), "infinite volley still alive")
	assert_gt(w.get_curves_elapsed_time(), 0.0, "curve clock advances while infinite")


func test_invalid_lifetimes_rejected_at_setter() -> void:
	var data := H.make_volley_data(2, 100.0, 5.0)
	data.max_life_time = 0.0
	expect_error_sequence(["BulletVolleyData2D max_life_time must be > 0 when lifetime is not infinite (or enable is_life_time_infinite). Keeping previous value."])
	assert_eq(data.max_life_time, 5.0, "zero lifetime rejected, old kept")
	data.max_life_time = NAN
	expect_error_sequence(["BulletVolleyData2D max_life_time must be finite, keeping previous value."])
	assert_eq(data.max_life_time, 5.0, "NaN lifetime rejected, old kept")
	factory.spawn_volley(data)
	assert_eq(factory.debug_get_total_bullets_amount(), 1, "spawn uses the kept valid lifetime")


func test_max_collisions_zero_is_infinite() -> void:
	var t: BulletVolley2D = quick_volley(1, 0.0, 30.0)
	t.set_bullet_max_collision_count(0)
	assert_eq(t.get_bullet_max_collision_count(), 0, "max 0 stored (infinite)")
	t.set_bullet_collision_count(0, 5)
	assert_eq(t.get_bullet_collision_count(0), 5, "count tracked even when infinite")
	t.set_bullet_max_collision_count(-1)
	expect_error_sequence(["set_bullet_max_collision_count: value must be >= 0 (0 = infinite collisions). Keeping previous value."])
	assert_eq(t.get_bullet_max_collision_count(), 0, "negative max rejected")


func test_curve_clock_rejects_nan() -> void:
	var c: BulletVolley2D = quick_volley(1, 100.0, 5.0)
	var before: float = c.get_curves_elapsed_time()
	c.set_curves_elapsed_time(NAN)
	expect_error_sequence(["set_curves_elapsed_time: new_time must be a finite value >= 0."])
	assert_eq(c.get_curves_elapsed_time(), before, "NaN curve time rejected")


func test_live_infinite_toggle() -> void:
	var lv: BulletVolley2D = quick_volley(1, 100.0, 5.0)
	lv.set_is_life_time_infinite(true)
	assert_true(lv.get_is_life_time_infinite(), "toggle to infinite")
	lv.set_is_life_time_infinite(false)
	assert_false(lv.get_is_life_time_infinite(), "toggle back (max_life_time 5 s > 0)")
	assert_true(lv.get_bullet_transform(0).is_finite())


func test_counts_readable_then_expiry_pools() -> void:
	var ev: BulletVolley2D = quick_volley(1, 0.0, 0.2)
	ev.set_bullet_max_collision_count(4)
	ev.set_bullet_collision_count(0, 2)
	assert_eq(ev.get_bullets_current_collision_count()[0], 2, "count readable pre-expiry")
	await physics(30)
	await idle(1)
	assert_eq(factory.debug_get_bullets_pool_amount(), 1, "short volley pooled after expiry")
