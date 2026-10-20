extends BlastTest
## Seeded chaos: a battlefield of spawners that share one factory, one pool
## and the same flock / graze / sound / split Resources fire thousands of
## bullets through REAL physics (walls that kill and walls that bounce,
## graze and homing targets), while the run frees, re-creates, reparents and
## re-times spawners (every orphan policy), edits the shared Resources in
## flight, clears, resets and frees bullets, converts them to pickups,
## pre-populates the pool, pauses the factory and frees targets, and the
## signal handlers do the same from INSIDE the sweep (queue_free, the
## *_deferred twins, disables, spawns, release-build frees of the emitter
## through debug_free_like_release, hostile but legal values on bullets in
## flight; spawners travel paths whose movement handlers stop, seek, reverse
## and restart them). Every operation is legal: the run
## must print no error at all, and after every step the books balance
## (debug_assert_no_dangling) and every live bullet is finite.
## A failure names its seed and step; CHAOS_SEEDS=<n> (environment) runs n
## seeds instead of the default set (CHAOS_SEEDS=<a>,<b> runs exactly
## those), CHAOS_SKIP=<id,id,...> skips those
## _operate ids and CHAOS_SKIP_HANDLERS=1 keeps the handlers passive (to
## bisect a failure) (tools/run_sanitizer_tests.py runs it
## under AddressSanitizer: a write into freed memory passes every assert).

const DEFAULT_SEEDS := [1, 2, 3, 4, 5, 6]
const STEPS := 240
const MAX_SPAWNERS := 8
const TARGET_GROUP := &"chaos_targets"
## Warnings the run may legally cause (each is a documented one-time
## explanation, never a failure): orphaned hits nobody hears, empty homing
## or graze sources while targets are freed, the per-sweep children cap.
const WARNING_OK := ["no one", "orphan", "homing", "graze", "target", "split", "sound", "listener", "flock", "direction curve"]

var rng := RandomNumberGenerator.new()
var spawners: Array = []
var volleys: Array = [] # every volley a signal handed out (validated before use)
var targets: Array = []
var world: Node2D
var other_parent: Node2D
var flock: BulletFlock2D
var zones: Array = []
var sound: BulletSoundData2D
var chain: BulletSplitData2D
var datas: Array = []
var pickup: BulletPickupData2D
var shared_generator: Node2D # the generator two spawners share (op 30)
var probe_scene: PackedScene
var stats := {}
var skipped_ops: Array = []
var passive_handlers := false


func _seeds() -> Array:
	var n := OS.get_environment("CHAOS_SEEDS")
	if n.is_valid_int() and int(n) > 0:
		return range(1, int(n) + 1)
	if n.contains(","):
		return Array(n.split(",", false)).map(func(x): return int(x))
	return DEFAULT_SEEDS


func before_each() -> void:
	await super()
	spawners.clear()
	volleys.clear()
	targets.clear()
	datas.clear()
	zones.clear()
	skipped_ops = Array(OS.get_environment("CHAOS_SKIP").split(",", false)).map(func(x): return int(x))
	passive_handlers = OS.get_environment("CHAOS_SKIP_HANDLERS") == "1"
	stats = {"shots": 0, "frees": 0, "reparents": 0, "clears": 0, "resets": 0, "pickups": 0, "handler_actions": 0, "max_live": 0, "splits": 0, "grazes": 0}


