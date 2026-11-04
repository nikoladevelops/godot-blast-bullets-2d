extends BlastTest
## Script readouts of a live laser and the factory's read-only laser queries:
## is_hitting follows the hitting phases, get_beam_contacts lists the targets
## a beam hits now (nearest first, freed ones left out), get_beam_target
## reads back set_beam_target, is_beam_spent / get_beam_pierced_total follow a
## bolt's pierce limit, touches_circle / touches_rect test the body (half its
## width included), sound_get_effects / sound_get_listener_group read back the
## sound arming, and BulletFactory2D.get_lasers_in_circle / _rect list the
## lasers end_lasers_in_* would end, ending none.

const DT := 0.0625


func _fire(d: LaserData2D, pose := Transform2D()) -> LaserVolley2D:
	var t: Array[Transform2D] = [pose]
	var l := factory.spawn_laser(d, t)
	assert_not_null(l, "fired")
	return l


func test_every_readout_is_bound() -> void:
	var l := _fire(H.make_laser())
	for m in ["is_hitting", "get_beam_contacts", "get_beam_target", "is_beam_spent", "get_beam_pierced_total", "touches_circle", "touches_rect", "sound_get_effects", "sound_get_listener_group", "is_beam_tip_hitting"]:
		assert_true(l.has_method(m), "LaserVolley2D.%s is bound" % m)
	for m in ["get_lasers_in_circle", "get_lasers_in_rect"]:
		assert_true(factory.has_method(m), "BulletFactory2D.%s is bound" % m)


func test_is_hitting_follows_the_hitting_phases() -> void:
	var d := H.make_laser(0.125, 0.125, 0.125, 0.125) # 2 ticks each
	d.hit_while_growing = false
	var l := _fire(d)
	var seen := []
	for i in 9:
		step_factory(1, DT)
		seen.append([l.get_phase(), l.is_hitting()])
	var expected := [
		[LaserVolley2D.PHASE_CHARGING, false], [LaserVolley2D.PHASE_GROWING, false], [LaserVolley2D.PHASE_GROWING, false],
		[LaserVolley2D.PHASE_ACTIVE, true], [LaserVolley2D.PHASE_FADING, false],
	]
	for e in expected:
		assert_has(seen, e, "seen %s" % [e])
	d.hit_while_growing = true
	var l2 := _fire(d)
	step_factory(2, DT)
	assert_eq([l2.get_phase(), l2.is_hitting()], [LaserVolley2D.PHASE_GROWING, true], "hit_while_growing: growing hits")


func test_beam_contacts_list_the_targets_hit_now_nearest_first() -> void:
	var far := make_area(Vector2(200, 0), Vector2(20, 60))
	var near := make_area(Vector2(100, 0), Vector2(20, 60))
	await idle(2)
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0, 400.0))
	assert_eq(l.get_beam_contacts(0), [], "nothing before the first tick")
	step_factory(2, DT)
	var c: Array = l.get_beam_contacts(0)
	assert_eq(c.map(func(e): return e["target"]), [near, far], "nearest first")
	assert_almost_eq(c[0]["position"], Vector2(100, 0), Vector2(0.01, 0.01), "where the beam meets it")
	assert_almost_eq(c[1]["distance"], 200.0, 0.01, "its distance along the beam")
	assert_almost_eq(c[0]["since"], DT, 0.0001, "touching since the first tick")
	assert_eq(l.get_pierced_targets(0), [near, far], "the same targets as get_pierced_targets")
	near.free()
	assert_eq(l.get_beam_contacts(0).map(func(e): return e["target"]), [far], "a freed target is left out at once")
	l.get_beam_contacts(3)
	expect_error_sequence(["Invalid beam index in get_beam_contacts: 3 (beam count: 1)"])


func test_beam_target_reads_back_what_set_beam_target_gave() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 400.0)
	d.aim = LaserData2D.LASER_AIM_TRACK
	var l := _fire(d)
	assert_null(l.get_beam_target(0), "none")
	l.set_beam_target(0, Vector2(50, 60))
	assert_eq(l.get_beam_target(0), Vector2(50, 60), "a point")
	var node := Node2D.new()
	add(node)
	l.set_beam_target(0, node)
	assert_same(l.get_beam_target(0), node, "a node")
	node.free()
	assert_null(l.get_beam_target(0), "a freed node reads null")
	l.set_beam_target(0, null)
	assert_null(l.get_beam_target(0), "cleared")
	expect_no_errors()


