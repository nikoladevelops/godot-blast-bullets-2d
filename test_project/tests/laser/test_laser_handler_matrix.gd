extends BlastTest
## Laser handlers may do anything legal: inside EVERY laser signal (read from
## ClassDB, so new signals are covered), on a factory laser and on a
## LaserSpawner2D's laser, a handler ends lasers (one, all, by area), clears or
## re-arms graze, retargets, unanchors, sets custom data, edits the shared
## LaserData2D (a type switch, reflections, a tiny length), spawns more
## lasers, teleports or re-applies its spawner, queues pool and reset calls,
## or queue_frees the laser. Contract: no error, the books balance (checked
## after every run), every laser finishes or fires on, nothing is left firing
## after the wind-down.

const DT := 0.0625
const GROUP := &"laser_matrix_targets"
const ACTIONS := ["end_fade", "end_now", "clear_all", "end_by_area", "graze_clear", "graze_rearm",
	"retarget", "unanchor", "custom_data", "data_type", "data_reflections", "data_length",
	"spawn_more", "teleport", "apply", "end_live", "pool_deferred", "reset_deferred", "queue_free"]

var _grazer: Node2D


func before_each() -> void:
	await super()
	make_area(Vector2(100, 0), Vector2(20, 60))
	make_wall(Vector2(200, 0), Vector2(20, 400), 16) # blocks the sweep for its first ticks
	_grazer = Node2D.new()
	_grazer.position = Vector2(150, 24)
	add(_grazer)
	_grazer.add_to_group(GROUP)
	await idle(2)


func _signals() -> Array:
	var out: Array = []
	for s in ClassDB.class_get_signal_list("BulletFactory2D", true):
		if String(s["name"]).begins_with("laser_"):
			out.append(String(s["name"]))
	return out


## A beam that turns away from its target, wall and grazer: every signal
## fires within a few ticks (pierce start/end, wall hit, graze/exit,
## phases, finish).
func _data() -> LaserData2D:
	var d := H.make_laser(0.0, 0.0, 0.5, 0.0625, 400.0)
	d.wall_mask = 16
	d.sweep_speed_degrees = 240.0
	d.hit_interval_sec = 0.0625
	return d


func _zone() -> BulletGrazeZone2D:
	return H.make_graze_zone([40.0])


## Fires one laser on `route` ("factory" or "spawner"); returns [laser, emitter, spawner].
func _fire(route: String) -> Array:
	var data := _data()
	if route == "factory":
		var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO)]
		var l := factory.spawn_laser(data, poses)
		l.graze_set_zones([_zone()], GROUP)
		return [l, factory, null]
	var sp := LaserSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.laser_data = data
	sp.pattern_source = LaserSpawner2D.PATTERN_FROM_HELPER_CUSTOM
	sp.helper_custom_transforms = [Transform2D(0.0, Vector2.ZERO)]
	sp.graze_enabled = true
	sp.graze_zones = [_zone()]
	sp.graze_node_group = GROUP
	add(sp)
	sp.set_bullet_factory(factory)
	var got: Array = []
	sp.laser_fired.connect(func(l: LaserVolley2D, _n: int) -> void: got.append(l), CONNECT_ONE_SHOT)
	sp.shoot_once()
	return [got[0] if not got.is_empty() else null, sp, sp]


