extends BlastTest
## Godot refuses free() on an object that is emitting a signal ONLY in debug
## builds (the lock check of Object::callp is DEBUG_ENABLED): in an exported
## release game a handler that frees the emitter deletes it in the middle of
## the plugin's code. BulletFactory2D.debug_free_like_release() frees the
## release way. For every signal of BulletSpawner2D, LaserSpawner2D and
## BulletFactory2D (the laser signals too)
## (read from ClassDB, so new signals are covered), a handler frees its
## emitter on the first emission: the plugin never touches freed memory
## (tools/run_sanitizer_tests.py runs this under AddressSanitizer), the
## emitter is gone by the next frame, and nothing reports an error.

## Signals no scenario below can raise, and why.
const UNREACHED_OK := {
	"movement_finished": "Once mode ends the run; the scenario loops (movement_loop_completed covers the emit site)",
}

var target: Node2D
var path: Path2D


func before_each() -> void:
	await super()
	target = Node2D.new()
	target.position = Vector2(260, 0)
	target.add_to_group(&"rf_targets")
	add(target)
	var grazed := Node2D.new() # bullets pass it and leave: grazes and graze exits
	grazed.position = Vector2(30, 0)
	grazed.add_to_group(&"rf_graze")
	add(grazed)
	path = Path2D.new()
	path.curve = Curve2D.new()
	path.curve.add_point(Vector2(0, 0))
	path.curve.add_point(Vector2(30, 0))
	add(path)
	make_wall(Vector2(100, 0)) # hits (layer value 4)
	make_wall(Vector2(-30, 0), Vector2(10, 60), 16) # bounces (layer value 16), close: homing turns bullets soon
	make_area(Vector2(0, 100), Vector2(400, 20)) # area hits
	make_area(Vector2(0, -30), Vector2(60, 10), 16) # area bounces


## The signals a plugin class and its plugin bases declare.
func _signal_names(cls: String) -> Array:
	var out: Array = []
	var c := StringName(cls)
	while c != &"" and ClassDB.class_get_api_type(c) == ClassDB.API_EXTENSION:
		for s in ClassDB.class_get_signal_list(c, true):
			out.append(str(s.name))
		c = ClassDB.get_parent_class(c)
	out.sort()
	return out


func _arg_count(obj: Object, sig: String) -> int:
	for s in obj.get_signal_list():
		if str(s.name) == sig:
			return (s.args as Array).size()
	return 0


## Volley data that raises every bullet signal: hits a wall and an area,
## bounces off another wall, expires (with the signal on), splits on expiry.
func _busy_data(n: int) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 400.0, 0.5)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(TAU * i / n, Vector2.ZERO))
	d.transforms = arr
	d.monitorable = true
	d.collision_shape = H.make_circle_shape(6.0)
	d.set_collision_mask_from_array([3, 5])
	d.set_bounce_mask_from_array([5])
	d.bullet_max_collision_count = 2
	d.is_life_time_over_signal_enabled = true
	return d


func _split_chain() -> BulletSplitData2D:
	var stage := BulletSplitStage2D.new()
	stage.count = 2
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	return chain


## Arms the handler that release-frees `emitter` on the first `sig`.
func _arm(emitter: Object, sig: String, fired: Array) -> void:
	var free_it := func() -> void:
		if fired.is_empty() and is_instance_valid(emitter):
			fired.append(true)
			BulletFactory2D.debug_free_like_release(emitter)
	emitter.connect(sig, _taking(free_it, _arg_count(emitter, sig)))


## `call` accepting `n` ignored arguments (unbind(0) is an error).
func _taking(call: Callable, n: int) -> Callable:
	return call.unbind(n) if n > 0 else call


## A no-op handler on every signal: nothing counts as unhandled (those
## warnings are not what this suite is about).
func _handle_all(emitter: Object) -> void:
	for s in emitter.get_signal_list():
		var sig := str(s.name)
		if ClassDB.class_has_signal(emitter.get_class(), sig) and not ClassDB.class_has_signal("Node2D", sig):
			emitter.connect(sig, _taking((func() -> void: pass), (s.args as Array).size()))


## Claims and returns the errors the case printed.
func _errors() -> Array:
	var out: Array = []
	for err in get_errors():
		if not err.handled:
			err.handled = true
			out.append(str(err.code))
	return out


