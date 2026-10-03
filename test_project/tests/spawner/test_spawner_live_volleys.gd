extends BlastTest
## Live-volley API: adopt_live_volley rejects bad instances loudly,
## clear_live_volleys_homing really empties the queues, and
## override_live_volleys_velocity changes flight (and rejects NaN).

var target: Node2D


func before_each() -> void:
	await super()
	target = Node2D.new()
	target.position = Vector2(400, 0)
	add(target)


func _homing_spawner() -> BulletSpawner2D:
	var sp := make_spawner(H.make_directional_data(2, 50.0, 30.0))
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	sp.set_homing_target_path(sp.get_path_to(target))
	return sp


func test_adopt_rejects_null_pooled_and_dying_volleys() -> void:
	var sp := _homing_spawner()
	assert_false(sp.adopt_live_volley(null), "null")
	expect_error_sequence(["adopt_live_volley: instance is null"])
	var pooled := spawn_dir(2)
	await idle(1)
	factory.clear_active_bullets()
	await idle(1)
	assert_false(sp.adopt_live_volley(pooled), "pooled")
	expect_error_sequence(["adopt_live_volley: instance is not live"])
	var dying := spawn_dir(2)
	dying.queue_free()
	assert_false(sp.adopt_live_volley(dying), "queued for deletion")
	expect_error_sequence(["adopt_live_volley: instance is queued for deletion"])
	assert_eq(sp.get_live_volley_count(), 0, "nothing adopted")


func test_clear_live_volleys_homing_empties_the_queues() -> void:
	var sp := _homing_spawner()
	assert_true(sp.shoot_once(), "homing shot")
	var v: DirectionalBullets2D = sp.get_live_volleys()[0]
	assert_eq(v.shared_homing_deque_get_current_homing_target(), target, "chasing before")
	assert_eq(sp.clear_live_volleys_homing(), 1, "one volley cleared")
	assert_null(v.shared_homing_deque_get_current_homing_target(), "queue empty after")


func test_override_velocity_changes_flight_and_rejects_nan() -> void:
	var sp := _homing_spawner()
	sp.set_homing_retarget_mode(BulletSpawner2D.HOMING_RETARGET_OFF) # no re-aim mid-measure
	assert_true(sp.shoot_once(), "shot")
	var v: DirectionalBullets2D = sp.get_live_volleys()[0]
	sp.clear_live_volleys_homing() # straight flight from here
	assert_eq(sp.override_live_volleys_velocity(Vector2(0, 600)), 1, "one volley overridden")
	var before: Vector2 = v.get_bullet_transform(0).origin
	await physics(6)
	var moved: Vector2 = v.get_bullet_transform(0).origin - before
	assert_gt(moved.y, 40.0, "flies along the new velocity (+Y)")
	assert_lt(absf(moved.x), 5.0, "no leftover sideways motion")
	sp.override_live_volleys_velocity(Vector2(NAN, 0))
	expect_error_sequence(["override_live_volleys_velocity: velocity must be finite"])
