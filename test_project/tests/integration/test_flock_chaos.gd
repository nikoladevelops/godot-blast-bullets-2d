extends BlastTest
## Chaos sweep around flocking: seeded random sequences over real frames of
## everything that touches a flock at once (spawner shots under every scope,
## flocks swapped/toggled/edited in flight, factory volleys joining and
## leaving, homing and orbiting swarms, bounce walls, grazing targets,
## freezes and wakes, teleports, velocity sets, clears, pickup conversions,
## time scale and despawn bounds changes, spawners freed mid-flight (orphans),
## mixed bullet counts churning the pool, and handlers inside the sweep that
## join, leave, edit and free). After every step the books must balance: no
## errors, a dangling-free factory (the armed count included), finite poses
## and unit headings, neighbor counts within the cap. After a full reset no
## flock resource may be kept alive by the plugin.

const P := preload("res://tests/common/pickup_test_helpers.gd")
const SEEDS := [3, 17, 42, 77, 128, 255]
const STEPS := 120

var rng := RandomNumberGenerator.new()
var flocks: Array = []
var spawners: Array = []
var volleys: Array = []
var trigger_data: BulletVolleyData2D
var handler_ops := 0
## Flocks a handler edited inside a sweep this step: the documented rule is
## that such an edit applies from the NEXT pass, so this step's counts may
## still follow the old cap.
var handler_edited: Array = []
var in_handler := false


func before_each() -> void:
	await super()
	flocks.clear()
	spawners.clear()
	volleys.clear()
	handler_ops = 0
	trigger_data = H.make_flight_data([Transform2D(0.0, Vector2(-900, -900))], 0.0, 0.05)
	trigger_data.is_life_time_over_signal_enabled = true
	factory.life_time_over.connect(_on_lifetime)
	make_graze_target(Vector2(250, 200), &"chaos_graze")
	var wall := StaticBody2D.new()
	wall.position = Vector2(620, 200)
	wall.collision_layer = 8
	wall.collision_mask = 2
	var col := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = Vector2(20, 600)
	col.shape = box
	wall.add_child(col)
	add(wall)


func _random_flock() -> BulletFlock2D:
	if not flocks.is_empty() and rng.randf() < 0.6:
		return flocks[rng.randi() % flocks.size()]
	if rng.randf() < 0.25:
		var p := BulletFlock2D.create_preset(rng.randi_range(0, 4))
		flocks.append(p)
		return p
	var f := H.make_flock(rng.randf_range(0, 4), rng.randf_range(0, 3), rng.randf_range(0, 3), rng.randf_range(20, 160), rng.randf_range(0, 80))
	f.max_neighbors = [0, 1, 4, 32][rng.randi() % 4]
	f.max_turn_rate_degrees = [0.0, 90.0, 720.0][rng.randi() % 3]
	f.face_heading = rng.randf() < 0.7
	flocks.append(f)
	return f


func _live_volleys() -> Array:
	volleys = volleys.filter(func(v): return is_instance_valid(v))
	return volleys.filter(func(v): return v.debug_get_life_state() == "active")


func _pick_volley() -> BulletVolley2D:
	var live := _live_volleys()
	return null if live.is_empty() else live[rng.randi() % live.size()]


func _flight_data(count: int) -> BulletVolleyData2D:
	var tr: Array = []
	var at := Vector2(rng.randf_range(0, 500), rng.randf_range(0, 400))
	for k in count:
		tr.append(Transform2D(rng.randf_range(-PI, PI), at + Vector2(rng.randf_range(-30, 30), rng.randf_range(-30, 30))))
	var d := H.make_flight_data(tr, rng.randf_range(-60, 260), [0.3, 2.0, 30.0][rng.randi() % 3])
	d.set_collision_mask_from_array([4])
	if rng.randf() < 0.4:
		d.set_bounce_mask_from_array([4])
	if rng.randf() < 0.4:
		d.homing_smoothing = rng.randf_range(0, 6)
		d.homing_take_control_of_texture_rotation = rng.randf() < 0.7
	if rng.randf() < 0.2:
		d.gravity = Vector2(0, rng.randf_range(0, 300))
	if rng.randf() < 0.2:
		d.shared_bullet_rotation_data = H.make_rotation(rng.randf_range(-4, 4), 10.0)
		d.adjust_direction_based_on_rotation = rng.randf() < 0.5
		d.rotate_only_textures = rng.randf() < 0.5
	return d


