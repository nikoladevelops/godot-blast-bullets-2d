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
	expect_warning_sequence(["BulletSoundData2D: streams is empty, the sound plays nothing."])
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
	short.streams = [H.make_sound_stream(0.05, false)]
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


func test_steal_mode_setter_rejects_and_keeps() -> void:
	var s := BulletSoundData2D.new()
	assert_has_method(s, "set_steal_mode", "bound set_steal_mode")
	assert_has_method(s, "get_steal_mode", "bound get_steal_mode")
	assert_eq(s.steal_mode, BulletSoundData2D.STEAL_OLDEST, "oldest by default")
	s.set_steal_mode(2)
	expect_error_sequence(["BulletSoundData2D: steal_mode must be 0 (Oldest) or 1 (Quietest), keeping the old value."])
	assert_eq(s.steal_mode, BulletSoundData2D.STEAL_OLDEST, "kept")


func _busy_volumes() -> Array:
	var out: Array = []
	for e in busy_voices():
		out.append(float((e as Dictionary)["volume_db"]))
	out.sort()
	return out


func test_quietest_steal_takes_the_quietest_voice() -> void:
	godot_listener_at(Vector2.ZERO)
	for mode in [BulletSoundData2D.STEAL_QUIETEST, BulletSoundData2D.STEAL_OLDEST]:
		var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 2)
		s.steal_mode = mode
		s.volume_db = -2.0
		assert_true(factory.play_sound(s, Vector2.ZERO), "loud plays")
		await physics(2)
		s.volume_db = -20.0
		assert_true(factory.play_sound(s, Vector2(10, 0)), "quiet plays")
		await physics(2)
		assert_eq(busy_voices().size(), 2, "two voices busy")
		s.volume_db = -10.0
		assert_true(factory.play_sound(s, Vector2(20, 0)), "third offered")
		await physics(2)
		if mode == BulletSoundData2D.STEAL_QUIETEST:
			assert_eq(_busy_volumes(), [-10.0, -2.0], "quietest stolen")
		else:
			assert_eq(_busy_volumes(), [-20.0, -10.0], "oldest stolen")
		factory.stop_sounds()
		await idle(1)


func test_fade_setter_rejects_and_keeps() -> void:
	var s := BulletSoundData2D.new()
	assert_has_method(s, "set_fade_out_sec", "bound set_fade_out_sec")
	assert_has_method(s, "get_fade_out_sec", "bound get_fade_out_sec")
	assert_eq(s.get("fade_out_sec"), 0.0, "instant by default")
	s.set("fade_out_sec", -1.0)
	expect_error_sequence(["BulletSoundData2D: fade_out_sec must be finite and >= 0, keeping the old value."])
	assert_eq(s.get("fade_out_sec"), 0.0, "kept")
	s.set("fade_out_sec", NAN)
	expect_error_sequence(["BulletSoundData2D: fade_out_sec must be finite and >= 0, keeping the old value."])
	s.set("fade_out_sec", 0.2)
	assert_eq(s.get("fade_out_sec"), 0.2, "valid value applies")


func test_duration_expiry_fades_before_releasing() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.volume_db = -6.0
	s.max_duration_sec = 0.05
	s.set("fade_out_sec", 0.2)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "voice busy")
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["volume_db"]), -6.0, 0.05, "full volume first")
	await physics(5)
	assert_eq(busy_voices().size(), 1, "fading voice still counted busy")
	assert_true(bool((busy_voices()[0] as Dictionary).get("fading", false)), "marked fading past duration")
	var mid: float = float((busy_voices()[0] as Dictionary)["volume_db"])
	assert_lt(mid, -6.0, "fade dipped the volume")
	await physics(20)
	assert_eq(busy_voices().size(), 0, "fade finished releases the voice")


