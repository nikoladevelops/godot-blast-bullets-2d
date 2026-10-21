extends BlastTest
## Every public setter hammered with hostile input (NaN/Inf/null/OOB/empty/
## degenerate) plus adversarial ticks (freed targets mid-flight, teleport and
## disable storms, pool churn, maxed queues). Contract: never crash, never
## NaN-poison a volley, siblings intact. Hostile calls must fail loud.


func _finite_volley(v: BulletVolley2D) -> bool:
	for i in v.get_amount_bullets():
		if not v.get_bullet_transform(i).is_finite() or not v.get_bullet_direction(i).is_finite() \
				or not v.get_bullet_velocity(i).is_finite():
			return false
	return true


func _wob(amp: float) -> BulletWobbleData2D:
	var w := BulletWobbleData2D.new()
	w.enabled = true
	w.amplitude = amp
	w.frequency_hz = 2.0
	return w


func test_hostile_spawn_data() -> void:
	var d := H.make_volley_data(3, 100.0)
	d.all_bullet_speed_data = [null, H.make_speed(150.0), null]
	d.all_bullet_rotation_data = [null, null, null]
	d.all_bullet_curves_data = [null, null, null]
	d.all_bullet_wobble_data = [null, null, null]
	d.all_bullet_gravity = [Vector2(NAN, NAN), Vector2(INF, 0), Vector2.ZERO]
	var v: BulletVolley2D = factory.spawn_volley(d)
	swallow_errors()
	assert_not_null(v, "hostile spawn returns a volley")
	assert_true(_finite_volley(v), "hostile spawn stays finite")
	assert_almost_eq(v.get_bullet_speed_data(1).speed, 150.0, 0.01, "valid slot survives null siblings")
	assert_almost_eq(v.get_bullet_speed_data(0).speed, 0.0, 0.01, "null slot 0 reads default 0")
	assert_almost_eq(v.get_bullet_speed_data(2).speed, 0.0, 0.01, "null slot 2 reads default 0")


func test_hostile_live_setters() -> void:
	var v: BulletVolley2D = quick_volley(3, 200.0)
	var keep_dir: Vector2 = v.get_bullet_direction(0)
	var keep_spd: float = v.get_bullet_speed_data(0).speed
	v.set_bullet_direction(0, Vector2(NAN, NAN))
	v.set_bullet_direction(0, Vector2(INF, INF))
	v.set_bullet_direction(0, Vector2.ZERO)
	v.set_bullet_speed_data(0, null)
	expect_any_error("hostile direction/speed writes fail loud", 4)
	assert_almost_eq(v.get_bullet_speed_data(0).speed, keep_spd, 0.01, "null speed rejected")
	v.set_bullet_speed_data(0, H.make_speed(321.0))
	assert_almost_eq(v.get_bullet_speed_data(0).speed, 321.0, 0.01, "valid speed write lands after reject")
	v.set_bullet_speed_data(0, H.make_speed(keep_spd))
	v.bullet_set_gravity(0, Vector2(NAN, 1))
	v.bullet_set_gravity(0, Vector2(INF, INF))
	v.set_gravity(Vector2(NAN, 0))
	v.bullet_set_homing_smoothing(0, NAN)
	v.bullet_set_homing_smoothing(0, -5.0)
	v.set_homing_smoothing(NAN)
	v.set_linear_drag(NAN)
	v.set_linear_drag(-1.0)
	v.bullet_set_velocity(0, Vector2(NAN, NAN))
	v.bullet_set_wobble_data(0, null)
	var dead_wob := BulletWobbleData2D.new()
	dead_wob.enabled = false
	v.bullet_set_wobble_data(0, dead_wob)
	v.set_shared_bullet_wobble_data(dead_wob)
	expect_any_error("hostile setters fail loud", 8)
	assert_eq(v.get_bullet_direction(0), keep_dir, "direction rejects hostile input")
	assert_almost_eq(v.get_bullet_speed_data(0).speed, keep_spd, 0.01, "speed rejects hostile input")
	assert_true(_finite_volley(v), "volley finite after hostile setters")
	await physics(20)
	assert_true(_finite_volley(v), "volley finite after ticks with hostile history")


