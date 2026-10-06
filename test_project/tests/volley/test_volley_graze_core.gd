extends BlastTest
## Graze detection core on factory-owned volleys (volley API + factory
## signals), stepped deterministically with factory.debug_advance_time:
## a ring grazes once per bullet life (Once policy), rings touched in one
## tick fire outer first, the swept test catches bullets faster than a thin
## ring, the effective radius is inclusive and counts the bullet's bounding
## radius (each shape, scaled and mirrored), zero-delta ticks test the
## current pose, resets re-arm, live zone edits reach flying bullets, null
## zones keep their index, the arming API rejects bad input and pooled
## handles with exact texts, and an unarmed volley carries no state.

const GROUP := &"graze_targets"


func test_graze_api_is_bound() -> void:
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	for m in ["graze_set_zones", "graze_clear", "get_graze_zones", "is_graze_armed", "get_bullet_grazed_rings", "is_bullet_inside_graze", "bullet_reset_graze", "debug_get_graze_info"]:
		assert_true(v.has_method(m), "BulletVolley2D.%s is bound" % m)
	for m in ["debug_get_graze_targets", "debug_get_graze_stats"]:
		assert_true(factory.has_method(m), "BulletFactory2D.%s is bound" % m)
	for s in ["bullet_grazed", "bullet_graze_exited"]:
		assert_true(factory.has_signal(s), "BulletFactory2D.%s is declared" % s)
	assert_eq(BulletVolley2D.MAX_GRAZE_ZONES, 4, "zone cap")


func test_a_bullet_passing_a_ring_grazes_exactly_once() -> void:
	var t := make_graze_target(Vector2(100, 20))
	var z := H.make_graze_zone([30.0])
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 600.0)
	var log := H.record_graze(factory)
	assert_true(v.graze_set_zones([z]), "armed")
	step_factory(30) # 300 px: in, through and out of the ring
	assert_eq(H.graze_kinds(log), ["enter:0:0", "exit:0:0"], "one enter, one exit")
	assert_eq(log[0].slice(1), [t, v, 0, z, 0], "enter payload: target, volley, bullet, zone, ring")
	assert_eq(log[1].slice(1), [t, v, 0, z, 0], "exit payload: deepest ring 0")
	assert_eq(v.get_bullet_grazed_rings(0, 0), 1, "ring 0 marked grazed")
	assert_false(v.is_bullet_inside_graze(0, 0), "left the zone")
	assert_eq(factory.debug_get_graze_stats()["events_total"], 2, "factory counted both events")


func test_rings_touched_in_one_tick_fire_outer_first() -> void:
	make_graze_target(Vector2(150, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 600.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([10.0, 30.0, 20.0])])
	step_factory(1, 0.5) # one 300 px step straight through the center
	assert_eq(H.graze_kinds(log), ["enter:0:1", "enter:0:2", "enter:0:0", "exit:0:0"], "largest radius first, then the exit with the deepest ring")


func test_fast_bullet_crossing_a_thin_ring_is_caught_by_the_sweep() -> void:
	make_graze_target(Vector2(250, 3))
	make_graze_target(Vector2(250, 300), &"graze_core_far")
	var v := graze_volley(H.transforms_at([Vector2.ZERO, Vector2(0, 300)]), 6000.0)
	var log := H.record_graze(factory)
	# 100 px per tick: the bullet sits at x = 200 and 300, never within 5 px.
	v.graze_set_zones([H.make_graze_zone([5.0]), H.make_graze_zone([5.0], &"graze_core_far")])
	step_factory(4)
	assert_eq(H.graze_kinds(log), ["enter:0:0", "exit:0:0", "enter:1:0", "exit:1:0"], "both 100 px steps graze their 5 px ring")
	log.clear()
	var miss := graze_volley(H.transforms_at([Vector2(0, 600)]), 6000.0)
	make_graze_target(Vector2(250, 606), &"graze_core_miss")
	miss.graze_set_zones([H.make_graze_zone([5.0], &"graze_core_miss")])
	step_factory(4)
	assert_eq(log, [], "a pass 6 px away misses a 5 px ring")


func test_effective_radius_boundary_is_inclusive() -> void:
	make_graze_target(Vector2(30, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO, Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([30.0]), H.make_graze_zone([29.5])])
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:1:0"], "both bullets graze the 30 px zone exactly on its edge")
	assert_eq(log[0][4], v.get_graze_zones()[0], "the 30 px zone")
	assert_eq(v.get_bullet_grazed_rings(0, 1), 0, "29.5 px is just out of reach")


