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


func test_shoot_once_with_is_one_shot_with_temporary_overrides() -> void:
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	watch_signals(sp)
	sp.helper_ring_radius = 20.0
	sp.volley_fired.connect(func(_v, _n) -> void: sp.helper_ring_radius += 10.0)
	assert_true(sp.shoot_once_with({"helper_bullets_amount": 6, "pattern_source": BulletSpawner2D.PATTERN_FROM_HELPER_FAN}), "fires")
	var v: BulletVolley2D = get_signal_parameters(sp, "volley_fired", 0)[0]
	assert_eq(v.get_amount_bullets(), 6, "with the overrides")
	assert_eq(sp.helper_bullets_amount, 4, "the amount went back")
	assert_eq(sp.pattern_source, BulletSpawner2D.PATTERN_FROM_HELPER_RING, "the source went back")
	assert_eq(sp.helper_ring_radius, 30.0, "the handler's own edit stuck")
	assert_eq(sp.get_volleys_fired(), 1, "a normal shot: counted")
	assert_signal_emit_count(sp, "pattern_list_finished", 0, "no list report")


func test_shoot_once_with_names_itself_in_its_errors() -> void:
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	assert_true(sp.shoot_once_with({"helper_bulets_amount": 6}), "a bad key is skipped, the shot fires")
	expect_error_sequence(["BulletSpawner2D::shoot_once_with: unknown entry key 'helper_bulets_amount' (did you mean 'helper_bullets_amount'?); valid keys: preset, pattern_source, helper_bullets_amount, spawn_data. Ignoring it."])
	sp.shoot_once_with({"pattern_source": 9999})
	expect_error_sequence(["BulletSpawner2D::shoot_once_with: entry 'pattern_source' out of range, keeping current."])
	assert_eq(sp.helper_bullets_amount, 4, "untouched")


func test_shoot_once_with_from_a_shot_handler_is_refused_and_restores() -> void:
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	var nested := [true]
	sp.volley_fired.connect(func(_v, _n) -> void: nested[0] = sp.shoot_once_with({"helper_bullets_amount": 9}), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "the outer shot fires")
	assert_false(nested[0], "the nested one is refused")
	expect_errors_containing("re-entrant call from inside a spawner signal handler", 1, "the nesting rule of shoot_once")
	assert_eq(sp.helper_bullets_amount, 4, "and its override went back")


