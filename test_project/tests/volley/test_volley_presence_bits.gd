extends BlastTest
## Per-bullet seed PRESENCE: shared fallbacks no longer treat an all-zero
## triple as "no entry". A valid all-zero BulletSpeedData2D/RotationData2D
## means "don't move/spin" and must survive shared data; only invalid (null)
## entries are gaps. Fill-once order holds; direct writes claim presence;
## pooled reuse starts with fresh bits.


## n bullets at the origin; the caller supplies per-bullet data.
func _bare(n: int = 2) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 0.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D())
	d.transforms = arr
	d.all_bullet_speed_data = []
	return d


func _spd(speed: float, max_speed: float = 3000.0, acc: float = 0.0) -> BulletSpeedData2D:
	return H.make_speed(speed, max_speed, acc)


func _rot(speed: float, max_speed: float = 3000.0, acc: float = 0.0) -> BulletRotationData2D:
	return H.make_rotation(speed, max_speed, acc)


func _speed_of(v: BulletVolley2D, i: int) -> float:
	return float(v.debug_get_bullet_info(i)["speed"])


func _rot_of(v: BulletVolley2D, i: int) -> float:
	return float(v.debug_get_bullet_info(i)["rotation_speed"])


func _spawn(d: BulletVolleyData2D) -> BulletVolley2D:
	return factory.spawn_volley(d)


func test_speed_deliberate_zero_is_not_a_gap() -> void:
	var d := _bare()
	d.all_bullet_speed_data = [_spd(200.0), _spd(0.0, 0.0)]
	var v := _spawn(d)
	assert_not_null(v)
	v.set_shared_bullet_speed_data(_spd(150.0))
	assert_almost_eq(_speed_of(v, 0), 200.0, 0.01, "bullet 0 keeps its own speed")
	assert_almost_eq(_speed_of(v, 1), 0.0, 0.01, "bullet 1 stays frozen, not overwritten with 150")
	var before := v.get_bullet_transform(1).origin
	await physics(2)
	assert_lt(before.distance_to(v.get_bullet_transform(1).origin), 0.5, "frozen bullet does not travel")
	assert_almost_eq(_speed_of(v, 0), 200.0, 0.01, "bullet 0 still at 200 after ticks")


func test_speed_invalid_entry_is_a_gap() -> void:
	var d := _bare()
	d.all_bullet_speed_data = [_spd(200.0), null]
	var v := _spawn(d)
	v.set_shared_bullet_speed_data(_spd(150.0))
	assert_almost_eq(_speed_of(v, 0), 200.0, 0.01, "bullet 0 keeps per-bullet 200")
	assert_almost_eq(_speed_of(v, 1), 150.0, 0.01, "null-entry bullet 1 falls back to shared 150")


func test_rejected_nan_is_a_deliberate_zero() -> void:
	var nan_entry := BulletSpeedData2D.new()
	nan_entry.speed = NAN
	expect_error_sequence(["BulletSpeedData2D.speed must be a finite value"])
	assert_false(is_nan(nan_entry.speed), "BulletSpeedData2D refuses to store NaN")
	var d := _bare()
	d.all_bullet_speed_data = [_spd(200.0), nan_entry]
	var v := _spawn(d)
	v.set_shared_bullet_speed_data(_spd(150.0))
	assert_almost_eq(_speed_of(v, 0), 200.0, 0.01, "sibling keeps its own 200")
	assert_almost_eq(_speed_of(v, 1), 0.0, 0.01, "rejected-NaN entry is a valid zero, not filled")
	assert_true((v.debug_get_bullet_info(0)["velocity"] as Vector2).is_finite(), "sibling velocity finite")


func test_rotation_deliberate_zero_is_not_a_gap() -> void:
	var d := _bare()
	d.all_bullet_speed_data = [_spd(0.0), _spd(0.0)]
	d.all_bullet_rotation_data = [_rot(4.0, 10.0), _rot(0.0, 0.0)]
	var v := _spawn(d)
	v.set_shared_bullet_rotation_data(_rot(7.0))
	assert_almost_eq(_rot_of(v, 0), 4.0, 0.01, "bullet 0 keeps its own spin")
	assert_almost_eq(_rot_of(v, 1), 0.0, 0.01, "bullet 1 stays no-spin, not overwritten with 7")
	var a1 := v.get_bullet_transform(1).get_rotation()
	var a0 := v.get_bullet_transform(0).get_rotation()
	await physics(2)
	assert_lt(absf(angle_difference(a1, v.get_bullet_transform(1).get_rotation())), 0.0001, "no-spin angle unchanged")
	assert_gt(absf(angle_difference(a0, v.get_bullet_transform(0).get_rotation())), 0.0001, "spinning angle advanced")


func test_rotation_invalid_entry_is_a_gap() -> void:
	var d := _bare()
	d.all_bullet_speed_data = [_spd(0.0), _spd(0.0)]
	d.all_bullet_rotation_data = [_rot(4.0, 10.0), null]
	var v := _spawn(d)
	expect_error_sequence(["Invalid rotation data at index 1: expected BulletRotationData2D"])
	v.set_shared_bullet_rotation_data(_rot(7.0))
	assert_almost_eq(_rot_of(v, 0), 4.0, 0.01, "bullet 0 keeps per-bullet 4")
	assert_almost_eq(_rot_of(v, 1), 7.0, 0.01, "null-entry bullet 1 falls back to shared 7")


