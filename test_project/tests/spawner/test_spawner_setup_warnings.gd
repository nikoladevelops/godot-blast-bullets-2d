extends BlastTest
## get_setup_warnings() (the editor's warning icon) names every
## misconfiguration that would make shots fail or a feature silently do
## nothing - and is empty for a correctly wired spawner.


func _has(sp: BulletSpawner2D, text: String) -> bool:
	return Array(sp.get_setup_warnings()).any(func(w): return text in str(w))


func test_wired_spawner_has_no_warnings() -> void:
	var sp := make_spawner()
	assert_eq(Array(sp.get_setup_warnings()), [], "nothing to fix")


func test_each_misconfiguration_is_named() -> void:
	var sp := make_spawner()
	var wrong := Node2D.new()
	add(wrong)
	sp.set_bullet_factory(null)
	assert_true(_has(sp, "No BulletFactory2D assigned"), "missing factory")
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(null)
	assert_true(_has(sp, "No spawn_data"), "missing spawn data")
	var invisible := BulletVolleyData2D.new()
	sp.set_spawn_data(invisible)
	assert_true(_has(sp, "bullets will be invisible"), "spawn data without art")
	sp.set_spawn_data(H.make_volley_data(2))
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_AIMED
	assert_true(_has(sp, "Aimed pattern needs helper_aimed_target"), "aimed without a target")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_CORRIDOR
	assert_true(_has(sp, "Corridor pattern aims at helper_aimed_target"), "corridor without a target")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D
	assert_true(_has(sp, "Path2D pattern needs helper_path2d_path"), "path pattern without a path")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM
	assert_true(_has(sp, "Custom pattern has no helper_custom_transforms"), "empty custom pattern")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
	sp.movement_enabled = true
	assert_true(_has(sp, "movement_path is empty"), "movement without a path")
	sp.movement_enabled = false
	sp.set_orbiting_enabled(true)
	assert_true(_has(sp, "orbiting_enabled needs homing_enabled"), "orbiting without homing")
	sp.set_orbiting_enabled(false)
	assert_eq(Array(sp.get_setup_warnings()), [], "clean again")
