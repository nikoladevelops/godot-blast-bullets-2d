class_name FeatureMix
extends RefCounted
## Crosses any set of features through the REAL spawner path and checks what
## must hold whatever is mixed (not a suite: no test_ prefix).
##
## One run: a spawner auto-fires a ring of bullets into a small arena (bounce
## walls, kill walls, graze and homing targets), the features act on a fixed
## timeline (teleports, freezes, clears, pickups, an orphaning free), then
## every bullet leaves and the books must balance. Checked every frame:
## - the MixLedger rules (one death per life, every report names a live bullet
##   of its life, death triggers match the death, signals see live bullets);
## - every live bullet finite with a unit heading, the factory's books;
## - what is drawn: with interpolation off the multimesh buffer holds every
##   live bullet's pose after the tick; with it on the drawn pose is the
##   previous pose (the headless fraction is 0) and a teleported bullet never
##   streaks (previous == current right after the move); trails sit on the
##   drawn pose, also right after a teleport;
## and at the end: no bullet alive, every volley pooled, parked or freed.
## run_twice() repeats the run with a fresh spawner on the warm pool: no new
## volleys and the same deaths (pool reuse is neutral).
##
## Add a feature: a FEATURES entry, its setup in _configure, its timeline
## actions in _act, and the proof that it did something in _evidence (checked
## by the solo runs).

const B := preload("res://tests/common/blast_test_helpers.gd")

const FEATURES := [
	# motion
	"speed_curve", "accel_speed", "rotation_spin", "rotation_curve", "direction_curve", "wobble",
	"gravity", "movement_pattern", "texture_rotation", "inherited_velocity", "time_scale",
	# steering
	"homing", "orbit", "flock", "retarget", "aimed",
	# collision
	"bounce", "hits", "graze",
	# lifecycle
	"lifetime_short", "lifetime_handler", "split", "despawn_bounds", "area_clear", "teleport",
	"freeze_wake", "park", "pickups", "timers", "orphan", "life_times", "emitter",
	# visual
	"interpolation", "animation", "effects", "attachments", "random_scale", "random_jitter", "colors", "scale_in",
	# audio
	"sound",
	# spawner
	"burst", "telegraph", "spin", "muzzle_offset", "pattern_list", "spawner_movement", "max_live_bullets",
]
## A feature that only works with others on.
const NEEDS := {"orbit": ["homing"], "retarget": ["homing"], "inherited_velocity": ["spawner_movement"]}
## Everything the user named at once (plus pooling and lifetime).
const KITCHEN_SINK := ["orbit", "homing", "bounce", "speed_curve", "teleport", "interpolation", "sound",
	"animation", "effects", "split", "lifetime_handler", "graze", "flock", "hits", "burst", "telegraph",
	"pattern_list", "random_scale", "park", "freeze_wake"]

const N := 10 # bullets per shot
const TARGET_GROUP := &"mix_targets"
const FLIGHT_FRAMES := 150
const WIND_DOWN_FRAMES := 480
const T1_POS := Vector2(120, 60)
const T2_POS := Vector2(-110, -50)
## Warnings a mix may legitimately print (documented setup hints).
const WARNING_OK := [
	"direction curve",
	# orphaned_volleys Keep Flying: the freed spawner's connections died with
	# it, so expiries and hits of its bullets warn once (by contract).
	"after that spawner was freed",
]

var t # the running BlastTest
var factory: BulletFactory2D
var features := {}
var seed_value := 1
var label := ""
var rng := RandomNumberGenerator.new()
var ledger: MixLedger
var probe: MotionProbe
var spawner: BulletSpawner2D
var data: BulletVolleyData2D
var curves: BulletCurvesData2D
var world: Node2D
var t1: Node2D
var t2: Node2D
var path: Path2D
var pickup: BulletPickupData2D
var volleys: Array = [] # every volley seen (fired, split, converted)
var parked: Array = []
var frozen: Array = [] # [volley, life id, bullet]
var extended := {} # "volley:life" -> expiries seen
var problem_list: Array[String] = []
var stats := {}
var _samples := {} # evidence samples
var _spawner_start := Vector2.ZERO
var _last_clock := {} # volley id -> [life id, curves clock] at the last drawn check
var _last_origin := {} # "volley:life:bullet" -> origin at the last sample
var _marker := Resource.new()
var _links: Array = [] # factory connections, cut when the run ends
## Set true to record every live bullet's pose per frame (debugging a
## divergence between two runs): trace[frame] = {"<volley order>:<bullet>": pose}.
var record_trace := false
var trace: Array = []


func _link(obj: Object, sig: String, callable: Callable) -> void:
	obj.connect(sig, callable)
	_links.append([obj, sig, callable])


## `test` is the running BlastTest (its factory, builders and frames);
## `world_root` holds the run's nodes (freed after the run).
func _init(test, feature_names: Array, run_seed := 1, run_label := "") -> void:
	t = test
	factory = test.factory
	for f in feature_names:
		features[f] = true
	seed_value = run_seed
	label = run_label if run_label != "" else "+".join(feature_names)
	rng.seed = run_seed


func has(f: String) -> bool:
	return features.has(f)


