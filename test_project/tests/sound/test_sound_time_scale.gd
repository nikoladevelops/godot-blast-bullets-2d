extends BlastTest
## Sound under a time scale: an entry with pitch_follows_time_scale (off by
## default) plays at base pitch x the scale of what it belongs to (the factory
## scale, or the followed bullet's volley scale), pauses while that scale is
## ~0, and is neutral again at 1. Plain entries never change. Fades and
## intervals stay in real time.


func _hum(follow_scale: bool, pitch := 1.0) -> BulletSoundData2D:
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.pitch_scale = pitch
	s.pitch_follows_time_scale = follow_scale
	return s


func _play(s: BulletSoundData2D) -> void:
	godot_listener_at(Vector2.ZERO)
	assert_true(factory.play_sound(s, Vector2.ZERO), "play_sound accepted")
	await physics(2)


func test_flag_defaults_off_and_is_a_serialized_bool() -> void:
	var s := BulletSoundData2D.new()
	assert_false(s.pitch_follows_time_scale, "off by default")
	watch_signals(s)
	s.pitch_follows_time_scale = true
	assert_signal_emit_count(s, "changed", 1, "an accepted edit emits changed")
	assert_true("pitch_follows_time_scale" in s, "bound property")


func test_flagged_voice_follows_the_factory_scale() -> void:
	await _play(_hum(true))
	assert_almost_eq(float(busy_voices()[0]["pitch_scale"]), 1.0, 0.001, "real time: base pitch")
	factory.time_scale = 0.5
	await physics(2)
	assert_almost_eq(float(busy_voices()[0]["pitch_scale"]), 0.5, 0.001, "half speed: half pitch")
	factory.time_scale = 2.0
	await physics(2)
	assert_almost_eq(float(busy_voices()[0]["pitch_scale"]), 2.0, 0.001, "double speed: double pitch")
	factory.time_scale = 1.0
	await physics(2)
	assert_almost_eq(float(busy_voices()[0]["pitch_scale"]), 1.0, 0.001, "and back to the base pitch")


func test_unflagged_voice_never_changes() -> void:
	await _play(_hum(false, 1.5))
	factory.time_scale = 0.25
	await physics(3)
	var v: Dictionary = busy_voices()[0]
	assert_almost_eq(float(v["pitch_scale"]), 1.5, 0.001, "pitch untouched")
	assert_false(bool(v["time_paused"]), "and it keeps playing")
	assert_false(bool(v["time_pitch"]), "the scale never touched it")


func test_scale_zero_pauses_and_resuming_unpauses() -> void:
	await _play(_hum(true))
	factory.time_scale = 0.0
	await physics(2)
	assert_true(bool(busy_voices()[0]["time_paused"]), "frozen world: the voice is paused")
	factory.time_scale = 0.5
	await physics(2)
	var v: Dictionary = busy_voices()[0]
	assert_false(bool(v["time_paused"]), "slow motion: playing again")
	assert_almost_eq(float(v["pitch_scale"]), 0.5, 0.001, "at the slowed pitch")


func test_a_sound_started_while_slowed_is_pitched_at_once() -> void:
	factory.time_scale = 0.5
	await _play(_hum(true, 1.2))
	assert_almost_eq(float(busy_voices()[0]["pitch_scale"]), 0.6, 0.001, "base 1.2 x 0.5")


func test_pitch_stays_inside_the_engine_cap() -> void:
	await _play(_hum(true, 900.0))
	factory.time_scale = 4.0
	await physics(2)
	assert_lte(float(busy_voices()[0]["pitch_scale"]), 1000.0, "never past the pinned pitch cap")
	factory.time_scale = 0.001
	await physics(2)
	assert_true(bool(busy_voices()[0]["time_paused"]), "a scale below 0.01 pauses instead of an absurd pitch")


func test_factory_pause_does_not_unpause_a_time_paused_voice() -> void:
	await _play(_hum(true))
	factory.time_scale = 0.0
	# Godot reports a stream pause only once the audio server registered the
	# playback (real time, not frames): wait for it, bounded.
	var engine_paused := false
	for i in 200:
		await physics(1)
		OS.delay_msec(5)
		if bool(busy_voices()[0]["paused"]):
			engine_paused = true
			break
	assert_true(bool(busy_voices()[0]["time_paused"]), "the scale paused it")
	assert_true(engine_paused, "and the engine really paused the stream")
	factory.set_is_factory_processing_bullets(false)
	factory.set_is_factory_processing_bullets(true)
	await physics(2)
	assert_true(bool(busy_voices()[0]["time_paused"]), "still paused by the scale after a hit-stop ended")
	assert_true(bool(busy_voices()[0]["paused"]), "and the stream itself stayed paused")


func test_followed_hum_uses_its_volleys_spawner_scale() -> void:
	godot_listener_at(Vector2.ZERO)
	var hum := H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT)
	hum.follow_bullet = true
	hum.pitch_follows_time_scale = true
	hum.min_interval_sec = 0.0
	var sp := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.time_scale = 0.5
	sp.sound_enabled = true
	sp.sound_effects = [hum]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot")
	await physics(4)
	var voices := busy_voices()
	assert_false(voices.is_empty(), "the hum plays")
	assert_almost_eq(float(voices[0]["pitch_scale"]), 0.5, 0.001, "pitched by the volley's scale (spawner 0.5 x factory 1)")
	factory.time_scale = 0.5
	await physics(3)
	assert_almost_eq(float(busy_voices()[0]["pitch_scale"]), 0.25, 0.001, "factory x spawner")


func test_a_reused_voice_starts_neutral() -> void:
	await _play(_hum(true))
	factory.time_scale = 0.0
	await physics(2)
	factory.stop_sounds()
	factory.time_scale = 1.0
	assert_eq(busy_voices().size(), 0, "stopped")
	await _play(_hum(false, 1.3))
	var v: Dictionary = busy_voices()[0]
	assert_false(bool(v["time_paused"]), "a time-paused voice does not carry its pause into the next sound")
	assert_false(bool(v["time_pitch"]), "nor its flag")
	assert_almost_eq(float(v["pitch_scale"]), 1.3, 0.001, "and plays at its own pitch")
