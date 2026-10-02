extends BlastTest
## Every major feature crossed with others: each test combines 2-4 systems,
## runs physics and asserts the mix stays finite, shows each system's
## signature effect and cleans up (after_each checks no dangling).


func _finite(v: DirectionalBullets2D) -> bool:
	for i in v.get_amount_bullets():
		if not v.get_bullet_transform(i).is_finite() or not v.get_bullet_velocity(i).is_finite():
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


func test_homing_snakes() -> void:
	var d := H.make_directional_data(3, 260.0)
	d.all_bullet_wobble_data = [_wob(36.0), _wob(36.0), _wob(36.0)]
	d.homing_smoothing = 5.0
	d.homing_take_control_of_texture_rotation = true
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	var tgt: Node2D = add(Node2D.new())
	tgt.position = Vector2(700, -250)
	for i in 3:
		v.bullet_homing_push_back_node2d_target(i, tgt)
	var yaw0: float = v.get_bullet_texture_rotation_radians(0)
	await physics(60)
	assert_true(_finite(v), "homing snakes finite")
	assert_gt(absf(v.get_bullet_texture_rotation_radians(0) - yaw0), 0.05, "snake texture follows path")
	var to_target := (tgt.global_position - v.get_bullet_global_transform(0).origin).normalized()
	assert_lt(absf(v.get_bullet_direction(0).angle_to(to_target)), 0.6, "snakes still converge while weaving")


func test_orbiting_wobble_ring() -> void:
	var d := H.make_directional_data(4, 240.0)
	d.all_bullet_wobble_data = [_wob(20.0, 2.0), _wob(20.0, 2.0), _wob(20.0, 2.0), _wob(20.0, 2.0)]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	v.set_homing_smoothing(7.0)
	v.shared_homing_deque_push_back_global_position_target(Vector2(400, 0))
	v.all_bullets_enable_orbiting(70.0)
	await physics(120)
	assert_true(_finite(v), "wobble ring finite")
	assert_true(v.bullet_is_orbiting_locked(0) or v.bullet_is_orbiting_enabled(0), "ring engaged under wobble")
	assert_true(v.get_is_wobble_enabled(), "wobble alive on ring")


func test_gravity_drag_speed_curve() -> void:
	var d := H.make_directional_data(2, 350.0)
	d.gravity = Vector2(0, 1400)
	d.linear_drag = 0.35
	d.shared_bullet_curves_data = _curve_move()
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	var p0: Vector2 = v.get_bullet_global_transform(0).origin
	var s0: float = v.get_bullet_speed_data(0).speed
	await physics(60)
	assert_true(_finite(v), "gravity/drag/curve finite")
	assert_gt(v.get_bullet_global_transform(0).origin.y, p0.y + 150.0, "arc drops under gravity")
	assert_lt(v.get_bullet_speed_data(0).speed, s0 + 400.0, "drag+curve bound speed growth")
	assert_gt(v.bullet_get_fall_speed(0), 20.0, "fall accumulates under curve")


func test_spin_pattern_homing() -> void:
	var d := H.make_directional_data(2, 220.0)
	d.all_bullet_rotation_data = [H.make_rotation(4.0, 60.0), H.make_rotation(4.0, 60.0)]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	var pc := Curve2D.new()
	pc.add_point(Vector2(0, 0))
	pc.add_point(Vector2(120, 40))
	pc.add_point(Vector2(200, 0))
	v.set_bullet_movement_pattern_from_curve(0, pc, true, true)
	v.set_homing_smoothing(9.0)
	v.bullet_homing_push_back_global_position_target(0, Vector2(600, 100))
	v.bullet_homing_push_back_global_position_target(1, Vector2(600, 100))
	await physics(60)
	assert_true(_finite(v), "spin+pattern+homing finite")
	assert_true(v.has_bullet_movement_pattern(0), "pattern survives the homing/rotation mix")
	assert_gt(absf(v.get_bullet_texture_rotation_radians(1)), 0.2, "spinner spins under homing")


func test_sovereign_per_bullet_everything() -> void:
	var d := H.make_directional_data(4, 200.0)
	var sp: Array = []
	for k in 4:
		sp.append(H.make_speed(150.0 + 60.0 * k))
	d.all_bullet_speed_data = sp
	d.all_bullet_gravity = [Vector2.ZERO, Vector2(0, 800), Vector2(400, 0), Vector2.ZERO]
	d.all_bullet_wobble_data = [_wob(10.0), _wob(30.0), _wob(50.0), _wob(10.0)]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	v.bullet_set_homing_smoothing(0, 0.5)
	v.bullet_set_homing_smoothing(3, 12.0)
	v.bullet_homing_push_back_global_position_target(0, Vector2(-600, 0))
	v.bullet_homing_push_back_global_position_target(3, Vector2(600, 0))
	v.bullet_enable_orbiting(2, 45.0)
	await physics(60)
	assert_true(_finite(v), "sovereign bullets finite")
	assert_gt(absf(v.get_bullet_speed_data(3).speed - v.get_bullet_speed_data(0).speed), 100.0, "per-bullet speeds stay distinct")
	assert_almost_eq(v.bullet_get_wobble_amplitude(2), 50.0, 0.01, "per-bullet wobble stays distinct")
	assert_eq(v.bullet_get_gravity(1), Vector2(0, 800), "per-bullet gravity stays distinct")


func test_spawner_driven_mixed_volley() -> void:
	var sd := H.make_directional_data(4, 230.0)
	sd.all_bullet_wobble_data = [_wob(26.0), _wob(26.0), _wob(26.0), _wob(26.0)]
	sd.gravity = Vector2(0, 350)
	sd.homing_smoothing = 5.0
	sd.homing_take_control_of_texture_rotation = true
	var spawner := make_spawner(sd, 1)
	spawner.homing_mode = 1
	spawner.homing_target_source = 2
	spawner.homing_global_position = Vector2(500, -100)
	spawner.set_homing_enabled(true)
	assert_true(spawner.shoot_once(), "mixed spawner shot fires")
	await physics(40)
	var live: Array = spawner.get_live_volleys()
	if live.is_empty():
		assert_gte(factory.debug_get_total_bullets_amount(0), 1, "mixed volley exists in factory")
	else:
		var lv: DirectionalBullets2D = live[0]
		assert_true(_finite(lv), "spawner mixed volley finite")
		assert_true(lv.get_is_wobble_enabled(), "spawner volley carries wobble")
		assert_gt(lv.bullet_get_gravity(0).y, 0.0, "spawner volley carries gravity")


func test_lifetime_expiry_inside_mix() -> void:
	var d := H.make_directional_data(2, 200.0)
	d.max_life_time = 0.4
	d.all_bullet_wobble_data = [_wob(30.0), _wob(30.0)]
	d.gravity = Vector2(0, 800)
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	v.set_homing_smoothing(6.0)
	v.bullet_homing_push_back_global_position_target(0, Vector2(500, 0))
	await physics(60)
	assert_false(v.is_bullet_status_enabled(0) and v.is_bullet_status_enabled(1), "expiry disables the mixed volley")