func test_bullet_size_counts_for_each_shape_and_scale() -> void:
	make_graze_target(Vector2(30, 0))
	var log := H.record_graze(factory)
	var cases := [
		["circle r4", H.make_circle_shape(4.0), Transform2D(0.0, Vector2.ZERO), 26.0],
		["rect 10x6 (half the short side)", _rect(Vector2(10, 6)), Transform2D(0.0, Vector2.ZERO), 27.0],
		["capsule h8 (half the height)", _capsule(2.0, 8.0), Transform2D(0.0, Vector2.ZERO), 26.0],
		["circle r4 scaled x2", H.make_circle_shape(4.0), Transform2D(0.0, Vector2(2, 2), 0.0, Vector2.ZERO), 22.0],
		["circle r4 mirrored x-2", H.make_circle_shape(4.0), Transform2D(0.0, Vector2(-2, 2), 0.0, Vector2.ZERO), 22.0],
	]
	for c in cases:
		log.clear()
		var v := graze_volley([c[2]], 0.0, c[1])
		var exact := H.make_graze_zone([c[3]])
		exact.count_bullet_size = true
		var short := H.make_graze_zone([c[3] - 0.5])
		short.count_bullet_size = true
		var off := H.make_graze_zone([c[3]])
		v.graze_set_zones([exact, short, off])
		step_factory()
		assert_eq(H.graze_kinds(log), ["enter:0:0"], "%s: ring + bullet radius reaches 30 px exactly; 0.5 px less or size off misses" % c[0])
		v.graze_clear()


func _rect(size: Vector2) -> RectangleShape2D:
	var r := RectangleShape2D.new()
	r.size = size
	return r


func _capsule(radius: float, height: float) -> CapsuleShape2D:
	var c := CapsuleShape2D.new()
	c.radius = radius
	c.height = height
	return c


func test_bullet_spawned_inside_grazes_on_the_first_tick() -> void:
	make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([50.0, 20.0])])
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:0:1"], "both rings on the first tick, outer first")
	step_factory(10)
	assert_eq(log.size(), 2, "a bullet resting inside grazes once")
	assert_true(v.is_bullet_inside_graze(0, 0), "still inside")


func test_zero_delta_tick_tests_the_current_pose() -> void:
	make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 300.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([20.0])])
	step_factory(1, 0.0)
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "a zero-delta tick still sees the bullet inside")
	assert_eq(v.get_bullet_transform(0).origin, Vector2.ZERO, "and moved nothing")


func test_target_moving_onto_a_resting_bullet_grazes_it() -> void:
	var t := make_graze_target(Vector2(100, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([20.0])])
	step_factory()
	assert_eq(log, [], "far away")
	t.position = Vector2(15, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the target came to the bullet")


func test_resets_let_a_bullet_graze_again() -> void:
	make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([20.0])])
	step_factory(2)
	assert_eq(log.size(), 1, "grazed once")
	v.bullet_reset_graze(0)
	assert_eq(v.get_bullet_grazed_rings(0, 0), 0, "bits cleared")
	assert_false(v.is_bullet_inside_graze(0, 0), "visit cleared")
	step_factory()
	assert_eq(log.size(), 2, "bullet_reset_graze re-arms it")
	v.bullet_reset_state(0)
	step_factory()
	assert_eq(log.size(), 3, "bullet_reset_state re-arms it too")
	v.all_bullets_reset_state()
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:0:0", "enter:0:0", "enter:0:0"], "all_bullets_reset_state too, never an exit")
	v.bullet_reset_graze(5)
	expect_error_sequence(["bullet_reset_graze"])


func test_equal_radii_fire_in_ring_order() -> void:
	make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([20.0, 20.0, 15.0])])
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:0:1", "enter:0:2"], "equal radii by ring index, then the smaller ring")


