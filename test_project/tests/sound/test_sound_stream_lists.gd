extends BlastTest
## Built-in stream lists: each play picks per stream_mode (nulls skipped,
## stream the fallback), pick cursors shared per resource like the limits.


func after_all() -> void:
	# Same headless-audio drain as the other sound suites.
	OS.delay_msec(500)
	await super()


func _list_sound(mode: int, streams: Array, weights := PackedFloat32Array()) -> BulletSoundData2D:
	var s := BulletSoundData2D.new()
	s.streams = streams
	s.stream_mode = mode
	s.stream_weights = weights
	s.min_interval_sec = 0.0
	s.max_voices = 1
	return s


func _played_stream_id() -> int:
	assert_eq(busy_voices().size(), 1, "one voice busy")
	return int((busy_voices()[0] as Dictionary).get("stream", 0))


func _play_each_sweep(s: BulletSoundData2D, n: int) -> Array:
	var ids: Array = []
	for i in n:
		assert_true(factory.play_sound(s, Vector2.ZERO), "offer %d accepted" % i)
		await physics(2)
		ids.append(_played_stream_id())
	return ids


func test_setters_reject_and_keep() -> void:
	var s := BulletSoundData2D.new()
	for m in ["set_streams", "get_streams", "set_stream_mode", "get_stream_mode", "set_stream_weights", "get_stream_weights"]:
		assert_has_method(s, m, "bound " + m)
	s.set_streams(["nope"] as Array)
	expect_error_sequence(["BulletSoundData2D: streams entries must be AudioStream or null, keeping the old value."])
	assert_true((s.streams as Array).is_empty(), "streams kept")
	s.set_stream_mode(4)
	expect_error_sequence(["BulletSoundData2D: stream_mode must be 0 (Random), 1 (Weighted), 2 (Sequence) or 3 (Shuffle), keeping the old value."])
	assert_eq(s.stream_mode, BulletSoundData2D.STREAM_RANDOM, "mode kept")
	s.set_stream_weights(PackedFloat32Array([-1.0]))
	expect_error_sequence(["BulletSoundData2D: stream_weights must hold finite numbers >= 0 only, keeping the old value."])
	assert_true(s.stream_weights.is_empty(), "weights kept")
	s.set_stream_weights(PackedFloat32Array([NAN]))
	expect_error_sequence(["BulletSoundData2D: stream_weights must hold finite numbers >= 0 only, keeping the old value."])


func test_empty_list_plays_nothing() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := BulletSoundData2D.new()
	s.min_interval_sec = 0.0
	assert_false(factory.play_sound(s, Vector2.ZERO), "nothing to play")
	expect_warning_sequence(["BulletSoundData2D: streams is empty, the sound plays nothing."])
	await physics(2)
	assert_eq(busy_voices().size(), 0, "no voice taken")


func test_single_entry_list_plays() -> void:
	godot_listener_at(Vector2.ZERO)
	var one := H.make_sound_stream()
	var s := BulletSoundData2D.new()
	s.streams = [one]
	s.min_interval_sec = 0.0
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	assert_eq(_played_stream_id(), one.get_instance_id(), "one-entry list plays")


func test_all_null_list_warns_and_plays_nothing() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := BulletSoundData2D.new()
	s.streams = [null, null]
	s.min_interval_sec = 0.0
	assert_false(factory.play_sound(s, Vector2.ZERO), "nothing to play")
	expect_warning_sequence(["BulletSoundData2D: streams is empty, the sound plays nothing."])
	await physics(2)
	assert_eq(busy_voices().size(), 0, "no voice taken")


func test_sequence_plays_exact_order() -> void:
	godot_listener_at(Vector2.ZERO)
	var trio := [H.make_sound_stream(0.2), H.make_sound_stream(0.21), H.make_sound_stream(0.22)]
	var s := _list_sound(BulletSoundData2D.STREAM_SEQUENCE, trio)
	var ids := await _play_each_sweep(s, 6)
	var want: Array = []
	for k in 6:
		want.append((trio[k % 3] as AudioStreamWAV).get_instance_id())
	assert_eq(ids, want, "round-robin order shared across sweeps")


func test_shuffle_deals_every_entry_per_cycle() -> void:
	godot_listener_at(Vector2.ZERO)
	var quad := [H.make_sound_stream(0.2), H.make_sound_stream(0.21), H.make_sound_stream(0.22), H.make_sound_stream(0.23)]
	var s := _list_sound(BulletSoundData2D.STREAM_SHUFFLE, quad)
	for cycle in 2:
		var ids := await _play_each_sweep(s, 4)
		ids.sort()
		var want: Array = []
		for q in quad:
			want.append((q as AudioStreamWAV).get_instance_id())
		want.sort()
		assert_eq(ids, want, "cycle %d deals every entry once" % cycle)