func _new_spawner() -> void:
	var sp := make_spawner(_flight_data(rng.randi_range(1, 16)), [BulletSpawner2D.PATTERN_FROM_HELPER_RING, BulletSpawner2D.PATTERN_FROM_HELPER_FAN, BulletSpawner2D.PATTERN_FROM_HELPER_GRID][rng.randi() % 3], rng.randi_range(1, 24))
	sp.position = Vector2(rng.randf_range(0, 500), rng.randf_range(0, 400))
	sp.helper_ring_radius = rng.randf_range(5, 60)
	sp.flock_enabled = rng.randf() < 0.8
	sp.flock = _random_flock() if rng.randf() < 0.8 else null # null: the factory's default flock
	sp.flock_scope = rng.randi() % 3
	if rng.randf() < 0.3:
		sp.bullet_scale_mode = 1 + rng.randi() % 2 # Uniform / Per Axis
		sp.bullet_scale_min = rng.randf_range(0.2, 1.0)
		sp.bullet_scale_max = rng.randf_range(1.0, 3.0)
		sp.bullet_scale_min_xy = Vector2(rng.randf_range(0.2, 1.0), rng.randf_range(0.2, 1.0))
		sp.bullet_scale_max_xy = Vector2(rng.randf_range(1.0, 3.0), rng.randf_range(1.0, 3.0))
	if rng.randf() < 0.2:
		sp.burst_enabled = true
		sp.burst_count = rng.randi_range(2, 4)
		sp.burst_interval_sec = rng.randf_range(0.02, 0.2)
	sp.time_scale = [0.0, 0.5, 1.0, 2.0][rng.randi() % 4]
	sp.orphaned_volleys = rng.randi() % 4
	if rng.randf() < 0.3:
		sp.graze_zones = [H.make_graze_zone([rng.randf_range(10, 80)])]
		sp.graze_node_group = &"chaos_graze"
		sp.graze_enabled = true
	# Burst shots come over later frames: every shot joins the checks.
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: volleys.append(v))
	spawners.append(sp)


func _spawner_shot() -> void:
	spawners = spawners.filter(func(s): return is_instance_valid(s))
	if spawners.is_empty() or rng.randf() < 0.25:
		_new_spawner()
	var sp: BulletSpawner2D = spawners[rng.randi() % spawners.size()]
	var got: Array = []
	var grab := func(v: BulletVolley2D, _n: int) -> void: got.append(v)
	sp.volley_fired.connect(grab)
	sp.shoot_once()
	sp.volley_fired.disconnect(grab)
	for v in got:
		volleys.append(v)
		if rng.randf() < 0.3:
			v.shared_homing_deque_push_back_global_position_target(Vector2(rng.randf_range(0, 600), rng.randf_range(0, 400)))
		if rng.randf() < 0.15:
			for i in v.get_amount_bullets():
				v.bullet_enable_orbiting(i, rng.randf_range(20, 80), BulletVolley2D.OrbitRight, BulletVolley2D.FaceTarget)


func _factory_volley() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_flight_data(rng.randi_range(1, 30)))
	volleys.append(v)
	if rng.randf() < 0.8:
		v.flock_join(_random_flock(), rng.randi() % 3)


