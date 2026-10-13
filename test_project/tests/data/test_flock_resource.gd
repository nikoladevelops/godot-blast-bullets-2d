extends BlastTest
## BulletFlock2D, the flock rules resource (how a flock steers; who flies
## together is the arming side's business, never a property here):
## documented defaults (tuned so homing + flocking is visible out of the box), every accepted change emits `changed` exactly once
## (rejected and same-value sets emit nothing), rejects keep the old value
## with exact texts, the serialized Scope ids are locked, the inspector
## hints are pinned, and a flock survives a .tres round trip.

const TRES_PATH := "user://flock_round_trip.tres"
const PROPS := ["separation", "alignment", "cohesion", "neighbor_range", "separation_range", "separation_lookahead_sec",
	"count_bullet_size", "max_turn_rate_degrees", "turn_smoothing_sec", "blend_in_sec", "face_heading", "max_neighbors", "update_interval", "view_angle_degrees"]


func test_defaults_match_the_documented_values() -> void:
	var f := BulletFlock2D.new()
	assert_eq(f.separation, 10.0, "separation: turns faster than the spawner's default homing (5)")
	assert_eq(f.alignment, 1.0, "alignment")
	assert_eq(f.cohesion, 1.5, "cohesion")
	assert_eq(f.neighbor_range, 150.0, "neighbor range (an edge gap)")
	assert_eq(f.separation_range, 24.0, "separation range (an edge gap)")
	assert_almost_eq(f.separation_lookahead_sec, 0.2, 1e-6, "separation looks 0.2 s ahead: parting flockmates push less")
	assert_true(f.count_bullet_size, "ranges between bullet edges: one default fits every sprite size")
	assert_eq(f.max_turn_rate_degrees, 270.0, "every turn a smooth arc")
	assert_almost_eq(f.turn_smoothing_sec, 0.1, 1e-6, "turn inertia: a crowd never makes a bullet vibrate")
	assert_almost_eq(f.blend_in_sec, 0.3, 1e-6, "a new volley eases in over 0.3 s")
	assert_true(f.face_heading, "the rotation follows the heading")
	assert_eq(f.max_neighbors, 32, "32 flockmates per bullet")
	assert_eq(f.update_interval, 0.0, "steering recomputed every tick")
	for banned in ["scope", "flock_scope", "enabled", "group", "flock_index"]:
		assert_false(banned in f, "%s: who flies together is decided where the flock is armed" % banned)


