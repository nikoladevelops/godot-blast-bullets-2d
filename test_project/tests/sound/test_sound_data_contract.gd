extends BlastTest
## BulletSoundData2D contract: defaults, happy path, reject-and-keep with the
## exact wording, changed-emission discipline, hint strings and inspector gating.
## A weaker model extends this file: one hostile value per setter, exact text.


func test_bind_and_defaults() -> void:
	var s := BulletSoundData2D.new()
	for m in ["set_enabled", "get_enabled", "set_trigger", "get_trigger", "set_volume_db", "get_volume_db", "set_pitch_scale", "get_pitch_scale", "set_random_pitch", "get_random_pitch", "set_random_volume_offset_db", "get_random_volume_offset_db", "set_trigger_chance", "get_trigger_chance", "set_bus", "get_bus", "set_positional", "get_positional", "set_max_distance", "get_max_distance", "set_attenuation", "get_attenuation", "set_panning_strength", "get_panning_strength", "set_area_mask", "get_area_mask", "set_min_interval_sec", "get_min_interval_sec", "set_max_voices", "get_max_voices", "set_when_limit_reached", "get_when_limit_reached", "set_priority", "get_priority", "set_max_duration_sec", "get_max_duration_sec"]:
		assert_has_method(s, m, "bound " + m)
	assert_true(s.enabled, "enabled defaults true")
	assert_eq(s.trigger, BulletSoundData2D.SOUND_ON_SHOT, "trigger defaults On Shot")
	assert_false(s.has_method("set_stream"), "single stream removed")
	assert_false(s.has_method("get_stream"), "single stream removed")
	assert_false("stream" in s, "single stream property removed")
	assert_true((s.streams as Array).is_empty(), "list empty by default")
	assert_eq(s.volume_db, 0.0, "volume_db defaults 0")
	assert_eq(s.pitch_scale, 1.0, "pitch_scale defaults 1")
	assert_eq(s.random_pitch, 1.0, "random_pitch defaults 1")
	assert_eq(s.random_volume_offset_db, 0.0, "random offset defaults 0")
	assert_eq(s.trigger_chance, 1.0, "trigger_chance defaults 1")
	assert_eq(s.bus, &"Master", "bus defaults Master")
	assert_true(s.positional, "positional defaults true")
	assert_eq(s.max_distance, 2000.0, "max_distance defaults 2000")
	assert_eq(s.attenuation, 1.0, "attenuation defaults 1")
	assert_eq(s.panning_strength, 1.0, "panning defaults 1")
	assert_eq(s.area_mask, 1, "area_mask defaults 1")
	assert_eq(s.min_interval_sec, 0.05, "interval defaults 0.05")
	assert_eq(s.max_voices, 4, "max_voices defaults 4")
	assert_eq(s.when_limit_reached, BulletSoundData2D.SOUND_LIMIT_REPLACE_OLDEST, "limit mode defaults Replace Oldest")
	assert_eq(s.priority, 0, "priority defaults 0")
	assert_eq(s.max_duration_sec, 0.0, "max duration defaults 0")
	assert_eq(BulletSoundData2D.SOUND_ON_TELEGRAPH, 9, "telegraph id serialized")
	assert_eq(BulletSoundData2D.SOUND_ON_FLIGHT, 10, "flight id appended")
	assert_eq(BulletSoundData2D.SOUND_LIMIT_SKIP_NEW, 1, "skip-new id serialized")


func test_trigger_rejects_and_keeps() -> void:
	var s := BulletSoundData2D.new()
	s.trigger = BulletSoundData2D.SOUND_ON_GRAZE
	s.set_trigger(-1)
	expect_error_sequence(["BulletSoundData2D: trigger must be 0 (On Shot), 1 (On Hit), 2 (On Destroy), 3 (On Bounce), 4 (On Lifetime Over), 5 (On Clear), 6 (On Homing Target Reached), 7 (On Graze), 8 (On Graze Exit), 9 (On Telegraph) or 10 (On Flight), keeping the old value."])
	assert_eq(s.trigger, BulletSoundData2D.SOUND_ON_GRAZE, "old trigger kept")
	s.set_trigger(11)
	expect_error_sequence(["BulletSoundData2D: trigger must be 0 (On Shot), 1 (On Hit), 2 (On Destroy), 3 (On Bounce), 4 (On Lifetime Over), 5 (On Clear), 6 (On Homing Target Reached), 7 (On Graze), 8 (On Graze Exit), 9 (On Telegraph) or 10 (On Flight), keeping the old value."])
	assert_eq(s.trigger, BulletSoundData2D.SOUND_ON_GRAZE, "old trigger kept")
	s.set_trigger(BulletSoundData2D.SOUND_ON_FLIGHT)
	assert_eq(s.trigger, BulletSoundData2D.SOUND_ON_FLIGHT, "flight trigger applies")