func test_steal_fades_instead_of_cutting() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 1)
	s.volume_db = -6.0
	s.set("fade_out_sec", 0.5)
	assert_true(factory.play_sound(s, Vector2.ZERO), "first plays")
	await physics(2)
	assert_true(factory.play_sound(s, Vector2(10, 0)), "second steals under Replace Oldest")
	await physics(2)
	assert_eq(busy_voices().size(), 2, "stolen voice fades out under the new one")
	var vols := _busy_volumes()
	assert_eq(vols[vols.size() - 1], -6.0, "newcomer at full volume")
	assert_lt(vols[0], -6.0, "stolen voice fading down")


func test_stop_sounds_stays_instant_with_fade() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.set("fade_out_sec", 0.5)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "voice busy")
	factory.stop_sounds()
	assert_eq(busy_voices().size(), 0, "stop means silence now, fade or not")


func test_duck_setter_rejects_and_keeps() -> void:
	var s := BulletSoundData2D.new()
	assert_has_method(s, "set_duck_amount_db", "bound set_duck_amount_db")
	assert_has_method(s, "get_duck_amount_db", "bound get_duck_amount_db")
	assert_eq(s.get("duck_amount_db"), 0.0, "no ducking by default")
	s.set("duck_amount_db", -1.0)
	expect_error_sequence(["BulletSoundData2D: duck_amount_db must be finite and >= 0, keeping the old value."])
	assert_eq(s.get("duck_amount_db"), 0.0, "kept")
	s.set("duck_amount_db", NAN)
	expect_error_sequence(["BulletSoundData2D: duck_amount_db must be finite and >= 0, keeping the old value."])
	s.set("duck_amount_db", 6.0)
	assert_eq(s.get("duck_amount_db"), 6.0, "valid value applies")


func _voice_volume(sound_id: int) -> float:
	for e in busy_voices():
		if int((e as Dictionary)["sound"]) == sound_id:
			return float((e as Dictionary)["volume_db"])
	return NAN


func test_duck_dips_lower_priority_until_it_ends() -> void:
	godot_listener_at(Vector2.ZERO)
	var sting := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	sting.volume_db = -4.0
	sting.priority = 0
	var bomb := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	bomb.volume_db = -2.0
	bomb.priority = 5
	bomb.max_duration_sec = 0.3
	bomb.set("duck_amount_db", 6.0)
	var sting_id: int = sting.get_instance_id()
	var bomb_id: int = bomb.get_instance_id()
	assert_true(factory.play_sound(sting, Vector2.ZERO), "sting plays")
	await physics(2)
	assert_almost_eq(_voice_volume(sting_id), -4.0, 0.05, "sting at base alone")
	assert_true(factory.play_sound(bomb, Vector2(10, 0)), "bomb plays")
	await physics(2)
	assert_almost_eq(_voice_volume(sting_id), -10.0, 0.05, "sting dipped while the bomb lives")
	assert_almost_eq(_voice_volume(bomb_id), -2.0, 0.05, "bomb itself never dipped")
	await physics(25)
	assert_almost_eq(_voice_volume(sting_id), -4.0, 0.05, "sting restored after the bomb ends")
	factory.stop_sounds()
	await idle(1)


func test_duck_ignores_ties_and_fading_voices() -> void:
	godot_listener_at(Vector2.ZERO)
	var a := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	a.volume_db = -4.0
	a.priority = 2
	a.set("duck_amount_db", 6.0)
	var b := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	b.volume_db = -4.0
	b.priority = 2
	var aid: int = a.get_instance_id()
	assert_true(factory.play_sound(a, Vector2.ZERO), "a plays")
	await physics(2)
	assert_true(factory.play_sound(b, Vector2(10, 0)), "tied b plays")
	await physics(2)
	assert_almost_eq(_voice_volume(aid), -4.0, 0.05, "ties never dip each other")
	factory.stop_sounds()
	await idle(1)
	var sting := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	sting.volume_db = -4.0
	var loud := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	loud.priority = 5
	loud.max_duration_sec = 0.1
	loud.set("fade_out_sec", 0.5)
	loud.set("duck_amount_db", 6.0)
	var sid: int = sting.get_instance_id()
	assert_true(factory.play_sound(sting, Vector2.ZERO), "sting plays")
	await physics(2)
	assert_true(factory.play_sound(loud, Vector2(10, 0)), "loud plays")
	await physics(2)
	assert_almost_eq(_voice_volume(sid), -10.0, 0.05, "sting dipped while loud is fully live")
	await physics(10)
	assert_almost_eq(_voice_volume(sid), -4.0, 0.05, "fading voices release their duck at once")


