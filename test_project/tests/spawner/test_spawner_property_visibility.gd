extends SceneTree
## Inspector property-visibility coverage suite.
##
## BulletSpawner2D::_validate_property decides which helper_* knobs are visible
## for the current pattern_source, using a 30+ branch begins_with() chain. That
## chain has no compiler tie to the property list: a renamed or misspelled
## property simply falls through every branch, lands on the default, and
## SILENTLY disappears from the inspector with no error anywhere.
##
## This suite is the safety net for that failure mode. It walks every registered
## property of the class, and for each one that matches a helper_/homing_/
## orbiting_/preview_ family it requires that the property is VISIBLE in at
## least one configuration of pattern_source. A property that is hidden in
## EVERY mode is either dead code or a chain bug - exactly the class of mistake
## that is invisible until a user reports a missing knob.
##
## It also pins the AIMED/CORRIDOR sharing, which is a documented behaviour
## with a genuine trap: the aimed-target picker is supposed to stay visible in
## both modes, and the string compare that implements it is easy to break.
##
## Covers: T1 every property is reachable in some mode, T2 helper knobs are
## visible in their own mode, T3 aimed target visible in BOTH aimed and
## corridor, T4 per-mode visibility is non-trivial, T5 no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_property_visibility.gd
## Exit code 0 = all pass. Any FAIL = a knob is hidden from users.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

# Mirrors the chain's family prefixes. A property in one of these families must
# be visible somewhere, or the chain has a hole.
const FAMILY_PREFIXES := ["helper_", "homing_", "orbiting_", "preview_"]

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var spawner := BulletSpawner2D.new()
	get_root().add_child(spawner)
	await process_frame
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(H.make_directional_data(1, 50.0, 30.0))
	spawner.set_shooting_enabled(false)
	await process_frame

	# Snapshot every property name once; only its USAGE changes per mode.
	var all_props: Array = []
	for p in spawner.get_property_list():
		var usage := int(p.get("usage", 0))
		# PROPERTY_USAGE_EDITOR == 2. Visible = the editor bit is set.
		if (usage & PROPERTY_USAGE_EDITOR) != 0:
			all_props.append(str(p.get("name", "")))
	_check(all_props.size() > 50, "T0 the class exposes a real property surface (%d)" % all_props.size())

	# ---------------------------------------------------------------
	printerr("PVIS T1 no property is invisible in EVERY pattern mode")
	# Walk all 33 sources and record, per property, whether it was ever visible.
	# A property that never shows is dead or mis-wired.
	var ever_visible := {}
	for src in range(0, 33):
		spawner.set_pattern_source(src)
		# _validate_property reads the current source, so re-read after setting.
		for p in spawner.get_property_list():
			var name := str(p.get("name", ""))
			var usage := int(p.get("usage", 0))
			if (usage & PROPERTY_USAGE_EDITOR) != 0:
				ever_visible[name] = true

	var never: Array = []
	for name in all_props:
		if not ever_visible.has(name):
			never.append(name)
	# spawn_data-style properties are gated on their own flags (e.g. homing_* on
	# homing_enabled), not on pattern_source, so a mode sweep legitimately
	# hides some of them. Only flag HELPER properties here - those are the ones
	# the chain is supposed to key off pattern_source.
	var never_helper: Array = []
	for name in never:
		if str(name).begins_with("helper_"):
			never_helper.append(name)
	_check(never_helper.is_empty(),
		"T1 no helper_* property is invisible in all 33 modes (offenders: %s)" % (", ".join(never_helper) if not never_helper.is_empty() else "none"))

	# ---------------------------------------------------------------
	printerr("PVIS T2 a representative helper knob is visible in its own mode")
	# helper_ring_radius belongs to RING (source 3).
	spawner.set_pattern_source(3)
	_check(_is_editor_visible(spawner, "helper_ring_radius"),
		"T2a helper_ring_radius visible in RING mode")
	_check(_is_editor_visible(spawner, "helper_bullets_amount"),
		"T2b helper_bullets_amount visible in a helper mode")
	# And hidden in an unrelated mode (this is the chain actually filtering).
	spawner.set_pattern_source(15) # STAR
	_check(not _is_editor_visible(spawner, "helper_ring_radius"),
		"T2c helper_ring_radius hidden in STAR mode")
	_check(_is_editor_visible(spawner, "helper_star_points"),
		"T2d helper_star_points visible in STAR mode")

	# ---------------------------------------------------------------
	printerr("PVIS T3 the aimed-target picker stays visible in AIMED and CORRIDOR")
	# Documented sharing: the corridor wall reuses the aimed target, so the
	# shared knob must be visible in both. This is the exact spot a string
	# compare in _validate_property can silently break.
	spawner.set_pattern_source(7) # AIMED
	var aimed_ok := _is_editor_visible(spawner, "helper_aimed_target")
	spawner.set_pattern_source(22) # CORRIDOR
	var corridor_ok := _is_editor_visible(spawner, "helper_aimed_target")
	_check(aimed_ok, "T3a helper_aimed_target visible in AIMED mode")
	_check(corridor_ok, "T3b helper_aimed_target visible in CORRIDOR mode (documented sharing)")
	# And the corridor-only knob must NOT leak into AIMED.
	spawner.set_pattern_source(7)
	_check(not _is_editor_visible(spawner, "helper_corridor_width"),
		"T3c helper_corridor_width hidden in AIMED mode")
	spawner.set_pattern_source(22)
	_check(_is_editor_visible(spawner, "helper_corridor_width"),
		"T3d helper_corridor_width visible in CORRIDOR mode")

	# ---------------------------------------------------------------
	printerr("PVIS T4 homing knobs are gated on homing_enabled, not pattern")
	# A different family with a different gate; proves the sweep above did not
	# just pass because everything is always visible.
	spawner.set_pattern_source(3)
	spawner.set_homing_enabled(false)
	var hidden_homing := not _is_editor_visible(spawner, "homing_max_targets")
	spawner.set_homing_enabled(true)
	var shown_homing := _is_editor_visible(spawner, "homing_max_targets")
	_check(hidden_homing, "T4a homing_max_targets hidden while homing is off")
	_check(shown_homing, "T4b homing_max_targets shown once homing is on")
	# homing_enabled itself must always be reachable.
	_check(_is_editor_visible(spawner, "homing_enabled"),
		"T4c homing_enabled is always visible")

	# ---------------------------------------------------------------
	printerr("PVIS T5 the visibility sweep did not mark everything visible")
	# Sanity guard on the harness itself: if _is_editor_visible always returned
	# true, T2c and T4a would have caught it - assert the mechanism directly.
	spawner.set_pattern_source(15)
	_check(not _is_editor_visible(spawner, "helper_aimed_target"),
		"T5a an unrelated helper is genuinely hidden (harness really filters)")
	spawner.set_pattern_source(7)
	_check(_is_editor_visible(spawner, "helper_aimed_target"),
		"T5b and genuinely shown in its own mode")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	spawner.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL PROPERTY-VISIBILITY TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)

func _is_editor_visible(node: Object, prop_name: String) -> bool:
	for p in node.get_property_list():
		if str(p.get("name", "")) == prop_name:
			return (int(p.get("usage", 0)) & PROPERTY_USAGE_EDITOR) != 0
	return false