func _edit_spawner() -> void:
	spawners = spawners.filter(func(s): return is_instance_valid(s))
	if spawners.is_empty():
		return
	var sp: BulletSpawner2D = spawners[rng.randi() % spawners.size()]
	match rng.randi() % 5:
		0: sp.flock_enabled = not sp.flock_enabled
		1: sp.flock = _random_flock() if rng.randf() < 0.8 else null
		2: sp.flock_scope = rng.randi() % 3
		3:
			if sp.burst_enabled:
				sp.begin_burst() # the shots come over the next frames
		4: sp.bullet_scale_mode = rng.randi() % 3
		3: sp.queue_free() # orphans its volleys per orphaned_volleys
		4: sp.time_scale = [0.0, 1.0, 3.0][rng.randi() % 3]


func _edit_flock() -> void:
	if flocks.is_empty():
		return
	var f: BulletFlock2D = flocks[rng.randi() % flocks.size()]
	if in_handler:
		handler_edited.append(f)
	match rng.randi() % 10:
		9: f.apply_preset(rng.randi_range(0, 4))
		6: f.blend_in_sec = rng.randf_range(0, 1)
		7: f.separation_lookahead_sec = rng.randf_range(0, 1)
		8: f.max_turn_rate_degrees = rng.randf_range(0, 720)
		0: f.separation = rng.randf_range(0, 10)
		1: f.alignment = rng.randf_range(0, 10)
		2: f.cohesion = rng.randf_range(0, 10)
		3: f.neighbor_range = rng.randf_range(1, 300)
		4: f.max_neighbors = rng.randi() % 64
		5: f.separation_range = rng.randf_range(0, 300)


func _touch_volley() -> void:
	var v := _pick_volley()
	if v == null:
		return
	var i := rng.randi() % v.get_amount_bullets()
	match rng.randi() % 7:
		0: v.flock_leave()
		1: v.flock_join(_random_flock() if rng.randf() < 0.8 else null, rng.randi() % 3)
		2:
			if v.is_bullet_status_enabled(i):
				v.disable_bullet(i)
		3:
			v.set_is_auto_pooling_enabled(false)
			if not v.is_bullet_status_enabled(i):
				v.enable_bullet(i)
		4: v.teleport_shift_all_bullets(Vector2(rng.randf_range(-200, 200), rng.randf_range(-200, 200)))
		5:
			if v.is_bullet_status_enabled(i):
				v.bullet_set_velocity(i, Vector2(rng.randf_range(-200, 200), rng.randf_range(-200, 200)))
		6: # their expiry runs _on_lifetime inside a sweep
			volleys.append(factory.spawn_volley(trigger_data))
			volleys.append(factory.spawn_volley(trigger_data))


func _on_lifetime(v: BulletVolley2D, _indexes: Array) -> void:
	if v.get_amount_bullets() != 1 or v.get_bullet_transform(0).origin.x > -800:
		return
	# Inside the sweep: edit flocks and volleys the sweep has not ticked yet.
	handler_ops += 1
	var other := _pick_volley()
	if other == null:
		return
	match rng.randi() % 4:
		0: other.flock_join(_random_flock(), rng.randi() % 3)
		1: other.flock_leave()
		2:
			in_handler = true
			_edit_flock()
			in_handler = false
		3:
			if other != v:
				other.queue_free()


func _step(op: int) -> void:
	match op:
		0, 1, 2: _spawner_shot()
		3, 4: _factory_volley()
		5: _edit_spawner()
		6: _edit_flock()
		7, 8: _touch_volley()
		9: factory.clear_bullets_in_circle(Vector2(rng.randf_range(0, 500), rng.randf_range(0, 400)), rng.randf_range(20, 200))
		10:
			var out: Array = factory.convert_bullets_in_circle_to_pickups(Vector2(rng.randf_range(0, 500), rng.randf_range(0, 400)), rng.randf_range(20, 200), P.coin(), Vector2(0, 0))
			volleys.append_array(out)
		11: factory.time_scale = [0.0, 0.5, 1.0, 2.0][rng.randi() % 4]
		12:
			factory.despawn_bounds_rect = Rect2(-200, -200, rng.randf_range(400, 1200), rng.randf_range(400, 1000))
			factory.despawn_bounds_enabled = rng.randf() < 0.5
		13: factory.clear_active_bullets()


