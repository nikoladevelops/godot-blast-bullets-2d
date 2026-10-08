extends BlastTest
## Editor sound preview: preview() is editor-only, preview_at() plays the
## picked stream with the mix applied through a transient player, a second call
## stops the first, spawner preview_sound_effect() routes by index.


func after_all() -> void:
	# Same headless-audio drain as the other sound suites.
	OS.delay_msec(500)
	await super()


func _preview_node(s: BulletSoundData2D) -> AudioStreamPlayer2D:
	return s.debug_get_preview_player() as AudioStreamPlayer2D


func _stop_preview(s: BulletSoundData2D) -> void:
	var p := _preview_node(s)
	if p != null and is_instance_valid(p):
		p.stop()
		p.queue_free()
	await idle(1)


func test_preview_bound_and_editor_only() -> void:
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	assert_has_method(s, "preview", "bound preview")
	assert_has_method(s, "preview_at", "bound preview_at")
	var sp := make_spawner()
	assert_has_method(sp, "preview_sound_effect", "bound preview_sound_effect")
	s.preview()
	expect_error_sequence(["BulletSoundData2D.preview: editor only, nothing plays."])
	assert_null(_preview_node(s), "nothing plays outside the editor")


func test_preview_sound_effect_index_refused() -> void:
	var sp := make_spawner()
	sp.sound_effects = []
	sp.preview_sound_effect(0)
	expect_error_sequence(["BulletSpawner2D.preview_sound_effect: index 0 out of range (sound_effects holds 0), nothing plays."])
	sp.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)]
	sp.preview_sound_effect(5)
	expect_error_sequence(["BulletSpawner2D.preview_sound_effect: index 5 out of range (sound_effects holds 1), nothing plays."])
	sp.preview_sound_effect(-1)
	expect_error_sequence(["BulletSpawner2D.preview_sound_effect: index -1 out of range (sound_effects holds 1), nothing plays."])


func test_preview_sound_effect_null_warns_and_silent() -> void:
	var sp := make_spawner()
	sp.sound_effects = [null]
	sp.preview_sound_effect(0)
	expect_warning_sequence(["BulletSpawner2D.preview_sound_effect: entry 0 is null, nothing plays."])


func test_preview_streamless_warns_and_silent() -> void:
	var s := BulletSoundData2D.new()
	var sp := make_spawner()
	sp.sound_effects = [s]
	sp.preview_sound_effect(0)
	expect_warning_sequence(["BulletSoundData2D: streams is empty, the sound plays nothing."])
	assert_null(_preview_node(s), "streamless plays nothing")


func test_preview_disabled_warns_and_silent() -> void:
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.enabled = false
	var sp := make_spawner()
	sp.sound_effects = [s]
	sp.preview_sound_effect(0)
	expect_warning_sequence(["BulletSoundData2D: the sound is disabled, nothing plays."])
	assert_null(_preview_node(s), "disabled plays nothing")


func test_preview_at_plays_with_mix() -> void:
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.volume_db = -6.0
	s.pitch_scale = 1.5
	s.preview_at(Vector2(100, 0), 2.0, true)
	await physics(1)
	var p := _preview_node(s)
	assert_not_null(p, "transient player created")
	assert_true(p.is_playing(), "playing")
	assert_almost_eq(p.volume_db, -4.0, 0.01, "volume + trim")
	assert_almost_eq(p.pitch_scale, 1.5, 0.01, "pitch")
	assert_eq(String(p.bus), "Master", "bus")
	assert_almost_eq(p.global_position, Vector2(100, 0), Vector2(0.5, 0.5), "at the pose")
	await _stop_preview(s)


func test_preview_second_call_stops_first() -> void:
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.preview_at(Vector2.ZERO, 0.0, true)
	await physics(1)
	var first := _preview_node(s)
	assert_not_null(first, "first playing")
	s.preview_at(Vector2.ZERO, 0.0, true)
	assert_true(not is_instance_valid(first), "first freed synchronously")
	await physics(1)
	var second := _preview_node(s)
	assert_not_null(second, "second playing")
	assert_true(second.is_playing(), "second plays")
	await _stop_preview(s)


func test_preview_at_centered_ignores_pose() -> void:
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.preview_at(Vector2(500, 0), 0.0, false)
	await physics(1)
	var p := _preview_node(s)
	assert_not_null(p, "plays centered")
	assert_almost_eq(p.max_distance, 1e30, 1e28, "centered max distance")
	assert_almost_eq(p.attenuation, 0.0, 0.001, "centered attenuation")
	await _stop_preview(s)