func test_float_setters_reject_and_keep() -> void:
	var s := BulletSoundData2D.new()
	s.set_volume_db(NAN)
	expect_error_sequence(["BulletSoundData2D: volume_db must be finite, keeping the old value."])
	assert_eq(s.volume_db, 0.0, "volume_db kept")
	s.set_pitch_scale(0.0)
	expect_error_sequence(["BulletSoundData2D: pitch_scale must be finite and >= 0.01, keeping the old value."])
	assert_eq(s.pitch_scale, 1.0, "pitch_scale kept")
	s.set_pitch_scale(INF)
	expect_error_sequence(["BulletSoundData2D: pitch_scale must be finite and >= 0.01, keeping the old value."])
	s.set_random_pitch(0.5)
	expect_error_sequence(["BulletSoundData2D: random_pitch must be finite and between 1 and 16, keeping the old value."])
	assert_eq(s.random_pitch, 1.0, "random_pitch kept")
	s.set_random_pitch(17.0)
	expect_error_sequence(["BulletSoundData2D: random_pitch must be finite and between 1 and 16, keeping the old value."])
	s.set_random_volume_offset_db(-1.0)
	expect_error_sequence(["BulletSoundData2D: random_volume_offset_db must be finite and between 0 and 40, keeping the old value."])
	assert_eq(s.random_volume_offset_db, 0.0, "random offset kept")
	s.set_random_volume_offset_db(41.0)
	expect_error_sequence(["BulletSoundData2D: random_volume_offset_db must be finite and between 0 and 40, keeping the old value."])
	s.set_trigger_chance(2.0)
	expect_error_sequence(["BulletSoundData2D: trigger_chance must be finite and between 0 and 1, keeping the old value."])
	assert_eq(s.trigger_chance, 1.0, "trigger_chance kept")
	s.set_trigger_chance(NAN)
	expect_error_sequence(["BulletSoundData2D: trigger_chance must be finite and between 0 and 1, keeping the old value."])
	s.set_max_distance(0.0)
	expect_error_sequence(["BulletSoundData2D: max_distance must be finite and >= 1, keeping the old value."])
	assert_eq(s.max_distance, 2000.0, "max_distance kept")
	s.set_attenuation(-0.5)
	expect_error_sequence(["BulletSoundData2D: attenuation must be finite and >= 0, keeping the old value."])
	assert_eq(s.attenuation, 1.0, "attenuation kept")
	s.set_panning_strength(-1.0)
	expect_error_sequence(["BulletSoundData2D: panning_strength must be finite and >= 0, keeping the old value."])
	assert_eq(s.panning_strength, 1.0, "panning kept")
	s.set_min_interval_sec(-1.0)
	expect_error_sequence(["BulletSoundData2D: min_interval_sec must be finite and >= 0, keeping the old value."])
	assert_eq(s.min_interval_sec, 0.05, "interval kept")
	s.set_max_duration_sec(-1.0)
	expect_error_sequence(["BulletSoundData2D: max_duration_sec must be finite and >= 0, keeping the old value."])
	assert_eq(s.max_duration_sec, 0.0, "max duration kept")
	s.set_volume_db(-6.0)
	assert_eq(s.volume_db, -6.0, "valid volume applies")


