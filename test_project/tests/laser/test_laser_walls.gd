extends BlastTest
## Laser walls: with wall_mask set, a beam traces a ray per segment and stops
## at the first wall (blocked, the wall point and normal, one laser_wall_hit
## each time it becomes blocked), or reflects off `reflections` walls first
## (a polyline of the same total length). A wall is never a target, targets
## behind a wall are never hit, wall_mask 0 passes through walls, and a beam
## growing at extend_speed grows up to the wall.

const DT := 0.0625

var walls: Array = [] # [tick, position, normal]
var tick := 0


func before_each() -> void:
	await super()
	walls.clear()
	tick = 0
	factory.laser_wall_hit.connect(func(_l: LaserVolley2D, _i: int, p: Vector2, n: Vector2) -> void: walls.append([tick, p.round(), n.round()]))


func _step(k: int) -> void:
	for i in k:
		tick += 1
		step_factory(1, DT)


func _fire(data: LaserData2D, angle := 0.0) -> LaserVolley2D:
	var t: Array[Transform2D] = [Transform2D(angle, Vector2.ZERO)]
	return factory.spawn_laser(data, t)


func _walled(mask := 16) -> LaserData2D:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0)
	d.wall_mask = mask
	return d


func test_a_beam_stops_at_the_first_wall() -> void:
	make_wall(Vector2(200, 0), Vector2(20, 200), 16)
	var front := make_area(Vector2(100, 0), Vector2(20, 60))
	var behind := make_area(Vector2(300, 0), Vector2(20, 60))
	await idle(2)
	var hits: Array = []
	factory.laser_hit.connect(func(t: Object, _l, _i, _p) -> void: hits.append(t))
	var l := _fire(_walled())
	_step(1)
	assert_true(l.is_beam_blocked(0), "blocked")
	assert_almost_eq(l.get_beam_end(0).x, 190.0, 0.01, "it ends on the wall's face")
	assert_almost_eq(l.get_beam_length(0), 190.0, 0.01, "length up to the wall")
	assert_eq(l.get_beam_wall_normal(0).round(), Vector2(-1, 0), "the wall's normal")
	assert_eq(walls, [[1, Vector2(190, 0), Vector2(-1, 0)]], "one laser_wall_hit")
	assert_eq(hits, [front], "the target in front is hit, the one behind the wall never")
	_step(3)
	assert_eq(walls.size(), 1, "once per block, not per tick")
	assert_ne(behind, null)


func test_reflections_bend_the_beam_and_keep_its_length() -> void:
	make_wall(Vector2(200, 0), Vector2(20, 600), 16)
	await idle(2)
	var d := _walled()
	d.reflections = 1
	var l := _fire(d, deg_to_rad(30.0))
	_step(1)
	var pts := l.get_beam_points(0)
	assert_eq(pts.size(), 3, "muzzle, the reflection, the end")
	assert_almost_eq(pts[1].x, 190.0, 0.01, "reflected on the wall's face")
	assert_almost_eq(pts[1].y, 190.0 * tan(deg_to_rad(30.0)), 0.01, "where the ray met it")
	assert_almost_eq((pts[2] - pts[1]).angle(), deg_to_rad(150.0), 0.001, "mirrored off the wall")
	assert_almost_eq(l.get_beam_length(0), 400.0, 0.05, "the same total length")
	assert_false(l.is_beam_blocked(0), "a reflection is not a stop")
	assert_eq(walls, [], "no laser_wall_hit while it reflects")


func test_a_wall_is_never_a_target_even_on_the_collision_mask() -> void:
	var wall := make_wall(Vector2(200, 0), Vector2(20, 200), 4) # on the targets' layer too
	var front := make_area(Vector2(100, 0), Vector2(20, 60))
	await idle(2)
	var hits: Array = []
	factory.laser_hit.connect(func(t: Object, _l, _i, _p) -> void: hits.append(t))
	_fire(_walled(4))
	_step(1)
	assert_eq(hits, [front], "the wall stops the beam, it is never hit")
	assert_ne(wall, null)


func test_no_wall_mask_passes_through_walls() -> void:
	make_wall(Vector2(200, 0), Vector2(20, 200), 16)
	await idle(2)
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0))
	_step(1)
	assert_false(l.is_beam_blocked(0), "not blocked")
	assert_almost_eq(l.get_beam_length(0), 400.0, 0.01, "full length")


