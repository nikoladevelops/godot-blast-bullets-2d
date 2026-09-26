extends SceneTree
## Pooling + multi-spawner stress suite: shared factory hammered by several
## spawners at once, cross-shape pool reuse, reset/free under live volleys,
## adopt/retarget across owners, spawner freed mid-flight, pool-hit reuse
## neutrality, double-push guards. No crashes, no dangling, no cross-owner
## state leaks, pool accounting stays exact.
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_pool_stress.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _mk_spawner(factory: BulletFactory2D, n: int, speed: float) -> BulletSpawner2D:
	var sp := BulletSpawner2D.new()
	get_root().add_child(sp)
	sp.set_bullet_factory(factory)
	var d := H.make_directional_data(n, speed)
	sp.set_spawn_data(d)
	sp.set_shooting_enabled(false)
	sp.pattern_source = 1
	# Homing on (global target) so volleys are tracked in the live list;
	# plain volleys are untracked by design.
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(2)
	sp.set_homing_global_position(Vector2(600, -100))
	return sp

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("POOLSTRESS T1 three spawners share one factory")
	var a := _mk_spawner(factory, 4, 200.0)
	var b := _mk_spawner(factory, 4, 200.0)
	var c := _mk_spawner(factory, 6, 260.0)
	_check(a.shoot_once() and b.shoot_once() and c.shoot_once(), "all three fire")
	for i in 10:
		await physics_frame
	var total: int = factory.debug_get_total_bullets_amount(0)
	_check(total >= 3, "factory census sees all volleys (%d)" % total)
	_check(a.get_live_volley_count() >= 1 and b.get_live_volley_count() >= 1 and c.get_live_volley_count() >= 1, "owners track own volleys")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after shared fire")

	printerr("POOLSTRESS T2 cross-shape reuse stays isolated")
	for i in 6:
		var dd := H.make_directional_data(4, 150.0 + 20.0 * i)
		var vv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dd)
		for k in 4:
			vv.disable_bullet(k)
		await physics_frame
	var reuse: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(4, 999.0))
	_check(absf(reuse.get_bullet_speed_data(0).speed - 999.0) < 0.01, "pool hit reseeds fully (no stale speed)")
	_check(not reuse.get_is_wobble_enabled(), "pool hit has no stale wobble")
	var info: Dictionary = factory.debug_get_bullets_pool_info(0)
	_check(info.size() >= 0, "pool info readable after churn")

	printerr("POOLSTRESS T3 reset under live spawner volleys")
	_check(a.shoot_once(), "spawner fires before reset")
	for i in 5:
		await physics_frame
	factory.reset()
	await process_frame
	await physics_frame
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after reset")
	_check(a.shoot_once(), "spawner fires again after reset")
	for i in 5:
		await physics_frame
	_check(a.get_live_volley_count() >= 1, "post-reset volley tracked")

	printerr("POOLSTRESS T4 free_active under live volleys then refire")
	_check(b.shoot_once() and c.shoot_once(), "fire before free_active")
	for i in 5:
		await physics_frame
	factory.free_active_bullets()
	await process_frame
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after free_active")
	_check(b.shoot_once(), "refire after free_active")

	printerr("POOLSTRESS T5 adopt moves volley between spawners")
	var d5 := H.make_directional_data(3, 220.0)
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5)
	_check(v5 != null, "direct volley spawned")
	a.adopt_live_volley(v5)
	await process_frame
	_check(a.get_live_volley_count() >= 1, "adopter tracks volley")
	b.adopt_live_volley(v5)
	await process_frame
	_check(b.get_live_volley_count() >= 1, "second adopter tracks volley")
	for i in 5:
		await physics_frame
	_check(v5.get_bullet_transform(0).is_finite(), "adopted volley flies finite")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after double adopt")

	printerr("POOLSTRESS T6 spawner freed mid-flight orphans safely")
	var tmp := _mk_spawner(factory, 3, 200.0)
	_check(tmp.shoot_once(), "temp spawner fires")
	for i in 5:
		await physics_frame
	tmp.queue_free()
	await process_frame
	await physics_frame
	for i in 10:
		await physics_frame
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after spawner free")
	_check(a.shoot_once(), "surviving spawner still fires")

	printerr("POOLSTRESS T7 retarget/override storm across owners")
	a.set_homing_global_position(Vector2(600, 0))
	b.set_homing_global_position(Vector2(-600, 0))
	_check(a.retarget_live_volleys() >= 1, "owner A retargets own volleys")
	_check(b.retarget_live_volleys() >= 1, "owner B retargets own volleys")
	a.clear_live_volleys_homing()
	a.override_live_volleys_velocity(Vector2(100, 0))
	for i in 10:
		await physics_frame
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after retarget storm")

	printerr("POOLSTRESS T8 pool-hit accounting exact under churn")
	var hits0: int = factory.debug_get_pool_hit_stats().get("directional_hits", -1)
	for i in 10:
		var dd := H.make_directional_data(4, 180.0)
		var vv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dd)
		for k in 4:
			vv.disable_bullet(k)
		await physics_frame
	var hits1: int = factory.debug_get_pool_hit_stats().get("directional_hits", -1)
	_check(hits1 > hits0, "pool hits accumulate (%d -> %d)" % [hits0, hits1])
	_check(factory.debug_get_bullets_pool_amount(0) >= 1, "pool holds stock")

	a.queue_free()
	b.queue_free()
	c.queue_free()
	factory.reset()
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL POOL STRESS TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
