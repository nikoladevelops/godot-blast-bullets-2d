extends BlastTest
## Introspection helpers per bullet: orbiting ranges + info (incl. OOB and
## inverted ranges), per-deque-wins orbit center, homing amount ranges,
## curves / pattern / wobble info shapes and sources, attachment info shape.


func test_orbiting_ranges_and_oob() -> void:
	var v: BulletVolley2D = quick_volley(3, 200.0)
	v.all_bullets_enable_orbiting(60.0)
	assert_eq(v.all_bullets_get_orbiting_center().size(), 3, "center range size 3")
	assert_eq(v.all_bullets_get_orbiting_angle().size(), 3, "angle range size 3")
	assert_true(v.all_bullets_get_orbiting_center(2, 1).is_empty(), "inverted range reads empty")
	expect_error_sequence(["Invalid index range in all_bullets_get_orbiting_center (start > end). Nothing was applied."])
	assert_eq(v.all_bullets_get_orbiting_angle(1, 1).size(), 1, "single-slot range")
	var oi: Dictionary = v.debug_get_orbiting_info(0)
	assert_true(oi["valid"] and oi["enabled"], "orbit info valid + enabled")
	assert_eq(str(oi["deque_src"]), "none", "no deque yet")
	assert_false(v.debug_get_orbiting_info(99)["valid"], "OOB invalid")
	assert_false(v.debug_get_orbiting_info(-1)["valid"], "negative invalid")


func test_center_precedence_and_homing_amounts() -> void:
	var v: BulletVolley2D = quick_volley(2, 0.0)
	v.all_bullets_enable_orbiting(60.0)
	v.bullet_homing_push_back_global_position_target(0, Vector2(-900, 0))
	v.shared_homing_deque_push_back_global_position_target(Vector2(900, 0))
	assert_eq(str(v.debug_get_orbiting_info(0)["deque_src"]), "per", "own deque wins")
	assert_eq(str(v.debug_get_orbiting_info(1)["deque_src"]), "shared", "shared fallback")
	assert_almost_eq(v.bullet_get_orbiting_center(0), Vector2(-900, 0), Vector2(1, 1), "center from the per deque")
	assert_almost_eq(v.bullet_get_orbiting_center(1), Vector2(900, 0), Vector2(1, 1), "center from the shared deque")
	v.bullet_homing_push_back_global_position_target(1, Vector2(10, 10))
	assert_eq(Array(v.all_bullets_get_homing_targets_amount()), [1, 1], "amount range [1, 1]")
	v.bullet_clear_homing_targets(1)
	assert_eq(Array(v.all_bullets_get_homing_targets_amount()), [1, 0], "amount range [1, 0] after clear")
	assert_eq(Array(v.all_bullets_get_homing_targets_amount(0, 0)), [1], "single-slot range")


func test_curves_info_sources() -> void:
	var v: BulletVolley2D = quick_volley(2, 200.0)
	var n: Dictionary = v.debug_get_curves_info(0)
	assert_true(n["valid"])
	assert_eq(str(n["speed_src"]), "none")
	assert_false(n["has_per_bullet_resource"])
	assert_false(n["has_shared_fallback"])
	var gx := BulletCurvesData2D.new()
	gx.x_direction_curve = H.make_flat_curve(0.2)
	var gy := BulletCurvesData2D.new()
	gy.y_direction_curve = H.make_flat_curve(0.3)
	v.set_shared_bullet_curves_data(gx)
	v.bullet_set_curves_data(0, gy)
	var m: Dictionary = v.debug_get_curves_info(0)
	assert_eq(str(m["x_src"]), "shared", "x from shared")
	assert_eq(str(m["y_src"]), "per", "y from per-bullet")
	assert_true(m["has_per_bullet_resource"] and m["has_shared_fallback"])
	var oob: Dictionary = v.debug_get_curves_info(99)
	assert_false(oob["valid"])
	assert_eq(str(oob["x_src"]), "none")


func test_pattern_info_sources() -> void:
	var v: BulletVolley2D = quick_volley(2, 200.0)
	var n: Dictionary = v.debug_get_pattern_info(0)
	assert_true(n["valid"])
	assert_eq(str(n["src"]), "none")
	assert_false(n["finished"])
	var pc := Curve2D.new()
	pc.add_point(Vector2(0, 0))
	pc.add_point(Vector2(400, 0))
	v.set_shared_movement_pattern_curve(pc)
	var s: Dictionary = v.debug_get_pattern_info(0)
	assert_eq(str(s["src"]), "shared")
	assert_false(s["face"])
	assert_true(s["repeat"])
	v.set_bullet_movement_pattern_from_curve(1, pc, true, true)
	var p: Dictionary = v.debug_get_pattern_info(1)
	assert_eq(str(p["src"]), "per")
	assert_true(p["face"])
	assert_false(v.debug_get_pattern_info(-1)["valid"], "OOB invalid")


func test_wobble_info_and_attachment_info() -> void:
	var w := BulletWobbleData2D.new()
	w.enabled = true
	w.mode = BulletWobbleData2D.WOBBLE_LATERAL
	w.amplitude = 30.0
	w.frequency_hz = 2.5
	w.face_movement_direction = true
	w.face_rotation_speed = 9.0
	var v: BulletVolley2D = quick_volley(2, 200.0)
	v.bullet_set_wobble_data(0, w)
	var wi: Dictionary = v.debug_get_wobble_info(0)
	assert_true(wi["active"])
	assert_almost_eq(float(wi["amplitude"]), 30.0, 0.01)
	assert_almost_eq(float(wi["frequency_hz"]), 2.5, 0.01)
	assert_eq(int(wi["mode"]), 0)
	assert_eq(int(wi["waveform"]), 0)
	assert_almost_eq(float(wi["face_rotation_speed"]), 9.0, 0.01)
	assert_true(wi["has_per_bullet_resource"])
	assert_false(v.debug_get_wobble_info(1)["has_per_bullet_resource"], "per-resource flag per slot")
	var woob: Dictionary = v.debug_get_wobble_info(99)
	assert_false(woob["active"] or woob["has_per_bullet_resource"], "OOB inactive")
	v.bullet_set_wobble_data(0, null)
	assert_false(v.debug_get_wobble_info(0)["active"], "null clears to inactive")
	var a0: Dictionary = v.debug_get_attachment_info(0)
	for key in ["has_attachment", "pooling_id", "owner_match"]:
		assert_has(a0, key)


func test_curves_clear_helpers_bound_and_work() -> void:
	var v: BulletVolley2D = quick_volley(3, 200.0)
	assert_true(v.has_method("clear_per_bullet_curves_data") and v.has_method("all_bullets_clear_curves_data"), "curves-clear helpers bound")
	var block := BulletCurvesData2D.new()
	v.bullet_set_curves_data(0, block)
	v.bullet_set_curves_data(1, block)
	v.all_bullets_clear_curves_data(0, 0)
	assert_true(v.bullet_get_curves_data(0) == null, "range clear drops slot 0")
	assert_true(v.bullet_get_curves_data(1) != null, "range clear spares slot 1")
	v.clear_per_bullet_curves_data(1)
	assert_true(v.bullet_get_curves_data(1) == null, "per-bullet clear drops slot 1")
	expect_errors_containing("has no individual curves data", 2, "cleared slots read loud")
