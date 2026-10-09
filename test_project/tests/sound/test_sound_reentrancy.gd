extends BlastTest
## User code that changes the world INSIDE the factory sweep while sound is
## armed: a listener moved by a handler silences that sweep (audibility is
## judged at flush), and handlers that clear volleys, free spawners, free
## listener nodes or edit a playing entry must never leave a stuck voice or
## touch freed memory.

var moved := 0


func after_all() -> void:
	# Same headless-audio drain as the other sound suites.
	OS.delay_msec(500)
	await super()


func before_each() -> void:
	await super()
	moved = 0


func _hum(follow := true) -> BulletSoundData2D:
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT, 32)
	s.max_distance = 500.0 # a listener 100000 px away hears nothing
	s.follow_bullet = follow
	return s


func _expiring_volley(lifetime: float) -> BulletVolley2D:
	var d := H.make_volley_data(2, 0.0, lifetime)
	d.is_life_time_over_signal_enabled = true
	return factory.spawn_volley(d)


## Three hum volleys around one whose body_entered handler (user code in the
## sweep) may move the listener. Returns the plays the mixer started in the
## sweep that ran the handler (-1: the handler never ran). The mixer judges
## audibility again at flush against the listener as it stands THEN, so a
## listener moved mid-sweep silences the whole sweep, whichever volleys had
## already offered (the offer-time cache is only an optimization of that).
func _plays_in_handler_sweep(listener: AudioListener2D, move_it: bool) -> int:
	make_wall(Vector2(200, 0))
	await physics()
	for k in 3:
		var d := H.make_volley_data(2, 600.0 if k == 1 else 0.0, 30.0)
		if k == 1:
			d.transforms = [Transform2D(), Transform2D(0.0, Vector2(0, 40))]
			d.monitorable = true
			d.set_collision_mask_from_array([3])
			d.collision_shape = H.make_circle_shape(6.0)
			d.bullet_max_collision_count = 100 # survives the hit: it still offers
		var v: BulletVolley2D = factory.spawn_volley(d)
		# Not followed: a followed bullet's repeat offer is dropped as a duplicate.
		assert_true(v.sound_set_effects([_hum(false)]), "volley %d hums" % k)
	factory.body_entered.connect(func(_body: Object, _v: BulletVolley2D, _i: int) -> void:
		if moved == 0 and move_it:
			listener.global_position = Vector2(100000, 0)
		moved += 1)
	for i in 60:
		var before := int(factory.debug_get_sound_stats()["plays_total"])
		await idle(1)
		if moved > 0:
			return int(factory.debug_get_sound_stats()["plays_total"]) - before
	return -1


func test_listener_moved_by_a_handler_silences_the_sweep() -> void:
	var listener := godot_listener_at(Vector2.ZERO)
	assert_eq(await _plays_in_handler_sweep(listener, true), 0, "nothing plays at the stale listener position")


func test_listener_left_alone_plays_the_sweep() -> void:
	# Control for the test above: same scene, the handler leaves the listener.
	var listener := godot_listener_at(Vector2.ZERO)
	assert_gt(await _plays_in_handler_sweep(listener, false), 0, "the same sweep plays")


func test_clearing_the_followed_volley_in_a_handler_silences_its_hum() -> void:
	godot_listener_at(Vector2.ZERO)
	var a := _expiring_volley(0.1)
	var b := quick_volley(3, 0.0, 30.0)
	assert_true(b.sound_set_effects([_hum()]), "B hums")
	assert_not_null(a, "A spawned")
	factory.life_time_over.connect(func(_v: BulletVolley2D, _i: Array) -> void:
		if b.get_amount_bullets() > 0:
			b.clear_all_bullets())
	step_factory(4)
	assert_eq(busy_voices().size(), 3, "one hum voice per followed bullet")
	for i in 40:
		step_factory(1)
		if busy_voices().is_empty():
			break
	assert_eq(busy_voices().size(), 0, "no stuck hum after the handler cleared the volley")
	expect_no_errors()


func test_freeing_all_bullets_mid_hum_leaves_no_voice() -> void:
	godot_listener_at(Vector2.ZERO)
	var v := quick_volley(4, 100.0, 30.0)
	assert_true(v.sound_set_effects([_hum()]), "hum armed")
	step_factory(5)
	assert_eq(busy_voices().size(), 4, "four followed voices")
	await idle(1)
	factory.free_active_bullets()
	await idle(1)
	for i in 30:
		step_factory(1)
		if busy_voices().is_empty():
			break
	assert_eq(busy_voices().size(), 0, "voices end when their volley is destroyed")
	expect_no_errors()


func test_spawner_freed_by_its_own_handler_leaves_no_stuck_voice() -> void:
	godot_listener_at(Vector2.ZERO)
	var d := H.make_volley_data(3, 0.0, 0.1)
	d.is_life_time_over_signal_enabled = true
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 3)
	sp.sound_effects = [_hum()]
	sp.life_time_over.connect(func(_v: BulletVolley2D, _i: Array) -> void:
		moved += 1
		sp.queue_free())
	assert_true(sp.shoot_once(), "fires")
	for i in 60:
		step_factory(1)
		if moved > 0:
			break
	assert_gt(moved, 0, "handler ran and queued the spawner for deletion")
	await idle(2)
	factory.free_active_bullets()
	await idle(2)
	for i in 30:
		step_factory(1)
		if busy_voices().is_empty():
			break
	assert_eq(busy_voices().size(), 0, "no voice outlives its volley")
	expect_no_errors()


func test_listener_node_freed_while_its_voice_plays() -> void:
	var node := make_listener(Vector2(30, 0), &"sound_ears")
	var v := quick_volley(2, 0.0, 30.0)
	assert_true(v.sound_set_effects([_hum()], &"sound_ears"), "flight hum armed with a group listener")
	step_factory(4)
	assert_gt(busy_voices().size(), 0, "voices play against the group listener")
	node.free()
	for i in 6:
		step_factory(1)
	var voices := busy_voices()
	for e in voices:
		assert_true(is_finite((e as Dictionary)["position"].x), "voice position stays finite")
	expect_no_errors()


func test_streams_emptied_mid_flight_warns_once_and_recovers() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := _hum()
	var v := quick_volley(2, 0.0, 30.0)
	assert_true(v.sound_set_effects([s]), "hum armed")
	step_factory(3)
	var before := busy_voices().size()
	assert_gt(before, 0, "hum plays")
	var good: Array = s.streams.duplicate()
	s.streams = []
	step_factory(3)
	expect_warning_sequence(["BulletSoundData2D: streams is empty, the sound plays nothing."])
	s.streams = good
	step_factory(3)
	expect_no_errors()
	assert_gt(busy_voices().size(), 0, "voices keep playing with the restored streams")
