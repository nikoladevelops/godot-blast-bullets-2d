extends BlastTest
## Laser lifecycle: a finished laser waits in the factory's pool (by beam
## count) and comes back with a clean life; get_active_lasers lists what
## fires; clear_active_lasers fades or ends them all (safe from handlers);
## reset() destroys every laser; a script may free a laser at any time (the
## factory forgets it), and a handler of ANY laser signal (or of the laser's
## own visibility_changed, which begin_life/release_life/enter_pool_unfired
## run) may free the laser, spawn more lasers or free the factory (queue_free,
## or the release way) without a crash or a dangling entry.

const DT := 0.0625
const SIGNALS := ["laser_phase_changed", "laser_pierce_started", "laser_hit", "laser_pierce_ended", "laser_wall_hit", "laser_finished"]


## A script LaserData2D whose PREDELETE frees its laser: dropping the data
## Ref inside release_life runs user code that may delete the node under it.
class HostileLaserData extends LaserData2D:
	var target: Node

	func _notification(what: int) -> void:
		if what == NOTIFICATION_PREDELETE and is_instance_valid(target):
			target.free()


## The same owner-free hazard for a zone dropped by graze_clear/graze_arm.
class HostileZone extends GrazeZone2D:
	var target: Node

	func _notification(what: int) -> void:
		if what == NOTIFICATION_PREDELETE and is_instance_valid(target):
			target.free()


## ... and for a Variant custom-data value replaced by set_custom_data.
class HostileData extends Resource:
	var target: Node

	func _notification(what: int) -> void:
		if what == NOTIFICATION_PREDELETE and is_instance_valid(target):
			target.free()


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


func _connect(sig: String, callable: Callable) -> void:
	var n := _arg_count(sig)
	factory.connect(sig, callable.unbind(n) if n > 0 else callable)


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


## A laser queued for deletion while it waits in the pool is never handed
## out again: the next spawn gets a fresh laser, which outlives the deletion.
func test_a_queued_pooled_laser_is_never_reused() -> void:
	var a := _fire(H.make_laser(0.0, 0.0, 0.0625, 0.0))
	step_factory(2, DT)
	assert_true(a.is_pooled(), "finished into the pool")
	a.queue_free()
	var b := _fire(H.make_laser(0.0, 0.0, 1.0, 0.0))
	assert_ne(b, a, "the queued laser is never reused")
	await idle(1)
	assert_true(is_instance_valid(b), "the new laser outlives the deletion")
	assert_true(b.is_laser_active(), "and still fires")
	step_factory(1, DT)
	assert_eq(factory.get_active_lasers(), [b], "listed")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "consistent books")


## A laser_finished handler that queue_frees its laser: the laser never
## comes back from the pool, even to a spawn later in the same sweep.
func test_a_laser_queued_by_its_finished_handler_never_comes_back() -> void:
	var a := _fire(H.make_laser(0.0, 0.0, 0.0625, 0.0))
	var b := _fire(H.make_laser(0.0, 0.0, 0.0625, 0.0))
	var spawned: Array = []
	factory.laser_finished.connect(func(l: LaserVolley2D) -> void:
		if l == a:
			a.queue_free()
		elif l == b:
			spawned.append(_fire(H.make_laser(0.0, 0.0, 1.0, 0.0))))
	step_factory(2, DT)
	assert_eq(spawned.size(), 1, "the second handler spawned")
	assert_ne(spawned[0], a, "never the queued laser")
	await idle(1)
	assert_true(is_instance_valid(spawned[0]) and spawned[0].is_laser_active(), "the new laser survives the deletion")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "consistent books")


## The laser's own visibility_changed is user code too (begin_life shows it,
## release_life/enter_pool_unfired hide it). A handler that frees the factory
## there must be survived like the volley twin: guarded visibility calls, no
## use of `this` (factory or laser) after the handler, never an engine abort.
func test_a_visibility_handler_freeing_the_factory_is_survived() -> void:
	check_factory_after = false
	var l := _fire(H.make_laser(0.0, 0.0, 0.0625, 0.0, 40.0))
	assert_not_null(l, "fired")
	var freed := [false]
	l.visibility_changed.connect(func() -> void:
		if not freed[0] and is_instance_valid(factory):
			freed[0] = true
			factory.free())
	for i in 4:
		if not is_instance_valid(factory):
			break
		step_factory(1, DT)
	assert_true(freed[0], "the handler ran from a laser set_visible")
	assert_false(is_instance_valid(factory), "the factory is gone")
	await idle(1)
	assert_false(is_instance_valid(l), "the refused laser free finished with queue_free")
	expect_no_errors()


## A release-build free of the LASER itself from its visibility_changed is
## refused by its emit guard and finished with queue_free (the volley twin's
## rule; the laser used to have no guard at all).
func test_a_release_free_of_the_laser_from_its_visibility_signal_is_refused() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 0.0625, 0.0, 40.0))
	var freed := [false]
	l.visibility_changed.connect(func() -> void:
		if not freed[0] and is_instance_valid(l):
			freed[0] = true
			BulletFactory2D.debug_free_like_release(l))
	step_factory(2, DT)
	assert_true(freed[0], "the handler ran from the release set_visible")
	assert_true(is_instance_valid(l), "the release free was refused")
	assert_true(l.is_queued_for_deletion(), "and finished with queue_free")
	await idle(1)
	assert_false(is_instance_valid(l), "gone at the end of the frame")
	expect_no_errors()


