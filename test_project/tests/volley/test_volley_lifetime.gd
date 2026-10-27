extends BlastTest
## Lifetimes: a short finite lifetime expires, signals (deferred) and pools;
## infinite never expires while the curve clock advances; invalid lifetimes
## are rejected at the resource setter; max collisions 0 = infinite; the
## curve clock rejects NaN; the live infinite toggle; counts readable before
## an expiry. Per-bullet lifetimes (all_bullet_life_times + tile,
## set_bullet_life_time_left): a bullet with its own lifetime ignores the
## volley timer and expires alone (life_time_over names exactly the bullets
## expiring together), uncovered bullets follow the volley timer (or live
## forever when it is infinite), a handler that gives more time keeps a
## bullet, a wake after expiry restarts its own lifetime, nothing survives
## the pool.


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
	expect_error_sequence(["BulletVolleyData2D: max_life_time must be > 0 when the lifetime is not infinite (or enable is_life_time_infinite), keeping the old value."])
	assert_eq(data.max_life_time, 5.0, "zero lifetime rejected, old kept")
	data.max_life_time = NAN
	expect_error_sequence(["BulletVolleyData2D: max_life_time must be finite, keeping the old value."])
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


# --- per-bullet lifetimes ---------------------------------------------------------------

## Ticks of exactly 1/16 s: every lifetime below is a multiple, so the clocks
## land on their deadlines bit for bit (no float drift between the volley
## timer and the per-bullet deadlines).
const DT := 0.0625

## Every life_time_over payload (sorted bullet indexes) with the tick it fired on.
var expiries: Array = []
var tick := 0


func _on_expiry(_v: BulletVolley2D, idx: Array) -> void:
	var sorted := Array(idx)
	sorted.sort()
	expiries.append([tick, sorted])


func _step(n: int) -> void:
	for i in n:
		tick += 1
		step_factory(1, DT)


func _timed(n: int, times: Array, tile := false, max_life := 5.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 0.0, max_life)
	d.is_life_time_over_signal_enabled = true
	d.all_bullet_life_times = PackedFloat64Array(times)
	d.tile_all_bullet_life_times = tile
	return d


func _lefts(v: BulletVolley2D) -> Array:
	var out: Array = []
	for x in v.all_bullets_get_life_time_left():
		out.append(snappedf(x, 0.0001))
	return out


func test_per_bullet_life_times_are_bound_and_checked() -> void:
	var d := BulletVolleyData2D.new()
	assert_true("all_bullet_life_times" in d and "tile_all_bullet_life_times" in d, "data properties")
	assert_eq([d.all_bullet_life_times.size(), d.tile_all_bullet_life_times], [0, false], "off by default")
	for m in ["get_bullet_life_time_left", "set_bullet_life_time_left", "all_bullets_get_life_time_left", "all_bullets_set_life_time_left"]:
		assert_true(quick_volley(1).has_method(m), "BulletVolley2D.%s is bound" % m)
	d.all_bullet_life_times = PackedFloat64Array([0.5])
	for bad in [[1.0, 0.0], [-1.0], [NAN], [INF], [1e300]]:
		d.all_bullet_life_times = PackedFloat64Array(bad)
		expect_error_sequence(["BulletVolleyData2D: all_bullet_life_times must hold finite seconds > 0, keeping the old value."])
	assert_eq(Array(d.all_bullet_life_times), [0.5], "the data kept its lifetimes")


func test_each_bullet_expires_on_its_own_lifetime() -> void:
	tick = 0
	expiries.clear()
	factory.life_time_over.connect(_on_expiry)
	var v: BulletVolley2D = factory.spawn_volley(_timed(3, [0.125, 0.25, 0.375]))
	assert_eq(v.get_bullet_life_time_left(1), 0.25, "readout at spawn")
	_step(8)
	assert_eq(expiries, [[2, [0]], [4, [1]], [6, [2]]], "one expiry per bullet, at its own time")
	assert_true(v.is_pooled(), "the last one pooled the volley")


func test_uncovered_bullets_follow_the_volley_timer() -> void:
	tick = 0
	expiries.clear()
	factory.life_time_over.connect(_on_expiry)
	var v: BulletVolley2D = factory.spawn_volley(_timed(3, [0.5], false, 0.125))
	expect_warning_sequence(["BulletVolley2D: all_bullet_life_times size (1) != bullets (3); uncovered bullets use max_life_time (check tile_all_bullet_life_times to wrap, or provide one entry per bullet)."])
	assert_eq(v.get_bullet_life_time_left(2), 0.125, "uncovered: the volley's time left")
	_step(10)
	assert_eq(expiries, [[2, [1, 2]], [8, [0]]], "the volley timer takes only the uncovered bullets")


