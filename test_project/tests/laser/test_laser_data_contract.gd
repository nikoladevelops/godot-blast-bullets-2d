extends BlastTest
## LaserData2D as a resource: defaults, serialized enum ids, reject-and-keep
## for its enums (the setter sweep in data/test_data_setter_contract.gd covers
## every number), inspector groups and per-type / per-aim gating, and the
## presets (create_preset, apply_preset writes the behavior only, the looks
## stay; get_matching_preset names a preset again; the editor-only `preset`
## property is never saved).


func test_defaults_and_serialized_ids() -> void:
	var d := LaserData2D.new()
	assert_eq([d.type, d.length, d.width, d.extend_speed, d.charge_sec, d.grow_sec, d.active_sec, d.fade_sec], [LaserData2D.LASER_BEAM, 600.0, 12.0, 0.0, 0.5, 0.1, 1.0, 0.2], "shape and phases")
	assert_eq([d.aim, d.turn_rate_degrees, d.keep_tracking_when_active, d.follow_spawner, d.sweep_speed_degrees, d.sweep_arc_degrees], [LaserData2D.LASER_AIM_PATTERN, 90.0, true, true, 0.0, 0.0], "aim and motion")
	assert_eq([d.wall_mask, d.reflections, d.collision_mask, d.collide_with_bodies, d.collide_with_areas, d.pierce_limit, d.hit_interval_sec, d.max_hits_per_beam], [0, 0, 1, true, true, 0, 0.0, 32], "walls and hits")
	assert_eq([LaserData2D.LASER_BEAM, LaserData2D.LASER_BOLT, LaserData2D.LASER_CURVY], [0, 1, 2], "type ids")
	assert_eq([LaserData2D.LASER_AIM_PATTERN, LaserData2D.LASER_AIM_TRACK, LaserData2D.LASER_AIM_LOCK_ON], [0, 1, 2], "aim ids")
	assert_eq([LaserData2D.BODY_STRETCH, LaserData2D.BODY_TILE], [0, 1], "body mode ids")


func test_enums_reject_and_keep() -> void:
	var d := LaserData2D.new()
	d.set("type", 7)
	expect_error_sequence(["LaserData2D: type must be 0 (Beam), 1 (Bolt) or 2 (Curvy), keeping the old value."])
	d.set("aim", -1)
	expect_error_sequence(["LaserData2D: aim must be 0 (Pattern), 1 (Track) or 2 (Lock On), keeping the old value."])
	d.set("body_mode", 5)
	expect_error_sequence(["LaserData2D: body_mode must be 0 (Stretch) or 1 (Tile), keeping the old value."])
	d.wall_mask = -1
	expect_error_sequence(["LaserData2D: wall_mask must be a 32-bit layer mask (0 = no walls), keeping the old value."])
	d.reflections = 999
	expect_error_sequence(["LaserData2D: reflections must be between 0 and 256 (each one is a ray per tick), keeping the old value."])
	d.max_hits_per_beam = 0
	expect_error_sequence(["LaserData2D: max_hits_per_beam must be between 1 and 4096, keeping the old value."])
	d.pierce_limit = -2
	expect_error_sequence(["LaserData2D: pierce_limit must be >= 0 (0 = passes through every target), keeping the old value."])
	d.modulate = Color(NAN, 0, 0, 1)
	expect_error_sequence(["LaserData2D: modulate must be finite, keeping the old value."])
	assert_eq([d.type, d.aim, d.body_mode, d.wall_mask, d.reflections, d.max_hits_per_beam, d.pierce_limit], [0, 0, 0, 0, 0, 32, 0], "every refusal kept the old value")