func test_degenerate_transforms_rejected() -> void:
	var v: BulletVolley2D = quick_volley(2, 200.0)
	var good: Vector2 = v.get_bullet_transform(0).origin
	v.set_bullet_transform(0, Transform2D(Vector2.ZERO, Vector2.ZERO, Vector2.ZERO))
	expect_any_error("zero-scale transform fails loud")
	assert_eq(v.get_bullet_transform(0).origin, good, "zero-scale transform rejected")
	v.set_bullet_transform(0, Transform2D(0.0, Vector2(NAN, NAN)))
	expect_any_error("NaN transform fails loud")
	assert_eq(v.get_bullet_transform(0).origin, good, "NaN transform rejected")
	v.teleport_bullet(0, Vector2(NAN, 0))
	v.teleport_bullet(0, Vector2(INF, INF))
	expect_any_error("NaN teleport fails loud", 2)
	assert_eq(v.get_bullet_transform(0).origin, good, "NaN teleport rejected")
	v.teleport_shift_bullet(0, Vector2(NAN, NAN))
	expect_any_error("NaN shift fails loud")
	assert_eq(v.get_bullet_transform(0).origin, good, "NaN shift rejected")
	v.set_bullet_direction_towards_position(0, v.get_bullet_transform(0).origin)
	swallow_errors()
	assert_true(v.get_bullet_direction(0).is_normalized(), "coincident aim keeps a normalized heading")


func test_freed_targets_mid_flight() -> void:
	var v: BulletVolley2D = quick_volley(3, 250.0)
	v.set_homing_smoothing(6.0)
	v.set_homing_take_control_of_texture_rotation(true)
	for i in 3:
		var t: Node2D = add(Node2D.new())
		t.position = Vector2(300 + 100 * i, -100 * i)
		v.bullet_homing_push_back_node2d_target(i, t)
	v.shared_homing_deque_push_back_global_position_target(Vector2(800, 0))
	v.bullet_enable_orbiting(0, 50.0)
	await physics(10)
	for c in get_children():
		if c is Node2D and not (c is BulletFactory2D):
			c.queue_free()
	await idle(1)
	await physics(30)
	assert_true(_finite_volley(v), "volley finite after all targets freed")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after freed targets")


func test_disable_teleport_storm() -> void:
	var v: BulletVolley2D = quick_volley(4, 220.0)
	v.set_homing_smoothing(5.0)
	v.bullet_homing_push_back_global_position_target(0, Vector2(500, 0))
	v.bullet_enable_orbiting(0, 55.0)
	for k in 12:
		v.disable_bullet(k % 4)
		v.teleport_shift_bullet((k + 1) % 4, Vector2(3, -2))
		await physics()
		v.enable_bullet(k % 4)
	swallow_errors()
	assert_true(_finite_volley(v), "volley finite after disable/teleport storm")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after storm")


func test_pool_churn() -> void:
	for k in 8:
		var n: int = 2 + (k % 3)
		var d := H.make_volley_data(n, 150.0 + 25.0 * k)
		if k % 2 == 0:
			var wobs: Array = []
			for i in n:
				var wb := BulletWobbleData2D.new()
				wb.enabled = true
				wobs.append(wb)
			d.all_bullet_wobble_data = wobs
		var vv: BulletVolley2D = factory.spawn_volley(d)
		await physics(5)
		assert_true(_finite_volley(vv), "churn volley %d finite" % k)
		for i in vv.get_amount_bullets():
			vv.disable_bullet(i)
		await physics()
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after churn")


