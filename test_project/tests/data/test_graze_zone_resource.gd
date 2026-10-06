extends BlastTest
## BulletGrazeZone2D, the graze configuration resource: documented defaults,
## ring knobs beyond ring_count hide in the inspector (still stored), every
## accepted change emits `changed` exactly once (rejected and same-value sets
## emit nothing), index accessors reject out-of-range indexes with exact
## texts, the serialized regraze ids are locked, and a zone survives a .tres
## round trip with every field intact.

const TRES_PATH := "user://graze_zone_round_trip.tres"


func test_defaults_match_the_documented_values() -> void:
	var z := BulletGrazeZone2D.new()
	assert_true(z.enabled, "enabled by default")
	assert_eq(z.target_group, &"player", "target group")
	assert_eq(z.ring_count, 1, "one ring")
	assert_eq([z.ring_1_radius, z.ring_2_radius, z.ring_3_radius, z.ring_4_radius], [24.0, 40.0, 56.0, 72.0], "ring radii")
	assert_true(z.count_bullet_size, "bullet size counts")
	assert_eq(z.regraze, BulletGrazeZone2D.REGRAZE_ONCE, "graze once")
	assert_true(z.preview_color.is_equal_approx(Color(0.2, 0.9, 0.8, 0.8)), "preview color")
	assert_eq(BulletGrazeZone2D.MAX_RINGS, 4, "ring cap")
	assert_eq(BulletGrazeZone2D.MAX_TARGETS, 4, "target cap")


func test_ring_properties_hide_beyond_ring_count() -> void:
	var z := BulletGrazeZone2D.new()
	for count in [1, 2, 3, 4]:
		z.ring_count = count
		var shown: Array = []
		for n in [1, 2, 3, 4]:
			if is_editor_visible(z, StringName("ring_%d_radius" % n)):
				shown.append(n)
		assert_eq(shown, range(1, count + 1), "ring_count %d shows rings 1..%d" % [count, count])
	z.ring_count = 1
	z.ring_4_radius = 99.0
	assert_eq(z.ring_4_radius, 99.0, "a hidden ring keeps a writable stored radius")
	var stored := false
	for p in z.get_property_list():
		if str(p["name"]) == "ring_4_radius":
			stored = (int(p["usage"]) & PROPERTY_USAGE_STORAGE) != 0
	assert_true(stored, "hidden rings are still saved")


func test_every_accepted_set_emits_changed_once() -> void:
	var z := BulletGrazeZone2D.new()
	watch_signals(z)
	var sets := [["enabled", false], ["target_group", &"enemies"], ["ring_count", 3], ["ring_1_radius", 10.0],
		["ring_2_radius", 20.0], ["ring_3_radius", 30.0], ["ring_4_radius", 40.0], ["count_bullet_size", false],
		["regraze", BulletGrazeZone2D.REGRAZE_AFTER_EXIT], ["preview_color", Color.RED]]
	var expected := 0
	for entry in sets:
		assert_true(entry[0] in z, "%s exists" % entry[0])
		z.set(entry[0], entry[1])
		expected += 1
		assert_eq(z.get(entry[0]), entry[1], "%s round-trips" % entry[0])
		assert_signal_emit_count(z, "changed", expected, "%s emits changed once" % entry[0])
	for entry in sets:
		z.set(entry[0], entry[1])
	assert_signal_emit_count(z, "changed", expected, "setting the same value again emits nothing")
	z.set_ring_radius(1, 21.0)
	assert_eq(z.ring_2_radius, 21.0, "set_ring_radius writes ring_2_radius")
	assert_signal_emit_count(z, "changed", expected + 1, "set_ring_radius emits changed")


