extends BlastTest
## Sound spatial mix and shared limits: per-play application of every knob,
## non-positional constants, random bands, spawner volume trim, and one
## shared budget across spawners.


func after_all() -> void:
	# Same headless-audio drain as the other sound suites.
	OS.delay_msec(500)
	await super()


func _vol(sound_id: int) -> float:
	for e in busy_voices():
		if int((e as Dictionary)["sound"]) == sound_id:
			return float((e as Dictionary)["volume_db"])
	return NAN


func _logged_count(sound_id: int) -> int:
	var n := 0
	for e in factory.debug_get_sound_log():
		if int((e as Dictionary).get("sound", 0)) == sound_id:
			n += 1
	return n


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


func test_amount_gain_exact_db() -> void:
	godot_listener_at(Vector2.ZERO)
	factory.debug_set_sound_log_enabled(true)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT)
	s.min_interval_sec = 0.0
	s.max_voices = 1
	s.amount_gain_db = 2.0
	s.volume_db = -10.0
	var sid: int = s.get_instance_id()
	var v := quick_volley(100)
	assert_true(v.sound_set_effects([s]), "entry armed")
	await physics(2)
	assert_true(_logged_count(sid) >= 1, "100-bullet flight logs")
	assert_eq(busy_voices().size(), 1, "voice busy")
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["volume_db"]), -6.0, 0.05, "100 bullets at k=2 gives base+4")


func test_min_gate_silences_small_volleys() -> void:
	godot_listener_at(Vector2.ZERO)
	factory.debug_set_sound_log_enabled(true)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT)
	s.min_interval_sec = 0.0
	s.min_volley_amount = 10
	var small := quick_volley(4)
	assert_true(small.sound_set_effects([s]), "entry armed")
	await physics(2)
	assert_eq(int(factory.debug_get_sound_stats()["plays_total"]), 0, "4-bullet volley below min 10 stays silent")


func test_max_gate_silences_huge_volleys() -> void:
	godot_listener_at(Vector2.ZERO)
	factory.debug_set_sound_log_enabled(true)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT)
	s.min_interval_sec = 0.0
	s.max_volley_amount = 10
	var huge := quick_volley(100)
	assert_true(huge.sound_set_effects([s]), "entry armed")
	var before: int = int(factory.debug_get_sound_stats()["plays_total"])
	await physics(2)
	assert_eq(int(factory.debug_get_sound_stats()["plays_total"]), before, "100-bullet volley past max 10 stays silent")


func test_manual_hatch_counts_as_one() -> void:
	godot_listener_at(Vector2.ZERO)
	factory.debug_set_sound_log_enabled(true)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.min_interval_sec = 0.0
	s.min_volley_amount = 2
	var sid: int = s.get_instance_id()
	factory.play_sound(s, Vector2.ZERO)
	await physics(2)
	assert_eq(_logged_count(sid), 0, "manual hatch counts as one: below min 2 stays silent")


func test_amount_setters_reject_and_keep() -> void:
	var s := BulletSoundData2D.new()
	assert_eq(s.min_volley_amount, 0, "min off by default")
	assert_eq(s.max_volley_amount, 0, "max off by default")
	assert_eq(s.amount_gain_db, 0.0, "gain off by default")
	s.set_min_volley_amount(-1)
	expect_error_sequence(["BulletSoundData2D: min_volley_amount must be >= 0, keeping the old value."])
	assert_eq(s.min_volley_amount, 0, "kept")
	s.set_max_volley_amount(-1)
	expect_error_sequence(["BulletSoundData2D: max_volley_amount must be >= 0, keeping the old value."])
	assert_eq(s.max_volley_amount, 0, "kept")
	s.set_amount_gain_db(-1.0)
	expect_error_sequence(["BulletSoundData2D: amount_gain_db must be finite and >= 0, keeping the old value."])
	assert_eq(s.amount_gain_db, 0.0, "kept")
	s.set_amount_gain_db(NAN)
	expect_error_sequence(["BulletSoundData2D: amount_gain_db must be finite and >= 0, keeping the old value."])


