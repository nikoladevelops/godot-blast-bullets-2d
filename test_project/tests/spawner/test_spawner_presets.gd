extends BlastTest
## apply_pattern_preset is CLEAN: every Bullet Patterns knob (except the
## Transform subgroup and node/array wiring) and Spin return to their defaults
## before the preset applies, so the same preset always gives the same
## pattern. Every preset draws exactly its amount.

const PRESET_COUNT := 20


func test_every_preset_draws_its_amount() -> void:
	var bad: Array = []
	var target := Node2D.new() # aimed presets need something to aim at
	target.position = Vector2(400, 0)
	add(target)
	for preset in PRESET_COUNT:
		var sp := make_spawner()
		sp.set_helper_aimed_target(target)
		sp.apply_pattern_preset(preset)
		var got: int = sp.collect_spawn_transforms().size()
		var want: int = sp.helper_custom_transforms.size() if sp.pattern_source == BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM else sp.helper_bullets_amount
		if got != want:
			bad.append("preset %d: %d of %d" % [preset, got, want])
	assert_true(bad.is_empty(), "every preset draws its amount:\n  " + "\n  ".join(bad))


func test_presets_reset_leftovers_but_keep_the_transform_subgroup() -> void:
	var sp := make_spawner()
	sp.helper_outline_placement = BulletPatterns2D.OUTLINE_LAYERS
	sp.helper_ring_facing_offset_deg = 45.0
	sp.helper_ring_y_scale = 0.5
	sp.spin_enabled = true
	sp.pattern_scale = 2.0
	sp.spawn_position_offset = Vector2(10, 0)
	sp.apply_pattern_preset(BulletPatterns2D.PATTERN_PRESET_RADIAL_DENSE)
	assert_eq(sp.helper_outline_placement, BulletPatterns2D.OUTLINE_ON_OUTLINE, "layers mode does not leak")
	assert_eq(sp.helper_ring_facing_offset_deg, 0.0, "facing offset does not leak")
	assert_eq(sp.helper_ring_y_scale, 1.0, "y scale does not leak")
	assert_false(sp.spin_enabled, "spin from an earlier setup does not leak")
	assert_eq(sp.pattern_scale, 2.0, "Transform subgroup kept (pattern_scale)")
	assert_eq(sp.spawn_position_offset, Vector2(10, 0), "Transform subgroup kept (muzzle offset)")


func test_presets_keep_node_wiring() -> void:
	var sp := make_spawner()
	var target := Node2D.new()
	add(target)
	sp.set_helper_aimed_target(target)
	sp.apply_pattern_preset(BulletPatterns2D.PATTERN_PRESET_AIMED_FAN_NARROW)
	assert_eq(sp.get_helper_aimed_target(), target, "the aimed target is wiring, not a pattern knob")


func test_same_preset_same_pattern() -> void:
	var sp := make_spawner()
	sp.apply_pattern_preset(BulletPatterns2D.PATTERN_PRESET_STAR_SHELL)
	var first: Array = sp.collect_spawn_transforms()
	sp.helper_star_inner_radius = 10.0
	sp.helper_star_base_rotation = 1.0
	sp.helper_outline_slot_offset = 3
	sp.apply_pattern_preset(BulletPatterns2D.PATTERN_PRESET_SPIRAL_3ARM)
	sp.apply_pattern_preset(BulletPatterns2D.PATTERN_PRESET_STAR_SHELL)
	var second: Array = sp.collect_spawn_transforms()
	assert_eq(second.size(), first.size(), "same count")
	for i in first.size():
		assert_almost_eq((second[i] as Transform2D).origin.distance_to((first[i] as Transform2D).origin), 0.0, 0.001, "slot %d identical" % i)


