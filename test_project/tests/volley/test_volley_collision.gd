extends BlastTest
## Volley collision with REAL physics: 1 bullet at 300 px/s +X toward a wall
## body + Area2D at x=200 (layer 3). body_entered fires with the
## slim payload (body, volley, index), the bullet disables at max 1, max 0 is
## infinite, and disable+wake keeps the factory consistent.


func _bodied_data() -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 300.0, 10.0)
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	return d


func before_each() -> void:
	await super()
	make_wall(Vector2(200, 0))
	make_area(Vector2(200, 0))
	watch_signals(factory)
	await physics()


func _wait_until(cond: Callable, frames := 90) -> void:
	for i in frames:
		await physics()
		if cond.call():
			return


func test_body_entered_kills_at_max_one() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data())
	assert_not_null(v, "walled spawn ok")
	await _wait_until(func(): return not v.is_bullet_status_enabled(0))
	assert_signal_emitted(factory, "body_entered", "body signal fired")
	var p = get_signal_parameters(factory, "body_entered", 0)
	assert_eq(p[1], v, "payload volley")
	assert_eq(int(p[2]), 0, "payload index 0")
	assert_false(v.is_bullet_status_enabled(0), "bullet disabled at max 1")
	assert_gte(v.get_bullet_collision_count(0), 1, "collision count tracked")
	# The Area2D sibling legitimately stays silent: max 1 means the first
	# record (body) kills the bullet and the area record for the dead slot is
	# skipped. Multi-hit volleys report both (next test).


func test_max_two_reports_body_and_area() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data())
	v.set_bullet_max_collision_count(2)
	await _wait_until(func(): return get_signal_emit_count(factory, "body_entered") >= 1 \
		and get_signal_emit_count(factory, "area_entered") >= 1)
	assert_signal_emitted(factory, "body_entered", "body reported on multi-hit volley")
	assert_signal_emitted(factory, "area_entered", "area reported on multi-hit volley")


func test_max_zero_is_infinite() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data())
	v.set_bullet_max_collision_count(0)
	await _wait_until(func(): return get_signal_emit_count(factory, "body_entered") >= 2)
	assert_signal_emitted(factory, "body_entered", "infinite bullet still reports hits")
	assert_true(v.is_bullet_status_enabled(0), "infinite bullet stays alive through the wall")


func test_disable_wake_same_life() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data())
	v.set_is_auto_pooling_enabled(false) # a drained 1-bullet volley parks instead of pooling
	v.disable_bullet(0)
	assert_false(v.is_bullet_status_enabled(0), "manual disable holds")
	v.wake_bullet(0)
	assert_true(v.is_bullet_status_enabled(0), "wake revives")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after wake")
