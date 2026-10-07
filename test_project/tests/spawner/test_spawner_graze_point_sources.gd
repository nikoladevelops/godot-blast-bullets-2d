extends BlastTest
## The point graze target sources: Mouse (the cursor, in the bullets' canvas
## coordinates) and Global Positions (every entry of graze_global_positions).
## Serialized ids 4 and 5, the positions setter rejects non-finite entries
## and keeps the old value, the inspector shows only the point knobs (no
## filter group, no update interval: points are read every tick), an empty
## positions list is named in the setup warnings. A point grazes like a
## node but the signals carry a null target (spawner and factory); a visit
## belongs to its point's index, so moving a point keeps it and removing it
## ends it silently. resolve_graze_targets() returns the points as Vector2,
## refresh_graze_targets() counts them. Edits reach bullets in flight,
## orphaned volleys keep the source, there is no cap (300 points, the slab
## path past 8), the runtime ring preview draws around each point and
## follows the cursor, the cursor is read through the camera, a switch
## between node and point sources re-anchors open visits, and both settings
## survive a scene round trip.

const MOUSE := BulletSpawner2D.GRAZE_SOURCE_MOUSE
const POSITIONS := BulletSpawner2D.GRAZE_SOURCE_GLOBAL_POSITIONS


## Graze settings for `source` on `sp`: one 20 px ring.
func _configure(sp: BulletSpawner2D, source: int, points := PackedVector2Array()) -> BulletSpawner2D:
	sp.graze_zones = [H.make_graze_zone([20.0])]
	sp.graze_enabled = true
	sp.graze_target_source = source
	sp.graze_global_positions = points
	return sp


## A spawner at `at` firing one resting bullet (circle r4) there.
func _spawner(source: int, points := PackedVector2Array(), at := Vector2.ZERO) -> BulletSpawner2D:
	var d := H.make_volley_data(1, 0.0, 60.0)
	d.collision_shape = H.make_circle_shape(4.0)
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.position = at
	return _configure(sp, source, points)


func _shoot(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fired")
	return got[0] if not got.is_empty() else null


func _centers(sp: BulletSpawner2D) -> Array:
	var out: Array = []
	for c in sp.debug_get_graze_preview_circles():
		out.append([c["center"], c["target_id"]])
	return out


func test_ids_property_defaults_and_setter() -> void:
	var sp := make_spawner()
	assert_eq([MOUSE, POSITIONS], [4, 5], "serialized source ids")
	for m in ["get_graze_global_positions", "set_graze_global_positions"]:
		assert_true(sp.has_method(m), m + " is bound")
	assert_eq(sp.graze_global_positions, PackedVector2Array(), "no positions by default")
	sp.graze_global_positions = PackedVector2Array([Vector2(1, 2), Vector2(3, 4)])
	sp.graze_global_positions = PackedVector2Array([Vector2(5, 6), Vector2(NAN, 0)])
	sp.graze_global_positions = PackedVector2Array([Vector2(0, INF)])
	expect_error_sequence([
		"BulletSpawner2D: graze_global_positions must hold finite positions only, keeping the old value.",
		"BulletSpawner2D: graze_global_positions must hold finite positions only, keeping the old value.",
	])
	assert_eq(sp.graze_global_positions, PackedVector2Array([Vector2(1, 2), Vector2(3, 4)]), "old value kept")
	sp.graze_global_positions = PackedVector2Array()
	assert_eq(sp.graze_global_positions, PackedVector2Array(), "an empty list is fine")


func test_the_inspector_shows_only_the_point_knobs() -> void:
	var sp := _spawner(MOUSE)
	var node_knobs := ["graze_node_group", "graze_filter_group", "graze_target_path", "graze_node_name", "graze_node_name_match_mode", "graze_node_name_case_sensitive", "graze_children_parent_path", "graze_children_recursive", "graze_update_interval"]
	for source in [MOUSE, POSITIONS]:
		sp.graze_target_source = source
		assert_true(is_editor_visible(sp, &"graze_target_source"), "the source shows")
		assert_eq(is_editor_visible(sp, &"graze_global_positions"), source == POSITIONS, "positions under source %d" % source)
		for k in node_knobs:
			assert_false(is_editor_visible(sp, StringName(k)), "%s hides under source %d (points are read every tick)" % [k, source])


func test_setup_warnings_name_an_empty_positions_list() -> void:
	var sp := _spawner(POSITIONS)
	var graze_warnings := func() -> Array: return Array(sp.get_setup_warnings()).filter(func(w: String) -> bool: return w.begins_with("graze_target_source"))
	assert_eq(graze_warnings.call(), ["graze_target_source is Global Positions but graze_global_positions is empty: no graze target is found."], "empty list")
	sp.graze_global_positions = PackedVector2Array([Vector2(10, 0)])
	assert_eq(graze_warnings.call(), [], "a point")
	sp.graze_target_source = MOUSE
	assert_eq(graze_warnings.call(), [], "the mouse needs no setting")


func test_each_position_grazes_with_a_null_target() -> void:
	var sp := _spawner(POSITIONS, PackedVector2Array([Vector2(500, 0), Vector2(10, 0)]))
	var on_spawner := H.record_graze(sp)
	var on_factory := H.record_graze(factory)
	var v := _shoot(sp)
	step_factory()
	assert_eq(H.graze_kinds(on_spawner), ["enter:0:0"], "the second point grazes")
	assert_null(on_spawner[0][1], "no node: a null target")
	assert_eq(H.graze_kinds(on_factory), ["enter:0:0"], "bubbled to the factory")
	assert_null(on_factory[0][1], "null there too")
	sp.graze_global_positions = PackedVector2Array([Vector2(500, 0), Vector2(14, 0)])
	step_factory()
	assert_eq(H.graze_kinds(on_spawner), ["enter:0:0"], "a point moved within the zone keeps its visit")
	assert_true(v.is_bullet_inside_graze(0, 0), "inside")
	sp.graze_global_positions = PackedVector2Array([Vector2(500, 0), Vector2(300, 0)])
	step_factory()
	assert_eq(H.graze_kinds(on_spawner), ["enter:0:0", "exit:0:0"], "moved away: the visit exits")
	assert_null(on_spawner[1][1], "with a null target")


func test_removing_the_point_of_a_visit_ends_it_silently() -> void:
	var sp := _spawner(POSITIONS, PackedVector2Array([Vector2(10, 0), Vector2(15, 0)]))
	var log := H.record_graze(sp)
	var v := _shoot(sp)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "one graze for the zone")
	sp.graze_global_positions = PackedVector2Array([Vector2(15, 0)])
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "index 0 now sits at 15 px: still inside, same visit")
	assert_true(v.is_bullet_inside_graze(0, 0), "inside")
	sp.graze_global_positions = PackedVector2Array()
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "no point left: the visit ended silently")
	assert_false(v.is_bullet_inside_graze(0, 0), "no visit")


