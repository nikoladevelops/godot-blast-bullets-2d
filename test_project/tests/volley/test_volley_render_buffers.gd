extends BlastTest
## What the GPU draws must match the simulation: the multimesh instance
## buffer is uploaded in ONE set_buffer at spawn (no per-instance writes), so
## these tests read the multimesh back and compare every instance with the
## bullet transforms - on a cold spawn, a pooled re-spawn, after a tick, and
## for a spawner shot (native transform path).


## Instance i from the raw 2D multimesh buffer ([x.x, y.x, 0, o.x, x.y,
## y.y, 0, o.y] per instance). Read the buffer, not get_instance_transform_2d:
## the headless dummy RenderingServer stores buffers but answers identity for
## per-instance getters.
static func _instance(buf: PackedFloat32Array, i: int) -> Transform2D:
	var b := i * 8
	return Transform2D(Vector2(buf[b], buf[b + 4]), Vector2(buf[b + 1], buf[b + 5]), Vector2(buf[b + 3], buf[b + 7]))


func _assert_buffer_matches(v: DirectionalBullets2D, label: String) -> void:
	var mm: MultiMesh = v.multimesh
	assert_not_null(mm, label + ": multimesh")
	assert_eq(mm.instance_count, v.get_amount_bullets(), label + ": instance count")
	var buf: PackedFloat32Array = mm.buffer
	assert_eq(buf.size(), v.get_amount_bullets() * 8, label + ": 2D transform buffer size")
	var inv := v.get_global_transform().affine_inverse()
	var bad := -1
	for i in v.get_amount_bullets():
		var want: Transform2D = inv * v.get_bullet_global_transform(i)
		var got: Transform2D = _instance(buf, i)
		if got.origin.distance_to(want.origin) > 0.01 or got.x.distance_to(want.x) > 0.001:
			bad = i
			break
	assert_eq(bad, -1, label + ": every instance drawn where its bullet is (first mismatch index)")


func _data() -> DirectionalBulletsData2D:
	var d := H.make_directional_data(64, 120.0, 30.0)
	var arr: Array = []
	for i in 64:
		arr.append(Transform2D(0.1 * i, Vector2(10 * (i % 8), 12 * (i / 8))))
	d.transforms = arr
	return d


func test_cold_spawn_buffer() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	_assert_buffer_matches(v, "cold spawn")


func test_pooled_respawn_buffer() -> void:
	var first: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	await physics(3)
	await idle(1)
	first.clear_all_bullets()
	await idle(2)
	var d := _data()
	var arr: Array = []
	for i in 64:
		arr.append(Transform2D(-0.05 * i, Vector2(400 - 5 * i, 300 + 3 * i)))
	d.transforms = arr
	factory.debug_reset_pool_stats()
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	assert_eq(int(factory.debug_get_pool_hit_stats()["directional_hits"]), 1, "re-spawn is a pool hit")
	_assert_buffer_matches(v, "pooled re-spawn (no stale previous-life poses)")


func test_buffer_after_ticks() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	await physics(5)
	_assert_buffer_matches(v, "after 5 ticks")


func test_spawner_native_shot_buffer() -> void:
	var sp := make_spawner(H.make_directional_data(1, 100.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 50)
	sp.position = Vector2(300, 200)
	sp.rotation = 0.4
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	var v: DirectionalBullets2D = get_signal_parameters(sp, "volley_fired", 0)[0]
	_assert_buffer_matches(v, "spawner native shot")
	var expected: Array = sp.collect_spawn_transforms()
	for i in v.get_amount_bullets():
		assert_almost_eq(v.get_bullet_global_transform(i).origin, (expected[i] as Transform2D).origin, Vector2(0.01, 0.01), "bullet %d spawned at its pattern slot" % i)
