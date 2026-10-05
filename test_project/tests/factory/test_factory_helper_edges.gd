extends BlastTest
## Helper hostile input: every helper_generate_transforms_* hammered with
## negative/huge/NaN/Inf/zero inputs and the caps (amount per call,
## grid-product, gap/points/vertices, scale-factor band). Helpers must fail
## loud with empty arrays - never hang, OOM, or emit NaN/Inf slots.

var m := Transform2D(0.0, Vector2.ZERO)


func test_amount_caps() -> void:
	assert_true(BulletPatterns2D.helper_generate_transforms_grid(20000, m, 10).is_empty(), "grid 20k rejected")
	expect_error_sequence(["helper_generate_transforms_grid: transforms_amount must be between 0 and 10000."])
	assert_true(BulletPatterns2D.helper_generate_transforms_ring(20000, m).is_empty(), "ring 20k rejected")
	expect_error_sequence(["helper_generate_transforms_ring: transforms_amount must be between 0 and 10000."])
	assert_true(BulletPatterns2D.helper_generate_transforms_fan(-5, m, 1.0).is_empty(), "fan negative rejected")
	expect_error_sequence(["helper_generate_transforms_fan: transforms_amount must be between 0 and 10000."])
	assert_true(BulletPatterns2D.helper_generate_transforms_line(0, m, Vector2.RIGHT, 10.0).is_empty(), "line 0 returns empty")
	assert_eq(BulletPatterns2D.helper_generate_transforms_circle(1, m).size(), 1, "circle 1 returns one slot")


func test_product_and_count_caps() -> void:
	assert_true(BulletPatterns2D.helper_generate_transforms_waterfall(10, m, 100000, 8.0, 100000, 8.0).is_empty(), "waterfall 100kx100k rejected")
	expect_error_sequence(["helper_generate_transforms_waterfall: columns*rows is absurdly large; keep the grid reasonable."])
	assert_true(BulletPatterns2D.helper_generate_transforms_lattice(10, m, 100000, 100000).is_empty(), "lattice 100kx100k rejected")
	expect_error_sequence(["helper_generate_transforms_lattice: columns*rows is absurdly large; keep the grid reasonable."])
	assert_true(BulletPatterns2D.helper_generate_transforms_star(10, m, 100000).is_empty(), "star 100k points rejected")
	expect_error_sequence(["helper_generate_transforms_star: points is absurdly large; keep it near the bullet count."])
	assert_true(BulletPatterns2D.helper_generate_transforms_polygon(10, m, 100000).is_empty(), "polygon 100k vertices rejected")
	expect_error_sequence(["helper_generate_transforms_polygon: vertices is absurdly large; keep it near the bullet count."])
	assert_true(BulletPatterns2D.helper_generate_transforms_ellipse(10, m, 60.0, 40.0, 0.0, 0.0, TAU, 2, 100000).is_empty(), "ellipse 100k gaps rejected")
	expect_error_sequence(["helper_generate_transforms_ellipse: gap_count is absurdly large; keep it near the bullet count."])


func test_nan_inf_rejected_loud() -> void:
	assert_true(BulletPatterns2D.helper_generate_transforms_grid(4, Transform2D(0.0, Vector2(NAN, 0)), 2).is_empty(), "grid NaN marker rejected")
	expect_error_sequence(["helper_generate_transforms_grid: marker_transform contains NaN/Inf."])
	assert_true(BulletPatterns2D.helper_generate_transforms_ring(8, m, NAN).is_empty(), "ring NaN radius rejected")
	expect_error_sequence(["helper_generate_transforms_ring: radius, start_angle and arc must be finite numbers."])
	assert_true(BulletPatterns2D.helper_generate_transforms_fan(4, m, INF, 0.0).is_empty(), "fan Inf spread rejected")
	expect_error_sequence(["helper_generate_transforms_fan: spread, direction_angle and step_offset must be finite numbers."])
	assert_true(BulletPatterns2D.helper_generate_transforms_line(4, m, Vector2.ZERO, 10.0).is_empty(), "line zero dir rejected")
	expect_error_sequence(["helper_generate_transforms_line: direction must not be zero, the line axis is undefined."])
	assert_true(BulletPatterns2D.helper_generate_transforms_wave(8, m, 100.0, 10.0, 2.0, Vector2(NAN, 0)).is_empty(), "wave NaN dir rejected")
	expect_error_sequence(["helper_generate_transforms_wave: direction must be finite and non-zero."])


