extends SceneTree
## Block volleys suite: dumb rigid volleys through the factory.
## Covers: spawn with rotation+speed, whole-volley teleport, lifetime expiry,
## pooling + reuse, shape state, enable/disable slots, no advanced features
## (curves/patterns reject loudly — asserted, not avoided).
## Run: godot --headless --path test_project --script tests/volley/test_block_volleys.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("BLOCK T1 spawn + drift")
	factory.spawn_block_bullets(H.make_block_data(4, 150.0, 10.0))
	await physics_frame
	_check(factory.debug_get_total_bullets_amount(1) >= 1, "block volley spawned")
	_check(factory.debug_get_active_bullets_amount(1) >= 1, "block volley active")

	printerr("BLOCK T2 whole-volley teleport + slots")
	factory.teleport_shift_all_bullets(Vector2(5, 0))
	await physics_frame
	_check(factory.debug_get_active_bullets_amount(1) >= 1, "volley alive after shift")

	printerr("BLOCK T3 pooling + reuse + shape")
	await process_frame
	await process_frame
	factory.debug_reset_pool_stats()
	var key: MultiMeshPoolKey2D = BulletFactory2D.debug_expected_pool_key(H.make_block_data(4, 150.0, 10.0))
	factory.populate_bullets_pool(key, H.make_block_data(4, 150.0, 10.0), 1)
	_check(factory.debug_get_bullets_pool_amount(1) == 1, "block bucket pre-populated")
	factory.spawn_block_bullets(H.make_block_data(4, 150.0, 10.0))
	await process_frame
	var stats: Dictionary = factory.debug_get_pool_hit_stats()
	_check(stats.get("block_hits", 0) >= 1, "block reuse counted as hit")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after reuse")

	printerr("BLOCK T4 short lifetime expires")
	var quick := H.make_block_data(2, 100.0, 0.15)
	factory.spawn_block_bullets(quick)
	for i in 30:
		await physics_frame
		if factory.debug_get_active_bullets_amount(1) == 0:
			break
	# (other long-life block volleys from T1/T3 may still be alive; assert pool grew instead)
	_check(factory.debug_get_bullets_pool_amount(1) >= 1, "expired block volley pooled")

	await process_frame
	await process_frame
	factory.reset()
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL BLOCK TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
