extends BlastTest
## The collision debugger draws lasers too: while the factory's debugger is
## enabled, an internal overlay shows every box the active lasers' contact
## queries test this tick (one per segment of every unspent beam, in a
## hitting phase: none while charging, none for a spent head), in the
## debugger color at debugger_z_index. The boxes are the queries themselves:
## a target just inside a box is hit, just outside is not. Disabling hides and
## empties the overlay at once; a finished laser leaves no box behind.

const DT := 0.0625


func _boxes() -> Array:
	return factory.debug_get_laser_debug_boxes()["boxes"]


func _fire(d: LaserData2D, poses: Array[Transform2D]) -> LaserVolley2D:
	var l := factory.spawn_laser(d, poses)
	assert_not_null(l, "fired")
	return l


func test_off_by_default_nothing_is_drawn() -> void:
	_fire(H.make_laser(0.0, 0.0, 0.0, 0.0), [Transform2D.IDENTITY] as Array[Transform2D])
	step_factory(2, DT)
	assert_false(factory.is_debugger_enabled, "the debugger is off by default")
	assert_eq(_boxes(), [], "no overlay box")
	assert_false(factory.debug_get_laser_debug_boxes()["visible"], "nothing shown")


func test_every_queried_segment_is_one_box() -> void:
	factory.is_debugger_enabled = true
	factory.debugger_color = Color(0, 1, 0, 1)
	make_wall(Vector2(200, 0), Vector2(20, 400), 16)
	await idle(2)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 300.0)
	d.width = 12.0
	d.wall_mask = 16
	d.reflections = 1 # one bounce off the wall: two segments
	var l := _fire(d, [Transform2D.IDENTITY] as Array[Transform2D])
	step_factory(1, DT)
	var info: Dictionary = factory.debug_get_laser_debug_boxes()
	assert_true(info["visible"], "shown")
	assert_eq(info["color"], Color(0, 1, 0, 1), "in the debugger color")
	assert_eq(info["z_index"], factory.debugger_z_index, "at the debugger's z order")
	var pts := l.get_beam_points(0)
	assert_eq(pts.size(), 3, "a reflected beam: two segments")
	var boxes := _boxes()
	assert_eq(boxes.size(), 2, "one box per segment")
	for k in 2:
		var box: Dictionary = boxes[k]
		var a: Vector2 = pts[k]
		var c: Vector2 = pts[k + 1]
		assert_almost_eq((box["transform"] as Transform2D).origin, (a + c) * 0.5, Vector2(0.001, 0.001), "box %d centered on its segment" % k)
		assert_almost_eq(angle_difference((box["transform"] as Transform2D).get_rotation(), (c - a).angle()), 0.0, 0.0001, "and turned along it")
		assert_almost_eq(box["extents"], Vector2(a.distance_to(c) * 0.5, 6.0), Vector2(0.001, 0.001), "half its length, half the width")
		assert_eq([box["laser"], box["beam"]], [l.get_instance_id(), 0], "naming its laser and beam")
	await idle(1) # a real frame renders the overlay
	assert_gt(int(factory.debug_get_laser_debug_boxes()["draws"]), 0, "drawn")


func test_the_boxes_are_what_the_queries_test() -> void:
	factory.is_debugger_enabled = true
	var inside := make_area(Vector2(100, 6.4), Vector2(20, 0.6)) # 6.1 .. 6.7 px off the axis
	var outside := make_area(Vector2(160, 7.0), Vector2(20, 0.6)) # 6.7 .. 7.3
	await idle(2)
	var hits: Array = []
	factory.laser_hit.connect(func(t, _l, _b, _p) -> void: hits.append(t))
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 300.0)
	d.width = 13.0 # the box reaches 6.5 px off the axis
	_fire(d, [Transform2D.IDENTITY] as Array[Transform2D])
	step_factory(1, DT)
	var box: Dictionary = _boxes()[0]
	assert_almost_eq((box["extents"] as Vector2).y, 6.5, 0.0001, "the overlay box reaches 6.5 px")
	assert_eq(hits, [inside], "the target inside it is hit, the one past its edge is not")
	assert_ne(outside, null)


func test_charging_spent_and_finished_lasers_show_no_box() -> void:
	factory.is_debugger_enabled = true
	var charging := _fire(H.make_laser(1.0, 0.0, 0.0, 0.0), [Transform2D.IDENTITY] as Array[Transform2D])
	step_factory(1, DT)
	assert_eq(_boxes(), [], "a charging laser queries nothing")
	charging.end_laser(false)
	var bolt := H.make_laser(0.0, 0.0, 0.0, 0.0, 40.0)
	bolt.type = LaserData2D.LASER_BOLT
	bolt.speed = 1600.0
	factory.despawn_bounds_rect = Rect2(-50, -50, 300, 100)
	factory.despawn_bounds_enabled = true
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO), Transform2D(PI / 2.0, Vector2.ZERO)]
	var l := _fire(bolt, poses)
	step_factory(2, DT)
	assert_true(_boxes().all(func(b): return int(b["beam"]) == 0), "the head that left the bounds is spent: only beam 0 keeps boxes")
	assert_gt(_boxes().size(), 0, "beam 0 still flies")
	await idle(1) # rendered with boxes
	var draws := int(factory.debug_get_laser_debug_boxes()["draws"])
	l.end_laser(false)
	step_factory(2, DT)
	assert_eq(_boxes(), [], "finished: no box left behind")
	await idle(1)
	assert_gt(int(factory.debug_get_laser_debug_boxes()["draws"]), draws, "redrawn empty")


func test_disabling_hides_the_overlay_at_once_and_settings_apply_live() -> void:
	factory.is_debugger_enabled = true
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)
	d.sweep_speed_degrees = 160.0 # 10 degrees per tick
	_fire(d, [Transform2D.IDENTITY] as Array[Transform2D])
	step_factory(1, DT)
	var before: float = (_boxes()[0]["transform"] as Transform2D).get_rotation()
	step_factory(1, DT)
	assert_almost_eq((_boxes()[0]["transform"] as Transform2D).get_rotation() - before, deg_to_rad(10.0), 0.0001, "the box follows the sweep every tick")
	factory.debugger_z_index = 42
	assert_eq(factory.debug_get_laser_debug_boxes()["z_index"], 42, "a new z order applies at once")
	factory.is_debugger_enabled = false
	var info: Dictionary = factory.debug_get_laser_debug_boxes()
	assert_eq([info["visible"], info["boxes"]], [false, []], "hidden and emptied at once")
	step_factory(2, DT)
	assert_eq(_boxes(), [], "stays empty while off")


func test_a_script_freeing_the_overlay_never_breaks_it() -> void:
	factory.is_debugger_enabled = true
	_fire(H.make_laser(0.0, 0.0, 0.0, 0.0), [Transform2D.IDENTITY] as Array[Transform2D])
	step_factory(1, DT)
	var layer: Node = factory.get_node_or_null(^"~BlastBulletsLaserDebug")
	assert_not_null(layer, "an internal child of the factory")
	assert_false(factory.get_children().has(layer), "never listed by get_children()")
	assert_null(layer.owner, "owner-less: never saved")
	layer.free()
	step_factory(1, DT)
	assert_eq(_boxes().size(), 1, "recreated at the next tick")
	expect_no_errors("freeing it reported nothing")
