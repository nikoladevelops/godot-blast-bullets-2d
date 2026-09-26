extends SceneTree
## Mix-and-match suites: every major feature crossed with every other one.
## Each group spawns volleys combining 2-4 systems, runs physics, and asserts
## the composition (a) stays finite, (b) shows each system's signature effect,
## (c) cleans up with no dangling state. This is the user-satisfaction suite:
## the combos players actually build (snakes that home, orbiting rings that
## wobble, gravity arcs with drag on curves, spawner-driven mixed volleys).
## Run: godot --headless --path test_project --script tests/volley/test_volley_mix_match.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _finite(v: DirectionalBullets2D) -> bool:
	for i in v.get_amount_bullets():
		if not v.get_bullet_transform(i).is_finite():
			return false
		if not v.get_bullet_velocity(i).is_finite():
			return false
	return true

func _wob(amp: float, freq: float = 3.0) -> BulletWobbleData2D:
	var w := BulletWobbleData2D.new()
	w.enabled = true
	w.amplitude = amp
	w.frequency_hz = freq
	w.phase_step_per_bullet = 0.6
	return w

func _curve_move() -> BulletCurvesData2D:
	var c := BulletCurvesData2D.new()
	var s := Curve.new()
	s.add_point(Vector2(0, 0.6))
	s.add_point(Vector2(1, 1.2))
	c.movement_speed_curve = s
	return c

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("MIXMATCH T1 homing snakes (wobble face + per-bullet targets)")
	var d1 := H.make_directional_data(3, 260.0)
	d1.all_bullet_wobble_data = [_wob(36.0), _wob(36.0), _wob(36.0)]
	d1.homing_smoothing = 5.0
	d1.homing_take_control_of_texture_rotation = true
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d1)
	var tgt1 := Node2D.new()
	tgt1.position = Vector2(700, -250)
	get_root().add_child(tgt1)
	for i in 3:
		v1.bullet_homing_push_back_node2d_target(i, tgt1)
	var yaws0: Array = [v1.get_bullet_texture_rotation_radians(0), v1.get_bullet_texture_rotation_radians(1)]
	for i in 60:
		await physics_frame
	_check(_finite(v1), "homing snakes finite")
	_check(absf(v1.get_bullet_texture_rotation_radians(0) - (yaws0[0] as float)) > 0.05, "snake texture follows path")
	var ang_before: float = absf(v1.get_bullet_direction(0).angle_to((tgt1.global_position - v1.get_bullet_global_transform(0).origin).normalized()))
	_check(ang_before < 0.6, "snakes still converge while weaving (%.3f)" % ang_before)
	tgt1.queue_free()

	printerr("MIXMATCH T2 orbiting wobble ring (shared target + face wobble)")
	var d2 := H.make_directional_data(4, 240.0)
	var wob2: Array = []
	for i in 4:
		wob2.append(_wob(20.0, 2.0))
	d2.all_bullet_wobble_data = wob2
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d2)
	v2.set_homing_smoothing(7.0)
	v2.shared_homing_deque_push_back_global_position_target(Vector2(400, 0))
	v2.all_bullets_enable_orbiting(70.0)
	for i in 120:
		await physics_frame
	_check(_finite(v2), "wobble ring finite")
	_check(v2.bullet_is_orbiting_locked(0) or v2.bullet_is_orbiting_enabled(0), "ring engaged under wobble")
	_check(v2.get_is_wobble_enabled(), "wobble alive on ring")

	printerr("MIXMATCH T3 gravity arcs + drag + speed curve")
	var d3 := H.make_directional_data(2, 350.0)
	d3.gravity = Vector2(0, 1400)
	d3.linear_drag = 0.35
	d3.shared_bullet_curves_data = _curve_move()
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d3)
	var p0: Vector2 = v3.get_bullet_global_transform(0).origin
	var s0: float = v3.get_bullet_speed_data(0).speed
	for i in 60:
		await physics_frame
	var p1: Vector2 = v3.get_bullet_global_transform(0).origin
	_check(_finite(v3), "gravity/drag/curve finite")
	_check(p1.y > p0.y + 150.0, "arc drops under gravity")
	_check(v3.get_bullet_speed_data(0).speed < s0 + 400.0, "drag+curve bound speed growth")
	_check(v3.bullet_get_fall_speed(0) > 20.0, "fall accumulates under curve")

	printerr("MIXMATCH T4 rotation spin + patterns + homing snap")
	var d4 := H.make_directional_data(2, 220.0)
	var rr := BulletRotationData2D.new()
	rr.rotation_speed = 4.0
	rr.max_rotation_speed = 60.0
	var rr2 := BulletRotationData2D.new()
	rr2.rotation_speed = 4.0
	rr2.max_rotation_speed = 60.0
	d4.all_bullet_rotation_data = [rr, rr2]
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4)
	var pc := Curve2D.new()
	pc.add_point(Vector2(0, 0))
	pc.add_point(Vector2(120, 40))
	pc.add_point(Vector2(200, 0))
	v4.set_bullet_movement_pattern_from_curve(0, pc, true, true)
	v4.set_homing_smoothing(9.0)
	v4.bullet_homing_push_back_global_position_target(0, Vector2(600, 100))
	v4.bullet_homing_push_back_global_position_target(1, Vector2(600, 100))
	for i in 60:
		await physics_frame
	_check(_finite(v4), "spin+pattern+homing finite")
	_check(v4.has_bullet_movement_pattern(0), "pattern survives homing/rotation mix")
	_check(absf(v4.get_bullet_texture_rotation_radians(1)) > 0.2, "spinner spins under homing")

	printerr("MIXMATCH T5 per-bullet everything differs (sovereign bullets)")
	var d5 := H.make_directional_data(4, 200.0)
	var sp: Array = []
	for k in 4:
		var s := BulletSpeedData2D.new()
		s.speed = 150.0 + 60.0 * k
		s.max_speed = 3000.0
		sp.append(s)
	d5.all_bullet_speed_data = sp
	d5.all_bullet_gravity = [Vector2(0, 0), Vector2(0, 800), Vector2(400, 0), Vector2(0, 0)]
	d5.all_bullet_wobble_data = [_wob(10.0), _wob(30.0), _wob(50.0), _wob(10.0)]
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5)
	v5.bullet_set_homing_smoothing(0, 0.5)
	v5.bullet_set_homing_smoothing(3, 12.0)
	v5.bullet_homing_push_back_global_position_target(0, Vector2(-600, 0))
	v5.bullet_homing_push_back_global_position_target(3, Vector2(600, 0))
	v5.bullet_enable_orbiting(2, 45.0)
	for i in 60:
		await physics_frame
	_check(_finite(v5), "sovereign bullets finite")
	_check(absf(v5.get_bullet_speed_data(3).speed - v5.get_bullet_speed_data(0).speed) > 100.0, "per-bullet speeds stay distinct")
	_check(absf(v5.bullet_get_wobble_amplitude(2) - 50.0) < 0.01, "per-bullet wobble stays distinct")
	_check(v5.bullet_get_gravity(1) == Vector2(0, 800), "per-bullet gravity stays distinct")

	printerr("MIXMATCH T6 spawner-driven mixed volley (patterns source + homing + wobble seed)")
	var spawner := BulletSpawner2D.new()
	get_root().add_child(spawner)
	await process_frame
	spawner.set_bullet_factory(factory)
	var sd := H.make_directional_data(4, 230.0)
	var sdwob: Array = []
	for i in 4:
		sdwob.append(_wob(26.0))
	sd.all_bullet_wobble_data = sdwob
	sd.gravity = Vector2(0, 350)
	sd.homing_smoothing = 5.0
	sd.homing_take_control_of_texture_rotation = true
	spawner.set_spawn_data(sd)
	spawner.set_shooting_enabled(false)
	spawner.pattern_source = 1
	spawner.homing_mode = 1
	spawner.homing_target_source = 2
	spawner.homing_global_position = Vector2(500, -100)
	_check(spawner.shoot_once(), "mixed spawner shot fires")
	for i in 40:
		await physics_frame
	var live: Array = spawner.get_live_volleys()
	if live.is_empty():
		_check(factory.debug_get_total_bullets_amount(0) >= 1, "mixed volley exists in factory")
	else:
		var lv: DirectionalBullets2D = live[0]
		_check(_finite(lv), "spawner mixed volley finite")
		_check(lv.get_is_wobble_enabled(), "spawner volley carries wobble")
		_check(lv.bullet_get_gravity(0).y > 0.0, "spawner volley carries gravity")
	spawner.queue_free()

	printerr("MIXMATCH T7 lifetime expiry inside every mix (no stuck volleys)")
	var d7 := H.make_directional_data(2, 200.0)
	d7.max_life_time = 0.4
	d7.all_bullet_wobble_data = [_wob(30.0), _wob(30.0)]
	d7.gravity = Vector2(0, 800)
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d7)
	v7.set_homing_smoothing(6.0)
	v7.bullet_homing_push_back_global_position_target(0, Vector2(500, 0))
	for i in 60:
		await physics_frame
	_check(not v7.is_bullet_status_enabled(0) or not v7.is_bullet_status_enabled(1) or factory.debug_get_active_bullets_amount(0) == 0, "expiry disables mixed volley")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after expiry mix")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL MIX-MATCH TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
