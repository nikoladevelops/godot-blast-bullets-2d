extends BlastTest
## Cosmetic sound rolls (pitch, volume, pan, picks) draw from the plugin's own
## generator: an audible play must not move Godot's global RNG, so a seeded
## game rolls the same whether sounds play or not.


func after_all() -> void:
	# Same headless-audio drain as the other sound suites.
	OS.delay_msec(500)
	await super()


func test_audible_sound_rolls_leave_global_rng_alone() -> void:
	godot_listener_at(Vector2.ZERO)
	seed(7)
	var expected := randf()
	seed(7)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.random_pitch = 2.0
	s.random_volume_offset_db = 3.0
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	assert_eq(int(factory.debug_get_sound_stats()["plays_total"]), 1, "the play rolled its mix")
	assert_eq(randf(), expected, "global RNG untouched by the sound rolls")
