extends BlastTest
## A one-bullet volley behaves exactly like bullet 0 of a bigger volley:
## per-bullet curves win over shared curves at assignment time, and the
## speed / rotation / direction / velocity accessors read and write slot 0
## the same way they read and write any slot. (These paths once had a
## separate "single shared entry" branch for the removed block bullets.)


func _curves(speed: float, rotation: float) -> BulletCurvesData2D:
	var c := BulletCurvesData2D.new()
	c.movement_speed_curve = H.make_flat_curve(speed)
	c.rotation_speed_curve = H.make_flat_curve(rotation)
	return c


func _volley(n: int) -> BulletVolley2D:
	var v: BulletVolley2D = quick_volley(n, 100.0, 30.0)
	assert_not_null(v, "volley spawned")
	return v


func test_per_bullet_curves_beat_shared_curves_in_one_bullet_volley(n: int = use_parameters([1, 3])) -> void:
	var v := _volley(n)
	await idle(1)
	v.bullet_set_curves_data(0, _curves(50.0, 2.0))
	v.set_shared_bullet_curves_data(_curves(900.0, 7.0))
	# Read immediately: the shared seed must skip channels bullet 0 owns, for
	# a volley of 1 exactly like a volley of 3.
	assert_almost_eq(v.get_bullet_speed_data(0).speed, 50.0, 0.001, "own speed curve wins over shared (n=%d)" % n)
	assert_almost_eq(v.get_bullet_rotation_data(0).rotation_speed, 2.0, 0.001, "own rotation curve wins over shared (n=%d)" % n)


func test_one_bullet_accessors_round_trip_slot_zero() -> void:
	var v := _volley(1)
	await idle(1)
	v.set_bullet_speed_data(0, H.make_speed(321.0, 999.0, 4.0))
	var s: BulletSpeedData2D = v.get_bullet_speed_data(0)
	assert_almost_eq(s.speed, 321.0, 0.001, "speed written to slot 0")
	assert_almost_eq(s.max_speed, 999.0, 0.001, "max speed written to slot 0")
	v.set_bullet_rotation_data(0, H.make_rotation(3.5, 9.0, 1.0))
	var r: BulletRotationData2D = v.get_bullet_rotation_data(0)
	assert_almost_eq(r.rotation_speed, 3.5, 0.001, "rotation written to slot 0")
	v.set_bullet_direction(0, Vector2(0, 1))
	assert_almost_eq(v.get_bullet_direction(0).y, 1.0, 0.001, "direction written to slot 0")
	v.bullet_set_velocity(0, Vector2(-40, 0))
	assert_almost_eq(v.get_bullet_velocity(0).x, -40.0, 0.001, "velocity written to slot 0")
	var all_v: Array = v.all_bullets_get_velocity()
	assert_eq(all_v.size(), 1, "range read covers exactly one bullet")
	assert_almost_eq((all_v[0] as Vector2).x, -40.0, 0.001, "range read returns slot 0")
	# The velocity write above also set the speed (|velocity| = 40).
	var speed_before: float = v.get_bullet_speed_data(0).speed
	assert_almost_eq(speed_before, 40.0, 0.001, "velocity write sets the speed")
	# Index 1 does not exist in a one-bullet volley: reject loudly, never alias slot 0.
	v.set_bullet_speed_data(1, H.make_speed(5.0))
	expect_error_sequence(["Invalid bullet index in set_bullet_speed_data: 1 (amount_bullets: 1)"])
	assert_almost_eq(v.get_bullet_speed_data(0).speed, speed_before, 0.001, "slot 0 untouched by the rejected write")


func test_one_bullet_volley_flies_with_its_own_speed() -> void:
	var v := _volley(1)
	await physics(1)
	var x0: float = v.get_bullet_transform(0).origin.x
	await physics(30)
	# 100 px/s for exactly 30 ticks at 60 Hz = 50 px.
	assert_almost_eq(v.get_bullet_transform(0).origin.x - x0, 50.0, 0.01, "one bullet moves at its speed")