## A spawn data template: `n` bullets of a random shape, features on by roll.
func _data(n: int) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, rng.randf_range(80.0, 400.0), rng.randf_range(0.5, 4.0))
	# One shared speed: the data is shared by spawners of any bullet count.
	d.shared_bullet_speed_data = d.all_bullet_speed_data[0]
	d.all_bullet_speed_data = []
	d.monitorable = true
	d.set_collision_mask_from_array([3, 4])
	match rng.randi() % 3:
		0:
			d.collision_shape = H.make_circle_shape(rng.randf_range(2.0, 12.0))
		1:
			var r := RectangleShape2D.new()
			r.size = Vector2(rng.randf_range(2.0, 16.0), rng.randf_range(2.0, 16.0))
			d.collision_shape = r
		2:
			var c := CapsuleShape2D.new()
			c.radius = rng.randf_range(2.0, 6.0)
			c.height = c.radius * 2.0 + rng.randf_range(0.0, 10.0)
			d.collision_shape = c
	d.bullet_max_collision_count = rng.randi_range(0, 3)
	if rng.randf() < 0.4:
		d.set_bounce_mask_from_array([4])
		d.bounce_max_count = rng.randi_range(0, 4)
	if rng.randf() < 0.3:
		d.gravity = Vector2(0, rng.randf_range(-200.0, 200.0))
	if rng.randf() < 0.3:
		var w := BulletWobbleData2D.new()
		w.enabled = true
		w.amplitude = rng.randf_range(1.0, 40.0)
		w.frequency_hz = rng.randf_range(0.5, 6.0)
		d.shared_bullet_wobble_data = w
	if rng.randf() < 0.3:
		d.effect_layers = [H.make_effect_layer([BulletEffectLayerData2D.EFFECT_ON_HIT, BulletEffectLayerData2D.EFFECT_ON_DESTROY, BulletEffectLayerData2D.EFFECT_ON_SPLIT, BulletEffectLayerData2D.EFFECT_ON_CLEAR][rng.randi() % 4])]
	if rng.randf() < 0.25:
		d.shared_bullet_attachment = make_probe_scene()
	d.is_life_time_over_signal_enabled = rng.randf() < 0.5
	return d


func _build_world() -> void:
	world = Node2D.new()
	add(world)
	other_parent = Node2D.new()
	add(other_parent)
	# Killing walls (layer 3 = value 4) and bouncing walls (layer 4 = value 8) around the arena.
	for k in 4:
		var angle := TAU * k / 4.0
		make_wall(Vector2.from_angle(angle) * 380.0, Vector2(40, 900), 4 if k % 2 == 0 else 8, 2)
	for k in 6:
		_add_target()
	flock = H.make_flock(rng.randf_range(0.0, 10.0), rng.randf_range(0.0, 2.0), rng.randf_range(0.0, 2.0))
	zones = [H.make_graze_zone([rng.randf_range(10.0, 60.0), rng.randf_range(60.0, 120.0)])]
	sound = H.make_sound([BulletSoundData2D.SOUND_ON_HIT, BulletSoundData2D.SOUND_ON_SPLIT, BulletSoundData2D.SOUND_ON_CLEAR, BulletSoundData2D.SOUND_ON_GRAZE][rng.randi() % 4], rng.randi_range(1, 6))
	var stage := BulletSplitStage2D.new()
	stage.count = rng.randi_range(1, 4)
	stage.split_on_hit = true
	stage.split_on_lifetime_over = rng.randf() < 0.8
	stage.split_on_bounce = rng.randf() < 0.3
	stage.split_on_graze = rng.randf() < 0.2
	stage.parent_survives = rng.randf() < 0.3
	stage.max_splits_per_bullet = rng.randi_range(1, 3)
	stage.child_data = _data(stage.count) if rng.randf() < 0.5 else null
	chain = BulletSplitData2D.new()
	chain.stages = [stage]
	chain.repeat_last_stage = rng.randf() < 0.3
	chain.max_depth = rng.randi_range(1, 3)
	for k in 6:
		datas.append(_data([1, 2, 4, 8, 16, 32, 64][rng.randi() % 7]))
	pickup = BulletPickupData2D.new()
	pickup.collect_on_collision = rng.randf() < 0.5
	probe_scene = make_probe_scene()
	factory.split_max_children_per_sweep = [0, 32, 256][rng.randi() % 3]


func _add_target() -> void:
	var t := Node2D.new()
	t.position = Vector2(rng.randf_range(-400, 400), rng.randf_range(-400, 400))
	world.add_child(t)
	t.add_to_group(TARGET_GROUP)
	targets.append(t)