func test_tile_wraps_the_life_times() -> void:
	tick = 0
	expiries.clear()
	factory.life_time_over.connect(_on_expiry)
	factory.spawn_volley(_timed(4, [0.125, 0.25], true))
	expect_warning_sequence(["BulletVolley2D: all_bullet_life_times size (2) != bullets (4); uncovered bullets use max_life_time (tiling on: wrapping short array)."])
	_step(5)
	assert_eq(expiries, [[2, [0, 2]], [4, [1, 3]]], "entries wrap: bullets 0 and 2, then 1 and 3")


func test_an_infinite_volley_lets_only_its_timed_bullets_expire() -> void:
	tick = 0
	expiries.clear()
	factory.life_time_over.connect(_on_expiry)
	var d := _timed(2, [0.125])
	d.is_life_time_infinite = true
	var v: BulletVolley2D = factory.spawn_volley(d)
	expect_warning_sequence(["BulletVolley2D: all_bullet_life_times size (1) != bullets (2); uncovered bullets use max_life_time (check tile_all_bullet_life_times to wrap, or provide one entry per bullet)."])
	assert_eq(v.get_bullet_life_time_left(1), 0.0, "an endless bullet reads 0 (never), like the volley")
	_step(40)
	assert_eq(expiries, [[2, [0]]], "only the timed bullet expired")
	assert_true(v.is_bullet_status_enabled(1), "the endless one still flies")


func test_a_handler_that_gives_more_time_keeps_the_bullet() -> void:
	tick = 0
	expiries.clear()
	var v: BulletVolley2D = factory.spawn_volley(_timed(2, [0.125, 0.125]))
	factory.life_time_over.connect(func(vol: BulletVolley2D, idx: Array) -> void:
		_on_expiry(vol, idx)
		if tick == 2:
			vol.set_bullet_life_time_left(1, 0.125))
	_step(6)
	assert_eq(expiries, [[2, [0, 1]], [4, [1]]], "bullet 1 got 0.125 s more inside the handler")
	assert_true(v.is_pooled(), "then both are gone")


func test_a_volley_expiry_handler_can_give_a_bullet_its_own_lifetime() -> void:
	tick = 0
	expiries.clear()
	var v: BulletVolley2D = factory.spawn_volley(_timed(3, [], false, 0.125))
	factory.life_time_over.connect(func(vol: BulletVolley2D, idx: Array) -> void:
		_on_expiry(vol, idx)
		if tick == 2:
			vol.set_bullet_life_time_left(0, 0.0625))
	_step(5)
	assert_eq(expiries, [[2, [0, 1, 2]], [3, [0]]], "bullet 0 left the volley timer and expired one tick later")
	assert_true(v.is_pooled(), "pooled after the last one")


func test_set_bullet_life_time_left_rejects_and_ranges_apply() -> void:
	var v: BulletVolley2D = quick_volley(3, 0.0, 5.0)
	v.set_bullet_life_time_left(0, 0.0)
	expect_error_sequence(["set_bullet_life_time_left: seconds must be finite and > 0, keeping the old value."])
	v.set_bullet_life_time_left(0, NAN)
	expect_error_sequence(["set_bullet_life_time_left: seconds must be finite and > 0, keeping the old value."])
	v.set_bullet_life_time_left(3, 1.0)
	expect_error_sequence(["Invalid bullet index in set_bullet_life_time_left: 3 (amount_bullets: 3)"])
	v.all_bullets_set_life_time_left(-2.0)
	expect_error_sequence(["all_bullets_set_life_time_left: seconds must be finite and > 0, keeping the old values."])
	assert_eq(_lefts(v), [5.0, 5.0, 5.0], "every refusal kept the volley timer")
	v.all_bullets_set_life_time_left(0.5, 1, -1)
	assert_eq(_lefts(v), [5.0, 0.5, 0.5], "a range gives bullets 1 and 2 their own lifetime")
	step_factory(2, DT)
	assert_eq(_lefts(v), [4.875, 0.375, 0.375], "both clocks run down")


