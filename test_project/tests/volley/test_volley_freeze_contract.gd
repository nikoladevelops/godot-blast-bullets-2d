extends BlastTest
## Freeze contract. disable_bullet() FREEZES a bullet: it stops rendering,
## colliding and moving, and keeps every runtime value (ballistics, hit
## count, homing queue, orbit, fall speed, custom data, attachment if asked)
## for enable_bullet() to RESUME exactly. State is cleared only on request
## (reset_state / bullet_reset_state) or when the volley goes back to the
## pool. The last bullet out PARKS the volley when is_auto_pooling_enabled is
## off (frozen, still owned, wakeable) or POOLS it when on (released; waking
## that stale handle is refused). get_life_id() changes once per life.


func _data(n: int = 2, speed: float = 200.0, lifetime: float = 10.0) -> BulletVolleyData2D:
	return H.make_volley_data(n, speed, lifetime)


func _info(v: BulletVolley2D, i: int) -> Dictionary:
	var d: Dictionary = v.debug_get_bullet_info(i)
	d.erase("active")
	return d


func test_a_frozen_bullet_keeps_every_runtime_value() -> void:
	var d := _data()
	d.gravity = Vector2(0, 50)
	var c0 := Resource.new()
	d.all_bullets_custom_data = [c0, Resource.new()]
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.set_bullet_max_collision_count(5)
	v.set_bullet_collision_count(0, 2)
	v.set_homing_take_control_of_texture_rotation(true)
	v.bullet_homing_push_back_global_position_target(0, Vector2(500, 300))
	await physics(10)
	var before := _info(v, 0)
	var pose: Transform2D = v.get_bullet_transform(0)
	v.disable_bullet(0)
	await physics(10)
	assert_false(v.is_bullet_status_enabled(0), "frozen")
	assert_eq(_info(v, 0), before, "every runtime value is kept while frozen")
	assert_eq(v.get_bullet_transform(0), pose, "a frozen bullet does not move")
	assert_eq(v.get_bullet_collision_count(0), 2, "hit count kept")
	assert_same(v.bullet_get_custom_data(0), c0, "custom data kept")
	v.enable_bullet(0)
	assert_eq(_info(v, 0), before, "the wake resumes every value")
	assert_eq(v.get_bullet_collision_count(0), 2, "wake keeps the hit count (collision_amount -1)")
	await physics(2)
	assert_gt(v.get_bullet_transform(0).origin.distance_to(pose.origin), 1.0, "it flies on from the frozen pose")


func test_a_frozen_bullet_never_collides_and_a_wake_reports_the_overlap_once() -> void:
	make_area(Vector2(0, 0), Vector2(40, 40), 8)
	var hits: Array = []
	factory.area_entered.connect(func(_a, _v, idx: int): hits.append(idx))
	var d := H.make_still_data(1)
	d.set_collision_mask_from_array([4])
	d.monitorable = true
	d.bullet_max_collision_count = 0
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.set_is_auto_pooling_enabled(false)
	v.disable_bullet(0)
	await physics(5)
	assert_eq(hits, [], "a frozen bullet inside an area reports nothing")
	v.enable_bullet(0)
	await physics(5)
	assert_eq(hits, [0], "the woken bullet reports the overlap exactly once")


func test_reset_state_clears_only_the_ledgers() -> void:
	var d := _data()
	d.gravity = Vector2(0, 80)
	var c0 := Resource.new()
	d.all_bullets_custom_data = [c0, Resource.new()]
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.set_bullet_max_collision_count(5)
	v.set_bullet_collision_count(0, 3)
	v.bullet_homing_push_back_global_position_target(0, Vector2(900, 0))
	v.bullet_enable_orbiting(0, 60.0, BulletVolley2D.OrbitRight, BulletVolley2D.FaceTarget)
	await physics(10)
	assert_gt(v.bullet_get_fall_speed(0), 0.0, "fell for a while")
	var speed: float = v.get_bullet_speed_data(0).speed
	var direction: Vector2 = v.get_bullet_direction(0)
	v.bullet_reset_state(0)
	assert_eq(v.get_bullet_collision_count(0), 0, "hit count cleared")
	assert_eq(v.bullet_homing_check_targets_amount(0), 0, "homing queue cleared")
	assert_false(v.bullet_is_orbiting_enabled(0), "orbit cleared")
	assert_eq(v.bullet_get_fall_speed(0), 0.0, "fall speed cleared")
	assert_almost_eq(v.get_bullet_speed_data(0).speed, speed, 0.001, "speed kept")
	assert_eq(v.get_bullet_direction(0), direction, "direction kept")
	assert_same(v.bullet_get_custom_data(0), c0, "custom data kept")
	assert_true(v.is_bullet_status_enabled(0), "a live bullet stays live")