func test_weighted_favors_heavier_entries() -> void:
	godot_listener_at(Vector2.ZERO)
	factory.debug_seed_cosmetic_rng(1234)
	var pair := [H.make_sound_stream(0.2), H.make_sound_stream(0.21)]
	var s := _list_sound(BulletSoundData2D.STREAM_WEIGHTED, pair, PackedFloat32Array([3.0, 1.0]))
	var ids := await _play_each_sweep(s, 200)
	var heavy := 0
	for id in ids:
		if id == (pair[0] as AudioStreamWAV).get_instance_id():
			heavy += 1
	assert_gte(heavy, 110, "heavy entry wins well above half")
	assert_lte(heavy, 170, "light entry still plays")


func test_weighted_fallbacks_play_uniform() -> void:
	godot_listener_at(Vector2.ZERO)
	factory.debug_seed_cosmetic_rng(1234)
	# All-zero, size-mismatched and empty weights all mean uniform (documented).
	var pair := [H.make_sound_stream(0.2), H.make_sound_stream(0.21)]
	for weights in [PackedFloat32Array([0.0, 0.0]), PackedFloat32Array([5.0]), PackedFloat32Array()]:
		var s := _list_sound(BulletSoundData2D.STREAM_WEIGHTED, pair, weights)
		var ids := await _play_each_sweep(s, 60)
		var first := 0
		for id in ids:
			if id == (pair[0] as AudioStreamWAV).get_instance_id():
				first += 1
		assert_gte(first, 15, "weights %s still play the first entry" % str(weights))
		assert_lte(first, 45, "weights %s still play the second entry" % str(weights))
		factory.stop_sounds()
		await idle(1)


func test_sequence_and_shuffle_skip_nulls() -> void:
	godot_listener_at(Vector2.ZERO)
	var a := H.make_sound_stream(0.2)
	var b := H.make_sound_stream(0.21)
	var s := _list_sound(BulletSoundData2D.STREAM_SEQUENCE, [a, null, b])
	var ids := await _play_each_sweep(s, 4)
	var want: Array = [a.get_instance_id(), b.get_instance_id(), a.get_instance_id(), b.get_instance_id()]
	assert_eq(ids, want, "sequence round-robins the playable entries")
	factory.stop_sounds()
	await idle(1)
	var sh := _list_sound(BulletSoundData2D.STREAM_SHUFFLE, [null, a, null, b, null])
	var dealt := await _play_each_sweep(sh, 2)
	dealt.sort()
	want = [a.get_instance_id(), b.get_instance_id()]
	want.sort()
	assert_eq(dealt, want, "shuffle deals each playable entry once per cycle")


func test_swapping_list_mid_fight_plays_new_content() -> void:
	godot_listener_at(Vector2.ZERO)
	var old := [H.make_sound_stream(0.2), H.make_sound_stream(0.21)]
	var fresh := [H.make_sound_stream(0.22), H.make_sound_stream(0.23)]
	var s := _list_sound(BulletSoundData2D.STREAM_RANDOM, old)
	await _play_each_sweep(s, 2)
	s.streams = fresh
	var ids := await _play_each_sweep(s, 8)
	var fresh_ids: Array = [(fresh[0] as AudioStreamWAV).get_instance_id(), (fresh[1] as AudioStreamWAV).get_instance_id()]
	for id in ids:
		assert_has(fresh_ids, id, "swapped list plays only new content")


func test_in_place_append_reaches_next_play() -> void:
	godot_listener_at(Vector2.ZERO)
	var a := H.make_sound_stream(0.2)
	var b := H.make_sound_stream(0.21)
	var s := _list_sound(BulletSoundData2D.STREAM_SEQUENCE, [a])
	var ids := await _play_each_sweep(s, 2)
	assert_eq(ids, [a.get_instance_id(), a.get_instance_id()], "one-entry list repeats")
	var live: Array = s.streams
	live.append(b)
	var more := await _play_each_sweep(s, 2)
	assert_has(more, b.get_instance_id(), "in-place append plays without reassign")


func test_swapping_weights_mid_fight_rebalances() -> void:
	godot_listener_at(Vector2.ZERO)
	factory.debug_seed_cosmetic_rng(1234)
	var pair := [H.make_sound_stream(0.2), H.make_sound_stream(0.21)]
	var s := _list_sound(BulletSoundData2D.STREAM_WEIGHTED, pair, PackedFloat32Array([3.0, 1.0]))
	var first := await _play_each_sweep(s, 120)
	var heavy := 0
	for id in first:
		if id == (pair[0] as AudioStreamWAV).get_instance_id():
			heavy += 1
	assert_gte(heavy, 70, "heavy entry wins first")
	assert_lte(heavy, 110, "light entry still plays")
	s.stream_weights = PackedFloat32Array([1.0, 3.0])
	var second := await _play_each_sweep(s, 120)
	heavy = 0
	for id in second:
		if id == (pair[0] as AudioStreamWAV).get_instance_id():
			heavy += 1
	assert_gte(heavy, 10, "rebalanced: first entry still plays")
	assert_lte(heavy, 50, "rebalanced: second entry wins now")