func test_huge_finite_inputs_clamp() -> void:
	var far: Array = BulletPatterns2D.helper_generate_transforms_line(8, m, Vector2.RIGHT, 1e30, true)
	assert_eq(far.size(), 8)
	assert_true(H.finite_volley(far), "line 1e30 spacing clamps finite")
	var wide: Array = BulletPatterns2D.helper_generate_transforms_grid(9, m, 3, 0, 1e30, 1e30, false)
	assert_eq(wide.size(), 9)
	assert_true(H.finite_volley(wide), "grid 1e30 offsets clamp finite")
	var big_ring: Array = BulletPatterns2D.helper_generate_transforms_ring(16, m, 1e30)
	assert_eq(big_ring.size(), 16)
	assert_true(H.finite_volley(big_ring), "ring 1e30 radius stays finite")


func test_scale_band_and_skip_guards() -> void:
	assert_false(is_finite(BulletPatterns2D.helper_layer_scale_factor(100000, 0.5, 0, 1, PackedFloat32Array())), "absurd layer count stays Inf (collapse precheck sees it)")
	assert_eq(BulletPatterns2D.helper_layer_scale_factor(-3, 0.5, 0, 1, PackedFloat32Array()), 1.0, "negative layer returns 1")
	var base: Array = BulletPatterns2D.helper_generate_transforms_line(5, m, Vector2.RIGHT, 10.0, true)
	assert_eq(BulletPatterns2D.helper_apply_skip_indices(base, [1, 99, -2]).size(), 4, "skip ignores OOB indices")
	assert_gte(BulletPatterns2D.helper_bullet_layer_index(3, 10, 4, 0, 0), 0, "layer index sane")


## Every shape generator (ClassDB-discovered, so new ones are covered) names
## the amount range and a NaN marker in ONE wording each. Polyline keeps its
## own pinned "must be in 0..10000." (patterns/test_patterns_module.gd).
func _shape_generators() -> Array:
	var names: Array = []
	for info in ClassDB.class_get_method_list(&"BulletPatterns2D", true):
		var n := String(info["name"])
		if n.begins_with("helper_generate_transforms_") and n != "helper_generate_transforms_polyline":
			names.append(info)
	names.sort_custom(func(a, b): return String(a["name"]) < String(b["name"]))
	return names


## Minimal valid call: amount, marker, then a sane value for every required argument.
func _generator_args(info: Dictionary, amount: int, marker: Transform2D) -> Array:
	var args: Array = [amount, marker]
	var required: int = info["args"].size() - info["default_args"].size()
	for i in range(2, required):
		match int(info["args"][i]["type"]):
			TYPE_VECTOR2:
				args.append(Vector2(100, 30))
			TYPE_PACKED_VECTOR2_ARRAY:
				args.append(PackedVector2Array([Vector2(0, 0), Vector2(100, 0), Vector2(100, 80)]))
			_:
				args.append(1)
	return args


func test_every_generator_reports_amount_and_marker_in_one_wording() -> void:
	var gens := _shape_generators()
	assert_eq(gens.size(), 29, "every shape generator discovered (polyline excluded)")
	for info in gens:
		var n := String(info["name"])
		for amount in [-1, 10001]:
			var got: Array = ClassDB.class_call_static.callv([&"BulletPatterns2D", n] + _generator_args(info, amount, m))
			assert_true(got.is_empty(), "%s amount %d returns empty" % [n, amount])
			expect_error_sequence(["%s: transforms_amount must be between 0 and 10000." % n])
		var bad: Array = ClassDB.class_call_static.callv([&"BulletPatterns2D", n] + _generator_args(info, 4, Transform2D(0.0, Vector2(NAN, 0))))
		assert_true(bad.is_empty(), "%s NaN marker returns empty" % n)
		expect_error_sequence(["%s: marker_transform contains NaN/Inf." % n])