func _add_spawner() -> void:
	var n := rng.randi_range(1, 64)
	var sources := [BulletSpawner2D.PATTERN_FROM_HELPER_RING, BulletSpawner2D.PATTERN_FROM_HELPER_SPIRAL, BulletSpawner2D.PATTERN_FROM_HELPER_FAN, BulletSpawner2D.PATTERN_FROM_HELPER_GRID]
	var data: BulletVolleyData2D = datas[rng.randi() % datas.size()] if rng.randf() < 0.7 else _data(n)
	var sp := make_spawner(data, sources[rng.randi() % sources.size()], n)
	sp.position = Vector2(rng.randf_range(-300, 300), rng.randf_range(-300, 300))
	sp.orphaned_volleys = rng.randi() % 4
	if rng.randf() < 0.5:
		sp.homing_enabled = true
		sp.homing_target_source = BulletSpawner2D.HOMING_SOURCE_NODE_GROUP
		sp.homing_node_group = TARGET_GROUP
	if rng.randf() < 0.4:
		sp.flock_enabled = true
		sp.flock = flock
	if rng.randf() < 0.4:
		sp.graze_enabled = true
		sp.graze_zones = zones
		sp.graze_node_group = TARGET_GROUP
	if rng.randf() < 0.3:
		sp.sound_enabled = true
		sp.sound_effects = [sound]
	if rng.randf() < 0.7:
		sp.split_enabled = true
		sp.split_data = chain
	if rng.randf() < 0.3:
		sp.time_scale = [0.0, 0.5, 2.0][rng.randi() % 3]
	if rng.randf() < 0.3:
		sp.burst_enabled = true
		sp.burst_count = rng.randi_range(2, 4)
		sp.burst_interval_sec = 0.02
	if rng.randf() < 0.3:
		sp.shoot_interval_sec = rng.randf_range(0.03, 0.3)
		sp.shooting_enabled = true
	_connect_handlers(sp)
	spawners.append(sp)


## Handlers run INSIDE the sweep: they only use what is legal there.
func _connect_handlers(emitter: Object) -> void:
	emitter.connect("body_entered", func(_b, v, i) -> void: _handler(v, i))
	emitter.connect("area_entered", func(_a, v, i) -> void: _handler(v, i))
	emitter.connect("life_time_over", func(v, _idx) -> void: _handler(v, -1))
	emitter.connect("bullet_split", func(_p, _c, children, _d) -> void:
		stats["splits"] += 1
		_handler(children, 0))
	emitter.connect("bullet_grazed", func(_t, v, i, _z, _r) -> void:
		stats["grazes"] += 1
		_handler(v, i))
	if emitter.has_signal("volley_fired"):
		emitter.connect("volley_fired", func(v, _n) -> void: volleys.append(v))


func _handler(v: Object, i: int) -> void:
	if not is_instance_valid(v):
		return
	volleys.append(v)
	if rng.randf() > 0.08 or passive_handlers:
		return
	stats["handler_actions"] += 1
	var volley := v as BulletVolley2D
	match rng.randi() % 12:
		0:
			if i >= 0 and i < volley.get_amount_bullets():
				volley.disable_bullet(i)
		1:
			volley.queue_free()
		2:
			factory.clear_bullets_in_circle(Vector2(rng.randf_range(-300, 300), rng.randf_range(-300, 300)), rng.randf_range(10.0, 200.0))
		3:
			factory.spawn_volley(datas[rng.randi() % datas.size()])
		4:
			factory.free_active_bullets_deferred()
		5:
			var sp = _live_spawner()
			if sp != null:
				sp.queue_free()
		6:
			factory.reset_deferred()
		7:
			var t = _live_target()
			if t != null:
				t.queue_free()
		8:
			if i >= 0 and i < volley.get_amount_bullets() and volley.is_bullet_status_enabled(i):
				volley.split_bullet(i)
		9:
			# What free() does in an exported release game (no debug lock):
			# the volley may be the one emitting.
			BulletFactory2D.debug_free_like_release(volley)
		10:
			var sp = _live_spawner()
			if sp != null and spawners.size() >= 3: # keep the battlefield firing
				BulletFactory2D.debug_free_like_release(sp)
		11:
			# Hostile but legal values on a bullet in flight: it stays finite.
			if i >= 0 and i < volley.get_amount_bullets():
				volley.set_bullet_direction(i, Vector2(1e20, rng.randf_range(-1e20, 1e20)))
				volley.bullet_set_gravity(i, Vector2(0, 3e38))


func _move_handler(sp: BulletSpawner2D) -> void:
	if not is_instance_valid(sp) or rng.randf() > 0.3:
		return
	match rng.randi() % 5:
		0:
			sp.movement_stop(rng.randf() < 0.5)
		1:
			sp.movement_seek(rng.randf())
		2:
			sp.movement_reverse()
		3:
			sp.movement_play()
		4:
			sp.movement_pause()


