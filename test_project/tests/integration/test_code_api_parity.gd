extends BlastTest
## The code API reaches what the spawner groups do, without a spawner.
## BulletTargets2D names WHO (a node group + filter, one node, a parent's
## children, nodes by name, fixed points, the mouse; update_interval) and is
## accepted wherever a target group was: graze_set_zones and
## sound_set_effects on volleys and lasers, plus LaserVolley2D.track (each
## beam aims at the target nearest its muzzle). Its detector is shared, so
## edits reach armed bullets in flight. Parity: a factory volley armed by code
## grazes exactly like a spawner-fired twin, a tracking laser aims exactly like
## a laser spawner's Node Group targeting.

const DT := 0.0625
const GROUP := &"code_api_targets"


func _target(pos: Vector2, group := GROUP, name := "") -> Node2D:
	var n := make_graze_target(pos, group)
	if name != "":
		n.name = name
	return n


func _names(list: Array) -> Array:
	var out: Array = []
	for t in list:
		out.append(str((t as Node).name) if t is Node else str(t))
	out.sort()
	return out


func test_every_source_resolves_its_targets() -> void:
	var a := _target(Vector2(10, 0), GROUP, "Alpha")
	var b := _target(Vector2(20, 0), GROUP, "Beta")
	b.add_to_group(&"code_api_filter")
	var parent := Node2D.new()
	add(parent)
	var child := Node2D.new()
	child.name = "Child"
	parent.add_child(child)
	await idle(1)
	assert_eq(_names(BulletTargets2D.from_group(GROUP).resolve(factory)), ["Alpha", "Beta"], "the group's members")
	assert_eq(_names(BulletTargets2D.from_group(GROUP, &"code_api_filter").resolve(factory)), ["Beta"], "the filter group")
	assert_eq(BulletTargets2D.from_node(a).resolve(factory), [a], "one node")
	assert_eq(BulletTargets2D.from_children(parent).resolve(factory), [child], "a parent's children")
	assert_eq(_names(BulletTargets2D.from_name("lph").resolve(factory)), ["Alpha"], "by name (Contains)")
	assert_eq(BulletTargets2D.from_positions(PackedVector2Array([Vector2(1, 2), Vector2(3, 4)])).resolve(factory), [Vector2(1, 2), Vector2(3, 4)], "fixed points")
	mouse_to(Vector2(50, 60))
	assert_eq(BulletTargets2D.from_mouse().resolve(factory), [Vector2(50, 60)], "the cursor")
	assert_eq([BulletTargets2D.from_group(GROUP).get_source(), BulletTargets2D.from_mouse().get_source()], [BulletTargets2D.SOURCE_NODE_GROUP, BulletTargets2D.SOURCE_MOUSE], "sources")


func test_bad_input_is_refused_with_one_error() -> void:
	var gone := Node2D.new()
	gone.free()
	assert_null(BulletTargets2D.from_group(&""), "empty group")
	assert_null(BulletTargets2D.from_node(gone), "freed node")
	assert_null(BulletTargets2D.from_children(null), "no parent")
	assert_null(BulletTargets2D.from_name(""), "empty name")
	assert_null(BulletTargets2D.from_name("x", 7), "bad match mode")
	assert_null(BulletTargets2D.from_positions(PackedVector2Array([Vector2(NAN, 0)])), "non-finite point")
	expect_error_sequence([
		"BulletTargets2D.from_group: group is empty (name the group whose nodes are the targets), nothing created.",
		"BulletTargets2D.from_node: node must be a live Node2D, nothing created.",
		"BulletTargets2D.from_children: parent must be a live Node, nothing created.",
		"BulletTargets2D.from_name: name is empty, nothing created.",
		"BulletTargets2D.from_name: match_mode must be 0 (Exact), 1 (Contains), 2 (Starts With) or 3 (Ends With), nothing created.",
		"BulletTargets2D.from_positions: positions must hold finite positions only, nothing created.",
	])
	var t := BulletTargets2D.from_mouse()
	t.update_interval = -1.0
	t.positions = PackedVector2Array([Vector2(INF, 0)])
	assert_eq(t.resolve(null), [], "resolve needs a factory")
	expect_error_sequence([
		"BulletTargets2D: update_interval must be finite and >= 0 (0 scans every tick), keeping the old value.",
		"BulletTargets2D: positions must hold finite positions only, keeping the old value.",
		"BulletTargets2D.resolve: factory must be a live BulletFactory2D, nothing resolved.",
	])
	var v := graze_volley([Transform2D.IDENTITY], 0.0)
	assert_false(v.graze_set_zones([H.make_graze_zone([10.0])], 5), "a number is no target")
	assert_false(v.sound_set_effects([], factory), "a factory is no listener source")
	expect_error_sequence([
		"graze_set_zones: target_group must be a group name or a BulletTargets2D, nothing changed.",
		"sound_set_effects: listener_group must be a group name or a BulletTargets2D, nothing changed.",
	])


