extends BlastTest
## Shared-vs-per-bullet model edges: wobble texture-follow (face on/off, slew
## snap, rotation-data skip), wobble live API, a full mixed stack, tick
## safety (coincident aim, zero-radius orbit, zero heading), pool-reuse
## neutrality, non-repeating pattern finish, monitorable round-trip.


func _wob(amp: float, face: bool = true, slew: float = 18.0) -> BulletWobbleData2D:
	var w := BulletWobbleData2D.new()
	w.enabled = true
	w.mode = BulletWobbleData2D.WOBBLE_LATERAL
	w.amplitude = amp
	w.frequency_hz = 3.0
	w.phase_step_per_bullet = 0.0
	w.face_movement_direction = face
	w.face_rotation_speed = slew
	return w


func test_wobble_face_follows_heading() -> void:
	var d := H.make_directional_data(2, 300.0)
	d.all_bullet_wobble_data = [_wob(40.0, true), _wob(40.0, false)]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	assert_true(v.bullet_get_wobble_face_movement_direction(0), "face flag seeded slot 0")
	assert_false(v.bullet_get_wobble_face_movement_direction(1), "face flag seeded slot 1")
	var yaw0: float = v.get_bullet_texture_rotation_radians(0)
	var yaw1: float = v.get_bullet_texture_rotation_radians(1)
	await physics(30)
	assert_gt(absf(v.get_bullet_texture_rotation_radians(0) - yaw0), 0.02, "face-on visual yaws with wobble")
	assert_lt(absf(v.get_bullet_texture_rotation_radians(1) - yaw1), 0.005, "face-off visual yaw untouched")
	assert_true(v.get_bullet_transform(0).is_finite() and v.get_bullet_transform(1).is_finite(), "both finite")


func test_snap_slew_and_rotation_data_skip() -> void:
	var d := H.make_directional_data(1, 300.0)
	d.all_bullet_wobble_data = [_wob(40.0, true, 0.0)]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	assert_almost_eq(v.bullet_get_wobble_face_rotation_speed(0), 0.0, 0.001, "snap slew seeded")
	await physics(10)
	assert_true(v.get_bullet_transform(0).is_finite(), "snap-slew wobble finite")
	var db := H.make_directional_data(1, 300.0)
	db.all_bullet_wobble_data = [_wob(40.0, true)]
	db.all_bullet_rotation_data = [H.make_rotation(3.0, 100.0)]
	var vb: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(db)
	var yaw: float = vb.get_bullet_texture_rotation_radians(0)
	await physics(10)
	assert_gt(absf(vb.get_bullet_texture_rotation_radians(0) - yaw), 0.05, "rotation data still spins under wobble")


func test_wobble_live_api() -> void:
	var v: DirectionalBullets2D = spawn_dir(2, 200.0)
	assert_false(v.get_is_wobble_enabled(), "wobble off fresh")
	var live := _wob(30.0)
	v.bullet_set_wobble_data(0, live)
	assert_true(v.get_is_wobble_enabled(), "live set enables the feature")
	assert_eq(v.bullet_get_wobble_data(0), live, "per-bullet resource readable")
	assert_null(v.bullet_get_wobble_data(1), "unset slot reads null")
	assert_almost_eq(v.bullet_get_wobble_amplitude(0), 30.0, 0.01, "amplitude getter")
	v.set_shared_bullet_wobble_data(_wob(12.0))
	assert_true(v.has_shared_bullet_wobble_data(), "shared stored")
	assert_almost_eq(v.bullet_get_wobble_amplitude(0), 30.0, 0.01, "live per-bullet wins over new shared")
	assert_almost_eq(v.bullet_get_wobble_amplitude(1), 12.0, 0.01, "empty slot seeded from shared")
	v.bullet_set_wobble_data(0, null)
	assert_almost_eq(v.bullet_get_wobble_amplitude(0), 12.0, 0.01, "null clears back to shared")
	v.remove_shared_bullet_wobble_data()
	assert_false(v.has_shared_bullet_wobble_data(), "shared removed")
	assert_almost_eq(v.bullet_get_wobble_amplitude(1), 0.0, 0.01, "fallback slots go inactive on shared clear")
	assert_almost_eq(v.bullet_get_wobble_amplitude(0), 0.0, 0.01, "cleared slot inactive without shared")
	assert_false(v.debug_get_wobble_info(0).get("has_shared_fallback", true), "debug reports no shared fallback")
	v.bullet_set_wobble_data(1, live)
	var info1: Dictionary = v.debug_get_wobble_info(1)
	assert_true(info1.get("active", false) and info1.get("has_per_bullet_resource", false), "debug reports live seed")
	var off := BulletWobbleData2D.new()
	off.enabled = false
	v.bullet_set_wobble_data(1, off)
	expect_error("wobble data is disabled or invalid")
	assert_almost_eq(v.bullet_get_wobble_amplitude(1), 0.0, 0.01, "disabled live data clears the slot")
	v.all_bullets_set_wobble_data(_wob(20.0))
	assert_almost_eq(v.bullet_get_wobble_amplitude(1), 20.0, 0.01, "all_bullets fan reseeds")


