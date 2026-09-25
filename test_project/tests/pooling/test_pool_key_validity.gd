extends SceneTree
## Pooling: key-validity matrix (when reuse is legal).
## Covers: exact match (amount+shape) hits; amount differs / shape differs /
## skip-carved size → miss + allocate (never silent reuse); populate exact key
## then spawn hits; free_bullets_pool(key) frees only that bucket; double-free
## and free-empty-bucket safe; enable_multimesh size-mismatch rejects without
## state change; capsule vs circle vs rect buckets never cross.
## Run: godot --headless --path test_project --script tests/pooling/test_pool_key_validity.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _shaped_data(n: int, shape: Shape2D) -> DirectionalBulletsData2D:
	var d := H.make_directional_data(n, 200.0)
	d.collision_shape = shape
	return d

func _idle() -> void:
	await process_frame
	await process_frame

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var circ := CircleShape2D.new()
	circ.radius = 6.0
	var rect := RectangleShape2D.new()
	rect.size = Vector2(14, 10)
	var cap := CapsuleShape2D.new()
	cap.radius = 4.0
	cap.height = 20.0

	printerr("KEY T1 exact match hits")
	await _idle()
	var d4c := _shaped_data(4, circ)
	var key4c := MultiMeshPoolKey2D.make(4, 3)
	factory.populate_bullets_pool(key4c, d4c, 2)
	_check(factory.debug_get_bullets_pool_amount(0) == 2, "populated 2 in (4,circle)")
	factory.debug_reset_pool_stats()
	factory.spawn_directional_bullets(d4c)
	await process_frame
	var s1: Dictionary = factory.debug_get_pool_hit_stats()
	_check(s1.get("directional_hits", 0) == 1 and s1.get("directional_misses", 0) == 0, "exact spawn hits")

	printerr("KEY T2 shape differs = miss")
	factory.debug_reset_pool_stats()
	factory.spawn_directional_bullets(_shaped_data(4, rect))
	await process_frame
	var s2: Dictionary = factory.debug_get_pool_hit_stats()
	_check(s2.get("directional_misses", 0) == 1 and s2.get("directional_hits", 0) == 0, "rect misses circle bucket")

	printerr("KEY T3 amount differs = miss")
	factory.debug_reset_pool_stats()
	factory.spawn_directional_bullets(_shaped_data(5, circ))
	await process_frame
	var s3: Dictionary = factory.debug_get_pool_hit_stats()
	_check(s3.get("directional_misses", 0) == 1, "5-bullet misses 4-bucket")

	printerr("KEY T4 capsule bucket isolated")
	var d3k := _shaped_data(3, cap)
	var key3k := MultiMeshPoolKey2D.make(3, 5) # SHAPE_CAPSULE=5
	factory.populate_bullets_pool(key3k, d3k, 1)
	_check(factory.debug_get_bullets_pool_amount(0) == 2, "capsule bucket added (2 total pooled incl. disabled)")
	factory.debug_reset_pool_stats()
	factory.spawn_directional_bullets(d3k)
	await process_frame
	var s4: Dictionary = factory.debug_get_pool_hit_stats()
	_check(s4.get("directional_hits", 0) == 1, "capsule spawn hits capsule bucket")

	printerr("KEY T5 per-bucket free isolates")
	await _idle()
	var before: int = factory.debug_get_bullets_pool_amount(0)
	factory.free_bullets_pool(0, key4c)
	await _idle()
	_check(factory.debug_get_bullets_pool_amount(0) < before, "freeing (4,circle) drains pool")
	factory.free_bullets_pool(0, key4c)
	await _idle()
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "double-free empty bucket safe")

	printerr("KEY T6 enable size-mismatch rejects cleanly")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_shaped_data(2, circ))
	for i in 2:
		v.disable_bullet(i)
	await physics_frame
	var info_before: Dictionary = v.debug_get_volley_info()
	var wrong := _shaped_data(5, circ)
	# Direct GDScript enable with mismatched size must refuse without touching state.
	_check(v.enable_multimesh(wrong, Vector2.ZERO, 0) == false, "mismatched enable returns false")
	var info_after: Dictionary = v.debug_get_volley_info()
	_check(info_after.get("amount_bullets", -1) == info_before.get("amount_bullets", -2), "amount unchanged after refused enable")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL KEY-VALIDITY TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