func test_a_removed_wall_unblocks_and_a_new_block_reports_again() -> void:
	var wall := make_wall(Vector2(200, 0), Vector2(20, 200), 16)
	await idle(2)
	var l := _fire(_walled())
	_step(1)
	wall.position = Vector2(200, 1000)
	await idle(1) # the physics space sees the move
	tick = 1
	_step(1)
	assert_false(l.is_beam_blocked(0), "unblocked")
	wall.position = Vector2(150, 0)
	await idle(1)
	_step(1)
	assert_true(l.is_beam_blocked(0), "blocked again")
	assert_eq(walls.size(), 2, "a new laser_wall_hit for the new block")
	assert_almost_eq(walls[1][1].x, 140.0, 0.5, "at the new wall")


func test_a_growing_beam_grows_up_to_the_wall() -> void:
	make_wall(Vector2(250, 0), Vector2(20, 200), 16)
	await idle(2)
	var d := _walled()
	d.extend_speed = 1600.0 # 100 px per tick
	var l := _fire(d)
	var lengths: Array = []
	for i in 4:
		_step(1)
		lengths.append(snappedf(l.get_beam_length(0), 0.01))
	assert_eq(lengths, [100.0, 200.0, 240.0, 240.0], "grows 100 px per tick, then the wall stops it")
	assert_eq(walls.size(), 1, "one block when it reached the wall")


func test_a_reflected_beam_pierces_targets_on_every_segment() -> void:
	# A corridor (faces at y = -40 and 40): a 45 deg beam reflects at x = 40,
	# 120, ..., 600 and stops at x = 680, so its 9 segments cross y = 0 at
	# x = 80, 160, ..., 640. One small target sits on each crossing (segments
	# 2..9, past the first group of 8), a decoy 45 px off the beam.
	make_wall(Vector2(400, -50), Vector2(1200, 20), 16)
	make_wall(Vector2(400, 50), Vector2(1200, 20), 16)
	var expected: Array = []
	for j in range(1, 9):
		var t := make_area(Vector2(80 * j, 0), Vector2(8, 8))
		t.name = "T%d" % j
		expected.append(str(t.name))
	var decoy := make_area(Vector2(40, -30), Vector2(8, 8))
	decoy.name = "D"
	await idle(2)
	var starts: Array = []
	factory.laser_pierce_started.connect(func(t: Object, _l, _i, p: Vector2) -> void: starts.append([str(t.name), p.round()]))
	var d := _walled()
	d.length = 2000.0
	d.reflections = 8
	var l := _fire(d, deg_to_rad(45.0))
	_step(1)
	var pts := l.get_beam_points(0)
	assert_eq(pts.size(), 10, "muzzle, 8 reflections, the stop")
	assert_true(l.is_beam_blocked(0), "the ninth wall stops it")
	assert_almost_eq(pts[9].x, 680.0, 0.05, "at x = 680")
	var names: Array = []
	for t in l.get_pierced_targets(0):
		names.append(str(t.name))
	assert_eq(names, expected, "every crossing target, nearest along the beam first, never the decoy")
	var want: Array = []
	for j in range(1, 9):
		want.append(["T%d" % j, Vector2(80 * j, 0)])
	assert_eq(starts, want, "each started where it sits on the beam")


func test_a_long_curvy_body_pierces_targets_along_its_whole_length() -> void:
	# A straight curvy body (no wave) 300 px long at 25 px per tick: 12
	# segments. After 16 ticks it spans x = 100..400 and covers A near its
	# tail, B and C near its head (the second group of segments).
	var names := ["A", "B", "C"]
	var xs := [120.0, 310.0, 390.0]
	for k in 3:
		var t := make_area(Vector2(xs[k], 0), Vector2(8, 8))
		t.name = names[k]
	await idle(2)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 300.0)
	d.type = LaserData2D.LASER_CURVY
	d.speed = 400.0
	var l := _fire(d)
	_step(16)
	assert_gt(l.get_beam_points(0).size(), 9, "more than one group of segments")
	assert_almost_eq(l.get_beam_start(0).x, 100.0, 0.05, "the tail")
	assert_almost_eq(l.get_beam_end(0).x, 400.0, 0.05, "the head")
	var got: Array = []
	for t in l.get_pierced_targets(0):
		got.append(str(t.name))
	assert_eq(got, names, "all three, from the tail to the head")
	_step(2)
	got.clear()
	for t in l.get_pierced_targets(0):
		got.append(str(t.name))
	assert_eq(got, ["B", "C"], "the tail passed A (x = 150 now)")