func test_full_stack_composes() -> void:
	var d := H.make_directional_data(2, 250.0)
	d.all_bullet_wobble_data = [_wob(24.0), _wob(24.0)]
	d.gravity = Vector2(0, 600)
	d.linear_drag = 0.2
	d.homing_smoothing = 4.0
	d.homing_take_control_of_texture_rotation = true
	var gc := BulletCurvesData2D.new()
	var ramp := Curve.new()
	ramp.add_point(Vector2(0, 0.5))
	ramp.add_point(Vector2(1, 1.0))
	gc.movement_speed_curve = ramp
	d.shared_bullet_curves_data = gc
	var tgt: Node2D = add(Node2D.new())
	tgt.position = Vector2(600, -300)
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	v.bullet_homing_push_back_node2d_target(0, tgt)
	v.bullet_homing_push_back_node2d_target(1, tgt)
	await physics(60)
	assert_true(v.get_bullet_transform(0).is_finite() and v.get_bullet_transform(1).is_finite(), "stack stays finite")
	assert_gt(v.bullet_get_fall_speed(0), 5.0, "gravity integrates under the stack")
	assert_true(v.get_is_wobble_enabled(), "wobble stays enabled under the stack")


func test_tick_safety_edges() -> void:
	var v: DirectionalBullets2D = spawn_dir(1, 200.0)
	var yaw: float = v.get_bullet_texture_rotation_radians(0)
	v.set_bullet_texture_rotation_towards_position(0, v.get_bullet_global_transform(0).origin)
	expect_error("already at the target position")
	assert_almost_eq(v.get_bullet_texture_rotation_radians(0), yaw, 0.0001, "coincident texture aim keeps rotation")
	var orb: Node2D = add(Node2D.new())
	orb.position = v.get_bullet_global_transform(0).origin
	await idle(1)
	v.set_homing_smoothing(6.0)
	v.bullet_homing_push_back_node2d_target(0, orb)
	v.bullet_enable_orbiting(0, 60.0)
	await physics(20)
	assert_true(v.get_bullet_transform(0).is_finite(), "zero-radius orbit stays finite")
	var vz: DirectionalBullets2D = spawn_dir(1, 0.0)
	vz.set_homing_smoothing(6.0)
	vz.set_homing_take_control_of_texture_rotation(true)
	vz.bullet_homing_push_back_global_position_target(0, Vector2(500, 0))
	await physics(10)
	assert_true(vz.get_bullet_transform(0).is_finite(), "zero-heading homing holds without NaN")


func test_pool_reuse_neutrality() -> void:
	var dw := H.make_directional_data(2, 200.0)
	dw.all_bullet_wobble_data = [_wob(30.0), _wob(30.0)]
	dw.shared_bullet_wobble_data = _wob(9.0)
	dw.adjust_direction_based_on_rotation = true
	var vw: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dw)
	assert_true(vw.get_is_wobble_enabled(), "wobble volley enabled")
	assert_not_null(vw.bullet_get_wobble_data(0), "per-bullet resource tracked")
	vw.disable_bullet(0)
	vw.disable_bullet(1)
	await physics()
	var vn: DirectionalBullets2D = spawn_dir(2, 200.0)
	assert_false(vn.get_is_wobble_enabled(), "reuse has no wobble")
	assert_null(vn.bullet_get_wobble_data(0), "reuse has no per-bullet resource")
	assert_false(vn.has_shared_bullet_wobble_data(), "reuse has no shared wobble")
	assert_almost_eq(vn.bullet_get_wobble_amplitude(0), 0.0, 0.01, "reuse amplitude zero")
	assert_false(vn.get_adjust_direction_based_on_rotation(), "reuse adjust flag neutral")
	assert_almost_eq(vn.get_curves_elapsed_time(), 0.0, 0.001, "reuse clock reset")


func test_non_repeating_pattern_finishes() -> void:
	var v: DirectionalBullets2D = spawn_dir(1, 100.0)
	var c := Curve2D.new()
	c.add_point(Vector2(0, 0))
	c.add_point(Vector2(50, 0))
	v.set_bullet_movement_pattern_from_curve(0, c, true, false)
	assert_true(v.has_bullet_movement_pattern(0), "pattern set")
	await physics(60)
	assert_false(v.has_bullet_movement_pattern(0), "non-repeating pattern finishes and clears")
	assert_true(v.get_bullet_transform(0).is_finite(), "pattern finish finite")


func test_monitorable_round_trip() -> void:
	var v: DirectionalBullets2D = spawn_dir(1, 100.0)
	v.set_monitorable(true)
	assert_true(v.get_monitorable(), "monitorable round-trips")
	v.set_monitorable(false)
	assert_false(v.get_monitorable(), "monitorable clears")