func test_freeing_the_spawner_from_its_own_signal(sig: String = use_parameters(_signal_names("BulletSpawner2D"))) -> void:
	if UNREACHED_OK.has(sig):
		pass_test("not raised by this scenario: " + UNREACHED_OK[sig])
		return
	godot_listener_at(Vector2.ZERO)
	var sp := make_spawner(_busy_data(6), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6)
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	sp.set_homing_target_path(sp.get_path_to(target))
	sp.homing_retarget_mode = BulletSpawner2D.HOMING_RETARGET_ON_INTERVAL
	sp.homing_retarget_interval_sec = 0.05
	sp.burst_enabled = true
	sp.burst_count = 2
	sp.burst_interval_sec = 0.05
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.05
	sp.shoot_interval_sec = 0.1
	sp.max_volleys = 4
	sp.helper_ring_radius = 10.0 # bullets start next to the graze target
	sp.graze_enabled = true
	sp.graze_zones = [H.make_graze_zone([50.0])]
	sp.graze_node_group = &"rf_graze"
	sp.split_enabled = true
	sp.split_data = _split_chain()
	sp.sound_enabled = true
	sp.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)]
	sp.set_movement_path_node(path)
	sp.movement_loop_mode = BulletSpawner2D.MOVEMENT_LOOP_LOOP
	sp.movement_duration_sec = 0.1
	if sig == "volley_skipped":
		sp.max_live_bullets = 1 # the second pull skips ("over_budget")
	_handle_all(sp)
	sp.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_REMOVE # its bullets leave with it (no unhandled-hit warnings after)
	var fired: Array = []
	_arm(sp, sig, fired)
	sp.movement_enabled = true
	if fired.is_empty():
		sp.set_shooting_enabled(true)
	if fired.is_empty():
		sp.spawn_pattern_list([{"helper_bullets_amount": 6}, {"helper_bullets_amount": 6}], false, 0.05)
	for k in 180:
		if not fired.is_empty() or not is_instance_valid(sp):
			break
		if k == 10 and sig == "shooting_stopped" and fired.is_empty():
			sp.pause_shooting() # while auto-fire still runs (the cap ends it later)
		await physics(1)
	assert_false(fired.is_empty(), "%s was raised by the scenario" % sig)
	await idle(2)
	assert_false(is_instance_valid(sp), "%s: the freed spawner is gone" % sig)
	assert_eq(_errors(), [], "%s: freeing the emitter reported nothing" % sig)
	var books: Dictionary = factory.debug_assert_no_dangling()
	assert_true(books["ok"], "%s: %s" % [sig, books["error"]])


func test_freeing_the_laser_spawner_from_its_own_signal(sig: String = use_parameters(_signal_names("LaserSpawner2D"))) -> void:
	if UNREACHED_OK.has(sig):
		pass_test("not raised by this scenario: " + UNREACHED_OK[sig])
		return
	godot_listener_at(Vector2.ZERO)
	var sp := LaserSpawner2D.new()
	sp.set_shooting_enabled(false)
	# Short phases, a sweep across the graze target, the layer-4 wall pierced
	# and the layer-16 wall blocking: every laser signal within the run.
	var laser := H.make_laser(0.02, 0.02, 0.3, 0.02, 300.0)
	laser.wall_mask = 16
	laser.hit_interval_sec = 0.05
	laser.sweep_speed_degrees = 720.0
	sp.laser_data = laser
	sp.pattern_source = LaserSpawner2D.PATTERN_FROM_HELPER_RING
	sp.helper_bullets_amount = 2
	sp.helper_ring_radius = 10.0
	add(sp)
	sp.set_bullet_factory(factory)
	sp.burst_enabled = true
	sp.burst_count = 2
	sp.burst_interval_sec = 0.05
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.05
	sp.shoot_interval_sec = 0.1
	sp.max_volleys = 4
	sp.graze_enabled = true
	sp.graze_zones = [H.make_graze_zone([15.0])] # the muzzles sit 20 px off the target: a sweep leaves the ring
	sp.graze_node_group = &"rf_graze"
	sp.sound_enabled = true
	sp.sound_effects = [H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_SHOT)]
	sp.set_movement_path_node(path)
	sp.movement_loop_mode = LaserSpawner2D.MOVEMENT_LOOP_LOOP
	sp.movement_duration_sec = 0.1
	if sig == "volley_skipped":
		sp.max_live_lasers = 1 # the second pull skips ("over_budget")
	_handle_all(sp)
	sp.orphaned_lasers = LaserSpawner2D.ORPHANED_LASERS_REMOVE
	var fired: Array = []
	_arm(sp, sig, fired)
	sp.movement_enabled = true
	if fired.is_empty():
		sp.set_shooting_enabled(true)
	if fired.is_empty():
		sp.spawn_pattern_list([{"helper_bullets_amount": 2}, {"helper_bullets_amount": 2}], false, 0.05)
	for k in 180:
		if not fired.is_empty() or not is_instance_valid(sp):
			break
		if k == 10 and sig == "shooting_stopped" and fired.is_empty():
			sp.pause_shooting()
		await physics(1)
	assert_false(fired.is_empty(), "%s was raised by the scenario" % sig)
	await idle(2)
	assert_false(is_instance_valid(sp), "%s: the freed spawner is gone" % sig)
	assert_eq(_errors(), [], "%s: freeing the emitter reported nothing" % sig)
	var books: Dictionary = factory.debug_assert_no_dangling()
	assert_true(books["ok"], "%s: %s" % [sig, books["error"]])


