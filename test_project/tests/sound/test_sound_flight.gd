extends BlastTest
## Flight trigger: a looped flight entry logs while the volley flies and stops
## after the drain; recommended settings bound a 100-bullet volley to 2 voices;
## unarmed volleys create no voices; frozen bullets stay silent.


func after_all() -> void:
	# Same headless-audio drain as the other sound suites.
	OS.delay_msec(500)
	await super()


func _flight_sound() -> BulletSoundData2D:
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT)
	s.min_interval_sec = 0.1
	return s


func _logged(sound_id: int) -> Array:
	var out: Array = []
	for e in factory.debug_get_sound_log():
		if int((e as Dictionary).get("sound", 0)) == sound_id:
			out.append(e)
	return out


func test_flight_trigger_bound() -> void:
	var s := BulletSoundData2D.new()
	assert_true(s.has_method("set_trigger"), "trigger setter bound")
	s.set_trigger(10)
	assert_eq(s.trigger, 10, "flight trigger sets")
	assert_true(BulletSoundData2D.SOUND_ON_FLIGHT == 10, "flight is trigger 10")


func test_flight_logs_while_flying_and_stops_after_drain() -> void:
	factory.debug_set_sound_log_enabled(true)
	var s := _flight_sound()
	var v := quick_volley()
	assert_true(v.sound_set_effects([s]), "flight entry armed")
	var sid: int = s.get_instance_id()
	await physics(5)
	assert_false(_logged(sid).is_empty(), "logs while flying")
	v.clear_all_bullets()
	factory.debug_clear_sound_log()
	for i in 10:
		await physics(1)
	assert_true(_logged(sid).is_empty(), "silent after the drain")


func test_flight_bounded_to_max_voices() -> void:
	factory.debug_set_sound_log_enabled(true)
	var s := _flight_sound()
	s.min_interval_sec = 0.0 # up to max_voices per sweep, nearest first
	s.max_voices = 2
	var v := quick_volley(100)
	assert_true(v.sound_set_effects([s]), "flight entry armed")
	await physics(5)
	assert_eq(busy_voices().size(), 2, "100-bullet flight bound to 2 voices")


func test_unarmed_volleys_create_no_voices() -> void:
	factory.debug_set_sound_log_enabled(true)
	var v := quick_volley()
	await physics(5)
	assert_false(v.is_sound_armed(), "no flight entry armed")
	assert_eq(busy_voices().size(), 0, "unarmed flight creates no voices")


func test_frozen_bullets_stay_silent() -> void:
	factory.debug_set_sound_log_enabled(true)
	var s := _flight_sound()
	var v := quick_volley()
	assert_true(v.sound_set_effects([s]), "flight entry armed")
	for i in 4:
		v.disable_bullet(i)
	await physics(5)
	var sid: int = s.get_instance_id()
	assert_true(_logged(sid).is_empty(), "frozen bullets never offer flight")
