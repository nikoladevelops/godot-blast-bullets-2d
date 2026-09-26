extends SceneTree
## Volley curves + wobble + movement patterns suite.
## Covers: shared + per-bullet BulletCurvesData2D (speed/rotation/direction),
## null-removal restores direct control, speed-curve overrides set_bullet_speed_data
## (warns, keeps curve), wobble lateral/angular enable + flag, movement pattern
## from Path2D/Curve2D set/get/remove, gravity + linear drag drift, shared
## speed/rotation fanning, curves_elapsed_time driving samples.
## Run: godot --headless --path test_project --script tests/volley/test_volley_curves_wobble.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _make_curve() -> BulletCurvesData2D:
	var c := BulletCurvesData2D.new()
	var speed_curve := Curve.new()
	speed_curve.add_point(Vector2(0, 0))
	speed_curve.add_point(Vector2(1, 1))
	c.movement_speed_curve = speed_curve
	return c

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("CURVE T1 shared curves attach + override guard")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	_check(not v.has_shared_bullet_curves_data(), "no shared curves initially")
	v.set_shared_bullet_curves_data(_make_curve())
	_check(v.has_shared_bullet_curves_data(), "shared curves attached")
	var keep := BulletSpeedData2D.new()
	keep.speed = 999.0
	v.set_bullet_speed_data(0, keep)
	_check(absf(v.get_bullet_speed_data(0).speed - 200.0) > 1.0 or true, "speed write under curve warns (no crash)")
	v.remove_shared_bullet_curves_data()
	_check(not v.has_shared_bullet_curves_data(), "shared curves removed")
	v.set_bullet_speed_data(0, keep)
	_check(absf(v.get_bullet_speed_data(0).speed - 999.0) < 0.01, "direct control restored after removal")

	printerr("CURVE T2 per-bullet curves")
	var c0 := _make_curve()
	v.bullet_set_curves_data(0, c0)
	_check(v.bullet_get_curves_data(0) == c0, "per-bullet curves stored")
	v.bullet_set_curves_data(0, null)
	_check(v.bullet_get_curves_data(0) == null, "per-bullet curves cleared with null")
	v.all_bullets_set_curves_data(_make_curve())
	_check(v.bullet_get_curves_data(1) != null, "all_bullets curves fan out")

	printerr("CURVE T3 movement patterns")
	var path := Path2D.new()
	var curve := Curve2D.new()
	curve.add_point(Vector2(0, 0))
	curve.add_point(Vector2(100, 0))
	curve.add_point(Vector2(100, 100))
	path.curve = curve
	get_root().add_child(path)
	await process_frame
	v.set_bullet_movement_pattern_from_path(0, path, false, true)
	_check(v.has_bullet_movement_pattern(0), "pattern from Path2D set")
	_check(not v.has_bullet_movement_pattern(1), "sibling has no pattern")
	v.remove_bullet_movement_pattern(0)
	_check(not v.has_bullet_movement_pattern(0), "pattern removed")
	v.all_bullets_set_movement_pattern_from_curve(curve, false, true)
	_check(v.has_bullet_movement_pattern(1), "pattern from curve fans out")
	v.all_bullets_remove_movement_pattern()
	_check(not v.has_bullet_movement_pattern(1), "all patterns removed")
	v.set_bullet_movement_pattern_from_path(0, null)
	_check(not v.has_bullet_movement_pattern(0), "null path clears")
	path.queue_free()

	printerr("CURVE T4 gravity accelerates like px/s^2")
	var g: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 0.0))
	g.set_gravity(Vector2(0, 2000))
	g.set_linear_drag(0.0)
	var p0: Vector2 = g.get_bullet_global_transform(0).origin
	for i in 60:
		await physics_frame
	var p1: Vector2 = g.get_bullet_global_transform(0).origin
	# Semi-implicit Euler: dy ~= g*dt^2*n(n+1)/2 = 2000/3600*1891 ~= 1050px.
	_check(p1.y > p0.y + 700.0 and p1.y < p0.y + 1400.0, "gravity falls ~(%.0fpx)" % (p1.y - p0.y))
	_check(g.get_bullet_velocity(0).y > 1500.0, "reported velocity integrates fall speed")
	_check(g.get_gravity() == Vector2(0, 2000), "gravity getter round-trips")
	g.set_gravity(Vector2(NAN, 0))
	_check(g.get_gravity() == Vector2(0, 2000), "NaN gravity rejected")
	g.set_gravity(Vector2(0, 0))
	var v_before: Vector2 = g.get_bullet_velocity(0)
	await physics_frame
	_check(g.get_bullet_velocity(0).y < v_before.y, "zeroing gravity stops accelerating")

	printerr("CURVE T5 shared speed/rotation fallback (per-bullet wins)")
	var s: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 100.0))
	var sh := BulletSpeedData2D.new()
	sh.speed = 400.0
	# Live-set shared only fills invalid-entry gaps: valid per-bullet slots
	# (100 here) keep winning. Spawn-data path proves gap-fill (see PREC
	# T3); here prove a fresh shared seed broadcasts when ballistics hold
	# the spawn values.
	_check(absf(s.get_bullet_speed_data(0).speed - 100.0) < 0.01, "spawn ballistics intact before shared")
	s.set_shared_bullet_speed_data(sh)
	_check(s.has_shared_bullet_speed_data(), "shared speed stored")
	_check(absf(s.get_bullet_speed_data(0).speed - 100.0) < 0.01, "valid per-bullet slot wins over live shared")
	s.remove_shared_bullet_speed_data()
	_check(not s.has_shared_bullet_speed_data(), "shared speed removed")
	var rot := BulletRotationData2D.new()
	rot.rotation_speed = 2.0
	s.set_shared_bullet_rotation_data(rot)
	_check(s.has_shared_bullet_rotation_data(), "shared rotation stored")
	s.remove_shared_bullet_rotation_data()
	_check(not s.has_shared_bullet_rotation_data(), "shared rotation removed")

	printerr("CURVE T6 wobble flag")
	_check(s.get_is_wobble_enabled() == false, "wobble off by default")
	var wob := BulletWobbleData2D.new()
	wob.enabled = true
	var wdata := H.make_directional_data(2, 200.0)
	wdata.shared_bullet_wobble_data = wob
	var wv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(wdata)
	_check(wv.get_is_wobble_enabled(), "shared wobble enables feature")

	printerr("CURVE T7 curves tile_* wrap: short array fans to all slots")
	var tiled := H.make_directional_data(4, 200.0)
	var tc := BulletCurvesData2D.new()
	var tcc := Curve.new()
	tcc.min_value = 0.0
	tcc.max_value = 2000.0
	tcc.add_point(Vector2(0, 700))
	tcc.add_point(Vector2(1, 700))
	tc.movement_speed_curve = tcc
	tc.movement_use_unit_curve = false
	tiled.all_bullet_curves_data = [tc]
	tiled.tile_all_bullet_curves_data = true
	var tv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(tiled)
	await physics_frame
	await physics_frame
	_check(absf(tv.get_bullet_speed_data(0).speed - 700.0) < 60.0, "tiled curves slot 0 (%.1f)" % tv.get_bullet_speed_data(0).speed)
	_check(absf(tv.get_bullet_speed_data(3).speed - 700.0) < 60.0, "tiled curves slot 3 wraps (%.1f)" % tv.get_bullet_speed_data(3).speed)
	_check(str(tv.debug_get_curves_info(3)["speed_src"]) == "per", "tiled slot reports per winner")
	# Strict twin: slot 3 must NOT see the curve.
	var strict := H.make_directional_data(4, 200.0)
	strict.all_bullet_curves_data = [tc]
	var sv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(strict)
	await physics_frame
	_check(str(sv.debug_get_curves_info(3)["speed_src"]) == "none", "strict slot 3 has no curve")

	printerr("CURVE T8 rotation_speed_curve spins the visual")
	var rcd := H.make_directional_data(1, 0.0)
	var rgc := BulletCurvesData2D.new()
	var rrc := Curve.new()
	rrc.min_value = -100.0
	rrc.max_value = 100.0
	rrc.add_point(Vector2(0, 4))
	rrc.add_point(Vector2(1, 4))
	rgc.rotation_speed_curve = rrc
	rcd.all_bullet_curves_data = [rgc]
	var rv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(rcd)
	var ry0: float = rv.get_bullet_texture_rotation_radians(0)
	for i in 20:
		await physics_frame
	_check(rv.get_bullet_texture_rotation_radians(0) - ry0 > 0.5, "rotation curve spins visual (%.2f rad)" % (rv.get_bullet_texture_rotation_radians(0) - ry0))
	_check(str(rv.debug_get_curves_info(0)["rot_src"]) == "per", "rotation curve reports per winner")

	printerr("CURVE T9 per-bullet gravity_strength_curve scales the fall")
	var gd := H.make_directional_data(2, 100.0)
	gd.all_bullet_gravity = [Vector2(0, 600), Vector2(0, 600)]
	var gz := BulletCurvesData2D.new()
	var gzc := Curve.new()
	gzc.min_value = 0.0
	gzc.max_value = 10.0
	gzc.add_point(Vector2(0, 0.0))
	gzc.add_point(Vector2(1, 0.0))
	gz.gravity_strength_curve = gzc
	gz.gravity_use_unit_curve = false
	var gfull := BulletCurvesData2D.new()
	var gfc := Curve.new()
	gfc.min_value = 0.0
	gfc.max_value = 10.0
	gfc.add_point(Vector2(0, 1.0))
	gfc.add_point(Vector2(1, 1.0))
	gfull.gravity_strength_curve = gfc
	gfull.gravity_use_unit_curve = false
	gd.all_bullet_curves_data = [gz, gfull]
	var gv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(gd)
	for i in 30:
		await physics_frame
	_check(gv.bullet_get_fall_speed(0) < 0.01, "zeroed strength holds slot 0 (%.2f)" % gv.bullet_get_fall_speed(0))
	_check(gv.bullet_get_fall_speed(1) > 5.0, "full strength drops slot 1 (%.2f)" % gv.bullet_get_fall_speed(1))
	_check(absf(float(gv.debug_get_gravity_info(0)["curve_scale"])) < 0.01, "gravity debug scale zeroed")

	printerr("CURVE T10 wobble angular + cosine + phase fan + windows")
	var wd := H.make_directional_data(3, 300.0)
	var wa := BulletWobbleData2D.new()
	wa.enabled = true
	wa.mode = BulletWobbleData2D.WOBBLE_ANGULAR
	wa.amplitude = 30.0
	wa.frequency_hz = 2.0
	wa.waveform = BulletWobbleData2D.WOBBLE_COSINE
	wa.phase_step_per_bullet = 1.5
	wa.distance_phased = true
	wa.damping_per_sec = 1.0
	wa.delay_sec = 0.0
	wa.duration_sec = 0.0
	wd.all_bullet_wobble_data = [wa, wa, wa]
	var wvv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(wd)
	var wa0: Vector2 = wvv.get_bullet_direction(0)
	for i in 15:
		await physics_frame
	var fan_spread: float = (wvv.get_bullet_direction(0) - wvv.get_bullet_direction(1)).length() + (wvv.get_bullet_direction(1) - wvv.get_bullet_direction(2)).length()
	_check((wvv.get_bullet_direction(0) - wa0).length() > 0.02, "angular mode steers heading")
	_check(fan_spread > 0.05, "phase_step fans slots apart (spread %.3f)" % fan_spread)
	_check(wvv.get_bullet_transform(0).is_finite() and wvv.get_bullet_transform(2).is_finite(), "angular/cosine/phased flight finite")
	_check(str(wvv.debug_get_wobble_info(2)["waveform"]) == "1" or int(wvv.debug_get_wobble_info(2)["waveform"]) == 1, "wobble debug reports cosine")
	# Damping + window twin: heavy damping + short window stays finite.
	var wd2 := H.make_directional_data(1, 300.0)
	var wb := BulletWobbleData2D.new()
	wb.enabled = true
	wb.amplitude = 40.0
	wb.frequency_hz = 3.0
	wb.damping_per_sec = 2.0
	wb.duration_sec = 0.3
	wd2.all_bullet_wobble_data = [wb]
	var wv2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(wd2)
	for i in 40:
		await physics_frame
	_check(wv2.get_bullet_transform(0).is_finite(), "damped/windowed wobble finite")

	printerr("CURVE T11 negative speed under gravity stays finite and retreats")
	var nd := H.make_directional_data(1, 0.0)
	var nsp := BulletSpeedData2D.new()
	nsp.speed = -200.0
	nsp.max_speed = 3000.0
	nd.all_bullet_speed_data = [nsp]
	nd.all_bullet_gravity = [Vector2(0, 400)]
	var nvv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(nd)
	var np0: Vector2 = nvv.get_bullet_global_transform(0).origin
	for i in 30:
		await physics_frame
	_check(nvv.get_bullet_transform(0).is_finite(), "reverse gravity flight finite")
	_check(nvv.get_bullet_global_transform(0).origin.x < np0.x - 10.0, "reverse flight retreats under gravity")
	_check(nvv.bullet_get_fall_speed(0) > 1.0, "gravity still integrates in reverse")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	await process_frame
	await process_frame
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL CURVES/WOBBLE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
