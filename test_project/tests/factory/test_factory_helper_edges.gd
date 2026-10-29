extends BlastTest
## Helper hostile input: every helper_generate_transforms_* hammered with
## negative/huge/NaN/Inf/zero inputs and the caps (amount per call,
## grid-product, gap/points/vertices, scale-factor band). Helpers must fail
## loud with empty arrays - never hang, OOM, or emit NaN/Inf slots.
##
## Singular markers: generators that invert the marker must reject singular
## markers loudly; non-inverting generators degrade to finite zero-size.
##
## A (0, 1)-scale marker passes every finite check but has determinant 0, so
## affine_inverse() (outline layout, Path2D linker) is garbage.

var m := Transform2D(0.0, Vector2.ZERO)
var singular := Transform2D(Vector2(0, 0), Vector2(0, 1), Vector2(100, 50))


func test_amount_caps() -> void:
	assert_true(BulletPatterns2D.helper_generate_transforms_grid(200000, m, 10).is_empty(), "grid 200k rejected")
	expect_error_sequence(["helper_generate_transforms_grid: transforms_amount must be between 0 and 20000."])
	assert_true(BulletPatterns2D.helper_generate_transforms_ring(200000, m).is_empty(), "ring 200k rejected")
	expect_error_sequence(["helper_generate_transforms_ring: transforms_amount must be between 0 and 20000."])
	assert_true(BulletPatterns2D.helper_generate_transforms_fan(-5, m, 1.0).is_empty(), "fan negative rejected")
	expect_error_sequence(["helper_generate_transforms_fan: transforms_amount must be between 0 and 20000."])
	assert_true(BulletPatterns2D.helper_generate_transforms_line(0, m, Vector2.RIGHT, 10.0).is_empty(), "line 0 returns empty")
	assert_eq(BulletPatterns2D.helper_generate_transforms_circle(1, m).size(), 1, "circle 1 returns one slot")


func test_product_and_count_caps() -> void:
	assert_true(BulletPatterns2D.helper_generate_transforms_waterfall(10, m, 100000, 8.0, 100000, 8.0).is_empty(), "waterfall 100kx100k rejected")
	expect_error_sequence(["helper_generate_transforms_waterfall: columns*rows is absurdly large; keep the grid reasonable."])
	assert_true(BulletPatterns2D.helper_generate_transforms_lattice(10, m, 100000, 100000).is_empty(), "lattice 100kx100k rejected")
	expect_error_sequence(["helper_generate_transforms_lattice: columns*rows is absurdly large; keep the grid reasonable."])
	assert_true(BulletPatterns2D.helper_generate_transforms_star(10, m, 1000000).is_empty(), "star 1M points rejected")
	expect_error_sequence(["helper_generate_transforms_star: points is absurdly large; keep it near the bullet count."])
	assert_true(BulletPatterns2D.helper_generate_transforms_polygon(10, m, 1000000).is_empty(), "polygon 1M vertices rejected")
	expect_error_sequence(["helper_generate_transforms_polygon: vertices is absurdly large; keep it near the bullet count."])
	assert_true(BulletPatterns2D.helper_generate_transforms_ellipse(10, m, 60.0, 40.0, 0.0, 0.0, TAU, 2, 1000000).is_empty(), "ellipse 1M gaps rejected")
	expect_error_sequence(["helper_generate_transforms_ellipse: gap_count is absurdly large; keep it near the bullet count."])
	# A FAN flower walks every petal (Fill Inside: 17 points each): 2e9 petals
	# froze for seconds and could run out of memory, in the editor preview too.
	var t0 := Time.get_ticks_msec()
	assert_true(BulletPatterns2D.helper_generate_transforms_flower(10, m, 2000000000).is_empty(), "flower 2e9 petals rejected")
	expect_error_sequence(["helper_generate_transforms_flower: petals is absurdly large; keep it near the bullet count."])
	assert_eq(BulletPatterns2D.helper_generate_transforms_flower(10, m, 20000).size(), 10, "the pattern limit itself is fine")
	BulletPatterns2D.helper_sample_outline_flower(BulletPatterns2D.FLOWER_FAN, 2000000000, 100.0, 0.5, 0.0, 0.0, 0.5, 0.5, 4.0, 1.0, 0.0, -1)
	expect_error_sequence(["helper_sample_outline_flower: petals is absurdly large; keep it near the bullet count."])
	var sp := BulletSpawner2D.new()
	sp.helper_flower_petals = 20001
	expect_error_sequence(["BulletSpawner2D: helper_flower_petals must be <= 20000, keeping the old value."])
	assert_eq(sp.helper_flower_petals, 6, "old value kept")
	sp.helper_flower_petals = 0
	expect_error_sequence(["BulletSpawner2D: helper_flower_petals must be >= 1, keeping the old value."])
	sp.free()
	assert_lt(Time.get_ticks_msec() - t0, 2000, "no freeze")
	# Fill Inside at the limit: the silhouette sort was O(n^2) in atan2 calls
	# (17 points per petal: 20000 petals took about 11 minutes).
	t0 = Time.get_ticks_msec()
	var filled := BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER, 2000, m, {"helper_flower_petals": 20000, "helper_outline_placement": BulletPatterns2D.OUTLINE_FILL_INSIDE})
	assert_eq(filled.size(), 2000, "every bullet placed")
	assert_lt(Time.get_ticks_msec() - t0, time_budget_ms(2000.0), "a Fill Inside flower at the limit stays fast")


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
	expect_warning_sequence(["helper_generate_transforms_line: some slots overflowed to Inf/NaN or left the world box and were clamped to its edge; shrink the spacing/radius."], "the clamp is named")
	var wide: Array = BulletPatterns2D.helper_generate_transforms_grid(9, m, 3, 0, 1e30, 1e30, false)
	assert_eq(wide.size(), 9)
	assert_true(H.finite_volley(wide), "grid 1e30 offsets clamp finite")
	expect_warning_sequence(["helper_generate_transforms_grid: some slots overflowed to Inf/NaN or left the world box and were clamped to its edge; shrink the spacing/radius."], "the clamp is named")
	var big_ring: Array = BulletPatterns2D.helper_generate_transforms_ring(16, m, 1e30)
	assert_eq(big_ring.size(), 16)
	assert_true(H.finite_volley(big_ring), "ring 1e30 radius stays finite")