func test_freeing_the_factory_from_its_own_signal(sig: String = use_parameters(_signal_names("BulletFactory2D"))) -> void:
	godot_listener_at(Vector2.ZERO)
	check_factory_after = false
	var f := factory
	_handle_all(f)
	var fired: Array = []
	_arm(f, sig, fired)
	var d := _busy_data(6)
	var v: BulletVolley2D = f.spawn_volley(d)
	v.split_set_data(_split_chain(), H.make_volley_data(2, 100.0, 0.3))
	v.graze_set_zones([H.make_graze_zone([50.0])], &"rf_graze")
	# A laser raises every laser signal: +X pierces the layer-4 wall, -X is
	# blocked by the layer-16 wall; short phases end it within the scenario.
	# Sweeping (720 deg/s) it also enters and leaves the graze target.
	var laser := H.make_laser(0.05, 0.05, 0.6, 0.05, 300.0)
	laser.wall_mask = 16
	laser.hit_interval_sec = 0.05
	laser.sweep_speed_degrees = 720.0
	var beams: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO), Transform2D(PI, Vector2.ZERO)]
	var lv := f.spawn_laser(laser, beams)
	lv.graze_set_zones([H.make_graze_zone([20.0])], &"rf_graze")
	var pickup := BulletPickupData2D.new()
	for k in 200:
		if not fired.is_empty() or not is_instance_valid(f):
			break
		match k:
			50:
				f.spawn_volley(_busy_data(4))
				f.clear_active_bullets()
			60:
				f.spawn_volley(H.make_volley_data(3, 0.0, 30.0))
				f.convert_all_bullets_to_pickups(pickup, target)
			130:
				f.spawn_volley(_busy_data(4))
				f.despawn_bounds_rect = Rect2(-50, -50, 100, 100)
				f.despawn_bounds_enabled = true
			170:
				f.reset()
		await idle(1)
	assert_false(fired.is_empty(), "%s was raised by the scenario" % sig)
	await idle(2)
	assert_false(is_instance_valid(f), "%s: the freed factory is gone" % sig)
	assert_eq(_errors(), [], "%s: freeing the emitter reported nothing" % sig)


func test_freeing_a_volley_from_its_own_signal(sig: String = use_parameters(_signal_names("BulletVolley2D"))) -> void:
	var d := H.make_still_data(2)
	d.homing_distance_before_reached = 400.0 # reached on the first tick
	d.homing_take_control_of_texture_rotation = true
	if sig == "sprite_animation_finished":
		var sf := SpriteFrames.new()
		var img := Image.create_empty(1, 1, false, Image.FORMAT_RGBA8)
		var tex := ImageTexture.create_from_image(img)
		sf.add_frame("default", tex)
		sf.add_frame("default", tex)
		sf.set_animation_speed("default", 30.0)
		sf.set_animation_loop("default", false)
		d.sprite_frames = sf
	var v: BulletVolley2D = factory.spawn_volley(d)
	if sig == "bullet_homing_target_reached":
		v.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	_handle_all(v)
	var fired: Array = []
	_arm(v, sig, fired)
	for k in 60:
		if not fired.is_empty():
			break
		await physics(1)
	assert_false(fired.is_empty(), "%s was raised by the scenario" % sig)
	await idle(2)
	assert_false(is_instance_valid(v), "%s: the freed volley is gone" % sig)
	assert_eq(_errors(), [], "%s: freeing the emitter reported nothing" % sig)
	var books: Dictionary = factory.debug_assert_no_dangling()
	assert_true(books["ok"], "%s: %s" % [sig, books["error"]])