func test_a_wake_after_expiry_restarts_the_own_lifetime_and_a_freeze_keeps_it() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_timed(2, [0.125, 0.5]))
	v.set_is_auto_pooling_enabled(false)
	step_factory(1, DT)
	v.disable_bullet(1) # frozen with 0.4375 s left
	step_factory(2, DT) # bullet 0 expires at 0.125 s: the drained volley parks, its clock holds
	assert_false(v.is_bullet_status_enabled(0), "bullet 0 expired")
	await idle(1)
	assert_true(v.is_parked(), "drained with pooling off: parked")
	v.enable_bullet(0)
	assert_eq(v.get_bullet_life_time_left(0), 0.125, "woken after expiry: a fresh 0.125 s")
	v.enable_bullet(1)
	assert_eq(v.get_bullet_life_time_left(1), 0.375, "a freeze kept its deadline (the parked clock held)")
	step_factory(2, DT)
	assert_false(v.is_bullet_status_enabled(0), "the woken bullet expires again after its own lifetime")
	assert_true(v.is_bullet_status_enabled(1), "bullet 1 is still inside its time")


func test_nothing_of_the_own_lifetimes_survives_the_pool() -> void:
	var first: BulletVolley2D = factory.spawn_volley(_timed(2, [0.0625, 0.0625]))
	step_factory(2, DT)
	await idle(1)
	assert_true(first.is_pooled(), "expired into the pool")
	var again: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 0.0, 0.5))
	assert_eq(again, first, "the same volley, a new life")
	assert_eq(_lefts(again), [0.5, 0.5], "both bullets follow the new volley timer")
	assert_eq(again.debug_check_invariants(), "", "storage agrees")


## all_bullets_scale_life_time multiplies the time each bullet has left: a
## bullet on the volley timer takes its own lifetime from it (and dies on
## that), an own lifetime scales, an infinite volley's bullets stay infinite.
func test_scaling_life_time_moves_each_bullets_deadline() -> void:
	tick = 0
	expiries.clear()
	var v: BulletVolley2D = factory.spawn_volley(_timed(3, [0.5, 1.0], false, 1.0)) # bullet 2: on the volley's 1 s
	expect_warning_sequence(["BulletVolley2D: all_bullet_life_times size (2) != bullets (3); uncovered bullets use max_life_time (check tile_all_bullet_life_times to wrap, or provide one entry per bullet)."])
	factory.life_time_over.connect(_on_expiry)
	v.all_bullets_scale_life_time(0.0)
	v.all_bullets_scale_life_time(NAN)
	expect_error_sequence([
		"all_bullets_scale_life_time: factor must be finite and > 0, keeping the old values.",
		"all_bullets_scale_life_time: factor must be finite and > 0, keeping the old values.",
	])
	_step(2) # 0.125 s
	v.all_bullets_scale_life_time(2.0, 0, 0) # own 0.375 left -> 0.75
	v.all_bullets_scale_life_time(0.5, 2, 2) # the volley timer's 0.875 -> its own 0.4375
	assert_eq(_lefts(v), [0.75, 0.875, 0.4375], "scaled where asked, the rest untouched")
	_step(14)
	# Ticks of 1/16 s: bullet 2 at 0.125 + 0.4375, bullet 0 at 0.125 + 0.75,
	# bullet 1 on its own unscaled 1 s.
	assert_eq(expiries, [[9, [2]], [14, [0]], [16, [1]]], "each bullet at its (scaled) deadline")
	v.all_bullets_scale_life_time(2.0)
	expect_error_sequence(["all_bullets_scale_life_time: this volley is in the pool (its last bullet died), so this handle is stale. Spawn a new volley instead."])


func test_scaling_life_time_keeps_an_infinite_volley_infinite() -> void:
	tick = 0
	expiries.clear()
	var d := _timed(2, [0.5], false, 1.0) # bullet 1: on the (infinite) volley timer
	d.is_life_time_infinite = true
	var v: BulletVolley2D = factory.spawn_volley(d)
	expect_warning_sequence(["BulletVolley2D: all_bullet_life_times size (1) != bullets (2); uncovered bullets use max_life_time (check tile_all_bullet_life_times to wrap, or provide one entry per bullet)."])
	factory.life_time_over.connect(_on_expiry)
	_step(1)
	v.all_bullets_scale_life_time(4.0)
	assert_eq(_lefts(v), [1.75, 0.0], "the own lifetime scales (0.4375 x 4), the infinite one stays (0 = infinite)")
	_step(40)
	assert_eq(expiries, [[29, [0]]], "only the own lifetime ends")
	var huge: BulletVolley2D = factory.spawn_volley(_timed(2, [4.0, 4.0]))
	huge.all_bullets_scale_life_time(1e308) # 4 s x 1e308 is not a finite time
	expect_error_sequence(["all_bullets_scale_life_time: the scaled lifetime of bullet 0 (and any later one) is not a finite time, keeping its old value."])
	assert_eq(_lefts(huge), [4.0, 4.0], "both kept")