func test_lowering_ring_count_shrinks_the_zone() -> void:
	var t := make_graze_target(Vector2(25, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	var z := H.make_graze_zone([10.0, 20.0, 30.0])
	v.graze_set_zones([z])
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:2"], "only the 30 px ring reaches")
	z.ring_count = 1
	t.position = Vector2(15, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:2", "exit:0:2"], "the zone is now 10 px: the bullet left it (ring 1 is no longer tested)")
	t.position = Vector2(8, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:2", "exit:0:2", "enter:0:0"], "ring 0 still grazes")


func test_live_zone_edit_applies_to_flying_bullets() -> void:
	make_graze_target(Vector2(40, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	var z := H.make_graze_zone([30.0])
	v.graze_set_zones([z])
	step_factory(3)
	assert_eq(log, [], "40 px away, 30 px ring")
	z.ring_1_radius = 45.0
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "a widened ring reaches the bullet already in flight")
	z.enabled = false
	z.ring_1_radius = 30.0
	step_factory()
	z.enabled = true
	z.regraze = BulletGrazeZone2D.REGRAZE_AFTER_EXIT
	z.ring_1_radius = 45.0
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "disabling ended the visit silently; Once bits survive it")


func test_null_zones_keep_their_index() -> void:
	make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	var z := H.make_graze_zone([20.0])
	assert_true(v.graze_set_zones([null, z, null]), "nulls accepted")
	assert_eq(v.get_graze_zones(), [null, z], "trailing nulls dropped, inner ones kept")
	step_factory()
	assert_eq(log.size(), 1, "one event")
	assert_eq(log[0][4], z, "the zone resource travels in the payload")
	assert_eq(v.get_bullet_grazed_rings(0, 1), 1, "state lives at the zone's index")
	assert_eq(v.get_bullet_grazed_rings(0, 0), 0, "the null slot holds nothing")
	v.get_bullet_grazed_rings(0, 2)
	v.is_bullet_inside_graze(0, -1)
	expect_error_sequence([
		"get_bullet_grazed_rings: zone_index 2 is out of range (the volley has 2 graze zone slots).",
		"is_bullet_inside_graze: zone_index -1 is out of range (the volley has 2 graze zone slots).",
	])


func test_set_zones_rejects_bad_input_and_changes_nothing() -> void:
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var z := H.make_graze_zone()
	assert_true(v.graze_set_zones([z]), "armed")
	assert_false(v.graze_set_zones([z, z, z, z, z]), "five zones")
	assert_false(v.graze_set_zones([z, Node2D]), "a class is not a zone")
	assert_false(v.graze_set_zones([z, 3]), "an int is not a zone")
	assert_false(v.graze_set_zones([BulletSpeedData2D.new()]), "another resource is not a zone")
	expect_error_sequence([
		"graze_set_zones: at most 4 zones (got 5), nothing changed.",
		"graze_set_zones: entry 1 is not a BulletGrazeZone2D, nothing changed.",
		"graze_set_zones: entry 1 is not a BulletGrazeZone2D, nothing changed.",
		"graze_set_zones: entry 0 is not a BulletGrazeZone2D, nothing changed.",
	])
	assert_eq(v.get_graze_zones(), [z], "the armed zones are untouched")
	assert_true(v.graze_set_zones([]), "an empty array disarms")
	assert_false(v.is_graze_armed(), "disarmed")


func test_set_zones_refuses_a_pooled_handle() -> void:
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	v.disable_bullet(0)
	assert_eq(v.debug_get_life_state(), "pooled", "the last bullet pooled the volley")
	assert_false(v.graze_set_zones([H.make_graze_zone()]), "refused")
	v.graze_clear()
	expect_error_sequence([
		"graze_set_zones: this volley is in the pool (its last bullet died), so this handle is stale.",
		"graze_clear: this volley is in the pool (its last bullet died), so this handle is stale.",
	])


func test_unarmed_volley_has_no_graze_state() -> void:
	make_graze_target(Vector2(5, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO, Vector2.ONE]), 0.0)
	var log := H.record_graze(factory)
	step_factory(3)
	assert_eq(log, [], "no zones, no events")
	var info: Dictionary = v.debug_get_graze_info()
	assert_false(info["armed"], "not armed")
	assert_eq(info["state_bytes"], 0, "no per-bullet state allocated")
	v.graze_set_zones([H.make_graze_zone()])
	assert_eq(v.debug_get_graze_info()["state_bytes"], 4, "2 bytes per bullet per zone")
	v.graze_clear()
	assert_eq(v.debug_get_graze_info()["state_bytes"], 0, "cleared")


func test_two_targets_in_one_zone_graze_once_and_name_the_nearest() -> void:
	var near := make_graze_target(Vector2(10, 0))
	make_graze_target(Vector2(-15, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([20.0])])
	step_factory(3)
	assert_eq(log.size(), 1, "a zone grazes once per ring however many targets reach the bullet")
	assert_eq(log[0][1], near, "the payload names the nearest target")


func test_separate_zones_graze_independently() -> void:
	var a := make_graze_target(Vector2(10, 0), &"graze_core_a")
	var b := make_graze_target(Vector2(-10, 0), &"graze_core_b")
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([20.0], &"graze_core_a"), H.make_graze_zone([20.0], &"graze_core_b")])
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:0:0"], "one graze per zone")
	assert_eq([log[0][1], log[1][1]], [a, b], "each zone names its own target")


func test_factory_reset_with_armed_volleys_is_clean() -> void:
	make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([20.0])])
	step_factory()
	factory.reset()
	step_factory(3)
	assert_eq(log.size(), 1, "nothing after the reset")
	assert_eq(factory.debug_get_graze_stats()["cached_groups"], 0, "reset dropped the cache and no armed volley asked again")


func test_factory_target_cache_snapshot() -> void:
	var a := make_graze_target(Vector2(1, 2))
	var b := make_graze_target(Vector2(3, 4))
	var targets: Array = factory.debug_get_graze_targets(GROUP)
	assert_eq(targets.size(), 2, "both targets")
	assert_eq(targets[0]["id"], a.get_instance_id(), "tree order")
	assert_eq(targets[1]["position"], Vector2(3, 4), "positions")
	assert_eq(factory.debug_get_graze_targets(&""), [], "an empty group has no targets")
