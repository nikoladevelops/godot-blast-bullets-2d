extends BlastTest
## Bolts and curvy lasers: the head flies at `speed` from the muzzle (turning
## toward its target with AIM_TRACK; a curvy head also waves sideways) and the
## body is the newest `length` px of the path it took, tail to head (the head
## is get_beam_end). A head reflects off `reflections` walls, then a wall
## spends it (one laser_wall_hit); pierce_limit N spends it after N distinct
## targets; leaving the despawn bounds spends it (one laser_despawned per
## head, on the owner spawner or the factory; ignore_despawn_bounds flies on).
## A spent beam is never drawn or hit again, and the laser finishes when every
## beam is spent.

const DT := 0.0625

var log: Array = []
var tick := 0


func before_each() -> void:
	await super()
	log.clear()
	tick = 0
	factory.laser_hit.connect(func(t: Object, _l, i: int, _p) -> void: log.append([tick, "hit", str(t.name), i]))
	factory.laser_pierce_ended.connect(func(t: Object, _l, i: int) -> void: log.append([tick, "end", str(t.name), i]))
	factory.laser_wall_hit.connect(func(_l, i: int, p: Vector2, _n) -> void: log.append([tick, "wall", i, p.round()]))
	factory.laser_finished.connect(func(_l) -> void: log.append([tick, "finished"]))
	factory.laser_despawned.connect(func(_l, i: int, p: Vector2) -> void: log.append([tick, "despawned", i, p.round()]))


func _step(k: int) -> void:
	for i in k:
		tick += 1
		step_factory(1, DT)


func _bolt(type := LaserData2D.LASER_BOLT) -> LaserData2D:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 80.0)
	d.type = type
	d.speed = 1600.0 # 100 px per tick
	return d


func _fire(data: LaserData2D, poses: Array = [Transform2D(0.0, Vector2.ZERO)]) -> LaserVolley2D:
	var t: Array[Transform2D] = []
	for p in poses:
		t.append(p)
	return factory.spawn_laser(data, t)


func test_a_bolt_flies_with_its_body_trailing_the_head() -> void:
	var l := _fire(_bolt())
	_step(1)
	assert_almost_eq(l.get_beam_end(0).x, 100.0, 0.01, "the head flew 100 px")
	assert_almost_eq(l.get_beam_start(0).x, 20.0, 0.01, "the tail trails it by length")
	assert_almost_eq(l.get_beam_length(0), 80.0, 0.01, "exactly length long")
	_step(2)
	assert_almost_eq(l.get_beam_end(0).x, 300.0, 0.01, "keeps flying")
	assert_almost_eq(l.get_beam_length(0), 80.0, 0.01, "and keeps its length")
	assert_eq(l.get_beam_points(0).size() <= 4, true, "the path behind the tail is dropped")


func test_pierce_limit_spends_a_bolt_after_its_targets() -> void:
	var a := make_area(Vector2(300, 0), Vector2(20, 60))
	a.name = "A"
	var b := make_area(Vector2(500, 0), Vector2(20, 60))
	b.name = "B"
	await idle(2)
	var d := _bolt()
	d.pierce_limit = 1
	var l := _fire(d)
	_step(8)
	assert_eq(log, [[3, "hit", "A", 0], [3, "end", "A", 0], [3, "finished"]], "one target, then spent: its contact ends and the laser finishes")
	assert_true(l.is_pooled(), "pooled")
	log.clear()
	tick = 0
	d.pierce_limit = 0
	_fire(d)
	_step(6)
	var hits: Array = []
	for e in log:
		if e[1] == "hit":
			hits.append(e[2])
	assert_eq(hits, ["A", "B"], "pierce_limit 0 passes through every target")


func test_a_wall_reflects_then_spends_the_head() -> void:
	make_wall(Vector2(250, 0), Vector2(20, 400), 16)
	await idle(2)
	var d := _bolt()
	d.wall_mask = 16
	d.reflections = 1
	var l := _fire(d)
	_step(3) # tick 3 reaches the wall face (240) and turns back
	assert_almost_eq(l.get_beam_angle(0), PI, 0.001, "reflected straight back")
	assert_eq(log, [], "a reflection is not a stop")
	_step(1)
	assert_lt(l.get_beam_end(0).x, 240.0, "flying back")
	make_wall(Vector2(-150, 0), Vector2(20, 400), 16)
	await idle(1)
	tick = 4
	_step(6)
	var walls: Array = []
	for e in log:
		if e[1] == "wall":
			walls.append(e[3])
	assert_eq(walls, [Vector2(-140, 0)], "no reflection left: the second wall stops it (one laser_wall_hit)")
	assert_eq(log.back()[1], "finished", "and the spent bolt ends the laser")


