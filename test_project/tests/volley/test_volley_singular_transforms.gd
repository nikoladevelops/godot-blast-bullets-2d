extends SceneTree
## Singular-transform suite: zero/singular scales are rejected everywhere a
## transform is validated or inverted.
##
## The gap: several guards used get_scale().length_squared(), which a (0, 1)
## scale passes (length 1) even though its determinant is 0 and affine_inverse
## is garbage. All validation/inversion sites now centralise on
## is_transform_invertible_safe(). Setters that previously accepted a
## singular basis could NaN-poison interpolation, FX local conversion, and
## direction math.
##
## Covers: T1 spawn rejects a singular transform, T2 set_bullet_transform
## rejects singular and keeps the old, T3 zero scale still rejected, T4 the
## volley stays finite, T5 no dangling.
##
## Run: godot --headless --path test_project --script tests/volley/test_volley_singular_transforms.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _speeds(n: int, speed: float) -> Array:
	var out: Array = []
	for i in n:
		var s := BulletSpeedData2D.new()
		s.speed = speed
		s.max_speed = 3000.0
		out.append(s)
	return out

func _base_data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	d.all_bullet_speed_data = _speeds(1, 100.0)
	d.max_life_time = 60.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	return d

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("SINGULAR T1 spawn rejects a singular (0, 1)-scale transform")
	var bad := _base_data()
	# Determinant 0 but finite with unit length: the old length_squared
	# guard passed this straight into the volley.
	bad.transforms = [Transform2D(Vector2(0, 0), Vector2(0, 1), Vector2.ZERO)]
	var vnull: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(bad)
	_check(vnull == null, "T1 singular spawn refused")
	_check(factory.debug_get_active_bullets_amount(0) == 0, "T1 nothing live after refusal")

	# ---------------------------------------------------------------
	printerr("SINGULAR T2 set_bullet_transform rejects singular, keeps old")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_base_data())
	var before: Transform2D = v.get_bullet_transform(0)
	v.set_bullet_transform(0, Transform2D(Vector2(0, 0), Vector2(0, 1), Vector2(5, 5)))
	var after: Transform2D = v.get_bullet_transform(0)
	_check(after.is_equal_approx(before), "T2 old transform kept (got %s)" % str(after))

	# ---------------------------------------------------------------
	printerr("SINGULAR T3 zero scale still rejected")
	v.set_bullet_transform(0, Transform2D(0.0, Vector2.ZERO).scaled(Vector2(0, 0)))
	_check(v.get_bullet_transform(0).is_equal_approx(before), "T3 zero scale refused")

	# ---------------------------------------------------------------
	printerr("SINGULAR T4 volley stays finite through ticks")
	for i in 10:
		await physics_frame
	_check(v.get_bullet_transform(0).is_finite(), "T4 transform finite after ticks")
	_check(v.get_bullet_velocity(0).is_finite(), "T4 velocity finite after ticks")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL SINGULAR-TRANSFORM TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