func _problem(text: String) -> void:
	if problem_list.size() < 24:
		problem_list.append("[%s seed %d] %s" % [label, seed_value, text])


func _stat(key: String, n := 1) -> void:
	stats[key] = int(stats.get(key, 0)) + n


# ---------------------------------------------------------------- the world

func _build_world() -> void:
	world = Node2D.new()
	t.add(world)
	t1 = Node2D.new()
	t1.position = T1_POS
	world.add_child(t1)
	t1.add_to_group(TARGET_GROUP)
	if has("retarget"):
		t2 = Node2D.new()
		t2.position = T2_POS
		world.add_child(t2)
		t2.add_to_group(TARGET_GROUP)
	if has("bounce"):
		for x in [-130.0, 130.0]:
			world.add_child(_body(Vector2(x, 0), Vector2(20, 600), 8)) # layer 4
	if has("hits"):
		world.add_child(_body(Vector2(0, -150), Vector2(600, 20), 4)) # layer 3
		var area := Area2D.new()
		area.position = Vector2(0, 150)
		area.collision_layer = 4
		area.monitorable = true
		var col := CollisionShape2D.new()
		var box := RectangleShape2D.new()
		box.size = Vector2(600, 20)
		col.shape = box
		area.add_child(col)
		world.add_child(area)
	if has("spawner_movement"):
		path = Path2D.new()
		path.curve = Curve2D.new()
		path.curve.add_point(Vector2(-40, 0))
		path.curve.add_point(Vector2(40, 10))
		world.add_child(path)


func _body(pos: Vector2, size: Vector2, layer: int) -> StaticBody2D:
	var body := StaticBody2D.new()
	body.position = pos
	body.collision_layer = layer
	body.collision_mask = 0
	var col := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = size
	col.shape = box
	body.add_child(col)
	return body


func _data() -> BulletVolleyData2D:
	var d := B.make_volley_data(N, 160.0, 0.6 if has("lifetime_short") else 1.2)
	d.shared_bullet_speed_data = d.all_bullet_speed_data[0]
	d.all_bullet_speed_data = []
	d.collision_shape = B.make_circle_shape(5.0)
	d.monitorable = true
	d.bullet_max_collision_count = 0
	var mask: Array = []
	if has("hits"):
		mask.append(3)
		d.bullet_max_collision_count = rng.randi_range(1, 3)
	if has("bounce"):
		mask.append(4)
		d.set_bounce_mask_from_array([4])
		d.bounce_mode = 1 # precise: flat walls
	d.set_collision_mask_from_array(mask)
	return d


func _curves() -> BulletCurvesData2D:
	if curves == null:
		curves = BulletCurvesData2D.new()
		data.shared_bullet_curves_data = curves
	return curves


func _ramp(a: float, b: float, seconds: float) -> Curve:
	var c := Curve.new()
	c.min_value = -10000.0
	c.max_value = 10000.0
	c.max_domain = seconds
	c.add_point(Vector2(0, a))
	c.add_point(Vector2(seconds, b))
	return c


# ---------------------------------------------------------------- features

