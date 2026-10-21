extends BlastTest
## Laser aim and motion: a beam points along its slot (AIM_PATTERN), follows
## its anchor's moves and turns while follow_spawner is on (a spawner anchor
## adds its spin; a lost anchor leaves the beam where it was), sweeps around
## its muzzle (continuously, or swinging inside sweep_arc_degrees), grows at
## extend_speed behind a full-length warning, turns toward its target at
## turn_rate_degrees (AIM_TRACK; 0 snaps; keep_tracking_when_active off stops
## once active), ends on its target (AIM_LOCK_ON, capped at length), and
## takes set_beam_angle / set_targets / set_anchor at any time.

const DT := 0.0625


func _fire(data: LaserData2D, pose := Transform2D(0.0, Vector2.ZERO), anchor: Node2D = null) -> LaserVolley2D:
	var t: Array[Transform2D] = [pose]
	return factory.spawn_laser(data, t, anchor)


func _endless() -> LaserData2D:
	return H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)


func _deg(l: LaserVolley2D) -> float:
	return snappedf(rad_to_deg(l.get_beam_angle(0)), 0.001)


func test_a_beam_points_along_its_slot() -> void:
	var l := _fire(_endless(), Transform2D(PI / 2.0, Vector2(10, 20)))
	step_factory(1, DT)
	assert_eq(l.get_beam_start(0), Vector2(10, 20), "the muzzle is the slot's origin")
	assert_almost_eq(l.get_beam_end(0).distance_to(Vector2(10, 220)), 0.0, 0.01, "along the slot's x axis, length long")


func test_it_follows_its_anchor_while_follow_spawner_is_on() -> void:
	var anchor := Node2D.new()
	anchor.position = Vector2(50, 50)
	add(anchor)
	var l := _fire(_endless(), Transform2D(0.0, Vector2(60, 50)), anchor) # 10 px ahead of the anchor
	step_factory(1, DT)
	anchor.position = Vector2(100, 100)
	anchor.rotation = PI / 2.0
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_start(0).distance_to(Vector2(100, 110)), 0.0, 0.01, "the slot rides the anchor (moved and turned)")
	assert_almost_eq(_deg(l), 90.0, 0.01, "and turns with it")
	var still := _endless()
	still.follow_spawner = false
	var fixed := _fire(still, Transform2D(0.0, Vector2(60, 50)), anchor)
	anchor.position = Vector2(300, 300)
	step_factory(1, DT)
	assert_eq(fixed.get_beam_start(0), Vector2(60, 50), "follow_spawner off: it stays where it was fired")
	assert_almost_eq(l.get_beam_start(0).distance_to(Vector2(300, 310)), 0.0, 0.01, "the following beam went along")
	anchor.free()
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_start(0).distance_to(Vector2(300, 310)), 0.0, 0.01, "a freed anchor leaves the beam where it was")
	assert_null(l.get_anchor(), "no anchor")


func test_a_spawner_anchor_adds_its_spin() -> void:
	var sp := make_spawner()
	sp.position = Vector2(200, 0)
	var l := _fire(_endless(), Transform2D(0.0, Vector2(200, 0)), sp)
	sp.set_spin_angle_deg(90.0)
	step_factory(1, DT)
	assert_almost_eq(_deg(l), 90.0, 0.01, "the spawner's spin turns the beam")


func test_set_anchor_starts_and_stops_following() -> void:
	var l := _fire(_endless(), Transform2D(0.0, Vector2(0, 0)))
	var anchor := Node2D.new()
	add(anchor)
	l.set_anchor(anchor)
	anchor.position = Vector2(30, 0)
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_start(0).x, 30.0, 0.01, "rides the new anchor")
	l.set_anchor(null)
	anchor.position = Vector2(90, 0)
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_start(0).x, 30.0, 0.01, "null: stays")


func test_sweep_turns_continuously_or_swings_inside_its_arc() -> void:
	var d := _endless()
	d.sweep_speed_degrees = 90.0 # 5.625 deg per tick
	var l := _fire(d)
	step_factory(4, DT)
	assert_almost_eq(_deg(l), 22.5, 0.001, "four ticks of 90 deg/s")
	var swing := _endless()
	swing.sweep_speed_degrees = 160.0 # 10 deg per tick
	swing.sweep_arc_degrees = 30.0
	var s := _fire(swing)
	var seen: Array = []
	for i in 6:
		step_factory(1, DT)
		seen.append(roundf(_deg(s)))
	assert_eq(seen, [10.0, 10.0, 0.0, -10.0, -10.0, 0.0], "bounces off the +/-15 deg edges")


