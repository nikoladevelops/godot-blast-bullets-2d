extends BlastTest
## Curves + wobble + movement patterns: shared/per-bullet BulletCurvesData2D,
## null removal restores direct control, patterns from Path2D/Curve2D, gravity
## and drag, shared speed/rotation fallback, tiled vs strict curves, rotation
## and gravity-strength curves, angular/cosine/phased wobble, reverse flight.


func _make_curve() -> BulletCurvesData2D:
	var c := BulletCurvesData2D.new()
	var sc := Curve.new()
	sc.add_point(Vector2(0, 0))
	sc.add_point(Vector2(1, 1))
	c.movement_speed_curve = sc
	return c


func _flat(value: float, lo: float, hi: float) -> Curve:
	var c := Curve.new()
	c.min_value = lo
	c.max_value = hi
	c.add_point(Vector2(0, value))
	c.add_point(Vector2(1, value))
	return c


func test_shared_curves_attach_and_override_guard() -> void:
	var v: DirectionalBullets2D = spawn_dir(2, 200.0)
	assert_false(v.has_shared_bullet_curves_data(), "no shared curves initially")
	v.set_shared_bullet_curves_data(_make_curve())
	assert_true(v.has_shared_bullet_curves_data(), "shared curves attached")
	var keep := BulletSpeedData2D.new()
	keep.speed = 999.0
	v.set_bullet_speed_data(0, keep)
	swallow_errors() # speed write under a curve warns; the curve keeps control
	v.remove_shared_bullet_curves_data()
	assert_false(v.has_shared_bullet_curves_data(), "shared curves removed")
	v.set_bullet_speed_data(0, keep)
	assert_almost_eq(v.get_bullet_speed_data(0).speed, 999.0, 0.01, "direct control restored after removal")


func test_per_bullet_curves() -> void:
	var v: DirectionalBullets2D = spawn_dir(2, 200.0)
	var c0 := _make_curve()
	v.bullet_set_curves_data(0, c0)
	assert_eq(v.bullet_get_curves_data(0), c0, "per-bullet curves stored")
	v.bullet_set_curves_data(0, null)
	assert_null(v.bullet_get_curves_data(0), "per-bullet curves cleared with null")
	expect_error("This bullet has no individual curves data")
	v.all_bullets_set_curves_data(_make_curve())
	assert_not_null(v.bullet_get_curves_data(1), "all_bullets curves fan out")


func test_movement_patterns() -> void:
	var v: DirectionalBullets2D = spawn_dir(2, 200.0)
	var path: Path2D = add(Path2D.new())
	var curve := Curve2D.new()
	curve.add_point(Vector2(0, 0))
	curve.add_point(Vector2(100, 0))
	curve.add_point(Vector2(100, 100))
	path.curve = curve
	await idle(1)
	v.set_bullet_movement_pattern_from_path(0, path, false, true)
	assert_true(v.has_bullet_movement_pattern(0), "pattern from Path2D set")
	assert_false(v.has_bullet_movement_pattern(1), "sibling has no pattern")
	v.remove_bullet_movement_pattern(0)
	assert_false(v.has_bullet_movement_pattern(0), "pattern removed")
	v.all_bullets_set_movement_pattern_from_curve(curve, false, true)
	assert_true(v.has_bullet_movement_pattern(1), "pattern from curve fans out")
	v.all_bullets_remove_movement_pattern()
	assert_false(v.has_bullet_movement_pattern(1), "all patterns removed")
	v.set_bullet_movement_pattern_from_path(0, null)
	swallow_errors()
	assert_false(v.has_bullet_movement_pattern(0), "null path clears")


func test_gravity_accelerates_px_per_s2() -> void:
	var g: DirectionalBullets2D = spawn_dir(1, 0.0)
	g.set_gravity(Vector2(0, 2000))
	g.set_linear_drag(0.0)
	var p0: Vector2 = g.get_bullet_global_transform(0).origin
	await physics(60)
	# Semi-implicit Euler: dy ~= g*dt^2*n(n+1)/2 ~= 1050 px.
	assert_between(g.get_bullet_global_transform(0).origin.y - p0.y, 700.0, 1400.0, "gravity falls")
	assert_gt(g.get_bullet_velocity(0).y, 1500.0, "reported velocity integrates fall speed")
	assert_eq(g.get_gravity(), Vector2(0, 2000), "gravity getter round-trips")
	g.set_gravity(Vector2(NAN, 0))
	expect_any_error("NaN gravity fails loud")
	assert_eq(g.get_gravity(), Vector2(0, 2000), "NaN gravity rejected")
	g.set_gravity(Vector2.ZERO)
	var vb: Vector2 = g.get_bullet_velocity(0)
	await physics()
	assert_lt(g.get_bullet_velocity(0).y, vb.y, "zeroing gravity stops accelerating")


func test_shared_speed_rotation_fallback() -> void:
	var s: DirectionalBullets2D = spawn_dir(2, 100.0)
	var sh := BulletSpeedData2D.new()
	sh.speed = 400.0
	assert_almost_eq(s.get_bullet_speed_data(0).speed, 100.0, 0.01, "spawn ballistics intact before shared")
	s.set_shared_bullet_speed_data(sh)
	assert_true(s.has_shared_bullet_speed_data(), "shared speed stored")
	assert_almost_eq(s.get_bullet_speed_data(0).speed, 100.0, 0.01, "valid per-bullet slot wins over live shared")
	s.remove_shared_bullet_speed_data()
	assert_false(s.has_shared_bullet_speed_data(), "shared speed removed")
	var rot := BulletRotationData2D.new()
	rot.rotation_speed = 2.0
	s.set_shared_bullet_rotation_data(rot)
	assert_true(s.has_shared_bullet_rotation_data(), "shared rotation stored")
	s.remove_shared_bullet_rotation_data()
	assert_false(s.has_shared_bullet_rotation_data(), "shared rotation removed")
	assert_false(s.get_is_wobble_enabled(), "wobble off by default")
	var wob := BulletWobbleData2D.new()
	wob.enabled = true
	var wdata := H.make_directional_data(2, 200.0)
	wdata.shared_bullet_wobble_data = wob
	var wv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(wdata)
	assert_true(wv.get_is_wobble_enabled(), "shared wobble enables the feature")