func _configure() -> void:
	var sp := spawner
	var d := data
	if has("speed_curve"):
		_curves().movement_speed_curve = _ramp(90.0, 260.0, 1.0)
		curves.movement_use_unit_curve = false
	if has("accel_speed"):
		d.shared_bullet_speed_data = B.make_speed(80.0, 420.0, 300.0)
	if has("rotation_spin"):
		d.shared_bullet_rotation_data = B.make_rotation(4.0, 8.0)
		d.adjust_direction_based_on_rotation = rng.randf() < 0.5
	if has("rotation_curve"):
		_curves().rotation_speed_curve = B.make_flat_curve(3.0)
		curves.rotation_use_unit_curve = false
	if has("direction_curve"):
		_curves().x_direction_curve = B.make_flat_curve(0.3)
		curves.x_direction_use_unit_curve = false
	if has("wobble"):
		var w := BulletWobbleData2D.new()
		w.enabled = true
		w.mode = BulletWobbleData2D.WOBBLE_LATERAL if rng.randf() < 0.5 else BulletWobbleData2D.WOBBLE_ANGULAR
		w.amplitude = 8.0 if w.mode == BulletWobbleData2D.WOBBLE_LATERAL else 0.3
		w.frequency_hz = 3.0
		d.shared_bullet_wobble_data = w
	if has("gravity"):
		d.gravity = Vector2(0, 120)
	if has("texture_rotation"):
		d.texture_rotation_radians = PI / 2.0
	if has("time_scale"):
		sp.time_scale = 0.5 if rng.randf() < 0.5 else 1.5
		factory.time_scale = 1.6 if rng.randf() < 0.5 else 0.75
	if has("homing"):
		sp.homing_node_group = TARGET_GROUP
		sp.homing_smoothing = 6.0
		sp.homing_take_control_of_texture_rotation = true
		sp.homing_distance_before_reached = 12.0
		# A fixed retarget phase: the default staggers spawners by instance id,
		# so two runs would retarget on different ticks (and diverge).
		sp.homing_retarget_phase = 0.1
		sp.set_homing_enabled(true)
	if has("orbit"):
		sp.orbiting_radius = 40.0
		sp.orbiting_enabled = true
	if has("flock"):
		sp.flock_enabled = true
	if has("retarget"):
		sp.homing_retarget_interval_sec = 0.2
		sp.retarget_applied.connect(func(_n) -> void: _stat("retargets"))
	if has("aimed"):
		sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_AIMED
		sp.set_helper_aimed_target(t1)
	if has("hits"):
		for s in ["area_entered", "body_entered"]:
			sp.connect(s, func(_t, _v, _i) -> void: _stat("hits_handled"))
	if has("graze"):
		sp.graze_zones = [B.make_graze_zone([30.0, 60.0])]
		sp.graze_node_group = TARGET_GROUP
		sp.graze_enabled = true
	if has("lifetime_handler"):
		d.is_life_time_over_signal_enabled = true
		sp.life_time_over.connect(_on_life_time_over)
		_link(factory, "life_time_over", _on_life_time_over) # orphans and split children
	if has("split"):
		var stage := BulletSplitStage2D.new()
		stage.count = 2
		stage.split_after_sec = 0.25
		stage.split_on_lifetime_over = true
		stage.split_on_hit = true
		stage.split_on_bounce = has("bounce")
		stage.parent_survives = rng.randf() < 0.5
		stage.max_splits_per_bullet = 1
		var chain := BulletSplitData2D.new()
		chain.stages = [stage]
		sp.split_data = chain
		sp.split_enabled = true
	if has("despawn_bounds"):
		factory.despawn_bounds_rect = Rect2(-200, -120, 400, 240)
		factory.despawn_bounds_enabled = true
	if has("pickups"):
		pickup = BulletPickupData2D.new()
		pickup.magnet_delay_sec = 0.1
		pickup.magnet_acceleration = 3000.0
		pickup.magnet_max_speed = 1500.0
		pickup.drag = 2.0
		pickup.collect_radius = 20.0
		pickup.lifetime_sec = 4.0
		_link(factory, "pickup_collected", func(_t, _p, _v, _i, _val) -> void: _stat("collected"))
	if has("orphan"):
		sp.orphaned_volleys = rng.randi_range(0, 3)
	if has("interpolation"):
		factory.physics_interpolation = BulletFactory2D.INTERPOLATION_ON
	if has("animation"):
		d.sprite_frames = B.make_effect_frames(4, 20.0) # 0.2 s, no loop
	if has("effects"):
		var layers: Array = []
		for trigger in 9: # trail + every one-shot trigger
			layers.append(B.make_effect_layer(trigger))
		d.effect_layers = layers
	if has("attachments"):
		d.shared_bullet_attachment = t.make_probe_scene()
	if has("random_scale"):
		sp.bullet_scale_mode = 1 # Uniform
		sp.bullet_scale_min = 0.6
		sp.bullet_scale_max = 1.6
		sp.bullet_random_seed = seed_value + 11
	if has("colors"):
		sp.bullet_color_mode = rng.randi_range(1, 3)
		var g := Gradient.new()
		g.colors = PackedColorArray([Color(1.0, 0.2, 0.2), Color(0.2, 0.4, 1.0)])
		sp.bullet_color_gradient = g if rng.randf() < 0.5 else null # null: the rainbow
	if has("life_times"):
		sp.bullet_life_time_scale_min = 0.6
		sp.bullet_life_time_scale_max = 1.4
		if sp.bullet_random_seed == 0:
			sp.bullet_random_seed = seed_value + 29
	if has("scale_in"):
		d.scale_in_sec = 0.2
		d.scale_in_from = 0.3
	if has("emitter"):
		# Every flying bullet fires a small template's ring twice.
		var tpl := BulletSpawner2D.new()
		tpl.set_shooting_enabled(false)
		tpl.set_homing_enabled(false)
		tpl.set_spawn_data(B.make_volley_data(2, 120.0, 0.4))
		tpl.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
		tpl.helper_bullets_amount = 2
		t.add(tpl)
		tpl.set_bullet_factory(factory)
		tpl.volley_fired.connect(func(v, _n) -> void:
			_stat("emitted")
			volleys.append(v))
		var e := BulletEmitterData2D.new()
		e.interval_sec = 0.3
		e.max_shots = 2
		sp.emitter_enabled = true
		sp.emitter = e
		sp.emitter_spawner_path = sp.get_path_to(tpl)
	if has("random_jitter"):
		sp.bullet_rotation_min_degrees = -15.0
		sp.bullet_rotation_max_degrees = 15.0
		sp.bullet_position_jitter_min = 0.0
		sp.bullet_position_jitter_max = 5.0
		sp.bullet_speed_scale_min = 0.8
		sp.bullet_speed_scale_max = 1.3
		sp.bullet_random_seed = seed_value + 23
	if has("sound"):
		var entries: Array = []
		for trigger in 13:
			var s := B.make_sound(trigger, 2)
			s.follow_bullet = trigger == BulletSoundData2D.SOUND_ON_FLIGHT
			entries.append(s)
		sp.sound_effects = entries
		sp.sound_enabled = true
	if has("burst"):
		sp.burst_count = 3
		sp.burst_interval_sec = 0.05
		sp.burst_enabled = true
		sp.burst_shot_fired.connect(func(_n, _m) -> void: _stat("burst_shots"))
	if has("telegraph"):
		sp.telegraph_sec = 0.1
		sp.telegraph_enabled = true
		sp.volley_telegraphed.connect(func(_aim) -> void: _stat("telegraphs"))
	if has("spin"):
		sp.spin_speed_deg_per_sec = 200.0
		sp.spin_enabled = true
	if has("muzzle_offset"):
		sp.spawn_position_offset = Vector2(14, 0)
		sp.spawn_position_offset_space = BulletSpawner2D.SPAWN_OFFSET_LOCAL
	if has("pattern_list"):
		sp.pattern_list_finished.connect(func() -> void: _stat("pattern_lists"))
	if has("spawner_movement"):
		sp.set_movement_path_node(path)
		sp.movement_duration_sec = 0.4
		sp.movement_loop_mode = BulletSpawner2D.MOVEMENT_LOOP_PING_PONG
		sp.movement_enabled = true
		sp.inherit_movement_velocity = has("inherited_velocity")
	if has("max_live_bullets"):
		sp.max_live_bullets = 15
	sp.volley_skipped.connect(func(reason: StringName) -> void: _stat("skipped:" + String(reason)))
	sp.volley_fired.connect(_on_fired)
	sp.bullet_split.connect(func(_p, _c, children, _d) -> void: _seen(children))
	_link(factory, "bullet_split", func(_p, _c, children, _d) -> void: _seen(children))
	_link(factory, "bullets_converted", func(_n, list) -> void:
		for v in list:
			_seen(v))