func test_maxed_deque_timers_collisions() -> void:
	var v: BulletVolley2D = quick_volley(2, 180.0)
	for i in 300:
		v.bullet_homing_push_back_global_position_target(0, Vector2(i, i))
	assert_eq(v.bullet_homing_check_targets_amount(0), 300, "the queue holds every target (no cap)")
	await idle()
	for i in 70:
		v.attach_time_based_function(30.0, func() -> void: pass)
	assert_eq(v.debug_get_timer_count(), 64, "timer cap holds under flood")
	v.set_bullet_max_collision_count(2)
	v.set_bullet_collision_count(0, 999)
	assert_eq(v.get_bullet_collision_count(0), 1, "collision clamps to max-1")
	v.set_bullet_collision_count(0, -99)
	assert_eq(v.get_bullet_collision_count(0), 0, "collision clamps negatives")
	swallow_errors()
	await idle()
	v.detach_all_time_based_functions()
	assert_true(_finite_volley(v), "volley finite after floods")


func test_zero_single_and_mega_volley() -> void:
	var d0 := H.make_volley_data(1, 100.0)
	d0.transforms = []
	var v0: BulletVolley2D = factory.spawn_volley(d0)
	swallow_errors()
	assert_true(v0 == null or v0.get_amount_bullets() <= 1, "empty transforms rejected or empty")
	var v1: BulletVolley2D = quick_volley(1, 300.0)
	await physics(10)
	assert_true(_finite_volley(v1), "single bullet finite")
	var vm: BulletVolley2D = quick_volley(64, 200.0)
	vm.all_bullets_set_wobble_data(_wob(20.0))
	vm.all_bullets_set_gravity(Vector2(0, 300))
	vm.all_bullets_push_back_homing_target(Vector2(900, 0))
	await physics(20)
	assert_true(_finite_volley(vm), "64-bullet full stack finite")


## Neutral value per argument type for the method sweeps below; ints use the
## given probe so the same sweep can hit index 0 and an out-of-range index.
func _sweep_arg(type: int, int_probe: int) -> Variant:
	match type:
		TYPE_INT: return int_probe
		TYPE_FLOAT: return 0.0
		TYPE_BOOL: return false
		TYPE_VECTOR2: return Vector2.ZERO
		TYPE_TRANSFORM2D: return Transform2D()
		TYPE_COLOR: return Color()
		TYPE_STRING: return ""
		TYPE_STRING_NAME: return &""
		TYPE_NODE_PATH: return NodePath()
		TYPE_ARRAY: return []
		TYPE_DICTIONARY: return {}
		TYPE_CALLABLE: return Callable()
		TYPE_PACKED_INT32_ARRAY: return PackedInt32Array()
		TYPE_PACKED_INT64_ARRAY: return PackedInt64Array()
		TYPE_PACKED_FLOAT32_ARRAY: return PackedFloat32Array()
		TYPE_PACKED_VECTOR2_ARRAY: return PackedVector2Array()
	return null


## Calls every bound BulletVolley2D method (internal _-prefixed ones excluded)
## with neutral arguments. Returns how many methods were called.
func _sweep_all_methods(v: BulletVolley2D, int_probe: int) -> int:
	var called := 0
	for m in ClassDB.class_get_method_list("BulletVolley2D", true):
		var name := str(m["name"])
		if name.begins_with("_"):
			continue
		var args: Array = []
		for a in m["args"]:
			args.append(_sweep_arg(int(a["type"]), int_probe))
		v.callv(name, args)
		called += 1
		if not is_instance_valid(v):
			break
	return called


func test_bare_volley_survives_every_method() -> void:
	# A volley created with new() was never spawned: no MultiMesh, no area,
	# zero bullets. Every bound method must fail loud or no-op, never crash.
	for probe in [0, -1, 99]:
		var bare := BulletVolley2D.new()
		add(bare)
		await idle(1)
		var called := _sweep_all_methods(bare, probe)
		assert_gt(called, 200, "every bound method was exercised (probe %d)" % probe)
		assert_true(is_instance_valid(bare), "bare volley survives the sweep (probe %d)" % probe)
		assert_eq(bare.get_amount_bullets(), 0, "still zero bullets (probe %d)" % probe)
		swallow_errors()
		bare.queue_free()
		await idle(1)


