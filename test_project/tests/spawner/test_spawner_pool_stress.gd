extends BlastTest
## Shared-factory stress: three spawners fire together, cross-bucket reuse
## reseeds fully, reset / free_active under live volleys, adopt chains move
## volleys between owners, a spawner freed mid-flight orphans safely, a
## retarget/override storm stays sane, pool-hit accounting is exact.

var a: BulletSpawner2D
var b: BulletSpawner2D
var c: BulletSpawner2D


func _mk(n: int, speed: float) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(n, speed), BulletSpawner2D.PATTERN_FROM_SELF, n)
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	sp.set_homing_global_position(Vector2(600, -100))
	return sp


func before_each() -> void:
	await super()
	a = _mk(4, 200.0)
	b = _mk(4, 200.0)
	c = _mk(6, 260.0)
	await idle(1)


func test_three_spawners_share_one_factory() -> void:
	assert_true(a.shoot_once() and b.shoot_once() and c.shoot_once(), "all three fire")
	await physics(10)
	assert_gte(factory.debug_get_total_bullets_amount(), 3, "factory census sees all volleys")
	for sp in [a, b, c]:
		assert_gte(sp.get_live_volley_count(), 1, "owner tracks its own volley")


func test_cross_bucket_reuse_reseeds() -> void:
	for i in 6:
		var vv: BulletVolley2D = factory.spawn_volley(H.make_volley_data(4, 150.0 + 20.0 * i))
		for k in 4:
			vv.disable_bullet(k)
		await physics()
	var reuse: BulletVolley2D = factory.spawn_volley(H.make_volley_data(4, 999.0))
	assert_almost_eq(reuse.get_bullet_speed_data(0).speed, 999.0, 0.01, "pool hit reseeds fully")
	assert_false(reuse.get_is_wobble_enabled(), "no stale wobble")


func test_reset_and_free_under_live_volleys() -> void:
	assert_true(a.shoot_once())
	await physics(5)
	await idle(1)
	factory.reset()
	await idle(1)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after reset")
	assert_true(a.shoot_once(), "fires again after reset")
	await physics(5)
	assert_gte(a.get_live_volley_count(), 1, "post-reset volley tracked")
	assert_true(b.shoot_once() and c.shoot_once())
	await physics(5)
	await idle(1)
	factory.free_active_bullets()
	await idle(1)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after free_active")
	assert_true(b.shoot_once(), "refire after free_active")


func test_adopt_chain() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(3, 220.0))
	assert_true(a.adopt_live_volley(v), "first adopt")
	assert_eq(a.get_live_volley_count(), 1)
	assert_true(b.adopt_live_volley(v), "second adopt")
	assert_eq(b.get_live_volley_count(), 1, "second adopter tracks the volley")
	assert_eq(a.get_live_volley_count(), 0, "first adopter pruned it")
	await physics(5)
	assert_true(v.get_bullet_transform(0).is_finite(), "adopted volley flies finite")


func test_spawner_freed_mid_flight() -> void:
	var tmp := _mk(3, 200.0)
	assert_true(tmp.shoot_once())
	await physics(5)
	tmp.queue_free()
	await idle(1)
	await physics(10)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after the spawner was freed")
	assert_true(a.shoot_once(), "surviving spawner still fires")


func test_retarget_override_storm() -> void:
	assert_true(a.shoot_once() and b.shoot_once())
	a.set_homing_global_position(Vector2(600, 0))
	b.set_homing_global_position(Vector2(-600, 0))
	assert_gte(a.retarget_live_volleys(), 1, "A retargets its own volleys")
	assert_gte(b.retarget_live_volleys(), 1, "B retargets its own volleys")
	assert_eq(a.clear_live_volleys_homing(), 1)
	assert_eq(a.override_live_volleys_velocity(Vector2(100, 0)), 1)
	await physics(10)
	assert_true(_live_finite(a), "A's volley flies finite after the storm")


func _live_finite(sp: BulletSpawner2D) -> bool:
	for v in sp.get_live_volleys():
		if not (v as BulletVolley2D).get_bullet_transform(0).is_finite():
			return false
	return true


func test_pool_hit_accounting() -> void:
	var hits0: int = factory.debug_get_pool_hit_stats().get("hits", -1)
	for i in 10:
		var vv: BulletVolley2D = factory.spawn_volley(H.make_volley_data(4, 180.0))
		for k in 4:
			vv.disable_bullet(k)
		await physics()
	assert_eq(int(factory.debug_get_pool_hit_stats().get("hits", -1)), hits0 + 9, "every spawn after the first is a pool hit")
	assert_gte(factory.debug_get_bullets_pool_amount(), 1, "pool holds stock")
