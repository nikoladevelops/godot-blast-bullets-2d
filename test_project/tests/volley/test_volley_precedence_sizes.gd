extends BlastTest
## Strict per-bullet indexing + shared fallback + tile checkboxes + negative speed.
## Rule: entry i belongs to bullet i and nobody else. Each bullet resolves
## per-bullet (valid) > shared (valid) > feature default. A short array only
## covers its own indices. Each tile_* checkbox (off by default) restores
## wrap-around (slot i reads entry i % size) for that array only.



func _speed(v: float) -> BulletSpeedData2D:
	var sp := BulletSpeedData2D.new()
	sp.speed = v
	sp.max_speed = 3000.0
	sp.acceleration = 0.0
	return sp

func _rot(v: float) -> BulletRotationData2D:
	var r := BulletRotationData2D.new()
	r.rotation_speed = v
	r.max_rotation_speed = 1000.0
	r.rotation_acceleration = 0.0
	return r



func test_strict_2_speed_entries_cover_only_bullets_0_1_rest_default() -> void:
	# PREC T1 strict: 2 speed entries cover only bullets 0-1, rest default
	var d1 := H.make_directional_data(4, 0.0)
	d1.all_bullet_speed_data = [_speed(100.0), _speed(300.0)]
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d1)
	assert_true(absf(v1.get_bullet_speed_data(0).speed - 100.0) < 0.01, "slot 0 reads entry 0")
	assert_true(absf(v1.get_bullet_speed_data(1).speed - 300.0) < 0.01, "slot 1 reads entry 1")
	assert_true(absf(v1.get_bullet_speed_data(2).speed) < 0.01, "slot 2 falls back to default 0")
	assert_true(absf(v1.get_bullet_speed_data(3).speed) < 0.01, "slot 3 falls back to default 0")


func test_tile_checkbox_wraps_the_same_array() -> void:
	# PREC T1b tile checkbox wraps the same array
	var d1b := H.make_directional_data(4, 0.0)
	d1b.all_bullet_speed_data = [_speed(100.0), _speed(300.0)]
	d1b.tile_all_bullet_speed_data = true
	var v1b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d1b)
	assert_true(absf(v1b.get_bullet_speed_data(2).speed - 100.0) < 0.01, "tiled slot 2 wraps to entry 0")
	assert_true(absf(v1b.get_bullet_speed_data(3).speed - 300.0) < 0.01, "tiled slot 3 wraps to entry 1")


func test_per_bullet_speed_wins_over_shared() -> void:
	# PREC T2 per-bullet speed wins over shared
	var d2 := H.make_directional_data(3, 100.0)
	d2.all_bullet_speed_data = [_speed(111.0), _speed(222.0), _speed(333.0)]
	var sh2 := _speed(999.0)
	d2.shared_bullet_speed_data = sh2
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d2)
	assert_true(v2.has_shared_bullet_speed_data(), "shared stored")
	assert_true(absf(v2.get_bullet_speed_data(0).speed - 111.0) < 0.01, "per-bullet slot 0 wins")
	assert_true(absf(v2.get_bullet_speed_data(1).speed - 222.0) < 0.01, "per-bullet slot 1 wins")
	assert_true(absf(v2.get_bullet_speed_data(2).speed - 333.0) < 0.01, "per-bullet slot 2 wins")


func test_short_array_shared_covered_win_rest_fall_to_shared() -> void:
	# PREC T2b short array + shared: covered win, rest fall to shared
	var d2b := H.make_directional_data(3, 0.0)
	d2b.all_bullet_speed_data = [_speed(111.0)]
	d2b.shared_bullet_speed_data = _speed(999.0)
	var v2b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d2b)
	assert_true(absf(v2b.get_bullet_speed_data(0).speed - 111.0) < 0.01, "covered slot keeps per-bullet")
	assert_true(absf(v2b.get_bullet_speed_data(1).speed - 999.0) < 0.01, "uncovered slot 1 falls to shared")
	assert_true(absf(v2b.get_bullet_speed_data(2).speed - 999.0) < 0.01, "uncovered slot 2 falls to shared")