func test_tiled_vs_strict_curves() -> void:
	var tc := BulletCurvesData2D.new()
	tc.movement_speed_curve = _flat(700.0, 0.0, 2000.0)
	tc.movement_use_unit_curve = false
	var tiled := H.make_directional_data(4, 200.0)
	tiled.all_bullet_curves_data = [tc]
	tiled.tile_all_bullet_curves_data = true
	var tv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(tiled)
	await physics(2)
	assert_almost_eq(tv.get_bullet_speed_data(0).speed, 700.0, 60.0, "tiled curves slot 0")
	assert_almost_eq(tv.get_bullet_speed_data(3).speed, 700.0, 60.0, "tiled curves slot 3 wraps")
	assert_eq(str(tv.debug_get_curves_info(3)["speed_src"]), "per", "tiled slot reports the per winner")
	var strict := H.make_directional_data(4, 200.0)
	strict.all_bullet_curves_data = [tc]
	var sv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(strict)
	await physics()
	assert_eq(str(sv.debug_get_curves_info(3)["speed_src"]), "none", "strict slot 3 has no curve")


func test_rotation_speed_curve_spins_visual() -> void:
	var d := H.make_directional_data(1, 0.0)
	var rgc := BulletCurvesData2D.new()
	rgc.rotation_speed_curve = _flat(4.0, -100.0, 100.0)
	d.all_bullet_curves_data = [rgc]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	var y0: float = v.get_bullet_texture_rotation_radians(0)
	await physics(20)
	assert_gt(v.get_bullet_texture_rotation_radians(0) - y0, 0.5, "rotation curve spins visual")
	assert_eq(str(v.debug_get_curves_info(0)["rot_src"]), "per", "rotation curve reports the per winner")


func test_gravity_strength_curve_per_bullet() -> void:
	var d := H.make_directional_data(2, 100.0)
	d.all_bullet_gravity = [Vector2(0, 600), Vector2(0, 600)]
	var gz := BulletCurvesData2D.new()
	gz.gravity_strength_curve = _flat(0.0, 0.0, 10.0)
	gz.gravity_use_unit_curve = false
	var gfull := BulletCurvesData2D.new()
	gfull.gravity_strength_curve = _flat(1.0, 0.0, 10.0)
	gfull.gravity_use_unit_curve = false
	d.all_bullet_curves_data = [gz, gfull]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	await physics(30)
	assert_lt(v.bullet_get_fall_speed(0), 0.01, "zeroed strength holds slot 0")
	assert_gt(v.bullet_get_fall_speed(1), 5.0, "full strength drops slot 1")
	assert_almost_eq(float(v.debug_get_gravity_info(0)["curve_scale"]), 0.0, 0.01, "gravity debug scale zeroed")


func test_wobble_angular_cosine_phase_windows() -> void:
	var d := H.make_directional_data(3, 300.0)
	var wa := BulletWobbleData2D.new()
	wa.enabled = true
	wa.mode = BulletWobbleData2D.WOBBLE_ANGULAR
	wa.amplitude = 30.0
	wa.frequency_hz = 2.0
	wa.waveform = BulletWobbleData2D.WOBBLE_COSINE
	wa.phase_step_per_bullet = 1.5
	wa.distance_phased = true
	wa.damping_per_sec = 1.0
	d.all_bullet_wobble_data = [wa, wa, wa]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	var a0: Vector2 = v.get_bullet_direction(0)
	await physics(15)
	var spread: float = (v.get_bullet_direction(0) - v.get_bullet_direction(1)).length() + (v.get_bullet_direction(1) - v.get_bullet_direction(2)).length()
	assert_gt((v.get_bullet_direction(0) - a0).length(), 0.02, "angular mode steers heading")
	assert_gt(spread, 0.05, "phase_step fans slots apart")
	assert_true(v.get_bullet_transform(0).is_finite() and v.get_bullet_transform(2).is_finite(), "angular/cosine/phased finite")
	assert_eq(int(v.debug_get_wobble_info(2)["waveform"]), 1, "wobble debug reports cosine")
	var d2 := H.make_directional_data(1, 300.0)
	var wb := BulletWobbleData2D.new()
	wb.enabled = true
	wb.amplitude = 40.0
	wb.frequency_hz = 3.0
	wb.damping_per_sec = 2.0
	wb.duration_sec = 0.3
	d2.all_bullet_wobble_data = [wb]
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d2)
	await physics(40)
	assert_true(v2.get_bullet_transform(0).is_finite(), "damped/windowed wobble finite")


func test_negative_speed_under_gravity() -> void:
	var d := H.make_directional_data(1, 0.0)
	var nsp := BulletSpeedData2D.new()
	nsp.speed = -200.0
	nsp.max_speed = 3000.0
	d.all_bullet_speed_data = [nsp]
	d.all_bullet_gravity = [Vector2(0, 400)]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	var p0: Vector2 = v.get_bullet_global_transform(0).origin
	await physics(30)
	assert_true(v.get_bullet_transform(0).is_finite(), "reverse gravity flight finite")
	assert_lt(v.get_bullet_global_transform(0).origin.x, p0.x - 10.0, "reverse flight retreats")
	assert_gt(v.bullet_get_fall_speed(0), 1.0, "gravity still integrates in reverse")
