extends BlastTest
## Reject-and-keep contract for EVERY spawner property, discovered from
## get_property_list() so new properties are covered automatically:
##   - FLOAT: NaN and +Inf are rejected with exactly one error that says the
##     old value is kept, and the value is unchanged;
##   - VECTOR2: a NaN vector is rejected the same way;
##   - PACKED_VECTOR2_ARRAY: a list with a NaN or Inf entry is rejected the
##     same way;
##   - INT with an enum hint: an id outside the hint is rejected the same way.
## Properties that legitimately accept any value are listed with a reason.

## Every rejection message says this (exact wording pinned below).
const KEEP_TEXT := "keeping the old value"

## FLOAT/VECTOR2 properties with no finiteness rule, and why.
const ANY_VALUE_OK := {
}


## Properties BulletSpawner2D declares itself (inherited Node2D ones are the
## engine's business), as seen on a live instance (usage flags applied).
func _editor_props(obj: Object) -> Array:
	var own := {}
	for p in ClassDB.class_get_property_list("BulletSpawner2D", true):
		own[str(p.name)] = true
	var out: Array = []
	for p in obj.get_property_list():
		if not own.has(str(p.name)):
			continue
		var usage := int(p.get("usage", 0))
		if (usage & PROPERTY_USAGE_STORAGE) == 0 and (usage & PROPERTY_USAGE_EDITOR) == 0:
			continue
		if (usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) != 0:
			continue
		out.append(p)
	return out


## Sets `value` and returns a problem string, or "" when the property
## rejected it loudly (one KEEP_TEXT error) and kept the old value.
func _probe(sp: BulletSpawner2D, prop: String, value: Variant) -> String:
	var before: Variant = sp.get(prop)
	expect_no_errors("clean before probing " + prop)
	sp.set(prop, value)
	var after: Variant = sp.get(prop)
	var seen := 0
	var wrong := ""
	for err in get_errors():
		if err.handled:
			continue
		err.handled = true
		if err.contains_text(KEEP_TEXT):
			seen += 1
		else:
			wrong = str(err.code)
	if seen != 1 or wrong != "":
		return "%s = %s: expected one '%s' error, got %d (other: %s)" % [prop, str(value), KEEP_TEXT, seen, wrong]
	if typeof(before) != typeof(after) or not (before == after or (typeof(before) == TYPE_FLOAT and is_nan(before) and is_nan(after))):
		return "%s = %s: value changed from %s to %s" % [prop, str(value), str(before), str(after)]
	return ""


func _assert_none(bad: Array, what: String) -> void:
	assert_true(bad.is_empty(), "%s - %d problem(s):\n  %s" % [what, bad.size(), "\n  ".join(bad)])


func test_float_properties_reject_nan_and_inf() -> void:
	var sp := make_spawner()
	var bad: Array = []
	var probed := 0
	for p in _editor_props(sp):
		var prop := str(p.name)
		if int(p.type) != TYPE_FLOAT or ANY_VALUE_OK.has(prop):
			continue
		probed += 1
		for v in [NAN, INF]:
			var problem := _probe(sp, prop, v)
			if problem != "":
				bad.append(problem)
	assert_gt(probed, 100, "the sweep really covers the float surface")
	_assert_none(bad, "float setters reject NaN/Inf and keep the old value")


func test_vector_properties_reject_nan() -> void:
	var sp := make_spawner()
	var bad: Array = []
	var probed := 0
	for p in _editor_props(sp):
		var prop := str(p.name)
		if int(p.type) != TYPE_VECTOR2 or ANY_VALUE_OK.has(prop):
			continue
		probed += 1
		var problem := _probe(sp, prop, Vector2(NAN, 1.0))
		if problem != "":
			bad.append(problem)
	assert_gt(probed, 8, "the sweep really covers the vector surface")
	_assert_none(bad, "vector setters reject NaN and keep the old value")


func test_point_list_properties_reject_non_finite_entries() -> void:
	var sp := make_spawner()
	var bad: Array = []
	var probed := 0
	for p in _editor_props(sp):
		var prop := str(p.name)
		if int(p.type) != TYPE_PACKED_VECTOR2_ARRAY or ANY_VALUE_OK.has(prop):
			continue
		probed += 1
		sp.set(prop, PackedVector2Array([Vector2(1, 2)])) # an old value worth keeping
		for v in [PackedVector2Array([Vector2(3, 4), Vector2(NAN, 0)]), PackedVector2Array([Vector2(0, INF)])]:
			var problem := _probe(sp, prop, v)
			if problem != "":
				bad.append(problem)
	assert_gte(probed, 1, "the sweep covers every point list (graze_global_positions)")
	_assert_none(bad, "point lists reject non-finite entries and keep the old value")