func test_resolve_and_refresh_report_the_points() -> void:
	var sp := _spawner(POSITIONS, PackedVector2Array([Vector2(500, 0), Vector2(10, 0)]))
	assert_eq(sp.resolve_graze_targets(), [Vector2(500, 0), Vector2(10, 0)], "the points, in array order")
	assert_eq(sp.refresh_graze_targets(), 2, "counted")
	sp.graze_global_positions = PackedVector2Array()
	assert_eq(sp.resolve_graze_targets(), [], "none")
	assert_eq(sp.refresh_graze_targets(), 0, "zero")
	sp.graze_target_source = MOUSE
	mouse_to(Vector2(40, -30))
	assert_eq(sp.resolve_graze_targets(), [Vector2(40, -30)], "the cursor")
	assert_eq(sp.refresh_graze_targets(), 1, "one")


func test_edits_reach_bullets_in_flight() -> void:
	var sp := _spawner(POSITIONS, PackedVector2Array([Vector2(500, 0)]))
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory(2)
	assert_eq(log, [], "far")
	sp.graze_global_positions = PackedVector2Array([Vector2(10, 0)])
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the flying bullet tests the new point next tick")


func test_the_filter_group_and_the_interval_never_apply_to_points() -> void:
	var sp := _spawner(POSITIONS, PackedVector2Array([Vector2(500, 0)]))
	sp.graze_filter_group = &"nobody"
	sp.graze_update_interval = 10.0
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory(2)
	sp.graze_global_positions = PackedVector2Array([Vector2(10, 0)])
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "points have no group and are read every tick")


func test_the_cursor_is_read_every_tick_whatever_the_interval() -> void:
	mouse_to(Vector2(500, 0))
	var sp := _spawner(MOUSE)
	sp.graze_update_interval = 10.0
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory(2)
	assert_eq(log, [], "far")
	mouse_to(Vector2(10, 0))
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the cursor moved onto the bullet: grazed next tick, no rescan wait")


func test_points_survive_the_handlers_of_earlier_volleys_in_the_sweep() -> void:
	# A handler is user code: later volleys of the same sweep re-validate the
	# shared target list (freed nodes drop). Points never die.
	for source in [POSITIONS, MOUSE]:
		mouse_to(Vector2(10, 0))
		var sp := _spawner(source, PackedVector2Array([Vector2(10, 0)]))
		var log := H.record_graze(sp)
		_shoot(sp)
		_shoot(sp)
		step_factory()
		assert_eq(log.size(), 2, "source %d: both volleys grazed in the same tick" % source)
		assert_ne(log[0][2], log[1][2], "source %d: one event per volley" % source)
		factory.clear_active_bullets()


