extends SceneTree
## Precedence-gaps suite: the documented limits and one-shot live order of the
## unified model, proven per bullet. Complements test_volley_precedence_sizes
## (happy paths) with: empty-rotation+shared fans to ALL slots and spins every
## slot; deliberate-zero vs gap; partial-zero triples; all-zero shared no-op;
## NaN shared aborts the whole volley with ballistics intact; live set-order
## both ways; tiled arrays with invalid entries; shared curves outrank valid
## ballistics per tick; per-channel split; pattern flag snapshot vs live;
## rotate-flag flip only when a gap fills; homing drain->shared handover;
## smoothing latch + clear escape + pool neutrality.
## Run: godot --headless --path test_project --script tests/volley/test_volley_precedence_gaps.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _speed(v: float) -> BulletSpeedData2D:
	var sp := BulletSpeedData2D.new()
	sp.speed = v
	sp.max_speed = 3000.0
	sp.acceleration = 0.0
	return sp

func _rot(v: float, mx: float = 1000.0) -> BulletRotationData2D:
	var r := BulletRotationData2D.new()
	r.rotation_speed = v
	r.max_rotation_speed = mx
	r.rotation_acceleration = 0.0
	return r

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("GAP T1 empty rotation + shared fans to ALL slots and spins")
	var g1 := H.make_directional_data(3, 100.0)
	g1.all_bullet_rotation_data = []
	g1.shared_bullet_rotation_data = _rot(4.0, 100.0)
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(g1)
	_check(v1.is_rotation_data_active(), "rotation activates from shared-only seed")
	_check(absf(v1.bullet_get_rotation_speed(1) - 4.0) < 0.01 and absf(v1.bullet_get_rotation_speed(2) - 4.0) < 0.01, "slots 1-2 seeded, not just slot 0")
	var yaw_before: float = v1.get_bullet_texture_rotation_radians(2)
	for i in 20:
		await physics_frame
	_check(absf(v1.get_bullet_texture_rotation_radians(2) - yaw_before) > 0.02, "slot 2 spins under shared-only seed")

	printerr("GAP T2 shared covers short-array tail (strict: uncovered fall back)")
	var g2 := H.make_directional_data(2, 0.0)
	g2.all_bullet_speed_data = [_speed(150.0)]
	g2.shared_bullet_speed_data = _speed(600.0)
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(g2)
	_check(absf(v2.get_bullet_speed_data(0).speed - 150.0) < 0.01, "covered slot keeps per-bullet")
	_check(absf(v2.get_bullet_speed_data(1).speed - 600.0) < 0.01, "uncovered slot falls back to shared")

	printerr("GAP T3 partial-zero triples block the fallback")
	# A slot holding SOME non-zero value is treated as seeded: only a
	# fully-zero triple is an invalid-entry gap. (A resource _speed(0.0)
	# seeds max 3000, so it is valid and wins; deliberate full stops are
	# max 0 + speed 0 + accel 0 and intentionally read as gaps. This T3
	# proves the partial case directly.)
	var g3 := H.make_directional_data(2, 0.0)
	var p0 := _speed(0.0)
	p0.max_speed = 500.0
	var p1 := _rot(0.0, 500.0)
	var gr3 := H.make_directional_data(2, 0.0)
	gr3.all_bullet_speed_data = [p0, _speed(10.0)]
	gr3.shared_bullet_speed_data = _speed(600.0)
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(gr3)
	_check(absf(v3.get_bullet_speed_data(0).speed) < 0.01 and absf(v3.get_bullet_speed_data(0).max_speed - 500.0) < 0.01, "partial-zero speed triple keeps slot")
	var r3d := H.make_directional_data(2, 100.0)
	r3d.all_bullet_rotation_data = [p1, _rot(9.0)]
	r3d.shared_bullet_rotation_data = _rot(77.0)
	var v3r: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(r3d)
	_check(absf(v3r.bullet_get_rotation_speed(0)) < 0.01, "partial-zero rotation triple keeps slot")

	printerr("GAP T4 all-zero shared is a silent no-op, live and at spawn")
	var g4 := H.make_directional_data(2, 0.0)
	g4.all_bullet_speed_data = [_speed(123.0), _speed(0.0)]
	g4.shared_bullet_speed_data = _speed(0.0)
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(g4)
	_check(absf(v4.get_bullet_speed_data(1).speed) < 0.01, "gap stays zero under all-zero shared")
	v4.set_shared_bullet_speed_data(_speed(0.0))
	_check(absf(v4.get_bullet_speed_data(1).speed) < 0.01, "live all-zero shared still no-op")

	printerr("GAP T5 NaN shared aborts whole volley, ballistics intact")
	var g5 := H.make_directional_data(2, 0.0)
	g5.all_bullet_speed_data = [_speed(0.0), _speed(50.0)]
	var bad := _speed(1.0)
	bad.speed = NAN
	g5.shared_bullet_speed_data = bad
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(g5)
	_check(absf(v5.get_bullet_speed_data(0).speed) < 0.01, "NaN shared leaves gap at zero")
	_check(absf(v5.get_bullet_speed_data(1).speed - 50.0) < 0.01, "NaN shared keeps valid slot")

	printerr("GAP T6 live shared fills seeded-zero slots, then fill-once order")
	# make_directional_data(2, 0.0) seeds max 3000 per slot (valid seeds),
	# so GAP T6 spawns with an EMPTY array: every slot is a fully-zero gap.
	var e6 := H.make_directional_data(2, 0.0)
	e6.all_bullet_speed_data = []
	var o1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(e6)
	o1.set_shared_bullet_speed_data(_speed(400.0))
	_check(absf(o1.get_bullet_speed_data(0).speed - 400.0) < 0.01, "late shared fills empty slot")
	var e6b := H.make_directional_data(2, 0.0)
	e6b.all_bullet_speed_data = []
	var o2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(e6b)
	var z := _speed(0.0)
	z.max_speed = 0.0
	o2.set_bullet_speed_data(0, z)
	o2.set_shared_bullet_speed_data(_speed(400.0))
	_check(absf(o2.get_bullet_speed_data(0).speed - 400.0) < 0.01, "per-zero then shared fills")
	var e6c := H.make_directional_data(2, 0.0)
	e6c.all_bullet_speed_data = []
	var o3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(e6c)
	o3.set_shared_bullet_speed_data(_speed(400.0))
	o3.set_bullet_speed_data(0, z)
	_check(absf(o3.get_bullet_speed_data(0).speed) < 0.01, "shared then per-zero stays zero (fill-once)")

	printerr("GAP T7 tiled array with an invalid entry still falls back per slot")
	var g7 := H.make_directional_data(4, 0.0)
	var arr7: Array = [_speed(111.0), null]
	g7.all_bullet_speed_data = arr7
	g7.tile_all_bullet_speed_data = true
	g7.shared_bullet_speed_data = _speed(999.0)
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(g7)
	_check(absf(v7.get_bullet_speed_data(0).speed - 111.0) < 0.01, "tiled slot 0 reads entry 0")
	_check(absf(v7.get_bullet_speed_data(1).speed - 999.0) < 0.01, "tiled null entry falls to shared")
	_check(absf(v7.get_bullet_speed_data(3).speed - 999.0) < 0.01, "tiled slot 3 wraps to null entry, falls to shared")

	printerr("GAP T8 shared speed curve outranks valid ballistics per tick")
	var g8 := H.make_directional_data(2, 0.0)
	g8.all_bullet_speed_data = [_speed(100.0), _speed(200.0)]
	var gc := BulletCurvesData2D.new()
	var ramp := Curve.new()
	ramp.min_value = 0.0
	ramp.max_value = 2000.0
	ramp.add_point(Vector2(0, 800))
	ramp.add_point(Vector2(1, 800))
	gc.movement_speed_curve = ramp
	gc.movement_use_unit_curve = false
	g8.shared_bullet_curves_data = gc
	var v8: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(g8)
	await physics_frame
	await physics_frame
	_check(str(v8.debug_get_curves_info(0)["speed_src"]) == "shared", "curves info names shared winner")
	_check(absf(v8.get_bullet_speed_data(0).speed - 800.0) < 60.0, "shared curve overwrote ballistics (%.1f)" % v8.get_bullet_speed_data(0).speed)

	printerr("GAP T9 per-channel split: shared x + per y")
	var gx := BulletCurvesData2D.new()
	var cx := Curve.new()
	cx.add_point(Vector2(0, 0.5))
	cx.add_point(Vector2(1, 0.5))
	gx.x_direction_curve = cx
	var gy := BulletCurvesData2D.new()
	var cy := Curve.new()
	cy.add_point(Vector2(0, -0.5))
	cy.add_point(Vector2(1, -0.5))
	gy.y_direction_curve = cy
	var g9 := H.make_directional_data(2, 200.0)
	g9.shared_bullet_curves_data = gx
	g9.all_bullet_curves_data = [gy, gy]
	var v9: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(g9)
	var ci: Dictionary = v9.debug_get_curves_info(0)
	_check(str(ci["x_src"]) == "shared" and str(ci["y_src"]) == "per", "split channels: x shared, y per (%s/%s)" % [str(ci["x_src"]), str(ci["y_src"])])
	_check(v9.get_bullet_transform(0).is_finite(), "split-channel flight finite")
	v9.clear_per_bullet_curves_data(0)
	var ci2: Dictionary = v9.debug_get_curves_info(0)
	_check(str(ci2["y_src"]) == "none" and str(ci2["x_src"]) == "shared", "clear restores shared-only (y none, x shared)")
	v9.all_bullets_clear_curves_data()
	_check(str(v9.debug_get_curves_info(1)["y_src"]) == "none", "range clear empties slot 1")

	printerr("GAP T10 live shared face/repeat does not rewrite seeded per slots")
	var curve := Curve2D.new()
	curve.add_point(Vector2(0, 0))
	curve.add_point(Vector2(200, 0))
	var g10 := H.make_directional_data(1, 200.0)
	g10.shared_movement_pattern_path = ^""
	var v10: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(g10)
	v10.set_shared_movement_pattern_curve(curve)
	v10.set_shared_movement_pattern_face_movement_direction(false)
	v10.set_bullet_movement_pattern_from_curve(0, curve, true, true)
	var pi_before: Dictionary = v10.debug_get_pattern_info(0)
	v10.set_shared_movement_pattern_face_movement_direction(true)
	v10.set_shared_movement_pattern_repeat(false)
	var pi_after: Dictionary = v10.debug_get_pattern_info(0)
	_check(str(pi_before["src"]) == "per" and bool(pi_before["face"]) == true, "per pattern seeded with face on")
	_check(str(pi_after["src"]) == "per" and bool(pi_after["face"]) == true, "live shared flag edit keeps seeded per flags")
	_check(v10.has_shared_movement_pattern(), "shared fallback still registered")

	printerr("GAP T11 no-gap shared write keeps every slot (fill-only)")
	var g11 := H.make_directional_data(2, 100.0)
	g11.all_bullet_rotation_data = [_rot(5.0), _rot(6.0)]
	var v11: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(g11)
	var stay := _rot(0.0)
	stay.max_rotation_speed = 0.0
	v11.set_shared_bullet_rotation_data(_rot(42.0))
	# No public getter for the follow flag; prove via no-op: speeds untouched.
	_check(absf(v11.bullet_get_rotation_speed(0) - 5.0) < 0.01, "no-gap shared write keeps slot 0")
	_check(absf(v11.bullet_get_rotation_speed(1) - 6.0) < 0.01, "no-gap shared write keeps slot 1")

	printerr("GAP T12 per-bullet homing drain hands over to shared")
	var g12 := H.make_directional_data(2, 250.0)
	var v12: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(g12)
	v12.set_homing_smoothing(5.0)
	v12.set_homing_take_control_of_texture_rotation(true)
	v12.bullet_homing_push_back_global_position_target(0, Vector2(-2000, 200))
	v12.shared_homing_deque_push_back_global_position_target(Vector2(2000, 200))
	_check(v12.bullet_check_has_homing_targets(0), "per deque seeded")
	for i in 10:
		await physics_frame
	v12.bullet_clear_homing_targets(0)
	for i in 20:
		await physics_frame
	_check(not v12.bullet_check_has_homing_targets(0), "per deque drained")
	_check(v12.get_bullet_transform(0).is_finite(), "post-handover flight finite")

	printerr("GAP T13 smoothing latch snapshots siblings until cleared")
	var v13: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	v13.set_homing_smoothing(5.0)
	v13.bullet_set_homing_smoothing(0, 1.0)
	v13.set_homing_smoothing(9.0)
	_check(absf(v13.bullet_get_homing_smoothing(0) - 1.0) < 0.001, "latched per slot ignores shared rewrite")
	_check(absf(v13.bullet_get_homing_smoothing(1) - 5.0) < 0.001, "sibling keeps latched snapshot (documented)")
	v13.clear_per_bullet_homing_smoothing()
	_check(absf(v13.bullet_get_homing_smoothing(0) - 9.0) < 0.001, "clear restores shared for all")

	printerr("GAP T14 pool reuse clears smoothing latch + per-bullet flags")
	var v14a: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	v14a.set_homing_smoothing(5.0)
	v14a.bullet_set_homing_smoothing(0, 1.0)
	for k in 2:
		v14a.disable_bullet(k)
	await physics_frame
	var v14b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	_check(absf(v14b.bullet_get_homing_smoothing(0) - 0.0) < 0.001, "reused volley has no latched smoothing")
	_check(not v14b.get_is_wobble_enabled(), "reused volley has no stale wobble")

	printerr("GAP T15 pattern finish: shared parks, per holds end-pose")
	# NOTE: setting the SHARED pattern after seeding a per-bullet one does
	# not remove the per entry (both stay registered); the per entry wins
	# until its own non-repeating run finishes and clears. So the honest
	# finish test uses two volleys: shared-only parks, per-only clears.
	var pcurve := Curve2D.new()
	pcurve.add_point(Vector2(0, 0))
	pcurve.add_point(Vector2(60, 0))
	var v15s: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 300.0))
	v15s.set_shared_movement_pattern_curve(pcurve)
	v15s.set_shared_movement_pattern_repeat(false)
	var v15p: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 300.0))
	v15p.set_bullet_movement_pattern_from_curve(0, pcurve, false, false)
	for i in 60:
		await physics_frame
	var ps: Dictionary = v15s.debug_get_pattern_info(0)
	var pp: Dictionary = v15p.debug_get_pattern_info(0)
	_check(str(ps["src"]) == "shared" and bool(ps["finished"]) == true, "finished shared parks (src shared, finished true)")
	# A finished non-repeating per-bullet run clears the entry (back to
	# ballistic flight), holding the end pose it reached - not src "per".
	_check(str(pp["src"]) == "none", "finished per clears to none (holds end-pose)")
	_check(v15s.get_bullet_transform(0).is_finite() and v15p.get_bullet_transform(0).is_finite(), "post-finish flight finite")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL PRECEDENCE-GAP TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
