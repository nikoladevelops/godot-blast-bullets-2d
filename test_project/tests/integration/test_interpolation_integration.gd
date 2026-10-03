extends BlastTest
## Integration: interpolation status + live toggle, factory pause freezes
## motion, churn (spawner freed with tracked volleys), two independent
## factories in one tree.


func test_interpolation_status_and_toggle() -> void:
	var st: Dictionary = factory.debug_check_interpolation_status()
	assert_has(st, "mismatch")
	assert_has(st, "hint")
	factory.set_use_physics_interpolation_editor(true)
	await idle(1)
	assert_true(factory.debug_check_interpolation_status().get("factory_enabled", false), "factory flag on")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	await physics(5)
	assert_true(v.get_bullet_transform(0).is_finite(), "volley ticks with interpolation on")
	await idle(1)
	factory.set_use_physics_interpolation_editor(false)
	assert_false(factory.debug_check_interpolation_status().get("factory_enabled", true), "factory flag off")


func test_pause_freezes_motion() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	await physics(2)
	var p0: Vector2 = v.get_bullet_global_transform(0).origin
	factory.set_is_factory_processing_bullets(false)
	await physics(10)
	assert_almost_eq(v.get_bullet_global_transform(0).origin, p0, Vector2(0.5, 0.5), "paused volley frozen")
	factory.set_is_factory_processing_bullets(true)
	await physics(2)
	assert_gt(v.get_bullet_global_transform(0).origin.x, p0.x, "resumed volley moves again")


func test_spawner_freed_with_tracked_volleys() -> void:
	var spawner := make_spawner(H.make_directional_data(2), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	spawner.set_homing_enabled(true)
	spawner.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	spawner.set_homing_global_position(Vector2(400, 0))
	await idle(1)
	assert_true(spawner.shoot_once(), "tracked shot fires")
	spawner.queue_free()
	await idle(1)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after the spawner was freed")
	await physics(3)
	assert_eq(factory.debug_get_active_bullets_amount(), 1, "orphaned volley keeps flying")


func test_two_factories_are_independent() -> void:
	var f2: BulletFactory2D = add(BulletFactory2D.new())
	await idle()
	f2.spawn_directional_bullets(H.make_directional_data(2, 150.0))
	assert_eq(f2.debug_get_total_bullets_amount(), 1, "second factory holds its own volley")
	assert_eq(factory.debug_get_total_bullets_amount(), 0, "first factory untouched")
	f2.reset()
