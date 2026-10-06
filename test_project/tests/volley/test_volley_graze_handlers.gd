extends BlastTest
## Live graze handler contract: graze signals fire inside the factory tick
## with the bullet alive (status, pose, velocity, custom data readable);
## whatever a handler changes is respected by the rest of the batch (a
## disabled bullet, a freed or queued target, replaced zones, a freed or
## queued volley end their events, never a crash); a pause lets the batch
## finish and stops the sweep; events an early tick return left queued are
## dispatched once on the next tick; structural calls are refused with the
## deferred hint while handlers may spawn; an unhandled graze warns once.


func test_handler_sees_a_live_bullet() -> void:
	make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO, Vector2(5, 0)]), 0.0)
	var data := [Resource.new(), Resource.new()]
	v.bullet_set_custom_data(0, data[0])
	v.bullet_set_custom_data(1, data[1])
	v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	var seen: Array = []
	factory.bullet_grazed.connect(func(_t: Node2D, vol: BulletVolley2D, i: int, _z: BulletGrazeZone2D, _r: int) -> void:
		seen.append([i, vol.is_bullet_status_enabled(i), vol.get_bullet_transform(i).origin, vol.bullet_get_custom_data(i) == data[i], vol.is_bullet_inside_graze(i, 0), vol.get_bullet_grazed_rings(i, 0)]))
	step_factory()
	assert_eq(seen, [[0, true, Vector2.ZERO, true, true, 1], [1, true, Vector2(5, 0), true, true, 1]], "alive, in place, own custom data, state already updated")


func test_handler_disabling_the_bullet_skips_its_later_events() -> void:
	make_graze_target(Vector2(150, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO, Vector2(0, 1)]), 600.0)
	v.graze_set_zones([H.make_graze_zone([30.0, 20.0, 10.0])], &"graze_targets")
	var log := H.record_graze(factory)
	factory.bullet_grazed.connect(func(_t: Node2D, vol: BulletVolley2D, i: int, _z: BulletGrazeZone2D, r: int) -> void:
		if i == 0 and r == 0:
			vol.disable_bullet(0))
	step_factory(1, 0.5)
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:1:0", "enter:1:1", "enter:1:2", "exit:1:2"], "bullet 0 reports nothing after its handler disabled it; bullet 1 is untouched")


func test_handler_waking_the_bullet_again_still_drops_its_stale_events() -> void:
	make_graze_target(Vector2(150, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO, Vector2(0, 1)]), 600.0)
	v.graze_set_zones([H.make_graze_zone([30.0, 20.0])], &"graze_targets")
	var log := H.record_graze(factory)
	factory.bullet_grazed.connect(func(_t: Node2D, vol: BulletVolley2D, i: int, _z: BulletGrazeZone2D, r: int) -> void:
		if i == 0 and r == 0:
			vol.disable_bullet(0)
			vol.enable_bullet(0))
	step_factory(1, 0.5)
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:1:0", "enter:1:1", "exit:1:1"], "the woken bullet is a new slot life: the rest of its old batch is dropped")
	assert_true(v.is_bullet_status_enabled(0), "awake")


func _two_bullets_one_target() -> Array:
	var t := make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO, Vector2(1, 0)]), 0.0)
	v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	return [t, v]