func test_rotation_full_shared_fan_out() -> void:
	var d := _bare(3)
	d.all_bullet_speed_data = [_spd(0.0), _spd(0.0), _spd(0.0)]
	var v := _spawn(d)
	v.set_shared_bullet_rotation_data(_rot(3.0))
	for i in 3:
		assert_almost_eq(_rot_of(v, i), 3.0, 0.01, "shared rotation fans out to slot %d" % i)


func test_speed_fill_once_both_orders() -> void:
	var d := _bare()
	d.all_bullet_speed_data = [_spd(200.0), null]
	var v := _spawn(d)
	v.set_shared_bullet_speed_data(_spd(150.0))
	v.set_shared_bullet_speed_data(_spd(999.0))
	assert_almost_eq(_speed_of(v, 0), 200.0, 0.01, "second shared call leaves the per-bullet slot alone")
	assert_almost_eq(_speed_of(v, 1), 150.0, 0.01, "second shared call does not re-fill")
	var vb := _spawn(_bare())
	vb.set_shared_bullet_speed_data(_spd(150.0))
	vb.set_bullet_speed_data(0, _spd(50.0))
	vb.set_bullet_speed_data(1, _spd(0.0, 0.0))
	assert_almost_eq(_speed_of(vb, 0), 50.0, 0.01, "per-bullet seeded after shared wins")
	assert_almost_eq(_speed_of(vb, 1), 0.0, 0.01, "deliberate zero seeded after shared survives")


func test_rotation_fill_once_both_orders() -> void:
	var d := _bare()
	d.all_bullet_speed_data = [_spd(0.0), _spd(0.0)]
	d.all_bullet_rotation_data = [_rot(4.0, 10.0), null]
	var v := _spawn(d)
	expect_error_sequence(["Invalid rotation data at index 1: expected BulletRotationData2D"])
	v.set_shared_bullet_rotation_data(_rot(7.0))
	v.set_shared_bullet_rotation_data(_rot(999.0))
	assert_almost_eq(_rot_of(v, 0), 4.0, 0.01, "second shared rotation leaves the per-bullet slot alone")
	assert_almost_eq(_rot_of(v, 1), 7.0, 0.01, "second shared rotation does not re-fill")
	var db := _bare()
	db.all_bullet_speed_data = [_spd(0.0), _spd(0.0)]
	var vb := _spawn(db)
	vb.set_shared_bullet_rotation_data(_rot(7.0))
	vb.set_bullet_rotation_data(0, _rot(2.0, 10.0))
	vb.set_bullet_rotation_data(1, _rot(0.0, 0.0))
	assert_almost_eq(_rot_of(vb, 0), 2.0, 0.01, "per-bullet rotation seeded after shared wins")
	assert_almost_eq(_rot_of(vb, 1), 0.0, 0.01, "deliberate no-spin seeded after shared survives")


func test_direct_write_claims_presence() -> void:
	var v := _spawn(_bare())
	v.set_shared_bullet_speed_data(_spd(150.0))
	assert_almost_eq(_speed_of(v, 0), 150.0, 0.01, "shared filled both slots first")
	v.set_bullet_speed_data(0, _spd(0.0, 0.0))
	v.set_bullet_speed_data(1, _spd(0.0, 0.0))
	v.set_shared_bullet_speed_data(_spd(999.0))
	assert_almost_eq(_speed_of(v, 0), 0.0, 0.01, "direct zero-write survives a later shared set (0)")
	assert_almost_eq(_speed_of(v, 1), 0.0, 0.01, "direct zero-write survives a later shared set (1)")
	var dr := _bare()
	dr.all_bullet_speed_data = [_spd(0.0), _spd(0.0)]
	var vr := _spawn(dr)
	vr.set_shared_bullet_rotation_data(_rot(7.0))
	vr.set_bullet_rotation_data(0, _rot(0.0, 0.0))
	vr.set_bullet_rotation_data(1, _rot(0.0, 0.0))
	vr.set_shared_bullet_rotation_data(_rot(999.0))
	assert_almost_eq(_rot_of(vr, 0), 0.0, 0.01, "direct no-spin write survives (0)")
	assert_almost_eq(_rot_of(vr, 1), 0.0, 0.01, "direct no-spin write survives (1)")


func test_pool_reuse_starts_fresh() -> void:
	var d := _bare()
	d.all_bullet_speed_data = [_spd(200.0), _spd(0.0, 0.0)]
	var v := _spawn(d)
	v.set_shared_bullet_speed_data(_spd(150.0))
	assert_almost_eq(_speed_of(v, 1), 0.0, 0.01, "first life slot 1 frozen by a valid zero")
	v.clear_all_bullets()
	await idle()
	var db := _bare()
	db.all_bullet_speed_data = [_spd(200.0), null]
	var vb := _spawn(db)
	vb.set_shared_bullet_speed_data(_spd(160.0))
	assert_almost_eq(_speed_of(vb, 1), 160.0, 0.01, "new life fills its own gap (no inherited bit)")
