extends BlastTest
## A pooled volley's reuse is a new life: pool hit, zeroed collision counts,
## a fresh dedup window (the same wall counts again), old custom timers never
## fire, no attachments carried over, reseeded ballistics win over the old
## per-bullet edits.

var _timer_fired := false


func _on_old_timer() -> void:
	_timer_fired = true


func _data() -> DirectionalBulletsData2D:
	var d := H.make_directional_data(1, 900.0, 60.0)
	d.monitorable = true
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	return d


func test_pooled_reuse_is_a_new_life() -> void:
	make_wall(Vector2(200, 0), Vector2(20, 400), 8, 2)
	await physics()
	var a: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	for i in 40:
		await physics()
		if a.get_bullet_collision_count(0) >= 1:
			break
	assert_gte(a.get_bullet_collision_count(0), 1, "first life registered the hit")
	await idle(1)
	a.multimesh_attach_time_based_function(0.5, _on_old_timer, false, true)
	assert_gte(a.debug_get_timer_count(), 1, "old-life timer attached")
	var spd := BulletSpeedData2D.new()
	spd.speed = 111.0
	spd.max_speed = 222.0
	a.set_bullet_speed_data(0, spd)
	for i in a.get_amount_bullets():
		a.disable_bullet(i)
	await idle(1)
	assert_gte(factory.debug_get_bullets_pool_amount(0), 1, "emptied volley parked pooled")

	factory.debug_reset_pool_stats()
	var b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	assert_gte(int(factory.debug_get_pool_hit_stats().get("directional_hits", 0)), 1, "second life is a pool hit")
	assert_eq(b.get_bullet_collision_count(0), 0, "collision count reset")
	assert_eq(b.debug_get_timer_count(), 0, "new life holds no timers")
	assert_eq(b.get_amount_active_attachments(), 0, "no attachments leak across")
	assert_almost_eq(b.get_bullet_speed_data(0).speed, 900.0, 0.01, "reseeded speed wins over the old edit")

	var rehit := false
	for i in 40:
		await physics()
		if b.get_bullet_collision_count(0) >= 1:
			rehit = true
			break
	assert_true(rehit, "dedup window is fresh: the same wall registers again")
	_timer_fired = false
	for i in 90:
		await physics()
		if _timer_fired:
			break
	assert_false(_timer_fired, "old 0.5s timer stayed dead after pooling")