func test_shared_fills_invalid_entry_gaps_only() -> void:
	# PREC T3 shared fills invalid-entry gaps only
	var d3 := H.make_directional_data(2, 100.0)
	d3.all_bullet_speed_data = [_speed(123.0), _speed(456.0)]
	d3.shared_bullet_speed_data = _speed(777.0)
	# Second entry replaced with null post-assignment (setter path rejects
	# NaN at the resource layer, so null is the invalid-entry channel here).
	var arr3: Array = d3.all_bullet_speed_data
	arr3[1] = null
	d3.all_bullet_speed_data = arr3
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d3)
	assert_true(absf(v3.get_bullet_speed_data(0).speed - 123.0) < 0.01, "valid slot keeps per-bullet")
	assert_true(absf(v3.get_bullet_speed_data(1).speed - 777.0) < 0.01, "invalid slot falls back to shared")


func test_shared_alone_covers_every_bullet_then_clears_cleanly() -> void:
	# PREC T4 shared alone covers every bullet, then clears cleanly
	var d4 := H.make_directional_data(2, 0.0)
	d4.all_bullet_speed_data = []
	d4.shared_bullet_speed_data = _speed(400.0)
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4)
	assert_true(absf(v4.get_bullet_speed_data(0).speed - 400.0) < 0.01, "shared covers slot 0")
	assert_true(absf(v4.get_bullet_speed_data(1).speed - 400.0) < 0.01, "shared covers slot 1")
	v4.remove_shared_bullet_speed_data()
	assert_true(not v4.has_shared_bullet_speed_data(), "shared removed")
	assert_true(absf(v4.get_bullet_speed_data(0).speed - 400.0) < 0.01, "ballistics persist after clear")


func test_strict_rotation_short_array_covers_own_indices_only() -> void:
	# PREC T5 strict rotation: short array covers own indices only
	var d5 := H.make_directional_data(4, 100.0)
	d5.all_bullet_rotation_data = [_rot(2.0)]
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5)
	assert_true(v5.is_rotation_data_active(), "rotation active with partial seed")
	assert_true(absf(v5.bullet_get_rotation_speed(0) - 2.0) < 0.01, "slot 0 reads entry 0")
	assert_true(absf(v5.bullet_get_rotation_speed(1)) < 0.01, "slot 1 uncovered reads 0")
	assert_true(absf(v5.bullet_get_rotation_speed(3)) < 0.01, "slot 3 uncovered reads 0")
	var d5b := H.make_directional_data(4, 100.0)
	d5b.all_bullet_rotation_data = [_rot(1.0), _rot(5.0)]
	d5b.tile_all_bullet_rotation_data = true
	var v5b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5b)
	assert_true(absf(v5b.bullet_get_rotation_speed(0) - 1.0) < 0.01, "tiled slot 0 reads entry 0")
	assert_true(absf(v5b.bullet_get_rotation_speed(1) - 5.0) < 0.01, "tiled slot 1 reads entry 1")
	assert_true(absf(v5b.bullet_get_rotation_speed(2) - 1.0) < 0.01, "tiled slot 2 wraps to entry 0")
	assert_true(absf(v5b.bullet_get_rotation_speed(3) - 5.0) < 0.01, "tiled slot 3 wraps to entry 1")


func test_per_bullet_rotation_wins_over_shared() -> void:
	# PREC T6 per-bullet rotation wins over shared
	var d6 := H.make_directional_data(2, 100.0)
	d6.all_bullet_rotation_data = [_rot(7.0), _rot(8.0)]
	var shr := _rot(99.0)
	d6.shared_bullet_rotation_data = shr
	var v6: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d6)
	assert_true(absf(v6.bullet_get_rotation_speed(0) - 7.0) < 0.01, "per-bullet rotation slot 0 wins")
	assert_true(absf(v6.bullet_get_rotation_speed(1) - 8.0) < 0.01, "per-bullet rotation slot 1 wins")
	for i in 20:
		await physics()
	var spin6: float = absf(v6.get_bullet_texture_rotation_radians(0))
	assert_true(spin6 < 3.0, "shared fast spin did not leak into tick (%.2f rad)" % spin6)


