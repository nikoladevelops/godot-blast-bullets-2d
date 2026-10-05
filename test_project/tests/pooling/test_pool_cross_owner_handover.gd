extends BlastTest
## Cross-owner handover: spawner A's volley adopted by B (A forgets, B
## retargets); a foreign pooled wake warns and revives with neutral
## ballistics until enable_volley reseeds; free_volley_deferred from inside a
## collision handler never corrupts the sweep.

var sa: BulletSpawner2D
var sb: BulletSpawner2D


func before_each() -> void:
	await super()
	sa = make_spawner(H.make_volley_data(2, 200.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sb = make_spawner(H.make_volley_data(2, 200.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	for s in [sa, sb]:
		s.set_homing_enabled(true)
		s.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
		s.set_homing_global_position(Vector2(400, 0))
	await idle(1)


func test_adopt_moves_ownership() -> void:
	assert_true(sa.shoot_once(), "A shoots a tracked volley")
	assert_eq(sa.get_live_volley_count(), 1, "A tracks it")
	var live: Array = sa.get_live_volleys()
	assert_eq(live.size(), 1)
	assert_true(live[0] is BulletVolley2D, "live volley resolvable")
	assert_true(sb.adopt_live_volley(live[0]), "B adopts")
	assert_eq(sb.get_live_volley_count(), 1, "B tracks after adopt")
	assert_eq(sa.get_live_volley_count(), 0, "A prunes the re-owned volley")
	assert_eq(sb.retarget_live_volleys(), 1, "B retargets the adopted volley")


func test_pooled_wake_is_refused_and_a_parked_wake_resumes() -> void:
	# Auto-pooling ON: the drained volley is released into the pool, so a
	# later wake through the old handle is refused (stale handle).
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 200.0))
	for i in 2:
		v.disable_bullet(i)
	await idle()
	assert_true(v.is_pooled(), "drained with pooling on: pooled")
	v.wake_bullet(0)
	expect_error_sequence(["enable_bullet: this volley went back to the pool when its last bullet died (is_auto_pooling_enabled was on), so this handle is stale. Spawn a new volley, or set is_auto_pooling_enabled = false before the last bullet dies to keep (park) the volley."])
	assert_false(v.is_bullet_status_enabled(0), "refused: the slot stays dead")
	assert_true(v.is_pooled(), "still pooled")
	# Auto-pooling OFF: the drained volley is parked (frozen, still owned),
	# and a wake resumes it exactly (ballistics kept, it moves on).
	var p: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 200.0))
	p.set_is_auto_pooling_enabled(false)
	for i in 2:
		p.disable_bullet(i)
	await idle()
	assert_true(p.is_parked(), "drained with pooling off: parked")
	p.wake_bullet(0)
	assert_true(p.is_bullet_status_enabled(0), "parked wake revives the slot")
	assert_almost_eq(p.get_bullet_speed_data(0).speed, 200.0, 0.01, "frozen ballistics resume")
	var p0: Vector2 = p.get_bullet_global_transform(0).origin
	await physics(5)
	assert_gt(p.get_bullet_global_transform(0).origin.distance_to(p0), 1.0, "the woken bullet actually moves")
	for i in 2:
		p.disable_bullet(i)
	await idle()
	assert_true(p.enable_volley(H.make_volley_data(2, 777.0), Vector2.ZERO, 0), "reseed via enable_volley succeeds on a parked volley")
	assert_almost_eq(p.get_bullet_speed_data(0).speed, 777.0, 0.01, "reseed replaces the frozen ballistics")


func test_free_volley_deferred_inside_handler() -> void:
	make_area(Vector2(150, 0), Vector2(40, 400), 8)
	await physics()
	var k: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 400.0))
	k.set_bullet_max_collision_count(1)
	var freed := [false]
	var cb := func(_hit: Object, vol: BulletVolley2D, _idx: int) -> void:
		if not freed[0]:
			freed[0] = true
			factory.free_volley_deferred(vol)
	factory.area_entered.connect(cb)
	for i in 60:
		await physics()
		if freed[0]:
			break
	assert_true(freed[0], "handler ran the deferred free")
	await idle()
	assert_false(is_instance_valid(k), "volley freed after the sweep")
	factory.area_entered.disconnect(cb)
