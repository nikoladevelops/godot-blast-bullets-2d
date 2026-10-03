extends BlastTest
## Per-bullet rotation API (get/set/range/clear) crossed with shared fallback,
## tick spin, clamps, stop flag, rotate_only_textures, adjust steering, pool
## reuse and hostile input. Mirrors the speed-data API contract.


func _rot(spd: float, mx: float = 1000.0, acc: float = 0.0) -> BulletRotationData2D:
	return H.make_rotation(spd, mx, acc)


func _yaw(v: BulletVolley2D, i := 0) -> float:
	return v.get_bullet_texture_rotation_radians(i)


func test_round_trip_range_and_oob() -> void:
	var d := H.make_volley_data(3, 100.0)
	d.all_bullet_rotation_data = [_rot(2.0), _rot(4.0), _rot(6.0)]
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_almost_eq(v.get_bullet_rotation_data(1).rotation_speed, 4.0, 0.01, "get reads live triple")
	v.set_bullet_rotation_data(1, _rot(40.0))
	assert_almost_eq(v.get_bullet_rotation_data(1).rotation_speed, 40.0, 0.01, "set writes live triple")
	assert_almost_eq(v.get_bullet_rotation_data(0).rotation_speed, 2.0, 0.01, "sibling untouched")
	assert_almost_eq(v.bullet_get_rotation_speed(1), 40.0, 0.01, "speed getter agrees with triple")
	v.all_bullets_set_rotation_data(_rot(11.0), 0, 1)
	assert_almost_eq(v.get_bullet_rotation_data(0).rotation_speed, 11.0, 0.01, "range fan slot 0")
	assert_almost_eq(v.get_bullet_rotation_data(1).rotation_speed, 11.0, 0.01, "range fan slot 1")
	assert_almost_eq(v.get_bullet_rotation_data(2).rotation_speed, 6.0, 0.01, "range spares slot 2")
	assert_eq(v.all_bullets_get_rotation_data().size(), 3, "range get size")
	v.set_bullet_rotation_data(-1, _rot(1.0))
	v.set_bullet_rotation_data(99, _rot(1.0))
	v.set_bullet_rotation_data(0, null)
	expect_any_error("OOB / null writes fail loud", 3)
	assert_almost_eq(v.get_bullet_rotation_data(0).rotation_speed, 11.0, 0.01, "null write rejected")
	var oob: BulletRotationData2D = v.get_bullet_rotation_data(99)
	expect_any_error("OOB read fails loud")
	assert_almost_eq(oob.rotation_speed, 0.0, 0.01, "OOB read returns a zeroed resource")


func test_live_writes_spin_and_clear_holds() -> void:
	var v: BulletVolley2D = quick_volley(2, 100.0)
	assert_false(v.is_rotation_data_active(), "rotation off fresh")
	v.set_bullet_rotation_data(0, _rot(30.0))
	v.set_bullet_rotation_data(1, _rot(30.0))
	assert_true(v.is_rotation_data_active(), "live write activates rotation")
	await physics(20)
	assert_gt(absf(_yaw(v)), 1.0, "live-seeded spin advances texture")
	v.clear_bullet_rotation_data()
	assert_false(v.is_rotation_data_active(), "clear turns rotation off")
	var frozen := _yaw(v)
	await physics(10)
	assert_almost_eq(_yaw(v), frozen, 0.01, "cleared volley holds yaw")


func test_shared_fallback_precedence() -> void:
	var d := H.make_volley_data(2, 100.0)
	d.all_bullet_rotation_data = [_rot(5.0), _rot(5.0)]
	d.shared_bullet_rotation_data = _rot(50.0)
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_almost_eq(v.get_bullet_rotation_data(0).rotation_speed, 5.0, 0.01, "per-bullet wins at seed")
	v.set_bullet_rotation_data(0, _rot(7.0))
	assert_almost_eq(v.get_bullet_rotation_data(0).rotation_speed, 7.0, 0.01, "live write wins over shared")
	v.set_shared_bullet_rotation_data(_rot(60.0))
	assert_almost_eq(v.get_bullet_rotation_data(0).rotation_speed, 7.0, 0.01, "shared reseed spares the live slot")


func test_accel_clamps_at_max() -> void:
	var v: BulletVolley2D = quick_volley(1, 0.0)
	v.set_bullet_rotation_data(0, _rot(0.0, 2.0, 100.0))
	await physics(30)
	assert_lte(absf(float(v.debug_get_bullet_info(0).get("rotation_speed", 99.0))), 2.01, "accel clamps at max")


func test_negative_spin_clamps_at_minus_max() -> void:
	var d := H.make_volley_data(2, 0.0)
	d.all_bullet_rotation_data = [_rot(-3.0, 5.0), _rot(-50.0, 5.0)]
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_almost_eq(v.bullet_get_rotation_speed(0), -3.0, 0.01, "negative speed seeds")
	var y0 := _yaw(v)
	await physics(20)
	assert_lt(_yaw(v) - y0, -0.2, "negative spin yaws backwards")
	assert_almost_eq(v.bullet_get_rotation_speed(1), -5.0, 0.01, "negative overspeed clamps at -max")
	assert_true(v.get_bullet_transform(0).is_finite() and v.get_bullet_transform(1).is_finite(), "reverse spin finite")


