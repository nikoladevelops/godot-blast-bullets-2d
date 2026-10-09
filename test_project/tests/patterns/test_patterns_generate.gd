extends BlastTest
## BulletPatterns2D.generate(shape, amount, marker, params): every pattern
## source without a spawner. Parity: for all 33 sources, generate() with a
## few of the shape's own knobs perturbed equals a BulletSpawner2D with the
## same knobs whose generator sits at the marker (identity pose). Knob keys
## work with or without the helper_ prefix; a value the spawner refuses is
## refused with the same rule ("BulletPatterns2D.generate: <knob> ...,
## keeping the old value.") and the default stays; unknown params fail loud
## with a did-you-mean while the rest still apply; scene inputs (aim_position,
## path_points, children) are required where the spawner would read the tree.

const MARKER := Transform2D(0.4, Vector2(1.25, 0.8), 0.0, Vector2(300, 200))
const AMOUNT := 13


func _same(a: Array, b: Array, label: String) -> String:
	if a.size() != b.size():
		return "%s: %d vs %d transforms" % [label, a.size(), b.size()]
	for i in a.size():
		var x: Transform2D = a[i]
		var y: Transform2D = b[i]
		if x.origin.distance_to(y.origin) > 0.01 or x.x.distance_to(y.x) > 0.001 or x.y.distance_to(y.y) > 0.001:
			return "%s: slot %d %s vs %s" % [label, i, x, y]
	return ""


func _perturbed(p: Dictionary, v: Variant) -> Variant:
	match typeof(v):
		TYPE_BOOL:
			return not v
		TYPE_INT:
			if int(p.get("hint", 0)) == PROPERTY_HINT_ENUM:
				return (int(v) + 1) % maxi(1, str(p.get("hint_string", "")).split(",").size())
			return int(v) + 1
		TYPE_FLOAT:
			return float(v) * 1.25 + 3.0
		TYPE_VECTOR2:
			return (v as Vector2) * 1.25 + Vector2(3, 2)
	return null


func _claim_errors() -> int:
	var n := 0
	for e in get_errors():
		if not e.handled:
			e.handled = true
			n += 1
	return n


func test_generate_matches_a_spawner_for_every_source() -> void:
	var gen: Node2D = add(Node2D.new())
	gen.transform = MARKER
	var target: Node2D = add(Node2D.new())
	target.position = Vector2(900, -300)
	var path := Path2D.new()
	var curve := Curve2D.new()
	for p in [Vector2(0, 0), Vector2(100, 40), Vector2(200, -20), Vector2(320, 30)]:
		curve.add_point(p)
	path.curve = curve
	path.transform = MARKER
	add(path)
	var customs := [Transform2D(0.3, Vector2(10, 0)), Transform2D(-0.2, Vector2(0, 25)), Transform2D(1.0, Vector2(-15, -5))]
	var problems: Array = []
	for s in BulletPatterns2D.get_shapes():
		var src: int = s["id"]
		var sp := make_spawner(H.make_volley_data(4), src, AMOUNT)
		sp.set_transforms_generator(gen)
		var params := {}
		match src:
			BulletSpawner2D.PATTERN_FROM_HELPER_AIMED, BulletSpawner2D.PATTERN_FROM_HELPER_CORRIDOR:
				sp.set_helper_aimed_target(target)
				params["aim_position"] = target.global_position
			BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM:
				sp.set_helper_custom_transforms(customs)
				params["custom_transforms"] = customs
			BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D:
				# Follow Generator (default): both sides recenter the curve shape.
				sp.set_helper_path2d_node(path)
				params["path_points"] = curve.get_baked_points()
			BulletSpawner2D.PATTERN_FROM_CHILDREN:
				var kids := []
				for i in 3:
					var m := Node2D.new()
					m.position = Vector2(20 * i, -10 * i)
					m.rotation = 0.3 * i
					gen.add_child(m)
					kids.append(m.global_transform)
				params["children"] = kids
		# Perturb up to three of the shape's own knobs the spawner accepts
		# (seeded where random so both sides roll the same layout).
		var pre: String = s["knob_prefix"]
		for p in sp.get_property_list():
			var n := str(p["name"])
			if pre != "" and n.begins_with(pre) and n.ends_with("_seed"):
				sp.set(n, 4242)
				params[n.trim_prefix("helper_")] = 4242
		var perturbed := 0
		for p in sp.get_property_list():
			var n := str(p["name"])
			if pre == "" or not n.begins_with(pre) or n.ends_with("_path") or n.ends_with("_target") or n.ends_with("_seed") or n == "helper_path2d_space" or perturbed >= 3:
				continue
			var nv: Variant = _perturbed(p, sp.get(n))
			if nv == null:
				continue
			sp.set(n, nv)
			if _claim_errors() > 0 or sp.get(n) != nv:
				continue
			params[n.trim_prefix("helper_")] = nv
			perturbed += 1
		var expected: Array = sp.collect_spawn_transforms()
		_claim_errors()
		var got: Array = BulletPatterns2D.generate(src, AMOUNT, MARKER, params)
		if _claim_errors() > 0:
			problems.append("%s: generate pushed errors" % s["name"])
		var diff := _same(got, expected, "%s %s" % [s["name"], params.keys()])
		if diff != "":
			problems.append(diff)
		if src == BulletSpawner2D.PATTERN_FROM_CHILDREN:
			for ch in gen.get_children():
				ch.free()
		sp.free()
	assert_eq("\n".join(problems), "", "generate() matches a spawner for every pattern source")


