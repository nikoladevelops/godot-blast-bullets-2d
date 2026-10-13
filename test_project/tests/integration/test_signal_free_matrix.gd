extends BlastTest
## Spawner signals are user code the spawner emits from inside its own
## operations (a shot, auto-fire, a pattern list, its Path2D movement) and
## the factory sweep emits on the spawner (hits, bounces, lifetime, homing
## reached, graze, which then bubbles on to the factory); custom timer
## callbacks and the volley's own sprite_animation_finished run inside the
## sweep too. The spawner is locked while it emits, but its
## FACTORY is not: a handler may free() the factory, or the volley the
## signal names. The contract: nothing touches freed memory afterwards
## (tools/run_sanitizer_tests.py runs this under AddressSanitizer), nothing
## hangs or prints an error, and a survivor keeps working.

const VICTIMS := ["volley", "factory"]

var victim_volley: BulletVolley2D
var fired := 0


func _spawner(data: BulletVolleyData2D) -> BulletSpawner2D:
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: victim_volley = v)
	return sp


func _data(speed: float, lifetime := 30.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(1, speed, lifetime)
	d.collision_shape = H.make_circle_shape(4.0)
	d.bullet_max_collision_count = 1
	d.monitorable = true
	d.set_collision_mask_from_array([3]) # make_wall's layer
	d.is_life_time_over_signal_enabled = true
	return d


## The handler: frees the victim once (the factory, or the signal's volley).
func _freeing(victim: String) -> Callable:
	if victim == "factory":
		check_factory_after = false
	var f := factory
	return func(_a = null, _b = null, _c = null, _d = null, _e = null) -> void:
		if fired > 0:
			return
		fired += 1
		if victim == "factory":
			if is_instance_valid(f):
				f.free()
		elif is_instance_valid(victim_volley):
			victim_volley.free()


func _new_case() -> void:
	fired = 0
	victim_volley = null
	if is_instance_valid(factory):
		factory.queue_free()
		await idle(1)
	factory = BulletFactory2D.new()
	add_child_autofree(factory)
	check_factory_after = true
	await idle()


func _run_case(signal_name: String, victim: String) -> String:
	await _new_case()
	var orphans_before := int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT))
	var sp: BulletSpawner2D
	match signal_name:
		"pre_shoot", "volley_fired", "homing_targets_resolved":
			sp = _spawner(_data(0.0))
			if signal_name == "homing_targets_resolved":
				make_graze_target(Vector2(300, 0), &"signal_targets")
				sp.homing_node_group = &"signal_targets"
				sp.set_homing_enabled(true)
			sp.connect(signal_name, _freeing(victim))
			sp.shoot_once()
		"body_entered":
			make_wall(Vector2(60, 0))
			await physics()
			sp = _spawner(_data(600.0))
			sp.connect(signal_name, _freeing(victim))
			sp.shoot_once()
		"life_time_over":
			sp = _spawner(_data(0.0, 0.05))
			sp.connect(signal_name, _freeing(victim))
			sp.shoot_once()
		"volley_bullet_homing_target_reached":
			make_graze_target(Vector2(40, 0), &"signal_targets")
			var d := _data(300.0)
			d.homing_distance_before_reached = 10.0
			sp = _spawner(d)
			sp.homing_node_group = &"signal_targets"
			sp.set_homing_enabled(true)
			sp.connect(signal_name, _freeing(victim))
			sp.shoot_once()
		"bullet_grazed":
			make_graze_target(Vector2(40, 0), &"signal_targets")
			sp = _spawner(_data(300.0))
			var zone := H.make_graze_zone([30.0])
			sp.graze_zones = [zone]
			sp.graze_enabled = true
			sp.graze_node_group = &"signal_targets"
			factory.bullet_grazed.connect(func(_t, _v, _i, _z, _r) -> void: pass)
			sp.connect(signal_name, _freeing(victim))
			sp.shoot_once()
		"shooting_started":
			sp = _spawner(_data(0.0))
			sp.connect(signal_name, _freeing(victim))
			sp.set_shooting_enabled(true)
		"pattern_list_finished":
			sp = _spawner(_data(0.0))
			sp.connect(signal_name, _freeing(victim))
			sp.spawn_pattern_list([{}], true)
		"bounce_body_entered":
			make_wall(Vector2(60, 0))
			await physics()
			var d := _data(600.0)
			d.set_bounce_mask_from_array([3])
			sp = _spawner(d)
			sp.body_entered.connect(func(_b, _v, _i) -> void: pass) # the hit is handled
			sp.connect(signal_name, _freeing(victim))
			sp.shoot_once()
		"movement_finished":
			var path := Path2D.new()
			var curve := Curve2D.new()
			curve.add_point(Vector2.ZERO)
			curve.add_point(Vector2(50, 0))
			path.curve = curve
			add(path)
			sp = _spawner(_data(0.0))
			sp.shoot_once() # the volley a volley victim names
			sp.movement_duration_sec = 0.1
			sp.set_movement_path_node(path)
			sp.connect(signal_name, _freeing(victim))
			sp.movement_enabled = true
		"timer":
			# A custom timer callback: a plain call from the factory sweep.
			sp = _spawner(_data(0.0))
			sp.shoot_once()
			victim_volley.attach_time_based_function(0.05, _freeing(victim), false, false)
		"bullet_homing_target_reached":
			# The volley's OWN reached signal (emitted before the spawner's).
			make_graze_target(Vector2(40, 0), &"signal_targets")
			var d := _data(300.0)
			d.homing_distance_before_reached = 10.0
			sp = _spawner(d)
			sp.homing_node_group = &"signal_targets"
			sp.set_homing_enabled(true)
			sp.shoot_once()
			victim_volley.bullet_homing_target_reached.connect(_freeing(victim))
		"sprite_animation_finished":
			# Emitted by the volley itself (Godot refuses to free the emitter,
			# so only its factory is a victim): the factory teardown must not
			# delete the emitting volley under Godot's emit.
			var d := _data(0.0)
			var frames := SpriteFrames.new()
			var img := Image.create_empty(1, 1, false, Image.FORMAT_RGBA8)
			img.fill(Color.WHITE)
			var tex := ImageTexture.create_from_image(img)
			frames.add_frame("default", tex)
			frames.add_frame("default", tex)
			frames.set_animation_speed("default", 30.0)
			frames.set_animation_loop("default", false)
			d.sprite_frames = frames
			sp = _spawner(d)
			sp.shoot_once()
			victim_volley.sprite_animation_finished.connect(_freeing(victim))
	for k in 40:
		await physics(1)
		if fired > 0:
			break
	for k in 3:
		await physics(1)
	await idle(2)
	var problems: Array = []
	var where := "%s / %s" % [signal_name, victim]
	if fired != 1:
		problems.append("%s: the handler ran %d times" % [where, fired])
	# Auto-fire with its factory gone reports it once (latched), by design.
	var allowed := ["BulletSpawner2D::shoot_once: no BulletFactory2D assigned (bullet_factory_path)."] if signal_name == "shooting_started" and victim == "factory" else []
	var seen: Array = []
	for err in get_errors():
		if not err.handled:
			err.handled = true
			seen.append(str(err.code))
	if seen != allowed:
		problems.append("%s errors: %s, expected %s" % [where, seen, allowed])
	if is_instance_valid(factory):
		var books: Dictionary = factory.debug_assert_no_dangling()
		if not books["ok"]:
			problems.append("%s: %s" % [where, books["error"]])
	if is_instance_valid(sp):
		sp.set_shooting_enabled(false)
		sp.queue_free()
	await idle(2)
	var orphans := int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT)) - orphans_before
	if orphans != 0:
		problems.append("%s: %d orphan nodes" % [where, orphans])
	return "\n".join(problems)


func test_freeing_from_spawner_signal_handlers_never_touches_freed_memory() -> void:
	var problems: Array = []
	for signal_name in ["pre_shoot", "volley_fired", "homing_targets_resolved", "body_entered", "life_time_over",
			"volley_bullet_homing_target_reached", "bullet_grazed", "shooting_started", "pattern_list_finished",
			"bounce_body_entered", "movement_finished", "timer", "bullet_homing_target_reached", "sprite_animation_finished"]:
		for victim in VICTIMS:
			if signal_name in ["bullet_homing_target_reached", "sprite_animation_finished"] and victim == "volley":
				continue # the volley emits these: Godot refuses that free itself
			var p: String = await _run_case(signal_name, victim)
			if p != "":
				problems.append(p)
	assert_eq("\n".join(problems), "", "every spawner signal survives a free from its handler")