func test_there_is_no_cap() -> void:
	var points := PackedVector2Array()
	for i in 300:
		points.append(Vector2(1000 + 10 * i, 0))
	points.append(Vector2(10, 0)) # the 301st, past the slab threshold
	var sp := _spawner(POSITIONS, points)
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the 301st point grazes")
	assert_eq(sp.refresh_graze_targets(), 301, "every point counts")


func test_orphaned_volleys_keep_the_source() -> void:
	for source in [POSITIONS, MOUSE]:
		mouse_to(Vector2(10, 0))
		var d := H.make_volley_data(1, 0.0, 60.0)
		d.collision_shape = H.make_circle_shape(4.0)
		var sp := BulletSpawner2D.new()
		sp.set_shooting_enabled(false)
		sp.set_homing_enabled(false)
		sp.set_spawn_data(d)
		sp.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
		sp.helper_bullets_amount = 1
		add_child(sp)
		sp.set_bullet_factory(factory)
		_configure(sp, source, PackedVector2Array([Vector2(10, 0)]))
		var log := H.record_graze(factory)
		_shoot(sp)
		sp.free()
		step_factory()
		assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d: the orphaned volley still grazes" % source)
		assert_null(log[0][1], "source %d: a null target" % source)
		factory.clear_active_bullets()
		for c in factory.get_signal_connection_list("bullet_grazed") + factory.get_signal_connection_list("bullet_graze_exited"):
			factory.disconnect(c["signal"].get_name(), c["callable"])


func test_the_mouse_grazes_with_a_null_target() -> void:
	mouse_to(Vector2(10, 0))
	var sp := _spawner(MOUSE)
	var log := H.record_graze(sp)
	var v := _shoot(sp)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the cursor grazes")
	assert_null(log[0][1], "a null target")
	mouse_to(Vector2(15, 5))
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "moving inside keeps the visit")
	mouse_to(Vector2(300, 0))
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "exit:0:0"], "moving away exits")
	assert_false(v.is_bullet_inside_graze(0, 0), "outside")


func test_the_cursor_is_read_through_the_camera() -> void:
	# The bullet sits far from the screen origin; with the camera on it, the
	# cursor's canvas position (not its screen position) is what grazes.
	var cam := Camera2D.new()
	add(cam)
	cam.position = Vector2(2000, 1000)
	cam.make_current()
	await idle(2)
	mouse_to(Vector2(2010, 1000))
	var sp := _spawner(MOUSE, PackedVector2Array(), Vector2(2000, 1000))
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "grazed where the cursor is in the world")


func test_the_runtime_preview_draws_each_point_and_follows_the_cursor() -> void:
	var sp := _spawner(POSITIONS, PackedVector2Array([Vector2(100, 0), Vector2(200, 50)]))
	sp.graze_show_preview = true
	sp.graze_preview_during_runtime = true
	await idle(2)
	assert_eq(_centers(sp), [[Vector2(100, 0), 0], [Vector2(200, 50), 0]], "a ring around each point (no node: target_id 0)")
	sp.graze_target_source = MOUSE
	mouse_to(Vector2(-40, 60))
	await idle(2)
	assert_eq(_centers(sp), [[Vector2(-40, 60), 0]], "around the cursor")
	mouse_to(Vector2(70, -20))
	await idle(2)
	assert_eq(_centers(sp), [[Vector2(70, -20), 0]], "following it")


func test_switching_between_node_and_point_sources_re_anchors_visits() -> void:
	var t := make_graze_target(Vector2(10, 0), &"nodes")
	var sp := _spawner(BulletSpawner2D.GRAZE_SOURCE_NODE_GROUP)
	sp.graze_node_group = &"nodes"
	var log := H.record_graze(sp)
	var v := _shoot(sp)
	step_factory()
	assert_eq(log[0][1], t, "a node visit")
	sp.graze_global_positions = PackedVector2Array([Vector2(-12, 0)])
	sp.graze_target_source = POSITIONS
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the visit moved to the point it is inside: no exit, no new graze")
	assert_true(v.is_bullet_inside_graze(0, 0), "still inside")
	mouse_to(Vector2(400, 0))
	sp.graze_target_source = MOUSE
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the cursor is far: the visit ended silently")
	assert_false(v.is_bullet_inside_graze(0, 0), "no visit")


func test_point_settings_survive_a_scene_round_trip() -> void:
	var src := BulletSpawner2D.new()
	src.set_shooting_enabled(false)
	src.graze_enabled = true
	src.graze_target_source = POSITIONS
	src.graze_global_positions = PackedVector2Array([Vector2(1, 2), Vector2(-3, 4.5)])
	var scene := PackedScene.new()
	assert_eq(scene.pack(src), OK, "packs")
	src.free()
	var back := scene.instantiate() as BulletSpawner2D
	assert_eq(back.graze_target_source, POSITIONS, "source survives")
	assert_eq(back.graze_global_positions, PackedVector2Array([Vector2(1, 2), Vector2(-3, 4.5)]), "positions survive")
	back.free()