# Golden table: [preset, source, amount, spin_enabled, spin_speed]. Presets are
# content users build on, so a silent edit to one is a behavior change.
# TERRAIN_CREST is Custom: its amount is the baked array (120 slots).
const GOLDEN := [
	[BulletPatterns2D.PATTERN_PRESET_RADIAL_DENSE, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 36, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_RADIAL_SPARSE, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 12, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_SPIRAL_3ARM, BulletSpawner2D.PATTERN_FROM_HELPER_MULTISPIRAL, 30, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_AIMED_FAN_NARROW, BulletSpawner2D.PATTERN_FROM_HELPER_AIMED, 5, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_AIMED_FAN_WIDE, BulletSpawner2D.PATTERN_FROM_HELPER_AIMED, 9, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_RING_SLOW, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 24, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_WALL_GAPS, BulletSpawner2D.PATTERN_FROM_HELPER_ELLIPSE, 40, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_RAIN, BulletSpawner2D.PATTERN_FROM_HELPER_RAIN, 24, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_FLOWER_6, BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER, 30, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_SCATTER_BURST, BulletSpawner2D.PATTERN_FROM_HELPER_SCATTER, 26, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_CROSS_BURST, BulletSpawner2D.PATTERN_FROM_HELPER_CROSS, 24, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_STAR_SHELL, BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 20, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_HEART_BLOOM, BulletSpawner2D.PATTERN_FROM_HELPER_HEART, 40, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_SNAKE_WAVE, BulletSpawner2D.PATTERN_FROM_HELPER_WAVE, 28, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_WATERFALL_CURTAIN, BulletSpawner2D.PATTERN_FROM_HELPER_WATERFALL, 36, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_PETAL_STORM, BulletSpawner2D.PATTERN_FROM_HELPER_ROSE, 48, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_TWIN_SPIRAL_COUNTER, BulletSpawner2D.PATTERN_FROM_HELPER_COUNTER_SPIRAL, 40, true, -60.0],
	[BulletPatterns2D.PATTERN_PRESET_AIMED_TRAP, BulletSpawner2D.PATTERN_FROM_HELPER_CORRIDOR, 25, false, 0.0],
	[BulletPatterns2D.PATTERN_PRESET_BLOSSOM_FINALE, BulletSpawner2D.PATTERN_FROM_HELPER_ROSE, 60, true, 30.0],
	[BulletPatterns2D.PATTERN_PRESET_TERRAIN_CREST, BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM, 120, false, 0.0],
]


func test_every_preset_matches_its_golden_source_amount_and_spin() -> void:
	assert_eq(GOLDEN.size(), PRESET_COUNT, "one golden row per preset")
	var bad: Array = []
	for row in GOLDEN:
		var sp := make_spawner()
		sp.apply_pattern_preset(row[0])
		var amount: int = sp.helper_custom_transforms.size() if sp.pattern_source == BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM else sp.helper_bullets_amount
		var got := [sp.pattern_source, amount, sp.spin_enabled, sp.spin_speed_deg_per_sec if sp.spin_enabled else 0.0]
		var want: Array = row.slice(1)
		if got != want:
			bad.append("preset %d: got %s want %s" % [row[0], got, want])
	assert_eq(bad, [], "golden presets")


func test_custom_is_a_no_op_and_out_of_range_fails_loud() -> void:
	var sp := make_spawner(null, BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 7)
	sp.helper_star_points = 6
	sp.apply_pattern_preset(BulletPatterns2D.PATTERN_PRESET_CUSTOM)
	expect_no_errors()
	assert_eq([sp.pattern_source, sp.helper_bullets_amount, sp.helper_star_points], [BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 7, 6], "Custom keeps everything")
	for bad in [BulletPatterns2D.PATTERN_PRESET_CUSTOM - 1, PRESET_COUNT]:
		sp.apply_pattern_preset(bad)
		expect_error_sequence(["BulletSpawner2D::apply_pattern_preset: preset out of range, nothing applied."])
	assert_eq([sp.pattern_source, sp.helper_bullets_amount, sp.helper_star_points], [BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 7, 6], "a rejected preset applies nothing")