func test_invalid_rotation_entry_fails_open_per_slot_not_whole_volley() -> void:
	# PREC T7 invalid rotation entry fails open per slot, not whole volley
	var d7 := H.make_directional_data(2, 100.0)
	d7.all_bullet_rotation_data = [_rot(25.0), _rot(25.0)]
	d7.shared_bullet_rotation_data = _rot(25.0)
	var arr7: Array = d7.all_bullet_rotation_data
	arr7[1] = null
	d7.all_bullet_rotation_data = arr7
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d7)
	expect_errors_containing("Invalid rotation data", 1, "null rotation entry fails loud")
	assert_true(v7.is_rotation_data_active(), "rotation stays active with one bad entry")
	assert_true(absf(v7.bullet_get_rotation_speed(0) - 25.0) < 0.01, "valid slot keeps per-bullet")
	assert_true(absf(v7.bullet_get_rotation_speed(1) - 25.0) < 0.01, "bad slot falls back to shared")


func test_wobble_per_bullet_wins_over_shared() -> void:
	# PREC T8 wobble per-bullet wins over shared
	var wob_shared := BulletWobbleData2D.new()
	wob_shared.enabled = true
	wob_shared.amplitude = 10.0
	var wob_per := BulletWobbleData2D.new()
	wob_per.enabled = true
	wob_per.amplitude = 40.0
	var d8 := H.make_directional_data(2, 200.0)
	d8.shared_bullet_wobble_data = wob_shared
	d8.all_bullet_wobble_data = [wob_per, wob_per]
	var v8: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d8)
	assert_true(v8.get_is_wobble_enabled(), "wobble enabled with both set")
	assert_true(absf(v8.bullet_get_wobble_amplitude(0) - 40.0) < 0.01, "per-bullet wobble wins slot 0")
	assert_true(absf(v8.bullet_get_wobble_amplitude(1) - 40.0) < 0.01, "per-bullet wobble wins slot 1")
	var d8b := H.make_directional_data(2, 200.0)
	d8b.shared_bullet_wobble_data = wob_shared
	var dis := BulletWobbleData2D.new()
	dis.enabled = false
	d8b.all_bullet_wobble_data = [dis, dis]
	var v8b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d8b)
	assert_true(v8b.get_is_wobble_enabled(), "disabled per-bullet falls back to shared")
	assert_true(absf(v8b.bullet_get_wobble_amplitude(0) - 10.0) < 0.01, "fallback amplitude slot 0")

	# PREC T8b short wobble array strict + tiled
	var d8c := H.make_directional_data(3, 200.0)
	d8c.shared_bullet_wobble_data = wob_shared
	d8c.all_bullet_wobble_data = [wob_per]
	var v8c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d8c)
	assert_true(absf(v8c.bullet_get_wobble_amplitude(0) - 40.0) < 0.01, "covered wobble slot wins")
	assert_true(absf(v8c.bullet_get_wobble_amplitude(1) - 10.0) < 0.01, "uncovered slot falls to shared")
	assert_true(absf(v8c.bullet_get_wobble_amplitude(2) - 10.0) < 0.01, "uncovered slot 2 falls to shared")
	var d8d := H.make_directional_data(3, 200.0)
	d8d.all_bullet_wobble_data = [wob_per]
	d8d.tile_all_bullet_wobble_data = true
	var v8d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d8d)
	assert_true(absf(v8d.bullet_get_wobble_amplitude(2) - 40.0) < 0.01, "tiled wobble wraps to entry 0")


