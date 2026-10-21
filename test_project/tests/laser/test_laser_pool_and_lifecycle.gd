extends BlastTest
## Laser lifecycle: a finished laser waits in the factory's pool (by beam
## count) and comes back with a clean life; get_active_lasers lists what
## fires; clear_active_lasers fades or ends them all (safe from handlers);
## reset() destroys every laser; a script may free a laser at any time (the
## factory forgets it), and a handler of ANY laser signal may free the laser,
## spawn more lasers or free the factory (queue_free, or the release way)
## without a crash or a dangling entry.

const DT := 0.0625
const SIGNALS := ["laser_phase_changed", "laser_pierce_started", "laser_hit", "laser_pierce_ended", "laser_wall_hit", "laser_finished"]


func _busy() -> LaserData2D:
	# Raises every laser signal within a few ticks: a target to pierce, a wall
	# to block on, a short life.
	var d := H.make_laser(0.0625, 0.0625, 0.125, 0.0625)
	d.wall_mask = 16
	return d


func _scene() -> void:
	make_area(Vector2(100, 0), Vector2(20, 60))
	make_wall(Vector2(200, 0), Vector2(20, 200), 16)
	await idle(2)


func _fire(data: LaserData2D, beams := 1) -> LaserVolley2D:
	var t: Array[Transform2D] = []
	for i in beams:
		t.append(Transform2D(0.0, Vector2(0, 4 * i)))
	return factory.spawn_laser(data, t)


func _arg_count(sig: String) -> int:
	for s in factory.get_signal_list():
		if str(s.name) == sig:
			return (s.args as Array).size()
	return 0


func _connect(sig: String, call: Callable) -> void:
	var n := _arg_count(sig)
	factory.connect(sig, call.unbind(n) if n > 0 else call)


func test_active_lasers_are_listed_and_cleared() -> void:
	var a := _fire(H.make_laser(0.0, 0.0, 0.0, 0.25))
	var b := _fire(H.make_laser(0.0, 0.0, 0.0, 0.25), 3)
	assert_eq(factory.get_active_lasers(), [a, b], "both fire")
	assert_eq(factory.get_active_laser_count(), 2, "counted")
	assert_eq(factory.clear_active_lasers(), 2, "both cleared")
	assert_eq([a.get_phase(), b.get_phase()], [LaserVolley2D.PHASE_FADING, LaserVolley2D.PHASE_FADING], "fading")
	assert_eq(factory.clear_active_lasers(), 2, "fading lasers can be ended again (at once)")
	assert_eq(factory.clear_active_lasers(false), 2, "and at once")
	step_factory(1, DT)
	assert_eq(factory.get_active_laser_count(), 0, "gone after the tick")
	assert_eq(factory.debug_get_laser_pool_amount(), 2, "both pooled")


func test_the_pool_is_keyed_by_beam_count() -> void:
	var one := _fire(H.make_laser(0.0, 0.0, 0.0625, 0.0))
	step_factory(2, DT)
	var three := _fire(H.make_laser(0.0, 0.0, 1.0, 0.0), 3)
	assert_ne(three, one, "a three-beam laser never takes a one-beam one")
	var again := _fire(H.make_laser(0.0, 0.0, 1.0, 0.0))
	assert_eq(again, one, "the one-beam laser is reused")


func test_reset_destroys_every_laser() -> void:
	var firing := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0))
	var pooled := _fire(H.make_laser(0.0, 0.0, 0.0625, 0.0), 2)
	step_factory(2, DT)
	assert_true(pooled.is_pooled(), "one pooled")
	factory.reset()
	assert_false(is_instance_valid(firing), "the firing laser is gone")
	assert_false(is_instance_valid(pooled), "the pooled one too")
	assert_eq([factory.get_active_laser_count(), factory.debug_get_laser_pool_amount()], [0, 0], "nothing tracked")
	var fresh := _fire(H.make_laser())
	assert_not_null(fresh, "the factory fires again")


func test_a_script_may_free_a_laser_any_time() -> void:
	var a := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0))
	var b := _fire(H.make_laser(0.0, 0.0, 0.0625, 0.0))
	step_factory(2, DT)
	assert_true(b.is_pooled(), "b pooled")
	a.free()
	b.free()
	assert_eq([factory.get_active_laser_count(), factory.debug_get_laser_pool_amount()], [0, 0], "the factory forgot both")
	step_factory(2, DT)
	assert_true(factory.debug_assert_no_dangling()["ok"], "nothing dangles")


func test_a_handler_may_free_the_laser_from_every_signal(sig: String = use_parameters(SIGNALS)) -> void:
	await _scene()
	var fired: Array = []
	_connect(sig, func() -> void:
		if fired.is_empty():
			fired.append(true)
			for l in factory.get_active_lasers():
				(l as LaserVolley2D).free())
	_fire(_busy(), 2)
	for i in 12:
		step_factory(1, DT)
	assert_false(fired.is_empty(), "%s was raised" % sig)
	assert_eq(factory.get_active_laser_count(), 0, "%s: the laser is gone" % sig)
	assert_true(factory.debug_assert_no_dangling()["ok"], "%s: nothing dangles" % sig)