func test_live_volley_survives_every_method_out_of_range() -> void:
	# Same sweep on a live volley with out-of-range indexes: nothing may write
	# outside the volley or poison the in-range bullets.
	var v: BulletVolley2D = quick_volley(3, 100.0, 30.0)
	await idle(1)
	var called := _sweep_all_methods(v, 99)
	assert_gt(called, 200, "every bound method was exercised")
	assert_true(is_instance_valid(v), "live volley survives the sweep")
	swallow_errors()
	await physics(5)
	assert_true(_finite_volley(v), "no NaN after the out-of-range sweep")
	swallow_errors()


# ---- Random API storm -----------------------------------------------------------
# Seeded random SEQUENCES of every bound BulletVolley2D method with typed
# arguments (mostly valid, some hostile) on several live volleys (some
# flocking), interleaved with ticks of random length and real frames: the
# state interactions a one-call-per-method sweep never reaches. Contract:
# never crash, never a non-finite bullet, the factory books always balance.

const STORM_SEEDS := [5, 29, 61, 97]
const STORM_STEPS := 250
const STORM_SKIP := ["enable_volley_for_script"] # needs a disabled volley; covered by the pooling suites

var storm_rng := RandomNumberGenerator.new()
var storm_calls := 0
var storm_target: Node2D
var storm_scene: PackedScene


func _storm_object(class_name_hint: String) -> Variant:
	if storm_rng.randf() < 0.15:
		return null
	match class_name_hint:
		"Node2D", "Node":
			return storm_target
		"BulletSpeedData2D":
			return H.make_speed(storm_rng.randf_range(-50, 400), storm_rng.randf_range(0, 800), storm_rng.randf_range(-200, 200))
		"BulletRotationData2D":
			return H.make_rotation(storm_rng.randf_range(-5, 5), storm_rng.randf_range(0, 10), storm_rng.randf_range(-2, 2))
		"BulletWobbleData2D":
			return _wob(storm_rng.randf_range(0, 40))
		"BulletCurvesData2D":
			var c := BulletCurvesData2D.new()
			c.movement_speed_curve = H.make_flat_curve(storm_rng.randf_range(0, 300))
			if storm_rng.randf() < 0.5:
				c.x_direction_curve = H.make_flat_curve(storm_rng.randf_range(-1, 1))
				c.x_direction_curve_mode = storm_rng.randi() % 2
			return c
		"BulletFlock2D":
			return H.make_flock(storm_rng.randf_range(0, 3), storm_rng.randf_range(0, 3), storm_rng.randf_range(0, 3))
		"PackedScene":
			return storm_scene
		"Curve2D":
			var c2 := Curve2D.new()
			c2.add_point(Vector2.ZERO)
			c2.add_point(Vector2(storm_rng.randf_range(20, 200), storm_rng.randf_range(-80, 80)))
			return c2
		"Resource":
			return Resource.new()
	return null


