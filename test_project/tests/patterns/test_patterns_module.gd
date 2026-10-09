extends BlastTest
## The pattern library lives in its own class: BulletPatterns2D (static-only,
## abstract) owns every generator, preview track, layout helper, inspector
## and pattern enum; BulletFactory2D exposes none of them anymore. The
## spawner's Path2D mode and BulletPatterns2D.helper_generate_transforms_polyline
## share one polyline layout, so both produce identical transforms for the
## same knobs, and their enum ids mirror each other.

const MOVED := [
	"helper_generate_transforms_ring", "helper_generate_transforms_grid", "helper_generate_transforms_flower",
	"helper_generate_transforms_polygon", "helper_generate_transforms_edge_from_points", "helper_sample_outline_ring",
	"helper_sample_outline_heart", "helper_apply_skip_indices", "helper_layer_scale_factor", "helper_bullet_layer_index",
	"debug_describe_outline", "debug_verify_volley", "debug_volley_gaps", "debug_outline_quotas",
]


func test_patterns_are_a_static_abstract_library() -> void:
	assert_true(ClassDB.class_exists("BulletPatterns2D"), "registered")
	assert_false(ClassDB.can_instantiate("BulletPatterns2D"), "abstract: never instantiated")
	for m in MOVED:
		assert_true(ClassDB.class_has_method("BulletPatterns2D", m), "BulletPatterns2D.%s" % m)
		assert_false(ClassDB.class_has_method("BulletFactory2D", m, true), "the factory no longer exposes %s" % m)
	var ring: Array = BulletPatterns2D.helper_generate_transforms_ring(8, Transform2D())
	assert_eq(ring.size(), 8, "static call works")
	assert_eq(BulletPatterns2D.OUTLINE_LAYERS, 1, "pattern enums live on BulletPatterns2D")
	assert_false(ClassDB.class_has_integer_constant("BulletFactory2D", "OUTLINE_LAYERS"), "and not on the factory")


func test_polyline_enum_ids_mirror_the_path2d_enums() -> void:
	assert_eq(BulletPatterns2D.POLYLINE_DISTRIBUTION_FIXED_SPACING, BulletSpawner2D.PATH2D_DISTRIBUTION_FIXED_SPACING)
	assert_eq(BulletPatterns2D.POLYLINE_DISTRIBUTION_EVEN, BulletSpawner2D.PATH2D_DISTRIBUTION_EVEN)
	assert_eq(BulletPatterns2D.POLYLINE_OVERFLOW_CLAMP, BulletSpawner2D.PATH2D_OVERFLOW_CLAMP)
	assert_eq(BulletPatterns2D.POLYLINE_OVERFLOW_WRAP, BulletSpawner2D.PATH2D_OVERFLOW_WRAP)
	assert_eq(BulletPatterns2D.POLYLINE_OVERFLOW_SHRINK_TO_FIT, BulletSpawner2D.PATH2D_OVERFLOW_SHRINK_TO_FIT)
	assert_eq(BulletPatterns2D.POLYLINE_ANCHOR_START, BulletSpawner2D.PATH2D_ANCHOR_START)
	assert_eq(BulletPatterns2D.POLYLINE_ANCHOR_CENTER, BulletSpawner2D.PATH2D_ANCHOR_CENTER)
	assert_eq(BulletPatterns2D.POLYLINE_ANCHOR_END, BulletSpawner2D.PATH2D_ANCHOR_END)
	assert_eq(BulletPatterns2D.POLYLINE_FACING_ALONG_PATH, BulletSpawner2D.PATH2D_FACING_ALONG_PATH)
	assert_eq(BulletPatterns2D.POLYLINE_FACING_NORMAL_P90, BulletSpawner2D.PATH2D_FACING_NORMAL_P90)
	assert_eq(BulletPatterns2D.POLYLINE_FACING_NORMAL_M90, BulletSpawner2D.PATH2D_FACING_NORMAL_M90)


func _points() -> PackedVector2Array:
	return PackedVector2Array([Vector2(-120, -40), Vector2(-20, 60), Vector2(80, -20), Vector2(140, 50)])