func test_int_and_bus_setters_reject_and_keep() -> void:
	var s := BulletSoundData2D.new()
	s.set_bus(&"")
	expect_error_sequence(["BulletSoundData2D: bus must not be empty, keeping the old value."])
	assert_eq(s.bus, &"Master", "bus kept")
	s.set_area_mask(-1)
	expect_error_sequence(["BulletSoundData2D: area_mask must be >= 0, keeping the old value."])
	assert_eq(s.area_mask, 1, "area_mask kept")
	s.set_max_voices(0)
	expect_error_sequence(["BulletSoundData2D: max_voices must be between 1 and 32, keeping the old value."])
	assert_eq(s.max_voices, 4, "max_voices kept")
	s.set_max_voices(33)
	expect_error_sequence(["BulletSoundData2D: max_voices must be between 1 and 32, keeping the old value."])
	s.set_when_limit_reached(2)
	expect_error_sequence(["BulletSoundData2D: when_limit_reached must be 0 (Replace Oldest) or 1 (Skip New), keeping the old value."])
	assert_eq(s.when_limit_reached, BulletSoundData2D.SOUND_LIMIT_REPLACE_OLDEST, "limit mode kept")
	s.set_priority(17)
	expect_error_sequence(["BulletSoundData2D: priority must be between -16 and 16, keeping the old value."])
	assert_eq(s.priority, 0, "priority kept")
	s.set_priority(-17)
	expect_error_sequence(["BulletSoundData2D: priority must be between -16 and 16, keeping the old value."])
	s.set_priority(-5)
	assert_eq(s.priority, -5, "valid priority applies")


func test_changed_emission_discipline() -> void:
	var s := BulletSoundData2D.new()
	var log: Array = []
	s.changed.connect(func() -> void: log.append(1))
	s.set_volume_db(0.0) # same value: silent
	s.set_enabled(true) # same value: silent
	s.set_bus(&"Master") # same value: silent
	assert_eq(log.size(), 0, "same-value sets emit nothing")
	s.set_volume_db(-3.0)
	s.set_enabled(false)
	s.set_bus(&"Effects")
	assert_eq(log.size(), 3, "accepted changes emit changed")


func test_hint_strings_byte_exact() -> void:
	var s := BulletSoundData2D.new()
	var hints := {}
	for p in s.get_property_list():
		hints[StringName(p.get("name", ""))] = str(p.get("hint_string", ""))
	assert_eq(hints.get(&"volume_db", ""), "-80,80,0.1,suffix:dB", "volume hint")
	assert_eq(hints.get(&"pitch_scale", ""), "0.01,4,0.01,or_greater", "pitch hint")
	assert_eq(hints.get(&"random_pitch", ""), "1,16,0.01", "random pitch hint")
	assert_eq(hints.get(&"random_volume_offset_db", ""), "0,40,0.01,suffix:dB", "random volume hint")
	assert_eq(hints.get(&"random_volume_min_db", ""), "-40,40,0.01,suffix:dB", "random volume min hint")
	assert_eq(hints.get(&"random_volume_max_db", ""), "-40,40,0.01,suffix:dB", "random volume max hint")
	assert_eq(hints.get(&"random_pitch_min", ""), "0.01,16,0.01", "random pitch min hint")
	assert_eq(hints.get(&"random_pitch_max", ""), "0.01,16,0.01", "random pitch max hint")
	assert_eq(hints.get(&"random_pan", ""), "0,1,0.01", "random pan hint")
	assert_eq(hints.get(&"volume_min_db", ""), "-80,80,0.1,suffix:dB", "volume floor hint")
	assert_eq(hints.get(&"volume_max_db", ""), "-80,80,0.1,suffix:dB", "volume ceiling hint")
	assert_eq(hints.get(&"pitch_min", ""), "0.01,16,0.01,or_greater", "pitch floor hint")
	assert_eq(hints.get(&"pitch_max", ""), "0.01,16,0.01,or_greater", "pitch ceiling hint")
	assert_eq(hints.get(&"fade_out_sec", ""), "0,10,0.01,or_greater,suffix:s", "fade hint")
	assert_eq(hints.get(&"duck_amount_db", ""), "0,24,0.01,or_greater,suffix:dB", "duck hint")
	assert_eq(hints.get(&"zoom_gain_db", ""), "-24,24,0.01,suffix:dB", "zoom gain hint")
	assert_eq(hints.get(&"occlusion_db", ""), "0,24,0.01,or_greater,suffix:dB", "occlusion hint")
	assert_eq(hints.get(&"trigger_chance", ""), "0,1,0.01", "chance hint")
	assert_eq(hints.get(&"max_distance", ""), "1,4096,1,or_greater,exp,suffix:px", "max distance hint")
	assert_eq(hints.get(&"panning_strength", ""), "0,3,0.01,or_greater", "panning hint")
	assert_eq(hints.get(&"min_interval_sec", ""), "0,1,0.001,or_greater,suffix:s", "interval hint")
	assert_eq(hints.get(&"max_voices", ""), "1,32,1,or_greater", "max voices hint")
	assert_eq(hints.get(&"priority", ""), "-16,16,1,or_greater,or_less", "priority hint")
	assert_eq(hints.get(&"max_duration_sec", ""), "0,10,0.01,or_greater,suffix:s", "duration hint")
	assert_string_contains(hints.get(&"bus", ""), "Master", "bus enum lists Master")