func test_color_properties_reject_nan() -> void:
	var sp := make_spawner()
	var bad: Array = []
	var probed := 0
	for p in _editor_props(sp):
		if int(p.type) != TYPE_COLOR:
			continue
		probed += 1
		var problem := _probe(sp, str(p.name), Color(NAN, 0.5, 0.5, 1.0))
		if problem != "":
			bad.append(problem)
	assert_gt(probed, 4, "the sweep really covers the colour surface")
	_assert_none(bad, "colour setters reject NaN and keep the old value")


func test_enum_properties_reject_unknown_ids() -> void:
	var sp := make_spawner()
	var bad: Array = []
	var probed := 0
	for p in _editor_props(sp):
		var prop := str(p.name)
		if int(p.type) != TYPE_INT or int(p.hint) != PROPERTY_HINT_ENUM:
			continue
		var ids: Array = []
		var next := 0
		for entry in str(p.hint_string).split(","):
			var parts := entry.split(":")
			var id := int(parts[1]) if parts.size() > 1 else next
			ids.append(id)
			next = id + 1
		probed += 1
		for v in [ids.max() + 1, ids.min() - 1]:
			var problem := _probe(sp, prop, v)
			if problem != "":
				bad.append(problem)
	assert_gt(probed, 20, "the sweep really covers the enum surface")
	_assert_none(bad, "enum setters reject ids outside their hint and keep the old value")


# --- Specific rules --------------------------------------------------------

func test_caps_match_the_generators() -> void:
	var sp := make_spawner()
	for case in [
		["helper_ellipse_gap_count", 100001, "helper_ellipse_gap_count must be between 0 and 100000"],
		["helper_star_points", 100001, "helper_star_points must be between 2 and 100000"],
		["helper_polygon_vertices", 100001, "helper_polygon_vertices must be between 3 and 100000"],
		["helper_waterfall_columns", 400001, "helper_waterfall_columns must be between 1 and 400000"],
		["helper_waterfall_rows", 400001, "helper_waterfall_rows must be between 1 and 400000"],
		["helper_lattice_columns", 400001, "helper_lattice_columns must be between 1 and 400000"],
		["helper_lattice_rows", 400001, "helper_lattice_rows must be between 1 and 400000"],
		["burst_count", 1025, "burst_count must be between 1 and 1024"],
		["helper_line_spacing", 0.0, "helper_line_spacing must be finite and > 0"],
		["helper_line_spacing", -5.0, "helper_line_spacing must be finite and > 0"],
		["helper_ring_y_scale", 0.0, "helper_ring_y_scale must be finite and > 0"],
		["helper_ring_y_scale", -1.0, "helper_ring_y_scale must be finite and > 0"],
	]:
		var before: Variant = sp.get(case[0])
		sp.set(case[0], case[1])
		expect_error_sequence([case[2]], "%s = %s" % [case[0], str(case[1])])
		assert_eq(sp.get(case[0]), before, "%s kept its old value" % case[0])


func test_grid_product_over_the_cap_warns_once_and_fires_nothing() -> void:
	var sp := make_spawner(null, BulletSpawner2D.PATTERN_FROM_HELPER_LATTICE, 10)
	sp.helper_lattice_columns = 1000
	sp.helper_lattice_rows = 1000 # 1000000 > 400000 slots
	assert_eq(sp.collect_spawn_transforms().size(), 0, "no slots")
	assert_eq(sp.collect_spawn_transforms().size(), 0, "still none")
	var warnings := 0
	for err in get_errors():
		if err.is_push_warning() and err.contains_text("helper_lattice_columns * helper_lattice_rows exceeds 400000"):
			warnings += 1
	assert_eq(warnings, 1, "warned once, not per collect")