func test_a_factory_volley_grazes_the_handles_targets_and_follows_its_edits() -> void:
	var only := _target(Vector2(100, 10))
	_target(Vector2(100, -10)) # same group, not named by the handle
	await idle(1)
	var v := graze_volley([Transform2D(0.0, Vector2(0, 0))], 1600.0) # 100 px per tick along +x
	var log := H.record_graze(factory)
	var targets := BulletTargets2D.from_node(only)
	assert_true(v.graze_set_zones([H.make_graze_zone([15.0])], targets), "armed with the handle")
	targets = null # the volley keeps the detector
	step_factory(2, DT)
	assert_eq(log.filter(func(e): return e[0] == "enter").map(func(e): return e[1]), [only], "only the handle's node grazed")
	log.clear()
	var points := BulletTargets2D.from_positions(PackedVector2Array([Vector2(1000, 1000)]))
	var w := graze_volley([Transform2D(0.0, Vector2(0, 200))], 1600.0)
	assert_true(w.graze_set_zones([H.make_graze_zone([15.0])], points), "armed with points")
	step_factory(1, DT)
	assert_eq(log, [], "nothing near the point")
	points.positions = PackedVector2Array([Vector2(w.get_bullet_transform(0).origin.x + 100.0, 205.0)])
	step_factory(1, DT)
	assert_eq(log.size(), 1, "the moved point is grazed by the bullet in flight")
	assert_null(log[0][1], "a point target has a null target in the signal")


func test_a_volley_and_a_laser_hear_through_the_handles_listeners() -> void:
	var ear := _target(Vector2(0, 0), &"code_api_ears")
	await idle(1)
	factory.debug_set_sound_log_enabled(true)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	s.positional = true
	s.max_distance = 100.0
	var listeners := BulletTargets2D.from_group(&"code_api_ears")
	var heard := graze_volley([Transform2D(0.0, Vector2(50, 0))], 0.0)
	assert_true(heard.sound_set_effects([s], listeners), "armed with the handle's listeners")
	heard.clear_all_bullets()
	step_factory(1, DT)
	assert_eq(factory.debug_get_sound_log().size(), 1, "50 px from the listener: it plays")
	factory.debug_clear_sound_log()
	ear.position = Vector2(1000, 0)
	listeners.refresh()
	var far := graze_volley([Transform2D(0.0, Vector2(50, 0))], 0.0)
	assert_true(far.sound_set_effects([s], listeners), "armed alike")
	far.clear_all_bullets()
	step_factory(1, DT)
	assert_eq(factory.debug_get_sound_log().size(), 0, "the listener moved away: out of range, nothing plays")
	var l := factory.spawn_laser(H.make_laser(), [Transform2D.IDENTITY] as Array[Transform2D])
	assert_true(l.sound_set_effects([H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_HIT)], listeners), "a laser takes the handle too")
	assert_true(l.graze_set_zones([H.make_graze_zone([10.0])], BulletTargets2D.from_node(ear)), "and for graze")
	assert_true(l.is_sound_armed() and l.is_graze_armed(), "both armed")


