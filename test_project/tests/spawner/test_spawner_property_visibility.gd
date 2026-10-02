extends BlastTest
## _validate_property decides helper_* visibility per pattern_source through a
## string-prefix chain with no compiler tie to the property list, so a
## renamed property silently vanishes from the inspector. Safety net: every
## helper_* property is visible in at least one mode, mode-specific knobs
## show/hide correctly, AIMED and CORRIDOR share the aimed target, homing
## knobs gate on homing_enabled.

var sp: BulletSpawner2D


func before_each() -> void:
	await super()
	sp = make_spawner(H.make_directional_data(1, 50.0, 30.0))
	await idle(1)


func test_no_helper_property_is_invisible_in_every_mode() -> void:
	var all_props: Array = []
	for p in sp.get_property_list():
		if (int(p.get("usage", 0)) & PROPERTY_USAGE_EDITOR) != 0:
			all_props.append(str(p.get("name", "")))
	assert_gt(all_props.size(), 50, "the class exposes a real property surface")
	var ever_visible := {}
	for src in range(BulletSpawner2D.PATTERN_FROM_LAST):
		sp.set_pattern_source(src)
		for p in sp.get_property_list():
			if (int(p.get("usage", 0)) & PROPERTY_USAGE_EDITOR) != 0:
				ever_visible[str(p.get("name", ""))] = true
	var never_helper: Array = []
	for prop_name in all_props:
		if not ever_visible.has(prop_name) and str(prop_name).begins_with("helper_"):
			never_helper.append(prop_name)
	assert_eq(never_helper, [], "no helper_* property is invisible in all modes")


func test_mode_specific_knobs() -> void:
	sp.set_pattern_source(BulletSpawner2D.PATTERN_FROM_HELPER_RING)
	assert_true(is_editor_visible(sp, &"helper_ring_radius"), "ring radius visible in RING")
	assert_true(is_editor_visible(sp, &"helper_bullets_amount"), "amount visible in a helper mode")
	sp.set_pattern_source(BulletSpawner2D.PATTERN_FROM_HELPER_STAR)
	assert_false(is_editor_visible(sp, &"helper_ring_radius"), "ring radius hidden in STAR")
	assert_true(is_editor_visible(sp, &"helper_star_points"), "star points visible in STAR")
	assert_false(is_editor_visible(sp, &"helper_aimed_target"), "unrelated helper genuinely hidden")


func test_aimed_target_shared_by_aimed_and_corridor() -> void:
	sp.set_pattern_source(BulletSpawner2D.PATTERN_FROM_HELPER_AIMED)
	assert_true(is_editor_visible(sp, &"helper_aimed_target"), "visible in AIMED")
	assert_false(is_editor_visible(sp, &"helper_corridor_width"), "corridor-only knob hidden in AIMED")
	sp.set_pattern_source(BulletSpawner2D.PATTERN_FROM_HELPER_CORRIDOR)
	assert_true(is_editor_visible(sp, &"helper_aimed_target"), "visible in CORRIDOR (documented sharing)")
	assert_true(is_editor_visible(sp, &"helper_corridor_width"), "corridor width visible in CORRIDOR")


func test_homing_knobs_gate_on_homing_enabled() -> void:
	sp.set_pattern_source(BulletSpawner2D.PATTERN_FROM_HELPER_RING)
	sp.set_homing_enabled(false)
	assert_false(is_editor_visible(sp, &"homing_max_targets"), "hidden while homing is off")
	sp.set_homing_enabled(true)
	assert_true(is_editor_visible(sp, &"homing_max_targets"), "shown once homing is on")
	assert_true(is_editor_visible(sp, &"homing_enabled"), "homing_enabled always visible")


# --- Inspector layout (groups, prefixed subgroups) --------------------------

