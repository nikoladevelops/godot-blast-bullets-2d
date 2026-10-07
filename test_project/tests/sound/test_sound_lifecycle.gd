extends BlastTest
## Sound lifecycle: pause freezes busy voices and drops offers, resume
## unpauses, reset/stop silence everything, pool reuse is neutral, and an
## unused factory creates no voices at all (zero cost).


func after_all() -> void:
	# Same headless-audio drain as the other sound suites.
	OS.delay_msec(500)
	await super()


func test_pause_freezes_busy_voices_and_drops_offers() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(6) # let the dummy driver's playback establish: pausing a
	assert_eq(busy_voices().size(), 1, "voice busy") # just-started playback silently ignores the pause headless
	factory.set_is_factory_processing_bullets(false)
	assert_true(bool((busy_voices()[0] as Dictionary).get("paused", false)), "busy voice paused")
	assert_false(factory.play_sound(s, Vector2(10, 0)), "paused offer dropped")
	await physics(2)
	assert_eq(int(factory.debug_get_sound_stats()["pending"]), 0, "nothing pending while paused")
	factory.set_is_factory_processing_bullets(true)
	assert_false(bool((busy_voices()[0] as Dictionary).get("paused", true)), "resume unpauses")
	assert_true(factory.play_sound(s, Vector2(20, 0)), "offer accepted after resume")


func test_reset_stops_busy_and_forgets() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "voice busy")
	await idle(1)
	factory.reset()
	await idle(1)
	assert_eq(busy_voices().size(), 0, "reset stops every voice")
	var stats: Dictionary = factory.debug_get_sound_stats()
	assert_eq(int(stats["channels"]), 0, "channels forgotten")
	assert_eq(int(stats["pending"]), 0, "nothing pending")


func test_reuse_rearms_cleanly_after_release() -> void:
	factory.debug_set_sound_log_enabled(true)
	var first := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0, 30.0))
	v.sound_set_effects([first])
	assert_eq(v.clear_all_bullets(), 1, "drained to the pool")
	await physics(2)
	assert_eq(_logged(first.get_instance_id()).size(), 1, "first life sounded")
	var w: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0, 30.0))
	assert_true(w.sound_get_effects().is_empty(), "reused volley starts disarmed")
	assert_false(w.is_sound_armed(), "mask cleared")
	var second := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	w.sound_set_effects([second])
	assert_true(w.clear_bullet(0), "cleared")
	await physics(2)
	assert_eq(_logged(first.get_instance_id()).size(), 1, "first life stays silent")
	assert_eq(_logged(second.get_instance_id()).size(), 1, "re-armed life sounds")


func test_pooled_handle_refuses_writes() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0, 30.0))
	assert_eq(v.clear_all_bullets(), 1, "drained to the pool")
	await idle(1)
	assert_false(v.sound_set_effects([H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)]), "pooled handle refused")
	expect_error_sequence(["sound_set_effects: this volley is in the pool (its last bullet died), so this handle is stale. Spawn a new volley instead."])


func test_unused_factory_creates_no_voices() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(100, 200.0, 30.0))
	await physics(10)
	assert_true(v.get_bullet_transform(0).is_finite(), "volley ticks")
	var stats: Dictionary = factory.debug_get_sound_stats()
	assert_eq(int(stats["voices_total"]), 0, "no voice nodes created")
	assert_eq(int(stats["channels"]), 0, "no channels opened")
	assert_eq(int(stats["plays_total"]), 0, "nothing played")


func _logged(sound_id: int) -> Array:
	var out: Array = []
	for e in factory.debug_get_sound_log():
		if int((e as Dictionary).get("sound", 0)) == sound_id:
			out.append(e)
	return out
