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


## LaserData2D is read every tick: a follow_spawner edit reaches a firing
## laser. Off, its beams stay where they are; on again, they ride the anchor
## from where they are (no jump); a laser fired with it off picks it up. A
## script's own set_anchor still rides between edits.
func test_a_follow_spawner_edit_reaches_a_firing_laser() -> void:
	var anchor := Node2D.new()
	anchor.position = Vector2(50, 50)
	add(anchor)
	var d := _endless()
	var l := _fire(d, Transform2D(0.0, Vector2(60, 50)), anchor)
	step_factory(1, DT)
	d.follow_spawner = false
	anchor.position = Vector2(100, 50)
	step_factory(1, DT)
	assert_eq(l.get_beam_start(0), Vector2(60, 50), "off: the beam stays where it is")
	d.follow_spawner = true
	step_factory(1, DT)
	assert_eq(l.get_beam_start(0), Vector2(60, 50), "on again: no jump onto the anchor")
	anchor.position = Vector2(130, 70)
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_start(0).distance_to(Vector2(90, 70)), 0.0, 0.01, "it rides the anchor from where it was")
	var late := _endless()
	late.follow_spawner = false
	var fixed := _fire(late, Transform2D(0.0, Vector2(140, 70)), anchor)
	step_factory(1, DT)
	late.follow_spawner = true
	step_factory(1, DT)
	anchor.position = Vector2(130, 100)
	step_factory(1, DT)
	assert_almost_eq(fixed.get_beam_start(0).distance_to(Vector2(140, 100)), 0.0, 0.01, "fired with it off, it rides once turned on")
	var scripted := _endless()
	scripted.follow_spawner = false
	var by_script := _fire(scripted, Transform2D(0.0, Vector2(0, 0)))
	by_script.set_anchor(anchor)
	anchor.position = Vector2(150, 100)
	step_factory(1, DT)
	assert_almost_eq(by_script.get_beam_start(0).distance_to(Vector2(20, 0)), 0.0, 0.01, "a script's set_anchor rides between edits")


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
	target.queue_free()
	await idle(1)
	step_factory(2, DT)
	assert_almost_eq(_deg(l), 90.0, 0.001, "active: the aim holds when the target is gone too")


## Lock On with keep_tracking_when_active off: "charge, lock, fire". The beam
## fires along the locked line AND ends where the telegraph ended (where the
## target stood at the lock), wherever the target moves or goes afterwards.
func test_lock_on_without_tracking_freezes_its_length_at_the_lock() -> void:
	var d := H.make_laser(0.125, 0.0, 0.0, 0.0, 400.0)
	d.aim = LaserData2D.LASER_AIM_LOCK_ON
	d.keep_tracking_when_active = false
	var l := _fire(d)
	var target := Node2D.new()
	target.position = Vector2(60, 80) # 100 px away
	add(target)
	l.set_beam_target(0, target)
	step_factory(2, DT) # charging: tracks
	target.position = Vector2(0, 50) # dodges inward
	step_factory(2, DT) # active
	assert_almost_eq(l.get_beam_end(0).distance_to(Vector2(60, 80)), 0.0, 0.01, "ends at the locked point, not on the moved target")
	target.position = Vector2(0, 300)
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_length(0), 100.0, 0.01, "the length holds")
	target.queue_free()
	await idle(1)
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_end(0).distance_to(Vector2(60, 80)), 0.0, 0.01, "a lost target changes nothing")


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


func _sweeper(arc := 0.0) -> LaserData2D:
	var d := _endless()
	d.sweep_speed_degrees = 90.0
	d.sweep_arc_degrees = arc
	return d


func _same_angle(a: float, b: float) -> bool:
	return absf(angle_difference(a, b)) < 0.0001


## set_anchor keeps a running sweep exactly where it is (null = stay, a node
## = ride it): the beam goes on sweeping like a beam that was never re-anchored,
## and its arc stays centered on the pattern direction.
func test_set_anchor_keeps_the_sweep_phase(arc: float = use_parameters([0.0, 60.0])) -> void:
	var control := _fire(_sweeper(arc))
	var l := _fire(_sweeper(arc))
	step_factory(3, DT)
	l.set_anchor(null)
	assert_true(_same_angle(l.get_beam_angle(0), control.get_beam_angle(0)), "unchanged at once")
	for i in 5:
		step_factory(1, DT)
		assert_true(_same_angle(l.get_beam_angle(0), control.get_beam_angle(0)), "null: the sweep goes on (tick %d)" % i)
	var anchor := Node2D.new()
	add(anchor)
	l.set_anchor(anchor)
	for i in 12:
		step_factory(1, DT)
		assert_true(_same_angle(l.get_beam_angle(0), control.get_beam_angle(0)), "riding a still anchor: the same sweep (tick %d)" % i)


## set_beam_angle reads back at once (a sweep's offset rides on top) and the
## next tick goes on from there.
func test_set_beam_angle_reads_back_at_once() -> void:
	var l := _fire(_endless())
	step_factory(1, DT)
	l.set_beam_angle(0, 1.0)
	assert_almost_eq(l.get_beam_angle(0), 1.0, 0.0001, "now")
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_angle(0), 1.0, 0.0001, "and after a tick")
	var s := _fire(_sweeper())
	step_factory(2, DT)
	var before := s.get_beam_angle(0)
	var offset := before # pattern aim 0: the angle is the sweep's offset
	s.set_beam_angle(0, 1.0)
	assert_almost_eq(s.get_beam_angle(0), 1.0 + offset, 0.0001, "a sweeping beam: the new aim plus its sweep")
	step_factory(1, DT)
	assert_almost_eq(s.get_beam_angle(0), 1.0 + offset + deg_to_rad(90.0) * DT, 0.0001, "the sweep goes on from there")