func _broken() -> Array:
	var bad: Array = []
	for v in _live_volleys():
		var f: BulletFlock2D = v.flock_get()
		for i in v.get_amount_bullets():
			if not v.is_bullet_status_enabled(i):
				continue
			var d: Vector2 = v.get_bullet_direction(i)
			if not v.get_bullet_transform(i).is_finite() or not d.is_finite():
				bad.append("non-finite bullet %d" % i)
			elif f != null and d.length_squared() > 1e-8 and absf(d.length() - 1.0) > 1e-3:
				bad.append("flocking heading |%s| = %f" % [d, d.length()])
			if f != null and f.max_neighbors > 0 and not handler_edited.has(f) and v.debug_get_bullet_flock_neighbors(i) > f.max_neighbors:
				bad.append("bullet %d read %d > cap %d" % [i, v.debug_get_bullet_flock_neighbors(i), f.max_neighbors])
	var dangling: Dictionary = factory.debug_assert_no_dangling()
	if not dangling["ok"]:
		bad.append(dangling["error"])
	return bad


## SEEDS, plus BLAST_FUZZ_SEEDS more for a soak run:
## BLAST_FUZZ_SEEDS=200 python3 tools/run_tests.py --suite flock_chaos
func _seeds() -> Array:
	var out: Array = SEEDS.duplicate()
	for k in int(OS.get_environment("BLAST_FUZZ_SEEDS")):
		out.append(1000 + k)
	return out


func test_random_flocking_sequences_keep_every_invariant() -> void:
	var weak_flocks: Array = []
	var peak_armed := 0
	var peak_grid := 0
	var checks := 0
	for sd in _seeds():
		rng.seed = sd
		seed(sd)
		for step in STEPS:
			var op := rng.randi() % 14
			handler_edited.clear()
			_step(op)
			for k in rng.randi_range(1, 3):
				await physics()
			await idle(1)
			var where := "seed %d step %d op %d" % [sd, step, op]
			var stats: Dictionary = factory.debug_get_flock_stats()
			peak_armed = maxi(peak_armed, stats["armed_volleys"])
			peak_grid = maxi(peak_grid, stats["grid_members"])
			checks += int(stats["neighbor_checks"])
			var bad := _broken()
			if not bad.is_empty():
				assert_eq(bad.slice(0, 4), [], where)
				return
			expect_no_errors("no error, " + where)
		# Wind down: nothing may linger after a full reset.
		factory.time_scale = 1.0
		factory.despawn_bounds_enabled = false
		for sp in spawners:
			if is_instance_valid(sp):
				sp.free()
		spawners.clear()
		factory.reset()
		await idle(3)
		assert_eq(factory.get_active_bullet_count(), 0, "reset leaves nothing, seed %d" % sd)
		assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "nothing armed after the reset, seed %d" % sd)
		assert_true(factory.debug_assert_no_dangling()["ok"], "clean after reset, seed %d" % sd)
		weak_flocks.append_array(flocks.map(func(f: BulletFlock2D) -> WeakRef: return weakref(f)))
		flocks.clear()
		volleys.clear()
	await idle(2)
	var alive := weak_flocks.filter(func(w): return w.get_ref() != null)
	assert_eq(alive.size(), 0, "no flock resource outlives its users (%d of %d alive)" % [alive.size(), weak_flocks.size()])
	assert_gt(handler_ops, 3, "handlers edited flocks inside sweeps (%d times)" % handler_ops)
	assert_gt(peak_armed, 5, "many volleys flocked at once (peak %d)" % peak_armed)
	assert_gt(peak_grid, 50, "big flocks formed (peak %d bullets)" % peak_grid)
	assert_gt(checks, 10000, "the pass did real work (%d checks sampled)" % checks)
