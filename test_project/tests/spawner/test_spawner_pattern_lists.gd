extends BlastTest
## spawn_pattern_list: every entry is a TEMPORARY override in both modes.
## After each entry everything it changed returns to the spawner's own value
## (source, amount, spawn data and anything a preset entry wrote; a shot
## handler's edits of other knobs stick), pattern_list_finished
## reports once in both modes, the same spawn data does not re-duplicate, and
## bad inputs fail loud with exact wording.


func _spawner() -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(4, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	watch_signals(sp)
	return sp


func _volley_sizes(sp: BulletSpawner2D) -> Array:
	var out: Array = []
	for i in get_signal_emit_count(sp, "volley_fired"):
		var v: BulletVolley2D = get_signal_parameters(sp, "volley_fired", i)[0]
		out.append(v.get_amount_bullets())
	return out


func test_sequential_entries_fire_in_order_and_restore_everything() -> void:
	var sp := _spawner()
	var data: BulletVolleyData2D = sp.get_spawn_data()
	var entries := [
		{"pattern_source": BulletSpawner2D.PATTERN_FROM_HELPER_FAN, "helper_bullets_amount": 3},
		{"helper_bullets_amount": 5},
		{"pattern_source": BulletSpawner2D.PATTERN_FROM_HELPER_CIRCLE, "helper_bullets_amount": 7},
	]
	assert_eq(sp.spawn_pattern_list(entries, false, 0.05), 3, "three entries queued")
	for i in 60:
		await idle(1)
		if get_signal_emit_count(sp, "pattern_list_finished") > 0:
			break
	assert_eq(_volley_sizes(sp), [3, 5, 7], "one volley per entry, in order, with its amount")
	assert_signal_emit_count(sp, "pattern_list_finished", 1, "finished once")
	assert_eq(sp.pattern_source, BulletSpawner2D.PATTERN_FROM_HELPER_RING, "source restored after the list")
	assert_eq(sp.helper_bullets_amount, 4, "amount restored")
	assert_eq(sp.get_spawn_data(), data, "spawn data restored")
	assert_false(sp.is_pattern_list_active(), "list drained")


func test_simultaneous_fires_all_restores_and_reports_finished() -> void:
	var sp := _spawner()
	var fired: int = sp.spawn_pattern_list([{"helper_bullets_amount": 2}, {"pattern_source": BulletSpawner2D.PATTERN_FROM_HELPER_LINE}], true)
	assert_eq(fired, 2, "returns how many volleys fired")
	assert_eq(_volley_sizes(sp), [2, 4], "each entry overrides only its own keys")
	assert_signal_emit_count(sp, "pattern_list_finished", 1, "simultaneous lists report finished too")
	assert_eq(sp.pattern_source, BulletSpawner2D.PATTERN_FROM_HELPER_RING, "source restored")
	assert_eq(sp.helper_bullets_amount, 4, "amount restored")


func test_preset_entries_are_undone_after_the_entry() -> void:
	var sp := _spawner()
	var before: Array = sp.collect_spawn_transforms()
	sp.spawn_pattern_list([{"preset": BulletPatterns2D.PATTERN_PRESET_TWIN_SPIRAL_COUNTER}], true)
	assert_eq(_volley_sizes(sp), [40], "the preset shaped the entry's volley")
	assert_false(sp.spin_enabled, "a spin preset entry does not leave spin on")
	assert_eq(sp.pattern_source, BulletSpawner2D.PATTERN_FROM_HELPER_RING, "source restored")
	assert_eq(sp.helper_counter_spiral_arms, 2, "preset-written knobs restored")
	var after: Array = sp.collect_spawn_transforms()
	assert_eq(after.size(), before.size(), "same pattern as before the list")
	for i in before.size():
		assert_almost_eq((after[i] as Transform2D).origin.distance_to((before[i] as Transform2D).origin), 0.0, 0.001, "slot %d unchanged" % i)


func test_reusing_the_same_spawn_data_keeps_the_template() -> void:
	var sp := _spawner()
	assert_true(sp.shoot_once(), "prime the duplicate cache")
	sp.spawn_pattern_list([{"spawn_data": sp.get_spawn_data()}], true)
	var info: Dictionary = sp.debug_get_pattern_cache_info()
	assert_true(info["template_valid"], "assigning the same resource keeps the template")
	assert_true(info["spawn_id_match"], "still the live resource")


func test_bad_inputs_fail_loud() -> void:
	var sp := _spawner()
	assert_eq(sp.spawn_pattern_list([], false), 0, "empty list")
	expect_error_sequence(["spawn_pattern_list: entries is empty, nothing queued"])
	assert_eq(sp.spawn_pattern_list([{"helper_bullets_amount": 2}], false, NAN), 1, "bad interval still queues")
	expect_error_sequence(["spawn_pattern_list: interval_sec must be finite and >= 0; using 0 (one entry per frame)"])
	sp.stop_pattern_list()
	sp.spawn_pattern_list([42, {"pattern_source": 999}, {"helper_bullets_amount": "x"}], true)
	expect_error_sequence([
		"every entry must be a Dictionary",
		"entry 'pattern_source' out of range",
		"entry 'helper_bullets_amount' must be an int",
	])


## A list started from a handler during a simultaneous list runs on its own:
## the outer list neither iterates the new entries nor drops them.
func test_a_list_started_inside_a_simultaneous_list_runs() -> void:
	var sp := _spawner()
	sp.volley_fired.connect(func(_v, _n): sp.spawn_pattern_list([{"helper_bullets_amount": 6}, {"helper_bullets_amount": 7}], false, 0.0), CONNECT_ONE_SHOT)
	assert_eq(sp.spawn_pattern_list([{"helper_bullets_amount": 2}, {"helper_bullets_amount": 3}], true), 2, "the outer list fired both entries")
	assert_signal_emit_count(sp, "pattern_list_finished", 1, "the outer list finished")
	assert_true(sp.is_pattern_list_active(), "the nested sequential list is queued")
	for i in 30:
		await idle(1)
		if not sp.is_pattern_list_active():
			break
	assert_eq(_volley_sizes(sp), [2, 3, 6, 7], "outer entries, then the nested list")
	assert_signal_emit_count(sp, "pattern_list_finished", 2, "each list finished once")


## A handler that stops or replaces the running sequential list owns it: the
## old list fires nothing more and never reports finished.
func test_a_handler_stopping_or_replacing_a_sequential_list() -> void:
	var sp := _spawner()
	sp.volley_fired.connect(func(_v, _n): sp.stop_pattern_list(), CONNECT_ONE_SHOT)
	sp.spawn_pattern_list([{"helper_bullets_amount": 2}, {"helper_bullets_amount": 3}], false, 0.0)
	await idle(6)
	assert_eq(_volley_sizes(sp), [2], "stopped after the first entry")
	assert_signal_emit_count(sp, "pattern_list_finished", 0, "a stopped list never finishes")
	var sp2 := _spawner()
	sp2.volley_fired.connect(func(_v, _n): sp2.spawn_pattern_list([{"helper_bullets_amount": 9}], false, 0.0), CONNECT_ONE_SHOT)
	sp2.spawn_pattern_list([{"helper_bullets_amount": 2}, {"helper_bullets_amount": 3}], false, 0.0)
	await idle(6)
	assert_eq(_volley_sizes(sp2), [2, 9], "the replacement fired; the old list's rest was dropped")
	assert_signal_emit_count(sp2, "pattern_list_finished", 1, "only the replacement finished")
	assert_false(sp2.is_pattern_list_active(), "nothing left")


## An entry restores only what it changed itself: a shot handler's own edit
## of another knob, made while the entry fires, sticks (a knob the entry set
## goes back to the spawner's value, the entry being the temporary override).
func test_an_entry_restores_only_what_it_changed() -> void:
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.helper_ring_radius = 20.0
	sp.volley_fired.connect(func(_v, _n) -> void: sp.helper_ring_radius += 10.0)
	assert_eq(sp.spawn_pattern_list([{"helper_bullets_amount": 6}, {"helper_bullets_amount": 8}], true, 0.0), 2, "both entries fired")
	assert_eq(sp.helper_ring_radius, 40.0, "the handler's two edits stuck")
	assert_eq(sp.helper_bullets_amount, 4, "the entries' own override went back")
	assert_true(sp.shoot_once(), "a normal shot")
	assert_eq(sp.helper_ring_radius, 50.0, "and the handler keeps editing")


func test_a_handler_edit_of_a_knob_the_entry_set_goes_back_with_it() -> void:
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.volley_fired.connect(func(_v, _n) -> void: sp.helper_bullets_amount = 12)
	sp.spawn_pattern_list([{"helper_bullets_amount": 6}], true, 0.0)
	assert_eq(sp.helper_bullets_amount, 4, "the entry's knob is restored to the spawner's own value")