func _seen(v: Object) -> void:
	if is_instance_valid(v) and not volleys.has(v):
		volleys.append(v)
		probe.add_volley(v, data.texture_rotation_radians, Vector2(16, 16))


func _on_fired(v: BulletVolley2D, _n: int) -> void:
	_stat("fired")
	_seen(v)
	if has("movement_pattern"):
		var c := Curve2D.new()
		for p in [Vector2(0, 0), Vector2(30, 10), Vector2(60, -10), Vector2(90, 0)]:
			c.add_point(p)
		v.set_shared_movement_pattern_curve(c)
	if has("timers"):
		var id := v.get_instance_id()
		var life := v.get_life_id()
		v.attach_time_based_function(0.1, func() -> void:
			_stat("timer_calls")
			var vv = instance_from_id(id)
			if is_instance_valid(vv) and vv.get_life_id() == life and vv.debug_get_life_state() == "active":
				vv.bullet_set_custom_data(0, _marker), true)
	if has("park") and int(stats.get("fired", 0)) % 2 == 1:
		v.set_is_auto_pooling_enabled(false)
		parked.append(v)


func _on_life_time_over(v: BulletVolley2D, idx: Array) -> void:
	var key := "%d:%d" % [v.get_instance_id(), v.get_life_id()]
	extended[key] = int(extended.get(key, 0)) + 1
	if int(extended[key]) == 1:
		v.set_life_time_left(0.15) # the whole volley lives on
		_stat("extensions")
	elif not idx.is_empty() and v.is_bullet_status_enabled(idx[0]):
		v.disable_bullet(idx[0]) # the handler takes one bullet itself


func _live() -> Array:
	return volleys.filter(func(v): return is_instance_valid(v) and v.debug_get_life_state() == "active")


func _act(frame: int) -> void:
	if has("pattern_list") and frame == 5 and is_instance_valid(spawner):
		var alt := data.duplicate() as BulletVolleyData2D
		alt.collision_shape = RectangleShape2D.new()
		spawner.spawn_pattern_list([{"pattern_source": BulletSpawner2D.PATTERN_FROM_HELPER_FAN, "helper_bullets_amount": 6}, {"spawn_data": alt}], false, 0.1)
	if has("teleport") and frame in [9, 15, 21, 27]:
		_stat("teleports")
		var moved: Array = []
		for v in _live():
			var n: int = v.get_amount_bullets()
			match frame:
				9:
					if v.is_bullet_status_enabled(0):
						v.teleport_bullet(0, v.get_bullet_transform(0).origin + Vector2(10, -10))
						moved.append([v, 0])
				15, 27:
					if frame == 15:
						v.teleport_shift_all_bullets(Vector2(-6, 4))
					for i in n:
						if v.is_bullet_status_enabled(i):
							moved.append([v, i])
				21:
					var i := mini(1, n - 1)
					if v.is_bullet_status_enabled(i):
						var tr: Transform2D = v.get_bullet_transform(i)
						v.set_bullet_transform(i, Transform2D(tr.get_rotation(), tr.get_scale(), 0.0, tr.origin + Vector2(4, 4)), false)
						moved.append([v, i])
		if frame == 27:
			factory.teleport_shift_all_bullets(Vector2(3, 0))
		_check_moved_now("after the teleports of frame %d" % frame, moved)
	if has("freeze_wake"):
		if frame == 12:
			for v in _live():
				for i in mini(2, v.get_amount_bullets()):
					if v.is_bullet_status_enabled(i):
						v.disable_bullet(i, false)
						frozen.append([v, v.get_life_id(), i])
		elif frame == 22:
			var woken: Array = []
			for e in frozen:
				var v = e[0]
				if is_instance_valid(v) and not v.is_pooled() and v.get_life_id() == e[1] and not v.is_bullet_status_enabled(e[2]):
					v.enable_bullet(e[2])
					_stat("wakes")
					woken.append([v, e[2]])
			frozen.clear()
			_check_moved_now("after the wakes", woken)
	if has("area_clear") and frame == 24:
		factory.clear_bullets_in_circle(Vector2.ZERO, 100.0)
	if has("pickups") and frame == 36:
		factory.convert_all_bullets_to_pickups(pickup, t1)
	if has("park") and frame == 100: # the first parked volley expired at ~72
		for v in parked:
			if is_instance_valid(v) and v.is_parked():
				v.enable_bullet(0) # wakes the whole parked volley
				_stat("parked_wakes")
				_check_moved_now("after a parked wake", [[v, 0]])
	if has("retarget") and t2 != null:
		t2.position += Vector2(2, 1)
	if has("orphan") and frame == 50 and is_instance_valid(spawner):
		spawner.queue_free()
		_stat("orphaned")