func test_disable_with_reset_state_and_reset_of_a_frozen_bullet() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	v.set_bullet_max_collision_count(5)
	v.set_bullet_collision_count(0, 3)
	v.set_bullet_collision_count(1, 2)
	v.disable_bullet(0, true, true)
	assert_eq(v.get_bullet_collision_count(0), 0, "disable_bullet(i, true, true) resets")
	v.disable_bullet(1)
	assert_eq(v.get_bullet_collision_count(1), 2, "plain disable keeps")


func test_wake_collision_amount_keeps_sets_and_clamps() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	v.set_bullet_max_collision_count(5)
	v.set_bullet_collision_count(0, 2)
	v.disable_bullet(0)
	v.enable_bullet(0)
	assert_eq(v.get_bullet_collision_count(0), 2, "-1 (default) keeps")
	v.disable_bullet(0)
	v.enable_bullet(0, 0)
	assert_eq(v.get_bullet_collision_count(0), 0, "0 sets a fresh bullet")
	v.disable_bullet(0)
	v.enable_bullet(0, 99)
	assert_eq(v.get_bullet_collision_count(0), 4, "at/above max leaves exactly one hit")


func test_drain_with_pooling_off_parks_and_a_wake_resumes_the_whole_volley() -> void:
	var sp := make_spawner(_data(2), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	watch_signals(sp)
	assert_true(sp.shoot_once(), "fired")
	var v: BulletVolley2D = get_signal_parameters(sp, "volley_fired", 0)[0]
	v.set_is_auto_pooling_enabled(false)
	var fired: Array = []
	v.attach_time_based_function(0.3, func(): fired.append(Engine.get_physics_frames()))
	v.set_gravity(Vector2(0, 30))
	var life_id := v.get_life_id()
	v.disable_bullet(0)
	v.disable_bullet(1)
	await idle(1)
	assert_true(v.is_parked(), "parked")
	assert_eq(v.debug_get_life_state(), "parked", "life state")
	assert_false(v.is_pooled(), "not pooled")
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "the pool holds nothing")
	assert_eq(int(v.debug_get_volley_info().get("owner_spawner_id", 0)), sp.get_instance_id(), "the owner is kept")
	assert_eq(v.debug_get_timer_count(), 1, "timers are kept")
	assert_eq(v.get_gravity(), Vector2(0, 30), "volley features are kept")
	v.enable_bullet(1)
	assert_true(v.debug_get_volley_info().get("is_active", false), "the wake resumes the volley")
	assert_eq(v.get_life_id(), life_id, "same life")
	for i in 40:
		await physics()
		if not fired.is_empty():
			break
	assert_eq(fired.size(), 1, "the kept timer fires once the volley runs again")


func test_an_expired_parked_volley_wakes_with_a_fresh_lifetime() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1, 0.0, 0.2))
	v.set_is_auto_pooling_enabled(false)
	for i in 40:
		await physics()
		if not v.is_bullet_status_enabled(0):
			break
	assert_true(v.is_parked(), "expiry parked the volley")
	assert_eq(v.get_life_time_left(), 0.0, "nothing left")
	v.enable_bullet(0)
	assert_almost_eq(v.get_life_time_left(), 0.2, 0.0001, "fresh lifetime")
	assert_eq(float(v.debug_get_clocks().get("curves_elapsed_time", -1.0)), 0.0, "curve clock restarts with it")


