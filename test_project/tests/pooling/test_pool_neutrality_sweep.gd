extends BlastTest
## Pool neutrality: a volley that lived with non-default runtime settings,
## went back to the pool and was popped again must read exactly like a cold
## volley built from the same plain data. Runtime-only knobs (ones that spawn
## data never seeds) are the classic leak: they survive the reset unless the
## new life resets them explicitly.

const N := 3


func _plain() -> BulletVolleyData2D:
	return H.make_volley_data(N, 120.0, 5.0)


## Drains every bullet so the volley goes back to the pool, then spawns the
## same key again and returns the reused instance (asserted to be a pool hit).
func _drain_and_reuse(v: BulletVolley2D) -> BulletVolley2D:
	for i in v.get_amount_bullets():
		v.disable_bullet(i)
	await idle(1)
	var reused: BulletVolley2D = factory.spawn_volley(_plain())
	assert_same(reused, v, "same key reuses the pooled instance")
	return reused


func test_collision_dedup_mode_is_reset_for_the_next_life() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_plain())
	v.set_collision_dedup_by_object(false)
	var reused := await _drain_and_reuse(v)
	assert_true(reused.get_collision_dedup_by_object(), "dedup mode is per life: the next owner starts object-level (default)")
