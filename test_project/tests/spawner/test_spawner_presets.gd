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
