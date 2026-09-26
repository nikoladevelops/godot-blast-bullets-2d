extends SceneTree
## Crash-proofing suite: every public setter/getter hammered with hostile
## input (NaN/Inf/null/wrong-type/OOB/empty/inverted/degenerate) plus
## adversarial tick scenarios (freed targets mid-flight, teleport storms,
## disable storms, pool churn, maxed-out queues). The plugin must never
## crash, NaN-poison a volley, leak state, or leave dangling references:
## every hostile call fails loud (error) or silent-safe with siblings intact.
## Run: godot --headless --path test_project --script tests/volley/test_volley_crash_proof.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _finite_volley(v: DirectionalBullets2D) -> bool:
	for i in v.get_amount_bullets():
		if not v.get_bullet_transform(i).is_finite():
			return false
		if not v.get_bullet_direction(i).is_finite():
			return false
		if not v.get_bullet_velocity(i).is_finite():
			return false
	return true

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("CRASH T1 hostile spawn data never crashes")
	var bad_speeds: Array = [null, BulletSpeedData2D.new(), null]
	(bad_speeds[1] as BulletSpeedData2D).speed = 150.0
	(bad_speeds[1] as BulletSpeedData2D).max_speed = 3000.0
	var d1 := H.make_directional_data(3, 100.0)
	d1.all_bullet_speed_data = bad_speeds
	d1.all_bullet_rotation_data = [null, null, null]
	d1.all_bullet_curves_data = [null, null, null]
	d1.all_bullet_wobble_data = [null, null, null]
	d1.all_bullet_gravity = [Vector2(NAN, NAN), Vector2(INF, 0), Vector2(0, 0)]
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d1)
	_check(v1 != null, "hostile spawn returns a volley")
	_check(_finite_volley(v1), "hostile spawn stays finite")
	_check(absf(v1.get_bullet_speed_data(1).speed - 150.0) < 0.01, "valid slot survives null siblings")
	_check(absf(v1.get_bullet_speed_data(0).speed) < 0.01, "null slot 0 reads default 0")
	_check(absf(v1.get_bullet_speed_data(2).speed) < 0.01, "null slot 2 reads default 0")

	printerr("CRASH T2 hostile live setters never poison siblings")
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(3, 200.0))
	var keep_dir: Vector2 = v2.get_bullet_direction(0)
	var keep_spd: float = v2.get_bullet_speed_data(0).speed
	v2.set_bullet_direction(0, Vector2(NAN, NAN))
	v2.set_bullet_direction(0, Vector2(INF, INF))
	v2.set_bullet_direction(0, Vector2.ZERO)
	# null is rejected (stays at spawn speed). The resource's own setter
	# already rejects NaN (verified live: set("speed", NAN) errors and keeps
	# the old value), so set_bullet_speed_data's finite-check is
	# defense-in-depth for direct member writes; assert the rejection path
	# with a null plus a valid write-after-reject instead.
	v2.set_bullet_speed_data(0, null)
	_check(absf(v2.get_bullet_speed_data(0).speed - keep_spd) < 0.01, "null speed rejected")
	var ok_sp := BulletSpeedData2D.new()
	ok_sp.speed = 321.0
	ok_sp.max_speed = 3000.0
	v2.set_bullet_speed_data(0, ok_sp)
	_check(absf(v2.get_bullet_speed_data(0).speed - 321.0) < 0.01, "valid speed write lands after reject")
	v2.set_bullet_speed_data(0, null)
	var restore := BulletSpeedData2D.new()
	restore.speed = keep_spd
	restore.max_speed = 3000.0
	v2.set_bullet_speed_data(0, restore)
	v2.bullet_set_gravity(0, Vector2(NAN, 1))
	v2.bullet_set_gravity(0, Vector2(INF, INF))
	v2.set_gravity(Vector2(NAN, 0))
	v2.bullet_set_homing_smoothing(0, NAN)
	v2.bullet_set_homing_smoothing(0, -5.0)
	v2.set_homing_smoothing(NAN)
	v2.set_linear_drag(NAN)
	v2.set_linear_drag(-1.0)
	v2.bullet_set_velocity(0, Vector2(NAN, NAN))
	v2.bullet_set_wobble_data(0, null)
	var dead_wob := BulletWobbleData2D.new()
	dead_wob.enabled = false
	v2.bullet_set_wobble_data(0, dead_wob)
	v2.set_shared_bullet_wobble_data(dead_wob)
	_check(v2.get_bullet_direction(0) == keep_dir, "direction rejects hostile input")
	_check(absf(v2.get_bullet_speed_data(0).speed - keep_spd) < 0.01, "speed rejects hostile input")
	_check(_finite_volley(v2), "volley finite after hostile setters")
	for i in 20:
		await physics_frame
	_check(_finite_volley(v2), "volley finite after ticks with hostile history")

	printerr("CRASH T3 degenerate transforms rejected, never normalized to stall")
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	var good_t: Transform2D = v3.get_bullet_transform(0)
	v3.set_bullet_transform(0, Transform2D(Vector2.ZERO, Vector2.ZERO, Vector2.ZERO))
	_check(v3.get_bullet_transform(0).origin == good_t.origin, "zero-scale transform rejected")
	v3.set_bullet_transform(0, Transform2D(0.0, Vector2(NAN, NAN)))
	_check(v3.get_bullet_transform(0).origin == good_t.origin, "NaN transform rejected")
	v3.teleport_bullet(0, Vector2(NAN, 0))
	v3.teleport_bullet(0, Vector2(INF, INF))
	_check(v3.get_bullet_transform(0).origin == good_t.origin, "NaN teleport rejected")
	v3.teleport_shift_bullet(0, Vector2(NAN, NAN))
	_check(v3.get_bullet_transform(0).origin == good_t.origin, "NaN shift rejected")
	v3.set_bullet_direction_towards_position(0, v3.get_bullet_global_transform(0).origin)
	_check(v3.get_bullet_direction(0).is_normalized(), "coincident aim keeps normalized heading")

	printerr("CRASH T4 freed Node2D targets mid-flight never crash")
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(3, 250.0))
	v4.set_homing_smoothing(6.0)
	v4.set_homing_take_control_of_texture_rotation(true)
	var doomed: Array = []
	for i in 3:
		var t := Node2D.new()
		t.position = Vector2(300 + 100 * i, -100 * i)
		get_root().add_child(t)
		doomed.append(t)
		v4.bullet_homing_push_back_node2d_target(i, t)
	v4.shared_homing_deque_push_back_global_position_target(Vector2(800, 0))
	v4.bullet_enable_orbiting(0, 50.0)
	for i in 10:
		await physics_frame
	for t in doomed:
		(t as Node).queue_free()
	await process_frame
	for i in 30:
		await physics_frame
	_check(_finite_volley(v4), "volley finite after all targets freed")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after freed targets")

	printerr("CRASH T5 disable/teleport storm keeps pool consistent")
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(4, 220.0))
	v5.set_homing_smoothing(5.0)
	v5.bullet_homing_push_back_global_position_target(0, Vector2(500, 0))
	v5.bullet_enable_orbiting(0, 55.0)
	for k in 12:
		v5.disable_bullet(k % 4)
		v5.teleport_shift_bullet((k + 1) % 4, Vector2(3, -2))
		await physics_frame
		v5.wake_bullet(k % 4)
	_check(_finite_volley(v5), "volley finite after disable/teleport storm")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after storm")

	printerr("CRASH T6 pool churn across shapes/owners stays clean")
	for k in 8:
		var n: int = 2 + (k % 3)
		var dd := H.make_directional_data(n, 150.0 + 25.0 * k)
		if k % 2 == 0:
			var wobs: Array = []
			for i in n:
				var wb := BulletWobbleData2D.new()
				wb.enabled = true
				wobs.append(wb)
			dd.all_bullet_wobble_data = wobs
		var vv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dd)
		for i in 5:
			await physics_frame
		_check(_finite_volley(vv), "churn volley %d finite" % k)
		for i in vv.get_amount_bullets():
			vv.disable_bullet(i)
		await physics_frame
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after churn")

	printerr("CRASH T7 maxed deque + maxed timers + maxed collisions")
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 180.0))
	for i in 300:
		v7.bullet_homing_push_back_global_position_target(0, Vector2(i, i))
	_check(v7.bullet_homing_check_targets_amount(0) == 256, "deque capped at 256, no unbounded growth")
	# Attach flood runs on idle frames (counts apply immediately there).
	await process_frame
	await process_frame
	for i in 70:
		v7.multimesh_attach_time_based_function(30.0, func() -> void: pass)
	_check(v7.debug_get_timer_count() == 64, "timer cap holds under flood")
	v7.set_bullet_max_collision_count(2)
	v7.set_bullet_collision_count(0, 999)
	_check(v7.get_bullet_collision_count(0) == 1, "collision clamps to max-1")
	v7.set_bullet_collision_count(0, -99)
	_check(v7.get_bullet_collision_count(0) == 0, "collision clamps negatives")
	await process_frame
	await process_frame
	v7.multimesh_detach_all_time_based_functions()
	_check(_finite_volley(v7), "volley finite after floods")

	printerr("CRASH T8 zero bullets / single bullet / mega volley")
	var d0 := H.make_directional_data(1, 100.0)
	d0.transforms = []
	var v0: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d0)
	_check(v0 == null or v0.get_amount_bullets() <= 1, "empty transforms rejected or empty")
	var v8: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 300.0))
	for i in 10:
		await physics_frame
	_check(_finite_volley(v8), "single bullet finite")
	var vm: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(64, 200.0))
	vm.all_bullets_set_wobble_data(_mk_wob(20.0))
	vm.all_bullets_set_gravity(Vector2(0, 300))
	vm.all_bullets_push_back_homing_target(Vector2(900, 0))
	for i in 20:
		await physics_frame
	_check(_finite_volley(vm), "64-bullet full-stack finite")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL CRASH-PROOF TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)

func _mk_wob(amp: float) -> BulletWobbleData2D:
	var w := BulletWobbleData2D.new()
	w.enabled = true
	w.amplitude = amp
	w.frequency_hz = 2.0
	return w