func test_spatial_knobs_hide_while_not_positional() -> void:
	var s := BulletSoundData2D.new()
	assert_true(is_editor_visible(s, &"max_distance"), "spatial knobs visible while positional")
	s.positional = false
	assert_false(is_editor_visible(s, &"max_distance"), "max_distance hidden")
	assert_false(is_editor_visible(s, &"attenuation"), "attenuation hidden")
	assert_false(is_editor_visible(s, &"panning_strength"), "panning hidden")
	assert_false(is_editor_visible(s, &"area_mask"), "area_mask hidden")
	assert_true(is_editor_visible(s, &"positional"), "positional itself stays")


func test_ring_index_visible_only_for_graze_triggers() -> void:
	var s := BulletSoundData2D.new()
	# Default trigger On Shot: ring makes no sense (ignored by non-graze sites).
	assert_false(is_editor_visible(s, &"ring_index"), "ring hidden for On Shot")
	s.trigger = BulletSoundData2D.SOUND_ON_HIT
	assert_false(is_editor_visible(s, &"ring_index"), "ring hidden for On Hit")
	s.trigger = BulletSoundData2D.SOUND_ON_GRAZE
	assert_true(is_editor_visible(s, &"ring_index"), "ring visible for On Graze")
	s.trigger = BulletSoundData2D.SOUND_ON_GRAZE_EXIT
	assert_true(is_editor_visible(s, &"ring_index"), "ring visible for On Graze Exit")
	s.trigger = BulletSoundData2D.SOUND_ON_FLIGHT
	assert_false(is_editor_visible(s, &"ring_index"), "ring hidden for On Flight")


func test_stream_property_removed_list_only() -> void:
	var s := BulletSoundData2D.new()
	assert_false(s.has_method("set_stream"), "no single-stream setter")
	assert_false(s.has_method("get_stream"), "no single-stream getter")
	assert_false(is_editor_visible(s, &"stream"), "no single-stream inspector entry")
	assert_false("stream" in s, "no single-stream property")
	# A one-entry list is the single-sound path now (no fallback involved).
	s.streams = [H.make_sound_stream()]
	assert_eq((s.streams as Array).size(), 1, "one-entry list holds the sound")


func test_stream_weights_visible_only_for_weighted_with_list() -> void:
	var s := BulletSoundData2D.new()
	# Empty list: nothing to weight, even in Weighted mode.
	s.stream_mode = BulletSoundData2D.STREAM_WEIGHTED
	assert_false(is_editor_visible(s, &"stream_weights"), "weights hidden with empty list")
	s.streams = [H.make_sound_stream(), H.make_sound_stream()]
	assert_true(is_editor_visible(s, &"stream_weights"), "weights visible for Weighted with a list")
	s.stream_mode = BulletSoundData2D.STREAM_RANDOM
	assert_false(is_editor_visible(s, &"stream_weights"), "weights hidden for Random")
	s.stream_mode = BulletSoundData2D.STREAM_SEQUENCE
	assert_false(is_editor_visible(s, &"stream_weights"), "weights hidden for Sequence")