func test_stop_flag_at_max() -> void:
	for stop in [true, false]:
		var d := H.make_volley_data(1, 0.0)
		d.stop_rotation_when_max_reached = stop
		d.all_bullet_rotation_data = [_rot(5.0, 5.0)]
		var v: BulletVolley2D = factory.spawn_volley(d)
		var y0 := _yaw(v)
		await physics(20)
		var moved := absf(_yaw(v) - y0)
		if stop:
			assert_lt(moved, 0.01, "stop=true holds yaw at max")
		else:
			assert_gt(moved, 0.2, "stop=false spins through max")


func test_rotate_only_textures_spins_either_way() -> void:
	for rot_only in [true, false]:
		var d := H.make_volley_data(1, 0.0)
		d.rotate_only_textures = rot_only
		d.all_bullet_rotation_data = [_rot(4.0, 100.0)]
		var v: BulletVolley2D = factory.spawn_volley(d)
		var y0 := _yaw(v)
		await physics(10)
		assert_gt(absf(_yaw(v) - y0), 0.05, "rotate_only=%s instance spins" % rot_only)


func test_adjust_flag_steering() -> void:
	var d := H.make_volley_data(1, 200.0)
	d.adjust_direction_based_on_rotation = true
	d.rotate_only_textures = false
	d.all_bullet_rotation_data = [_rot(6.0, 100.0)]
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.set_bullet_texture_rotation_radians(0, 1.0)
	var dir0: Vector2 = v.get_bullet_direction(0)
	await physics(30)
	assert_gt((v.get_bullet_direction(0) - dir0).length(), 0.5, "adjust=true steers direction from spin")
	var dd := H.make_volley_data(1, 200.0)
	dd.adjust_direction_based_on_rotation = true
	dd.rotate_only_textures = true
	var vd: BulletVolley2D = factory.spawn_volley(dd)
	vd.set_bullet_texture_rotation_radians(0, 1.0)
	var dir0d: Vector2 = vd.get_bullet_direction(0)
	await physics(30)
	assert_lt((vd.get_bullet_direction(0) - dir0d).length(), 0.01, "rotate_only=true skips adjust steering")


func test_live_order_remove_and_getters() -> void:
	var z := _rot(0.0, 0.0)
	var sh9 := _rot(9.0, 100.0)
	var e1 := H.make_volley_data(2, 100.0)
	e1.all_bullet_rotation_data = []
	var o1: BulletVolley2D = factory.spawn_volley(e1)
	o1.set_bullet_rotation_data(0, z)
	o1.set_shared_bullet_rotation_data(sh9)
	# Presence bit: an explicit all-zero per-bullet entry is INTENT ("must not
	# spin"), so it wins regardless of call order.
	assert_almost_eq(o1.bullet_get_rotation_speed(0), 0.0, 0.01, "per-zero then shared stays zero")
	var e2 := H.make_volley_data(2, 100.0)
	e2.all_bullet_rotation_data = []
	var o2: BulletVolley2D = factory.spawn_volley(e2)
	o2.set_shared_bullet_rotation_data(sh9)
	o2.set_bullet_rotation_data(0, z)
	assert_almost_eq(o2.bullet_get_rotation_speed(0), 0.0, 0.01, "shared then per-zero stays zero")
	o2.remove_shared_bullet_rotation_data()
	assert_false(o2.has_shared_bullet_rotation_data(), "shared rotation removed")
	assert_almost_eq(o2.bullet_get_rotation_speed(1), 9.0, 0.01, "rotation ballistics persist after clear")
	assert_null(o2.get_shared_bullet_rotation_data(), "shared getter null after clear")
	var e3 := H.make_volley_data(2, 100.0)
	e3.all_bullet_rotation_data = [_rot(3.0), _rot(4.0)]
	var shs := _rot(1.0, 100.0)
	e3.shared_bullet_rotation_data = shs
	var o3: BulletVolley2D = factory.spawn_volley(e3)
	assert_almost_eq(o3.get_bullet_rotation_data(0).rotation_speed, 3.0, 0.01, "seed order keeps per-bullet")
	assert_eq(o3.get_shared_bullet_rotation_data(), shs, "shared getter returns the stored resource")
	assert_null(o3.get_shared_bullet_speed_data(), "shared speed getter null when unset")
	assert_true(o3.all_bullets_get_rotation_data(1, 0).is_empty(), "inverted range reads empty")
	expect_error_sequence(["Invalid index range in all_bullets_get_rotation_data (start > end)"])
	assert_true(o3.all_bullets_get_rotation_data(99, 99).is_empty(), "OOB range reads empty (fails loud)")
	expect_error_sequence(["Invalid index range in all_bullets_get_rotation_data (99..99 outside 0..1"])


func test_pool_reuse_drops_live_edits() -> void:
	var v: BulletVolley2D = quick_volley(1, 0.0)
	v.set_bullet_rotation_data(0, _rot(77.0))
	v.disable_bullet(0)
	await physics()
	var v6: BulletVolley2D = quick_volley(1, 100.0)
	assert_false(v6.is_rotation_data_active(), "reuse has no rotation")
	assert_almost_eq(v6.bullet_get_rotation_speed(0), 0.0, 0.01, "reuse speed zeroed")
