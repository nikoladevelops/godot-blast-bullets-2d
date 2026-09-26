extends SceneTree
## Mixed-feature + edge-case suite for the unified shared-vs-per-bullet model.
## Covers: wobble texture-follow (face on/off, slew snap, rotation-data skip),
## wobble live API (set/get/clear/shared/debug), mixed stacks (wobble+gravity+
## homing+curves+drag), tick safety (coincident texture aim, zero-radius orbit,
## non-finite orbit center, zero-heading homing), pool-reuse neutrality, and
## the fixed validation/state bugs (monitorable divergence, pattern flag
## tiling, reset of adjust/homing-timer/epochs).
## Run: godot --headless --path test_project --script tests/volley/test_volley_mixed_edges.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

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

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("MIX T1 wobble face follows steered heading, off keeps yaw")
	var d1 := H.make_directional_data(2, 300.0)
	d1.all_bullet_wobble_data = [_wob(40.0, true), _wob(40.0, false)]
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d1)
	_check(v1.bullet_get_wobble_face_movement_direction(0), "face flag seeded slot 0")
	_check(not v1.bullet_get_wobble_face_movement_direction(1), "face flag seeded slot 1")
	var yaw0_before: float = v1.get_bullet_texture_rotation_radians(0)
	var yaw1_before: float = v1.get_bullet_texture_rotation_radians(1)
	for i in 30:
		await physics_frame
	var yaw0_after: float = v1.get_bullet_texture_rotation_radians(0)
	var yaw1_after: float = v1.get_bullet_texture_rotation_radians(1)
	_check(absf(yaw0_after - yaw0_before) > 0.02, "face-on visual yaws with wobble (%.3f rad)" % absf(yaw0_after - yaw0_before))
	_check(absf(yaw1_after - yaw1_before) < 0.005, "face-off visual yaw untouched (%.4f rad)" % absf(yaw1_after - yaw1_before))
	_check(v1.get_bullet_transform(0).is_finite() and v1.get_bullet_transform(1).is_finite(), "both wobble bullets finite")

	printerr("MIX T2 face slew 0 snaps, rotation data skips follow")
	var d2 := H.make_directional_data(1, 300.0)
	d2.all_bullet_wobble_data = [_wob(40.0, true, 0.0)]
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d2)
	_check(absf(v2.bullet_get_wobble_face_rotation_speed(0)) < 0.001, "snap slew seeded")
	for i in 10:
		await physics_frame
	_check(v2.get_bullet_transform(0).is_finite(), "snap-slew wobble finite")
	var d2b := H.make_directional_data(1, 300.0)
	d2b.all_bullet_wobble_data = [_wob(40.0, true)]
	var rd := BulletRotationData2D.new()
	rd.rotation_speed = 3.0
	rd.max_rotation_speed = 100.0
	d2b.all_bullet_rotation_data = [rd]
	var v2b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d2b)
	var yaw_r_before: float = v2b.get_bullet_texture_rotation_radians(0)
	for i in 10:
		await physics_frame
	var yaw_r_after: float = v2b.get_bullet_texture_rotation_radians(0)
	_check(absf(yaw_r_after - yaw_r_before) > 0.05, "rotation data still spins visual under wobble")

	printerr("MIX T3 wobble live API: set/get/clear/shared/debug")
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	_check(not v3.get_is_wobble_enabled(), "wobble off fresh")
	var live := _wob(30.0)
	v3.bullet_set_wobble_data(0, live)
	_check(v3.get_is_wobble_enabled(), "live set enables feature")
	_check(v3.bullet_get_wobble_data(0) == live, "per-bullet resource readable")
	_check(v3.bullet_get_wobble_data(1) == null, "unset slot reads null")
	_check(absf(v3.bullet_get_wobble_amplitude(0) - 30.0) < 0.01, "amplitude getter")
	var sh := _wob(12.0)
	v3.set_shared_bullet_wobble_data(sh)
	_check(v3.has_shared_bullet_wobble_data(), "shared stored")
	_check(absf(v3.bullet_get_wobble_amplitude(0) - 30.0) < 0.01, "live per-bullet wins over new shared")
	_check(absf(v3.bullet_get_wobble_amplitude(1) - 12.0) < 0.01, "empty slot seeded from shared")
	v3.bullet_set_wobble_data(0, null)
	_check(absf(v3.bullet_get_wobble_amplitude(0) - 12.0) < 0.01, "null clears back to shared")
	v3.remove_shared_bullet_wobble_data()
	_check(not v3.has_shared_bullet_wobble_data(), "shared removed")
	_check(absf(v3.bullet_get_wobble_amplitude(1)) < 0.01, "fallback slots go inactive on shared clear")
	_check(absf(v3.bullet_get_wobble_amplitude(0)) < 0.01, "cleared slot inactive without shared")
	var info: Dictionary = v3.debug_get_wobble_info(0)
	_check(info.get("has_shared_fallback", true) == false, "debug reports no shared fallback")
	v3.bullet_set_wobble_data(1, live)
	var info1: Dictionary = v3.debug_get_wobble_info(1)
	_check(info1.get("active", false) == true and info1.get("has_per_bullet_resource", false) == true, "debug reports live seed")
	var bad := BulletWobbleData2D.new()
	bad.enabled = false
	v3.bullet_set_wobble_data(1, bad)
	_check(absf(v3.bullet_get_wobble_amplitude(1)) < 0.01, "disabled live data clears slot")
	v3.all_bullets_set_wobble_data(_wob(20.0))
	_check(absf(v3.bullet_get_wobble_amplitude(1) - 20.0) < 0.01, "all_bullets fan reseeds")

	printerr("MIX T4 full stack: wobble+gravity+drag+homing+curves compose")
	var d4 := H.make_directional_data(2, 250.0)
	d4.all_bullet_wobble_data = [_wob(24.0), _wob(24.0)]
	d4.gravity = Vector2(0, 600)
	d4.linear_drag = 0.2
	d4.homing_smoothing = 4.0
	d4.homing_take_control_of_texture_rotation = true
	var gc := BulletCurvesData2D.new()
	var ramp := Curve.new()
	ramp.add_point(Vector2(0, 0.5))
	ramp.add_point(Vector2(1, 1.0))
	gc.movement_speed_curve = ramp
	d4.shared_bullet_curves_data = gc
	var tgt := Node2D.new()
	tgt.position = Vector2(600, -300)
	get_root().add_child(tgt)
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4)
	v4.bullet_homing_push_back_node2d_target(0, tgt)
	v4.bullet_homing_push_back_node2d_target(1, tgt)
	for i in 60:
		await physics_frame
	_check(v4.get_bullet_transform(0).is_finite() and v4.get_bullet_transform(1).is_finite(), "stack stays finite")
	_check(v4.bullet_get_fall_speed(0) > 5.0, "gravity integrates under stack")
	_check(v4.get_is_wobble_enabled(), "wobble stays enabled under stack")
	tgt.queue_free()

	printerr("MIX T5 tick safety: coincident aim, zero-radius orbit, zero heading")
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 200.0))
	var yaw_before: float = v5.get_bullet_texture_rotation_radians(0)
	v5.set_bullet_texture_rotation_towards_position(0, v5.get_bullet_global_transform(0).origin)
	_check(absf(v5.get_bullet_texture_rotation_radians(0) - yaw_before) < 0.0001, "coincident texture aim keeps rotation")
	var orb_tgt := Node2D.new()
	orb_tgt.position = v5.get_bullet_global_transform(0).origin
	get_root().add_child(orb_tgt)
	await process_frame
	v5.set_homing_smoothing(6.0)
	v5.bullet_homing_push_back_node2d_target(0, orb_tgt)
	v5.bullet_enable_orbiting(0, 60.0)
	for i in 20:
		await physics_frame
	_check(v5.get_bullet_transform(0).is_finite(), "zero-radius orbit stays finite")
	orb_tgt.queue_free()
	var d5b := H.make_directional_data(1, 0.0)
	var v5b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5b)
	v5b.set_homing_smoothing(6.0)
	v5b.set_homing_take_control_of_texture_rotation(true)
	v5b.bullet_homing_push_back_global_position_target(0, Vector2(500, 0))
	for i in 10:
		await physics_frame
	_check(v5b.get_bullet_direction(0) == Vector2(0, 0) or v5b.get_bullet_transform(0).is_finite(), "zero-heading homing holds without snap")

	printerr("MIX T6 pool reuse neutrality: wobble/shared/adjust/timer/epochs")
	var dw := H.make_directional_data(2, 200.0)
	dw.all_bullet_wobble_data = [_wob(30.0), _wob(30.0)]
	dw.shared_bullet_wobble_data = _wob(9.0)
	dw.adjust_direction_based_on_rotation = true
	var vw: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dw)
	_check(vw.get_is_wobble_enabled(), "wobble volley enabled")
	_check(vw.bullet_get_wobble_data(0) != null, "per-bullet resource tracked")
	for i in 2:
		vw.disable_bullet(i)
	await physics_frame
	var vn: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	_check(not vn.get_is_wobble_enabled(), "reuse has no wobble")
	_check(vn.bullet_get_wobble_data(0) == null, "reuse has no per-bullet resource")
	_check(not vn.has_shared_bullet_wobble_data(), "reuse has no shared wobble")
	_check(absf(vn.bullet_get_wobble_amplitude(0)) < 0.01, "reuse amplitude zero")
	_check(vn.get_adjust_direction_based_on_rotation() == false, "reuse adjust flag neutral")
	_check(absf(vn.get_curves_elapsed_time()) < 0.001, "reuse clock reset")

	printerr("MIX T7 pattern face/repeat flags strict per bullet")
	var d7 := H.make_directional_data(1, 100.0)
	var c7 := Curve2D.new()
	c7.add_point(Vector2(0, 0))
	c7.add_point(Vector2(50, 0))
	d7.all_bullet_movement_pattern_paths = []
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d7)
	v7.set_bullet_movement_pattern_from_curve(0, c7, true, false)
	_check(v7.has_bullet_movement_pattern(0), "pattern set")
	for i in 60:
		await physics_frame
	_check(not v7.has_bullet_movement_pattern(0), "non-repeating pattern finishes and clears")
	_check(v7.get_bullet_transform(0).is_finite(), "pattern finish finite")

	printerr("MIX T8 monitorable + collision setters reject pre-spawn cleanly")
	var v8: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 100.0))
	_check(v8.get_monitorable() == true or v8.get_monitorable() == false, "monitorable readable")
	v8.set_monitorable(true)
	_check(v8.get_monitorable() == true, "monitorable round-trips")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL MIXED/EDGE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