func test_corridor_width_and_gap_load_in_any_order() -> void:
	# Width is stored (and loaded) before the gap: a narrow corridor used to
	# be rejected at load because the default gap (96) was still in place.
	var src := BulletSpawner2D.new()
	src.set_shooting_enabled(false)
	src.helper_corridor_gap_width = 20.0
	src.helper_corridor_width = 60.0
	var scene := PackedScene.new()
	assert_eq(scene.pack(src), OK, "packs")
	src.free()
	var loaded: BulletSpawner2D = scene.instantiate()
	assert_eq(loaded.helper_corridor_width, 60.0, "narrow width survives loading")
	assert_eq(loaded.helper_corridor_gap_width, 20.0, "gap survives loading")
	loaded.free()


func test_corridor_gap_wider_than_wall_is_clamped_with_one_warning() -> void:
	var sp := make_spawner(null, BulletSpawner2D.PATTERN_FROM_HELPER_CORRIDOR, 10)
	sp.helper_corridor_width = 100.0
	sp.helper_corridor_gap_width = 150.0 # accepted alone: load order must not matter
	var pts: Array = sp.collect_spawn_transforms()
	sp.collect_spawn_transforms()
	assert_eq(pts.size(), 10, "every bullet still fires")
	for t in pts:
		var x: float = (t as Transform2D).origin.x
		assert_true(absf(x) >= 25.0 - 0.01 and absf(x) <= 50.0 + 0.01, "door clamped to half the width (x=%.2f)" % x)
	var warnings := 0
	for err in get_errors():
		if err.is_push_warning() and err.contains_text("helper_corridor_gap_width must be smaller than helper_corridor_width"):
			warnings += 1
	assert_eq(warnings, 1, "warned once")


func test_node_paths_reject_wrong_types_in_tree() -> void:
	var sp := make_spawner()
	var plain := Node.new()
	add(plain)
	var node2d := Node2D.new()
	add(node2d)
	var path_to_plain := sp.get_path_to(plain)
	var path_to_2d := sp.get_path_to(node2d)
	var old_factory: NodePath = sp.bullet_factory_path
	sp.bullet_factory_path = path_to_2d
	expect_error_sequence(["bullet_factory_path must point to a BulletFactory2D"])
	assert_eq(sp.bullet_factory_path, old_factory, "factory path kept")
	sp.transforms_generator = path_to_plain
	expect_error_sequence(["transforms_generator must point to a Node2D"])
	sp.helper_aimed_target = path_to_plain
	expect_error_sequence(["helper_aimed_target must point to a Node2D"])
	sp.helper_path2d_path = path_to_2d
	expect_error_sequence(["helper_path2d_path must point to a Path2D"])
	sp.movement_path = path_to_2d
	expect_error_sequence(["movement_path must point to a Path2D"])
	sp.transforms_generator = NodePath("DoesNotExistYet")
	expect_no_errors("an unresolved path is accepted (the node may come later)")
	assert_eq(sp.transforms_generator, NodePath("DoesNotExistYet"), "pending path stored")


func test_shorter_interval_applies_to_the_running_wait() -> void:
	var sp := make_spawner()
	sp.set_shoot_interval_sec(10.0)
	watch_signals(sp)
	sp.set_shooting_enabled(true) # first shot is due at once, then a 10 s wait
	for i in 5:
		await idle(1)
		if get_signal_emit_count(sp, "volley_fired") > 0:
			break
	assert_eq(get_signal_emit_count(sp, "volley_fired"), 1, "first shot")
	sp.set_shoot_interval_sec(0.05) # the running 10 s wait shrinks to 0.05 s
	for i in 20:
		await idle(1)
		if get_signal_emit_count(sp, "volley_fired") > 1:
			break
	assert_eq(get_signal_emit_count(sp, "volley_fired"), 2, "second shot on the new, shorter interval")
	sp.set_shooting_enabled(false)


func test_out_of_range_skip_index_warns_once_and_never_from_the_preview() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6)
	sp.helper_skip_indices = PackedInt32Array([99])
	await idle(6) # several preview rebuilds
	var warnings := 0
	for err in get_errors():
		if err.is_push_warning() and err.contains_text("helper_skip_indices holds an index outside"):
			warnings += 1
	assert_eq(warnings, 0, "the preview stays quiet")
	assert_true(sp.shoot_once(), "shot 1")
	assert_true(sp.shoot_once(), "shot 2")
	for err in get_errors():
		if err.is_push_warning() and err.contains_text("helper_skip_indices holds an index outside"):
			warnings += 1
	assert_eq(warnings, 1, "one warning for two shots")