func test_property_list_order_and_hints_are_pinned() -> void:
	var want := {
		"separation": [TYPE_FLOAT, PROPERTY_HINT_RANGE, "0,20,0.01,or_greater"],
		"alignment": [TYPE_FLOAT, PROPERTY_HINT_RANGE, "0,20,0.01,or_greater"],
		"cohesion": [TYPE_FLOAT, PROPERTY_HINT_RANGE, "0,20,0.01,or_greater"],
		"neighbor_range": [TYPE_FLOAT, PROPERTY_HINT_RANGE, "1,1000,0.5,or_greater,suffix:px"],
		"separation_range": [TYPE_FLOAT, PROPERTY_HINT_RANGE, "0,500,0.5,or_greater,suffix:px"],
		"separation_lookahead_sec": [TYPE_FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:s"],
		"count_bullet_size": [TYPE_BOOL, PROPERTY_HINT_NONE, ""],
		"max_turn_rate_degrees": [TYPE_FLOAT, PROPERTY_HINT_RANGE, "0,1440,1,or_greater,suffix:deg/s"],
		"turn_smoothing_sec": [TYPE_FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:s"],
		"blend_in_sec": [TYPE_FLOAT, PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:s"],
		"face_heading": [TYPE_BOOL, PROPERTY_HINT_NONE, ""],
		"max_neighbors": [TYPE_INT, PROPERTY_HINT_RANGE, "0,256,1,or_greater"],
		"update_interval": [TYPE_FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:s"],
		"view_angle_degrees": [TYPE_FLOAT, PROPERTY_HINT_RANGE, "0,360,1,suffix:deg"],
	}
	var order: Array = []
	var problems: Array = []
	for p in ClassDB.class_get_property_list("BulletFlock2D", true):
		var name := str(p["name"])
		if not want.has(name):
			continue
		order.append(name)
		var w: Array = want[name]
		if int(p["type"]) != w[0] or int(p["hint"]) != w[1] or str(p["hint_string"]) != w[2]:
			problems.append("%s: type %d hint %d '%s'" % [name, p["type"], p["hint"], p["hint_string"]])
		if (int(p["usage"]) & PROPERTY_USAGE_STORAGE) == 0 or (int(p["usage"]) & PROPERTY_USAGE_EDITOR) == 0:
			problems.append("%s: not stored and shown" % name)
	assert_eq(order, PROPS, "inspector order")
	assert_eq("\n".join(problems), "", "hints and usage")


func test_every_accepted_set_emits_changed_once() -> void:
	var f := BulletFlock2D.new()
	watch_signals(f)
	var sets := [["separation", 3.0], ["alignment", 0.0], ["cohesion", 2.5], ["neighbor_range", 64.0],
		["separation_range", 0.0], ["separation_lookahead_sec", 0.0], ["count_bullet_size", false], ["max_turn_rate_degrees", 180.0], ["turn_smoothing_sec", 0.25], ["blend_in_sec", 1.5], ["face_heading", false], ["max_neighbors", 0], ["update_interval", 0.25], ["view_angle_degrees", 270.0]]
	var expected := 0
	for entry in sets:
		assert_true(entry[0] in f, "%s exists" % entry[0])
		f.set(entry[0], entry[1])
		expected += 1
		assert_eq(f.get(entry[0]), entry[1], "%s round-trips" % entry[0])
		assert_signal_emit_count(f, "changed", expected, "%s emits changed once" % entry[0])
	for entry in sets:
		f.set(entry[0], entry[1])
	assert_signal_emit_count(f, "changed", expected, "setting the same value again emits nothing")


func test_rejected_sets_keep_the_value_and_emit_nothing() -> void:
	var f := BulletFlock2D.new()
	watch_signals(f)
	f.separation = -0.5
	f.alignment = NAN
	f.cohesion = INF
	f.neighbor_range = 0.0
	f.neighbor_range = -10.0
	f.separation_range = -1.0
	f.max_turn_rate_degrees = -90.0
	f.max_turn_rate_degrees = INF
	f.max_neighbors = -1
	f.separation_lookahead_sec = -0.1
	f.blend_in_sec = NAN
	f.turn_smoothing_sec = -1.0
	expect_error_sequence([
		"BulletFlock2D: separation must be finite and >= 0, keeping the old value.",
		"BulletFlock2D: alignment must be finite and >= 0, keeping the old value.",
		"BulletFlock2D: cohesion must be finite and >= 0, keeping the old value.",
		"BulletFlock2D: neighbor_range must be finite and > 0, keeping the old value.",
		"BulletFlock2D: neighbor_range must be finite and > 0, keeping the old value.",
		"BulletFlock2D: separation_range must be finite and >= 0, keeping the old value.",
		"BulletFlock2D: max_turn_rate_degrees must be finite and >= 0, keeping the old value.",
		"BulletFlock2D: max_turn_rate_degrees must be finite and >= 0, keeping the old value.",
		"BulletFlock2D: max_neighbors must be >= 0 (0 = no limit), keeping the old value.",
		"BulletFlock2D: separation_lookahead_sec must be finite and >= 0, keeping the old value.",
		"BulletFlock2D: blend_in_sec must be finite and >= 0, keeping the old value.",
		"BulletFlock2D: turn_smoothing_sec must be finite and >= 0, keeping the old value.",
	])
	var fresh := BulletFlock2D.new()
	for prop in PROPS:
		assert_eq(f.get(prop), fresh.get(prop), prop + " kept")
	assert_signal_emit_count(f, "changed", 0, "rejected sets emit nothing")


func test_huge_finite_values_are_accepted() -> void:
	# No hidden limits: the developer decides (the solver stays bounded).
	var f := BulletFlock2D.new()
	f.neighbor_range = 1e30
	f.separation_range = 1e30
	f.separation = 1e30
	f.max_neighbors = 2147483647
	f.neighbor_range = 0.001
	expect_no_errors("finite values of any size are valid")
	assert_eq(f.max_neighbors, 2147483647, "no upper bound on the neighbor cap")
	assert_almost_eq(f.neighbor_range, 0.001, 1e-9, "tiny ranges are valid")


func test_scope_ids_are_locked() -> void:
	assert_eq(BulletFlock2D.SCOPE_PER_SPAWNER, 0, "Per Spawner is 0")
	assert_eq(BulletFlock2D.SCOPE_SHARED, 1, "Shared is 1")
	assert_eq(BulletFlock2D.SCOPE_PER_VOLLEY, 2, "Per Volley is 2")
	assert_eq(Array(ClassDB.class_get_enum_constants("BulletFlock2D", "Scope", true)),
		["SCOPE_PER_SPAWNER", "SCOPE_SHARED", "SCOPE_PER_VOLLEY"], "the Scope enum")


func test_flock_survives_a_tres_round_trip() -> void:
	var f := BulletFlock2D.new()
	f.separation = 2.25
	f.alignment = 0.5
	f.cohesion = 0.75
	f.neighbor_range = 72.0
	f.separation_range = 18.5
	f.count_bullet_size = false
	f.max_turn_rate_degrees = 90.0
	f.separation_lookahead_sec = 0.45
	f.blend_in_sec = 0.75
	f.turn_smoothing_sec = 0.375
	f.face_heading = false
	f.max_neighbors = 12
	f.update_interval = 0.125
	f.view_angle_degrees = 300.0
	assert_eq(ResourceSaver.save(f, TRES_PATH), OK, "saved")
	var back := ResourceLoader.load(TRES_PATH, "", ResourceLoader.CACHE_MODE_IGNORE) as BulletFlock2D
	DirAccess.remove_absolute(ProjectSettings.globalize_path(TRES_PATH))
	assert_not_null(back, "loads back as a BulletFlock2D")
	if back == null:
		return
	for prop in PROPS:
		assert_eq(back.get(prop), f.get(prop), prop + " survives")


func test_presets_write_the_steering_knobs_and_report_the_match() -> void:
	var f := BulletFlock2D.new()
	assert_eq(f.get_matching_preset(), BulletFlock2D.PRESET_SWARM, "the defaults are the Swarm preset")
	f.count_bullet_size = false
	f.face_heading = false
	f.max_neighbors = 9
	f.update_interval = 0.25 # exact in float32
	watch_signals(f)
	f.apply_preset(BulletFlock2D.PRESET_FLY_TOGETHER)
	assert_signal_emit_count(f, "changed", 1, "one changed per preset")
	assert_eq([f.separation, f.alignment, f.cohesion, f.neighbor_range, f.separation_range, f.max_turn_rate_degrees, f.view_angle_degrees],
		[6.0, 3.0, 2.0, 150.0, 16.0, 180.0, 360.0], "Fly Together: an aligned ribbon")
	assert_eq(f.get_matching_preset(), BulletFlock2D.PRESET_FLY_TOGETHER, "reported")
	assert_eq([f.count_bullet_size, f.face_heading, f.max_neighbors, f.update_interval], [false, false, 9, 0.25], "size, facing, cap and interval are left alone")
	f.apply_preset(BulletFlock2D.PRESET_FLY_TOGETHER)
	f.apply_preset(BulletFlock2D.PRESET_CUSTOM)
	assert_signal_emit_count(f, "changed", 1, "re-applying the same preset or Custom changes nothing")
	f.preset = BulletFlock2D.PRESET_KEEP_DISTANCE
	assert_eq([f.alignment, f.cohesion], [0.0, 0.0], "Keep Distance: only avoid each other")
	assert_eq(f.preset, BulletFlock2D.PRESET_KEEP_DISTANCE, "the inspector menu reads the match")
	f.separation = 2.0
	assert_eq(f.preset, BulletFlock2D.PRESET_CUSTOM, "a tuned knob is Custom")
	f.apply_preset(5)
	f.apply_preset(-1)
	expect_error_sequence([
		"BulletFlock2D: preset must be 0 (Custom), 1 (Swarm), 2 (Fly Together), 3 (Keep Distance) or 4 (Tight Group), keeping the old values.",
		"BulletFlock2D: preset must be 0 (Custom), 1 (Swarm), 2 (Fly Together), 3 (Keep Distance) or 4 (Tight Group), keeping the old values.",
	])
	assert_eq(f.separation, 2.0, "kept")


func test_create_preset_builds_each_preset() -> void:
	for p in [BulletFlock2D.PRESET_CUSTOM, BulletFlock2D.PRESET_SWARM, BulletFlock2D.PRESET_FLY_TOGETHER, BulletFlock2D.PRESET_KEEP_DISTANCE, BulletFlock2D.PRESET_TIGHT_GROUP]:
		var f := BulletFlock2D.create_preset(p)
		assert_not_null(f, "preset %d built" % p)
		var want: int = BulletFlock2D.PRESET_SWARM if p == BulletFlock2D.PRESET_CUSTOM else p
		assert_eq(f.get_matching_preset(), want, "preset %d: its rules (Custom = the defaults)" % p)
	assert_null(BulletFlock2D.create_preset(9), "a bad id builds nothing")
	expect_error("BulletFlock2D.create_preset: preset must be 0 (Custom), 1 (Swarm), 2 (Fly Together), 3 (Keep Distance) or 4 (Tight Group), nothing created.")


func test_the_preset_menu_is_editor_only_and_its_ids_are_locked() -> void:
	assert_eq([BulletFlock2D.PRESET_CUSTOM, BulletFlock2D.PRESET_SWARM, BulletFlock2D.PRESET_FLY_TOGETHER, BulletFlock2D.PRESET_KEEP_DISTANCE, BulletFlock2D.PRESET_TIGHT_GROUP], [0, 1, 2, 3, 4], "ids")
	var found := false
	for p in ClassDB.class_get_property_list("BulletFlock2D", true):
		if str(p["name"]) != "preset":
			continue
		found = true
		assert_eq(str(p["hint_string"]), "Custom,Swarm,Fly Together,Keep Distance,Tight Group", "menu entries")
		assert_true((int(p["usage"]) & PROPERTY_USAGE_EDITOR) != 0, "shown")
		assert_eq(int(p["usage"]) & PROPERTY_USAGE_STORAGE, 0, "never saved (the knobs are)")
	assert_true(found, "the inspector has a preset menu")
	var f := BulletFlock2D.create_preset(BulletFlock2D.PRESET_TIGHT_GROUP)
	assert_eq(ResourceSaver.save(f, TRES_PATH), OK, "saved")
	var back := ResourceLoader.load(TRES_PATH, "", ResourceLoader.CACHE_MODE_IGNORE) as BulletFlock2D
	DirAccess.remove_absolute(ProjectSettings.globalize_path(TRES_PATH))
	assert_eq(back.get_matching_preset(), BulletFlock2D.PRESET_TIGHT_GROUP, "the saved knobs still match the preset")