func test_pause_freezes_fades_until_resume() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.max_duration_sec = 0.1
	s.set("fade_out_sec", 0.5)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(10)
	assert_true(bool((busy_voices()[0] as Dictionary).get("fading", false)), "fading past duration")
	factory.set_is_factory_processing_bullets(false)
	await physics(5)
	assert_eq(busy_voices().size(), 1, "paused fade frozen, not released")
	factory.set_is_factory_processing_bullets(true)
	await physics(40)
	assert_eq(busy_voices().size(), 0, "resume lets the fade finish")


func test_layered_entries_keep_independent_budgets() -> void:
	godot_listener_at(Vector2.ZERO)
	var gated := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	gated.min_interval_sec = 10.0
	var free := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	free.min_interval_sec = 0.0
	free.max_voices = 4
	for i in 6:
		assert_true(factory.play_sound(gated, Vector2.ZERO), "gated offer %d accepted" % i)
		assert_true(factory.play_sound(free, Vector2.ZERO), "free offer %d accepted" % i)
		await physics(2)
	var stats: Dictionary = factory.debug_get_sound_stats()
	assert_eq(int(stats["plays_total"]), 7, "gated plays once, free plays every sweep")
	factory.stop_sounds()
	await idle(1)


func test_stacking_same_entry_double_offers_without_interval() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	s.min_interval_sec = 0.0
	s.max_voices = 4
	var v := quick_volley(1, 0.0, 30.0)
	assert_true(v.sound_set_effects([s, s]), "same entry twice arms")
	assert_true(v.clear_bullet(0), "cleared")
	await physics(2)
	assert_eq(int(factory.debug_get_sound_stats()["plays_total"]), 2, "one channel, two candidates, both win")
	factory.stop_sounds()
	await idle(1)


func _offer_temps(n: int) -> void:
	for i in n:
		var tmp := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
		tmp.min_interval_sec = 0.0
		assert_true(factory.play_sound(tmp, Vector2.ZERO), "temp offer %d accepted" % i)
	await physics(2)


func test_dead_resource_channels_pruned() -> void:
	godot_listener_at(Vector2.ZERO)
	var base: int = int(factory.debug_get_sound_stats()["channels"])
	_offer_temps(3)
	assert_eq(int(factory.debug_get_sound_stats()["channels"]), base + 3, "one channel per resource")
	await physics(4)
	assert_eq(int(factory.debug_get_sound_stats()["channels"]), base, "dead channels pruned, live memory kept")
	factory.stop_sounds()
	await idle(1)


func test_voices_bounded_over_waves() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 4)
	s.min_interval_sec = 0.0
	for wave in 5:
		for i in 4:
			assert_true(factory.play_sound(s, Vector2(i * 10, 0)), "wave %d offer %d" % [wave, i])
		await physics(2)
	var stats: Dictionary = factory.debug_get_sound_stats()
	assert_lte(int(stats["voices_total"]), 8, "pool never grows past the factory cap")
	assert_eq(int(stats["channels"]), 1, "one shared channel for one resource")
	factory.stop_sounds()
	await idle(1)


func test_free_factory_with_busy_voices_cleans_up() -> void:
	check_factory_after = false
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 4)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "voice busy")
	factory.queue_free()
	await idle(2)
	assert_no_new_orphans("voices and container die with the factory")
