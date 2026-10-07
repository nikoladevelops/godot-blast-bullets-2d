extends BlastTest
## SoundMixer2D + factory glue through factory.play_sound only (no volleys):
## bind/defaults, happy path, shared per-resource budget, nearest wins,
## max_distance drop, Replace/Skip, priority steal, durations, pause, voices.


func after_all() -> void:
	# Headless audio mixes in real time while tests run faster: the dummy
	# server releases stopped playbacks on its own mix cycles, not on stop().
	# One drain per file (resets already stopped every voice) keeps the exit
	# leak gate green without slowing the tests.
	OS.delay_msec(500)
	await super()


func test_bind_and_max_voices_default() -> void:
	for m in ["play_sound", "stop_sounds", "get_sound_max_voices", "set_sound_max_voices", "debug_set_sound_log_enabled", "debug_get_sound_log", "debug_clear_sound_log", "debug_get_sound_voices", "debug_get_sound_stats", "debug_stop_sound_voices"]:
		assert_has_method(factory, m, "bound " + m)
	assert_eq(factory.sound_max_voices, 32, "pool defaults 32")
	factory.set_sound_max_voices(0)
	expect_error_sequence(["BulletFactory2D: sound_max_voices must be between 1 and 256, keeping the old value."])
	assert_eq(factory.sound_max_voices, 32, "old value kept")
	factory.set_sound_max_voices(257)
	expect_error_sequence(["BulletFactory2D: sound_max_voices must be between 1 and 256, keeping the old value."])
	factory.set_sound_max_voices(8)
	assert_eq(factory.sound_max_voices, 8, "valid value applies")


func test_play_sound_happy_path_applies_mix() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.volume_db = -6.0
	s.pitch_scale = 1.5
	assert_true(factory.play_sound(s, Vector2(100, 0)), "offer accepted")
	await physics(2)
	var busy := busy_voices()
	assert_eq(busy.size(), 1, "one voice busy")
	assert_almost_eq((busy[0] as Dictionary)["position"], Vector2(100, 0), Vector2(0.5, 0.5), "Godot listener: voice at the event")
	assert_almost_eq(float((busy[0] as Dictionary)["volume_db"]), -6.0, 0.01, "volume applied")
	assert_almost_eq(float((busy[0] as Dictionary)["pitch_scale"]), 1.5, 0.01, "pitch applied")
	assert_eq(String((busy[0] as Dictionary)["bus"]), "Master", "default bus kept")


func test_play_sound_refusals() -> void:
	assert_false(factory.play_sound(null, Vector2.ZERO), "null refused")
	expect_error_sequence(["BulletFactory2D.play_sound: sound is null, nothing plays."])
	var disabled := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	disabled.enabled = false
	assert_false(factory.play_sound(disabled, Vector2.ZERO), "disabled plays nothing")
	var streamless := BulletSoundData2D.new()
	assert_false(factory.play_sound(streamless, Vector2.ZERO), "streamless refused")
	expect_warning_sequence(["BulletSoundData2D: stream is empty, the sound plays nothing."])
	var chanceless := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	chanceless.trigger_chance = 0.0
	assert_false(factory.play_sound(chanceless, Vector2.ZERO), "chance 0 never plays")
	await physics(2)
	assert_eq(busy_voices().size(), 0, "nothing busy after refusals")


func test_interval_budget_is_shared_per_resource() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.min_interval_sec = 10.0 # one play until the clock moves past it
	assert_true(factory.play_sound(s, Vector2(10, 0)), "first offer accepted")
	assert_true(factory.play_sound(s, Vector2.ZERO), "nearer offer accepted (queued)")
	await physics(2)
	assert_eq(int(factory.debug_get_sound_stats()["plays_total"]), 1, "one play per interval")
	assert_eq(int(factory.debug_get_sound_stats()["dropped_total"]), 1, "farther candidate evicted")
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], Vector2.ZERO, Vector2(0.5, 0.5), "nearest wins")
	assert_true(factory.play_sound(s, Vector2(30, 0)), "offered again inside the interval")
	await physics(2)
	assert_eq(int(factory.debug_get_sound_stats()["plays_total"]), 1, "interval holds across sweeps")
	s.min_interval_sec = 0.0
	assert_true(factory.play_sound(s, Vector2(20, 0)), "interval cleared")
	await physics(2)
	assert_eq(int(factory.debug_get_sound_stats()["plays_total"]), 2, "plays again once open")


func test_interval_zero_plays_up_to_max_voices_nearest_first() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 2)
	assert_true(factory.play_sound(s, Vector2(100, 0)), "offer 0 queued")
	assert_true(factory.play_sound(s, Vector2(110, 0)), "offer 1 queued")
	for i in 3:
		assert_false(factory.play_sound(s, Vector2(120 + i * 10, 0)), "farther offer %d turned away" % i)
	await physics(2)
	assert_eq(busy_voices().size(), 2, "up to max_voices per sweep")
	assert_eq(int(factory.debug_get_sound_stats()["plays_total"]), 2, "two plays")
	assert_eq(int(factory.debug_get_sound_stats()["dropped_total"]), 3, "three evicted")