## Path2D spawner in Follow Generator space at the origin: the curve shape is
## re-centered on its bounding box, so the library is fed the same centered
## points to compare like with like.
func _centered(pts: PackedVector2Array) -> PackedVector2Array:
	var mn := pts[0]
	var mx := pts[0]
	for p in pts:
		mn = mn.min(p)
		mx = mx.max(p)
	var c := (mn + mx) * 0.5
	var out := PackedVector2Array()
	for p in pts:
		out.append(p - c)
	return out


func test_polyline_library_matches_the_spawner_path2d_mode() -> void:
	var path := Path2D.new()
	var curve := Curve2D.new()
	for p in _points():
		curve.add_point(p)
	path.curve = curve
	add(path)
	var sp := make_spawner(H.make_volley_data(1), BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D, 7)
	sp.set_helper_path2d_node(path)
	await idle(1)
	var baked: PackedVector2Array = _centered(curve.get_baked_points())
	var combos := 0
	for dist in [0, 1]:
		for overflow in [0, 1, 2]:
			for anchor in [0, 1, 2]:
				for facing in [0, 1, 2]:
					for flags in [[false, false], [true, true]]:
						sp.helper_path2d_distribution = dist
						sp.helper_path2d_overflow = overflow
						sp.helper_path2d_anchor = anchor
						sp.helper_path2d_facing = facing
						sp.helper_path2d_closed = flags[0]
						sp.helper_path2d_reverse = flags[1]
						sp.helper_path2d_spacing = 40.0
						sp.helper_path2d_start_offset = 15.0
						sp.helper_path2d_facing_offset_deg = 10.0
						var from_spawner: Array = sp.collect_spawn_transforms()
						var from_library: Array = BulletPatterns2D.helper_generate_transforms_polyline(7, sp.get_global_transform(), baked, flags[0], dist, 40.0, overflow, anchor, 15.0, flags[1], facing, 10.0)
						assert_eq(from_library.size(), from_spawner.size(), "same count")
						for i in from_spawner.size():
							if not (from_spawner[i] as Transform2D).is_equal_approx(from_library[i]):
								assert_true(false, "slot %d differs for dist %d overflow %d anchor %d facing %d %s" % [i, dist, overflow, anchor, facing, str(flags)])
								return
						combos += 1
	assert_eq(combos, 108, "every knob combination agrees")


func test_polyline_rejects_hostile_input() -> void:
	BulletPatterns2D.helper_generate_transforms_polyline(-1, Transform2D(), _points())
	BulletPatterns2D.helper_generate_transforms_polyline(100001, Transform2D(), _points())
	BulletPatterns2D.helper_generate_transforms_polyline(4, Transform2D(), PackedVector2Array([Vector2(NAN, 0), Vector2(1, 1)]))
	BulletPatterns2D.helper_generate_transforms_polyline(4, Transform2D(), PackedVector2Array([Vector2(1, 1)]))
	expect_error_sequence([
		"helper_generate_transforms_polyline: transforms_amount must be in 0..100000.",
		"helper_generate_transforms_polyline: transforms_amount must be in 0..100000.",
		"helper_generate_transforms_polyline: points contain NaN/Inf.",
		"helper_generate_transforms_polyline: Path2D mode produced no points.",
	])
	assert_eq(BulletPatterns2D.helper_generate_transforms_polyline(0, Transform2D(), _points()).size(), 0, "zero is an empty, valid request")


func test_polyline_even_closed_ring_never_doubles_the_seam() -> void:
	var square := PackedVector2Array([Vector2(-50, -50), Vector2(50, -50), Vector2(50, 50), Vector2(-50, 50)])
	var out: Array = BulletPatterns2D.helper_generate_transforms_polyline(8, Transform2D(), square, true)
	assert_eq(out.size(), 8, "eight slots")
	for i in out.size():
		for j in range(i + 1, out.size()):
			assert_gt((out[i] as Transform2D).origin.distance_to((out[j] as Transform2D).origin), 0.5, "slots %d and %d distinct" % [i, j])