func test_volume_clamp_applies_last() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.volume_db = 0.0
	s.set("volume_min_db", -6.0)
	s.set("volume_max_db", 6.0)
	# Loud entry clamps to the ceiling, quiet entry to the floor.
	s.volume_db = 30.0
	assert_true(factory.play_sound(s, Vector2.ZERO), "loud offer accepted")
	await physics(2)
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["volume_db"]), 6.0, 0.05, "ceiling clamps the mix")
	factory.stop_sounds()
	await idle(1)
	s.volume_db = -30.0
	assert_true(factory.play_sound(s, Vector2.ZERO), "quiet offer accepted")
	await physics(2)
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["volume_db"]), -6.0, 0.05, "floor clamps the mix")
	factory.stop_sounds()
	await idle(1)
	# Inverted bounds sort themselves (no setter cross-check): still clamps.
	s.volume_db = 30.0
	s.set("volume_min_db", 6.0)
	s.set("volume_max_db", -6.0)
	assert_true(factory.play_sound(s, Vector2.ZERO), "inverted offer accepted")
	await physics(2)
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["volume_db"]), 6.0, 0.05, "inverted bounds still clamp")


func test_pitch_clamp_applies_last() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.pitch_scale = 8.0
	s.set("pitch_min", 0.5)
	s.set("pitch_max", 2.0)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["pitch_scale"]), 2.0, 0.01, "ceiling clamps the pitch")


func test_asymmetric_volume_random_stays_inside() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 32)
	s.volume_db = -10.0
	s.set("random_volume_min_db", -3.0)
	s.set("random_volume_max_db", 1.0)
	for i in 12:
		assert_true(factory.play_sound(s, Vector2(i * 5, 0)), "offer %d accepted" % i)
	await physics(2)
	assert_eq(busy_voices().size(), 12, "twelve voices busy")
	var vols: Array = []
	for e in busy_voices():
		vols.append(float((e as Dictionary)["volume_db"]))
	vols.sort()
	assert_gte(vols[0], -13.0, "at least base+min")
	assert_lte(vols[vols.size() - 1], -9.0, "at most base+max")
	assert_gt(vols[vols.size() - 1] - vols[0], 0.5, "rolls vary across the range")


func test_deterministic_volume_offset_without_range() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.volume_db = -10.0
	s.set("random_volume_min_db", 2.0)
	s.set("random_volume_max_db", 2.0)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["volume_db"]), -8.0, 0.01, "min==max adds without RNG")


func test_deterministic_pitch_offset_without_range() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.pitch_scale = 2.0
	s.set("random_pitch_min", 1.5)
	s.set("random_pitch_max", 1.5)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["pitch_scale"]), 3.0, 0.01, "min==max multiplies without RNG")


func test_random_pan_spreads_positional_voices() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 32)
	s.set("random_pan", 1.0)
	for i in 12:
		assert_true(factory.play_sound(s, Vector2(100, 0)), "offer %d accepted" % i)
	await physics(2)
	assert_eq(busy_voices().size(), 12, "twelve voices busy")
	var ys: Array = []
	for e in busy_voices():
		ys.append(float((e as Dictionary)["position"].y))
		assert_almost_eq(float((e as Dictionary)["position"].x), 100.0, 0.6, "x stays on the event")
	ys.sort()
	assert_gt(ys[ys.size() - 1] - ys[0], 10.0, "perpendicular offsets vary the placement")
	assert_gte(ys[0], -100.0, "jitter bounded by distance")
	assert_lte(ys[ys.size() - 1], 100.0, "jitter bounded by distance")


func test_random_pan_leaves_centered_voices_centered() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.positional = false
	s.set("random_pan", 1.0)
	assert_true(factory.play_sound(s, Vector2(5000, 0)), "distance never drops it")
	await physics(2)
	var v: Dictionary = busy_voices()[0]
	assert_almost_eq(v["position"], Vector2.ZERO, Vector2(0.5, 0.5), "still centered on the listener")
	assert_almost_eq(float(v["panning_strength"]), 0.0, 0.001, "still no panning")


func test_random_pan_setter_rejects_and_keeps() -> void:
	var s := BulletSoundData2D.new()
	assert_has_method(s, "set_random_pan", "bound set_random_pan")
	assert_has_method(s, "get_random_pan", "bound get_random_pan")
	assert_eq(s.get("random_pan"), 0.0, "off by default")
	s.set("random_pan", NAN)
	expect_error_sequence(["BulletSoundData2D: random_pan must be finite and between 0 and 1, keeping the old value."])
	assert_eq(s.get("random_pan"), 0.0, "kept")
	s.set("random_pan", 1.5)
	expect_error_sequence(["BulletSoundData2D: random_pan must be finite and between 0 and 1, keeping the old value."])
	s.set("random_pan", 0.5)
	assert_eq(s.get("random_pan"), 0.5, "valid value applies")