func _storm_arg(a: Dictionary, v: BulletVolley2D) -> Variant:
	var n := v.get_amount_bullets()
	match int(a["type"]):
		TYPE_INT:
			return [0, 1, n - 1, -1, n, 99, storm_rng.randi_range(0, maxi(n - 1, 0)), storm_rng.randi_range(0, maxi(n - 1, 0))][storm_rng.randi() % 8]
		TYPE_FLOAT:
			return [0.0, 0.5, 1.0, 2.0, 30.0, 300.0, -1.0, storm_rng.randf_range(-10, 10), NAN, INF][storm_rng.randi() % 10]
		TYPE_BOOL:
			return storm_rng.randf() < 0.5
		TYPE_VECTOR2:
			return Vector2(NAN, 0) if storm_rng.randf() < 0.05 else Vector2(storm_rng.randf_range(-300, 300), storm_rng.randf_range(-300, 300))
		TYPE_TRANSFORM2D:
			return Transform2D(storm_rng.randf_range(-PI, PI), Vector2(storm_rng.randf_range(-300, 300), storm_rng.randf_range(-300, 300)))
		TYPE_COLOR:
			return Color(storm_rng.randf(), storm_rng.randf(), storm_rng.randf(), storm_rng.randf())
		TYPE_STRING, TYPE_STRING_NAME:
			return [&"", &"graze_targets", &"storm"][storm_rng.randi() % 3]
		TYPE_NODE_PATH:
			return NodePath()
		TYPE_ARRAY:
			return []
		TYPE_DICTIONARY:
			return {}
		TYPE_CALLABLE:
			return (func(_a = null, _b = null, _c = null) -> void: storm_calls += 1) if storm_rng.randf() < 0.8 else Callable()
		TYPE_PACKED_INT32_ARRAY:
			return PackedInt32Array([0, storm_rng.randi_range(0, maxi(n - 1, 0))])
		TYPE_PACKED_INT64_ARRAY:
			return PackedInt64Array()
		TYPE_PACKED_FLOAT32_ARRAY:
			return PackedFloat32Array([storm_rng.randf_range(0, 2)])
		TYPE_PACKED_VECTOR2_ARRAY:
			return PackedVector2Array([Vector2(storm_rng.randf_range(-200, 200), 0)])
		TYPE_OBJECT:
			return _storm_object(str(a["class_name"]))
	return null


func _storm_volley(index: int) -> BulletVolley2D:
	var n := storm_rng.randi_range(1, 24)
	var d := H.make_volley_data(n, storm_rng.randf_range(0, 250), 60.0)
	var tr: Array = []
	for i in n:
		tr.append(Transform2D(storm_rng.randf_range(-PI, PI), Vector2(storm_rng.randf_range(-100, 300), storm_rng.randf_range(-100, 300))))
	d.transforms = tr
	var v: BulletVolley2D = factory.spawn_volley(d)
	if index % 2 == 0:
		v.flock_join(H.make_flock(1.5, 1.0, 1.0), storm_rng.randi() % 3)
	return v


func test_random_api_storm_keeps_every_invariant() -> void:
	storm_target = make_graze_target(Vector2(120, 80))
	storm_scene = make_probe_scene()
	var methods: Array = []
	for m in ClassDB.class_get_method_list("BulletVolley2D", true):
		var name := str(m["name"])
		if not name.begins_with("_") and not (name in STORM_SKIP):
			methods.append(m)
	var total_calls := 0
	for sd in STORM_SEEDS:
		storm_rng.seed = sd
		seed(sd)
		var volleys: Array = []
		for k in 4:
			volleys.append(_storm_volley(k))
		for step in STORM_STEPS:
			for k in volleys.size():
				var live: BulletVolley2D = volleys[k]
				if not is_instance_valid(live) or live.debug_get_life_state() == "pooled":
					volleys[k] = _storm_volley(k)
			for c in 3:
				var v: BulletVolley2D = volleys[storm_rng.randi() % volleys.size()]
				var m: Dictionary = methods[storm_rng.randi() % methods.size()]
				var args: Array = []
				for a in m["args"]:
					args.append(_storm_arg(a, v))
				v.callv(str(m["name"]), args)
				total_calls += 1
			if storm_rng.randf() < 0.15:
				await physics(1)
				await idle(1)
			else:
				factory.debug_advance_time([0.0, 1.0 / 60.0, 1.0 / 30.0, 0.25][storm_rng.randi() % 4])
			swallow_errors()
			var where := "seed %d step %d" % [sd, step]
			for v in volleys:
				if is_instance_valid(v) and v.debug_get_life_state() == "active":
					assert_true(_finite_volley(v), "finite bullets, " + where)
			var books: Dictionary = factory.debug_assert_no_dangling()
			if not books["ok"]:
				assert_true(false, "books balance, %s: %s" % [where, books["error"]])
				return
		factory.reset()
		await idle(2)
		swallow_errors()
	assert_gt(total_calls, 2500, "the storm made %d calls" % total_calls)