func test_beyond_max_distance_never_takes_a_voice() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.max_distance = 2000.0
	assert_false(factory.play_sound(s, Vector2(5000, 0)), "inaudible dropped")
	await physics(2)
	assert_eq(busy_voices().size(), 0, "no voice taken")
	assert_eq(int(factory.debug_get_sound_stats()["dropped_total"]), 1, "drop counted")


func test_unknown_bus_warns_once_and_plays_master() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.bus = &"NoSuchBus_xyz"
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	expect_warning_sequence(["BulletSoundData2D: bus \"NoSuchBus_xyz\" does not exist, playing on Master."])
	assert_eq(busy_voices().size(), 1, "still plays")
	assert_eq(String((busy_voices()[0] as Dictionary)["bus"]), "Master", "falls back to Master")
	assert_true(factory.play_sound(s, Vector2.ZERO), "second offer accepted")
	await physics(2)
	var fresh_warnings := 0
	for err in get_errors():
		if not err.handled and err.is_push_warning():
			fresh_warnings += 1
	assert_eq(fresh_warnings, 0, "bus warning fires once per name")


func test_replace_oldest_restarts_skip_new_keeps() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 1)
	assert_true(factory.play_sound(s, Vector2.ZERO), "first plays")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "one voice busy")
	assert_true(factory.play_sound(s, Vector2(50, 0)), "re-offered under Replace Oldest")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "Replace Oldest: still one voice")
	assert_eq(int(factory.debug_get_sound_stats()["plays_total"]), 2, "Replace Oldest: newest plays")
	s.when_limit_reached = BulletSoundData2D.SOUND_LIMIT_SKIP_NEW
	assert_true(factory.play_sound(s, Vector2(100, 0)), "re-offered under Skip New")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "Skip New: still one voice")
	assert_eq(int(factory.debug_get_sound_stats()["plays_total"]), 2, "Skip New: candidate dropped")


func test_max_duration_stops_loopers_and_short_streams_finish() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.max_duration_sec = 0.05
	assert_true(factory.play_sound(s, Vector2.ZERO), "looping offer accepted")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "looper busy")
	await physics(5)
	assert_eq(busy_voices().size(), 0, "looper stopped after its duration")
	var short := BulletSoundData2D.new()
	short.stream = H.make_sound_stream(0.05, false)
	short.min_interval_sec = 0.0
	short.max_duration_sec = 0.05 # deterministic under simulated time (headless audio mixes in real time)
	assert_true(factory.play_sound(short, Vector2.ZERO), "short offer accepted")
	await physics(2)
	await physics(6)
	assert_eq(busy_voices().size(), 0, "stopped voice released its stream")


func test_priority_steal_takes_only_equal_or_lower() -> void:
	godot_listener_at(Vector2.ZERO)
	factory.sound_max_voices = 1
	var low := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 4)
	low.priority = -5
	var high := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 4)
	high.priority = 5
	assert_true(factory.play_sound(low, Vector2.ZERO), "low plays")
	await physics(2)
	assert_eq(int((busy_voices()[0] as Dictionary)["sound"]), low.get_instance_id(), "low busy")
	assert_true(factory.play_sound(high, Vector2(10, 0)), "high offered")
	await physics(2)
	assert_eq(int((busy_voices()[0] as Dictionary)["sound"]), high.get_instance_id(), "high steals low")
	assert_true(factory.play_sound(low, Vector2(20, 0)), "low offered again")
	await physics(2)
	assert_eq(int((busy_voices()[0] as Dictionary)["sound"]), high.get_instance_id(), "low cannot steal high")


func test_stop_and_reset_silence() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "voice busy")
	factory.stop_sounds()
	assert_eq(busy_voices().size(), 0, "stop_sounds stops every voice")
	assert_true(factory.play_sound(s, Vector2(10, 0)), "offer accepted again")
	await idle(1)
	factory.reset()
	await idle(1)
	assert_eq(busy_voices().size(), 0, "reset stops every voice")
	assert_eq(int(factory.debug_get_sound_stats()["channels"]), 0, "reset forgets channels")


func test_paused_offers_drop_and_resume_replays() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	factory.set_is_factory_processing_bullets(false)
	assert_false(factory.play_sound(s, Vector2.ZERO), "paused offer dropped")
	await physics(2)
	assert_eq(busy_voices().size(), 0, "nothing plays while paused")
	factory.set_is_factory_processing_bullets(true)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted after resume")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "plays after resume")


func test_voices_opt_out_of_engine_interpolation() -> void:
	get_tree().physics_interpolation = true
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "voice busy")
	var found: Array = []
	_collect_interpolated(factory, found)
	assert_eq(found, [], "voices never engine-interpolate")
	get_tree().physics_interpolation = false


func _collect_interpolated(n: Node, out: Array) -> void:
	for c in n.get_children(true):
		if c is CanvasItem and (c as CanvasItem).is_physics_interpolated_and_enabled():
			out.append(str(n.get_path_to(c)))
		_collect_interpolated(c, out)