func test_volley_non_positional_plays_centered() -> void:
	godot_listener_at(Vector2.ZERO)
	var d := H.make_volley_data(1, 0.0, 30.0)
	d.transforms = [Transform2D(0.0, Vector2(5000, 0))] as Array
	var v := factory.spawn_volley(d)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	s.positional = false
	assert_true(v.sound_set_effects([s]), "entry armed")
	assert_true(v.clear_bullet(0), "cleared")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "voice busy despite the far event")
	var b: Dictionary = busy_voices()[0]
	assert_almost_eq(b["position"], Vector2.ZERO, Vector2(0.5, 0.5), "centered on the listener")
	assert_almost_eq(float(b["attenuation"]), 0.0, 0.001, "flat attenuation from a volley too")


func test_occlusion_setters_reject_and_keep() -> void:
	var s := BulletSoundData2D.new()
	assert_has_method(s, "set_occlusion_mask", "bound set_occlusion_mask")
	assert_has_method(s, "get_occlusion_mask", "bound get_occlusion_mask")
	assert_has_method(s, "set_occlusion_db", "bound set_occlusion_db")
	assert_has_method(s, "get_occlusion_db", "bound get_occlusion_db")
	assert_eq(s.get("occlusion_mask"), 0, "off by default")
	assert_eq(s.get("occlusion_db"), 0.0, "no penalty by default")
	s.set("occlusion_mask", -1)
	expect_error_sequence(["BulletSoundData2D: occlusion_mask must be >= 0 (0 disables occlusion), keeping the old value."])
	assert_eq(s.get("occlusion_mask"), 0, "kept")
	s.set("occlusion_db", -1.0)
	expect_error_sequence(["BulletSoundData2D: occlusion_db must be finite and >= 0, keeping the old value."])
	assert_eq(s.get("occlusion_db"), 0.0, "kept")
	s.set("occlusion_db", NAN)
	expect_error_sequence(["BulletSoundData2D: occlusion_db must be finite and >= 0, keeping the old value."])
	s.set("occlusion_mask", 4)
	s.set("occlusion_db", 6.0)
	assert_eq(s.get("occlusion_mask"), 4, "valid mask applies")
	assert_eq(s.get("occlusion_db"), 6.0, "valid penalty applies")


func test_wall_between_listener_and_event_muffles() -> void:
	godot_listener_at(Vector2.ZERO)
	var wall := make_wall(Vector2(250, 0))
	await physics(2)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.volume_db = -6.0
	s.set("occlusion_mask", 4)
	s.set("occlusion_db", 6.0)
	var sid: int = s.get_instance_id()
	assert_true(factory.play_sound(s, Vector2(500, 0)), "offer accepted")
	await physics(2)
	assert_almost_eq(_vol(sid), -12.0, 0.05, "wall on the ray dips the play")
	factory.stop_sounds()
	await idle(1)
	wall.position.x = 2000.0
	await physics(2)
	assert_true(factory.play_sound(s, Vector2(500, 0)), "offer accepted again")
	await physics(2)
	assert_almost_eq(_vol(sid), -6.0, 0.05, "moved wall re-evaluated: full volume")


func test_occlusion_off_or_wrong_mask_plays_full() -> void:
	godot_listener_at(Vector2.ZERO)
	make_wall(Vector2(250, 0))
	await physics(2)
	var off := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	off.volume_db = -6.0
	assert_true(factory.play_sound(off, Vector2(500, 0)), "mask 0 offers")
	await physics(2)
	assert_almost_eq(_vol(off.get_instance_id()), -6.0, 0.05, "mask 0 never raycasts")
	factory.stop_sounds()
	await idle(1)
	var other := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	other.volume_db = -6.0
	other.set("occlusion_mask", 8)
	other.set("occlusion_db", 6.0)
	assert_true(factory.play_sound(other, Vector2(500, 0)), "wrong mask offers")
	await physics(2)
	assert_almost_eq(_vol(other.get_instance_id()), -6.0, 0.05, "ray misses the wall layer")


func _ensure_test_bus(bus_name: StringName) -> void:
	if AudioServer.get_bus_index(bus_name) < 0:
		AudioServer.add_bus()
		AudioServer.set_bus_name(AudioServer.get_bus_count() - 1, bus_name)