func test_random_spawner_and_factory_storm_keeps_every_invariant() -> void:
	# The same storm over every bound BulletSpawner2D and BulletFactory2D
	# method (setters, shots, conversions, clears, structural calls on idle
	# points), with flocking spawners firing throughout.
	storm_target = make_graze_target(Vector2(120, 80))
	storm_scene = make_probe_scene()
	var fired: Array = []
	var calls := 0
	for sd in STORM_SEEDS:
		storm_rng.seed = sd
		seed(sd)
		var spawners: Array = []
		for k in 2:
			var sp := make_spawner(H.make_volley_data(storm_rng.randi_range(1, 16), 120.0, 3.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, storm_rng.randi_range(1, 16))
			sp.helper_ring_radius = 30.0
			sp.flock_enabled = true
			sp.flock = H.make_flock(1.5, 1.0, 1.0)
			sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: fired.append(v))
			spawners.append(sp)
		var targets: Array = [] # [object, method dict]
		for m in ClassDB.class_get_method_list("BulletSpawner2D", true):
			targets.append(["spawner", m])
		for m in ClassDB.class_get_method_list("BulletFactory2D", true):
			targets.append(["factory", m])
		targets = targets.filter(func(t): return not str(t[1]["name"]).begins_with("_") and not (t[1]["args"] as Array).any(func(a): return int(a["type"]) == TYPE_RID))
		for step in 150:
			for c in 3:
				var pick: Array = targets[storm_rng.randi() % targets.size()]
				var obj: Object = factory
				if pick[0] == "spawner":
					spawners = spawners.filter(func(s): return is_instance_valid(s))
					if spawners.is_empty():
						continue
					obj = spawners[storm_rng.randi() % spawners.size()]
				var args: Array = []
				for a in pick[1]["args"]:
					var arg: Variant = null
					if int(a["type"]) == TYPE_OBJECT and str(a["class_name"]) == "BulletVolley2D":
						fired = fired.filter(func(v): return is_instance_valid(v))
						arg = null if fired.is_empty() else fired[storm_rng.randi() % fired.size()]
					elif int(a["type"]) == TYPE_OBJECT and str(a["class_name"]) == "BulletPickupData2D":
						arg = load("res://tests/common/pickup_test_helpers.gd").coin()
					else:
						var probe_volley: BulletVolley2D = null
						fired = fired.filter(func(v): return is_instance_valid(v))
						if not fired.is_empty():
							probe_volley = fired[0]
						arg = _storm_arg(a, probe_volley) if probe_volley != null else 0
					args.append(arg)
				obj.callv(str(pick[1]["name"]), args)
				calls += 1
			if not factory.is_inside_tree():
				break
			if storm_rng.randf() < 0.2:
				await physics(1)
				await idle(1)
			else:
				factory.debug_advance_time([0.0, 1.0 / 60.0, 1.0 / 30.0][storm_rng.randi() % 3])
			for sp in spawners:
				if is_instance_valid(sp) and storm_rng.randf() < 0.3:
					sp.shoot_once()
			swallow_errors()
			var where := "seed %d step %d" % [sd, step]
			fired = fired.filter(func(v): return is_instance_valid(v))
			for v in fired:
				if v.debug_get_life_state() == "active":
					assert_true(_finite_volley(v), "finite bullets, " + where)
			var books: Dictionary = factory.debug_assert_no_dangling()
			if not books["ok"]:
				assert_true(false, "books balance, %s: %s" % [where, books["error"]])
				return
		factory.set_is_factory_processing_bullets(true)
		factory.time_scale = 1.0
		factory.reset()
		await idle(2)
		swallow_errors()
		for sp in spawners:
			if is_instance_valid(sp):
				sp.queue_free()
		await idle(1)
		fired.clear()
	assert_gt(calls, 1500, "the storm made %d calls" % calls)