func test_handler_freeing_the_target_skips_events_naming_it() -> void:
	var pair := _two_bullets_one_target()
	var log := H.record_graze(factory)
	factory.bullet_grazed.connect(func(t: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
		t.free())
	step_factory(3)
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the second bullet's event names a freed target: dropped")
	assert_false(is_instance_valid(pair[0]), "freed")


func test_handler_queue_freeing_the_target_skips_events_naming_it() -> void:
	var pair := _two_bullets_one_target()
	var log := H.record_graze(factory)
	factory.bullet_grazed.connect(func(t: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
		t.queue_free())
	step_factory(3)
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "a target queued for deletion is not grazed")


func test_target_freed_between_volleys_of_one_sweep() -> void:
	var t := make_graze_target(Vector2(10, 0))
	var first := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var second := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	for v in [first, second]:
		v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	var log := H.record_graze(factory)
	factory.bullet_grazed.connect(func(target: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
		target.free())
	step_factory(2)
	assert_eq(log.size(), 1, "the first volley's handler freed the target; the second volley never tests the snapshot's dead id")
	assert_eq(log[0][2], first, "the first volley grazed")
	assert_false(is_instance_valid(t), "freed")
	assert_eq(second.get_bullet_grazed_rings(0, 0), 0, "the second volley's Once ring was not spent on the dead target")
	make_graze_target(Vector2(10, 0))
	step_factory()
	assert_eq(log.size(), 2, "so it grazes the next target")
	assert_eq(log[1][2], second, "the second volley")


func test_target_freed_by_any_live_handler_is_never_tested_later_in_the_sweep() -> void:
	# Not a graze handler this time: a lifetime handler of an earlier volley
	# frees the target after this sweep's target list was built.
	var t := make_graze_target(Vector2(10, 0))
	var expiring_data := H.make_volley_data(1, 0.0, 0.01) # dies within the first tick
	expiring_data.transforms = H.transforms_at([Vector2(0, 500)])
	expiring_data.is_life_time_over_signal_enabled = true
	var expiring: BulletVolley2D = factory.spawn_volley(expiring_data)
	expiring.graze_set_zones([H.make_graze_zone([5.0])], &"graze_targets") # builds the list first
	var later := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	later.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	var log := H.record_graze(factory)
	factory.life_time_over.connect(func(_v: BulletVolley2D, _i: Array) -> void:
		if is_instance_valid(t):
			t.free())
	step_factory()
	assert_false(is_instance_valid(t), "the lifetime handler freed the target")
	assert_eq(log, [], "the later volley never grazed the dead target")
	assert_eq(later.get_bullet_grazed_rings(0, 0), 0, "nor spent its Once ring on it")


func test_handler_queue_freeing_the_volley_stops_dispatch() -> void:
	var pair := _two_bullets_one_target()
	var log := H.record_graze(factory)
	factory.bullet_grazed.connect(func(_t: Node2D, v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
		v.queue_free())
	step_factory(2)
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "a volley queued for deletion reports nothing more")
	await idle()
	assert_false(is_instance_valid(pair[1]), "the volley is gone")


func test_handler_freeing_the_volley_is_survived() -> void:
	var pair := _two_bullets_one_target()
	var second := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	second.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	var log := H.record_graze(factory)
	var first: BulletVolley2D = pair[1]
	var first_id := first.get_instance_id()
	factory.bullet_grazed.connect(func(_t: Node2D, v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
		if v.get_instance_id() == first_id:
			v.free())
	step_factory(2)
	assert_false(is_instance_valid(first), "freed against the contract")
	assert_eq(log.size(), 2, "the freed volley stopped after its first event; the next volley still grazed")
	assert_eq(log[1][2], second, "the sweep continued with the second volley")


func test_handler_pausing_the_factory_finishes_the_batch() -> void:
	make_graze_target(Vector2(10, 0))
	var first := graze_volley(H.transforms_at([Vector2.ZERO, Vector2(1, 0)]), 0.0)
	var second := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	for v in [first, second]:
		v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	var log := H.record_graze(factory)
	factory.bullet_grazed.connect(func(_t: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
		factory.is_factory_processing_bullets = false)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:1:0"], "the batch of the volley that paused finishes")
	assert_false(factory.debug_advance_time(1.0 / 60.0), "paused: nothing advances")
	factory.is_factory_processing_bullets = true
	step_factory()
	assert_eq(log.size(), 3, "after the resume the second volley grazes")
	assert_eq(log[2][2], second, "the second volley")


func test_events_left_by_an_early_return_dispatch_once_on_the_next_tick() -> void:
	var t := make_graze_target(Vector2(3, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	v.homing_take_control_of_texture_rotation = true
	v.shared_homing_deque_push_back_node2d_target(t)
	var log := H.record_graze(factory)
	v.bullet_homing_target_reached.connect(func(_v: BulletVolley2D, _i: int, _t: Node2D, _p: Vector2) -> void:
		factory.is_factory_processing_bullets = false)
	step_factory()
	assert_eq(log, [], "the homing handler paused the factory before the graze batch")
	assert_eq(v.debug_get_graze_info()["pending_events"], 1, "the graze waits")
	factory.is_factory_processing_bullets = true
	step_factory(3)
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "dispatched once after the resume")
	assert_eq(v.debug_get_graze_info()["pending_events"], 0, "nothing left")


func test_handler_replacing_zones_skips_stale_events() -> void:
	make_graze_target(Vector2(150, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 600.0)
	v.graze_set_zones([H.make_graze_zone([30.0, 20.0])], &"graze_targets")
	var other := H.make_graze_zone([5.0]) # too small to reach the target
	var log := H.record_graze(factory)
	factory.bullet_grazed.connect(func(_t: Node2D, vol: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
		vol.graze_set_zones([other], &"graze_targets"))
	step_factory(1, 0.5)
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "events of the replaced zones are dropped")
	assert_eq(v.get_graze_zones(), [other], "the handler's zones stand")


func test_structural_call_in_a_handler_is_refused_and_the_deferred_twin_works() -> void:
	var pair := _two_bullets_one_target()
	var calls := [0]
	factory.bullet_grazed.connect(func(_t: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
		calls[0] += 1
		if calls[0] == 1:
			factory.reset()
			factory.reset_deferred())
	step_factory()
	expect_error_sequence(["BulletFactory2D::reset cannot run inside a physics frame or while bullets are being processed"])
	assert_eq(calls[0], 2, "the refused reset changed nothing: both bullets reported")
	await idle()
	assert_false(is_instance_valid(pair[1]) and (pair[1] as BulletVolley2D).debug_get_life_state() == "active", "the deferred reset ran on the idle frame")


func test_handler_may_spawn() -> void:
	_two_bullets_one_target()
	var spawned: Array = []
	factory.bullet_grazed.connect(func(_t: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
		spawned.append(factory.spawn_volley(H.make_volley_data(2))))
	step_factory()
	assert_eq(spawned.size(), 2, "one spawn per graze")
	for v in spawned:
		assert_not_null(v, "spawned inside the handler")
		assert_eq((v as BulletVolley2D).debug_get_life_state(), "active", "and live")


func test_an_unhandled_graze_warns_once() -> void:
	make_graze_target(Vector2(10, 0))
	for k in 2:
		var v := graze_volley(H.transforms_at([Vector2.ZERO, Vector2(1, 0)]), 0.0)
		v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	step_factory(3)
	expect_warning_sequence(["its bullets were grazed, but nothing is connected to bullet_grazed (on it or on BulletFactory2D), so the graze is not handled. Connect BulletFactory2D.bullet_grazed once (it receives every graze), or remove the graze zones."])
	assert_eq(factory.debug_get_graze_stats()["events_total"], 4, "every graze still counted")
