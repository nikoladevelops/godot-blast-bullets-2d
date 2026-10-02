extends BlastTest
## Cross-owner handover: spawner A's volley adopted by B (A forgets, B
## retargets); a foreign pooled wake warns and revives with neutral
## ballistics until enable_multimesh reseeds; free_volley_deferred from inside a
## collision handler never corrupts the sweep.

var sa: BulletSpawner2D
var sb: BulletSpawner2D


func before_each() -> void:
	await super()
	sa = make_spawner(H.make_directional_data(2, 200.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sb = make_spawner(H.make_directional_data(2, 200.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
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
	assert_true(live[0] is DirectionalBullets2D, "live volley resolvable")
	assert_true(sb.adopt_live_volley(live[0]), "B adopts")
	assert_eq(sb.get_live_volley_count(), 1, "B tracks after adopt")
	assert_eq(sa.get_live_volley_count(), 0, "A prunes the re-owned volley")
	assert_gte(sb.retarget_live_volleys(), 1, "B retargets the adopted volley")


func test_foreign_wake_warns_then_reseed_cleans() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	for i in 2:
		v.disable_bullet(i)
	await idle()
	v.wake_bullet(0)
	assert_push_warning("woke a pooled volley", "foreign wake warns")
	assert_true(v.is_bullet_status_enabled(0), "foreign wake revives the slot")
	# Contract fix: a full drain keeps linear ballistics, so a wake resumes
	# them (it used to revive a frozen bullet at speed 0).
	assert_gt(v.get_bullet_speed_data(0).speed, 0.0, "full-drain wake resumes the bullet's speed")
	var p0: Vector2 = v.get_bullet_global_transform(0).origin
	await physics(5)
	assert_gt(v.get_bullet_global_transform(0).origin.distance_to(p0), 1.0, "the woken bullet actually moves")
	for i in 2:
		v.disable_bullet(i)
	await idle()
	assert_true(v.enable_multimesh(H.make_directional_data(2, 777.0), Vector2.ZERO, 0), "reseed via enable_multimesh succeeds")
	assert_almost_eq(v.get_bullet_speed_data(0).speed, 777.0, 0.01, "reseed replaces stale ballistics")


func test_free_volley_deferred_inside_handler() -> void:
	make_area(Vector2(150, 0), Vector2(40, 400), 8)
	await physics()
	var k: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 400.0))
	k.set_bullet_max_collision_count(1)
	var freed := [false]
	var cb := func(_hit: Object, vol: DirectionalBullets2D, _idx: int) -> void:
		if not freed[0]:
			freed[0] = true
			factory.free_volley_deferred(vol)
	factory.directional_area_entered.connect(cb)
	for i in 60:
		await physics()
		if freed[0]:
			break
	assert_true(freed[0], "handler ran the deferred free")
	await idle()
	assert_false(is_instance_valid(k), "volley freed after the sweep")
	factory.directional_area_entered.disconnect(cb)