func _origins(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		out.append(v.get_bullet_transform(i).origin.snapped(Vector2(0.01, 0.01)))
	return out


func test_shoot_from_fires_the_pattern_posed_at_the_transform() -> void:
	var sp := make_spawner(H.make_volley_data(4, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.helper_ring_radius = 20.0
	var got: Array = []
	sp.volley_fired.connect(func(v, _n) -> void: got.append(v))
	var pose := Transform2D(PI / 2.0, Vector2(300, 100))
	assert_true(sp.shoot_from(pose), "fires")
	assert_eq(_origins(got[0]), [Vector2(300, 120), Vector2(280, 100), Vector2(300, 80), Vector2(320, 100)], "the ring around the pose, turned with it")
	assert_true(sp.shoot_from(Transform2D(0.0, Vector2(0, 0)), 90.0), "with extra spin")
	assert_eq(_origins(got[1]), [Vector2(0, 20), Vector2(-20, 0), Vector2(0, -20), Vector2(20, 0)], "turned by the extra 90 degrees")
	assert_eq(sp.get_volleys_fired(), 2, "counted like any shot")
	assert_true(sp.shoot_once(), "a normal shot after it")
	assert_eq(_origins(got[2]), [Vector2(20, 0), Vector2(0, 20), Vector2(-20, 0), Vector2(0, -20)], "back at the generator: the pose was for one shot only")


func test_shoot_from_moves_children_and_the_local_offset_with_the_pose() -> void:
	var sp := make_spawner(H.make_volley_data(2, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_CHILDREN, 2)
	for p in [Vector2(10, 0), Vector2(0, 10)]:
		var m := Marker2D.new()
		m.position = p
		sp.add_child(m)
	sp.spawn_position_offset = Vector2(5, 0)
	sp.spawn_position_offset_space = BulletSpawner2D.SPAWN_OFFSET_LOCAL
	var got: Array = []
	sp.volley_fired.connect(func(v, _n) -> void: got.append(v))
	assert_true(sp.shoot_from(Transform2D(PI, Vector2(100, 0))), "fires")
	assert_eq(_origins(got[0]), [Vector2(85, 0), Vector2(95, -10)], "markers and the muzzle offset turned with the pose")


func test_shoot_from_refuses_bad_poses_and_nesting() -> void:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	for bad in [Transform2D(0.0, Vector2(NAN, 0)), Transform2D(Vector2.ZERO, Vector2.ZERO, Vector2.ZERO)]:
		assert_false(sp.shoot_from(bad), "refused")
		expect_error_sequence(["BulletSpawner2D::shoot_from: global_transform must be finite and invertible (and the spin finite), nothing fired."])
	var nested := [true]
	var poses: Array = []
	sp.volley_fired.connect(func(v, _n) -> void:
		poses.append(v.get_bullet_transform(0).origin)
		nested[0] = sp.shoot_from(Transform2D(0.0, Vector2(999, 999))), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_from(Transform2D(0.0, Vector2(50, 50))), "the outer shot fires")
	assert_false(nested[0], "a shoot_from inside the shot is refused")
	expect_errors_containing("re-entrant call from inside a spawner signal handler", 1, "with shoot_once's nesting error")
	assert_eq(poses, [Vector2(50, 50)], "and never moved the outer shot")


func test_shoot_from_inherits_no_spawner_motion() -> void:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	var path := Path2D.new()
	var c := Curve2D.new()
	c.add_point(Vector2(0, 0))
	c.add_point(Vector2(400, 0))
	path.curve = c
	add(path)
	sp.movement_path = sp.get_path_to(path)
	sp.movement_duration_sec = 1.0
	sp.inherit_movement_velocity = true
	sp.movement_enabled = true
	sp.movement_play()
	await idle(10)
	assert_gt(sp.get_movement_velocity().length(), 100.0, "the spawner moves")
	var got: Array = []
	sp.volley_fired.connect(func(v, _n) -> void: got.append(v))
	assert_true(sp.shoot_from(Transform2D(0.0, Vector2(0, 300))), "fires")
	assert_eq(got[0].get_bullet_velocity(0), Vector2.ZERO, "a posed shot keeps none of the spawner's motion")


## An override restores natively (no property walk): an empty one changes
## nothing at all, so the spawner's bake keeps serving shots, and every knob
## a preset entry writes (int, float, bool, enum, Vector2, arrays, the
## hand-written pattern properties, spin_*, the payload) comes back exactly.
func test_an_empty_override_keeps_the_bake_and_every_kind_of_knob_comes_back() -> void:
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8)
	sp.shoot_once()
	var info: Dictionary = sp.debug_get_pattern_cache_info()
	var bakes: int = info["bakes"]
	var hits: int = info["hits"]
	for k in 3:
		assert_true(sp.shoot_once_with({}), "an empty override fires")
	info = sp.debug_get_pattern_cache_info()
	assert_eq(int(info["bakes"]), bakes, "an empty override never rebakes")
	assert_eq(int(info["hits"]) - hits, 3, "each empty override is served from the bake")
	# A preset entry writes knobs of every kind; all of them come back.
	sp.helper_skip_indices = PackedInt32Array([1, 2])
	sp.helper_custom_transforms = [Transform2D(0.0, Vector2(5, 5))]
	sp.spin_speed_deg_per_sec = 45.0
	var before := {}
	for p in sp.get_property_list():
		var pname := String(p["name"])
		if pname.begins_with("helper_") or pname.begins_with("spin_") or pname in ["pattern_source", "spawn_data"]:
			before[pname] = sp.get(pname)
	var other_data := H.make_volley_data(4)
	assert_true(sp.shoot_once_with({"preset": BulletPatterns2D.PATTERN_PRESET_PETAL_STORM, "helper_bullets_amount": 5, "spawn_data": other_data}), "fires with a preset entry")
	var changed: Array = []
	for pname in before:
		if sp.get(pname) != before[pname]:
			changed.append(pname)
	assert_eq(changed, [], "every knob the preset entry wrote came back")


func test_a_same_value_knob_write_neither_rebakes_nor_rebuilds() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8)
	await idle(6)
	sp.collect_spawn_transforms()
	var bakes: int = sp.debug_get_pattern_cache_info()["bakes"]
	var rebuilds: int = sp.debug_get_preview_stats()["rebuilds"]
	sp.helper_ring_radius = sp.helper_ring_radius
	sp.helper_bullets_amount = 8
	sp.helper_ring_rotate_with_marker = sp.helper_ring_rotate_with_marker
	await idle(6)
	sp.collect_spawn_transforms()
	assert_eq(int(sp.debug_get_pattern_cache_info()["bakes"]), bakes, "no rebake")
	assert_eq(int(sp.debug_get_preview_stats()["rebuilds"]), rebuilds, "no preview rebuild")
	sp.helper_ring_radius = sp.helper_ring_radius + 1.0
	await idle(6)
	sp.collect_spawn_transforms()
	assert_gt(int(sp.debug_get_pattern_cache_info()["bakes"]), bakes, "a real change still rebakes")
	assert_gt(int(sp.debug_get_preview_stats()["rebuilds"]), rebuilds, "and rebuilds the preview")
