extends BlastTest
## Per-ring graze sounds: entries with ring_index >= 0 only match that ring;
## a zone with fewer rings never matches (silent); exits name the deepest ring.


func after_all() -> void:
	# Same headless-audio drain as the other sound suites.
	OS.delay_msec(500)
	await super()


func _ring_sound(ring: int) -> BulletSoundData2D:
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_GRAZE)
	s.ring_index = ring
	return s


func _logged(sound_id: int) -> Array:
	var out: Array = []
	for e in factory.debug_get_sound_log():
		if int((e as Dictionary).get("sound", 0)) == sound_id:
			out.append(e)
	return out


func test_ring_setters_reject_and_keep() -> void:
	var s := BulletSoundData2D.new()
	assert_has_method(s, "set_ring_index", "bound set_ring_index")
	assert_has_method(s, "get_ring_index", "bound get_ring_index")
	assert_eq(s.ring_index, -1, "any ring by default")
	s.set_ring_index(-2)
	expect_error_sequence(["BulletSoundData2D: ring_index must be -1 (any ring) or 0..3, keeping the old value."])
	assert_eq(s.ring_index, -1, "kept")
	s.set_ring_index(4)
	expect_error_sequence(["BulletSoundData2D: ring_index must be -1 (any ring) or 0..3, keeping the old value."])
	s.set_ring_index(2)
	assert_eq(s.ring_index, 2, "valid ring applies")


func test_each_ring_fires_its_entry() -> void:
	factory.debug_set_sound_log_enabled(true)
	make_graze_target(Vector2(100, 0), &"g")
	var zone := H.make_graze_zone([120.0, 80.0, 40.0, 20.0])
	var v := graze_volley(H.transforms_at([Vector2(-100, 0)]), 200.0)
	assert_true(v.graze_set_zones([zone], &"g"), "zones armed")
	var entries := [_ring_sound(0), _ring_sound(1), _ring_sound(2), _ring_sound(3)]
	v.sound_set_effects(entries)
	for i in 120:
		await physics(1)
		var done := true
		for e in entries:
			if _logged((e as BulletSoundData2D).get_instance_id()).is_empty():
				done = false
		if done:
			break
	for k in 4:
		assert_eq(_logged((entries[k] as BulletSoundData2D).get_instance_id()).size(), 1, "ring %d sounds once" % k)
	var total := 0
	for e in entries:
		total += _logged((e as BulletSoundData2D).get_instance_id()).size()
	assert_eq(total, 4, "four rings, four sounds, no doubles")
	await physics(60) # past every ring crossing: each ring sounded once
	total = 0
	for e in entries:
		total += _logged((e as BulletSoundData2D).get_instance_id()).size()
	assert_eq(total, 4, "no late doubles")


func test_missing_ring_stays_silent() -> void:
	factory.debug_set_sound_log_enabled(true)
	make_graze_target(Vector2(100, 0), &"g")
	var zone := H.make_graze_zone([24.0])
	var v := graze_volley(H.transforms_at([Vector2(-100, 0)]), 200.0)
	assert_true(v.graze_set_zones([zone], &"g"), "zones armed")
	var narrow := _ring_sound(2)
	var wide := _ring_sound(-1)
	v.sound_set_effects([narrow, wide])
	for i in 120:
		await physics(1)
		if not _logged(wide.get_instance_id()).is_empty():
			break
	assert_eq(_logged(wide.get_instance_id()).size(), 1, "any-ring entry sounds")
	assert_true(_logged(narrow.get_instance_id()).is_empty(), "missing ring stays silent")


func test_exit_names_the_deepest_ring() -> void:
	factory.debug_set_sound_log_enabled(true)
	make_graze_target(Vector2(100, 0), &"g")
	var results := {}
	for ring in [-1, 0, 1]:
		var zone := H.make_graze_zone([60.0, 20.0])
		var v := graze_volley(H.transforms_at([Vector2(-100, 0)]), 200.0)
		assert_true(v.graze_set_zones([zone], &"g"), "zones armed")
		var x := H.make_sound(BulletSoundData2D.SOUND_ON_GRAZE_EXIT)
		x.ring_index = ring
		v.sound_set_effects([x])
		for i in 120:
			await physics(1)
			if not _logged(x.get_instance_id()).is_empty():
				break
		results[ring] = _logged(x.get_instance_id()).size()
	assert_eq(results[-1], 1, "any-ring exit sounds")
	assert_eq(results[0] + results[1], 1, "exactly one ring owns the exit")
