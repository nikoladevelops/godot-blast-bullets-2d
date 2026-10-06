extends BlastTest
## Volley core API: speed / direction / transform / velocity / texture
## rotation get-set, per-bullet and all_bullets_ variants, NaN / zero / null /
## OOB rejects keep the old value, derive-direction from a transform,
## teleport carry, shape state, default max_speed 0 = unlimited.

var v: BulletVolley2D


func before_each() -> void:
	await super()
	v = quick_volley(3, 200.0)


func test_speed_data() -> void:
	assert_almost_eq(v.get_bullet_speed_data(0).speed, 200.0, 0.01, "speed seeded")
	v.set_bullet_speed_data(1, H.make_speed(500.0))
	assert_almost_eq(v.get_bullet_speed_data(1).speed, 500.0, 0.01, "per-bullet speed set")
	assert_almost_eq(v.get_bullet_speed_data(0).speed, 200.0, 0.01, "sibling untouched")
	var bad_sp := BulletSpeedData2D.new()
	bad_sp.speed = NAN
	expect_error_sequence(["BulletSpeedData2D: speed must be finite (NaN/Inf would poison bullet movement), keeping the old value."])
	assert_eq(bad_sp.speed, 0.0, "resource setter rejects NaN, keeps default")
	v.set_bullet_speed_data(0, null)
	expect_error("is null")
	assert_almost_eq(v.get_bullet_speed_data(0).speed, 200.0, 0.01, "null speed data rejected")
	v.set_bullet_speed_data(-1, H.make_speed(500.0))
	v.set_bullet_speed_data(99, H.make_speed(500.0))
	expect_errors_containing("Invalid bullet index", 2)


func test_direction() -> void:
	v.set_bullet_direction(0, Vector2(0, 1))
	assert_almost_eq(v.get_bullet_direction(0), Vector2(0, 1), Vector2(0.01, 0.01), "direction set")
	v.set_bullet_direction(0, Vector2.ZERO)
	expect_error("direction is zero")
	v.set_bullet_direction(0, Vector2(NAN, 0))
	expect_error("must be finite")
	assert_almost_eq(v.get_bullet_direction(0), Vector2(0, 1), Vector2(0.01, 0.01), "zero and NaN rejected")
	var vel: Vector2 = v.get_bullet_velocity(0)
	assert_true(vel.is_finite() and vel.length() > 1.0, "velocity finite and live")
	v.all_bullets_set_direction(Vector2(1, 0))
	assert_almost_eq(v.get_bullet_direction(2), Vector2(1, 0), Vector2(0.01, 0.01), "all_bullets direction fans out")


func test_transforms_and_teleport() -> void:
	var t0: Transform2D = v.get_bullet_transform(0)
	assert_true(t0.is_finite())
	assert_eq(v.get_bullet_global_transform(0), t0, "global transform round-trips the cache")
	v.set_bullet_transform(0, Transform2D(0.0, Vector2(300, 400)), true)
	assert_almost_eq(v.get_bullet_transform(0).origin, Vector2(300, 400), Vector2(0.01, 0.01), "set transform moves")
	assert_almost_eq(v.get_bullet_direction(0), Vector2(1, 0), Vector2(0.5, 0.5), "derive-direction follows the transform")
	v.set_bullet_transform(1, Transform2D(0.0, Vector2(NAN, 0)))
	expect_error("must be finite")
	v.set_bullet_transform(1, Transform2D.IDENTITY.scaled(Vector2(0, 0)))
	expect_error_sequence(["set_bullet_transform: scale must be non-zero and non-singular, keeping the old transform."])
	assert_true(v.get_bullet_transform(1).is_finite(), "bad transforms rejected")
	v.teleport_shift_all_bullets(Vector2(10, 0))
	assert_almost_eq(v.get_bullet_transform(0).origin, Vector2(310, 400), Vector2(0.01, 0.01), "volley shift carries")
	var shape: Dictionary = v.debug_get_shape_state()
	assert_true(shape.get("valid", false), "shape state valid")
	# One shared server shape per volley (F1: N RIDs made cold spawns O(N^2)),
	# attached once per bullet.
	assert_eq(int(shape.get("rid_count", 0)), 1, "one shared shape RID per volley")
	assert_eq(int(shape.get("shape_count", 0)), 3, "one area shape slot per bullet")