## Walks get_property_list() in order: {prop: [group, subgroup, subgroup_prefix]}
## plus the ordered subgroup titles. Subgroup state resets at each group.
func _layout(obj: Object) -> Dictionary:
	var where := {}
	var subgroups: Array = []
	var group := ""
	var sub := ""
	var sub_prefix := ""
	for p in obj.get_property_list():
		var pname := str(p.get("name", ""))
		var usage := int(p.get("usage", 0))
		if (usage & PROPERTY_USAGE_GROUP) != 0:
			group = pname
			sub = ""
			sub_prefix = ""
		elif (usage & PROPERTY_USAGE_SUBGROUP) != 0:
			sub = pname
			sub_prefix = str(p.get("hint_string", ""))
			subgroups.append(pname)
		elif (usage & PROPERTY_USAGE_EDITOR) != 0 or (usage & PROPERTY_USAGE_STORAGE) != 0:
			where[pname] = [group, sub, sub_prefix]
	return {"where": where, "subgroups": subgroups}


func test_every_prefixed_subgroup_member_carries_the_prefix() -> void:
	# The inspector EJECTS a property from a prefixed subgroup when its name
	# does not start with the prefix: a misplaced ADD_PROPERTY would render
	# under the wrong header. Every member must carry its subgroup prefix.
	var where: Dictionary = _layout(sp)["where"]
	var strays: Array = []
	for prop_name in where:
		var prefix: String = where[prop_name][2]
		if prefix != "" and not str(prop_name).begins_with(prefix):
			strays.append("%s under '%s'" % [prop_name, prefix])
	assert_eq(strays, [], "no stray property inside a prefixed subgroup")


func test_every_helper_shape_property_sits_in_a_shape_subgroup() -> void:
	var where: Dictionary = _layout(sp)["where"]
	var loose: Array = []
	for prop_name in where:
		var pn := str(prop_name)
		if pn.begins_with("helper_") and pn != "helper_bullets_amount" and pn != "helper_skip_indices":
			if where[prop_name][0] != "Bullet Patterns" or where[prop_name][2] == "":
				loose.append(pn)
	assert_eq(loose, [], "every helper_<shape>_ knob lives in its shape subgroup")


func test_setup_transform_and_outline_placement() -> void:
	var lay := _layout(sp)
	var where: Dictionary = lay["where"]
	for prop_name in ["bullet_factory_path", "transforms_generator", "spawn_data"]:
		assert_eq(where[prop_name][0], "Setup", prop_name + " in Setup")
	for prop_name in ["pattern_source", "helper_bullets_amount"]:
		assert_eq(where[prop_name].slice(0, 2), ["Bullet Patterns", ""], prop_name + " heads Bullet Patterns")
	for prop_name in ["pattern_scale", "transforms_scale", "spawn_position_offset", "spawn_position_offset_space", "helper_skip_indices"]:
		assert_eq(where[prop_name].slice(0, 2), ["Bullet Patterns", "Transform"], prop_name + " in the Transform subgroup")
	assert_eq(where["helper_ring_radius"].slice(1, 3), ["Ring", "helper_ring_"], "ring knobs strip their prefix")
	# The star_polygon prefix also starts with helper_star_: both must land
	# in their own subgroup.
	assert_eq(where["helper_star_polygon_radius"][1], "Star Polygon", "star polygon not swallowed by Star")
	assert_eq(where["helper_star_points"][1], "Star", "star in Star")
	assert_eq(where["helper_counter_spiral_arms"][1], "Counter Spiral", "counter spiral not swallowed by Spiral")
	var subs: Array = lay["subgroups"]
	assert_eq(subs[0], "Transform", "Transform subgroup comes first")
	assert_eq(subs.filter(func(t): return t == "Outline Layers").size(), 1, "one Outline Layers subgroup")
	var outline_idx := subs.find("Outline Layers")
	var polygon_idx := subs.find("Polygon")
	assert_gt(outline_idx, polygon_idx, "Outline Layers after the shapes")
	assert_eq(where["helper_outline_placement"].slice(0, 2), ["Bullet Patterns", "Outline Layers"], "outline knobs in Outline Layers")


