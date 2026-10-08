extends BlastTest
## Feature mixing: pairs and triples of sound knobs composed in one play.
## Every test is deterministic (deterministic randoms, exact gains, clock-based
## durations). If any of these fails on unchanged C++, a composition is broken.


func after_all() -> void:
	# Same headless-audio drain as the other sound suites.
	OS.delay_msec(500)
	await super()


func _vol(sound_id: int) -> float:
	for e in busy_voices():
		if int((e as Dictionary)["sound"]) == sound_id:
			return float((e as Dictionary)["volume_db"])
	return NAN


func test_spawner_trim_meets_entry_clamp() -> void:
	godot_listener_at(Vector2.ZERO)
	var sp := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.sound_enabled = true
	sp.sound_volume_db = -4.0
	var loud := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	loud.volume_db = 30.0
	loud.set("volume_max_db", 6.0)
	loud.set("volume_min_db", -6.0)
	sp.sound_effects = [loud]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	await physics(2)
	assert_almost_eq(_vol(loud.get_instance_id()), 6.0, 0.05, "trim + base clamps at the ceiling")
	factory.stop_sounds()
	await idle(1)
	loud.volume_db = -30.0
	assert_true(sp.shoot_once(), "second shot fires")
	await physics(2)
	assert_almost_eq(_vol(loud.get_instance_id()), -6.0, 0.05, "trim + base clamps at the floor")


func test_amount_gain_meets_ceiling() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT)
	s.min_interval_sec = 0.0
	s.max_voices = 1
	s.amount_gain_db = 2.0
	s.volume_db = -10.0
	s.set("volume_max_db", -8.0)
	var sid: int = s.get_instance_id()
	var v := quick_volley(100)
	assert_true(v.sound_set_effects([s]), "entry armed")
	await physics(2)
	assert_almost_eq(_vol(sid), -8.0, 0.05, "100 bullets at k=2 would give -6, ceiling holds -8")


func test_deterministic_random_meets_gain() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT)
	s.min_interval_sec = 0.0
	s.max_voices = 1
	s.amount_gain_db = 2.0
	s.volume_db = -10.0
	s.set("random_volume_min_db", 2.0)
	s.set("random_volume_max_db", 2.0)
	var sid: int = s.get_instance_id()
	var v := quick_volley(100)
	assert_true(v.sound_set_effects([s]), "entry armed")
	await physics(2)
	assert_almost_eq(_vol(sid), -4.0, 0.05, "base -10 + gain +4 + random +2, no clamp involved")


func test_sequence_order_survives_interval() -> void:
	godot_listener_at(Vector2.ZERO)
	var trio := [H.make_sound_stream(0.2), H.make_sound_stream(0.21), H.make_sound_stream(0.22)]
	var s := BulletSoundData2D.new()
	s.streams = trio
	s.stream_mode = BulletSoundData2D.STREAM_SEQUENCE
	s.min_interval_sec = 0.05
	s.max_voices = 1
	var ids: Array = []
	for i in 3:
		assert_true(factory.play_sound(s, Vector2.ZERO), "offer %d accepted" % i)
		await physics(4)
		assert_eq(busy_voices().size(), 1, "one voice busy")
		ids.append(int((busy_voices()[0] as Dictionary).get("stream", 0)))
		factory.stop_sounds()
		await idle(1)
	var want: Array = []
	for t in trio:
		want.append((t as AudioStreamWAV).get_instance_id())
	assert_eq(ids, want, "interval pacing keeps the round-robin order")


func test_duck_meets_pool_steal() -> void:
	godot_listener_at(Vector2.ZERO)
	factory.sound_max_voices = 2
	var a := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	a.volume_db = -4.0
	a.priority = 0
	var b := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	b.volume_db = -2.0
	b.priority = 5
	b.set("duck_amount_db", 6.0)
	var c := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	c.volume_db = -3.0
	c.priority = 9
	var aid: int = a.get_instance_id()
	var bid: int = b.get_instance_id()
	var cid: int = c.get_instance_id()
	assert_true(factory.play_sound(a, Vector2.ZERO), "a plays")
	await physics(2)
	assert_true(factory.play_sound(b, Vector2(10, 0)), "b plays")
	await physics(2)
	assert_almost_eq(_vol(aid), -10.0, 0.05, "a dipped by b")
	assert_true(factory.play_sound(c, Vector2(20, 0)), "c steals the oldest voice")
	await physics(2)
	assert_eq(busy_voices().size(), 2, "pool still holds two")
	assert_true(is_nan(_vol(aid)), "stolen a is gone")
	assert_almost_eq(_vol(bid), -2.0, 0.05, "b restored: c dips nothing")
	assert_almost_eq(_vol(cid), -3.0, 0.05, "c at base")