func test_default_max_speed_is_unlimited() -> void:
	var d := H.make_volley_data(1, 300.0, 10.0)
	var sp := BulletSpeedData2D.new()
	sp.speed = 300.0 # max_speed left at 0 = unlimited, not a brake
	d.all_bullet_speed_data = [sp]
	var pv: BulletVolley2D = factory.spawn_volley(d)
	var x0: float = pv.get_bullet_global_transform(0).origin.x
	await physics(10)
	assert_gt(pv.get_bullet_global_transform(0).origin.x, x0 + 20.0, "default max_speed flies")


func test_texture_rotation() -> void:
	v.set_bullet_texture_rotation_radians(0, PI * 0.5)
	assert_almost_eq(v.get_bullet_texture_rotation_radians(0), PI * 0.5, 0.001, "texture rotation set")
	v.set_bullet_texture_rotation_degrees(0, 0.0)
	assert_almost_eq(v.get_bullet_texture_rotation_degrees(0), 0.0, 0.01, "degrees set")
	v.set_bullet_texture_rotation_radians(0, NAN)
	expect_error("must be finite")
	assert_almost_eq(v.get_bullet_texture_rotation_radians(0), 0.0, 0.001, "NaN rejected")


func test_texture_rotation_round_trip_with_offset() -> void:
	var d := H.make_volley_data(1, 0.0)
	d.texture_rotation_radians = 0.7
	var w: BulletVolley2D = factory.spawn_volley(d)
	var r0: float = w.get_bullet_texture_rotation_radians(0)
	assert_almost_eq(r0, 0.0, 0.001, "fresh bullet reads 0 (volley offset excluded)")
	w.set_bullet_texture_rotation_radians(0, w.get_bullet_texture_rotation_radians(0))
	w.set_bullet_texture_rotation_radians(0, w.get_bullet_texture_rotation_radians(0))
	assert_almost_eq(w.get_bullet_texture_rotation_radians(0), r0, 0.001, "set(get()) is a no-op")
	w.set_bullet_texture_rotation_degrees(0, 30.0)
	assert_almost_eq(w.get_bullet_texture_rotation_degrees(0), 30.0, 0.01, "degrees round-trip")
	assert_almost_eq(angle_difference(w.get_bullet_transform(0).get_rotation(), deg_to_rad(30.0) + 0.7), 0.0, 0.001, "instance still carries the offset")


## A direction curve owns the heading: every direction setter refuses the
## write with ONE warning in direction wording (it once said "speed data").
func test_direction_setters_under_a_direction_curve_refuse_with_direction_wording() -> void:
	var target := Node2D.new()
	target.position = Vector2(0, 300)
	add(target)
	for owner in ["shared", "individual"]:
		var curves := BulletCurvesData2D.new()
		curves.x_direction_curve = H.make_flat_curve(0.5)
		var d := H.make_volley_data(2, 100.0, 30.0)
		if owner == "shared":
			d.shared_bullet_curves_data = curves
		else:
			d.all_bullet_curves_data = [curves, curves]
		var v: BulletVolley2D = factory.spawn_volley(d)
		var before: Vector2 = v.get_bullet_direction(0)
		var text := "You are trying to set bullet direction directly while having a direction curve assigned to the %s curves data. The curve will override any direct direction changes. Set the curve to null first if you want to set direction directly." % owner
		v.set_bullet_direction(0, Vector2(0, -1))
		expect_warning_sequence([text])
		v.set_bullet_direction_towards_position(0, Vector2(-400, 0))
		expect_warning_sequence([text])
		v.set_bullet_direction_towards_node2d(0, target)
		expect_warning_sequence([text])
		assert_eq(v.get_bullet_direction(0), before, "%s curve: the direction was left to the curve" % owner)
		v.clear_all_bullets()
		await idle(1)