func test_prefixed_and_short_keys_are_the_same_knob() -> void:
	var a: Array = BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8, MARKER, {"ring_radius": 80.0, "outline_placement": 1, "outline_layer_count": 3})
	var b: Array = BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8, MARKER, {"helper_ring_radius": 80.0, "helper_outline_placement": 1, "helper_outline_layer_count": 3})
	assert_eq(_same(a, b, "ring"), "", "same result")
	var plain: Array = BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8, MARKER)
	assert_ne(_same(a, plain, "ring"), "", "and the knobs really applied")


func test_a_refused_value_keeps_the_default_with_the_spawner_rule() -> void:
	var plain: Array = BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6, MARKER)
	var nan_radius: Array = BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6, MARKER, {"ring_radius": NAN})
	expect_error_sequence(["BulletPatterns2D.generate: helper_ring_radius must be finite, keeping the old value."])
	assert_eq(_same(nan_radius, plain, "nan"), "", "the default radius stayed")
	BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6, MARKER, {"ring_radius": "big", "ring_face_outward": 3})
	expect_error_sequence(["BulletPatterns2D.generate: helper_ring_radius must be a number, keeping the old value.", "BulletPatterns2D.generate: helper_ring_face_outward must be a bool, keeping the old value."])
	assert_eq(BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 0, MARKER), [], "amount below 1")
	expect_error_sequence(["BulletPatterns2D.generate: helper_bullets_amount must be >= 1, keeping the old value."])
	assert_eq(BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 20001, MARKER), [], "amount above the limit")
	expect_error_sequence(["BulletPatterns2D.generate: helper_bullets_amount must be <= 20000, keeping the old value."])


func test_unknown_params_fail_loud_with_a_did_you_mean_and_the_rest_apply() -> void:
	var good: Array = BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6, MARKER, {"ring_radius": 60.0})
	var mixed: Array = BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6, MARKER, {"ring_radius": 60.0, "ring_radiuss": 99.0, "zzz": 1})
	expect_errors_containing("unknown param", 2, "one error per unknown key")
	assert_eq(_same(mixed, good, "mixed"), "", "the valid key still applied")
	BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6, MARKER, {"ring_radiuss": 99.0})
	expect_error_sequence(["BulletPatterns2D.generate: unknown param 'ring_radiuss' (did you mean 'ring_radius'?), ignoring it."])
	BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_AIMED, 6, MARKER, {"aim_positon": Vector2(1, 1)})
	expect_error_sequence(["BulletPatterns2D.generate: unknown param 'aim_positon' (did you mean 'aim_position'?), ignoring it.", "BulletPatterns2D.generate: the Aimed shape needs params.aim_position (a global Vector2), nothing generated."])


func test_scene_inputs_and_shape_ids_are_checked() -> void:
	assert_eq(BulletPatterns2D.generate(99, 6, MARKER), [], "unknown shape")
	expect_error_sequence(["BulletPatterns2D.generate: unknown shape 99 (see BulletPatterns2D.get_shapes()), nothing generated."])
	assert_eq(BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D, 6, MARKER, {"path_points": PackedVector2Array([Vector2.ZERO])}), [], "one point is no path")
	expect_error_sequence(["BulletPatterns2D.generate: the Path2D shape needs params.path_points (at least 2 marker-local points), nothing generated."])
	assert_eq(BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_SELF, 6, MARKER).size(), 1, "From Self is the marker")
	assert_eq(BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_CHILDREN, 6, MARKER, {"children": [Transform2D(), Transform2D(0.0, Vector2(5, 5))]}).size(), 2, "From Children uses the given transforms")


func test_skip_indices_carve_the_result() -> void:
	var full: Array = BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8, MARKER)
	var carved: Array = BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8, MARKER, {"skip_indices": PackedInt32Array([0, 3])})
	assert_eq(carved.size(), 6, "two slots removed")
	assert_eq(carved[0], full[1], "slot 0 skipped")
	assert_eq(carved[2], full[4], "slot 3 skipped")
