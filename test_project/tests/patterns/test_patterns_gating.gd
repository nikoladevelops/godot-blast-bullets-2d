extends BlastTest
## Inspector gating of the helper_* knobs, pinned as an independent truth
## table (nothing here reads the C++ rules back). Ownership: a shape knob
## shows only under the shape whose registry prefix owns it, longest prefix
## first (helper_star_polygon_* is Star Polygon's, not Star's). Sub-rules:
## flower type, ellipse wall, equilateral triangle, Path2D fixed spacing, the
## shared outline layout (placement, layer fill, corner shapes, pinned
## corners) and the shared amount / skip / aimed-target knobs. The registry
## itself (BulletPatterns2D.get_shapes) matches the pattern_source hint, and
## its capability flags match the documented shape families.

const OUTLINE_SHAPES := ["Circle", "Square", "Rectangle", "Triangle", "Diamond", "Trapezoid", "Polygon", "Ellipse", "Ring", "Star", "Heart", "Flower", "Rose", "Lissajous"]
const CORNER_SHAPES := ["Square", "Rectangle", "Triangle", "Diamond", "Trapezoid", "Polygon", "Star"]
# Read nodes or user arrays, so the bake cache always regenerates them.
const EXTERNAL_SHAPES := ["From Children", "Path2D", "Aimed", "Custom", "Corridor"]
# Knobs shared across shapes (gated by their own rules below).
const SHARED_KNOBS := ["helper_bullets_amount", "helper_skip_indices", "helper_aimed_target"]
# Shape knobs with a sub-rule: hidden at some default setting of their shape.
const SUB_RULED := ["helper_flower_petals", "helper_flower_petal_spread", "helper_flower_petal_sharpness", "helper_flower_inner_radius_scale", "helper_flower_spiro_roller", "helper_flower_spiro_pen", "helper_flower_super_lobes", "helper_flower_super_fullness", "helper_ellipse_gap_count", "helper_ellipse_gap_width", "helper_triangle_size_b", "helper_path2d_spacing", "helper_path2d_overflow", "helper_path2d_anchor"]

var sp: BulletSpawner2D
var shapes: Array = []


func before_each() -> void:
	await super()
	sp = make_spawner(H.make_volley_data(1, 50.0, 30.0))
	shapes = BulletPatterns2D.get_shapes()


func _visible_set() -> Dictionary:
	var out := {}
	for p in sp.get_property_list():
		if (int(p.get("usage", 0)) & PROPERTY_USAGE_EDITOR) != 0:
			out[str(p.get("name", ""))] = true
	return out


func _helper_props(prefix := "helper_") -> Array:
	var out: Array = []
	for p in sp.get_property_list():
		var n := str(p.get("name", ""))
		if n.begins_with(prefix) and (int(p.get("usage", 0)) & PROPERTY_USAGE_STORAGE) != 0:
			out.append(n)
	return out


func _id(shape_name: String) -> int:
	for s in shapes:
		if s["name"] == shape_name:
			return int(s["id"])
	return -1


func _owner(prop: String) -> int:
	var best := -1
	var best_len := 0
	for s in shapes:
		var pre: String = s["knob_prefix"]
		if pre != "" and pre.length() > best_len and prop.begins_with(pre):
			best = int(s["id"])
			best_len = pre.length()
	return best