func test_homing_per_bullet_deque_wins_per_bullet_shared_is_fallback() -> void:
	# PREC T9 homing per-bullet deque wins per bullet, shared is fallback
	# Convergence-style check (mirrors HOME T3): bullet 0 owns a left-side
	# target, bullet 1 must converge on the shared right-side target.
	# If shared suppressed per-bullet (old bug), bullet 0 would steer right
	# and this fails; if per-bullet leaked into bullet 1, it would steer left.
	var d9 := H.make_directional_data(2, 250.0)
	var v9: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d9)
	v9.set_homing_smoothing(5.0)
	v9.set_homing_take_control_of_texture_rotation(true)
	v9.bullet_homing_push_back_global_position_target(0, Vector2(-2000, 200))
	v9.shared_homing_deque_push_back_global_position_target(Vector2(2000, 200))
	var d0_before: float = absf(v9.get_bullet_direction(0).angle_to((Vector2(-2000, 200) - v9.get_bullet_global_transform(0).origin).normalized()))
	var d1_before: float = absf(v9.get_bullet_direction(1).angle_to((Vector2(2000, 200) - v9.get_bullet_global_transform(1).origin).normalized()))
	assert_true(v9.bullet_check_has_homing_targets(0), "bullet 0 owns a target")
	assert_true(not v9.bullet_check_has_homing_targets(1), "bullet 1 has no own target")
	for i in 30:
		await physics()
	var d0_after: float = absf(v9.get_bullet_direction(0).angle_to((Vector2(-2000, 200) - v9.get_bullet_global_transform(0).origin).normalized()))
	var d1_after: float = absf(v9.get_bullet_direction(1).angle_to((Vector2(2000, 200) - v9.get_bullet_global_transform(1).origin).normalized()))
	assert_true(d0_after < d0_before + 0.05, "bullet 0 converged on own target (%.3f -> %.3f)" % [d0_before, d0_after])
	assert_true(d1_after < d1_before + 0.05, "bullet 1 converged on shared target (%.3f -> %.3f)" % [d1_before, d1_after])


func test_per_bullet_smoothing_wins_shared_stays_for_others() -> void:
	# PREC T10 per-bullet smoothing wins, shared stays for others
	var d10 := H.make_directional_data(2, 200.0)
	var v10: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d10)
	v10.set_homing_smoothing(5.0)
	v10.bullet_set_homing_smoothing(0, 0.0)
	assert_true(absf(v10.bullet_get_homing_smoothing(0) - 0.0) < 0.001, "per-bullet smoothing read slot 0")
	assert_true(absf(v10.bullet_get_homing_smoothing(1) - 5.0) < 0.001, "shared smoothing read slot 1")


func test_collision_counts_strict_tiled_clamp() -> void:
	# PREC T11 collision counts strict + tiled + clamp
	var d11 := H.make_directional_data(4, 100.0)
	d11.bullet_max_collision_count = 3
	d11.bullets_current_collision_count = [1, 2]
	var v11: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d11)
	var cc: Array = v11.get_bullets_current_collision_count()
	assert_true(int(cc[0]) == 1 and int(cc[1]) == 2 and int(cc[2]) == 0 and int(cc[3]) == 0, "strict: covered keep, rest start at 0")
	var d11t := H.make_directional_data(4, 100.0)
	d11t.bullet_max_collision_count = 3
	d11t.bullets_current_collision_count = [1, 2]
	d11t.tile_bullets_current_collision_count = true
	var v11t: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d11t)
	var cct: Array = v11t.get_bullets_current_collision_count()
	assert_true(int(cct[0]) == 1 and int(cct[1]) == 2 and int(cct[2]) == 1 and int(cct[3]) == 2, "tiled: counts wrap modulo")
	var d11b := H.make_directional_data(2, 100.0)
	d11b.bullet_max_collision_count = 3
	d11b.bullets_current_collision_count = [-5, 99]
	var v11b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d11b)
	var cc2: Array = v11b.get_bullets_current_collision_count()
	assert_true(int(cc2[0]) == 0 and int(cc2[1]) == 2, "counts clamp (neg->0, over->max-1)")