func test_a_tracking_bolt_turns_toward_its_target() -> void:
	var d := _bolt()
	d.aim = LaserData2D.LASER_AIM_TRACK
	d.turn_rate_degrees = 90.0
	var l := _fire(d)
	l.set_targets([Vector2(0, 2000)])
	_step(4)
	assert_almost_eq(rad_to_deg(l.get_beam_angle(0)), 22.5, 0.01, "the heading turns 90 deg/s")


func test_a_curvy_body_follows_the_path_its_head_took() -> void:
	var d := _bolt(LaserData2D.LASER_CURVY)
	d.length = 300.0
	d.wave_amplitude = 40.0
	d.wave_frequency = 1.0
	var l := _fire(d)
	_step(6)
	var pts := l.get_beam_points(0)
	assert_gt(pts.size(), 3, "a bent body, not a segment")
	var off_axis := 0.0
	for p in pts:
		off_axis = maxf(off_axis, absf(p.y))
	assert_gt(off_axis, 5.0, "the wave moved the head sideways")
	assert_almost_eq(l.get_beam_length(0), 300.0, 0.5, "the body is length long")
	assert_eq(l.get_beam_end(0), pts[pts.size() - 1], "the head is the end")


func test_leaving_the_despawn_bounds_spends_it_with_one_signal() -> void:
	factory.despawn_bounds_rect = Rect2(-50, -50, 300, 100)
	factory.despawn_bounds_enabled = true
	var l := _fire(_bolt())
	_step(6)
	# The head reaches x 400 on tick 4: head and tail both out of the box.
	assert_eq(log, [[4, "despawned", 0, Vector2(400, 0)], [4, "finished"]], "out of the bounds by tick 4: announced where it left, then finished")
	assert_true(l.is_pooled(), "pooled")


func test_ignore_despawn_bounds_flies_on_until_its_phases_end() -> void:
	factory.despawn_bounds_rect = Rect2(-50, -50, 300, 100)
	factory.despawn_bounds_enabled = true
	var d := _bolt()
	d.active_sec = 0.5
	d.ignore_despawn_bounds = true
	var l := _fire(d)
	_step(6)
	assert_eq(log, [], "never spent by the bounds")
	assert_gt(l.get_beam_end(0).x, 500.0, "still flying outside them")
	_step(4)
	assert_eq(log, [[8, "finished"]], "it ends with its phases (0.5 s = 8 ticks)")


func test_a_spawner_bolt_despawns_on_its_spawner_and_may_fire_from_outside_when_ignoring() -> void:
	factory.despawn_bounds_rect = Rect2(-50, -50, 300, 100)
	factory.despawn_bounds_enabled = true
	var sp := LaserSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.laser_data = _bolt()
	sp.pattern_source = LaserSpawner2D.PATTERN_FROM_HELPER_CUSTOM
	sp.helper_custom_transforms = [Transform2D(0.0, Vector2.ZERO)]
	add(sp)
	sp.set_bullet_factory(factory)
	var on_spawner: Array = []
	sp.laser_despawned.connect(func(_l, i: int, _p) -> void: on_spawner.append(i))
	assert_true(sp.shoot_once(), "fires")
	_step(6)
	assert_eq(on_spawner, [0], "the owner spawner hears it")
	assert_false(log.any(func(e): return e[1] == "despawned"), "never the factory as well")
	sp.position = Vector2(-400, 0) # the muzzle outside the bounds
	assert_false(sp.shoot_once(), "refused outside the bounds")
	sp.laser_data.ignore_despawn_bounds = true
	assert_true(sp.shoot_once(), "ignore_despawn_bounds lets it fire from outside")


func test_the_laser_ends_only_when_every_beam_is_spent() -> void:
	var a := make_area(Vector2(200, 0), Vector2(20, 60))
	a.name = "A"
	await idle(2)
	var d := _bolt()
	d.pierce_limit = 1
	var l := _fire(d, [Transform2D(0.0, Vector2.ZERO), Transform2D(PI / 2.0, Vector2.ZERO)])
	_step(3)
	var state: Array = l.debug_get_draw_state(1.0)
	assert_eq([state[0]["spent"], state[1]["spent"]], [true, false], "beam 0 hit A and is spent, beam 1 flies on")
	assert_false(l.is_pooled(), "still firing")


func test_charging_shows_the_path_from_the_muzzle() -> void:
	var d := _bolt()
	d.charge_sec = 0.25
	var l := _fire(d, [Transform2D(0.0, Vector2(10, 10))])
	_step(1)
	var state: Array = l.debug_get_draw_state(1.0)
	assert_true(state[0]["warning"], "a warning while charging")
	assert_eq((state[0]["points"] as PackedVector2Array)[0], Vector2(10, 10), "from the muzzle")
	assert_almost_eq(l.get_beam_end(0).x, 90.0, 0.01, "along its path, length long")