func _live_spawner():
	spawners = spawners.filter(func(s): return is_instance_valid(s) and not s.is_queued_for_deletion())
	return null if spawners.is_empty() else spawners[rng.randi() % spawners.size()]


func _live_target():
	targets = targets.filter(func(t): return is_instance_valid(t) and not t.is_queued_for_deletion())
	return null if targets.is_empty() else targets[rng.randi() % targets.size()]


## One operation from an idle point (structural calls allowed).
func _operate() -> void:
	var roll := rng.randi() % 66
	if roll >= 33:
		roll = 0 # half of all operations are shots
	if skipped_ops.has(roll):
		return
	match roll:
		0, 1, 2, 3, 4, 5:
			for k in rng.randi_range(1, 4):
				var sp = _live_spawner()
				if sp != null:
					sp.shoot_once()
					stats["shots"] += 1
		6:
			var sp = _live_spawner()
			if sp != null:
				stats["frees"] += 1
				if rng.randf() < 0.5:
					sp.free()
				else:
					sp.queue_free()
		7:
			if spawners.size() < MAX_SPAWNERS:
				_add_spawner()
		8:
			var sp = _live_spawner()
			if sp != null:
				stats["reparents"] += 1
				var to: Node = other_parent if sp.get_parent() != other_parent else self
				sp.get_parent().remove_child(sp)
				to.add_child(sp)
		9:
			factory.time_scale = [0.0, 0.25, 1.0, 1.0, 3.0][rng.randi() % 5]
		10:
			factory.set_is_factory_processing_bullets(rng.randf() < 0.7)
		11:
			stats["clears"] += 1
			match rng.randi() % 3:
				0:
					factory.clear_active_bullets()
				1:
					factory.clear_bullets_in_rect(Rect2(rng.randf_range(-400, 0), rng.randf_range(-400, 0), rng.randf_range(50, 600), rng.randf_range(50, 600)))
				2:
					factory.clear_bullets_in_circle(Vector2.ZERO, rng.randf_range(20.0, 300.0))
		12:
			stats["resets"] += 1
			var keyed := rng.randf() < 0.5
			var live := volleys.filter(func(v): return is_instance_valid(v))
			var key: VolleyPoolKey2D = factory.debug_get_pool_bucket(live[rng.randi() % live.size()]) if keyed and not live.is_empty() else null
			if rng.randf() < 0.5:
				factory.reset(key)
			else:
				factory.free_active_bullets(key)
		13:
			stats["pickups"] += 1
			factory.convert_all_bullets_to_pickups(pickup, Vector2(rng.randf_range(-200, 200), rng.randf_range(-200, 200)))
		14:
			var d: BulletVolleyData2D = datas[rng.randi() % datas.size()]
			factory.populate_bullets_pool(VolleyPoolKey2D.make(d.transforms.size(), _shape_type(d)), d, rng.randi_range(1, 3))
		15:
			# Live edits of the shared Resources (they reach bullets in flight).
			match rng.randi() % 4:
				0:
					flock.separation = rng.randf_range(0.0, 12.0)
				1:
					(zones[0] as BulletGrazeZone2D).ring_1_radius = rng.randf_range(5.0, 80.0)
				2:
					(chain.stages[0] as BulletSplitStage2D).count = rng.randi_range(1, 5)
				3:
					sound.max_voices = rng.randi_range(1, 8)
		16:
			var t = _live_target()
			if t != null:
				if rng.randf() < 0.5:
					t.free()
				else:
					t.position += Vector2(rng.randf_range(-100, 100), rng.randf_range(-100, 100))
			if targets.size() < 4:
				_add_target()
		17:
			var sp = _live_spawner()
			if sp != null:
				sp.split_enabled = not sp.split_enabled
				sp.flock_enabled = not sp.flock_enabled
		18:
			var live := volleys.filter(func(v): return is_instance_valid(v) and v.debug_get_life_state() == "active")
			if not live.is_empty():
				var v: BulletVolley2D = live[rng.randi() % live.size()]
				v.set_is_auto_pooling_enabled(rng.randf() < 0.5)
				var n := v.get_amount_bullets()
				for k in rng.randi_range(1, 4):
					var i := rng.randi() % n
					if v.is_bullet_status_enabled(i):
						v.disable_bullet(i, rng.randf() < 0.5)
					elif v.debug_get_life_state() != "pooled":
						v.enable_bullet(i)
		19:
			for k in 3:
				_add_target()
		20:
			factory.despawn_bounds_rect = Rect2(rng.randf_range(-600, -200), rng.randf_range(-600, -200), rng.randf_range(400, 1200), rng.randf_range(400, 1200))
			factory.despawn_bounds_enabled = rng.randf() < 0.6
		21:
			factory.use_physics_interpolation = not factory.use_physics_interpolation
		22:
			var sp = _live_spawner()
			if sp != null:
				sp.homing_target_source = BulletSpawner2D.HOMING_SOURCE_NODE_GROUP
				sp.homing_node_group = TARGET_GROUP
				sp.homing_enabled = true
				sp.orbiting_radius = rng.randf_range(20.0, 120.0)
				sp.orbiting_enabled = not sp.orbiting_enabled
		23:
			var sp = _live_spawner()
			if sp != null:
				sp.show_pattern_preview = rng.randf() < 0.5
				sp.show_preview_during_runtime = rng.randf() < 0.5
		24:
			if rng.randf() < 0.6:
				factory.populate_attachments_pool(probe_scene, rng.randi_range(1, 8))
			else:
				factory.free_attachments_pool()
		25:
			var live := volleys.filter(func(v): return is_instance_valid(v) and v.debug_get_life_state() == "active")
			if not live.is_empty():
				var shapes := [H.make_circle_shape(rng.randf_range(2.0, 10.0)), RectangleShape2D.new(), CapsuleShape2D.new()]
				(live[rng.randi() % live.size()] as BulletVolley2D).set_collision_shape_runtime_deferred(shapes[rng.randi() % shapes.size()])
		26:
			# A timer whose callback acts inside the sweep's timer pass.
			var live := volleys.filter(func(v): return is_instance_valid(v) and v.debug_get_life_state() == "active")
			if not live.is_empty():
				var v: BulletVolley2D = live[rng.randi() % live.size()]
				v.attach_time_based_function(rng.randf_range(0.02, 0.3), func() -> void: _handler(v, -1), rng.randf() < 0.5, rng.randf() < 0.5)
		27:
			var t = _live_target()
			if t != null:
				stats["pickups"] += 1
				factory.convert_all_bullets_to_pickups(pickup, t) # collected by homing onto a live node
		28:
			# A script frees one of the plugin's helper nodes (the factory
			# recreates what it needs).
			var names := ["BulletVolleysContainer", "BulletAttachmentsContainer", "BulletVolleysDebugger", "SpriteEffectsContainer", "SoundVoices"]
			var helper: Node = factory.get_node_or_null(names[rng.randi() % names.size()])
			if helper != null:
				volleys = volleys.filter(func(v): return is_instance_valid(v) and not helper.is_ancestor_of(v))
				helper.free()
		29:
			# A script frees one sound voice (playing or idle).
			var voices: Node = factory.get_node_or_null("SoundVoices")
			if voices != null and voices.get_child_count(true) > 0:
				voices.get_child(rng.randi() % voices.get_child_count(true), true).free()
		30:
			# Two spawners share one generator with runtime previews; one
			# toggles its preview (each owns its holder).
			var a = _live_spawner()
			var b = _live_spawner()
			if a != null and b != null:
				if shared_generator == null or not is_instance_valid(shared_generator):
					shared_generator = add(Node2D.new())
				shared_generator.position = Vector2(rng.randf_range(0, 800), rng.randf_range(0, 600))
				for sp in [a, b]:
					sp.set_transforms_generator(shared_generator)
					sp.show_preview_during_runtime = true
					sp.show_pattern_preview = true
				a.show_pattern_preview = rng.randf() < 0.5
		31:
			# Unlimited acceleration (top speed 0) on a volley in flight.
			var live := volleys.filter(func(v): return is_instance_valid(v) and v.debug_get_life_state() == "active")
			if not live.is_empty():
				var v: BulletVolley2D = live[rng.randi() % live.size()]
				var i := rng.randi() % v.get_amount_bullets()
				if v.is_bullet_status_enabled(i):
					v.set_bullet_speed_data(i, H.make_speed(rng.randf_range(0.0, 300.0), 0.0, [3e38, -3e38, 1e6][rng.randi() % 3]))
		32:
			# A spawner travels a path; its movement handlers stop, seek,
			# reverse or restart it.
			var sp = _live_spawner()
			if sp != null and not sp.has_meta(&"chaos_moves"):
				sp.set_meta(&"chaos_moves", true)
				var path := Path2D.new()
				path.curve = Curve2D.new()
				path.curve.add_point(Vector2(rng.randf_range(-200, 200), rng.randf_range(-200, 200)))
				path.curve.add_point(Vector2(rng.randf_range(-200, 200), rng.randf_range(-200, 200)))
				world.add_child(path)
				sp.set_movement_path_node(path)
				sp.movement_loop_mode = rng.randi() % 3
				sp.movement_duration_sec = rng.randf_range(0.05, 0.5)
				sp.movement_endpoint_reached.connect(func(_at_end) -> void: _move_handler(sp))
				sp.movement_loop_completed.connect(func(_loop) -> void: _move_handler(sp))
				sp.movement_finished.connect(func() -> void: _move_handler(sp))
				sp.movement_enabled = true


