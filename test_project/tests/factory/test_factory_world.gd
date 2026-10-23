extends BlastTest
## The bullet world controls of the factory: area queries and clears (volley
## and factory level, `bullets_cleared`), despawn bounds, and time_scale.

var cleared_log: Array = []


func _on_bullets_cleared(positions: PackedVector2Array, volley: BulletVolley2D, indexes: Array) -> void:
	cleared_log.append({ "positions": positions, "volley": volley, "indexes": indexes })


func before_each() -> void:
	await super()
	cleared_log.clear()


## A still volley with one bullet per position (speed 0, long life).
func _volley_at(points: Array, speed: float = 0.0) -> BulletVolley2D:
	var data := H.make_volley_data(points.size(), speed, 60.0)
	var arr: Array = []
	for p in points:
		arr.append(Transform2D(0.0, p))
	data.transforms = arr
	return factory.spawn_volley(data)


func _alive(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		if v.is_bullet_status_enabled(i):
			out.append(i)
	return out


# --- area queries ------------------------------------------------------------

func test_bound_surface() -> void:
	for m in ["get_bullet_indexes_in_circle", "get_bullet_indexes_in_rect", "clear_bullets_in_circle", "clear_bullets_in_rect"]:
		assert_true(ClassDB.class_has_method("BulletVolley2D", m), "volley has " + m)
	for m in ["clear_bullets_in_circle", "clear_bullets_in_rect", "count_bullets_in_circle", "count_bullets_in_rect", "get_time_scale", "set_time_scale", "get_despawn_bounds_enabled", "set_despawn_bounds_enabled", "get_despawn_bounds_rect", "set_despawn_bounds_rect"]:
		assert_true(factory.has_method(m), "factory has " + m)
	assert_true(factory.has_signal("bullets_cleared"), "bullets_cleared signal")
	for p in ["time_scale", "despawn_bounds_enabled", "despawn_bounds_rect"]:
		assert_true(p in factory, "factory property " + p)


func test_circle_query_is_exact_and_edge_inclusive() -> void:
	var v := _volley_at([Vector2(0, 0), Vector2(10, 0), Vector2(0, 10.5), Vector2(30, 40), Vector2(100, 100)])
	assert_eq(Array(v.get_bullet_indexes_in_circle(Vector2.ZERO, 10.0)), [0, 1], "r=10: origin and the bullet exactly on the edge")
	assert_eq(Array(v.get_bullet_indexes_in_circle(Vector2.ZERO, 50.0)), [0, 1, 2, 3], "r=50 includes the 3-4-5 point on the edge")
	assert_eq(Array(v.get_bullet_indexes_in_circle(Vector2.ZERO, 0.0)), [0], "radius 0 finds only a bullet exactly there")
	assert_eq(Array(v.get_bullet_indexes_in_circle(Vector2(500, 500), 5.0)), [], "far away finds nothing")


func test_rect_query_is_edge_inclusive_and_accepts_negative_sizes() -> void:
	var v := _volley_at([Vector2(0, 0), Vector2(10, 10), Vector2(10.5, 5), Vector2(-5, 5)])
	assert_eq(Array(v.get_bullet_indexes_in_rect(Rect2(0, 0, 10, 10))), [0, 1], "all four edges inclusive")
	assert_eq(Array(v.get_bullet_indexes_in_rect(Rect2(10, 10, -10, -10))), [0, 1], "a negative size is the same rect")


func test_queries_skip_cleared_bullets() -> void:
	var v := _volley_at([Vector2(0, 0), Vector2(1, 0), Vector2(2, 0)])
	v.disable_bullet(1)
	assert_eq(Array(v.get_bullet_indexes_in_circle(Vector2.ZERO, 5.0)), [0, 2], "a frozen bullet is not listed")


func test_hostile_query_input_fails_loud_and_returns_empty() -> void:
	var v := _volley_at([Vector2(0, 0)])
	assert_eq(Array(v.get_bullet_indexes_in_circle(Vector2(NAN, 0), 5.0)), [], "NaN center")
	expect_error_sequence(["get_bullet_indexes_in_circle: center must be finite and radius a finite number >= 0."])
	assert_eq(Array(v.get_bullet_indexes_in_circle(Vector2.ZERO, -1.0)), [], "negative radius")
	expect_error_sequence(["get_bullet_indexes_in_circle: center must be finite and radius a finite number >= 0."])
	assert_eq(Array(v.get_bullet_indexes_in_circle(Vector2.ZERO, INF)), [], "infinite radius")
	expect_error_sequence(["get_bullet_indexes_in_circle: center must be finite and radius a finite number >= 0."])
	assert_eq(Array(v.get_bullet_indexes_in_rect(Rect2(0, 0, NAN, 1))), [], "NaN rect")
	expect_error_sequence(["get_bullet_indexes_in_rect: rect must be finite."])
	assert_eq(v.clear_bullets_in_circle(Vector2.ZERO, -1.0), 0, "clear with a bad circle clears nothing")
	expect_error_sequence(["clear_bullets_in_circle: center must be finite and radius a finite number >= 0."])
	assert_eq(v.clear_bullets_in_rect(Rect2(INF, 0, 1, 1)), 0, "clear with a bad rect clears nothing")
	expect_error_sequence(["clear_bullets_in_rect: rect must be finite."])
	assert_eq(_alive(v), [0], "the bullet survived every refused call")


func test_factory_queries_list_what_the_counts_count() -> void:
	for m in ["get_bullets_in_circle", "get_bullets_in_rect", "get_active_volleys"]:
		assert_true(factory.has_method(m), "factory has " + m)
	var a := _volley_at([Vector2(0, 0), Vector2(100, 0), Vector2(20, 0)])
	var b := _volley_at([Vector2(5, 5), Vector2(200, 0), Vector2(6, 6)])
	var c := _volley_at([Vector2(1, 1)])
	b.disable_bullet(2) # a frozen bullet is not listed
	var parked := _volley_at([Vector2(2, 2)])
	parked.set_is_auto_pooling_enabled(false)
	parked.disable_bullet(0)
	var pooled := _volley_at([Vector2(3, 3)])
	pooled.clear_all_bullets()
	await idle(1)
	assert_true(parked.is_parked() and pooled.is_pooled(), "one parked, one pooled")
	assert_eq(factory.get_active_volleys(), [a, b, c], "the active volleys in factory order")
	var circle: Array = factory.get_bullets_in_circle(Vector2.ZERO, 20.0)
	assert_eq(circle.size(), 3, "one entry per volley with bullets inside")
	assert_eq(circle[0]["volley"], a, "factory order")
	assert_eq(Array(circle[0]["bullet_indexes"]), [0, 2], "a: the edge counts")
	assert_eq(Array(circle[0]["positions"]), [Vector2(0, 0), Vector2(20, 0)], "a: parallel positions")
	assert_eq(Array(circle[1]["bullet_indexes"]), [0], "b: the frozen one is left out")
	assert_eq(circle[2]["volley"], c, "c")
	var listed := 0
	for e in circle:
		listed += e["bullet_indexes"].size()
	assert_eq(listed, factory.count_bullets_in_circle(Vector2.ZERO, 20.0), "the query lists what the count counts")
	var rect: Array = factory.get_bullets_in_rect(Rect2(10, 10, -20, -20))
	assert_eq(rect.size(), 3, "a negative rect is normalized")
	assert_eq(Array(rect[1]["positions"]), [Vector2(5, 5)], "b in the rect")
	c.queue_free()
	assert_eq(factory.get_active_volleys(), [a, b], "a volley queued for deletion is left out")
	assert_eq(factory.get_bullets_in_circle(Vector2.ZERO, 20.0).size(), 2, "by the query too")
	assert_eq(factory.get_bullets_in_circle(Vector2(NAN, 0), 1.0), [], "a bad circle")
	expect_error_sequence(["get_bullets_in_circle: center must be finite and radius a finite number >= 0."])
	assert_eq(factory.get_bullets_in_rect(Rect2(0, 0, INF, 1)), [], "a bad rect")
	expect_error_sequence(["get_bullets_in_rect: rect must be finite."])


# --- area clears -------------------------------------------------------------

func test_volley_clear_in_circle_and_rect() -> void:
	var v := _volley_at([Vector2(0, 0), Vector2(5, 0), Vector2(50, 0), Vector2(60, 0)])
	assert_eq(v.clear_bullets_in_circle(Vector2.ZERO, 10.0), 2, "two bullets in the circle")
	assert_eq(_alive(v), [2, 3], "only the far ones live")
	assert_eq(v.clear_bullets_in_rect(Rect2(40, -5, 30, 10)), 2, "the rest in the rect")
	assert_eq(v.clear_bullets_in_circle(Vector2.ZERO, 1000.0), 0, "nothing left to clear")


func test_factory_clear_spans_volleys_and_reports_positions() -> void:
	var a := _volley_at([Vector2(0, 0), Vector2(100, 0)])
	var b := _volley_at([Vector2(5, 5), Vector2(200, 0), Vector2(6, 6)])
	factory.bullets_cleared.connect(_on_bullets_cleared)
	assert_eq(factory.count_bullets_in_circle(Vector2.ZERO, 20.0), 3, "count sees three")
	assert_eq(factory.count_bullets_in_rect(Rect2(-10, -10, 30, 30)), 3, "rect count agrees")
	assert_eq(factory.clear_bullets_in_circle(Vector2.ZERO, 20.0), 3, "three cleared across two volleys")
	assert_eq(cleared_log.size(), 2, "one bullets_cleared per volley")
	var by_volley := {}
	for e in cleared_log:
		by_volley[e["volley"]] = e
	assert_eq(Array(by_volley[a]["indexes"]), [0], "volley a: index 0")
	assert_eq(Array(by_volley[a]["positions"]), [Vector2(0, 0)], "volley a: position")
	assert_eq(Array(by_volley[b]["indexes"]), [0, 2], "volley b: indexes")
	assert_eq(Array(by_volley[b]["positions"]), [Vector2(5, 5), Vector2(6, 6)], "volley b: positions parallel to indexes")
	assert_eq(factory.count_bullets_in_circle(Vector2.ZERO, 20.0), 0, "nothing left inside")
	assert_eq(factory.get_active_bullet_count(), 2, "the two outside bullets live on")


func test_bullets_cleared_handler_can_spawn_pickups() -> void:
	# The signal fires after the busy latch is released: a handler turning
	# cleared bullets into score items must be able to spawn.
	_volley_at([Vector2(0, 0), Vector2(1, 0)])
	var spawned: Array = []
	factory.bullets_cleared.connect(func(positions: PackedVector2Array, _v: BulletVolley2D, _i: Array) -> void:
		for p in positions:
			spawned.append(_volley_at([p])))
	assert_eq(factory.clear_bullets_in_rect(Rect2(-5, -5, 10, 10)), 2, "both cleared")
	assert_eq(spawned.size(), 2, "one pickup spawned per cleared bullet")
	for s in spawned:
		assert_not_null(s, "the spawn inside the handler succeeded")
	assert_eq(factory.get_active_bullet_count(), 2, "only the pickups live")


func test_factory_area_calls_refuse_bad_input() -> void:
	_volley_at([Vector2(0, 0)])
	assert_eq(factory.clear_bullets_in_circle(Vector2(NAN, 0), 1.0), 0, "bad center")
	expect_error_sequence(["clear_bullets_in_circle: center must be finite and radius a finite number >= 0."])
	assert_eq(factory.clear_bullets_in_rect(Rect2(0, 0, 1, NAN)), 0, "bad rect")
	expect_error_sequence(["clear_bullets_in_rect: rect must be finite."])
	assert_eq(factory.count_bullets_in_circle(Vector2.ZERO, -2.0), 0, "bad count radius")
	expect_error_sequence(["count_bullets_in_circle: center must be finite and radius a finite number >= 0."])
	assert_eq(factory.count_bullets_in_rect(Rect2(NAN, 0, 1, 1)), 0, "bad count rect")
	expect_error_sequence(["count_bullets_in_rect: rect must be finite."])
	assert_eq(factory.get_active_bullet_count(), 1, "nothing was cleared")


# --- despawn bounds ----------------------------------------------------------

func test_despawn_bounds_cull_outbound_and_spare_inbound_bullets() -> void:
	factory.despawn_bounds_rect = Rect2(0, 0, 100, 100)
	factory.despawn_bounds_enabled = true
	var v := _volley_at([Vector2(50, 50), Vector2(150, 50), Vector2(-50, 50), Vector2(50, -40), Vector2(-50, 150)])
	v.bullet_set_velocity(0, Vector2(0, 0)) # inside, still
	v.bullet_set_velocity(1, Vector2(100, 0)) # outside right, flying away
	v.bullet_set_velocity(2, Vector2(100, 0)) # outside left, flying in
	v.bullet_set_velocity(3, Vector2(0, 100)) # outside above, flying in
	v.bullet_set_velocity(4, Vector2(100, 0)) # outside left AND below: in on x, away on y
	watch_signals(factory)
	await idle(1)
	assert_true(factory.debug_advance_time(0.1), "one tick")
	assert_eq(_alive(v), [0, 2, 3], "away-flying (1) and not-fully-returning (4) are gone; inbound (2, 3) and inside (0) live")
	assert_signal_not_emitted(factory, "life_time_over", "a despawn is silent")


func test_despawn_bounds_cull_a_bullet_that_flies_out() -> void:
	factory.despawn_bounds_rect = Rect2(0, 0, 100, 100)
	factory.despawn_bounds_enabled = true
	var v := _volley_at([Vector2(90, 50)])
	v.bullet_set_velocity(0, Vector2(200, 0))
	await idle(1)
	factory.debug_advance_time(0.01) # x = 92: still inside
	assert_eq(_alive(v), [0], "inside the rect it lives")
	for i in 5:
		factory.debug_advance_time(0.1)
	assert_eq(_alive(v), [], "past the edge, moving away: gone")
	assert_eq(factory.get_active_bullet_count(), 0, "the volley drained")


func test_despawn_bounds_off_by_default_and_togglable() -> void:
	assert_false(factory.despawn_bounds_enabled, "off by default")
	assert_eq(factory.despawn_bounds_rect, Rect2(0, 0, 1152, 648), "the default rect is Godot's default viewport")
	var v := _volley_at([Vector2(5000, 5000)])
	v.bullet_set_velocity(0, Vector2(100, 100))
	await idle(1)
	factory.debug_advance_time(0.1)
	assert_eq(_alive(v), [0], "disabled: far outside bullets live")
	factory.despawn_bounds_enabled = true
	factory.debug_advance_time(0.1)
	assert_eq(_alive(v), [], "enabled: it is culled on the next tick")


func test_despawn_bounds_setters_validate() -> void:
	factory.despawn_bounds_rect = Rect2(NAN, 0, 10, 10)
	expect_error_sequence(["BulletFactory2D: despawn_bounds_rect must be finite, keeping the old value."])
	assert_eq(factory.despawn_bounds_rect, Rect2(0, 0, 1152, 648), "old rect kept")
	factory.despawn_bounds_rect = Rect2(100, 100, -100, -100)
	assert_eq(factory.despawn_bounds_rect, Rect2(0, 0, 100, 100), "a negative size is normalized")


# --- time scale --------------------------------------------------------------

func test_time_scale_scales_motion_and_lifetime() -> void:
	var v := _volley_at([Vector2.ZERO], 0.0)
	v.bullet_set_velocity(0, Vector2(100, 0))
	await idle(1) # one real-time tick has already run
	var x0: float = v.get_bullet_transform(0).origin.x
	var life0: float = v.get_life_time_left()
	factory.time_scale = 0.5
	factory.debug_advance_time(1.0)
	assert_almost_eq(v.get_bullet_transform(0).origin.x - x0, 50.0, 0.01, "half speed: 50 px in a 1 s tick")
	assert_almost_eq(life0 - v.get_life_time_left(), 0.5, 0.01, "lifetime runs at the same scale")
	factory.time_scale = 0.0
	factory.debug_advance_time(1.0)
	assert_almost_eq(v.get_bullet_transform(0).origin.x - x0, 50.0, 0.01, "scale 0 freezes the bullet")
	factory.time_scale = 2.0
	factory.debug_advance_time(1.0)
	assert_almost_eq(v.get_bullet_transform(0).origin.x - x0, 250.0, 0.01, "double speed: +200 px")


## Any finite time_scale is accepted, but one volley tick never covers more
## than 10 s of bullet time: 1e300 would turn every pose into INF (and hand
## INF shapes to the physics server). The lifetime runs on the same clamped
## clock. Scales whose ticks stay below the cap are untouched (above: x2).
func test_a_huge_time_scale_keeps_bullets_finite() -> void:
	var v := _volley_at([Vector2.ZERO], 0.0)
	v.bullet_set_velocity(0, Vector2(100, 0))
	await idle(1)
	var x0: float = v.get_bullet_transform(0).origin.x
	factory.time_scale = 1e300
	factory.debug_advance_time(1.0 / 60.0)
	var p: Vector2 = v.get_bullet_transform(0).origin
	assert_true(p.is_finite(), "the bullet stays finite")
	assert_almost_eq(p.x - x0, 1000.0, 0.01, "one tick covers at most 10 s of bullet time (100 px/s)")
	expect_no_errors("no engine error from a huge scale")


func test_time_scale_default_and_validation() -> void:
	assert_eq(factory.time_scale, 1.0, "default is real time")
	factory.time_scale = -1.0
	expect_error_sequence(["BulletFactory2D: time_scale must be a finite number >= 0, keeping the old value."])
	factory.time_scale = NAN
	expect_error_sequence(["BulletFactory2D: time_scale must be a finite number >= 0, keeping the old value."])
	factory.time_scale = INF
	expect_error_sequence(["BulletFactory2D: time_scale must be a finite number >= 0, keeping the old value."])
	assert_eq(factory.time_scale, 1.0, "rejected values keep the old scale")
	factory.time_scale = 0.25
	assert_eq(factory.time_scale, 0.25, "valid value accepted")


# --- clear_active_bullets also announces itself -------------------------------------------------

func test_clear_active_bullets_emits_bullets_cleared_per_volley() -> void:
	var a := _volley_at([Vector2(0, 0), Vector2(10, 0)])
	var b := _volley_at([Vector2(50, 50)])
	factory.bullets_cleared.connect(_on_bullets_cleared)
	assert_eq(factory.clear_active_bullets(), 3, "three bullets cleared")
	assert_eq(cleared_log.size(), 2, "one signal per volley that lost bullets")
	var by_volley := {}
	for e in cleared_log:
		by_volley[e["volley"]] = e
	assert_eq(Array(by_volley[a]["positions"]), [Vector2(0, 0), Vector2(10, 0)], "positions of volley a")
	assert_eq(Array(by_volley[a]["indexes"]), [0, 1], "indexes of volley a")
	assert_eq(Array(by_volley[b]["positions"]), [Vector2(50, 50)], "positions of volley b")


func test_clear_active_bullets_with_a_key_announces_only_that_bucket() -> void:
	_volley_at([Vector2(0, 0)])
	_volley_at([Vector2(1, 0), Vector2(2, 0)])
	factory.bullets_cleared.connect(_on_bullets_cleared)
	var key: VolleyPoolKey2D = factory.debug_expected_pool_key(H.make_volley_data(2, 0.0, 60.0))
	assert_eq(factory.clear_active_bullets(key), 2, "only the two-bullet volley")
	assert_eq(cleared_log.size(), 1, "one signal")
	assert_eq(cleared_log[0]["positions"].size(), 2, "for that volley")


func test_nothing_to_clear_announces_nothing() -> void:
	factory.bullets_cleared.connect(_on_bullets_cleared)
	assert_eq(factory.clear_active_bullets(), 0, "empty factory")
	assert_eq(cleared_log.size(), 0, "no signal")


func test_a_cleared_handler_of_clear_active_bullets_may_spawn_pickups() -> void:
	_volley_at([Vector2(0, 0), Vector2(5, 5)])
	var made := []
	factory.bullets_cleared.connect(func(positions, _v, _i):
		for p in positions:
			made.append(_volley_at([p])))
	factory.clear_active_bullets()
	assert_eq(made.size(), 2, "the busy latch is down when the signal fires")
	assert_eq(factory.get_active_bullet_count(), 2, "only the spawned replacements live")


## A volley queued for deletion is gone for every area query alike: the
## counts agree with get_bullets_in_circle / rect and the clears.
func test_area_counts_skip_volleys_queued_for_deletion() -> void:
	var d := H.make_still_data(2)
	d.transforms = [Transform2D(0.0, Vector2(10, 10)), Transform2D(0.0, Vector2(20, 10))]
	var keep: BulletVolley2D = factory.spawn_volley(d)
	var gone: BulletVolley2D = factory.spawn_volley(d)
	gone.queue_free()
	assert_eq(factory.get_bullets_in_circle(Vector2(15, 10), 50.0).size(), 1, "one volley listed")
	assert_eq(factory.count_bullets_in_circle(Vector2(15, 10), 50.0), 2, "its two bullets counted (circle)")
	assert_eq(factory.count_bullets_in_rect(Rect2(0, 0, 40, 40)), 2, "its two bullets counted (rect)")
	assert_not_null(keep, "kept")