func test_inspector_groups_and_gating() -> void:
	var d := LaserData2D.new()
	var group := ""
	var groups := {}
	for p in d.get_property_list():
		if int(p["usage"]) & PROPERTY_USAGE_GROUP:
			group = str(p["name"])
		elif int(p["usage"]) & PROPERTY_USAGE_STORAGE:
			groups[str(p["name"])] = group
	for n in ["type", "length", "width"]:
		assert_eq(groups.get(n), "Laser", n)
	assert_eq([groups.get("charge_sec"), groups.get("aim"), groups.get("wall_mask"), groups.get("pierce_limit"), groups.get("body_frames")], ["Phases", "Aim and Motion", "Walls", "Hits", "Looks"], "groups")
	var shown := func(names: Array) -> Array:
		var out: Array = []
		for n in names:
			out.append(is_editor_visible(d, StringName(n)))
		return out
	assert_eq(shown.call(["extend_speed", "sweep_speed_degrees", "speed", "wave_amplitude"]), [true, true, false, false], "beam: extension and sweep")
	d.type = LaserData2D.LASER_BOLT
	assert_eq(shown.call(["extend_speed", "sweep_speed_degrees", "speed", "wave_amplitude"]), [false, false, true, false], "bolt: speed")
	d.type = LaserData2D.LASER_CURVY
	assert_eq(shown.call(["speed", "wave_amplitude", "wave_frequency"]), [true, true, true], "curvy: speed and wave")
	assert_eq(shown.call(["turn_rate_degrees", "keep_tracking_when_active"]), [false, false], "pattern aim: no tracking knobs")
	d.aim = LaserData2D.LASER_AIM_TRACK
	assert_eq(shown.call(["turn_rate_degrees", "keep_tracking_when_active"]), [true, true], "track: both")
	d.aim = LaserData2D.LASER_AIM_LOCK_ON
	assert_eq(shown.call(["turn_rate_degrees", "keep_tracking_when_active"]), [false, true], "lock-on snaps")
	assert_false(is_editor_visible(d, &"reflections"), "no walls: no reflections")
	d.wall_mask = 1
	assert_true(is_editor_visible(d, &"reflections"), "walls: reflections")
	assert_false(is_editor_visible(d, &"body_scroll_speed"), "stretch: no scroll")
	d.body_mode = LaserData2D.BODY_TILE
	assert_true(is_editor_visible(d, &"body_scroll_speed"), "tile: scroll")


func test_presets_write_behavior_and_keep_the_looks() -> void:
	var names := {LaserData2D.PRESET_INSTANT_BEAM: LaserData2D.LASER_BEAM, LaserData2D.PRESET_CHARGED_SWEEP: LaserData2D.LASER_BEAM, LaserData2D.PRESET_WALL_BEAM: LaserData2D.LASER_BEAM, LaserData2D.PRESET_REFLECTING_BEAM: LaserData2D.LASER_BEAM, LaserData2D.PRESET_BOLT: LaserData2D.LASER_BOLT, LaserData2D.PRESET_HOMING_CURVY: LaserData2D.LASER_CURVY, LaserData2D.PRESET_LOCK_ON: LaserData2D.LASER_BEAM}
	for p in names:
		var d := LaserData2D.create_preset(p)
		assert_eq(d.type, names[p], "preset %d type" % p)
		assert_eq(d.get_matching_preset(), p, "preset %d is recognized" % p)
	var looks := LaserData2D.new()
	looks.modulate = Color.RED
	looks.width = 3.0
	assert_eq(looks.get_matching_preset(), LaserData2D.PRESET_CUSTOM, "custom")
	looks.apply_preset(LaserData2D.PRESET_CHARGED_SWEEP)
	assert_eq(looks.modulate, Color.RED, "the looks stay")
	assert_eq([looks.charge_sec, looks.sweep_speed_degrees, looks.sweep_arc_degrees], [1.0, 45.0, 90.0], "the behavior is written")
	looks.apply_preset(LaserData2D.PRESET_CUSTOM)
	assert_eq(looks.get_matching_preset(), LaserData2D.PRESET_CHARGED_SWEEP, "Custom writes nothing")
	looks.apply_preset(99)
	expect_error_sequence(["LaserData2D: preset must be one of the Preset values, nothing changed."])
	var preset_usage := 0
	for p in looks.get_property_list():
		if str(p["name"]) == "preset":
			preset_usage = int(p["usage"])
	assert_eq(preset_usage & PROPERTY_USAGE_STORAGE, 0, "the preset is never saved")