func test_custom_data_strict_tiled_never_falls_back_to_shared() -> void:
	# PREC T12 custom data strict + tiled, never falls back to shared
	var d12 := H.make_directional_data(3, 100.0)
	var r1 := Resource.new()
	var r2 := Resource.new()
	d12.all_bullets_custom_data = [r1, r2]
	var shared_res := Resource.new()
	d12.shared_bullets_custom_data = shared_res
	var v12: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d12)
	assert_true(v12.bullet_get_custom_data(0) == r1, "custom slot 0")
	assert_true(v12.bullet_get_custom_data(1) == r2, "custom slot 1")
	assert_true(v12.bullet_get_custom_data(2) == null, "custom slot 2 uncovered reads null, never shared")
	var d12t := H.make_directional_data(3, 100.0)
	d12t.all_bullets_custom_data = [r1, r2]
	d12t.tile_all_bullets_custom_data = true
	var v12t: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d12t)
	assert_true(v12t.bullet_get_custom_data(2) == r1, "tiled custom slot 2 wraps to entry 0")
	var d12b := H.make_directional_data(1, 100.0)
	d12b.shared_bullets_custom_data = shared_res
	var v12b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d12b)
	assert_true(v12b.bullet_get_custom_data(0) == null, "no per-bullet value reads null, never shared")


func test_negative_speed_flies_backwards_symmetric_max_clamp() -> void:
	# PREC T13 negative speed flies backwards, symmetric max clamp
	var d13 := H.make_directional_data(2, 0.0)
	var neg := _speed(-250.0)
	neg.max_speed = 1000.0
	var pos := _speed(250.0)
	d13.all_bullet_speed_data = [neg, pos]
	var v13: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d13)
	assert_true(absf(v13.get_bullet_speed_data(0).speed + 250.0) < 0.01, "negative speed seeds")
	var p0: Vector2 = v13.get_bullet_global_transform(0).origin
	var p1: Vector2 = v13.get_bullet_global_transform(1).origin
	for i in 20:
		await physics()
	var q0: Vector2 = v13.get_bullet_global_transform(0).origin
	var q1: Vector2 = v13.get_bullet_global_transform(1).origin
	assert_true(q0.x < p0.x - 20.0, "negative-speed bullet retreats (%.1f px)" % (p0.x - q0.x))
	assert_true(q1.x > p1.x + 20.0, "positive-speed bullet advances")
	assert_true(v13.get_bullet_transform(0).is_finite() and v13.get_bullet_transform(1).is_finite(), "reverse flight stays finite")
	assert_true(absf(v13.get_bullet_speed_data(0).speed + 250.0) < 5.0, "reverse speed survives accel clamp (%.1f)" % v13.get_bullet_speed_data(0).speed)


func test_negative_curve_sample_flies_backwards() -> void:
	# PREC T14 negative curve sample flies backwards
	var d14 := H.make_directional_data(1, 200.0)
	var gc := BulletCurvesData2D.new()
	var ramp := Curve.new()
	# NOTE: Curve clamps point values to [min_value, max_value] (default
	# 0..1), so the range must be widened BEFORE adding negative points —
	# adding them first silently clamps to 0. Same rule as the editor.
	ramp.min_value = -500.0
	ramp.max_value = 500.0
	ramp.add_point(Vector2(0, -300))
	ramp.add_point(Vector2(1, -300))
	gc.movement_speed_curve = ramp
	gc.movement_use_unit_curve = false
	d14.shared_bullet_curves_data = gc
	var v14: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d14)
	var r0: Vector2 = v14.get_bullet_global_transform(0).origin
	for i in 20:
		await physics()
	var r1p: Vector2 = v14.get_bullet_global_transform(0).origin
	assert_true(r1p.x < r0.x - 10.0, "negative curve sample retreats")
	assert_true(v14.get_bullet_transform(0).is_finite(), "curve reverse flight finite")

	assert_true(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	await idle(1)
