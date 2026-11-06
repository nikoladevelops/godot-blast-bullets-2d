extends BlastTest
## Finite is not enough: a velocity or direction so large that squaring it
## overflows float32 (> ~1.8e19) used to be accepted, then speed became INF,
## direction (0,0), velocity NaN and the bullet's position NaN on the next
## tick, silently and for good. Contract: such a value is refused loudly and
## the bullet keeps flying exactly as before. Same for the transform/scale
## setters (a basis whose square overflows) and texture_size.

const HUGE := 1.0e30
const OVERFLOW_TEXT := "magnitude is too large to hold (its square overflows), keeping the old value."
const MAGNITUDE_TEXT := "is too large to represent (its square overflows), keeping the old "


func _all_finite(v: BulletVolley2D) -> bool:
	for i in v.get_amount_bullets():
		if not v.get_bullet_transform(i).is_finite() or not v.get_bullet_velocity(i).is_finite() or not v.get_bullet_direction(i).is_finite():
			return false
	return true


func test_a_velocity_that_overflows_is_refused_and_the_bullet_keeps_flying() -> void:
	var v: BulletVolley2D = quick_volley(3, 100.0)
	var before: Vector2 = v.get_bullet_velocity(0)
	v.bullet_set_velocity(0, Vector2(HUGE, 0.0))
	expect_error_sequence(["bullet_set_velocity: new_velocity " + OVERFLOW_TEXT])
	assert_eq(v.get_bullet_velocity(0), before, "velocity untouched")
	await physics(3)
	assert_true(_all_finite(v), "still finite after ticking")


func test_the_range_setter_refuses_per_bullet_and_changes_nothing() -> void:
	var v: BulletVolley2D = quick_volley(3, 100.0)
	var before: Array = Array(v.all_bullets_get_velocity())
	v.all_bullets_set_velocity(Vector2(0.0, -HUGE))
	expect_error_sequence(["bullet_set_velocity: new_velocity " + OVERFLOW_TEXT, "bullet_set_velocity: new_velocity " + OVERFLOW_TEXT, "bullet_set_velocity: new_velocity " + OVERFLOW_TEXT])
	assert_eq(Array(v.all_bullets_get_velocity()), before, "no bullet changed")
	await physics(3)
	assert_true(_all_finite(v), "volley stays finite")


func test_the_largest_velocity_that_squares_cleanly_is_still_accepted() -> void:
	# 1e19 squared is 1e38, just under float32's 3.4e38: legal (absurd, but finite all the way).
	var v: BulletVolley2D = quick_volley(1, 100.0)
	v.bullet_set_velocity(0, Vector2(1.0e19, 0.0))
	expect_no_errors("1e19 is representable end to end")
	assert_almost_eq(v.get_bullet_velocity(0).x, 1.0e19, 1.0e13, "stored (to float32 precision)")
	assert_true(_all_finite(v), "finite")


## A basis of (1e38, 1e-4) is finite and its determinant (1e34) is fine, but
## columns[0] squared overflows float32 to INF: every later conversion of the
## pose (shape, multimesh, interpolation) derives from an INF. transform_is_
## sane2d rejects exactly that; the bullet keeps its old pose. (Godot's
## 4-argument constructor is (rotation, scale, skew, position); and
## Transform2D.scaled() would scale the origin into INF first, which the plain
## finiteness check already catches.)
func test_an_absurd_but_finite_transform_is_refused_and_the_old_pose_kept() -> void:
	var v: BulletVolley2D = quick_volley(1, 100.0)
	var before: Transform2D = v.get_bullet_transform(0)
	v.set_bullet_transform(0, Transform2D(0.0, Vector2(1.0e38, 1.0e-4), 0.0, Vector2(50, 0)))
	expect_error_sequence(["set_bullet_transform: scale " + MAGNITUDE_TEXT + "transform."])
	assert_eq(v.get_bullet_transform(0), before, "the old pose is kept")
	await physics(3)
	assert_true(_all_finite(v), "still finite after ticking")


func test_an_absurd_but_finite_scale_is_refused_and_the_old_scale_kept() -> void:
	var v: BulletVolley2D = quick_volley(1, 100.0)
	var before: Vector2 = v.get_bullet_scale(0)
	v.set_bullet_scale(0, Vector2(1.0e38, 1.0e-4))
	expect_error_sequence(["set_bullet_scale: new_scale " + MAGNITUDE_TEXT + "scale."])
	assert_eq(v.get_bullet_scale(0), before, "the old scale is kept")
	await physics(3)
	assert_true(_all_finite(v), "still finite after ticking")


## The bound is where the square stops fitting float32, not the magnitude:
## 1e19 per axis (1e38 squared) is absurd but representable, so it stays legal
## exactly like the velocity pin above.
func test_a_scale_whose_square_still_fits_stays_accepted() -> void:
	var v: BulletVolley2D = quick_volley(1, 100.0)
	v.set_bullet_scale(0, Vector2(1.0e19, 1.0e-4))
	expect_no_errors("1e19 per axis is representable end to end")
	assert_true(v.get_bullet_scale(0).is_equal_approx(Vector2(1.0e19, 1.0e-4)), "stored (to float32 precision)")
	var pose := v.get_bullet_transform(0)
	v.set_bullet_transform(0, pose)
	expect_no_errors("a pose with the same huge basis round-trips")
	assert_true(_all_finite(v), "finite")


## texture_size feeds the quad mesh straight into the render server: a
## typo'd 1e7 px quad is a sane maximum away from a broken draw.
func test_a_texture_size_beyond_the_sane_maximum_is_refused() -> void:
	var d := BulletVolleyData2D.new()
	d.texture_size = Vector2(32, 32)
	d.texture_size = Vector2(1.0e7, 32)
	expect_error_sequence(["BulletVolleyData2D: texture_size must be at most 1000000 px per axis, keeping the old value."])
	assert_eq(d.texture_size, Vector2(32, 32), "1e7 px refused")
	d.texture_size = Vector2(32, 1.0e7)
	expect_error_sequence(["BulletVolleyData2D: texture_size must be at most 1000000 px per axis, keeping the old value."])
	assert_eq(d.texture_size, Vector2(32, 32), "per axis: 1e7 px on y refused too")
	d.texture_size = Vector2(1000000, 1000000)
	assert_eq(d.texture_size, Vector2(1000000, 1000000), "the bound itself stays legal")


## The spawn path already refused the same basis (its get_scale() reads INF):
## this pins that sibling so the two guards cannot drift apart silently.
func test_a_huge_spawn_transform_is_already_refused_by_the_spawn_guard() -> void:
	var d := H.make_volley_data(1, 100.0)
	d.transforms = [Transform2D(0.0, Vector2(1.0e38, 1.0e-4), 0.0, Vector2.ZERO)]
	assert_null(factory.spawn_volley(d), "the spawn path refuses an overflowing basis too")
	expect_error_sequence(["Error in spawn_volley: transforms[0] contains NaN/Inf. Nothing was spawned."])
	assert_eq(factory.debug_get_active_volleys_amount(), 0, "nothing live")
