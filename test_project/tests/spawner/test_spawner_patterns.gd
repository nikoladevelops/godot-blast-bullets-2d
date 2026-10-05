extends BlastTest
## Every PatternSource collects a sane volley (finite, non-empty unless it
## needs a target/input), invalid sources and the 10k cap reject, custom
## order ops (reverse, slot offset), spin + scales compose, skip indices carve,
## presets apply in one call, aimed needs a target.

var sp: BulletSpawner2D
const EMPTY_WITHOUT_INPUT := [
	BulletSpawner2D.PATTERN_FROM_HELPER_AIMED,
	BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM,
	BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D,
]


func before_each() -> void:
	await super()
	sp = make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6)


func test_all_sources_collect(src: int = use_parameters(range(BulletSpawner2D.PATTERN_FROM_LAST))) -> void:
	sp.pattern_source = src
	var tf: Array = sp.collect_spawn_transforms()
	if src in EMPTY_WITHOUT_INPUT:
		assert_true(tf.is_empty(), "source %d without input is empty" % src)
		var why := {
			BulletSpawner2D.PATTERN_FROM_HELPER_AIMED: "no aimed target assigned",
			BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM: "helper_custom_transforms is empty",
			BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D: "Path2D mode has no node assigned",
		}
		expect_error_sequence([why[src]], "source %d fails loud without input" % src)
	else:
		# Children/Self: one marker (no children here); generators: the amount.
		var want := 1 if src in [BulletSpawner2D.PATTERN_FROM_CHILDREN, BulletSpawner2D.PATTERN_FROM_SELF] else 6
		assert_eq(tf.size(), want, "source %d emits exactly %d" % [src, want])
		assert_true(H.finite_volley(tf), "source %d finite" % src)


func test_invalid_source_and_amount_cap() -> void:
	var src0: int = sp.get_pattern_source()
	sp.pattern_source = 99
	expect_error_sequence(["invalid pattern_source, keeping the old value"])
	assert_eq(sp.get_pattern_source(), src0, "source 99 rejected")
	sp.helper_bullets_amount = 10000
	assert_eq(sp.get_helper_bullets_amount(), 10000, "cap accepts 10000")
	sp.helper_bullets_amount = 10001
	expect_error_sequence(["helper_bullets_amount must be <= 10000, keeping the old value"])
	assert_eq(sp.get_helper_bullets_amount(), 10000, "10001 rejected")


func test_custom_order_ops() -> void:
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM
	sp.set_helper_custom_transforms([Transform2D(0.0, Vector2(10, 0)), Transform2D(0.0, Vector2(0, 20)), Transform2D(0.0, Vector2(-5, -5))])
	assert_eq(sp.collect_spawn_transforms().size(), 3, "custom collects all")
	sp.set_helper_custom_reverse(true)
	var rev: Array = sp.collect_spawn_transforms()
	assert_almost_eq((rev[0] as Transform2D).origin, Vector2(-5, -5), Vector2(0.01, 0.01), "reverse flips the order")
	sp.set_helper_custom_reverse(false)
	sp.set_helper_custom_slot_offset(1)
	var off: Array = sp.collect_spawn_transforms()
	assert_almost_eq((off[0] as Transform2D).origin, Vector2(0, 20), Vector2(0.01, 0.01), "slot offset rotates the order")


func test_spin_and_scales_compose() -> void:
	sp.helper_bullets_amount = 5
	var plain: Array = sp.collect_spawn_transforms()
	sp.set_spin_enabled(true)
	sp.set_spin_speed_deg_per_sec(360.0)
	await idle(15)
	assert_gt(sp.get_spin_angle_deg(), 1.0, "spin advances the angle")
	var spun: Array = sp.collect_spawn_transforms()
	assert_eq(spun.size(), plain.size(), "spin keeps the count")
	var moved := false
	for i in plain.size():
		if ((plain[i] as Transform2D).origin - (spun[i] as Transform2D).origin).length() > 1.0:
			moved = true
	assert_true(moved, "spin moves origins")
	sp.set_spin_enabled(false)
	sp.reset_spin_angle()
	assert_eq(sp.get_spin_angle_deg(), 0.0, "spin reset")
	sp.set_pattern_scale(2.0)
	assert_eq(sp.collect_spawn_transforms().size(), plain.size(), "scale keeps the count")


func test_skip_indices_and_presets() -> void:
	sp.helper_bullets_amount = 5
	sp.helper_skip_indices = PackedInt32Array([0, 2])
	assert_eq(sp.collect_spawn_transforms().size(), 3, "skip carves 2 of 5")
	sp.helper_skip_indices = PackedInt32Array()
	sp.apply_pattern_preset(0)
	assert_eq(sp.get_pattern_source(), BulletSpawner2D.PATTERN_FROM_HELPER_RING, "radial-dense preset selects ring")
	assert_eq(sp.collect_spawn_transforms().size(), 36, "preset amount applied")
	sp.apply_pattern_preset(19)
	assert_eq(sp.collect_spawn_transforms().size(), 120, "terrain crest collects its 120 baked slots")


func test_aimed_needs_target() -> void:
	var tgt: Node2D = add(Node2D.new())
	tgt.position = Vector2(300, 0)
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_AIMED
	sp.helper_bullets_amount = 5
	sp.set_helper_aimed_target(tgt)
	assert_eq(sp.collect_spawn_transforms().size(), 5, "aimed collects with a target")
	sp.set_helper_aimed_target(null)
	assert_true(sp.collect_spawn_transforms().is_empty(), "aimed empty without a target")
	expect_error("no aimed target")