func _drop_test_bus(bus_name: StringName) -> void:
	var i := AudioServer.get_bus_index(bus_name)
	if i > 0:
		AudioServer.remove_bus(i)


func test_occlusion_bus_routes_muffled() -> void:
	_drop_test_bus(&"Muffled")
	_ensure_test_bus(&"Muffled")
	godot_listener_at(Vector2.ZERO)
	var wall := make_wall(Vector2(250, 0))
	await physics(2)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.volume_db = -6.0
	s.set("occlusion_mask", 4)
	s.set("occlusion_db", 6.0)
	s.set("occlusion_bus", &"Muffled")
	var sid: int = s.get_instance_id()
	assert_true(factory.play_sound(s, Vector2(500, 0)), "offer accepted")
	await physics(2)
	assert_eq(String((busy_voices()[0] as Dictionary)["bus"]), "Muffled", "occluded play routes to the muffle bus")
	assert_almost_eq(_vol(sid), -12.0, 0.05, "dip and bus switch together")
	factory.stop_sounds()
	await idle(1)
	wall.position.x = 2000.0
	await physics(2)
	assert_true(factory.play_sound(s, Vector2(500, 0)), "offer accepted again")
	await physics(2)
	assert_eq(String((busy_voices()[0] as Dictionary)["bus"]), "Master", "clear play stays on the entry bus")
	_drop_test_bus(&"Muffled")


func test_occlusion_bus_unknown_falls_back() -> void:
	godot_listener_at(Vector2.ZERO)
	make_wall(Vector2(250, 0))
	await physics(2)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.set("occlusion_mask", 4)
	s.set("occlusion_db", 6.0)
	s.set("occlusion_bus", &"NoSuchBus_xyz")
	assert_true(factory.play_sound(s, Vector2(500, 0)), "offer accepted")
	await physics(2)
	expect_warning_sequence(["BulletSoundData2D: bus \"NoSuchBus_xyz\" does not exist, playing on Master."])
	assert_eq(busy_voices().size(), 1, "still plays on Master")
	assert_eq(String((busy_voices()[0] as Dictionary)["bus"]), "Master", "unknown muffle bus falls back")


func test_occlusion_bus_empty_and_gated() -> void:
	var s := BulletSoundData2D.new()
	assert_has_method(s, "set_occlusion_bus", "bound set_occlusion_bus")
	assert_has_method(s, "get_occlusion_bus", "bound get_occlusion_bus")
	assert_true("occlusion_bus" in s, "property exists")
	assert_eq(s.get("occlusion_bus"), &"", "empty disables the switch")
	assert_false(is_editor_visible(s, &"occlusion_bus"), "hidden while mask is 0")
	assert_false(is_editor_visible(s, &"occlusion_db"), "dip hidden while mask is 0")
	s.set("occlusion_mask", 4)
	assert_true(is_editor_visible(s, &"occlusion_bus"), "switch visible with a mask")
	assert_true(is_editor_visible(s, &"occlusion_db"), "dip visible with a mask")
	s.set("occlusion_bus", &"Muffled")
	assert_eq(s.get("occlusion_bus"), &"Muffled", "any name stores, validity resolves at play")


func test_explain_reports_occlusion_and_base() -> void:
	assert_has_method(factory, "debug_explain_sound", "bound explainer")
	if not factory.has_method("debug_explain_sound"):
		return
	godot_listener_at(Vector2.ZERO)
	var wall := make_wall(Vector2(250, 0))
	await physics(2)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.volume_db = -6.0
	s.set("occlusion_mask", 4)
	s.set("occlusion_db", 6.0)
	var hit: Dictionary = factory.debug_explain_sound(s, Vector2(500, 0))
	assert_true(bool(hit["audible"]), "muffled is not dropped")
	assert_true(bool(hit["occluded"]), "ray hit reported")
	assert_almost_eq(float(hit["volume_base"]), -12.0, 0.05, "base includes the dip")
	wall.position.x = 2000.0
	await physics(2)
	var clear: Dictionary = factory.debug_explain_sound(s, Vector2(500, 0))
	assert_false(bool(clear["occluded"]), "moved wall re-evaluated")
	assert_almost_eq(float(clear["volume_base"]), -6.0, 0.05, "base without the dip")