func test_rejected_sets_keep_the_value_and_emit_nothing() -> void:
	var z := BulletGrazeZone2D.new()
	watch_signals(z)
	z.ring_count = 0
	z.ring_count = 5
	z.ring_1_radius = 0.0
	z.ring_2_radius = -3.0
	z.regraze = 7
	z.preview_color = Color(NAN, 0, 0, 1)
	expect_error_sequence([
		"BulletGrazeZone2D: ring_count must be between 1 and 4, keeping the old value.",
		"BulletGrazeZone2D: ring_count must be between 1 and 4, keeping the old value.",
		"BulletGrazeZone2D: ring_1_radius must be finite and > 0, keeping the old value.",
		"BulletGrazeZone2D: ring_2_radius must be finite and > 0, keeping the old value.",
		"BulletGrazeZone2D: regraze must be 0 (Once) or 1 (After Exit), keeping the old value.",
		"BulletGrazeZone2D: preview_color must be finite, keeping the old value.",
	])
	assert_eq(z.ring_count, 1, "ring_count kept")
	assert_eq(z.ring_1_radius, 24.0, "ring 1 kept")
	assert_eq(z.ring_2_radius, 40.0, "ring 2 kept")
	assert_eq(z.regraze, BulletGrazeZone2D.REGRAZE_ONCE, "regraze kept")
	assert_signal_emit_count(z, "changed", 0, "rejected sets emit nothing")


func test_index_accessors_reject_out_of_range_indexes() -> void:
	var z := BulletGrazeZone2D.new()
	assert_eq(z.get_ring_radius(3), 72.0, "index 3 is ring_4 whatever ring_count is")
	assert_eq(z.get_ring_radius(4), 0.0, "OOB reads 0")
	assert_eq(z.get_ring_radius(-1), 0.0, "negative reads 0")
	z.set_ring_radius(4, 10.0)
	z.set_ring_radius(0, NAN)
	expect_error_sequence([
		"BulletGrazeZone2D.get_ring_radius: index 4 is out of range 0..3.",
		"BulletGrazeZone2D.get_ring_radius: index -1 is out of range 0..3.",
		"BulletGrazeZone2D.set_ring_radius: index 4 is out of range 0..3, nothing changed.",
		"BulletGrazeZone2D: ring_1_radius must be finite and > 0, keeping the old value.",
	])
	assert_eq(z.ring_1_radius, 24.0, "kept")


func test_active_ring_radii_follow_ring_count() -> void:
	var z := BulletGrazeZone2D.new()
	assert_eq(Array(z.get_active_ring_radii()), [24.0], "one ring")
	z.ring_count = 3
	assert_eq(Array(z.get_active_ring_radii()), [24.0, 40.0, 56.0], "three rings")


func test_regraze_ids_are_locked() -> void:
	assert_eq(BulletGrazeZone2D.REGRAZE_ONCE, 0, "Once is 0")
	assert_eq(BulletGrazeZone2D.REGRAZE_AFTER_EXIT, 1, "After Exit is 1")
	var hint := ""
	for p in ClassDB.class_get_property_list("BulletGrazeZone2D", true):
		if str(p["name"]) == "regraze":
			hint = str(p["hint_string"])
	assert_eq(hint, "Once:0,After Exit:1", "serialized enum hint")


func test_zone_survives_a_tres_round_trip() -> void:
	var z := BulletGrazeZone2D.new()
	z.enabled = false
	z.target_group = &"heroes"
	z.ring_count = 4
	z.ring_1_radius = 11.0
	z.ring_2_radius = 22.0
	z.ring_3_radius = 33.0
	z.ring_4_radius = 44.0
	z.count_bullet_size = false
	z.regraze = BulletGrazeZone2D.REGRAZE_AFTER_EXIT
	z.preview_color = Color(0.25, 0.5, 0.75, 1.0)
	assert_eq(ResourceSaver.save(z, TRES_PATH), OK, "saved")
	var back := ResourceLoader.load(TRES_PATH, "", ResourceLoader.CACHE_MODE_IGNORE) as BulletGrazeZone2D
	DirAccess.remove_absolute(ProjectSettings.globalize_path(TRES_PATH))
	assert_not_null(back, "loads back as a BulletGrazeZone2D")
	if back == null:
		return
	for prop in ["enabled", "target_group", "ring_count", "ring_1_radius", "ring_2_radius", "ring_3_radius", "ring_4_radius", "count_bullet_size", "regraze", "preview_color"]:
		assert_eq(back.get(prop), z.get(prop), prop + " survives")
