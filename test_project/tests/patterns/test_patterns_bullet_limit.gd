extends BlastTest
## The per-call bullet limit is one developer-owned setting, not a baked-in
## constant: BulletPatterns2D.max_bullets_per_pattern (default 20000, project
## setting blastbullets2d/patterns/max_bullets_per_pattern). Every generator,
## the spawner's helper_bullets_amount / helper_custom_transforms, the shape
## counts (star points, polygon vertices, ellipse gaps) and generate() read
## it live, and every rejection names the CURRENT value.

const DEFAULT_LIMIT := 20000
const SETTING := "blastbullets2d/patterns/max_bullets_per_pattern"


func after_each() -> void:
	# The limit is process-wide: never leak a test's value into the next one.
	BulletPatterns2D.set_max_bullets_per_pattern(DEFAULT_LIMIT)
	await super()


func test_limit_binds_exist() -> void:
	assert_true(ClassDB.class_has_method("BulletPatterns2D", "set_max_bullets_per_pattern", true), "setter bound")
	assert_true(ClassDB.class_has_method("BulletPatterns2D", "get_max_bullets_per_pattern", true), "getter bound")


func test_default_limit_is_twenty_thousand() -> void:
	assert_eq(BulletPatterns2D.get_max_bullets_per_pattern(), DEFAULT_LIMIT, "default")


func test_project_setting_is_registered_with_the_default() -> void:
	assert_true(ProjectSettings.has_setting(SETTING), "setting registered")
	assert_eq(ProjectSettings.get_setting(SETTING), DEFAULT_LIMIT, "setting default")


func test_old_ten_thousand_cap_is_gone() -> void:
	var v: Array = BulletPatterns2D.helper_generate_transforms_ring(10001, Transform2D())
	assert_eq(v.size(), 10001, "a generator draws past the old cap")
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.helper_bullets_amount = 20000
	assert_eq(sp.get_helper_bullets_amount(), 20000, "spawner accepts past the old cap")
	assert_eq(sp.collect_spawn_transforms().size(), 20000, "and collects them all")


func test_raising_the_limit_lets_generators_and_spawner_go_higher() -> void:
	BulletPatterns2D.set_max_bullets_per_pattern(250000)
	assert_eq(BulletPatterns2D.get_max_bullets_per_pattern(), 250000, "round trip")
	var v: Array = BulletPatterns2D.helper_generate_transforms_ring(DEFAULT_LIMIT + 1, Transform2D())
	assert_eq(v.size(), DEFAULT_LIMIT + 1, "ring past the old default")
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.helper_bullets_amount = DEFAULT_LIMIT + 1
	assert_eq(sp.get_helper_bullets_amount(), DEFAULT_LIMIT + 1, "spawner follows the raised limit")


func test_lowering_the_limit_rejects_generator_amounts_with_the_new_number() -> void:
	BulletPatterns2D.set_max_bullets_per_pattern(500)
	assert_eq(BulletPatterns2D.helper_generate_transforms_ring(500, Transform2D()).size(), 500, "limit itself ok")
	assert_eq(BulletPatterns2D.helper_generate_transforms_ring(501, Transform2D()).size(), 0, "limit + 1 rejected")
	expect_error_sequence(["helper_generate_transforms_ring: transforms_amount must be between 0 and 500."])


func test_lowering_the_limit_rejects_spawner_amount_and_keeps_the_old_value() -> void:
	BulletPatterns2D.set_max_bullets_per_pattern(500)
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.helper_bullets_amount = 501
	expect_error_sequence(["BulletSpawner2D: helper_bullets_amount must be <= 500, keeping the old value."])
	assert_eq(sp.get_helper_bullets_amount(), 4, "old value kept")
	sp.helper_bullets_amount = 500
	assert_eq(sp.get_helper_bullets_amount(), 500, "limit itself accepted")


func test_lowering_the_limit_rejects_custom_transforms_and_shape_counts() -> void:
	BulletPatterns2D.set_max_bullets_per_pattern(10)
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	var many: Array[Transform2D] = []
	many.resize(11)
	for i in many.size():
		many[i] = Transform2D()
	sp.helper_custom_transforms = many
	expect_error_sequence(["BulletSpawner2D: helper_custom_transforms must hold <= 10 entries, keeping the old value."])
	assert_eq(sp.helper_custom_transforms.size(), 0, "old (empty) array kept")
	sp.helper_star_points = 11
	expect_error_sequence(["BulletSpawner2D: helper_star_points must be between 2 and 10, keeping the old value."])
	sp.helper_polygon_vertices = 11
	expect_error_sequence(["BulletSpawner2D: helper_polygon_vertices must be between 3 and 10, keeping the old value."])


func test_generate_reads_the_live_limit() -> void:
	BulletPatterns2D.set_max_bullets_per_pattern(100)
	assert_eq(BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 101, Transform2D()), [], "101 over a limit of 100")
	expect_error_sequence(["BulletPatterns2D.generate: helper_bullets_amount must be <= 100, keeping the old value."])
	assert_eq(BulletPatterns2D.generate(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 100, Transform2D()).size(), 100, "100 fits")


func test_invalid_limits_are_rejected_and_the_old_value_kept() -> void:
	for bad in [0, -1, -2147483648]:
		BulletPatterns2D.set_max_bullets_per_pattern(bad)
		expect_error_sequence(["BulletPatterns2D: max_bullets_per_pattern must be >= 1, keeping the old value."])
		assert_eq(BulletPatterns2D.get_max_bullets_per_pattern(), DEFAULT_LIMIT, "kept after %d" % bad)
	BulletPatterns2D.set_max_bullets_per_pattern(1)
	assert_eq(BulletPatterns2D.get_max_bullets_per_pattern(), 1, "1 is the smallest legal limit")


func test_lowering_the_limit_invalidates_a_baked_pattern() -> void:
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 600)
	assert_eq(sp.collect_spawn_transforms().size(), 600, "baked under the default limit")
	assert_eq(sp.collect_spawn_transforms().size(), 600, "served again from the bake")
	BulletPatterns2D.set_max_bullets_per_pattern(500)
	var after: Array = sp.collect_spawn_transforms()
	assert_eq(after.size(), 0, "the old bake is not served under a lower limit")
	expect_error_sequence(["helper_generate_transforms_ring: transforms_amount must be between 0 and 500."])