func test_a_bolt_is_spent_at_its_pierce_limit() -> void:
	make_area(Vector2(150, 0), Vector2(20, 60))
	make_area(Vector2(300, 0), Vector2(20, 60))
	await idle(2)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 40.0)
	d.type = LaserData2D.LASER_BOLT
	d.speed = 1600.0 # 100 px per tick
	d.pierce_limit = 2
	var l := _fire(d)
	assert_eq([l.is_beam_spent(0), l.get_beam_pierced_total(0)], [false, 0], "fresh")
	var at_finish := []
	factory.laser_finished.connect(func(laser: LaserVolley2D) -> void: at_finish.append([laser.is_beam_spent(0), laser.get_beam_pierced_total(0)]))
	var totals := []
	for i in 6:
		step_factory(1, DT)
		if not at_finish.is_empty():
			break
		totals.append([l.is_beam_spent(0), l.get_beam_pierced_total(0)])
	assert_eq(totals, [[false, 0], [false, 1]], "in flight: then one target pierced, not spent")
	assert_eq(at_finish, [[true, 2]], "spent at the second target: the laser finishes (then pools)")


func test_touches_tests_the_body_with_half_its_width() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 400.0)
	d.width = 20.0
	var l := _fire(d)
	step_factory(1, DT)
	assert_true(l.touches_circle(Vector2(200, 15), 5.0), "within half the width plus the radius")
	assert_false(l.touches_circle(Vector2(200, 16), 5.0), "just past it")
	assert_true(l.touches_rect(Rect2(390, 5, 30, 30)), "a rect at the end")
	assert_true(l.touches_rect(Rect2(420, 35, -30, -30)), "a negative rect is normalized")
	assert_true(l.touches_rect(Rect2(410, -5, 30, 10)), "half the width past the end: edges count")
	assert_false(l.touches_rect(Rect2(411, -5, 30, 10)), "past it")
	assert_false(l.touches_circle(Vector2(NAN, 0), 5.0), "refused")
	assert_false(l.touches_circle(Vector2(0, 0), -1.0), "refused")
	assert_false(l.touches_rect(Rect2(0, 0, INF, 1)), "refused")
	expect_error_sequence([
		"touches_circle: center must be finite and radius a finite number >= 0.",
		"touches_circle: center must be finite and radius a finite number >= 0.",
		"touches_rect: rect must be finite.",
	])
	l.end_laser(false)
	step_factory(2, DT)
	assert_false(l.touches_circle(Vector2(200, 0), 50.0), "a finished laser touches nothing")


func test_sound_readouts_return_the_arming() -> void:
	var l := _fire(H.make_laser())
	assert_eq([l.sound_get_effects(), l.sound_get_listener_group()], [[], &""], "unarmed")
	var s := H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_FIRE)
	assert_true(l.sound_set_effects([s], &"ears"), "armed")
	assert_eq([l.sound_get_effects(), l.sound_get_listener_group()], [[s], &"ears"], "read back")


func test_factory_lists_the_lasers_in_an_area_and_ends_none() -> void:
	var low := H.make_laser(0.0, 0.0, 0.0, 0.0, 400.0)
	low.collision_mask = 4
	var high := H.make_laser(0.0, 0.0, 0.0, 0.0, 400.0)
	high.collision_mask = 8
	var a := _fire(low)
	var b := _fire(high, Transform2D(0.0, Vector2(0, 500)))
	step_factory(1, DT)
	assert_eq(factory.get_lasers_in_circle(Vector2(200, 0), 5.0), [a], "the beam through the circle")
	assert_eq(factory.get_lasers_in_rect(Rect2(100, -10, 50, 600)), [a, b], "both cross the rect")
	assert_eq(factory.get_lasers_in_rect(Rect2(100, -10, 50, 600), 8), [b], "can_hit_mask keeps the lasers that hit layer 8")
	assert_eq(factory.get_lasers_in_circle(Vector2(200, 250), 5.0), [], "between them: none")
	assert_eq([a.is_laser_active(), b.is_laser_active()], [true, true], "listing ends nothing")
	assert_eq(factory.get_lasers_in_circle(Vector2(INF, 0), 1.0), [], "refused")
	assert_eq(factory.get_lasers_in_rect(Rect2(0, 0, 1, 1), -1), [], "refused")
	expect_error_sequence([
		"get_lasers_in_circle: center must be finite and radius a finite number >= 0.",
		"get_lasers_in_rect: can_hit_mask must be between 0 and 4294967295 (0 matches everything).",
	])
	a.end_laser(false)
	step_factory(2, DT)
	assert_eq(factory.get_lasers_in_circle(Vector2(200, 0), 5.0), [], "a pooled laser is not listed")
