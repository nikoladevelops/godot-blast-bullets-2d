extends BlastTest
## Lasers in the world controls: end_lasers_in_circle / end_lasers_in_rect end
## every laser whose live body (half its current width included) touches the
## area, edges included, negative rects normalized, fading or at once; spent
## beams and finished lasers are left alone; bad areas are refused with the
## bullet clears' wording; no user code runs, so handlers may call them. The
## factory's time_scale and a pause hold lasers like bullets.

const DT := 0.0625


func _fire(pose: Transform2D, data: LaserData2D = null) -> LaserVolley2D:
	var t: Array[Transform2D] = [pose]
	return factory.spawn_laser(data if data != null else H.make_laser(0.0, 0.0, 0.0, 0.25, 200.0), t)


func test_a_circle_ends_the_lasers_whose_body_it_touches() -> void:
	var near := _fire(Transform2D(0.0, Vector2(0, 0))) # body y in [-6, 6]
	var far := _fire(Transform2D(0.0, Vector2(0, 100)))
	step_factory(1, DT)
	assert_eq(factory.end_lasers_in_circle(Vector2(100, 20), 14.0), 1, "14 px reaches the edge 6 px off the beam (20 - 6), inclusive")
	step_factory(1, DT)
	assert_eq([near.get_phase(), far.get_phase()], [LaserVolley2D.PHASE_FADING, LaserVolley2D.PHASE_ACTIVE], "only the one it touched fades")
	assert_eq(factory.end_lasers_in_circle(Vector2(100, 100), 1.0, false), 1, "at once")
	step_factory(1, DT)
	assert_true(far.is_pooled(), "ended without a fade")


func test_a_rect_ends_by_edges_and_normalizes_negative_sizes() -> void:
	var l := _fire(Transform2D(0.0, Vector2(0, 0)))
	step_factory(1, DT)
	assert_eq(factory.end_lasers_in_rect(Rect2(300, -50, 50, 100)), 0, "past the 200 px end")
	assert_eq(factory.end_lasers_in_rect(Rect2(200, 6, 50, -100)), 1, "a negative rect whose corner sits on the end and the body's edge")
	step_factory(1, DT)
	assert_eq(l.get_phase(), LaserVolley2D.PHASE_FADING, "fading")


func test_charging_lasers_are_ended_and_finished_or_spent_ones_are_not() -> void:
	var charging := _fire(Transform2D(0.0, Vector2.ZERO), H.make_laser(1.0, 0.0, 0.0, 0.0, 200.0))
	step_factory(1, DT)
	assert_eq(factory.end_lasers_in_circle(Vector2(100, 0), 5.0, false), 1, "a warning line is a laser too")
	step_factory(1, DT)
	assert_true(charging.is_pooled(), "gone before it fired")
	var bolt_data := H.make_laser(0.0, 0.0, 0.0, 0.0, 40.0)
	bolt_data.type = LaserData2D.LASER_BOLT
	bolt_data.speed = 1600.0
	factory.despawn_bounds_rect = Rect2(-50, -50, 300, 100)
	factory.despawn_bounds_enabled = true
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO), Transform2D(PI / 2.0, Vector2.ZERO)]
	var bolts := factory.spawn_laser(bolt_data, poses)
	step_factory(2, DT)
	var state: Array = bolts.debug_get_draw_state(1.0)
	assert_eq(state[1]["spent"], true, "the downward bolt is spent (out of the bounds)")
	assert_eq(factory.end_lasers_in_circle(Vector2(0, 200), 30.0), 0, "where only the spent bolt went: nothing ends")


func test_bad_areas_are_refused_like_the_bullet_clears() -> void:
	_fire(Transform2D(0.0, Vector2.ZERO))
	step_factory(1, DT)
	assert_eq(factory.end_lasers_in_circle(Vector2(NAN, 0), 10.0), 0, "refused")
	expect_error_sequence(["end_lasers_in_circle: center must be finite and radius a finite number >= 0."])
	assert_eq(factory.end_lasers_in_circle(Vector2.ZERO, -1.0), 0, "refused")
	expect_error_sequence(["end_lasers_in_circle: center must be finite and radius a finite number >= 0."])
	assert_eq(factory.end_lasers_in_rect(Rect2(0, 0, INF, 1)), 0, "refused")
	expect_error_sequence(["end_lasers_in_rect: rect must be finite."])
	assert_eq(factory.get_active_laser_count(), 1, "nothing ended")


func test_a_handler_may_end_lasers_by_area() -> void:
	make_area(Vector2(100, 0), Vector2(20, 60))
	await idle(2)
	var l := _fire(Transform2D(0.0, Vector2.ZERO))
	var other := _fire(Transform2D(0.0, Vector2(0, 300)))
	factory.laser_hit.connect(func(_t, _l, _b, _p) -> void: factory.end_lasers_in_circle(Vector2(50, 300), 10.0, false), CONNECT_ONE_SHOT)
	step_factory(2, DT)
	expect_no_errors()
	assert_true(other.is_pooled(), "the bomb inside the handler ended the other laser")
	assert_false(l.is_pooled(), "the hitting one fires on")


func test_time_scale_and_a_pause_hold_lasers() -> void:
	var d := H.make_laser(0.0, 0.0, 0.5, 0.0, 200.0)
	var l := _fire(Transform2D(0.0, Vector2.ZERO), d)
	step_factory(1, DT)
	factory.time_scale = 0.0
	step_factory(20, DT)
	assert_almost_eq(l.get_active_time_left(), 0.5 - DT, 1e-9, "time_scale 0 holds the clock")
	factory.time_scale = 1.0
	factory.set_is_factory_processing_bullets(false)
	await physics(10)
	await idle(1)
	assert_almost_eq(l.get_active_time_left(), 0.5 - DT, 1e-9, "a paused factory holds it")
	factory.set_is_factory_processing_bullets(true)
	step_factory(8, DT)
	assert_true(l.is_pooled(), "and it finishes on time once running")