func test_scale_band_and_skip_guards() -> void:
	assert_false(is_finite(BulletPatterns2D.helper_layer_scale_factor(100000, 0.5, 0, 1, PackedFloat32Array())), "absurd layer count stays Inf (collapse precheck sees it)")
	assert_eq(BulletPatterns2D.helper_layer_scale_factor(-3, 0.5, 0, 1, PackedFloat32Array()), 1.0, "negative layer returns 1")
	var base: Array = BulletPatterns2D.helper_generate_transforms_line(5, m, Vector2.RIGHT, 10.0, true)
	assert_eq(BulletPatterns2D.helper_apply_skip_indices(base, [1, 99, -2]).size(), 4, "skip ignores OOB indices")
	expect_warning_sequence(["helper_apply_skip_indices: skip index out of range, ignoring it."], "one warning per call")
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
		for amount in [-1, 20001]:
			var got: Array = ClassDB.class_call_static.callv([&"BulletPatterns2D", n] + _generator_args(info, amount, m))
			assert_true(got.is_empty(), "%s amount %d returns empty" % [n, amount])
			expect_error_sequence(["%s: transforms_amount must be between 0 and 20000." % n])
		var bad: Array = ClassDB.class_call_static.callv([&"BulletPatterns2D", n] + _generator_args(info, 4, Transform2D(0.0, Vector2(NAN, 0))))
		assert_true(bad.is_empty(), "%s NaN marker returns empty" % n)
		expect_error_sequence(["%s: marker_transform contains NaN/Inf." % n])


func test_singular_marker_is_finite_but_singular() -> void:
	assert_true(singular.is_finite(), "passes finite-only guards")
	assert_eq(singular.determinant(), 0.0)


func test_ring_rejects_singular_marker() -> void:
	var ring = BulletPatterns2D.helper_generate_transforms_ring(12, singular, 60.0, 0.0, TAU, true, false, true)
	assert_eq(ring.size(), 0, "ring volley empty on singular marker")
	expect_error_sequence(["helper_generate_transforms_ring: marker_transform is singular (zero or degenerate scale); volley skipped."])


func test_circle_rejects_singular_marker() -> void:
	var circ = BulletPatterns2D.helper_generate_transforms_circle(12, singular, 60.0, true, 0.0)
	assert_eq(circ.size(), 0, "circle volley empty on singular marker")
	expect_error_sequence(["helper_generate_transforms_circle: marker_transform is singular (zero or degenerate scale); volley skipped."])


func test_grid_degrades_finite_on_singular_marker() -> void:
	var grid = BulletPatterns2D.helper_generate_transforms_grid(6, singular, 3, 4, 32.0, 32.0, true, false, 0.0, 0)
	assert_eq(grid.size(), 6, "grid still emits")
	assert_true(H.finite_volley(grid), "grid slots finite (graceful zero-size)")


func test_sane_marker_unaffected() -> void:
	var ring_ok = BulletPatterns2D.helper_generate_transforms_ring(12, Transform2D(0.3, Vector2(100, 50)), 60.0, 0.0, TAU, true, false, true)
	assert_eq(ring_ok.size(), 12)
	assert_true(H.finite_volley(ring_ok), "ring slots finite")


## One volley is one physics area: past the developer-set
## max_bullets_per_pattern (Godot's broadphase aborts on ~35k clustered
## shapes) every way of making a volley refuses, not only the generators.
func _over_limit_error(caller: String, count: int) -> String:
	return "%s: a volley holds at most 8 bullets (got %d). Raise blastbullets2d/patterns/max_bullets_per_pattern (BulletPatterns2D.max_bullets_per_pattern) for more. Nothing was spawned." % [caller, count]


func test_every_volley_path_refuses_past_the_bullet_limit() -> void:
	BulletPatterns2D.set_max_bullets_per_pattern(8)
	assert_not_null(factory.spawn_volley(H.make_still_data(8)), "the limit itself spawns")
	assert_null(factory.spawn_volley(H.make_still_data(9)), "limit + 1 is refused")
	expect_error_sequence([_over_limit_error("spawn_volley", 9)])
	factory.populate_bullets_pool(VolleyPoolKey2D.make(9, PhysicsServer2D.SHAPE_CIRCLE), H.make_still_data(9), 1)
	expect_error_sequence([_over_limit_error("populate_bullets_pool", 9)])
	var sp := make_spawner(H.make_still_data(1), BulletSpawner2D.PATTERN_FROM_CHILDREN, 1)
	for i in 9:
		var marker := Marker2D.new()
		marker.position = Vector2(10 * i, 0)
		sp.add_child(marker)
	watch_signals(sp)
	assert_false(sp.shoot_once(), "nine children are one bullet too many")
	expect_error_sequence([_over_limit_error("spawn_volley", 9)])
	assert_signal_emitted_with_parameters(sp, "volley_skipped", [&"factory_refused"])