func test_a_manual_drain_keeps_the_remaining_lifetime() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1, 0.0, 5.0))
	v.set_is_auto_pooling_enabled(false)
	await physics(30)
	var left := v.get_life_time_left()
	var clock := float(v.debug_get_clocks().get("curves_elapsed_time", -1.0))
	assert_lt(left, 5.0, "time passed")
	v.disable_bullet(0)
	await physics(30)
	assert_almost_eq(v.get_life_time_left(), left, 0.0001, "a parked volley's lifetime stands still")
	v.enable_bullet(0)
	assert_almost_eq(v.get_life_time_left(), left, 0.0001, "the wake keeps the remaining time")
	assert_almost_eq(float(v.debug_get_clocks().get("curves_elapsed_time", -1.0)), clock, 0.0001, "and the curve clock")


func test_a_suspended_attachment_is_released_when_the_volley_pools() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(2))
	v.all_bullets_set_attachment(make_probe_scene(), Vector2.ZERO, true)
	var probe0 = v.bullet_get_attachment(0)
	v.disable_bullet(0, false)
	assert_same(v.bullet_get_attachment(0), probe0, "suspended attachment kept")
	assert_eq(int(probe0.get("disable_calls")), 1, "told once")
	v.disable_bullet(1)
	assert_true(v.is_pooled(), "drained with pooling on")
	assert_null(v.bullet_get_attachment(0), "the pool release returned the suspended attachment")
	assert_eq(int(probe0.get("disable_calls")), 1, "no second disable for an already-suspended attachment")
	assert_eq(factory.debug_get_active_attachments_amount(), 0, "no attachment left active")


func test_configure_a_frozen_bullet_then_wake_it() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(2, 200.0))
	v.set_homing_take_control_of_texture_rotation(true)
	v.disable_bullet(0)
	assert_true(v.bullet_homing_push_back_global_position_target(0, Vector2(0, 600)), "a frozen bullet accepts targets")
	expect_no_errors()
	assert_eq(v.bullet_homing_check_targets_amount(0), 1, "queued for the wake")
	v.enable_bullet(0)
	await physics(30)
	assert_gt(v.get_bullet_direction(0).y, 0.3, "the woken bullet steers toward the queued target")


## The freeze contract covers orbiting too (what the docs state): a frozen
## bullet accepts orbit arming and a new configuration in place, and a locked
## frozen bullet re-pins its center. Only a pooled volley refuses (stale).
func test_a_frozen_bullet_accepts_orbit_configuration() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	v.set_homing_take_control_of_texture_rotation(true)
	v.bullet_homing_push_back_global_position_target(0, Vector2(0, 300))
	v.bullet_enable_orbiting(0, 40.0)
	for i in 240:
		await physics(1)
		if v.bullet_is_orbiting_locked(0):
			break
	assert_true(v.bullet_is_orbiting_locked(0), "the ring locked")
	v.bullet_set_orbiting_lock_policy(0, BulletVolley2D.StayLocked) # keeps the lock across a re-arm
	v.disable_bullet(0)
	assert_true(v.bullet_is_orbiting_enabled(0), "a frozen bullet keeps its orbit armed")
	v.bullet_enable_orbiting(0, 40.0, BulletVolley2D.OrbitRight, BulletVolley2D.FaceTarget, BulletVolley2D.FollowTarget, 0.0, BulletVolley2D.StayLocked, true)
	assert_true(v.bullet_is_orbiting_locked(0), "a frozen bullet accepts a re-arm and StayLocked keeps the lock")
	v.bullet_set_orbiting_radius(0, 60.0)
	assert_almost_eq(v.bullet_get_orbiting_radius(0), 60.0, 0.01, "a frozen bullet takes new orbit settings in place")
	v.all_bullets_set_orbiting_center(Vector2(77, 77), 0, 0)
	assert_almost_eq(v.bullet_get_orbiting_center(0).distance_to(Vector2(77, 77)), 0.0, 0.01, "a locked frozen bullet re-pins")
	expect_no_errors()


func test_life_id_changes_once_per_life() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1))
	var first := v.get_life_id()
	assert_eq(first, int(v.debug_get_volley_info().get("generation", -1)), "life id is the generation")
	v.disable_bullet(0)
	assert_true(v.is_pooled(), "pooled")
	assert_eq(v.get_life_id(), first, "pooling ends a life but does not start one")
	var reused: BulletVolley2D = factory.spawn_volley(_data(1))
	assert_same(reused, v, "pool reuse")
	assert_eq(reused.get_life_id(), first + 1, "exactly one bump per new life")


