extends BlastTest
## Integration: interpolation status + live toggle, factory pause freezes
## motion, churn (spawner freed with tracked volleys), two independent
## factories in one tree.


func test_interpolation_status_and_toggle() -> void:
	var st: Dictionary = factory.debug_check_interpolation_status()
	assert_has(st, "mismatch")
	assert_has(st, "hint")
	factory.physics_interpolation = BulletFactory2D.INTERPOLATION_ON
	await idle(1)
	assert_true(factory.debug_check_interpolation_status().get("factory_enabled", false), "factory flag on")
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 200.0))
	await physics(5)
	assert_true(v.get_bullet_transform(0).is_finite(), "volley ticks with interpolation on")
	await idle(1)
	factory.physics_interpolation = BulletFactory2D.INTERPOLATION_OFF
	assert_false(factory.debug_check_interpolation_status().get("factory_enabled", true), "factory flag off")


func test_pause_freezes_motion() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 200.0))
	await physics(2)
	var p0: Vector2 = v.get_bullet_transform(0).origin
	factory.set_is_factory_processing_bullets(false)
	await physics(10)
	assert_almost_eq(v.get_bullet_transform(0).origin, p0, Vector2(0.5, 0.5), "paused volley frozen")
	factory.set_is_factory_processing_bullets(true)
	await physics(2)
	assert_gt(v.get_bullet_transform(0).origin.x, p0.x, "resumed volley moves again")


func test_spawner_freed_with_tracked_volleys() -> void:
	var spawner := make_spawner(H.make_volley_data(2), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	spawner.set_homing_enabled(true)
	spawner.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	spawner.set_homing_global_position(Vector2(400, 0))
	await idle(1)
	assert_true(spawner.shoot_once(), "tracked shot fires")
	spawner.queue_free()
	await idle(1)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after the spawner was freed")
	await physics(3)
	assert_eq(factory.debug_get_active_volleys_amount(), 1, "orphaned volley keeps flying")


func test_two_factories_are_independent() -> void:
	var f2: BulletFactory2D = add(BulletFactory2D.new())
	await idle()
	f2.spawn_volley(H.make_volley_data(2, 150.0))
	assert_eq(f2.debug_get_total_bullets_amount(), 1, "second factory holds its own volley")
	assert_eq(factory.debug_get_total_bullets_amount(), 0, "first factory untouched")
	f2.reset()


func test_turning_interpolation_on_at_runtime_starts_the_render_pass() -> void:
	# The factory started with interpolation off (its render pass asleep):
	# switching it on mid-game must wake the pass, or bullets stay drawn at
	# their physics poses.
	assert_false(factory.is_processing(), "the render pass sleeps while interpolation is off")
	factory.physics_interpolation = BulletFactory2D.INTERPOLATION_ON
	assert_true(factory.is_processing(), "switched on: the render pass runs")


func test_follow_project_tracks_the_scene_tree_at_runtime() -> void:
	assert_eq(factory.physics_interpolation, BulletFactory2D.INTERPOLATION_FOLLOW_PROJECT, "the default follows the project")
	assert_eq([BulletFactory2D.INTERPOLATION_OFF, BulletFactory2D.INTERPOLATION_ON, BulletFactory2D.INTERPOLATION_FOLLOW_PROJECT], [0, 1, 2], "serialized ids")
	var tree := get_tree()
	var was: bool = tree.physics_interpolation
	tree.physics_interpolation = false
	await physics(1)
	assert_false(factory.is_physics_interpolation_active(), "the tree is off: so are the bullets")
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 600.0))
	await physics(3)
	tree.physics_interpolation = true
	await physics(1)
	assert_true(factory.is_physics_interpolation_active(), "switched on in the tree: the factory follows within a frame")
	assert_true(factory.is_processing(), "and its render pass runs")
	assert_lt(v.debug_get_previous_origin(0).distance_to(v.get_bullet_transform(0).origin), 600.0 / 60.0 + 0.01, "the previous pose was seeded (no lerp from the spawn point)")
	factory.physics_interpolation = BulletFactory2D.INTERPOLATION_OFF
	assert_false(factory.is_physics_interpolation_active(), "Off wins over the tree")
	tree.physics_interpolation = false
	factory.physics_interpolation = BulletFactory2D.INTERPOLATION_ON
	assert_true(factory.is_physics_interpolation_active(), "On wins over the tree")
	factory.physics_interpolation = 7
	expect_error_sequence(["BulletFactory2D: physics_interpolation must be 0 (Off), 1 (On) or 2 (Follow Project), keeping the old value."])
	assert_eq(factory.physics_interpolation, BulletFactory2D.INTERPOLATION_ON, "kept")
	tree.physics_interpolation = was
