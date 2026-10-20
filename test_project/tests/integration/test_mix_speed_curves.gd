extends BlastTest
## Speed curves mixed with pooling, parking, time scale, lifetime extensions,
## orbits, bounces and splitting. A volley's curves run on its own clock
## (bullet time since its life began): a pool reuse and an expired parked
## volley's wake start it over (a manual drain's wake keeps it), the time
## scale scales it, a unit curve holds its last value past the lifetime it
## was drawn for, an orbiting bullet circles at the curve's speed, a
## bounce's strength carries across the curve's overwrites, and split
## children start their own clock.

const DT := 1.0 / 60.0


## Non-unit linear ramp: 100 px/s at t = 0 to 400 px/s at t = 1 s, then
## held (linear tangents: a plain add_point eases in and out).
func _ramp() -> BulletCurvesData2D:
	var c := Curve.new()
	c.min_value = 0.0
	c.max_value = 1000.0
	c.add_point(Vector2(0, 100), 0.0, 0.0, Curve.TANGENT_LINEAR, Curve.TANGENT_LINEAR)
	c.add_point(Vector2(1, 400), 0.0, 0.0, Curve.TANGENT_LINEAR, Curve.TANGENT_LINEAR)
	var curves := BulletCurvesData2D.new()
	curves.movement_speed_curve = c
	curves.movement_use_unit_curve = false
	return curves


func _data(n := 2, lifetime := 3.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 100.0, lifetime)
	d.shared_bullet_curves_data = _ramp()
	return d


## The ramp at `age` seconds of bullet time.
static func _ramp_at(age: float) -> float:
	return 100.0 + 300.0 * clampf(age, 0.0, 1.0)


## A tick samples the curve at the clock it starts from (the clock then
## advances): after a tick ending at clock `c` the speed is the ramp at
## c - the tick's delta.
func _speed(v: BulletVolley2D, i := 0) -> float:
	return v.get_bullet_velocity(i).length()


func _clock(v: BulletVolley2D) -> float:
	return float(v.debug_get_clocks()["curves_elapsed_time"])


func test_a_pool_reuse_starts_the_curve_over() -> void:
	var d := _data()
	var v: BulletVolley2D = factory.spawn_volley(d)
	step_factory(3)
	var cold := [_speed(v), _clock(v)]
	step_factory(30)
	v.clear_all_bullets()
	var again: BulletVolley2D = factory.spawn_volley(d)
	assert_eq(again, v, "reused")
	assert_eq(_clock(again), 0.0, "the clock starts over")
	step_factory(3)
	assert_almost_eq(_speed(again), float(cold[0]), 0.01, "the same speed as the cold life at the same age")
	assert_almost_eq(_clock(again), float(cold[1]), 1e-9, "the same clock")


func test_an_expired_park_restarts_the_curve_and_a_manual_drain_keeps_it(expired: bool = use_parameters([true, false])) -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(2, 0.5))
	v.set_is_auto_pooling_enabled(false)
	step_factory(12) # 0.2 s
	if expired:
		step_factory(30) # past 0.5 s: expired, parked
	else:
		v.disable_bullet(0)
		v.disable_bullet(1) # drained by hand at 0.2 s
	assert_true(v.is_parked(), "parked")
	var parked_clock := _clock(v)
	v.enable_bullet(0)
	if expired:
		assert_eq(_clock(v), 0.0, "an expired volley wakes with a fresh clock")
	else:
		assert_almost_eq(_clock(v), parked_clock, 1e-9, "a manual drain keeps its clock")
	var from := _clock(v)
	step_factory(1)
	assert_almost_eq(_speed(v), _ramp_at(from), 0.5, "the curve speed of that age (%s)" % ("fresh" if expired else "kept"))


func test_the_time_scale_scales_the_curve_clock(scale: float = use_parameters([0.25, 2.0])) -> void:
	factory.time_scale = scale
	var v: BulletVolley2D = factory.spawn_volley(_data())
	step_factory(12)
	assert_almost_eq(_clock(v), 12 * DT * scale, 1e-6, "scale %s: bullet time" % scale)
	assert_almost_eq(_speed(v), _ramp_at(11 * DT * scale), 0.5, "scale %s: the curve read at bullet time" % scale)


