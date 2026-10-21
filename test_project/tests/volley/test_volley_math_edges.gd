extends BlastTest
## Quantitative tick identities: linear drag decays geometrically, rotation
## acceleration clamps at max, lateral wobble stays bounded around the
## ballistic path, per-bullet smoothing clamps at zero.
##
## Zero/singular scales are rejected wherever a transform is validated or
## inverted (a (0, 1) scale has unit length but determinant 0): spawn
## refuses, set_bullet_transform refuses and keeps the old, the volley stays
## finite.

var singular := Transform2D(Vector2(0, 0), Vector2(0, 1), Vector2.ZERO)


func test_drag_decays_geometrically() -> void:
	var v: BulletVolley2D = quick_volley(1, 200.0)
	v.set_linear_drag(1.0)
	await physics(60)
	# 200 * (1 - 1/60)^60 ~= 73; band wide for headless dt jitter.
	assert_between(v.get_bullet_speed_data(0).speed, 30.0, 130.0, "drag decay band")
	v.set_linear_drag(-1.0)
	expect_error_sequence(["BulletVolley2D.set_linear_drag: value must be finite and >= 0, keeping the old value."])
	assert_eq(v.get_linear_drag(), 1.0, "negative drag rejected")


func test_rotation_accel_clamps_at_max() -> void:
	var r: BulletVolley2D = quick_volley(1, 0.0)
	r.set_shared_bullet_rotation_data(H.make_rotation(0.0, 2.0, 100.0))
	await physics(30)
	assert_ne(r.get_bullet_texture_rotation_radians(0), 0.0, "rotation advanced under accel")
	assert_lte(absf(r.bullet_get_rotation_speed(0)), 2.0 + 0.001, "speed clamped at max")
	r.remove_shared_bullet_rotation_data()
	assert_false(r.has_shared_bullet_rotation_data(), "rotation data removed")


func test_wobble_lateral_bounded() -> void:
	var d := H.make_volley_data(1, 300.0)
	var wob := BulletWobbleData2D.new()
	wob.enabled = true
	wob.amplitude = 24.0
	wob.frequency_hz = 2.0
	d.shared_bullet_wobble_data = wob
	var w: BulletVolley2D = factory.spawn_volley(d)
	var max_dev := 0.0
	for i in 60:
		await physics()
		max_dev = maxf(max_dev, absf(w.get_bullet_transform(0).origin.y))
	assert_lt(max_dev, 120.0, "lateral wobble bounded")
	assert_gt(w.get_bullet_transform(0).origin.x, 200.0, "forward progress kept")


func test_per_bullet_smoothing_clamps() -> void:
	var f: BulletVolley2D = quick_volley(2, 200.0)
	f.bullet_set_homing_smoothing(0, -5.0)
	expect_error_sequence(["bullet_set_homing_smoothing: value must be finite and >= 0"])
	assert_eq(f.bullet_get_homing_smoothing(0), 0.0, "negative smoothing clamped to 0")
	f.bullet_set_homing_smoothing(0, NAN)
	expect_error_sequence(["bullet_set_homing_smoothing: value must be finite and >= 0 (0 snaps instantly)."])
	assert_eq(f.bullet_get_homing_smoothing(0), 0.0, "NaN smoothing rejected")


func test_spawn_rejects_singular() -> void:
	var bad := H.make_volley_data(1, 100.0, 60.0)
	bad.transforms = [singular]
	assert_null(factory.spawn_volley(bad), "singular spawn refused")
	expect_error("zero or singular scale")
	assert_eq(factory.debug_get_active_bullets_amount(), 0, "nothing live after the refusal")


func test_setter_rejects_singular_and_zero_scale() -> void:
	var v: BulletVolley2D = quick_volley(1, 100.0, 60.0)
	var before: Transform2D = v.get_bullet_transform(0)
	v.set_bullet_transform(0, Transform2D(Vector2(0, 0), Vector2(0, 1), Vector2(5, 5)))
	expect_error("non-singular")
	assert_eq(v.get_bullet_transform(0), before, "old transform kept")
	v.set_bullet_transform(0, Transform2D(0.0, Vector2.ZERO).scaled(Vector2(0, 0)))
	expect_error_sequence(["set_bullet_transform: scale must be non-zero and non-singular, keeping the old transform."])
	assert_eq(v.get_bullet_transform(0), before, "zero scale refused")
	await physics(10)
	assert_true(v.get_bullet_transform(0).is_finite(), "transform finite after ticks")
	assert_true(v.get_bullet_velocity(0).is_finite(), "velocity finite after ticks")