func test_pan_meets_pinned_camera() -> void:
	godot_listener_at(Vector2.ZERO)
	var cam := Camera2D.new()
	cam.position = Vector2.ZERO
	add(cam)
	cam.make_current()
	await idle(1)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 32)
	s.audibility_mode = BulletSoundData2D.SOUND_AUDIBILITY_CAMERA
	s.camera_path = factory.get_path_to(cam)
	s.set("random_pan", 1.0)
	for i in 6:
		assert_true(factory.play_sound(s, Vector2(100, 0)), "offer %d accepted" % i)
	await physics(2)
	assert_eq(busy_voices().size(), 6, "inside the pinned view: all play")
	var ys: Array = []
	for e in busy_voices():
		ys.append(float((e as Dictionary)["position"].y))
	ys.sort()
	assert_gt(ys[ys.size() - 1] - ys[0], 5.0, "pan spreads placement under camera culling")


func test_ring_meets_clamp() -> void:
	factory.debug_set_sound_log_enabled(true)
	make_graze_target(Vector2(100, 0), &"g")
	var zone := H.make_graze_zone([120.0])
	var v := graze_volley(H.transforms_at([Vector2(-100, 0)]), 200.0)
	assert_true(v.graze_set_zones([zone], &"g"), "zones armed")
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_GRAZE)
	s.volume_db = 30.0
	s.set("volume_max_db", 6.0)
	v.sound_set_effects([s])
	for i in 120:
		await physics(1)
		if busy_voices().size() > 0:
			break
	assert_eq(busy_voices().size(), 1, "graze sounded")
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["volume_db"]), 6.0, 0.05, "ring-filtered graze still clamps")


func test_spawner_duck_path() -> void:
	godot_listener_at(Vector2.ZERO)
	var sp := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.sound_enabled = true
	sp.sound_volume_db = -4.0
	var sting := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	sting.volume_db = -2.0
	sp.sound_effects = [sting]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "spawner shot fires")
	await physics(2)
	var sid: int = sting.get_instance_id()
	assert_almost_eq(_vol(sid), -6.0, 0.05, "trim applied at the spawner")
	var bomb := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	bomb.priority = 5
	bomb.set("duck_amount_db", 6.0)
	assert_true(factory.play_sound(bomb, Vector2(10, 0)), "manual bomb plays")
	await physics(2)
	assert_almost_eq(_vol(sid), -12.0, 0.05, "duck subtracts from the trimmed base")


func test_duck_fade_steal_pressure_drops() -> void:
	godot_listener_at(Vector2.ZERO)
	factory.sound_max_voices = 1
	var a := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	a.volume_db = -4.0
	a.priority = 0
	a.set("fade_out_sec", 0.5)
	var b := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	b.volume_db = -2.0
	b.priority = 5
	b.set("duck_amount_db", 6.0)
	var aid: int = a.get_instance_id()
	var bid: int = b.get_instance_id()
	assert_true(factory.play_sound(a, Vector2.ZERO), "a plays")
	await physics(2)
	assert_true(factory.play_sound(b, Vector2(10, 0)), "b offered into a full pool")
	await physics(2)
	assert_true(bool((busy_voices()[0] as Dictionary).get("fading", false)), "stolen a fades")
	assert_true(is_nan(_vol(bid)), "newcomer drops while the pool fades")
	await physics(40)
	assert_eq(busy_voices().size(), 0, "fade released the slot")
	assert_true(factory.play_sound(b, Vector2(10, 0)), "b plays once free")
	await physics(2)
	assert_almost_eq(_vol(bid), -2.0, 0.05, "b at base")
	assert_true(is_nan(_vol(aid)), "a long gone")


func test_random_gain_clamp_triple() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT)
	s.min_interval_sec = 0.0
	s.max_voices = 1
	s.amount_gain_db = 2.0
	s.volume_db = -10.0
	s.set("random_volume_min_db", 2.0)
	s.set("random_volume_max_db", 2.0)
	s.set("volume_max_db", -6.0)
	var sid: int = s.get_instance_id()
	var v := quick_volley(100)
	assert_true(v.sound_set_effects([s]), "entry armed")
	await physics(2)
	assert_almost_eq(_vol(sid), -6.0, 0.05, "-10 +4 +2 would give -4, ceiling holds -6")
