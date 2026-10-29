extends BlastTest
## Laser hits: every hitting tick a beam queries the physics space along its
## length (collision_mask, bodies and/or areas). A target it starts touching
## gets laser_pierce_started then laser_hit (nearest first, at its position
## projected onto the beam); hit_interval_sec repeats laser_hit while it stays
## (no burst after a hitch); one it stops touching gets laser_pierce_ended (a
## freed one ends silently); every contact ends when the laser stops hitting.
## pierce_limit N hits the N nearest targets and cuts the beam at the last.
## Charging never hits; a handler that ends the laser stops the tick's hits.

const DT := 0.0625

var log: Array = [] # [tick, kind, target name, beam, position x]
var tick := 0
var a: Area2D
var b: Area2D
var c: Area2D


func before_each() -> void:
	await super()
	log.clear()
	tick = 0
	a = _target("A", 100.0)
	b = _target("B", 200.0)
	c = _target("C", 300.0)
	factory.laser_pierce_started.connect(func(t: Object, _l: LaserVolley2D, i: int, p: Vector2) -> void: log.append([tick, "start", str(t.name), i, roundi(p.x)]))
	factory.laser_hit.connect(func(t: Object, _l: LaserVolley2D, i: int, p: Vector2) -> void: log.append([tick, "hit", str(t.name), i, roundi(p.x)]))
	factory.laser_pierce_ended.connect(func(t: Object, _l: LaserVolley2D, i: int) -> void: log.append([tick, "end", str(t.name), i]))
	await idle(2) # the targets join the physics space


func _target(n: String, x: float) -> Area2D:
	var t := make_area(Vector2(x, 0), Vector2(20, 60))
	t.name = n
	return t


func _step(k: int) -> void:
	for i in k:
		tick += 1
		step_factory(1, DT)


func _fire(data: LaserData2D) -> LaserVolley2D:
	var t: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO)]
	return factory.spawn_laser(data, t)


func _kinds() -> Array:
	var out: Array = []
	for e in log:
		out.append("%s:%s" % [e[1], e[2]])
	return out


func test_every_target_along_the_beam_is_started_then_hit_nearest_first() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0))
	_step(1)
	assert_eq(log, [[1, "start", "A", 0, 100], [1, "hit", "A", 0, 100], [1, "start", "B", 0, 200], [1, "hit", "B", 0, 200], [1, "start", "C", 0, 300], [1, "hit", "C", 0, 300]], "one start + hit each, nearest first, on the beam")
	assert_eq(l.get_pierced_targets(0), [a, b, c], "the readout, nearest first")
	_step(3)
	assert_eq(log.size(), 6, "no interval: one hit per contact")


func test_hit_interval_repeats_hits_and_never_bursts() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 150.0) # reaches A only
	d.hit_interval_sec = 0.125
	_fire(d)
	_step(5)
	assert_eq(log, [[1, "start", "A", 0, 100], [1, "hit", "A", 0, 100], [3, "hit", "A", 0, 100], [5, "hit", "A", 0, 100]], "every 0.125 s")
	log.clear()
	tick = 0
	step_factory(1, 1.0) # a one-second hitch
	tick = 1
	_step(1)
	assert_eq(_kinds(), ["hit:A"], "one hit after the hitch, never the eight it missed")


## Raising hit_interval_sec on a live laser must reach a contact that already
## started: it was scheduled at INF while the interval was 0.
func test_raising_the_hit_interval_reaches_an_open_contact() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 150.0) # reaches A only
	_fire(d)
	_step(1)
	assert_eq(log, [[1, "start", "A", 0, 100], [1, "hit", "A", 0, 100]], "one hit while the interval is 0")
	log.clear()
	d.hit_interval_sec = 0.25
	_step(1)
	assert_eq(_kinds(), ["hit:A"], "the raised interval takes effect on the open contact")
	log.clear()
	_step(4)
	assert_eq(_kinds(), ["hit:A"], "and repeats every 0.25 s")


func test_pierce_limit_hits_the_nearest_and_cuts_the_beam() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0)
	d.pierce_limit = 2
	var l := _fire(d)
	_step(1)
	assert_eq(_kinds(), ["start:A", "hit:A", "start:B", "hit:B"], "the two nearest")
	assert_almost_eq(l.get_beam_length(0), 200.0, 0.01, "cut at the second target")
	assert_almost_eq(l.get_beam_end(0).x, 200.0, 0.01, "it ends on B")
	d.pierce_limit = 1
	_step(1)
	assert_eq(_kinds().slice(4), ["end:B"], "an edit in flight: B is no longer touched")
	assert_almost_eq(l.get_beam_length(0), 100.0, 0.01, "stops at the first target")