func test_related_knobs_are_adjacent() -> void:
	var names: Array = []
	for p in sp.get_property_list():
		names.append(str(p.get("name", "")))
	assert_eq(names.find("homing_retarget_phase"), names.find("homing_retarget_interval_sec") + 1, "retarget phase right after the interval")
	assert_lt(names.find("spin_mode"), names.find("spin_speed_deg_per_sec"), "spin mode before its per-mode knobs")


func test_spin_knobs_follow_enabled_and_mode() -> void:
	sp.set_spin_enabled(false)
	assert_true(is_editor_visible(sp, &"spin_enabled"), "master switch always visible")
	for prop_name in [&"spin_mode", &"spin_speed_deg_per_sec", &"spin_amplitude_deg", &"spin_frequency_hz"]:
		assert_false(is_editor_visible(sp, prop_name), str(prop_name) + " hidden while spin is off")
	sp.set_spin_enabled(true)
	sp.set_spin_mode(BulletSpawner2D.SPIN_CONTINUOUS)
	assert_true(is_editor_visible(sp, &"spin_mode"), "mode visible while spinning")
	assert_true(is_editor_visible(sp, &"spin_speed_deg_per_sec"), "speed drives CONTINUOUS")
	assert_false(is_editor_visible(sp, &"spin_amplitude_deg"), "amplitude unused in CONTINUOUS")
	assert_false(is_editor_visible(sp, &"spin_frequency_hz"), "frequency unused in CONTINUOUS")
	sp.set_spin_mode(BulletSpawner2D.SPIN_OSCILLATE)
	assert_false(is_editor_visible(sp, &"spin_speed_deg_per_sec"), "speed unused in OSCILLATE")
	assert_true(is_editor_visible(sp, &"spin_amplitude_deg"), "amplitude drives OSCILLATE")
	assert_true(is_editor_visible(sp, &"spin_frequency_hz"), "frequency drives OSCILLATE")


func test_burst_and_telegraph_knobs_follow_their_switch() -> void:
	sp.set_burst_enabled(false)
	sp.set_telegraph_enabled(false)
	for prop_name in [&"burst_count", &"burst_interval_sec", &"burst_alternate_mirror", &"telegraph_sec"]:
		assert_false(is_editor_visible(sp, prop_name), str(prop_name) + " hidden while off")
	assert_true(is_editor_visible(sp, &"burst_enabled"), "burst switch always visible")
	assert_true(is_editor_visible(sp, &"telegraph_enabled"), "telegraph switch always visible")
	sp.set_burst_enabled(true)
	sp.set_telegraph_enabled(true)
	for prop_name in [&"burst_count", &"burst_interval_sec", &"burst_alternate_mirror", &"telegraph_sec"]:
		assert_true(is_editor_visible(sp, prop_name), str(prop_name) + " shown once on")
	sp.set_burst_enabled(false)
	sp.set_telegraph_enabled(false)


func test_gating_switches_refresh_the_inspector() -> void:
	# Each switch that _validate_property reads must emit property_list_changed,
	# or the inspector keeps showing a stale layout until reselected.
	watch_signals(sp)
	sp.set_spin_enabled(true)
	assert_signal_emit_count(sp, "property_list_changed", 1, "spin_enabled")
	sp.set_spin_mode(BulletSpawner2D.SPIN_OSCILLATE)
	assert_signal_emit_count(sp, "property_list_changed", 2, "spin_mode")
	sp.set_burst_enabled(true)
	assert_signal_emit_count(sp, "property_list_changed", 3, "burst_enabled")
	sp.set_telegraph_enabled(true)
	assert_signal_emit_count(sp, "property_list_changed", 4, "telegraph_enabled")
	sp.set_spin_enabled(false)
	sp.set_burst_enabled(false)
	sp.set_telegraph_enabled(false)
