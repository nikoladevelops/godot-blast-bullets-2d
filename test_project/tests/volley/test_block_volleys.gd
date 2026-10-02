extends BlastTest
## Rigid block volleys through the factory: spawn + drift, whole-volley
## teleport, pre-populated pool reuse, short lifetime expiry pools, block data
## exposes no curves/pattern surface.


func test_spawn_drift_and_teleport() -> void:
	factory.spawn_block_bullets(H.make_block_data(4, 150.0, 10.0))
	await physics()
	assert_eq(factory.debug_get_total_bullets_amount(1), 1, "block volley spawned")
	assert_eq(factory.debug_get_active_bullets_amount(1), 1, "block volley active")
	factory.teleport_shift_all_bullets(Vector2(5, 0))
	await physics()
	assert_eq(factory.debug_get_active_bullets_amount(1), 1, "volley alive after the shift")


func test_pool_reuse() -> void:
	factory.debug_reset_pool_stats()
	var key: MultiMeshPoolKey2D = BulletFactory2D.debug_expected_pool_key(H.make_block_data(4, 150.0, 10.0))
	factory.populate_bullets_pool(key, H.make_block_data(4, 150.0, 10.0), 1)
	assert_eq(factory.debug_get_bullets_pool_amount(1), 1, "block bucket pre-populated")
	factory.spawn_block_bullets(H.make_block_data(4, 150.0, 10.0))
	assert_eq(factory.debug_get_pool_hit_stats().get("block_hits", 0), 1, "block reuse counted as a hit")


func test_short_lifetime_expires_and_pools() -> void:
	factory.spawn_block_bullets(H.make_block_data(2, 100.0, 0.15))
	for i in 30:
		await physics()
		if factory.debug_get_active_bullets_amount(1) == 0:
			break
	assert_eq(factory.debug_get_active_bullets_amount(1), 0, "expired")
	await idle(1)
	assert_eq(factory.debug_get_bullets_pool_amount(1), 1, "expired block volley pooled")


func test_block_data_has_no_advanced_surface() -> void:
	var bd := H.make_block_data(2, 100.0, 5.0)
	assert_false("all_bullet_curves_data" in bd, "no curves array")
	assert_false("shared_movement_pattern_path" in bd, "no pattern path")
	factory.spawn_block_bullets(bd)
	await physics()
	assert_eq(factory.debug_get_active_bullets_amount(1), 1, "block spawn alive")
	var dir_probe: DirectionalBullets2D = spawn_dir(2, 100.0)
	dir_probe.set_bullet_movement_pattern_from_curve(0, null)
	assert_false(dir_probe.has_bullet_movement_pattern(0), "null pattern clears cleanly")
	assert_true(dir_probe.get_bullet_transform(0).is_finite(), "post-clear flight finite")