func test_waking_a_pooled_volley_is_refused() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1))
	v.disable_bullet(0)
	v.enable_bullet(0)
	expect_error_sequence(["enable_bullet: this volley went back to the pool"])
	assert_false(v.is_bullet_status_enabled(0), "still dead")
	assert_true(v.is_pooled(), "still pooled")


func test_auto_pooling_toggles_between_parked_and_pooled() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1))
	v.set_is_auto_pooling_enabled(false)
	v.disable_bullet(0)
	assert_true(v.is_parked(), "parked")
	v.set_is_auto_pooling_enabled(true)
	assert_true(v.is_pooled(), "turning pooling on pools a parked volley at once")
	v.set_is_auto_pooling_enabled(false)
	assert_true(v.is_pooled(), "turning it off again never un-pools (the flag governs the next drain)")


## Every way into a new life bumps the generation exactly once: three pool
## reuses and an explicit `enable_volley` wake, while pooling itself never
## bumps it, and waking a PARKED volley keeps the life (it never ended).
func test_every_new_life_bumps_the_generation_once() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1))
	var life := v.get_life_id()
	for cycle in 3:
		v.disable_bullet(0)
		assert_true(v.is_pooled(), "cycle %d pooled" % cycle)
		assert_eq(v.get_life_id(), life, "pooling ends a life without starting one")
		var reused: BulletVolley2D = factory.spawn_volley(_data(1))
		assert_same(reused, v, "cycle %d reuses the instance" % cycle)
		life += 1
		assert_eq(reused.get_life_id(), life, "cycle %d is exactly one generation later" % cycle)
		v = reused
	v.disable_bullet(0)
	await idle(1)
	assert_true(v.is_pooled(), "pooled before the manual wake")
	var data := _data(1)
	assert_true(v.enable_volley(data, Vector2.ZERO, 0), "enable_volley starts a life on the pooled instance")
	assert_eq(v.get_life_id(), life + 1, "the manual wake bumps the generation exactly once")
	assert_eq(v.debug_get_life_state(), "active", "and the volley is active again")
	var parked: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 0.0, 30.0))
	parked.set_is_auto_pooling_enabled(false)
	parked.disable_bullet(0)
	parked.disable_bullet(1)
	assert_eq(parked.debug_get_life_state(), "parked", "the second volley parks")
	var parked_life := parked.get_life_id()
	parked.enable_bullet(0)
	assert_eq(parked.get_life_id(), parked_life, "a parked wake keeps the same life")


## The life state is ONE truth table: debug_get_life_state() agrees with
## is_pooled / is_parked / is_volley_active / is_queued_for_deletion and with
## the volley info's is_active / is_pooled flags in every state.
func _state_row(v: BulletVolley2D) -> Array:
	var info: Dictionary = v.debug_get_volley_info()
	return [
		v.debug_get_life_state(),
		v.is_pooled(),
		v.is_parked(),
		v.is_volley_active(),
		v.is_queued_for_deletion(),
		bool(info.get("is_active", false)),
		bool(info.get("is_pooled", false)),
	]


func test_the_life_state_truth_table_holds_across_the_whole_cycle() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(2))
	assert_eq(_state_row(v), ["active", false, false, true, false, true, false], "at spawn")
	v.disable_bullet(0)
	assert_eq(_state_row(v), ["active", false, false, true, false, true, false], "one bullet left")
	v.disable_bullet(1)
	assert_eq(_state_row(v), ["pooled", true, false, false, false, false, true], "the last bullet out pools it")
	var reused: BulletVolley2D = factory.spawn_volley(_data(2))
	assert_same(reused, v, "the pool hands it back")
	assert_eq(_state_row(v), ["active", false, false, true, false, true, false], "a reuse is active again")
	v.set_is_auto_pooling_enabled(false)
	v.disable_bullet(0)
	v.disable_bullet(1)
	assert_eq(_state_row(v), ["parked", false, true, false, false, false, false], "with pooling off it parks")
	v.enable_bullet(1)
	assert_eq(_state_row(v), ["active", false, false, true, false, true, false], "a single wake makes it active")
	assert_eq(_state_row(v)[0], v.debug_get_life_state(), "the state string is the last word")