func test_a_unit_curve_holds_its_end_past_an_extended_lifetime() -> void:
	var d := _data(1, 0.5)
	d.shared_bullet_curves_data.movement_use_unit_curve = true # x = age / max_life_time
	d.is_life_time_over_signal_enabled = true
	var v: BulletVolley2D = factory.spawn_volley(d)
	var extended := [false]
	factory.life_time_over.connect(func(vv, _idx) -> void:
		if not extended[0]:
			extended[0] = true
			vv.set_life_time_left(0.5))
	step_factory(45) # 0.75 s: past the drawn 0.5 s
	assert_true(extended[0], "extended")
	assert_true(v.is_bullet_status_enabled(0), "still flying")
	assert_almost_eq(_speed(v), 400.0, 0.5, "the curve's last value, held")
	assert_true(v.get_bullet_transform(0).is_finite(), "finite")


func test_an_orbit_circles_at_the_curve_speed() -> void:
	var target := make_graze_target(Vector2(150, 0), &"curve_orbit")
	var d := _data(1, 5.0)
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.helper_ring_radius = 10.0
	sp.homing_node_group = &"curve_orbit"
	sp.homing_smoothing = 8.0
	sp.homing_take_control_of_texture_rotation = true
	sp.homing_retarget_interval_sec = 1000.0
	sp.set_homing_enabled(true)
	sp.orbiting_radius = 40.0
	sp.orbiting_enabled = true
	var got: Array = []
	sp.volley_fired.connect(func(v, _n) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fires")
	var v: BulletVolley2D = got[0]
	var locked_at := -1
	for f in 120:
		await physics(1)
		if v.bullet_is_orbiting_locked(0):
			locked_at = f
			break
	assert_gt(locked_at, -1, "locked")
	var problems: Array = []
	for f in 20:
		var before: Vector2 = v.get_bullet_transform(0).origin
		var age := _clock(v)
		await physics(1)
		var step: float = v.get_bullet_transform(0).origin.distance_to(before)
		var want := _ramp_at(age - DT) * DT # the speed the previous tick sampled
		if absf(step - want) > want * 0.05:
			problems.append("tick %d: %.2f px, the curve says %.2f" % [f, step, want])
		assert_almost_eq(v.get_bullet_transform(0).origin.distance_to(target.global_position), 40.0, 0.05, "on the ring")
	assert_eq("\n".join(problems), "", "circling at the curve speed")


func test_a_bounce_strength_carries_across_the_curve() -> void:
	make_wall(Vector2(120, 0), Vector2(20, 400), 8)
	var d := _data(1, 5.0)
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	d.collision_shape = H.make_circle_shape(4.0)
	d.monitorable = true
	d.set_collision_mask_from_array([4])
	d.set_bounce_mask_from_array([4])
	d.bounce_mode = 1
	d.bounce_strength = 0.5
	d.bullet_max_collision_count = 0
	var v: BulletVolley2D = factory.spawn_volley(d)
	for f in 120:
		await physics(1)
		if v.bullet_get_bounce_count(0) > 0:
			break
	assert_eq(v.bullet_get_bounce_count(0), 1, "bounced")
	for f in 3:
		await physics(1)
	var age := _clock(v)
	assert_almost_eq(_speed(v), 0.5 * _ramp_at(age - DT), 0.5, "half the curve speed after a 0.5 bounce")
	assert_lt(v.get_bullet_velocity(0).x, 0.0, "flying back")


func test_split_children_start_their_own_curve_clock() -> void:
	var d := _data(1, 5.0)
	var stage := BulletSplitStage2D.new()
	stage.count = 2
	stage.split_after_sec = 0.5
	stage.parent_survives = true
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.split_set_data(chain, _data(2, 5.0))
	var children: Array = []
	factory.bullet_split.connect(func(_p, _c, ch, _d) -> void: children.append([ch, _clock(ch)]))
	for f in 40:
		await physics(1)
	assert_eq(children.size(), 1, "split once")
	assert_eq(float(children[0][1]), 0.0, "the children's clock starts at 0")
	assert_gt(_clock(v), 0.6, "the parent's runs on")
