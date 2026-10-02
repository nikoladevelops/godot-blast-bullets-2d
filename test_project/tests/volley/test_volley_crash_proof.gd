extends BlastTest
## Every public setter hammered with hostile input (NaN/Inf/null/OOB/empty/
## degenerate) plus adversarial ticks (freed targets mid-flight, teleport and
## disable storms, pool churn, maxed queues). Contract: never crash, never
## NaN-poison a volley, siblings intact. Hostile calls must fail loud.


func _finite_volley(v: DirectionalBullets2D) -> bool:
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
	var d := H.make_directional_data(3, 100.0)
	d.all_bullet_speed_data = [null, H.make_speed(150.0), null]
	d.all_bullet_rotation_data = [null, null, null]
	d.all_bullet_curves_data = [null, null, null]
	d.all_bullet_wobble_data = [null, null, null]
	d.all_bullet_gravity = [Vector2(NAN, NAN), Vector2(INF, 0), Vector2.ZERO]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	swallow_errors()
	assert_not_null(v, "hostile spawn returns a volley")
	assert_true(_finite_volley(v), "hostile spawn stays finite")
	assert_almost_eq(v.get_bullet_speed_data(1).speed, 150.0, 0.01, "valid slot survives null siblings")
	assert_almost_eq(v.get_bullet_speed_data(0).speed, 0.0, 0.01, "null slot 0 reads default 0")
	assert_almost_eq(v.get_bullet_speed_data(2).speed, 0.0, 0.01, "null slot 2 reads default 0")


func test_hostile_live_setters() -> void:
	var v: DirectionalBullets2D = spawn_dir(3, 200.0)
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
	var v: DirectionalBullets2D = spawn_dir(2, 200.0)
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
	v.set_bullet_direction_towards_position(0, v.get_bullet_global_transform(0).origin)
	swallow_errors()
	assert_true(v.get_bullet_direction(0).is_normalized(), "coincident aim keeps a normalized heading")


func test_freed_targets_mid_flight() -> void:
	var v: DirectionalBullets2D = spawn_dir(3, 250.0)
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
	var v: DirectionalBullets2D = spawn_dir(4, 220.0)
	v.set_homing_smoothing(5.0)
	v.bullet_homing_push_back_global_position_target(0, Vector2(500, 0))
	v.bullet_enable_orbiting(0, 55.0)
	for k in 12:
		v.disable_bullet(k % 4)
		v.teleport_shift_bullet((k + 1) % 4, Vector2(3, -2))
		await physics()
		v.wake_bullet(k % 4)
	swallow_errors()
	assert_true(_finite_volley(v), "volley finite after disable/teleport storm")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after storm")


func test_pool_churn() -> void:
	for k in 8:
		var n: int = 2 + (k % 3)
		var d := H.make_directional_data(n, 150.0 + 25.0 * k)
		if k % 2 == 0:
			var wobs: Array = []
			for i in n:
				var wb := BulletWobbleData2D.new()
				wb.enabled = true
				wobs.append(wb)
			d.all_bullet_wobble_data = wobs
		var vv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
		await physics(5)
		assert_true(_finite_volley(vv), "churn volley %d finite" % k)
		for i in vv.get_amount_bullets():
			vv.disable_bullet(i)
		await physics()
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after churn")


func test_maxed_deque_timers_collisions() -> void:
	var v: DirectionalBullets2D = spawn_dir(2, 180.0)
	for i in 300:
		v.bullet_homing_push_back_global_position_target(0, Vector2(i, i))
	assert_eq(v.bullet_homing_check_targets_amount(0), 256, "deque capped at 256")
	await idle()
	for i in 70:
		v.multimesh_attach_time_based_function(30.0, func() -> void: pass)
	assert_eq(v.debug_get_timer_count(), 64, "timer cap holds under flood")
	v.set_bullet_max_collision_count(2)
	v.set_bullet_collision_count(0, 999)
	assert_eq(v.get_bullet_collision_count(0), 1, "collision clamps to max-1")
	v.set_bullet_collision_count(0, -99)
	assert_eq(v.get_bullet_collision_count(0), 0, "collision clamps negatives")
	swallow_errors()
	await idle()
	v.multimesh_detach_all_time_based_functions()
	assert_true(_finite_volley(v), "volley finite after floods")


func test_zero_single_and_mega_volley() -> void:
	var d0 := H.make_directional_data(1, 100.0)
	d0.transforms = []
	var v0: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d0)
	swallow_errors()
	assert_true(v0 == null or v0.get_amount_bullets() <= 1, "empty transforms rejected or empty")
	var v1: DirectionalBullets2D = spawn_dir(1, 300.0)
	await physics(10)
	assert_true(_finite_volley(v1), "single bullet finite")
	var vm: DirectionalBullets2D = spawn_dir(64, 200.0)
	vm.all_bullets_set_wobble_data(_wob(20.0))
	vm.all_bullets_set_gravity(Vector2(0, 300))
	vm.all_bullets_push_back_homing_target(Vector2(900, 0))
	await physics(20)
	assert_true(_finite_volley(vm), "64-bullet full stack finite")
