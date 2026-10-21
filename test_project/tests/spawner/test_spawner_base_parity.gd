extends BlastTest
## PatternSpawner2D owns WHERE and WHEN to fire, for every spawner type: the
## same scenario on a BulletSpawner2D and a LaserSpawner2D produces the SAME
## timeline (shots and their poses, skips and their reasons, bursts,
## telegraphs, pattern lists, movement events) once the type's own words are
## normalized (volley_fired / laser_fired, no_spawn_data / no_laser_data).
## Every scenario also proves it did something (no vacuous parity).

const FRAMES := 60

var log: Array = []
var frame := 0
var current: PatternSpawner2D


## The spawner of `kind` ("bullet" / "laser"): 4 slots on a 20 px ring,
## shooting off until the scenario says so, at (100, 100).
func _spawner(kind: String, configure: Callable = Callable()) -> PatternSpawner2D:
	var sp: PatternSpawner2D
	if kind == "bullet":
		var b := BulletSpawner2D.new()
		b.set_homing_enabled(false)
		b.set_spawn_data(H.make_volley_data(4, 0.0, 30.0))
		sp = b
	else:
		var l := LaserSpawner2D.new()
		l.laser_data = H.make_laser(0.0, 0.0, 0.0, 0.0, 100.0)
		sp = l
	sp.set_shooting_enabled(false)
	sp.pattern_source = PatternSpawner2D.PATTERN_FROM_HELPER_RING
	sp.helper_bullets_amount = 4
	sp.helper_ring_radius = 20.0
	sp.position = Vector2(100, 100)
	if configure.is_valid():
		configure.call(sp)
	add(sp)
	sp.set_bullet_factory(factory)
	current = sp
	for s in ["volley_skipped", "volley_telegraphed", "burst_shot_fired", "burst_finished", "pattern_list_finished", "shooting_started", "shooting_stopped", "shooting_finished", "movement_started", "movement_finished", "movement_loop_completed", "movement_endpoint_reached"]:
		sp.connect(s, func(...args) -> void: log.append([frame, s, _plain(args)]))
	if kind == "bullet":
		sp.connect("volley_fired", func(v: BulletVolley2D, n: int) -> void: log.append([frame, "fired", n, _poses_bullets(v)]))
	else:
		sp.connect("laser_fired", func(l: LaserVolley2D, n: int) -> void: log.append([frame, "fired", n, _poses_lasers(l)]))
	return sp


func _plain(args: Array) -> Array:
	var out: Array = []
	for a in args:
		if a is String or a is StringName:
			out.append({"no_spawn_data": "no_payload", "no_laser_data": "no_payload"}.get(String(a), String(a)))
		elif a is Array:
			out.append((a as Array).size())
		else:
			out.append(a)
	return out