func test_registry_matches_the_pattern_source_hint_and_flags() -> void:
	assert_eq(shapes.size(), BulletSpawner2D.PATTERN_FROM_LAST, "one row per pattern source")
	var hint := ""
	for p in sp.get_property_list():
		if p["name"] == "pattern_source":
			hint = p["hint_string"]
	var parts: Array = []
	var ids := {}
	var problems: Array = []
	for s in shapes:
		parts.append("%s:%d" % [s["name"], s["id"]])
		ids[int(s["id"])] = true
		if bool(s["outline"]) != OUTLINE_SHAPES.has(s["name"]):
			problems.append("%s outline flag" % s["name"])
		if bool(s["corners"]) != CORNER_SHAPES.has(s["name"]):
			problems.append("%s corners flag" % s["name"])
		if bool(s["reads_external_state"]) != EXTERNAL_SHAPES.has(s["name"]):
			problems.append("%s external flag" % s["name"])
		if bool(s["corners"]) and not bool(s["outline"]):
			problems.append("%s has corners without an outline" % s["name"])
		var pre: String = s["knob_prefix"]
		if s["id"] > BulletSpawner2D.PATTERN_FROM_SELF and not (pre.begins_with("helper_") and pre.ends_with("_")):
			problems.append("%s knob prefix %s" % [s["name"], pre])
	assert_eq(problems, [], "registry flags match the shape families")
	assert_eq(",".join(parts), hint, "the inspector hint is built from the registry, in its order")
	for i in BulletSpawner2D.PATTERN_FROM_LAST:
		assert_true(ids.has(i), "id %d registered" % i)


func test_a_shape_knob_shows_only_under_its_owning_shape() -> void:
	var props := _helper_props()
	var problems: Array = []
	for prop in props:
		if prop.begins_with("helper_outline_") or SHARED_KNOBS.has(prop):
			continue
		if _owner(prop) < 0:
			problems.append("%s has no owning shape prefix" % prop)
	for src in BulletSpawner2D.PATTERN_FROM_LAST:
		sp.pattern_source = src
		var vis := _visible_set()
		for prop in props:
			if prop.begins_with("helper_outline_") or SHARED_KNOBS.has(prop):
				continue
			var owner := _owner(prop)
			if owner != src and vis.has(prop):
				problems.append("%s visible under source %d (owner %d)" % [prop, src, owner])
			elif owner == src and not vis.has(prop) and not SUB_RULED.has(prop):
				problems.append("%s hidden under its own source %d" % [prop, src])
	assert_eq(problems, [], "every shape knob follows its owner")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_STAR
	assert_false(is_editor_visible(sp, &"helper_star_polygon_vertices"), "longest prefix wins: a Star Polygon knob never shows under Star")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_STAR_POLYGON
	assert_false(is_editor_visible(sp, &"helper_star_points"), "and a Star knob never shows under Star Polygon")


func test_flower_knobs_follow_the_flower_type() -> void:
	var all_types := [BulletPatterns2D.FLOWER_FAN, BulletPatterns2D.FLOWER_RHODONEA, BulletPatterns2D.FLOWER_PHYLLOTAXIS, BulletPatterns2D.FLOWER_SPIROGRAPH, BulletPatterns2D.FLOWER_SUPERFORMULA]
	var truth := {
		"helper_flower_type": all_types,
		"helper_flower_radius": all_types,
		"helper_flower_base_rotation": all_types,
		"helper_flower_face_outward": all_types,
		"helper_flower_facing_offset_deg": all_types,
		"helper_flower_petals": [BulletPatterns2D.FLOWER_FAN],
		"helper_flower_petal_spread": [BulletPatterns2D.FLOWER_FAN],
		"helper_flower_petal_sharpness": [BulletPatterns2D.FLOWER_FAN, BulletPatterns2D.FLOWER_RHODONEA],
		"helper_flower_inner_radius_scale": [BulletPatterns2D.FLOWER_RHODONEA, BulletPatterns2D.FLOWER_PHYLLOTAXIS, BulletPatterns2D.FLOWER_SUPERFORMULA],
		"helper_flower_spiro_roller": [BulletPatterns2D.FLOWER_SPIROGRAPH],
		"helper_flower_spiro_pen": [BulletPatterns2D.FLOWER_SPIROGRAPH],
		"helper_flower_super_lobes": [BulletPatterns2D.FLOWER_SUPERFORMULA],
		"helper_flower_super_fullness": [BulletPatterns2D.FLOWER_SUPERFORMULA],
	}
	assert_eq_deep(_helper_props("helper_flower_").filter(func(p): return not truth.has(p)), [])
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER
	var problems: Array = []
	for t in all_types:
		sp.helper_flower_type = t
		var vis := _visible_set()
		for prop in truth:
			if vis.has(prop) != (truth[prop] as Array).has(t):
				problems.append("%s under type %d: visible=%s" % [prop, t, vis.has(prop)])
	assert_eq(problems, [], "flower knobs per type")