func _evidence(f: String) -> String:
	var c := ledger.counts
	var ok := true
	match f:
		"speed_curve", "accel_speed", "gravity": # gravity adds a fall speed
			ok = float(_samples.get("speed_max", 0.0)) > float(_samples.get("speed_min", INF)) + 20.0
		"rotation_spin", "rotation_curve":
			# The sprite turns; the heading too with adjust_direction_based_on_rotation.
			ok = float(_samples.get("turn", 0.0)) > 0.05 or float(_samples.get("facing_offset", 0.0)) > 0.5
		"direction_curve", "wobble", "movement_pattern", "homing":
			# The heading turns, or the bullet moves off it (a lateral wobble, a pattern).
			ok = float(_samples.get("turn", 0.0)) > 0.05 or float(_samples.get("path_bend", 0.0)) > 0.1
		"texture_rotation":
			ok = float(_samples.get("facing_offset", 0.0)) > 1.0
		"inherited_velocity":
			ok = float(_samples.get("drift", 0.0)) > 1.0 # the inherited offset of some volley
		"orbit":
			ok = int(stats.get("orbit_locked", 0)) > 0
		"flock":
			ok = int(stats.get("flock_members", 0)) > 0
		"retarget":
			ok = int(stats.get("retargets", 0)) > 0
		"aimed", "muzzle_offset", "time_scale", "interpolation", "random_jitter":
			ok = int(stats.get("fired", 0)) > 0
		"bounce":
			ok = int(c.get("signal:bounce_body_entered", 0)) > 0
		"hits":
			ok = int(c.get("disable:hit", 0)) > 0
		"graze":
			ok = int(c.get("signal:bullet_grazed", 0)) > 0
		"lifetime_short":
			ok = int(c.get("disable:lifetime", 0)) > 0
		"lifetime_handler":
			ok = int(stats.get("extensions", 0)) > 0
		"split":
			ok = int(c.get("signal:bullet_split", 0)) > 0
		"despawn_bounds":
			ok = int(c.get("disable:despawn", 0)) > 0
		"area_clear":
			ok = int(c.get("disable:clear", 0)) > 0
		"teleport":
			ok = int(stats.get("teleports", 0)) == 4
		"freeze_wake":
			ok = int(stats.get("wakes", 0)) > 0
		"park":
			ok = int(stats.get("parked_wakes", 0)) > 0
		"pickups":
			ok = int(c.get("disable:pickup", 0)) > 0 and int(c.get("disable:collect", 0)) > 0
		"timers":
			ok = int(stats.get("timer_calls", 0)) > 0
		"orphan":
			ok = int(stats.get("orphaned", 0)) == 1
		"animation":
			ok = int(c.get("signal:sprite_animation_finished", 0)) > 0
		"effects":
			ok = int(c.get("effect:1", 0)) > 0 and int(stats.get("trail_checks", 0)) > 0
		"attachments":
			ok = int(stats.get("attachments_seen", 0)) > 0
		"random_scale":
			ok = float(_samples.get("scale_max", 0.0)) > float(_samples.get("scale_min", INF)) + 0.1
		"colors":
			ok = int(stats.get("colored", 0)) > 0
		"life_times":
			ok = float(_samples.get("life_spread", 0.0)) > 0.05
		"scale_in":
			ok = int(stats.get("scale_in_seen", 0)) > 0
		"emitter":
			ok = int(stats.get("emitted", 0)) > 0
		"sound":
			ok = int(c.get("offer:0", 0)) > 0 and int(c.get("offer:10", 0)) > 0
		"burst":
			ok = int(stats.get("burst_shots", 0)) > 0
		"telegraph":
			ok = int(stats.get("telegraphs", 0)) > 0
		"spin":
			ok = float(_samples.get("spin", 0.0)) > 1.0
		"pattern_list":
			ok = int(stats.get("pattern_lists", 0)) > 0
		"spawner_movement":
			ok = float(_samples.get("spawner_moved", 0.0)) > 1.0
		"max_live_bullets":
			ok = int(stats.get("skipped:over_budget", 0)) > 0
	return "" if ok else "%s did nothing (stats %s, counts %s, samples %s)" % [f, stats, c, _samples]


