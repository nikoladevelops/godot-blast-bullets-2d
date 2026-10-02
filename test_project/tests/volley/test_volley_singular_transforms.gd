extends BlastTest
## Zero/singular scales are rejected wherever a transform is validated or
## inverted (a (0, 1) scale has unit length but determinant 0): spawn
## refuses, set_bullet_transform refuses and keeps the old, the volley stays
## finite.

var singular := Transform2D(Vector2(0, 0), Vector2(0, 1), Vector2.ZERO)


func test_spawn_rejects_singular() -> void:
	var bad := H.make_directional_data(1, 100.0, 60.0)
	bad.transforms = [singular]
	assert_null(factory.spawn_controllable_directional_bullets(bad), "singular spawn refused")
	expect_error("zero or singular scale")
	assert_eq(factory.debug_get_active_bullets_amount(0), 0, "nothing live after the refusal")


func test_setter_rejects_singular_and_zero_scale() -> void:
	var v: DirectionalBullets2D = spawn_dir(1, 100.0, 60.0)
	var before: Transform2D = v.get_bullet_transform(0)
	v.set_bullet_transform(0, Transform2D(Vector2(0, 0), Vector2(0, 1), Vector2(5, 5)))
	expect_error("non-singular")
	assert_eq(v.get_bullet_transform(0), before, "old transform kept")
	v.set_bullet_transform(0, Transform2D(0.0, Vector2.ZERO).scaled(Vector2(0, 0)))
	expect_any_error()
	assert_eq(v.get_bullet_transform(0), before, "zero scale refused")
	await physics(10)
	assert_true(v.get_bullet_transform(0).is_finite(), "transform finite after ticks")
	assert_true(v.get_bullet_velocity(0).is_finite(), "velocity finite after ticks")