func test_a_code_volley_grazes_like_a_spawner_twin() -> void:
	for p in [Vector2(120, 8), Vector2(260, -12), Vector2(400, 30)]:
		_target(p)
	await idle(1)
	var zone := H.make_graze_zone([10.0, 25.0])
	var sp := make_spawner(H.make_volley_data(1, 1600.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM, 1)
	sp.helper_custom_transforms = [Transform2D(0.0, Vector2(0, 0))]
	sp.graze_enabled = true
	sp.graze_zones = [zone]
	sp.graze_node_group = GROUP
	var twin_log := H.record_graze(sp)
	assert_true(sp.shoot_once(), "the spawner twin fires")
	var code := graze_volley([Transform2D(0.0, Vector2(0, 0))], 1600.0)
	var code_log: Array = []
	factory.bullet_grazed.connect(func(t, v, i, _z, r) -> void:
		if v == code:
			code_log.append([t, i, r]))
	assert_true(code.graze_set_zones([zone], BulletTargets2D.from_group(GROUP)), "the code volley is armed alike")
	step_factory(6, DT)
	var twin: Array = []
	for e in twin_log:
		if e[0] == "enter":
			twin.append([e[1], e[3], e[5]])
	assert_gt(twin.size(), 0, "the twin grazed")
	assert_eq(code_log, twin, "the same targets, bullets and rings in the same order")


func test_a_tracking_laser_aims_like_a_laser_spawner_twin() -> void:
	var a := _target(Vector2(200, 100))
	var b := _target(Vector2(-150, 50))
	await idle(1)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 400.0)
	d.aim = LaserData2D.LASER_AIM_LOCK_ON
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2(150, 0)), Transform2D(PI, Vector2(-100, 0))]
	var sp := LaserSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.laser_data = d
	sp.pattern_source = LaserSpawner2D.PATTERN_FROM_HELPER_CUSTOM
	sp.helper_custom_transforms = Array(poses)
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_GROUP
	sp.laser_target_group = GROUP
	add(sp)
	sp.set_bullet_factory(factory)
	var fired: Array = []
	sp.laser_fired.connect(func(l, _n) -> void: fired.append(l))
	assert_true(sp.shoot_once(), "the spawner twin fires")
	var code := factory.spawn_laser(d, poses)
	assert_true(code.track(BulletTargets2D.from_group(GROUP)), "the code laser tracks")
	assert_true(code.is_tracking(), "tracking")
	step_factory(1, DT)
	var twin: LaserVolley2D = fired[0]
	for i in 2:
		assert_almost_eq(code.get_beam_end(i), twin.get_beam_end(i), Vector2(0.001, 0.001), "beam %d ends where the twin's does" % i)
	assert_almost_eq(code.get_beam_end(0), a.global_position, Vector2(0.01, 0.01), "beam 0 locks on its nearest target")
	assert_almost_eq(code.get_beam_end(1), b.global_position, Vector2(0.01, 0.01), "beam 1 on its own nearest")
	a.position = Vector2(-140, 60) # both now nearest to beam 1's muzzle region
	var slow := factory.spawn_laser(d, poses)
	assert_true(slow.track(BulletTargets2D.from_group(GROUP), 0.25), "picks every 0.25 s")
	step_factory(1, DT)
	var first: Vector2 = slow.get_beam_end(0)
	b.position = Vector2(400, 0)
	step_factory(1, DT)
	assert_almost_eq(slow.get_beam_end(0), first, Vector2(0.01, 0.01), "between picks a beam keeps its target")
	step_factory(4, DT)
	assert_almost_eq(slow.get_beam_end(0), b.global_position, Vector2(0.01, 0.01), "the next pick moves to the nearest")
	assert_true(slow.track(null), "null stops")
	assert_false(slow.is_tracking(), "not tracking")
	assert_false(slow.track(factory), "not a handle")
	assert_false(slow.track(BulletTargets2D.from_mouse(), NAN), "bad interval")
	expect_error_sequence([
		"track: targets must be a BulletTargets2D or null, nothing changed.",
		"track: retarget_interval must be finite and >= 0 (0 picks every tick), nothing changed.",
	])
	assert_true(twin.track(BulletTargets2D.from_positions(PackedVector2Array([Vector2(150, -300)])), 10.0), "a spawner laser may track too (next pick in 10 s)")
	step_factory(1, DT) # the first pick
	sp.retarget_live_lasers()
	step_factory(1, DT)
	assert_almost_eq(twin.get_beam_end(0), Vector2(150, -300), Vector2(0.01, 0.01), "its spawner's retargets leave a tracking laser alone")