func test_extend_speed_grows_the_beam_behind_a_full_warning() -> void:
	var d := H.make_laser(0.125, 0.0, 0.0, 0.0, 400.0)
	d.extend_speed = 1600.0 # 100 px per tick
	var l := _fire(d)
	step_factory(1, DT)
	var warning: Array = l.debug_get_draw_state(1.0)
	assert_true(warning[0]["warning"], "charging: the warning line")
	assert_almost_eq((warning[0]["points"] as PackedVector2Array)[1].x, 400.0, 0.01, "the warning shows the whole path")
	var lengths: Array = []
	for i in 5:
		step_factory(1, DT)
		lengths.append(snappedf(l.get_beam_length(0), 0.01))
	assert_eq(lengths, [100.0, 200.0, 300.0, 400.0, 400.0], "grows 100 px per tick up to its length")


func test_track_turns_toward_its_target_at_the_turn_rate() -> void:
	var d := _endless()
	d.aim = LaserData2D.LASER_AIM_TRACK
	d.turn_rate_degrees = 90.0
	var l := _fire(d)
	l.set_targets([Vector2(0, 100)]) # straight below the muzzle
	step_factory(4, DT)
	assert_almost_eq(_deg(l), 22.5, 0.001, "90 deg/s for four ticks")
	step_factory(20, DT)
	assert_almost_eq(_deg(l), 90.0, 0.001, "on target, then it stays")
	d.turn_rate_degrees = 0.0
	l.set_targets([Vector2(-100, 0)])
	step_factory(1, DT)
	assert_almost_eq(absf(_deg(l)), 180.0, 0.001, "0: snaps")


func test_keep_tracking_when_active_off_freezes_the_aim() -> void:
	var d := H.make_laser(0.125, 0.0, 0.0, 0.0, 200.0)
	d.aim = LaserData2D.LASER_AIM_TRACK
	d.turn_rate_degrees = 0.0
	d.keep_tracking_when_active = false
	var l := _fire(d)
	var target := Node2D.new()
	target.position = Vector2(0, 100)
	add(target)
	l.set_targets([target])
	step_factory(2, DT) # charging tracks
	assert_almost_eq(_deg(l), 90.0, 0.001, "aimed while charging")
	target.position = Vector2(100, 0)
	step_factory(2, DT)
	assert_almost_eq(_deg(l), 90.0, 0.001, "active: the aim holds")


func test_lock_on_ends_on_its_target_capped_at_length() -> void:
	var d := _endless()
	d.aim = LaserData2D.LASER_AIM_LOCK_ON
	var l := _fire(d)
	var target := Node2D.new()
	target.position = Vector2(60, 80) # 100 px away
	add(target)
	l.set_beam_target(0, target)
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_end(0).distance_to(Vector2(60, 80)), 0.0, 0.01, "ends right on the target")
	target.position = Vector2(0, 900)
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_length(0), 200.0, 0.01, "a far target: capped at length")
	assert_almost_eq(_deg(l), 90.0, 0.001, "still aimed at it")


func test_setters_reject_and_keep() -> void:
	var l := _fire(_endless())
	l.set_beam_angle(0, PI)
	step_factory(1, DT)
	assert_almost_eq(absf(_deg(l)), 180.0, 0.001, "set_beam_angle turns the pattern aim")
	l.set_beam_angle(3, 0.0)
	expect_error_sequence(["Invalid beam index in set_beam_angle: 3 (beam count: 1)"])
	l.set_beam_angle(0, NAN)
	expect_error_sequence(["set_beam_angle: radians must be finite, keeping the old angle."])
	l.set_beam_target(0, "nope")
	expect_error_sequence(["set_beam_target: target must be a Node2D, a Vector2 or null, the beam has no target now."])
	l.set_beam_target(0, Vector2(INF, 0))
	expect_error_sequence(["set_beam_target: a Vector2 target must be finite, the beam has no target now."])
	l.get_beam_end(-1)
	expect_error_sequence(["Invalid beam index in get_beam_end: -1 (beam count: 1)"])
