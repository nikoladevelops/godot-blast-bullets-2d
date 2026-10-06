extends BlastTest
## Graze visits and bullet_graze_exited: a visit that fired a graze ends with
## exactly one exit carrying the deepest ring touched (the nearest target at
## that moment named); bullets killed, cleared, expired or frozen inside
## never exit; a wake starts a fresh visit; After Exit re-grazes every
## visit while Once stays silent on repeats (a deeper second visit grazes
## only its new ring); a teleport out exits on the next tick; losing every
## target (freed, left the group) ends visits silently.


func test_exit_reports_the_deepest_ring_of_the_visit() -> void:
	var t := make_graze_target(Vector2(100, 15))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 600.0)
	var log := H.record_graze(factory)
	var z := H.make_graze_zone([40.0, 20.0])
	v.graze_set_zones([z], &"graze_targets")
	step_factory(30)
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:0:1", "exit:0:1"], "outer ring, inner ring, exit with the inner one")
	assert_eq(log[2].slice(1), [t, v, 0, z, 1], "exit payload")


func test_exit_names_the_target_the_visit_grazed() -> void:
	var a := make_graze_target(Vector2(10, 0))
	var b := make_graze_target(Vector2(30, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	step_factory()
	assert_eq(log[0][1], a, "entered at a")
	a.position = Vector2(100, 0)
	b.position = Vector2(25, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "exit:0:0"], "outside every target: one exit")
	assert_eq(log[1][1], a, "the exit names the target the visit grazed, not the one nearest now")


func test_a_visit_moves_on_when_its_target_vanishes_inside_another() -> void:
	var a := make_graze_target(Vector2(5, 0))
	var b := make_graze_target(Vector2(-15, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	step_factory()
	assert_eq(log[0][1], a, "grazed the nearest target")
	a.free()
	make_graze_target(Vector2(-300, 0)) # a newcomer takes a's freed slot
	step_factory()
	assert_eq(log.size(), 1, "still inside b: the visit goes on, no exit, no new graze")
	assert_true(v.is_bullet_inside_graze(0, 0), "inside")
	b.position = Vector2(-100, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "exit:0:0"], "leaving b exits")
	assert_eq(log[1][1], b, "naming b, the target the visit moved on to")


func test_killed_cleared_or_expired_inside_never_exit() -> void:
	var t := make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO, Vector2.ZERO, Vector2.ZERO]), 0.0)
	var short := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	short.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	short.set_life_time_left(0.05)
	step_factory()
	assert_eq(log.size(), 4, "every bullet grazed")
	v.disable_bullet(0)
	v.clear_bullet(1)
	step_factory(6) # the short volley expires
	assert_eq(short.debug_get_life_state(), "pooled", "expired")
	log.clear()
	t.position = Vector2(200, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["exit:2:0"], "only the bullet still alive exits")
	assert_eq(log[0][2], v, "from the live volley")


func test_freeze_ends_the_visit_silently_and_a_wake_starts_fresh() -> void:
	var t := make_graze_target(Vector2(10, 0))
	var once := graze_volley(H.transforms_at([Vector2.ZERO, Vector2(0, 500)]), 0.0)
	var again := graze_volley(H.transforms_at([Vector2.ZERO, Vector2(0, 500)]), 0.0)
	var log := H.record_graze(factory)
	once.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	var after_exit := H.make_graze_zone([20.0])
	after_exit.regraze = BulletGrazeZone2D.REGRAZE_AFTER_EXIT
	again.graze_set_zones([after_exit], &"graze_targets")
	step_factory()
	assert_eq(log.size(), 2, "both grazed")
	for v in [once, again]:
		v.disable_bullet(0)
	t.position = Vector2(200, 0)
	step_factory()
	for v in [once, again]:
		v.enable_bullet(0)
		assert_false(v.is_bullet_inside_graze(0, 0), "the wake ended the visit")
	step_factory()
	assert_eq(log.size(), 2, "no exit for a visit the freeze ended")
	t.position = Vector2(10, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:0:0", "enter:0:0"], "Once keeps its ring; After Exit grazes again")
	assert_eq(log[2][2], again, "the After Exit volley")


func _toggle_visits(regraze: int) -> Array:
	var t := make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	var z := H.make_graze_zone([20.0])
	z.regraze = regraze
	v.graze_set_zones([z], &"graze_targets")
	for i in 6:
		t.position = Vector2(10, 0) if i % 2 == 0 else Vector2(100, 0)
		step_factory()
	return H.graze_kinds(log)


func test_after_exit_regrazes_on_every_visit() -> void:
	assert_eq(_toggle_visits(BulletGrazeZone2D.REGRAZE_AFTER_EXIT), ["enter:0:0", "exit:0:0", "enter:0:0", "exit:0:0", "enter:0:0", "exit:0:0"], "one enter and one exit per visit")


func test_once_repeat_visits_are_silent() -> void:
	assert_eq(_toggle_visits(BulletGrazeZone2D.REGRAZE_ONCE), ["enter:0:0", "exit:0:0"], "only the first visit reports")


func test_once_deeper_second_visit_grazes_only_its_new_ring() -> void:
	var t := make_graze_target(Vector2(30, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([40.0, 20.0])], &"graze_targets")
	step_factory()
	t.position = Vector2(100, 0)
	step_factory()
	t.position = Vector2(10, 0)
	step_factory()
	t.position = Vector2(100, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "exit:0:0", "enter:0:1", "exit:0:1"], "the second visit reports its new ring and exits with it")


func test_teleport_out_exits_on_the_next_tick() -> void:
	make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	step_factory()
	v.teleport_bullet(0, Vector2(500, 0))
	assert_eq(log.size(), 1, "teleporting reports nothing by itself")
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "exit:0:0"], "the next tick sees it outside")


func test_resting_inside_grazes_once_and_never_exits() -> void:
	make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	step_factory(30)
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "one enter in 30 ticks")
	assert_true(v.is_bullet_inside_graze(0, 0), "still inside")


func _lose_target(regraze: int, lose: Callable) -> Array:
	var t := make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var log := H.record_graze(factory)
	var z := H.make_graze_zone([20.0])
	z.regraze = regraze
	v.graze_set_zones([z], &"graze_targets")
	step_factory()
	lose.call(t)
	step_factory()
	assert_false(v.is_bullet_inside_graze(0, 0), "the visit ended")
	make_graze_target(Vector2(10, 0))
	step_factory()
	return H.graze_kinds(log)


func test_a_freed_target_ends_visits_silently() -> void:
	var freed := func(t: Node2D) -> void: t.free()
	assert_eq(_lose_target(BulletGrazeZone2D.REGRAZE_ONCE, freed), ["enter:0:0"], "no exit; Once does not re-graze the replacement")


func test_a_target_leaving_the_group_ends_visits_silently() -> void:
	var left := func(t: Node2D) -> void: t.remove_from_group(&"graze_targets")
	assert_eq(_lose_target(BulletGrazeZone2D.REGRAZE_AFTER_EXIT, left), ["enter:0:0", "enter:0:0"], "no exit; After Exit re-grazes the replacement")