func _shape_type(d: BulletVolleyData2D) -> int:
	if d.collision_shape is RectangleShape2D:
		return PhysicsServer2D.SHAPE_RECTANGLE
	if d.collision_shape is CapsuleShape2D:
		return PhysicsServer2D.SHAPE_CAPSULE
	return PhysicsServer2D.SHAPE_CIRCLE


## Errors the step printed (claimed), minus the documented warnings.
func _step_errors() -> Array:
	var out: Array = []
	for err in get_errors():
		if err.handled:
			continue
		err.handled = true
		var text := str(err.code)
		if err.is_push_warning() and WARNING_OK.any(func(w): return text.to_lower().contains(w)):
			continue
		out.append(text)
	return out


func _check(where: String) -> Array:
	var problems: Array = []
	var books: Dictionary = factory.debug_assert_no_dangling()
	if not books["ok"]:
		problems.append("%s: books: %s" % [where, books["error"]])
	volleys = volleys.filter(func(v): return is_instance_valid(v))
	var live_bullets := 0
	for v in volleys:
		if v.debug_get_life_state() != "active":
			continue
		for i in v.get_amount_bullets():
			if v.is_bullet_status_enabled(i):
				live_bullets += 1
				if not v.get_bullet_transform(i).is_finite() or not v.get_bullet_velocity(i).is_finite():
					problems.append("%s: volley %d bullet %d is not finite" % [where, v.get_instance_id(), i])
					return problems
	stats["max_live"] = maxi(stats["max_live"], factory.get_active_bullet_count())
	for e in _step_errors():
		problems.append("%s: error: %s" % [where, e])
	return problems