# ---------------------------------------------------------------- the checks

static func _instance(buf: PackedFloat32Array, i: int, stride := 8) -> Transform2D:
	var b := i * stride # 12 with per-bullet colors (the color follows the 2D transform)
	return Transform2D(Vector2(buf[b], buf[b + 4]), Vector2(buf[b + 1], buf[b + 5]), Vector2(buf[b + 3], buf[b + 7]))


## Right after the tick: the multimesh buffer was uploaded whole by the
## tick's flush (interpolation off) or the interpolation pass (on). Only
## volleys that ticked since the last check are compared: the headless
## renderer keeps whole-buffer uploads only (per-instance writes such as a
## spawner's muzzle-offset shift are dropped until the next upload).
func _check_drawn(where: String) -> void:
	var interp := factory.is_physics_interpolation_active()
	for v in _live():
		var clock: float = v.debug_get_clocks()["curves_elapsed_time"]
		var last: Array = _last_clock.get(v.get_instance_id(), [-1, -1.0])
		_last_clock[v.get_instance_id()] = [v.get_life_id(), clock]
		if int(last[0]) != v.get_life_id() or not (clock > float(last[1])):
			continue
		var buf: PackedFloat32Array = v.multimesh.buffer
		var stride := 12 if v.is_using_bullet_colors() else 8
		if buf.size() != v.get_amount_bullets() * stride:
			continue
		var inv: Transform2D = v.get_global_transform().affine_inverse()
		var trails: bool = v.has_trail_effects()
		for i in v.get_amount_bullets():
			if not v.is_bullet_status_enabled(i):
				continue
			var want: Vector2 = (inv * v.get_bullet_transform(i)).origin if not interp else (inv * v.debug_get_previous_origin(i))
			var got := _instance(buf, i, stride).origin
			if got.distance_to(want) > 0.05:
				_problem("%s: bullet %d of volley %d drawn at %s, expected %s (interpolation %s)" % [where, i, v.get_instance_id(), got, want, interp])
				return
			if trails:
				_stat("trail_checks")
				var trail: Vector2 = v.debug_get_trail_transform(0, i).origin
				var trail_want: Vector2 = v.get_bullet_transform(i).origin if not interp else v.debug_get_previous_origin(i)
				if trail.distance_to(trail_want) > 0.05:
					_problem("%s: trail of bullet %d of volley %d at %s, the bullet is drawn at %s" % [where, i, v.get_instance_id(), trail, trail_want])
					return


## Right after a script moved or woke bullets ([volley, bullet] pairs): no
## streak (the interpolation history starts at the new pose) and the trail
## moved with the bullet.
func _check_moved_now(where: String, moved: Array) -> void:
	var interp := factory.is_physics_interpolation_active()
	for m in moved:
		var v = m[0]
		var i: int = m[1]
		if not is_instance_valid(v) or v.is_pooled() or not v.is_bullet_status_enabled(i):
			continue
		var now: Vector2 = v.get_bullet_transform(i).origin
		if interp and v.debug_get_previous_origin(i).distance_to(now) > 0.05:
			_problem("%s: bullet %d of volley %d would streak from %s to %s" % [where, i, v.get_instance_id(), v.debug_get_previous_origin(i), now])
			return
		if v.has_trail_effects() and v.debug_get_trail_transform(0, i).origin.distance_to(now) > 0.05:
			_problem("%s: trail of bullet %d of volley %d left behind at %s, the bullet is at %s" % [where, i, v.get_instance_id(), v.debug_get_trail_transform(0, i).origin, now])
			return


func _check_books(where: String) -> void:
	var books: Dictionary = factory.debug_assert_no_dangling()
	if not books.get("ok", false):
		_problem("%s: books: %s" % [where, books.get("error", "")])
	for v in _live():
		var bad: Array = t.broken_bullets(v)
		if not bad.is_empty():
			_problem("%s: volley %d broken bullets %s" % [where, v.get_instance_id(), bad.slice(0, 3)])


func _check_errors(where: String) -> void:
	for err in t.get_errors():
		if err.handled:
			continue
		err.handled = true
		var text := str(err.code)
		if err.is_push_warning() and WARNING_OK.any(func(w): return text.to_lower().contains(w)):
			continue
		_problem("%s: %s %s" % [where, "warning" if err.is_push_warning() else "error", text])


