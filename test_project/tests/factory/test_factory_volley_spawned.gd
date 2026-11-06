extends BlastTest
## BulletFactory2D.volley_spawned(volley, spawner): once per volley life, from
## every spawn path (spawn_volley, a spawner's shot, a split child, a pickup
## conversion), at the start of the next factory step before the volley
## moves, fully armed. spawner is null for factory volleys and for a spawner
## freed since. Recorded only while connected; a life cleared, pooled or
## reused before the step is never announced. Handlers may free the factory,
## spawn (announced next step) or pause (the rest waits).

const DT := 1.0 / 16.0


func _record(log: Array) -> Callable:
	return func(volley: BulletVolley2D, spawner: Object) -> void:
		log.append({"volley": volley, "spawner": spawner, "origin": volley.get_bullet_transform(0).origin, "life": volley.get_life_id()})


func test_a_script_spawn_is_announced_once_before_it_moves() -> void:
	var log := []
	factory.volley_spawned.connect(_record(log))
	var d := H.make_volley_data(3, 200.0)
	var v := factory.spawn_volley(d)
	var start: Vector2 = v.get_bullet_transform(0).origin
	assert_eq(log.size(), 0, "nothing runs inside the spawn call")
	step_factory(1, DT)
	assert_eq(log.size(), 1, "announced at the next step")
	assert_eq(log[0].volley, v, "the spawned volley")
	assert_null(log[0].spawner, "a factory volley has no spawner")
	assert_almost_eq(log[0].origin, start, Vector2(0.001, 0.001), "announced before its first move")
	assert_ne(v.get_bullet_transform(0).origin, start, "the same step then moved it")
	step_factory(3, DT)
	assert_eq(log.size(), 1, "exactly once per life")


func test_a_spawner_shot_names_its_spawner_and_is_already_armed() -> void:
	var log := []
	factory.volley_spawned.connect(_record(log))
	var hit := H.make_sound(BulletSoundData2D.SOUND_ON_HIT)
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.sound_enabled = true
	sp.sound_effects = [hit]
	var armed := []
	factory.volley_spawned.connect(func(volley: BulletVolley2D, _s: Object) -> void: armed.append(volley.is_sound_armed()))
	assert_true(sp.shoot_once(), "fired")
	step_factory(1, DT)
	assert_eq(log.size(), 1, "one shot, one announcement")
	assert_eq(log[0].spawner, sp, "names the spawner that fired it")
	assert_eq(armed, [true], "the spawner's arming is done before the announcement")


func test_split_children_and_pickups_are_announced() -> void:
	var log := []
	factory.volley_spawned.connect(_record(log))
	var d := H.make_volley_data(2, 100.0)
	var v := factory.spawn_volley(d)
	step_factory(1, DT)
	assert_eq(log.size(), 1, "the parent")
	var stage := BulletSplitStage2D.new()
	stage.count = 3
	var split := BulletSplitData2D.new()
	split.stages = [stage]
	assert_true(v.split_set_data(split, d), "split armed (a factory volley names its child data)")
	v.split_bullet(0)
	step_factory(2, DT) # the flush spawns the children, the next step announces them
	var children := log.filter(func(e: Dictionary) -> bool: return e.volley != v)
	assert_eq(children.size(), 1, "one child volley per split parent")
	assert_eq((children[0].volley as BulletVolley2D).get_amount_bullets(), 3, "the split's children")
	log.clear()
	var coin := BulletPickupData2D.new()
	handle_events(factory)
	factory.convert_all_bullets_to_pickups(coin, null)
	step_factory(1, DT)
	assert_gt(log.size(), 0, "pickup volleys are announced too")
	for e in log:
		assert_not_null((e.volley as BulletVolley2D).get_pickup_data(), "an announced pickup volley carries its pickup")


func test_a_life_that_ended_before_the_step_is_never_announced() -> void:
	var log := []
	factory.volley_spawned.connect(_record(log))
	var d := H.make_volley_data(3)
	var first := factory.spawn_volley(d)
	var first_life := first.get_life_id()
	first.clear_all_bullets() # drained to the pool before any step
	var second := factory.spawn_volley(d) # same key: the pool may hand the same instance out again
	step_factory(1, DT)
	assert_eq(log.size(), 1, "only the live one")
	assert_eq(log[0].volley, second, "the second spawn")
	assert_eq(log[0].life, second.get_life_id(), "its current life")
	if first == second:
		assert_ne(first_life, second.get_life_id(), "a reused instance is announced for its new life only")


func test_nothing_is_recorded_while_unconnected() -> void:
	factory.spawn_volley(H.make_volley_data(2))
	var log := []
	factory.volley_spawned.connect(_record(log))
	step_factory(1, DT)
	assert_eq(log.size(), 0, "a spawn before the connection is not announced")


func test_a_freed_spawner_is_reported_as_null() -> void:
	var log := []
	factory.volley_spawned.connect(_record(log))
	var sp := make_spawner(H.make_volley_data(2), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_HAND_TO_FACTORY
	assert_true(sp.shoot_once(), "fired")
	sp.free()
	step_factory(1, DT)
	assert_eq(log.size(), 1, "the volley still announced")
	assert_null(log[0].spawner, "its freed spawner is null")


func test_handlers_may_spawn_pause_or_free_the_factory() -> void:
	# Spawn from the handler: announced at the next step, never in this loop.
	var log := []
	var spawned_inside := [false]
	factory.volley_spawned.connect(_record(log))
	factory.volley_spawned.connect(func(_v: BulletVolley2D, _s: Object) -> void:
		if not spawned_inside[0]:
			spawned_inside[0] = true
			factory.spawn_volley(H.make_volley_data(1)))
	factory.spawn_volley(H.make_volley_data(2))
	step_factory(1, DT)
	assert_eq(log.size(), 1, "the handler's spawn waits")
	step_factory(1, DT)
	assert_eq(log.size(), 2, "and is announced at the next step")
	# Pause from the handler: the rest of the batch waits for the resume.
	var paused := []
	var f2 := factory
	f2.volley_spawned.connect(func(v: BulletVolley2D, _s: Object) -> void:
		paused.append(v)
		f2.is_factory_processing_bullets = false)
	f2.spawn_volley(H.make_volley_data(1))
	f2.spawn_volley(H.make_volley_data(2))
	step_factory(1, DT)
	assert_eq(paused.size(), 1, "a pause stops the batch")
	f2.is_factory_processing_bullets = true
	expect_no_errors()


func test_a_handler_freeing_the_factory_is_survived() -> void:
	var f := BulletFactory2D.new()
	add(f)
	await idle(1)
	var calls := [0]
	f.volley_spawned.connect(func(_v: BulletVolley2D, _s: Object) -> void:
		calls[0] += 1
		BulletFactory2D.debug_free_like_release(f))
	f.spawn_volley(H.make_volley_data(2))
	f.spawn_volley(H.make_volley_data(3))
	f.debug_advance_time(DT)
	await idle(2)
	assert_eq(calls[0], 1, "the batch stops with the factory")
	assert_false(is_instance_valid(f), "freed (queued under the emit guard, then gone)")