func test_a_target_that_leaves_ends_and_a_freed_one_ends_silently() -> void:
	_fire(H.make_laser(0.0, 0.0, 0.0, 0.0))
	_step(1)
	log.clear()
	c.position = Vector2(300, 500) # out of the beam
	b.free()
	await idle(1) # the physics space sees the move (one factory tick too)
	tick = 2
	assert_eq(_kinds(), ["end:C"], "C ended, B (freed) ended silently")


func test_every_contact_ends_when_the_laser_stops_hitting() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 0.125, 0.125))
	_step(1)
	log.clear()
	_step(2) # active ends on tick 2: fading
	assert_eq(_kinds(), ["end:A", "end:B", "end:C"], "fading ends every contact")
	assert_eq(l.get_pierced_targets(0), [], "nothing touched")


func test_charging_never_hits_and_growing_hits_only_when_asked() -> void:
	var d := H.make_laser(0.125, 0.125, 0.0, 0.0)
	d.hit_while_growing = false
	_fire(d)
	_step(3) # charging 2 ticks, growing from tick 2
	assert_eq(log, [], "no hits while charging or (hit_while_growing off) growing")
	_step(2)
	assert_eq(_kinds().slice(0, 2), ["start:A", "hit:A"], "active: hits")


func test_bodies_and_areas_can_be_left_out() -> void:
	var wall := make_wall(Vector2(250, 0), Vector2(20, 60))
	wall.name = "W"
	await idle(2)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0)
	d.collide_with_areas = false
	_fire(d)
	_step(1)
	assert_eq(_kinds(), ["start:W", "hit:W"], "bodies only")
	log.clear()
	d.collide_with_areas = true
	d.collide_with_bodies = false
	_step(1)
	assert_eq(_kinds(), ["end:W", "start:A", "hit:A", "start:B", "hit:B", "start:C", "hit:C"], "areas only (edits reach the laser)")


func test_a_handler_that_ends_the_laser_stops_the_hits_of_that_tick() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 0.25))
	factory.laser_hit.connect(func(_t: Object, laser: LaserVolley2D, _i: int, _p: Vector2) -> void: laser.end_laser(), CONNECT_ONE_SHOT)
	_step(1)
	assert_eq(_kinds(), ["start:A", "hit:A", "end:A"], "the first hit ended it: B and C never start (nor end), A ends at once")
	_step(1)
	assert_eq(_kinds().size(), 3, "nothing more")
	assert_eq(l.get_phase(), LaserVolley2D.PHASE_FADING, "fading")


func test_two_beams_hit_on_their_own_index() -> void:
	var up := make_area(Vector2(0, -150), Vector2(60, 20))
	up.name = "U"
	await idle(2)
	var t: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO), Transform2D(-PI / 2.0, Vector2.ZERO)]
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 250.0)
	factory.spawn_laser(d, t)
	_step(1)
	var by_beam := {}
	for e in log:
		if e[1] == "hit":
			by_beam[e[2]] = e[3]
	assert_eq(by_beam, {"A": 0, "B": 0, "U": 1}, "each target on the beam that touched it")


func test_bullets_and_other_plugin_nodes_are_never_laser_targets() -> void:
	# Bullets on the laser's mask (value 4), between A, B and C: a volley is one
	# physics area inside the factory, never a target (the one target filter).
	var data := H.make_volley_data(2, 0.0, 30.0)
	data.collision_layer = 4
	data.monitorable = true
	data.transforms = [Transform2D(0.0, Vector2(150, 0)), Transform2D(0.0, Vector2(250, 0))]
	var v := factory.spawn_volley(data)
	await idle(2) # the volley's area joins the space
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0))
	_step(1)
	assert_eq(_kinds(), ["start:A", "hit:A", "start:B", "hit:B", "start:C", "hit:C"], "only the game's nodes")
	var names: Array = []
	for t in l.get_pierced_targets(0):
		names.append(str(t.name))
	assert_eq(names, ["A", "B", "C"], "and only they are pierced")
	assert_true(v.is_bullet_status_enabled(0), "the bullets are untouched")
