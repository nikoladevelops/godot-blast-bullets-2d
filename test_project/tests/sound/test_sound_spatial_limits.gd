extends BlastTest
## Sound spatial mix and shared limits: per-play application of every knob,
## non-positional constants, random bands, spawner volume trim, and one
## shared budget across spawners.


func after_all() -> void:
	# Same headless-audio drain as the other sound suites.
	OS.delay_msec(500)
	await super()


func test_spatial_knobs_applied_per_play() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.max_distance = 1500.0
	s.attenuation = 2.0
	s.panning_strength = 0.5
	s.area_mask = 4
	assert_true(factory.play_sound(s, Vector2(100, 0)), "offer accepted")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "voice busy")
	var v: Dictionary = busy_voices()[0]
	assert_almost_eq(float(v["max_distance"]), 1500.0, 0.01, "max distance applied")
	assert_almost_eq(float(v["attenuation"]), 2.0, 0.01, "attenuation applied")
	assert_almost_eq(float(v["panning_strength"]), 0.5, 0.01, "panning applied")
	assert_eq(int(v["area_mask"]), 4, "area mask applied")


func test_non_positional_uses_constant_full_volume() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.positional = false
	s.volume_db = -3.0
	assert_true(factory.play_sound(s, Vector2(5000, 0)), "distance never drops it")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "voice busy")
	var v: Dictionary = busy_voices()[0]
	assert_almost_eq(v["position"], Vector2.ZERO, Vector2(0.5, 0.5), "centered on the listener")
	assert_almost_eq(float(v["attenuation"]), 0.0, 0.001, "flat attenuation")
	assert_almost_eq(float(v["panning_strength"]), 0.0, 0.001, "centered panning")
	assert_eq(int(v["area_mask"]), 0, "no area overrides")
	assert_gt(float(v["max_distance"]), 1.0e29, "effectively infinite range")
	assert_almost_eq(float(v["volume_db"]), -3.0, 0.01, "constant volume")


func test_random_bands_stay_inside_the_math() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 32)
	s.volume_db = -10.0
	s.pitch_scale = 2.0
	s.random_pitch = 2.0
	s.random_volume_offset_db = 6.0
	for i in 6:
		assert_true(factory.play_sound(s, Vector2(i * 5, 0)), "offer %d accepted" % i)
	await physics(2)
	assert_eq(busy_voices().size(), 6, "six voices busy")
	for e in busy_voices():
		var v: Dictionary = e
		assert_gte(float(v["pitch_scale"]), 1.0, "pitch at least pitch/r")
		assert_lte(float(v["pitch_scale"]), 4.0, "pitch at most pitch*r")
		assert_gte(float(v["volume_db"]), -16.0, "volume at least base-v")
		assert_lte(float(v["volume_db"]), -4.0, "volume at most base+v")


func test_exact_scales_without_random() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.volume_db = -10.0
	s.pitch_scale = 2.0
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	var v: Dictionary = busy_voices()[0]
	assert_almost_eq(float(v["pitch_scale"]), 2.0, 0.001, "no random: exact pitch")
	assert_almost_eq(float(v["volume_db"]), -10.0, 0.001, "no random: exact volume")


func test_spawner_volume_trim_adds_to_every_entry() -> void:
	godot_listener_at(Vector2.ZERO)
	var sp := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.sound_enabled = true
	sp.sound_volume_db = -4.0
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.volume_db = -2.0
	sp.sound_effects = [s]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	await physics(2)
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["volume_db"]), -6.0, 0.01, "trim adds to the entry")


func test_one_resource_shares_one_budget_across_spawners() -> void:
	godot_listener_at(Vector2.ZERO)
	var shared := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	shared.min_interval_sec = 10.0
	var a := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	a.sound_enabled = true
	a.sound_effects = [shared]
	var b := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	b.sound_enabled = true
	b.sound_effects = [shared]
	watch_signals(a)
	watch_signals(b)
	assert_true(a.shoot_once(), "first spawner fires")
	assert_true(b.shoot_once(), "second spawner fires")
	await physics(2)
	assert_eq(int(factory.debug_get_sound_stats()["plays_total"]), 1, "one shared interval: one play")