## The spawn/reuse path is the second trigger: begin_life's set_visible(true)
## runs the handler of a pooled laser; freeing the factory there must return
## no stale laser (a LifeWatch in spawn_laser_span) and finish cleanly.
func test_a_visibility_handler_freeing_the_factory_during_reuse_is_survived() -> void:
	check_factory_after = false
	var a := _fire(H.make_laser(0.0, 0.0, 0.0625, 0.0, 40.0))
	for i in 4:
		if not is_instance_valid(factory):
			break
		step_factory(1, DT)
	assert_true(a.is_pooled(), "the first laser finished and pooled")
	var freed := [false]
	a.visibility_changed.connect(func() -> void:
		if not freed[0] and is_instance_valid(factory):
			freed[0] = true
			factory.free())
	var b := factory.spawn_laser(H.make_laser(0.0, 0.0, 0.5, 0.0, 40.0), [Transform2D.IDENTITY] as Array[Transform2D])
	assert_true(freed[0], "the handler ran from begin_life's set_visible")
	assert_null(b, "the dead factory hands out no laser")
	assert_false(is_instance_valid(factory), "the factory is gone")
	await idle(1)
	assert_false(is_instance_valid(a), "the reused laser's refused free finished")
	expect_no_errors()


## release_life drops the laser's data/look Refs: their PREDELETE is script
## user code that may free the laser (the volley's twin wraps those drops in
## guarded_user_call2d and re-checks). The laser used to continue on freed
## memory.
func test_a_data_predelete_that_frees_the_laser_is_survived() -> void:
	var d := HostileLaserData.new()
	d.charge_sec = 0.0
	d.grow_sec = 0.0
	d.active_sec = 0.0625
	d.fade_sec = 0.0
	d.length = 40.0
	var t: Array[Transform2D] = [Transform2D.IDENTITY]
	var l := factory.spawn_laser(d, t)
	assert_not_null(l, "fired")
	d.target = l
	d = null # the laser holds the data's last Ref
	var finished := [false]
	factory.laser_finished.connect(func(_l: LaserVolley2D) -> void: finished[0] = true)
	for i in 4:
		if not is_instance_valid(factory):
			break
		step_factory(1, DT)
	assert_true(finished[0] or not is_instance_valid(l), "the laser finished")
	await idle(1)
	assert_true(is_instance_valid(factory), "the factory survives")
	assert_false(is_instance_valid(l), "the PREDELETE free finished (refused and queued)")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books balanced")
	expect_no_errors()


## A zone's PREDELETE that frees the laser during graze_clear/graze_arm must
## be refused (queued), never let the arming write on freed memory.
func test_a_zone_predelete_that_frees_the_laser_is_survived() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 10.0, 0.0, 200.0))
	var z := HostileZone.new()
	z.ring_count = 1
	z.set_ring_radius(0, 24.0)
	z.target = l
	assert_true(l.graze_set_zones([z], &"graze_targets"), "armed")
	z = null # the laser holds the zone's last Ref
	l.graze_clear()
	await idle(1)
	assert_true(is_instance_valid(factory), "the factory survives")
	assert_false(is_instance_valid(l), "the refused PREDELETE free finished with queue_free")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books balanced")
	expect_no_errors()


## Replacing custom_data drops the old Variant: a script value PREDELETE
## that frees the laser must be survived.
func test_custom_data_predelete_that_frees_the_laser_is_survived() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 10.0, 0.0, 200.0))
	var d := HostileData.new()
	d.target = l
	l.set_custom_data(d)
	d = null # the laser holds the value's last Ref
	l.set_custom_data(null)
	await idle(1)
	assert_true(is_instance_valid(factory), "the factory survives")
	assert_false(is_instance_valid(l), "the value PREDELETE freed the laser cleanly")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books balanced")
	expect_no_errors()


## A laser queued for deletion (a script's queue_free) is gone for the
## factory at once, like a queued volley: never listed, counted, ticked or
## ended again.
func test_a_queued_laser_is_never_listed_counted_or_ticked() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0))
	var heard: Array = []
	factory.laser_phase_changed.connect(func(_l, p: int) -> void: heard.append(p))
	step_factory(1, DT)
	heard.clear()
	l.queue_free()
	assert_eq(factory.get_active_lasers(), [], "not listed")
	assert_eq(factory.get_active_laser_count(), 0, "not counted")
	assert_eq(factory.clear_active_lasers(), 0, "not ended again")
	l.end_laser(false)
	step_factory(2, DT)
	assert_eq(heard, [], "a queued laser never signals again")
	await idle(1)
	assert_false(is_instance_valid(l), "deleted")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "consistent books")