func test_new_random_and_limit_binds_and_defaults() -> void:
	var s := BulletSoundData2D.new()
	for m in ["set_random_volume_min_db", "get_random_volume_min_db", "set_random_volume_max_db", "get_random_volume_max_db", "set_random_pitch_min", "get_random_pitch_min", "set_random_pitch_max", "get_random_pitch_max", "set_volume_min_db", "get_volume_min_db", "set_volume_max_db", "get_volume_max_db", "set_pitch_min", "get_pitch_min", "set_pitch_max", "get_pitch_max"]:
		assert_has_method(s, m, "bound " + m)
	for k in ["random_volume_min_db", "random_volume_max_db", "random_pitch_min", "random_pitch_max", "volume_min_db", "volume_max_db", "pitch_min", "pitch_max"]:
		assert_true(k in s, "property " + k + " exists")
	assert_eq(s.get("random_volume_min_db"), 0.0, "random volume min off by default")
	assert_eq(s.get("random_volume_max_db"), 0.0, "random volume max off by default")
	assert_eq(s.get("random_pitch_min"), 1.0, "random pitch min off by default")
	assert_eq(s.get("random_pitch_max"), 1.0, "random pitch max off by default")
	assert_eq(s.get("volume_min_db"), -80.0, "volume floor defaults to silence")
	assert_eq(s.get("volume_max_db"), 80.0, "volume ceiling defaults wide open")
	assert_eq(s.get("pitch_min"), 0.01, "pitch floor defaults to setter floor")
	assert_eq(s.get("pitch_max"), 16.0, "pitch ceiling defaults to random ceiling")


func test_new_random_and_limit_setters_reject_and_keep() -> void:
	var s := BulletSoundData2D.new()
	s.set("random_volume_min_db", NAN)
	expect_error_sequence(["BulletSoundData2D: random_volume_min_db must be finite and between -40 and 40, keeping the old value."])
	assert_eq(s.get("random_volume_min_db"), 0.0, "kept")
	s.set("random_volume_min_db", 41.0)
	expect_error_sequence(["BulletSoundData2D: random_volume_min_db must be finite and between -40 and 40, keeping the old value."])
	s.set("random_volume_max_db", -41.0)
	expect_error_sequence(["BulletSoundData2D: random_volume_max_db must be finite and between -40 and 40, keeping the old value."])
	assert_eq(s.get("random_volume_max_db"), 0.0, "kept")
	s.set("random_pitch_min", 0.0)
	expect_error_sequence(["BulletSoundData2D: random_pitch_min must be finite and between 0.01 and 16, keeping the old value."])
	assert_eq(s.get("random_pitch_min"), 1.0, "kept")
	s.set("random_pitch_min", 17.0)
	expect_error_sequence(["BulletSoundData2D: random_pitch_min must be finite and between 0.01 and 16, keeping the old value."])
	s.set("random_pitch_max", NAN)
	expect_error_sequence(["BulletSoundData2D: random_pitch_max must be finite and between 0.01 and 16, keeping the old value."])
	assert_eq(s.get("random_pitch_max"), 1.0, "kept")
	s.set("volume_min_db", NAN)
	expect_error_sequence(["BulletSoundData2D: volume_min_db must be finite, keeping the old value."])
	assert_eq(s.get("volume_min_db"), -80.0, "kept")
	s.set("volume_max_db", INF)
	expect_error_sequence(["BulletSoundData2D: volume_max_db must be finite, keeping the old value."])
	assert_eq(s.get("volume_max_db"), 80.0, "kept")
	s.set("pitch_min", 0.0)
	expect_error_sequence(["BulletSoundData2D: pitch_min must be finite and >= 0.01, keeping the old value."])
	assert_eq(s.get("pitch_min"), 0.01, "kept")
	s.set("pitch_max", NAN)
	expect_error_sequence(["BulletSoundData2D: pitch_max must be finite and >= 0.01, keeping the old value."])
	assert_eq(s.get("pitch_max"), 16.0, "kept")
	# Accepted values apply and emit changed (same-value sets stay silent).
	var log: Array = []
	s.changed.connect(func() -> void: log.append(1))
	s.set("random_volume_min_db", -3.0)
	s.set("random_volume_max_db", 2.0)
	s.set("volume_max_db", 6.0)
	assert_eq(log.size(), 3, "accepted changes emit changed")
	s.set("volume_max_db", 6.0)
	assert_eq(log.size(), 3, "same-value set emits nothing")