func test_ellipse_triangle_and_path2d_sub_rules() -> void:
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_ELLIPSE
	for mode in [BulletPatterns2D.ELLIPSE_FULL, BulletPatterns2D.ELLIPSE_ARC, BulletPatterns2D.ELLIPSE_WALL]:
		sp.helper_ellipse_mode = mode
		var wall: bool = mode == BulletPatterns2D.ELLIPSE_WALL
		assert_eq(is_editor_visible(sp, &"helper_ellipse_gap_count"), wall, "gap count only in WALL (mode %d)" % mode)
		assert_eq(is_editor_visible(sp, &"helper_ellipse_gap_width"), wall, "gap width only in WALL (mode %d)" % mode)
		assert_true(is_editor_visible(sp, &"helper_ellipse_radius_x"), "radius always (mode %d)" % mode)
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_TRIANGLE
	sp.helper_triangle_type = BulletPatterns2D.TRIANGLE_EQUILATERAL
	assert_false(is_editor_visible(sp, &"helper_triangle_size_b"), "equilateral has no second size")
	assert_true(is_editor_visible(sp, &"helper_triangle_size_a"), "first size always")
	sp.helper_triangle_type = BulletPatterns2D.TRIANGLE_EQUILATERAL + 1
	assert_true(is_editor_visible(sp, &"helper_triangle_size_b"), "other kinds show it")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D
	for dist in [BulletSpawner2D.PATH2D_DISTRIBUTION_FIXED_SPACING, BulletSpawner2D.PATH2D_DISTRIBUTION_EVEN]:
		sp.helper_path2d_distribution = dist
		var fixed: bool = dist == BulletSpawner2D.PATH2D_DISTRIBUTION_FIXED_SPACING
		for prop in [&"helper_path2d_spacing", &"helper_path2d_overflow", &"helper_path2d_anchor"]:
			assert_eq(is_editor_visible(sp, prop), fixed, "%s only in Fixed Spacing (distribution %d)" % [prop, dist])
		assert_true(is_editor_visible(sp, &"helper_path2d_closed"), "closed always (distribution %d)" % dist)


func _outline_expected(prop: String, outline: bool, corners: bool, placement: int, layer_fill: int, corner_mode: int) -> bool:
	if not outline:
		return false
	var riding := placement == BulletPatterns2D.OUTLINE_ON_OUTLINE or placement == BulletPatterns2D.OUTLINE_LAYERS
	var layering := placement == BulletPatterns2D.OUTLINE_LAYERS
	match prop:
		"helper_outline_fill_spacing", "helper_outline_fill_stagger", "helper_outline_fill_margin", "helper_outline_fill_min_spacing":
			return placement == BulletPatterns2D.OUTLINE_FILL_INSIDE
		"helper_outline_layer_count", "helper_outline_layer_scale", "helper_outline_layer_side", "helper_outline_layer_fill", "helper_outline_layer_scale_curve", "helper_outline_layer_scales", "helper_outline_layer_twist", "helper_outline_layer_max_dots", "helper_outline_layer_layout":
			return layering
		"helper_outline_layer_start_offset":
			return layering and (layer_fill == BulletPatterns2D.OUTLINE_LAYER_SEQUENTIAL or layer_fill == BulletPatterns2D.OUTLINE_LAYER_OUTER_FIRST)
		"helper_outline_reverse", "helper_outline_slot_offset":
			return riding
		"helper_outline_distribution", "helper_outline_corner_priority", "helper_outline_corner_mode":
			return riding and corners
		"helper_outline_edge_margin":
			return riding and corners and corner_mode == BulletPatterns2D.OUTLINE_CORNER_MODE_PIN_CORNERS
		"helper_outline_placement", "helper_outline_facing", "helper_outline_corner_facing":
			return true
	return false