func _sample() -> void:
	for v in _live():
		var life_lo := INF
		var life_hi := -INF
		for i in v.get_amount_bullets():
			if not v.is_bullet_status_enabled(i):
				continue
			if v.is_using_bullet_colors() and v.get_bullet_color(i) != Color(1, 1, 1, 1):
				_stat("colored")
			var left: float = v.get_bullet_life_time_left(i)
			life_lo = minf(life_lo, left)
			life_hi = maxf(life_hi, left)
			var speed: float = v.get_bullet_velocity(i).length()
			_samples["speed_max"] = maxf(float(_samples.get("speed_max", 0.0)), speed)
			_samples["speed_min"] = minf(float(_samples.get("speed_min", INF)), speed)
			var tr: Transform2D = v.get_bullet_transform(i)
			var sc := tr.get_scale().x
			_samples["scale_max"] = maxf(float(_samples.get("scale_max", 0.0)), sc)
			_samples["scale_min"] = minf(float(_samples.get("scale_min", INF)), sc)
			if has("scale_in") and sc < 0.5:
				_stat("scale_in_seen") # spawned at 30 % of its size
			var dir: Vector2 = v.get_bullet_direction(i)
			var okey := "%d:%d:%d" % [v.get_instance_id(), v.get_life_id(), i]
			if _last_origin.has(okey):
				var step: Vector2 = tr.origin - _last_origin[okey]
				if step.length() > 0.5 and dir.length() > 0.5:
					_samples["path_bend"] = maxf(float(_samples.get("path_bend", 0.0)), absf(angle_difference(step.angle(), dir.angle())))
			_last_origin[okey] = tr.origin
			_samples["facing_offset"] = maxf(float(_samples.get("facing_offset", 0.0)), absf(angle_difference(tr.x.angle(), dir.angle())))
			_samples["drift"] = maxf(float(_samples.get("drift", 0.0)), v.get_inherited_velocity_offset().length())
			if v.bullet_is_orbiting_locked(i):
				_stat("orbit_locked")
			if v.bullet_get_attachment(i) != null:
				_stat("attachments_seen")
		if life_hi > life_lo:
			_samples["life_spread"] = maxf(float(_samples.get("life_spread", 0.0)), life_hi - life_lo)
	var turned := probe.turn_sum_deg
	_samples["turn"] = turned
	if is_instance_valid(spawner):
		_samples["spawner_moved"] = maxf(float(_samples.get("spawner_moved", 0.0)), spawner.global_position.distance_to(_spawner_start))
		_samples["spin"] = maxf(float(_samples.get("spin", 0.0)), absf(spawner.get_spin_angle_deg()))
	if has("flock"):
		stats["flock_members"] = maxi(int(stats.get("flock_members", 0)), int(factory.debug_get_flock_stats().get("members", 0)))


# ---------------------------------------------------------------- the run

## One full run; returns its problems (empty = clean).
func run() -> Array[String]:
	seed(seed_value)
	factory.debug_seed_cosmetic_rng(seed_value)
	_build_world()
	data = _data()
	spawner = t.make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, N)
	spawner.helper_ring_radius = 24.0
	_spawner_start = spawner.global_position
	ledger = MixLedger.new()
	ledger.attach(factory, [spawner]) # first: before every handler below
	probe = MotionProbe.new(factory, [])
	_configure()
	if has("spawner_movement"):
		spawner.movement_play()
	spawner.shoot_interval_sec = 0.35
	spawner.shoot_initial_delay_sec = 0.0
	spawner.max_volleys = 3 * (3 if has("burst") else 1)
	spawner.shooting_enabled = true
	_check_errors("setup")
	for frame in FLIGHT_FRAMES:
		await t.physics(1)
		if record_trace:
			var poses := {}
			for v in _live():
				for i in v.get_amount_bullets():
					if v.is_bullet_status_enabled(i):
						poses["%d:%d" % [volleys.find(v), i]] = [v.get_bullet_transform(i), v.get_bullet_direction(i), v.get_bullet_velocity(i), v.debug_get_orbiting_info(i), v.debug_get_bullet_flock_info(i) if has("flock") else {}, v.debug_get_bullet_info(i).merged({"bounces": v.bullet_get_bounce_count(i)}), v.debug_get_clocks(), v.shared_homing_deque_check_homing_targets_amount(), v.get_life_id()]
			trace.append(poses)
		_check_drawn("frame %d" % frame)
		probe.sample(1.0 / 60.0)
		_sample()
		_act(frame)
		ledger.pull()
		if frame % 10 == 0:
			_check_books("frame %d" % frame)
		_check_errors("frame %d" % frame)
		if problem_list.size() > 4:
			break
	# Wind-down: no more shots; everything leaves (lifetime, pickups).
	if is_instance_valid(spawner):
		spawner.shooting_enabled = false
		spawner.stop_pattern_list()
		spawner.burst_enabled = false
		spawner.telegraph_enabled = false
	var left := -1
	for frame in WIND_DOWN_FRAMES:
		await t.physics(1)
		_check_drawn("wind-down %d" % frame)
		probe.sample(1.0 / 60.0)
		ledger.pull()
		_check_errors("wind-down %d" % frame)
		left = factory.get_active_bullet_count()
		if left == 0 and factory.get_pending_split_count() == 0:
			break
	await t.idle(2)
	ledger.pull()
	_check_books("end")
	_check_errors("end")
	if left != 0:
		_problem("%d bullets still alive after the wind-down (%s)" % [left, ledger.terminal_summary()])
	if ledger.alive_count() != 0:
		_problem("the ledger still sees %d live bullets" % ledger.alive_count())
	for v in volleys:
		if is_instance_valid(v) and not v.is_pooled() and not v.is_parked() and not v.is_queued_for_deletion():
			_problem("volley %d is neither pooled, parked nor freed: %s" % [v.get_instance_id(), v.debug_get_life_state()])
	if probe.nonfinite > 0:
		_problem("%d non-finite poses" % probe.nonfinite)
	for p in ledger.problems():
		_problem("ledger: " + p)
	ledger.detach()
	for l in _links:
		if is_instance_valid(l[0]) and (l[0] as Object).is_connected(l[1], l[2]):
			(l[0] as Object).disconnect(l[1], l[2])
	_links.clear()
	if is_instance_valid(spawner):
		spawner.queue_free()
	world.queue_free()
	await t.idle(2)
	factory.time_scale = 1.0
	factory.despawn_bounds_enabled = false
	factory.physics_interpolation = BulletFactory2D.INTERPOLATION_OFF
	return problem_list


