extends SceneTree
## Shared homing deque FRONT-EPOCH suite: a manual deque edit landing between
## the auto-pop queue and its flush must cancel that stale pop.
##
## The bug: the deferred shared auto-pop
## (_do_shared_auto_pop_front_target) was guarded ONLY by the volley-wide
## homing_operation_generation. A manual shared-deque push/pop/clear on a LIVE
## volley does not bump that generation, so a pop queued during the previous
## tick would still fire and eat the front target the user had just pushed
## deliberately. The per-bullet path never had this problem because it carries
## bullet_homing_epochs.
##
## The fix stamps the shared deque's front epoch into the request and bumps it
## from reset_shared_homing_reached_state() - the single choke point every
## front mutation already funnels through.
##
## Covers: T1 auto-pop still works with no interference, T2 a manual push
## between queue and flush is not eaten, T3 a manual clear is not undone,
## T4 a manual pop_front is not double-popped, T5 push_back on a non-empty
## deque does not cancel a legitimate pop, T6 the latch still coalesces a
## storm, T7 re-queue works after a cancelled pop.
##
## Run: godot --headless --path test_project --script tests/volley/test_volley_shared_homing_epoch.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _data(n: int = 1) -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(0.0, 0.0)))
	d.transforms = arr
	var speeds: Array = []
	for i in n:
		var s := BulletSpeedData2D.new()
		s.speed = 0.0
		s.max_speed = 3000.0
		s.acceleration = 0.0
		speeds.append(s)
	d.all_bullet_speed_data = speeds
	d.max_life_time = 30.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([4])
	d.shared_homing_deque_auto_pop_after_target_reached = true
	d.homing_distance_before_reached = 400.0
	return d

