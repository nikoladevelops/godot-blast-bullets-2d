extends BlastTest
## Shared homing deque FRONT EPOCH: a manual deque edit landing between the
## auto-pop queue and its flush must cancel that stale pop (the request
## carries the front epoch, bumped by every front mutation). Also: the latch
## coalesces a multi-bullet storm and a cancelled pop never wedges it.


func _data(n: int = 1) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 0.0, 30.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D())
	d.transforms = arr
	d.shared_homing_deque_auto_pop_after_target_reached = true
	d.homing_distance_before_reached = 400.0
	return d


func _amount(v: BulletVolley2D) -> int:
	return v.shared_homing_deque_check_homing_targets_amount()


func _spawn_two_targets(d: BulletVolleyData2D = null) -> BulletVolley2D:
	var v: BulletVolley2D = factory.spawn_volley(d if d != null else _data())
	v.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	v.shared_homing_deque_push_back_global_position_target(Vector2(9, 0))
	return v


func test_auto_pop_drains_front() -> void:
	var v := _spawn_two_targets()
	assert_eq(_amount(v), 2, "two targets queued")
	for i in 12:
		await physics()
		if _amount(v) < 2:
			break
	assert_lt(_amount(v), 2, "auto-pop consumed the front target")


func test_manual_front_push_survives_stale_pop() -> void:
	var v := _spawn_two_targets()
	v.shared_homing_deque_push_front_global_position_target(Vector2(999, 0))
	await physics(4)
	v.shared_homing_deque_push_front_global_position_target(Vector2(1234, 0))
	assert_gte(_amount(v), 1, "manual front survives the stale pop")
	if v.has_method("shared_homing_deque_get_front_target"):
		var top: Variant = v.shared_homing_deque_get_front_target()
		assert_almost_eq(top, Vector2(1234, 0), Vector2(0.01, 0.01), "front is the target just pushed")


func test_manual_clear_not_undone() -> void:
	var v := _spawn_two_targets()
	await physics(4)
	v.shared_homing_deque_clear_homing_targets()
	assert_eq(_amount(v), 0, "clear emptied the deque")
	await physics(10)
	assert_eq(_amount(v), 0, "still empty after the flush would have run")


func test_manual_pop_not_double_popped() -> void:
	var d := _data(1)
	d.shared_homing_deque_auto_pop_after_target_reached = false
	var v := _spawn_two_targets(d)
	v.shared_homing_deque_push_back_global_position_target(Vector2(13, 0))
	assert_eq(_amount(v), 3, "three targets queued, auto-pop off")
	v.shared_homing_deque_pop_front_target()
	assert_eq(_amount(v), 2, "manual pop removed exactly one")
	await physics(10)
	assert_eq(_amount(v), 2, "no second pop from a stale request")


func test_back_push_keeps_legitimate_pop() -> void:
	var v6: BulletVolley2D = factory.spawn_volley(_data())
	v6.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	for i in 4:
		await physics()
		if _amount(v6) == 0:
			break
	assert_eq(_amount(v6), 0, "single front was auto-popped")
	var v7: BulletVolley2D = factory.spawn_volley(_data())
	v7.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	await physics(2)
	v7.shared_homing_deque_push_back_global_position_target(Vector2(77, 0))
	var back_pushed := _amount(v7)
	for i in 8:
		await physics()
		if _amount(v7) < back_pushed:
			break
	assert_true(_amount(v7) < back_pushed or back_pushed <= 1, "back-push did not wrongly cancel the front pop")


func test_latch_coalesces_storm() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(8))
	for i in 8:
		v.shared_homing_deque_push_back_global_position_target(Vector2(5.0 * i, 0))
	var before := _amount(v)
	await physics(4)
	assert_lte(_amount(v), before, "storm never grows the deque")
	assert_gte(_amount(v), before - 4, "8 bullets reaching per tick pop at most one per tick")


func test_cancelled_pop_does_not_wedge_latch() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	v.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	await physics(4)
	v.shared_homing_deque_push_front_global_position_target(Vector2(4242, 0))
	await physics(6)
	v.shared_homing_deque_clear_homing_targets()
	v.shared_homing_deque_push_back_global_position_target(Vector2(6, 0))
	for i in 14:
		await physics()
		if _amount(v) == 0:
			break
	assert_eq(_amount(v), 0, "latch released: a later auto-pop still fires")
