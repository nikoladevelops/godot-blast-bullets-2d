extends BlastTest
## Factory pooling + DX: spawner duplicate cache (swap + in-place edit),
## retarget-phase validation, debugger budget defaults/clamps/cycle, preview
## collision-ring overlay, pre-populated pool hits, size-mismatch misses.

var spawner: BulletSpawner2D


func _data(n: int = 3) -> DirectionalBulletsData2D:
	var data := H.make_directional_data(n, 250.0, 5.0)
	data.texture_size = Vector2(12, 12)
	return data


func before_each() -> void:
	await super()
	spawner = add(BulletSpawner2D.new())
	await idle(1)
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(_data(3))
	spawner.set_shooting_enabled(false)
	spawner.set_homing_enabled(false)
	spawner.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
	spawner.helper_bullets_amount = 4


func test_spawner_duplicate_cache() -> void:
	assert_true(spawner.shoot_once(), "first cached shot fires")
	var fired: int = spawner.get_volleys_fired()
	assert_true(spawner.shoot_once(), "second cached shot fires")
	assert_eq(spawner.get_volleys_fired(), fired + 1, "cached shot counted")
	spawner.set_spawn_data(_data(5))
	assert_true(spawner.shoot_once(), "shot after resource swap fires")
	var d: DirectionalBulletsData2D = spawner.get_spawn_data()
	d.max_life_time = 6.0
	await idle(1)
	assert_true(spawner.shoot_once(), "shot after in-place edit fires")


func test_retarget_phase_validation() -> void:
	var s2: BulletSpawner2D = add(BulletSpawner2D.new())
	s2.set_shooting_enabled(false)
	await idle(1)
	s2.set_homing_retarget_phase(0.0)
	spawner.set_homing_retarget_phase(0.0)
	spawner.get_parent().remove_child(spawner)
	add_child(spawner)
	await idle(1)
	spawner.set_homing_retarget_phase(0.25)
	assert_almost_eq(spawner.get_homing_retarget_phase(), 0.25, 0.0001, "explicit phase respected")
	spawner.set_homing_retarget_phase(-1.0)
	expect_any_error()
	assert_almost_eq(spawner.get_homing_retarget_phase(), 0.25, 0.0001, "negative phase rejected")


func test_debugger_budget_and_preview_rings() -> void:
	assert_eq(factory.get_debugger_max_providers(), 0, "budget default unlimited")
	assert_true(factory.get_debugger_draw_inactive(), "draw inactive default true")
	factory.set_debugger_max_providers(2)
	assert_eq(factory.get_debugger_max_providers(), 2, "budget set")
	factory.set_debugger_max_providers(-5)
	assert_eq(factory.get_debugger_max_providers(), 0, "negative budget clamps to unlimited")
	factory.set_debugger_draw_inactive(false)
	assert_false(factory.get_debugger_draw_inactive(), "draw inactive toggle")
	factory.set_debugger_draw_inactive(true)
	factory.set_is_debugger_enabled(true)
	factory.spawn_directional_bullets(_data(3))
	await physics()
	factory.set_is_debugger_enabled(false)
	assert_false(factory.get_is_debugger_enabled(), "debugger cycle survived")
	spawner.set_preview_draw_collision_rings(true)
	assert_true(spawner.get_preview_draw_collision_rings(), "rings toggle")
	spawner.set_preview_collision_ring_width(2.0)
	assert_almost_eq(spawner.get_preview_collision_ring_width(), 2.0, 0.0001, "ring width set")
	var ring_data := _data(3)
	ring_data.collision_shape = H.make_circle_shape(8.0)
	spawner.set_spawn_data(ring_data)
	spawner.show_preview_during_runtime = true
	spawner.show_pattern_preview = true
	await idle(1)
	assert_gte(spawner.collect_spawn_transforms().size(), 1, "rings preview collects")


func test_pool_hit_observability() -> void:
	factory.debug_reset_pool_stats()
	var pop_data := _data(4)
	factory.populate_bullets_pool(BulletFactory2D.debug_expected_pool_key(pop_data), pop_data, 2)
	assert_eq(factory.debug_get_bullets_pool_amount(0), 2, "pre-populated 2")
	factory.spawn_directional_bullets(pop_data)
	await idle(1)
	assert_gte(factory.debug_get_pool_hit_stats().get("directional_hits", 0), 1, "reuse counted as hit")
	var carved := _data(4)
	carved.transforms = [Transform2D.IDENTITY, Transform2D(0.0, Vector2(10, 0))]
	factory.spawn_directional_bullets(carved)
	swallow_errors()
	await idle(1)
	assert_gte(factory.debug_get_pool_hit_stats().get("directional_misses", 0), 1, "size mismatch counted as miss")
