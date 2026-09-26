extends SceneTree
## Debug-API suite: every new introspection/range helper proven per bullet,
## including OOB (-1/99), inverted ranges, dict shapes, and the fixed
## per-bullet-wins center precedence (per deque + shared -> center from per).
## Run: godot --headless --path test_project --script tests/volley/test_volley_debug_api.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("DBG T1 orbiting ranges + OOB")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(3, 200.0))
	v.all_bullets_enable_orbiting(60.0)
	var centers: Array = v.all_bullets_get_orbiting_center()
	_check(centers.size() == 3, "center range size 3")
	var angles: Array = v.all_bullets_get_orbiting_angle()
	_check(angles.size() == 3, "angle range size 3")
	var inv: Array = v.all_bullets_get_orbiting_center(2, 1)
	_check(inv.is_empty(), "inverted center range reads empty")
	var deep: Array = v.all_bullets_get_orbiting_angle(1, 1)
	_check(deep.size() == 1, "single-slot angle range size 1")
	var oi: Dictionary = v.debug_get_orbiting_info(0)
	_check(bool(oi["valid"]) and bool(oi["enabled"]) and str(oi["deque_src"]) == "none", "orbit info shape, no deque yet (none)")
	var oob: Dictionary = v.debug_get_orbiting_info(99)
	_check(not bool(oob["valid"]), "OOB orbit info valid=false")
	var oobn: Dictionary = v.debug_get_orbiting_info(-1)
	_check(not bool(oobn["valid"]), "negative orbit info valid=false")

	printerr("DBG T2 center precedence: per deque wins over shared")
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 0.0))
	v2.all_bullets_enable_orbiting(60.0)
	v2.bullet_homing_push_back_global_position_target(0, Vector2(-900, 0))
	v2.shared_homing_deque_push_back_global_position_target(Vector2(900, 0))
	var i0: Dictionary = v2.debug_get_orbiting_info(0)
	var i1: Dictionary = v2.debug_get_orbiting_info(1)
	_check(str(i0["deque_src"]) == "per", "slot 0 deque_src per (own deque wins)")
	_check(str(i1["deque_src"]) == "shared", "slot 1 deque_src shared (fallback)")
	var c0: Vector2 = v2.bullet_get_orbiting_center(0)
	_check(c0.distance_to(Vector2(-900, 0)) < 1.0, "center from per deque (%.1f, %.1f)" % [c0.x, c0.y])
	var c1: Vector2 = v2.bullet_get_orbiting_center(1)
	_check(c1.distance_to(Vector2(900, 0)) < 1.0, "center from shared deque (%.1f, %.1f)" % [c1.x, c1.y])

	printerr("DBG T3 homing amount range matches per-bullet reads")
	v2.bullet_homing_push_back_global_position_target(1, Vector2(10, 10))
	var amounts: Array = v2.all_bullets_get_homing_targets_amount()
	_check(int(amounts[0]) == 1 and int(amounts[1]) == 1, "amount range [1,1]")
	v2.bullet_clear_homing_targets(1)
	var amounts2: Array = v2.all_bullets_get_homing_targets_amount()
	_check(int(amounts2[0]) == 1 and int(amounts2[1]) == 0, "amount range [1,0] after clear")
	var invh: Array = v2.all_bullets_get_homing_targets_amount(0, 0)
	_check(invh.size() == 1 and int(invh[0]) == 1, "single-slot homing range size 1")

	printerr("DBG T4 curves info: none state + per/shared + OOB")
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	var n4: Dictionary = v4.debug_get_curves_info(0)
	_check(bool(n4["valid"]) and str(n4["speed_src"]) == "none" and not bool(n4["has_per_bullet_resource"]) and not bool(n4["has_shared_fallback"]), "virgin curves info all-none")
	var gx := BulletCurvesData2D.new()
	var cx := Curve.new()
	cx.add_point(Vector2(0, 0.2))
	cx.add_point(Vector2(1, 0.2))
	gx.x_direction_curve = cx
	var gy := BulletCurvesData2D.new()
	var cy := Curve.new()
	cy.add_point(Vector2(0, 0.3))
	cy.add_point(Vector2(1, 0.3))
	gy.y_direction_curve = cy
	v4.set_shared_bullet_curves_data(gx)
	v4.bullet_set_curves_data(0, gy)
	var m4: Dictionary = v4.debug_get_curves_info(0)
	_check(str(m4["x_src"]) == "shared" and str(m4["y_src"]) == "per" and bool(m4["has_per_bullet_resource"]) and bool(m4["has_shared_fallback"]), "mixed curves info x=shared y=per")
	var oobc: Dictionary = v4.debug_get_curves_info(99)
	_check(not bool(oobc["valid"]) and str(oobc["x_src"]) == "none", "OOB curves info valid=false")

	printerr("DBG T5 pattern info: none + per + shared + finished + OOB")
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	var n5: Dictionary = v5.debug_get_pattern_info(0)
	_check(bool(n5["valid"]) and str(n5["src"]) == "none" and not bool(n5["finished"]), "virgin pattern info none/unfinished")
	var pc := Curve2D.new()
	pc.add_point(Vector2(0, 0))
	pc.add_point(Vector2(400, 0))
	v5.set_shared_movement_pattern_curve(pc)
	var s5: Dictionary = v5.debug_get_pattern_info(0)
	_check(str(s5["src"]) == "shared" and bool(s5["face"]) == false and bool(s5["repeat"]) == true, "shared pattern info shape")
	v5.set_bullet_movement_pattern_from_curve(1, pc, true, true)
	var p5: Dictionary = v5.debug_get_pattern_info(1)
	_check(str(p5["src"]) == "per" and bool(p5["face"]) == true, "per pattern info face on")
	var oobp: Dictionary = v5.debug_get_pattern_info(-1)
	_check(not bool(oobp["valid"]), "OOB pattern info valid=false")

	printerr("DBG T6 wobble info full seed + curves-clear symmetry")
	var w := BulletWobbleData2D.new()
	w.enabled = true
	w.mode = BulletWobbleData2D.WOBBLE_LATERAL
	w.amplitude = 30.0
	w.frequency_hz = 2.5
	w.face_movement_direction = true
	w.face_rotation_speed = 9.0
	var v6: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	v6.bullet_set_wobble_data(0, w)
	var wi: Dictionary = v6.debug_get_wobble_info(0)
	_check(bool(wi["active"]) and absf(float(wi["amplitude"]) - 30.0) < 0.01 and absf(float(wi["frequency_hz"]) - 2.5) < 0.01, "wobble seed fields live")
	_check(int(wi["mode"]) == 0 and int(wi["waveform"]) == 0 and absf(float(wi["face_rotation_speed"]) - 9.0) < 0.01, "wobble mode/waveform/slew live")
	_check(bool(wi["has_per_bullet_resource"]) and not bool(v6.debug_get_wobble_info(1)["has_per_bullet_resource"]), "per-resource flag per slot")
	var woob: Dictionary = v6.debug_get_wobble_info(99)
	_check(not bool(woob["active"]) and not bool(woob["has_per_bullet_resource"]), "OOB wobble info inactive")
	_check(v6.has_method("clear_per_bullet_curves_data") and v6.has_method("all_bullets_clear_curves_data"), "curves-clear helpers bound")
	v6.bullet_set_wobble_data(0, null)
	_check(not bool(v6.debug_get_wobble_info(0)["active"]), "wobble null clears to inactive")

	printerr("DBG T7 attachment info needs its index (doc fix proof)")
	var a0: Dictionary = v6.debug_get_attachment_info(0)
	_check(a0.has("has_attachment") and a0.has("pooling_id") and a0.has("owner_match"), "attachment info shape intact")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL DEBUG-API TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
