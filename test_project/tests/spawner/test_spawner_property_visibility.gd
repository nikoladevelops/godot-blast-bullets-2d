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