func test_chaos_battlefield_never_breaks(chaos_seed: int = use_parameters(_seeds())) -> void:
	rng.seed = chaos_seed
	seed(chaos_seed)
	factory.debug_seed_cosmetic_rng(chaos_seed)
	_connect_handlers(factory)
	_build_world()
	for k in 6:
		_add_spawner()
	await idle(1)
	var problems: Array = []
	for step in STEPS:
		if not is_instance_valid(factory):
			problems.append("seed %d step %d: the factory is gone" % [chaos_seed, step])
			break
		for k in rng.randi_range(1, 3):
			_operate()
		if rng.randf() < 0.5:
			await physics(rng.randi_range(1, 3)) # real physics: collisions, bounces
			await idle(1)
		else:
			factory.debug_advance_time([0.0, 1.0 / 240.0, 1.0 / 60.0, 1.0 / 60.0, 1.0 / 30.0, 0.25][rng.randi() % 6])
			await idle(1) # queue_free()s from the handlers land (the books count them until then)
		problems.append_array(_check("seed %d step %d" % [chaos_seed, step]))
		if problems.size() > 0:
			break
	# Wind down: everything leaves, the pool comes back plain.
	factory.set_is_factory_processing_bullets(true)
	factory.time_scale = 1.0
	for sp in spawners:
		if is_instance_valid(sp):
			sp.free()
	await idle(2)
	factory.reset()
	await idle(2)
	problems.append_array(_check("seed %d wind-down" % chaos_seed))
	gut.p("chaos seed %d: %s" % [chaos_seed, stats])
	assert_eq("\n".join(problems), "", "seed %d: no error, balanced books, finite bullets" % chaos_seed)
	assert_gt(stats["shots"], 50, "the battlefield fired (%s)" % [stats])
