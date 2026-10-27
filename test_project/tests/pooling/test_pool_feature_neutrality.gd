extends BlastTest
## Flocking across the pool: a pooled volley releases its flock (weakref
## proof) and the factory's armed count; reused for plain data it flies
## exactly like a cold volley and reports no flock; a PARKED volley (auto
## pooling off) keeps its flock and a wake resumes flocking.
##
## Graze across the pool: a pooled volley releases its zone resources
## (weakref proof) and its per-bullet state; reused for plain data it is
## unarmed and reports nothing; a PARKED volley (auto pooling off) keeps
## its zones and a wake resumes grazing with a fresh visit; graze events
## still queued when the volley is pooled never reach the next life.

const DT := 1.0 / 60.0


func _data(lifetime := 1000.0) -> BulletVolleyData2D:
	return H.make_flight_data([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.5, Vector2(0, 30)), Transform2D(-0.5, Vector2(20, 10))], 100.0, lifetime)


func test_a_pooled_volley_releases_its_flock() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	var f := H.make_flock(1.0, 1.0, 1.0)
	var ref: WeakRef = weakref(f)
	v.flock_join(f)
	f = null
	step_factory()
	assert_not_null(ref.get_ref(), "the armed volley holds the flock")
	for i in 3:
		v.disable_bullet(i)
	assert_eq(v.debug_get_life_state(), "pooled", "pooled")
	assert_null(ref.get_ref(), "the pooled volley released it")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "disarmed")


func test_reuse_for_plain_data_flies_like_a_cold_volley() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	v.flock_join(H.make_flock(3.0, 2.0, 2.0))
	step_factory(10)
	for i in 3:
		v.disable_bullet(i)
	var reused: BulletVolley2D = factory.spawn_volley(_data())
	assert_eq(reused, v, "the pool handed it back")
	var cold: BulletVolley2D = factory.spawn_volley(_data())
	assert_ne(cold, reused, "a fresh volley")
	assert_null(reused.flock_get(), "no flock")
	assert_eq(reused.flock_get_scope(), -1, "no scope")
	step_factory(30)
	for i in 3:
		assert_eq(reused.get_bullet_transform(i), cold.get_bullet_transform(i), "bullet %d flies like the cold volley" % i)
		assert_eq(reused.debug_get_bullet_flock_neighbors(i), 0, "bullet %d reads no flock" % i)


func test_a_parked_volley_keeps_its_flock_and_a_wake_resumes_flocking() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	var f := H.make_flock(0.0, 0.0, 1.0)
	v.flock_join(f)
	v.set_is_auto_pooling_enabled(false)
	step_factory()
	for i in 3:
		v.disable_bullet(i)
	assert_eq(v.debug_get_life_state(), "parked", "parked, still owned")
	assert_eq(v.flock_get(), f, "a parked volley keeps its flock")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 1, "still armed")
	step_factory(3)
	assert_eq(factory.debug_get_flock_stats()["grid_members"], 0, "nothing gathered while parked")
	for i in 3:
		v.enable_bullet(i)
	step_factory()
	assert_eq(v.debug_get_bullet_flock_neighbors(0), 2, "the wake flocks again")
	assert_ne(v.debug_get_bullet_flock_steering(0), Vector2(), "with a pull")


func test_a_pooled_volley_releases_its_zones() -> void:
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var zone := H.make_graze_zone([20.0])
	var ref: WeakRef = weakref(zone)
	v.graze_set_zones([zone], &"graze_targets")
	zone = null
	assert_not_null(ref.get_ref(), "the armed volley holds the zone")
	v.disable_bullet(0)
	assert_eq(v.debug_get_life_state(), "pooled", "pooled")
	assert_null(ref.get_ref(), "the pooled volley released it")
	var info: Dictionary = v.debug_get_graze_info()
	assert_false(info["armed"], "disarmed")
	assert_eq(info["state_bytes"], 0, "no per-bullet state kept")


func test_reuse_for_plain_data_reports_nothing() -> void:
	make_graze_target(Vector2(10, 0))
	var log := H.record_graze(factory)
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	step_factory()
	assert_eq(log.size(), 1, "the first life grazed")
	v.disable_bullet(0)
	var reused := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	assert_eq(reused, v, "the pool handed it back")
	step_factory(3)
	assert_eq(log.size(), 1, "the plain reuse grazes nothing")
	assert_false(reused.is_graze_armed(), "unarmed")


func test_a_parked_volley_keeps_its_zones_and_a_wake_resumes_grazing() -> void:
	var t := make_graze_target(Vector2(10, 0))
	var log := H.record_graze(factory)
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var zone := H.make_graze_zone([20.0])
	zone.regraze = GrazeZone2D.REGRAZE_AFTER_EXIT
	v.graze_set_zones([zone], &"graze_targets")
	v.set_is_auto_pooling_enabled(false)
	step_factory()
	v.disable_bullet(0)
	assert_eq(v.debug_get_life_state(), "parked", "parked, still owned")
	assert_eq(v.get_graze_zones(), [zone], "a parked volley keeps its zones")
	step_factory(3)
	assert_eq(log.size(), 1, "nothing while parked")
	v.enable_bullet(0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:0:0"], "the wake starts a fresh visit (After Exit grazes again)")
	assert_eq(log[1][1], t, "same target")


func test_queued_events_never_reach_the_next_life() -> void:
	var t := make_graze_target(Vector2(3, 0))
	var log := H.record_graze(factory)
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	v.homing_take_control_of_texture_rotation = true
	v.shared_homing_deque_push_back_node2d_target(t)
	v.bullet_homing_target_reached.connect(func(_v: BulletVolley2D, _i: int, _t: Node2D, _p: Vector2) -> void:
		factory.is_factory_processing_bullets = false)
	step_factory()
	assert_eq(v.debug_get_graze_info()["pending_events"], 1, "a graze is queued behind the pause")
	v.disable_bullet(0)
	assert_eq(v.debug_get_life_state(), "pooled", "pooled while the graze waited")
	factory.is_factory_processing_bullets = true
	var reused := graze_volley(H.transforms_at([Vector2(500, 500)]), 0.0)
	assert_eq(reused, v, "reused")
	assert_eq(reused.debug_get_graze_info()["pending_events"], 0, "the new life starts with no queued events")
	step_factory(3)
	assert_eq(log, [], "the old life's graze never fires")