## The solo proof: the feature (with what it needs) visibly did something.
func evidence() -> Array[String]:
	var out: Array[String] = []
	for f in features:
		var e := _evidence(f)
		if e != "":
			out.append("[%s] %s" % [label, e])
	return out


## Runs the same mix twice: the second run (a fresh spawner, the same seed)
## flies on the warm pool. Returns every problem of both runs plus any
## difference the pool made.
static func run_twice(test, feature_names: Array, run_seed := 1, run_label := "", check_evidence := false) -> Array[String]:
	var first := FeatureMix.new(test, feature_names, run_seed, run_label)
	var out: Array[String] = await first.run()
	if check_evidence:
		out.append_array(first.evidence())
	if not out.is_empty():
		return out
	test.factory.debug_reset_pool_stats()
	var second := FeatureMix.new(test, feature_names, run_seed, run_label + " (warm pool)")
	out.append_array(await second.run())
	var misses := int(test.factory.debug_get_pool_hit_stats()["misses"])
	var parked_first := first.parked.size()
	if misses > parked_first:
		out.append("[%s] the warm run allocated %d new volleys (%d were parked by the first run)" % [first.label, misses, parked_first])
	var a := first.ledger.terminal_summary()
	var b := second.ledger.terminal_summary()
	if a != b:
		out.append("[%s] the warm run ended its bullets differently: cold %s, warm %s" % [first.label, a, b])
	return out


# ---------------------------------------------------------------- combos

## Mixes covering every feature pair at least once (greedy covering array,
## seeded), `size` features each plus what they need, and the kitchen sink.
static func cover(cover_seed := 1, size := 8, skip: Array = []) -> Array:
	var r := RandomNumberGenerator.new()
	r.seed = cover_seed
	var names: Array = FEATURES.filter(func(f): return not skip.has(f))
	var uncovered := {}
	for a in names.size():
		for b in range(a + 1, names.size()):
			uncovered[_pair(names[a], names[b])] = true
	var combos: Array = []
	while not uncovered.is_empty():
		var keys: Array = uncovered.keys()
		var start: Array = String(keys[r.randi() % keys.size()]).split("+")
		var combo: Array = [start[0], start[1]]
		while combo.size() < size:
			var best := ""
			var best_gain := -1
			for f in names:
				if combo.has(f):
					continue
				var gain := 0
				for m in combo:
					if uncovered.has(_pair(f, m)):
						gain += 1
				if gain > best_gain or (gain == best_gain and r.randf() < 0.3):
					best = f
					best_gain = gain
			if best == "":
				break
			combo.append(best)
		combo = _with_needs(combo)
		for a in combo.size():
			for b in range(a + 1, combo.size()):
				uncovered.erase(_pair(combo[a], combo[b]))
		combos.append(combo)
	combos.append(_with_needs(KITCHEN_SINK.filter(func(f): return not skip.has(f))))
	return combos


static func _pair(a: String, b: String) -> String:
	return a + "+" + b if a < b else b + "+" + a


static func _with_needs(combo: Array) -> Array:
	var out := combo.duplicate()
	for f in combo:
		for need in NEEDS.get(f, []):
			if not out.has(need):
				out.append(need)
	return out


## The cover seeds of this run: MIX_SEEDS=<n> (seeds 1..n) or 1.
static func seeds() -> Array:
	var env := OS.get_environment("MIX_SEEDS")
	if env == "":
		return [1]
	var out: Array = []
	for s in range(1, int(env) + 1):
		out.append(s)
	return out


## Parameters for slice `k` of `of`: [cover seed, combo index, features].
## MIX_ONLY=<seed>:<index> runs one combo; MIX_SKIP=<f,g> leaves features out.
static func slice(k: int, of: int) -> Array:
	var skip := Array(OS.get_environment("MIX_SKIP").split(",", false))
	var only := OS.get_environment("MIX_ONLY")
	var out: Array = []
	for s in seeds():
		var combos := cover(s, 8, skip)
		for i in combos.size():
			var id := "%d:%d" % [s, i]
			if only != "" and only != id:
				continue
			if only != "" or i % of == k:
				out.append([s, i, combos[i]])
	return out