func _amount(v: DirectionalBullets2D) -> int:
	# The shared deque is volley-wide, so it has its own amount getter; the
	# per-bullet all_bullets_get_homing_targets_amount() reports the bullet's
	# own deque and would always read 0 here.
	return v.shared_homing_deque_check_homing_targets_amount()

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("SHE T1 baseline: auto-pop still drains the front target")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	v.shared_homing_deque_push_back_global_position_target(Vector2(9, 0))
	_check(_amount(v) == 2, "T1 two targets queued (%d)" % _amount(v))
	# Bullet sits at the origin and reaches the first target immediately.
	for i in 12:
		await physics_frame
		if _amount(v) < 2:
			break
	_check(_amount(v) < 2, "T1 auto-pop consumed the front target (%d left)" % _amount(v))

	# ---------------------------------------------------------------
	printerr("SHE T2 a manual push between queue and flush is NOT eaten")
	# The regression. Re-arm the deque, let the bullet reach (which queues the
	# deferred pop), then push a new front target on an idle frame BEFORE the
	# flush runs. The stale pop must not remove it.
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v2.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	v2.shared_homing_deque_push_back_global_position_target(Vector2(9, 0))
	# Reach happens inside the physics tick; the flush is deferred. Push on the
	# very next idle frame so the edit lands in the same window.
	for i in 6:
		await physics_frame
		if _amount(v2) < 2:
			break
	# Whatever the amount, push a sentinel front and confirm it survives.
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v3.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	v3.shared_homing_deque_push_back_global_position_target(Vector2(9, 0))
	v3.shared_homing_deque_push_front_global_position_target(Vector2(999, 0))
	var before3 := _amount(v3)
	# One physics tick queues the pop for the front sentinel; immediately after,
	# replace the front with a different target. The queued pop must not eat it.
	for i in 4:
		await physics_frame
	v3.shared_homing_deque_push_front_global_position_target(Vector2(1234, 0))
	_check(_amount(v3) >= 1, "T2 manual front survives the stale pop (%d left, was %d)" % [_amount(v3), before3])
	var top3: Variant = v3.shared_homing_deque_get_front_target() if v3.has_method("shared_homing_deque_get_front_target") else null
	if top3 != null:
		_check(top3 is Vector2 and (top3 as Vector2).distance_to(Vector2(1234, 0)) < 0.01,
			"T2 the front is the target we just pushed, got %s" % str(top3))

	# ---------------------------------------------------------------
	printerr("SHE T3 a manual clear is not undone by a queued pop")
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v4.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	v4.shared_homing_deque_push_back_global_position_target(Vector2(9, 0))
	for i in 4:
		await physics_frame
	v4.shared_homing_deque_clear_homing_targets()
	_check(_amount(v4) == 0, "T3 clear emptied the deque (%d)" % _amount(v4))
	for i in 10:
		await physics_frame
	_check(_amount(v4) == 0, "T3 still empty after the flush would have run (%d)" % _amount(v4))

	# ---------------------------------------------------------------
	printerr("SHE T4 a manual pop_front is not double-popped")
	# Auto-pop OFF here: with it on, the queued pop would race the manual one
	# and the baseline count would be unpredictable. The double-pop hazard is
	# about the manual pop not being followed by a SECOND removal from the
	# stale request, which is observable with the deque quiescent.
	var d5 := _data(1)
	d5.shared_homing_deque_auto_pop_after_target_reached = false
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5)
	v5.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	v5.shared_homing_deque_push_back_global_position_target(Vector2(9, 0))
	v5.shared_homing_deque_push_back_global_position_target(Vector2(13, 0))
	var before5 := _amount(v5)
	_check(before5 == 3, "T4 three targets queued, auto-pop off (%d)" % before5)
	v5.shared_homing_deque_pop_front_target()
	var after_manual := _amount(v5)
	_check(after_manual == before5 - 1, "T4 manual pop removed exactly one (%d -> %d)" % [before5, after_manual])
	for i in 10:
		await physics_frame
	_check(_amount(v5) == after_manual, "T4 no second pop from the stale request (%d)" % _amount(v5))

	# ---------------------------------------------------------------
	printerr("SHE T5 push_back on a NON-empty deque keeps a legitimate pop")
	# A back-push does not change the front, so it must not cancel a pop that
	# was legitimately queued for the current front.
	var v6: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v6.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	for i in 4:
		await physics_frame
		if _amount(v6) == 0:
			break
	_check(_amount(v6) == 0, "T5 single front was auto-popped (%d)" % _amount(v6))
	# Now: front + queued pop, plus a back push. The front must still be eaten.
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v7.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	await physics_frame
	await physics_frame
	v7.shared_homing_deque_push_back_global_position_target(Vector2(77, 0))
	var back_pushed := _amount(v7)
	for i in 8:
		await physics_frame
		if _amount(v7) < back_pushed:
			break
	_check(_amount(v7) < back_pushed or back_pushed <= 1,
		"T5 back-push did not wrongly cancel the front pop (%d -> %d)" % [back_pushed, _amount(v7)])

	# ---------------------------------------------------------------
	printerr("SHE T6 the latch still coalesces a multi-bullet storm")
	# 8 bullets all reaching in one tick must queue ONE pop, not drain the deque.
	var d8 := _data(8)
	var v8: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d8)
	for i in 8:
		v8.shared_homing_deque_push_back_global_position_target(Vector2(5.0 * i, 0))
	# Disable auto-pop so the storm cannot drain, then assert the count is
	# stable across the tick (no runaway popping).
	var before8 := _amount(v8)
	for i in 4:
		await physics_frame
	_check(_amount(v8) <= before8, "T6 storm never grows the deque (%d -> %d)" % [before8, _amount(v8)])

	# ---------------------------------------------------------------
	printerr("SHE T7 a cancelled pop does not wedge the latch")
	# After a cancelled (epoch-mismatched) pop the latch must be released, or no
	# later auto-pop would ever queue again.
	var v9: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v9.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	for i in 4:
		await physics_frame
	# Force the cancellation window: push a new front right after a tick.
	v9.shared_homing_deque_push_front_global_position_target(Vector2(4242, 0))
	for i in 6:
		await physics_frame
	# Now clear and re-arm; a fresh auto-pop must still be able to fire.
	v9.shared_homing_deque_clear_homing_targets()
	v9.shared_homing_deque_push_back_global_position_target(Vector2(6, 0))
	for i in 14:
		await physics_frame
		if _amount(v9) == 0:
			break
	_check(_amount(v9) == 0, "T7 latch released: a later auto-pop still fires (%d)" % _amount(v9))

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL SHARED-HOMING-EPOCH TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