func _poses_bullets(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		var t := v.get_bullet_transform(i)
		out.append([t.origin.snapped(Vector2(0.01, 0.01)), snappedf(t.get_rotation(), 0.0001)])
	return out


func _poses_lasers(l: LaserVolley2D) -> Array:
	var out: Array = []
	for i in l.get_beam_count():
		out.append([l.get_beam_start(i).snapped(Vector2(0.01, 0.01)), snappedf(l.get_beam_angle(i), 0.0001)])
	return out


## Runs `scenario` on both kinds and returns [bullet log, laser log].
func _both(scenario: Callable) -> Array:
	var logs: Array = []
	for kind in ["bullet", "laser"]:
		log = []
		frame = 0
		seed(11)
		await scenario.call(kind)
		logs.append(log.duplicate(true))
		if is_instance_valid(current):
			current.queue_free() # its signals must not reach the next run's log
		factory.reset()
		await idle(1)
	return logs


func _frames(n: int) -> void:
	for i in n:
		frame += 1
		await idle(1)


func _kinds(l: Array) -> Array:
	return l.map(func(e): return e[1])


func test_auto_fire_cadence_budget_and_signals() -> void:
	var logs: Array = await _both(func(kind: String) -> void:
		var sp := _spawner(kind, func(s: PatternSpawner2D) -> void:
			s.shoot_interval_sec = 0.2
			s.max_volleys = 3)
		sp.set_shooting_enabled(true)
		await _frames(FRAMES))
	assert_eq(logs[0], logs[1], "the same timeline")
	assert_eq(_kinds(logs[0]).count("fired"), 3, "max_volleys shots")
	assert_has(_kinds(logs[0]), "shooting_finished", "the budget ends the run")


func test_bursts_and_telegraphs() -> void:
	var logs: Array = await _both(func(kind: String) -> void:
		var sp := _spawner(kind, func(s: PatternSpawner2D) -> void:
			s.shoot_interval_sec = 0.4
			s.burst_enabled = true
			s.burst_count = 3
			s.burst_interval_sec = 0.05
			s.burst_alternate_mirror = true
			s.telegraph_enabled = true
			s.telegraph_sec = 0.1
			s.max_volleys = 6)
		sp.set_shooting_enabled(true)
		await _frames(FRAMES))
	assert_eq(logs[0], logs[1], "the same timeline")
	var kinds := _kinds(logs[0])
	assert_gt(kinds.count("burst_shot_fired"), 2, "burst shots")
	assert_gt(kinds.count("volley_telegraphed"), 0, "telegraphs")
	assert_has(kinds, "burst_finished", "a finished burst")


func test_time_scale_initial_delay_and_seeded_reload_jitter() -> void:
	var logs: Array = await _both(func(kind: String) -> void:
		var sp := _spawner(kind, func(s: PatternSpawner2D) -> void:
			s.shoot_interval_sec = 0.1
			s.shoot_initial_delay_sec = 0.2
			s.reload_jitter_sec = 0.05
			s.reload_jitter_seed = 3
			s.time_scale = 0.5)
		sp.set_shooting_enabled(true)
		await _frames(FRAMES))
	assert_eq(logs[0], logs[1], "the same timeline")
	assert_gt(_kinds(logs[0]).count("fired"), 2, "it fired")
	var first: Array = logs[0].filter(func(e): return e[1] == "fired")[0]
	assert_gt(first[0], 20, "the 0.2 s initial delay ran on the half-speed clock (24 frames)")


func test_physics_process_callback_fires_on_physics_ticks() -> void:
	var logs: Array = await _both(func(kind: String) -> void:
		var sp := _spawner(kind, func(s: PatternSpawner2D) -> void:
			s.process_callback = PatternSpawner2D.PROCESS_PHYSICS
			s.shoot_interval_sec = 0.1)
		sp.set_shooting_enabled(true)
		for i in 30:
			frame += 1
			await physics(1)
		await idle(1))
	assert_eq(logs[0], logs[1], "the same timeline")
	assert_gt(_kinds(logs[0]).count("fired"), 3, "it fired")


func test_skips_name_the_same_reasons() -> void:
	var logs: Array = await _both(func(kind: String) -> void:
		var sp := _spawner(kind)
		if kind == "bullet":
			(sp as BulletSpawner2D).set_spawn_data(null)
		else:
			(sp as LaserSpawner2D).laser_data = null
		sp.shoot_once()
		for err in get_errors():
			err.handled = true
		await _frames(1)
		if kind == "bullet":
			(sp as BulletSpawner2D).set_spawn_data(H.make_volley_data(4, 0.0, 30.0))
		else:
			(sp as LaserSpawner2D).laser_data = H.make_laser(0.0, 0.0, 0.0, 0.0, 100.0)
		factory.despawn_bounds_rect = Rect2(500, 500, 10, 10)
		factory.despawn_bounds_enabled = true
		sp.shoot_once()
		await _frames(1)
		factory.despawn_bounds_enabled = false)
	assert_eq(logs[0], logs[1], "the same skips")
	assert_eq(logs[0].map(func(e): return e[2]), [["no_payload"], ["outside_despawn_bounds"]], "no payload, then outside the bounds")


func test_pattern_lists_and_shot_overrides_and_shoot_from() -> void:
	var logs: Array = await _both(func(kind: String) -> void:
		var sp := _spawner(kind)
		sp.spawn_pattern_list([{"helper_bullets_amount": 2}, {"pattern_source": PatternSpawner2D.PATTERN_FROM_HELPER_LINE, "helper_bullets_amount": 3}], false, 0.1)
		await _frames(20)
		sp.shoot_once_with({"helper_bullets_amount": 5})
		log.append([frame, "restored", sp.helper_bullets_amount])
		sp.shoot_from(Transform2D(0.5, Vector2(300, -40)), 15.0)
		await _frames(1))
	assert_eq(logs[0], logs[1], "the same shots and poses")
	var fired: Array = logs[0].filter(func(e): return e[1] == "fired")
	assert_eq(fired.map(func(e): return (e[3] as Array).size()), [2, 3, 5, 4], "list entries, the override, shoot_from")
	assert_has(_kinds(logs[0]), "pattern_list_finished", "the list finished")
	assert_has(logs[0], [20, "restored", 4], "the override was restored")


func test_placement_randomization_rolls_alike() -> void:
	var logs: Array = await _both(func(kind: String) -> void:
		var sp := _spawner(kind, func(s: PatternSpawner2D) -> void:
			s.bullet_rotation_min_degrees = -20.0
			s.bullet_rotation_max_degrees = 20.0
			s.bullet_position_jitter_min = 0.0
			s.bullet_position_jitter_max = 6.0
			s.bullet_random_seed = 5)
		sp.shoot_once()
		sp.shoot_once()
		await _frames(1))
	assert_eq(logs[0], logs[1], "the same rolled poses")
	assert_ne(logs[0][0][3], logs[0][1][3], "and they were rolled per shot")


func test_movement_spin_and_teleport() -> void:
	var path := Path2D.new()
	path.curve = Curve2D.new()
	for pt in [Vector2(0, 0), Vector2(80, 0), Vector2(80, 60)]:
		path.curve.add_point(pt)
	add(path)
	var logs: Array = await _both(func(kind: String) -> void:
		var sp := _spawner(kind, func(s: PatternSpawner2D) -> void:
			s.spin_enabled = true
			s.spin_speed_deg_per_sec = 90.0
			s.shoot_interval_sec = 0.25)
		sp.movement_path = sp.get_path_to(path)
		sp.movement_duration_sec = 0.5
		sp.movement_enabled = true
		sp.movement_play()
		sp.set_shooting_enabled(true)
		await _frames(40)
		sp.set_shooting_enabled(false)
		sp.teleport(Vector2(400, 400))
		sp.shoot_once()
		await _frames(1))
	assert_eq(logs[0], logs[1], "the same path, spin and teleport")
	var kinds := _kinds(logs[0])
	assert_has(kinds, "movement_started", "moved")
	assert_has(kinds, "movement_finished", "and arrived")
	assert_gt(kinds.count("fired"), 2, "fired while moving and spinning")


func test_tree_reentry_and_scene_round_trip_keep_the_base_settings() -> void:
	for kind in ["bullet", "laser"]:
		var sp := _spawner(kind, func(s: PatternSpawner2D) -> void:
			s.shoot_interval_sec = 0.3
			s.burst_count = 4
			s.spin_speed_deg_per_sec = 45.0
			s.time_scale = 0.75
			s.process_callback = PatternSpawner2D.PROCESS_PHYSICS)
		var parent := sp.get_parent()
		parent.remove_child(sp)
		parent.add_child(sp)
		assert_eq([sp.shoot_interval_sec, sp.burst_count, sp.spin_speed_deg_per_sec, sp.time_scale, sp.process_callback], [0.3, 4, 45.0, 0.75, PatternSpawner2D.PROCESS_PHYSICS], "%s: kept through re-entry" % kind)
		var holder := Node2D.new()
		var copy := sp.duplicate()
		holder.add_child(copy)
		copy.owner = holder
		var scene := PackedScene.new()
		assert_eq(scene.pack(holder), OK, "packs")
		var back: PatternSpawner2D = scene.instantiate().get_child(0)
		assert_eq([back.get_class(), back.shoot_interval_sec, back.burst_count, back.spin_speed_deg_per_sec, back.time_scale, back.process_callback], [sp.get_class(), 0.3, 4, 45.0, 0.75, PatternSpawner2D.PROCESS_PHYSICS], "%s: survives a scene round trip" % kind)
		back.get_parent().free()
		holder.free()