## set_targets validates the whole array first: one bad entry is one error
## and changes no beam.
func test_set_targets_refuses_a_bad_entry_and_changes_nothing() -> void:
	var d := _endless()
	d.aim = LaserData2D.LASER_AIM_TRACK
	d.turn_rate_degrees = 0.0
	var t: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2.ZERO)]
	var l := factory.spawn_laser(d, t)
	l.set_targets([Vector2(0, 100)])
	step_factory(1, DT)
	assert_eq([_deg(l), snappedf(rad_to_deg(l.get_beam_angle(2)), 0.001)], [90.0, 90.0], "every beam aims at the target")
	var gone := Node2D.new()
	add_child(gone)
	gone.free()
	for bad in [[Vector2(100, 0), Vector2(NAN, 0)], [Vector2(100, 0), gone], [Vector2(100, 0), 7]]:
		l.set_targets(bad)
		expect_error_sequence(["set_targets: every entry must be a Node2D, a finite Vector2 or null, nothing changed."])
		step_factory(1, DT)
		assert_eq([_deg(l), snappedf(rad_to_deg(l.get_beam_angle(1)), 0.001)], [90.0, 90.0], "no beam changed (%s)" % [bad])
	l.set_targets([Vector2(100, 0), null])
	step_factory(1, DT)
	assert_eq(_deg(l), 0.0, "a good array applies (null: no target)")


## spawn_laser with an anchor outside the tree fires from its global poses
## and starts riding the anchor once it enters, from where it is then (no
## jump by the anchor's own transform).
func test_an_anchor_entering_the_tree_after_the_shot_is_ridden_without_a_jump() -> void:
	var loose := Node2D.new()
	loose.position = Vector2(100, 0)
	var l := _fire(_endless(), Transform2D(0.0, Vector2(50, 50)), loose)
	step_factory(1, DT)
	assert_eq(l.get_beam_start(0), Vector2(50, 50), "at its fire pose")
	add(loose)
	step_factory(1, DT)
	assert_eq(l.get_beam_start(0), Vector2(50, 50), "entering the tree is no jump")
	loose.position = Vector2(110, 0)
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_start(0).distance_to(Vector2(60, 50)), 0.0, 0.001, "then it rides the anchor")


## set_anchor on a node outside the tree: the beam stays, then rides the node
## once it is in the tree (from where it is then).
func test_set_anchor_on_a_node_outside_the_tree_rides_it_once_it_enters() -> void:
	var l := _fire(_endless(), Transform2D(0.0, Vector2(20, 0)))
	step_factory(1, DT)
	var loose := Node2D.new()
	loose.position = Vector2(300, 0)
	l.set_anchor(loose)
	assert_eq(l.get_anchor(), loose, "anchored")
	step_factory(1, DT)
	assert_eq(l.get_beam_start(0), Vector2(20, 0), "stays while the node is out of the tree")
	add(loose)
	step_factory(1, DT)
	assert_eq(l.get_beam_start(0), Vector2(20, 0), "no jump when it enters")
	loose.position = Vector2(330, 0)
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_start(0).distance_to(Vector2(50, 0)), 0.0, 0.001, "rides it")


## A target on a Pattern-aimed laser steers nothing: it is kept (a later Track
## or Lock On aim uses it) and the first one per life warns; clearing never warns.
func test_a_target_on_a_pattern_laser_warns_once_per_life() -> void:
	var d := _endless()
	var t: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2.ZERO)]
	var l := factory.spawn_laser(d, t)
	l.set_beam_target(0, null)
	expect_warning_sequence([])
	l.set_targets([Vector2(0, 100)])
	expect_warning_sequence(["LaserVolley2D: laser_data aims along the pattern, so a target steers nothing (kept for a Track or Lock On aim)."])
	l.set_beam_target(1, Vector2(5, 5))
	expect_warning_sequence([])
	step_factory(1, DT)
	assert_eq(_deg(l), 0.0, "the beam keeps its pattern direction")
	l.end_laser()
	step_factory(2, DT)
	var again := factory.spawn_laser(d, t)
	again.set_targets([Vector2(0, 100)])
	expect_warning_sequence(["LaserVolley2D: laser_data aims along the pattern, so a target steers nothing (kept for a Track or Lock On aim)."])


## A bolt / curvy laser honours keep_tracking_when_active: once active, its
## head stops re-aiming at a moving target (only the beam used to).
func test_keep_tracking_when_active_freezes_a_bolt_head() -> void:
	var target := Node2D.new()
	target.position = Vector2(100, 0)
	add(target)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 40.0)
	d.type = LaserData2D.LASER_BOLT
	d.speed = 100.0
	d.aim = LaserData2D.LASER_AIM_TRACK
	d.turn_rate_degrees = 0.0 # snap
	d.keep_tracking_when_active = false
	var l := _fire(d)
	l.set_beam_target(0, target)
	step_factory(1, DT)
	var before := l.get_beam_angle(0)
	target.position = Vector2(0, 100) # desired heading becomes ~90 degrees
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_angle(0), before, 0.001, "a frozen bolt keeps its heading")