func test_outline_knobs_follow_shape_placement_fill_and_corner_mode() -> void:
	var props := _helper_props("helper_outline_")
	var known := ["helper_outline_fill_spacing", "helper_outline_fill_stagger", "helper_outline_fill_margin", "helper_outline_fill_min_spacing", "helper_outline_layer_count", "helper_outline_layer_scale", "helper_outline_layer_side", "helper_outline_layer_fill", "helper_outline_layer_scale_curve", "helper_outline_layer_scales", "helper_outline_layer_twist", "helper_outline_layer_max_dots", "helper_outline_layer_layout", "helper_outline_layer_start_offset", "helper_outline_reverse", "helper_outline_slot_offset", "helper_outline_distribution", "helper_outline_corner_priority", "helper_outline_corner_mode", "helper_outline_edge_margin", "helper_outline_placement", "helper_outline_facing", "helper_outline_corner_facing"]
	assert_eq_deep(props.filter(func(p): return not known.has(p)), [])
	var problems: Array = []
	var combos := 0
	# A smooth loop, two corner loops, a non-outline shape and the scatter
	# disc (which never takes the outline layout).
	for shape_name in ["Circle", "Rectangle", "Star", "Line", "Scatter"]:
		sp.pattern_source = _id(shape_name)
		var outline: bool = OUTLINE_SHAPES.has(shape_name)
		var corners: bool = CORNER_SHAPES.has(shape_name)
		for placement in [BulletPatterns2D.OUTLINE_ON_OUTLINE, BulletPatterns2D.OUTLINE_LAYERS, BulletPatterns2D.OUTLINE_FILL_INSIDE]:
			sp.helper_outline_placement = placement
			for layer_fill in [BulletPatterns2D.OUTLINE_LAYER_INTERLEAVED, BulletPatterns2D.OUTLINE_LAYER_SEQUENTIAL, BulletPatterns2D.OUTLINE_LAYER_OUTER_FIRST, BulletPatterns2D.OUTLINE_LAYER_PINGPONG]:
				sp.helper_outline_layer_fill = layer_fill
				for corner_mode in [BulletPatterns2D.OUTLINE_CORNER_MODE_PIN_CORNERS, BulletPatterns2D.OUTLINE_CORNER_MODE_EVEN_ARC]:
					sp.helper_outline_corner_mode = corner_mode
					combos += 1
					var vis := _visible_set()
					for prop in props:
						var want := _outline_expected(prop, outline, corners, placement, layer_fill, corner_mode)
						if vis.has(prop) != want:
							problems.append("%s on %s placement %d fill %d corner %d: visible=%s" % [prop, shape_name, placement, layer_fill, corner_mode, vis.has(prop)])
	assert_eq(combos, 120, "5 shapes x 3 placements x 4 fills x 2 corner modes")
	assert_eq(problems, [], "outline knobs per shape/placement/fill/corner mode")


func test_shared_amount_skip_and_aimed_target() -> void:
	var problems: Array = []
	for s in shapes:
		sp.pattern_source = int(s["id"])
		var vis := _visible_set()
		var helper_shape: bool = s["id"] > BulletSpawner2D.PATTERN_FROM_SELF
		if vis.has("helper_bullets_amount") != (helper_shape and s["name"] != "Custom"):
			problems.append("amount under %s" % s["name"])
		if vis.has("helper_skip_indices") != helper_shape:
			problems.append("skip indices under %s" % s["name"])
		if vis.has("helper_aimed_target") != (s["name"] == "Aimed" or s["name"] == "Corridor"):
			problems.append("aimed target under %s" % s["name"])
	assert_eq(problems, [], "shared knobs: amount everywhere but Children/Self/Custom, skip everywhere but Children/Self, the aimed target in Aimed + Corridor")
