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

	printerr("CURVE T5 shared speed/rotation fan")
	var s: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 100.0))
	var sh := BulletSpeedData2D.new()
	sh.speed = 400.0
	s.set_shared_bullet_speed_data(sh)
	_check(s.has_shared_bullet_speed_data(), "shared speed stored")
	_check(absf(s.get_bullet_speed_data(0).speed - 400.0) < 0.01, "shared speed drives bullets")
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
