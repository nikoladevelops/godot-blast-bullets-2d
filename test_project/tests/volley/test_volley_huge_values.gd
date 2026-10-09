extends BlastTest
## Finite is not enough: a velocity or direction so large that squaring it
## overflows float32 (> ~1.8e19) used to be accepted, then speed became INF,
## direction (0,0), velocity NaN and the bullet's position NaN on the next
## tick, silently and for good. Contract: such a value is refused loudly and
## the bullet keeps flying exactly as before.

const HUGE := 1.0e30
const OVERFLOW_TEXT := "magnitude is too large to hold (its square overflows), keeping the old value."


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