func test_a_handler_may_spawn_lasers_from_every_signal(sig: String = use_parameters(SIGNALS)) -> void:
	await _scene()
	var spawned: Array = []
	_connect(sig, func() -> void:
		if spawned.size() < 3:
			spawned.append(_fire(H.make_laser(0.0, 0.0, 0.0625, 0.0))))
	_fire(_busy(), 2)
	for i in 16:
		step_factory(1, DT)
	assert_gt(spawned.size(), 0, "%s spawned" % sig)
	for l in spawned:
		assert_not_null(l, "%s: every spawn from a handler fires" % sig)
	assert_true(factory.debug_assert_no_dangling()["ok"], "%s: nothing dangles" % sig)


func test_a_handler_may_free_the_factory_from_every_signal(sig: String = use_parameters(SIGNALS)) -> void:
	await _scene()
	check_factory_after = false
	var f := factory
	var fired: Array = []
	_connect(sig, func() -> void:
		if fired.is_empty() and is_instance_valid(f):
			fired.append(true)
			BulletFactory2D.debug_free_like_release(f))
	_fire(_busy(), 2)
	for i in 12:
		if not is_instance_valid(f) or f.is_queued_for_deletion():
			break # freed, or (a free refused under its emit guard) queued
		f.debug_advance_time(DT)
	assert_false(fired.is_empty(), "%s was raised" % sig)
	await idle(2)
	assert_false(is_instance_valid(f), "%s: the factory is gone" % sig)


func test_the_laser_container_survives_a_script_freeing_it() -> void:
	_fire(H.make_laser(0.0, 0.0, 0.0, 0.0))
	var container := factory.get_node("LaserVolleysContainer")
	container.free()
	assert_eq(factory.get_active_laser_count(), 0, "its lasers died with it and were forgotten")
	assert_not_null(_fire(H.make_laser()), "a new container for the next laser")


func test_custom_data_is_readable_in_signals_and_dropped_by_the_pool() -> void:
	await _scene()
	var l := _fire(H.make_laser(0.0, 0.0, 0.125, 0.0))
	l.set_custom_data({"damage": 3, "team": "boss"})
	var seen: Array = []
	factory.laser_hit.connect(func(_t, laser: LaserVolley2D, _b, _p) -> void: seen.append(laser.get_custom_data()))
	step_factory(1, DT)
	assert_eq(seen, [{"damage": 3, "team": "boss"}], "the hit handler reads it")
	step_factory(4, DT)
	assert_true(l.is_pooled(), "finished")
	assert_eq(l.get_custom_data(), null, "the pool dropped it")
	var again := _fire(H.make_laser(0.0, 0.0, 0.125, 0.0))
	assert_eq(again, l, "reused")
	assert_eq(again.get_custom_data(), null, "a new life starts without it")


func test_populate_lasers_pool_prewarms_and_free_lasers_pool_empties_it() -> void:
	assert_eq(factory.populate_lasers_pool(3, 4), 4, "four lasers of 3 beams")
	assert_eq(factory.debug_get_laser_pool_amount(), 4, "waiting in the pool")
	assert_eq(factory.get_active_laser_count(), 0, "none fires")
	var data := H.make_laser(0.0, 0.0, 0.125, 0.0)
	var objects := Performance.get_monitor(Performance.OBJECT_COUNT)
	var l := _fire(data, 3)
	assert_eq(factory.debug_get_laser_pool_amount(), 3, "the shot took a pre-warmed one")
	assert_eq(Performance.get_monitor(Performance.OBJECT_COUNT), objects, "no new object for the first shot")
	step_factory(1, DT)
	assert_eq(l.get_beam_count(), 3, "a normal life")
	factory.populate_lasers_pool(5, 2)
	assert_eq(factory.free_lasers_pool(3), 3, "one bucket")
	assert_eq(factory.debug_get_laser_pool_amount(), 2, "the other bucket stays")
	assert_true(is_instance_valid(l), "a firing laser is never freed")
	assert_eq(factory.free_lasers_pool(), 2, "every bucket")
	assert_eq(factory.debug_get_laser_pool_amount(), 0, "empty")
	step_factory(4, DT)
	assert_true(l.is_pooled(), "the firing one pools when it finishes")


func test_pool_calls_refuse_bad_requests_and_physics_frames() -> void:
	factory.populate_lasers_pool(0, 2)
	expect_error_sequence(["populate_lasers_pool: beam_count must be between 1 and BulletPatterns2D.max_bullets_per_pattern (20000), nothing was created."])
	factory.populate_lasers_pool(2, 0)
	expect_error_sequence(["populate_lasers_pool: amount must be > 0, nothing was created."])
	factory.free_lasers_pool(0)
	expect_error_sequence(["free_lasers_pool: beam_count must be -1 (every bucket) or > 0, nothing was freed."])
	await physics(1)
	factory.populate_lasers_pool(2, 2)
	expect_errors_containing("BulletFactory2D::populate_lasers_pool cannot run inside a physics frame", 1)
	factory.populate_lasers_pool_deferred(2, 2)
	factory.free_lasers_pool_deferred(-1)
	factory.populate_lasers_pool_deferred(4, 1)
	await idle(2)
	expect_no_errors()
	assert_eq(factory.debug_get_laser_pool_amount(), 1, "the deferred calls ran in order: 2 added, all freed, 1 added")