func _act(action: String, laser: LaserVolley2D, spawner: LaserSpawner2D) -> void:
	var alive := is_instance_valid(laser) and not laser.is_pooled()
	match action:
		"end_fade":
			if alive:
				laser.end_laser(true)
		"end_now":
			if alive:
				laser.end_laser(false)
		"clear_all":
			factory.clear_active_lasers(false)
		"end_by_area":
			factory.end_lasers_in_circle(Vector2(100, 0), 500.0, false)
		"graze_clear":
			if alive:
				laser.graze_clear()
		"graze_rearm":
			if alive:
				laser.graze_set_zones([H.make_graze_zone([60.0, 20.0])], GROUP)
		"retarget":
			if alive:
				laser.set_targets([Vector2(0, 300)])
				# This laser aims along the pattern: its one per-life warning
				# that a target steers nothing (pinned in test_laser_aim_and_motion).
				for err in get_errors():
					if not err.handled and err.is_push_warning() and err.contains_text("a target steers nothing"):
						err.handled = true
		"unanchor":
			if alive:
				laser.set_anchor(null)
		"custom_data":
			if alive:
				laser.set_custom_data({"hits": 1})
		"data_type":
			if alive:
				laser.get_laser_data().type = LaserData2D.LASER_CURVY
		"data_reflections":
			if alive:
				laser.get_laser_data().reflections = 3
		"data_length":
			if alive:
				laser.get_laser_data().length = 1.0
		"spawn_more":
			var poses: Array[Transform2D] = [Transform2D(1.0, Vector2(0, 50)), Transform2D(2.0, Vector2(0, 50))]
			factory.spawn_laser(_data(), poses)
		"teleport":
			if spawner != null:
				spawner.teleport(Vector2(30, 40), true)
		"apply":
			if spawner != null:
				spawner.graze_enabled = not spawner.graze_enabled
				spawner.apply_to_live_lasers()
		"end_live":
			if spawner != null:
				spawner.end_live_lasers(false)
		"pool_deferred":
			factory.populate_lasers_pool_deferred(2, 2)
			factory.free_lasers_pool_deferred()
		"reset_deferred":
			factory.reset_deferred()
		"queue_free":
			if alive:
				laser.queue_free()


## One run: fire, act inside the first `sig`, tick, wind down. Returns "" or the problem.
func _run(route: String, sig: String, action: String) -> String:
	var fired := _fire(route)
	var laser: LaserVolley2D = fired[0]
	var emitter: Object = fired[1]
	var spawner: LaserSpawner2D = fired[2]
	if laser == null:
		return "no laser fired"
	var done := [false]
	emitter.connect(sig, func(...args) -> void:
		if done[0]:
			return
		done[0] = true
		var l: LaserVolley2D = null
		for x in args:
			if x is LaserVolley2D:
				l = x
		_act(action, l, spawner))
	for i in 16:
		step_factory(1, DT)
	await idle(2) # deferred calls and queue_free land
	var problem := ""
	var texts: Array = []
	for err in get_errors():
		if not err.handled:
			err.handled = true
			texts.append(str(err.code))
	if not texts.is_empty():
		problem = "errors %s" % [texts]
	if problem == "" and not done[0]:
		problem = "the signal never fired"
	var books: Dictionary = factory.debug_assert_no_dangling()
	if problem == "" and not books["ok"]:
		problem = "books: %s" % books["error"]
	factory.clear_active_lasers(false)
	step_factory(2, DT)
	await idle(1)
	if problem == "" and factory.get_active_laser_count() != 0:
		problem = "%d lasers still firing after the wind-down" % factory.get_active_laser_count()
	factory.reset()
	await idle(1)
	if is_instance_valid(spawner):
		spawner.queue_free()
		await idle(1)
	return problem


func test_the_scene_raises_every_laser_signal_on_both_routes() -> void:
	for route in ["factory", "spawner"]:
		var fired := _fire(route)
		var heard := {}
		for sig in _signals():
			(fired[1] as Object).connect(sig, func(..._args) -> void: heard[sig] = true)
		for i in 16:
			step_factory(1, DT)
		var missing: Array = _signals().filter(func(s): return not heard.has(s))
		assert_eq(missing, [], "%s: every laser signal fires in the scene" % route)
		factory.reset()
		await idle(1)
		if fired[2] != null:
			(fired[2] as Node).queue_free()
			await idle(1)


func test_every_action_in_every_laser_signal(route: String = use_parameters(["factory", "spawner"])) -> void:
	var problems: Array = []
	for sig in _signals():
		for action in ACTIONS:
			var why: String = await _run(route, sig, action)
			if why != "":
				problems.append("%s / %s: %s" % [sig, action, why])
	assert_eq("\n".join(problems), "", "every handler action is survived")
