extends BlastTest
## Graze across the pool: a pooled volley releases its zone resources
## (weakref proof) and its per-bullet state; reused for plain data it is
## unarmed and reports nothing; a PARKED volley (auto pooling off) keeps
## its zones and a wake resumes grazing with a fresh visit; graze events
## still queued when the volley is pooled never reach the next life.


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
	zone.regraze = BulletGrazeZone2D.REGRAZE_AFTER_EXIT
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
