extends BlastTest
## Per-bullet seed PRESENCE: shared fallbacks no longer treat an all-zero
## triple as "no entry". A valid all-zero BulletSpeedData2D/RotationData2D
## means "don't move/spin" and must survive shared data; only invalid (null)
## entries are gaps. Fill-once order holds; direct writes claim presence;
## pooled reuse starts with fresh bits.
##
## clear_bullet_rotation_data() clears the presence bits with the values: a
## later shared fallback fills genuine gaps, an explicit zero still wins,
## and clear alone + shared full-seeds.
##
## Plain (same-owner) disables keep rotation presence decisions for the wake:
## authored entries survive disable -> wake -> shared write, genuine gaps
## still fill; a new pooled life re-derives presence from its seed.
##
## set_gravity() fills genuine gaps only (presence bits): authored per-bullet
## gravity (bullet_set_gravity or seeded) survives a shared write.




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
	expect_error_sequence(["BulletSpeedData2D: speed must be finite (NaN/Inf would poison bullet movement), keeping the old value."])
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


func _data() -> BulletVolleyData2D:
	var d := H.make_still_data(2)
	d.all_bullet_rotation_data = [H.make_rotation(0.0), H.make_rotation(9.0)]
	return d


func test_cleared_bits_let_shared_fill_gaps() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	v.clear_bullet_rotation_data()
	v.set_bullet_rotation_data(0, H.make_rotation(0.0))
	v.set_shared_bullet_rotation_data(H.make_rotation(7.0))
	assert_almost_eq(v.bullet_get_rotation_speed(1), 7.0, 0.01, "gap slot filled by shared")
	assert_almost_eq(v.bullet_get_rotation_speed(0), 0.0, 0.01, "authored zero survives")


func test_clear_alone_then_shared_full_seeds() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	v.clear_bullet_rotation_data()
	v.set_shared_bullet_rotation_data(H.make_rotation(5.0))
	assert_almost_eq(v.bullet_get_rotation_speed(0), 5.0, 0.01, "slot 0 seeded")
	assert_almost_eq(v.bullet_get_rotation_speed(1), 5.0, 0.01, "slot 1 seeded")


func _data_2() -> BulletVolleyData2D:
	var d := H.make_still_data(3)
	# Slot 0 deliberate zero, slot 1 authored spin, slot 2 a genuine gap.
	d.all_bullet_rotation_data = [H.make_rotation(0.0), H.make_rotation(9.0)]
	return d


func test_same_owner_wake_keeps_decisions() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data_2())
	v.set_is_auto_pooling_enabled(false)
	for i in 3:
		v.disable_bullet(i)
	await idle(1)
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "unpooled disable parks nothing")
	for i in 3:
		v.wake_bullet(i)
	v.set_shared_bullet_rotation_data(H.make_rotation(7.0))
	assert_almost_eq(v.bullet_get_rotation_speed(0), 0.0, 0.01, "authored zero survives wake + shared")
	assert_almost_eq(v.bullet_get_rotation_speed(1), 9.0, 0.01, "authored spin survives wake + shared")
	assert_almost_eq(v.bullet_get_rotation_speed(2), 7.0, 0.01, "gap fills after the wake")


func test_new_pooled_life_rederives_presence() -> void:
	var a: BulletVolley2D = factory.spawn_volley(_data_2())
	for i in 3:
		a.disable_bullet(i)
	await idle(1)
	factory.debug_reset_pool_stats()
	var w: BulletVolley2D = factory.spawn_volley(_data_2())
	assert_eq(int(factory.debug_get_pool_hit_stats().get("hits", 0)), 1, "second life reuses the pool")
	w.set_shared_bullet_rotation_data(H.make_rotation(7.0))
	assert_almost_eq(w.bullet_get_rotation_speed(0), 0.0, 0.01, "new life honors the authored zero")
	assert_almost_eq(w.bullet_get_rotation_speed(1), 9.0, 0.01, "new life honors the authored spin")


func _data_3() -> BulletVolleyData2D:
	var d := H.make_still_data(2)
	d.gravity = Vector2(0, 100)
	return d


func test_authored_slot_survives_gap_fills() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data_3())
	v.bullet_set_gravity(0, Vector2(50, 0))
	v.set_gravity(Vector2(0, 300))
	assert_eq(v.bullet_get_gravity(0), Vector2(50, 0), "authored gravity kept")
	assert_eq(v.bullet_get_gravity(1), Vector2(0, 300), "gap filled by shared")
	await physics(5)
	assert_true(v.get_bullet_velocity(0).is_finite() and v.get_bullet_velocity(1).is_finite(), "velocities finite under gravity")


func test_seeded_entries_claim_presence() -> void:
	var d := _data_3()
	d.all_bullet_gravity = [Vector2(10, 0), Vector2(20, 0)]
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.set_gravity(Vector2(0, 300))
	assert_eq(v.bullet_get_gravity(0), Vector2(10, 0), "seeded slot 0 kept")
	assert_eq(v.bullet_get_gravity(1), Vector2(20, 0), "seeded slot 1 kept")
