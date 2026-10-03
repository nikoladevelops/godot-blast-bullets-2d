extends BlastTest
## Misconfiguration UX + muzzle offset space:
## - spawn_position_offset_space GLOBAL (default, compat) vs LOCAL (turns
##   with the generator), and the preview draws the offset exactly where
##   the bullets spawn (spin included);
## - editor configuration warnings list every blocking misconfiguration;
## - an auto-firing misconfigured spawner reports ONCE, not every interval
##   (manual shoot_once calls always report), re-armed by a config change.


func _first_bullet(sp: BulletSpawner2D) -> Vector2:
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	var v: BulletVolley2D = get_signal_parameters(sp, "volley_fired", get_signal_emit_count(sp, "volley_fired") - 1)[0]
	return v.get_bullet_global_transform(0).origin


func test_offset_space_global_vs_local() -> void:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.global_position = Vector2(100, 100)
	sp.rotation = PI / 2.0
	sp.spawn_position_offset = Vector2(20, 0)
	assert_eq(sp.spawn_position_offset_space, BulletSpawner2D.SPAWN_OFFSET_GLOBAL, "default stays GLOBAL (scene compatibility)")
	assert_almost_eq(_first_bullet(sp), Vector2(120, 100), Vector2(0.01, 0.01), "GLOBAL: world-space shift")
	sp.spawn_position_offset_space = BulletSpawner2D.SPAWN_OFFSET_LOCAL
	assert_almost_eq(_first_bullet(sp), Vector2(100, 120), Vector2(0.01, 0.01), "LOCAL: the muzzle turns with the spawner")
	sp.spawn_position_offset_space = 5
	expect_error_sequence(["spawn_position_offset_space must be 0 (Global) or 1 (Local)"])
	assert_eq(sp.spawn_position_offset_space, BulletSpawner2D.SPAWN_OFFSET_LOCAL, "invalid space rejected")


func test_preview_draws_the_offset(space: int = use_parameters([0, 1])) -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 10)
	sp.position = Vector2(400, 300)
	sp.rotation = 0.7
	sp.spawn_position_offset = Vector2(30, -12)
	sp.spawn_position_offset_space = space
	sp.spin_enabled = true
	sp.spin_speed_deg_per_sec = 45.0
	await idle(12)
	var layer: Node2D = sp.get_node("~BlastBulletsPatternPreview/Dots")
	var dots: PackedVector2Array = sp.debug_get_preview_dot_points()
	watch_signals(sp)
	assert_true(sp.shoot_once())
	var v: BulletVolley2D = get_signal_parameters(sp, "volley_fired", 0)[0]
	for i in dots.size():
		assert_almost_eq(layer.get_global_transform() * dots[i], v.get_bullet_global_transform(i).origin, Vector2(0.05, 0.05), "space %d: dot %d drawn where bullet %d spawned" % [space, i, i])


func test_configuration_warnings() -> void:
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	add(sp)
	var w: PackedStringArray = sp.get_setup_warnings()
	assert_true(_has(w, "No BulletFactory2D assigned"), "missing factory listed")
	assert_true(_has(w, "No spawn_data"), "missing spawn data listed")
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(H.make_volley_data(2))
	sp.movement_enabled = true
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_AIMED
	w = sp.get_setup_warnings()
	assert_false(_has(w, "No BulletFactory2D"), "factory fixed")
	assert_false(_has(w, "No spawn_data"), "spawn data fixed")
	assert_true(_has(w, "movement_path is empty"), "movement without a path listed")
	assert_true(_has(w, "Aimed pattern needs helper_aimed_target"), "aimed without a target listed")
	var bare := H.make_volley_data(1)
	bare.sprite_frames = null
	sp.set_spawn_data(bare)
	assert_true(_has(sp.get_setup_warnings(), "bullets will be invisible"), "invisible bullets listed")


func _has(w: PackedStringArray, needle: String) -> bool:
	for s in w:
		if s.contains(needle):
			return true
	return false


func test_auto_fire_reports_a_misconfiguration_once() -> void:
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.shoot_interval_sec = 0.05
	add(sp)
	sp.set_bullet_factory(factory)
	sp.set_shooting_enabled(true) # no spawn_data: every interval fails
	await idle(30) # ~10 attempts
	assert_eq(expect_errors_containing("shoot_once: no spawn_data assigned", 1, "", true), 1, "auto-fire: one report, not one per interval") # lint: at-least the count is asserted exactly by assert_eq
	sp.set_spawn_data(null) # configuration touched: re-armed
	await idle(10)
	expect_errors_containing("shoot_once: no spawn_data assigned", 1, "re-armed after a config change")
	sp.set_shooting_enabled(false)
	sp.shoot_once()
	sp.shoot_once()
	expect_errors_containing("shoot_once: no spawn_data assigned", 2, "manual calls always report")