## The drain hide runs the volley's visibility_changed handlers (Godot emits
## it synchronously). A release-path free there must be refused by the drain
## guard and finished with queue_free, never deleting the volley mid-drain.
func test_a_visibility_handler_freeing_the_volley_on_drain_is_refused() -> void:
	var v: BulletVolley2D = quick_volley(1, 0.0)
	var fired := []
	v.visibility_changed.connect(func() -> void:
		if fired.is_empty():
			fired.append(true)
			BulletFactory2D.debug_free_like_release(v))
	v.disable_bullet(0) # the last bullet: the volley hides and drains
	assert_eq(fired, [true], "the handler ran from the drain hide")
	assert_true(is_instance_valid(v), "the release-path free was refused")
	assert_true(v.is_queued_for_deletion(), "and finished with queue_free")
	expect_no_errors()
	await idle(2)
	assert_false(is_instance_valid(v), "gone at frame end")


## Waking a parked volley shows it; a release-path free from that handler is
## refused and the wake is not left half-applied.
func test_a_visibility_handler_freeing_a_wake_is_refused() -> void:
	var v: BulletVolley2D = quick_volley(1, 0.0)
	v.set_is_auto_pooling_enabled(false)
	var fired := []
	v.visibility_changed.connect(func() -> void:
		if v.visible and fired.is_empty():
			fired.append(true)
			BulletFactory2D.debug_free_like_release(v))
	v.disable_bullet(0) # park: the hide fires while visible is already false
	assert_true(v.is_parked(), "parked")
	v.enable_bullet(0) # wake: set_visible(true)
	assert_eq(fired, [true], "the handler ran from the wake show")
	assert_true(is_instance_valid(v), "the release-path free was refused during the wake")
	assert_true(v.is_queued_for_deletion(), "finished with queue_free")
	expect_no_errors()
	await idle(2)
	assert_false(is_instance_valid(v), "gone at frame end")


## Reusing a pooled volley shows it; a surviving visibility handler freeing it
## must be refused and the spawn must still return a usable volley.
func test_a_visibility_handler_freeing_a_pooled_reuse_is_refused() -> void:
	var data := H.make_volley_data(1, 0.0, 30.0)
	var v: BulletVolley2D = factory.spawn_volley(data)
	var fired := []
	v.visibility_changed.connect(func() -> void:
		if v.visible and fired.is_empty():
			fired.append(true)
			BulletFactory2D.debug_free_like_release(v))
	v.disable_bullet(0) # drain -> pooled (the hide fires while invisible already)
	await idle(1)
	assert_true(v.is_pooled(), "pooled")
	var again: BulletVolley2D = factory.spawn_volley(data) # reuse: set_visible(true)
	assert_eq(fired, [true], "the handler ran from the reuse show")
	assert_true(is_instance_valid(again), "a usable volley came back")
	assert_false(again.is_queued_for_deletion(), "the returned one is not queued")
	expect_no_errors()
	await idle(2)
	assert_false(is_instance_valid(v), "the refused one died at frame end")


## release_life drops the life's user resources; a script resource whose
## PREDELETE frees the volley must stop the release, not leave it writing
## into freed memory (the release-path free the other setters already guard).
func test_a_resource_predelede_freeing_the_volley_during_release_is_survived() -> void:
	var data := H.make_volley_data(1, 0.0, 30.0)
	var v: BulletVolley2D = factory.spawn_volley(data)
	var probe := FreeVolleyOnPredeleteSpeed2D.new()
	FreeVolleyOnPredeleteSpeed2D.victim = v
	FreeVolleyOnPredeleteSpeed2D.fired = 0
	v.set_shared_bullet_speed_data(probe)
	probe = null
	v.disable_bullet(0) # drain -> release_life unrefs the speed resource -> PREDELETE -> free()
	assert_gt(FreeVolleyOnPredeleteSpeed2D.fired, 0, "the PREDELETE handler ran during the release")
	await idle(2)
	assert_not_null(quick_volley(1), "the factory still works")
	expect_no_errors()
