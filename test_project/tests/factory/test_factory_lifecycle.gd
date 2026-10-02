extends BlastTest
## Factory lifecycle: lazy init + state, spawn-data validation (null/empty/
## NaN/invisible lint), expected pool key, pool hit/miss accounting, deferred
## structural wrappers, pooling-flag reset on a new life, wake alias,
## free_volley_deferred, NaN-spawn atomicity, live ids + reset.


func _data(n: int = 4) -> DirectionalBulletsData2D:
	var data := H.make_directional_data(n, 200.0, 5.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(20.0 * i, 0.0)))
	data.transforms = arr
	return data


func test_ensure_and_state() -> void:
	assert_true(factory.ensure_factory_initialized(), "ensure_factory_initialized true in tree")
	var st: Dictionary = factory.debug_get_factory_state()
	assert_true(st.get("is_ready", false), "factory state is_ready")
	assert_has(st, "directional_total")
	assert_has(st, "block_total")
	var interp: Dictionary = factory.debug_check_interpolation_status()
	assert_has(interp, "mismatch")
	assert_has(interp, "hint")


func test_validate_and_invisible_lint() -> void:
	assert_false(BulletFactory2D.debug_validate_spawn_data(null).get("ok", true), "null rejected")
	assert_false(BulletFactory2D.debug_validate_spawn_data(DirectionalBulletsData2D.new()).get("ok", true), "empty transforms rejected")
	var nan_data := _data(2)
	nan_data.transforms = [Transform2D(0.0, Vector2(NAN, 0)), nan_data.transforms[1]]
	assert_false(BulletFactory2D.debug_validate_spawn_data(nan_data).get("ok", true), "NaN rejected")
	expect_any_error()
	var invis := DirectionalBulletsData2D.new()
	invis.transforms = [Transform2D.IDENTITY]
	invis.max_life_time = 2.0
	var invis_res: Dictionary = BulletFactory2D.debug_validate_spawn_data(invis)
	assert_true(invis_res.get("ok", false), "invisible data still valid")
	assert_ne(invis_res.get("warning", ""), "", "invisible bullets warn")
	var good := _data(3)
	assert_true(BulletFactory2D.debug_validate_spawn_data(good).get("ok", false), "good data ok")
	var key: MultiMeshPoolKey2D = BulletFactory2D.debug_expected_pool_key(good)
	assert_not_null(key)
	assert_eq(key.get_amount_bullets(), 3, "expected pool key amount")
	assert_null(BulletFactory2D.debug_expected_pool_key(null), "expected key null on null data")
	expect_any_error()


func test_spawn_pool_stats() -> void:
	factory.debug_reset_pool_stats()
	assert_eq(factory.debug_get_pool_hit_stats().get("directional_misses", -1), 0, "pool stats reset")
	factory.spawn_directional_bullets(_data(3))
	await physics()
	assert_gte(factory.debug_get_pool_hit_stats().get("directional_misses", 0), 1, "first spawn is a miss")
	assert_gte(factory.debug_get_total_bullets_amount(0), 1, "total volleys")
	assert_gte(factory.debug_get_active_bullets_amount(0), 1, "active volleys")


func test_deferred_wrappers_survive_physics() -> void:
	factory.spawn_directional_bullets(_data(2))
	await physics()
	factory.free_active_bullets_deferred(null)
	factory.free_disabled_bullets_deferred(null)
	factory.free_bullets_pool_deferred(0, null)
	factory.free_attachments_pool_deferred()
	factory.reset_deferred(null)
	factory.free_volley_deferred(null)
	expect_error("volley is null")
	await idle()
	await physics()
	assert_false(factory.debug_get_factory_state().get("is_tearing_down", true), "factory alive after deferred structural ops")
	assert_eq(factory.debug_get_active_bullets_amount(0), 0, "deferred free/reset ran")


func test_pooling_flags_reset_on_new_life() -> void:
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(2))
	assert_not_null(v1)
	v1.set_is_multimesh_auto_pooling_enabled(false)
	v1.set_is_attachments_auto_pooling_enabled(false)
	assert_false(v1.debug_get_volley_info().get("auto_pool_multimesh", true), "flags flipped")
	v1.set_is_multimesh_auto_pooling_enabled(true)
	for i in v1.get_amount_bullets():
		v1.disable_bullet(i)
	await physics()
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(2))
	assert_not_null(v2)
	var info2: Dictionary = v2.debug_get_volley_info()
	assert_true(info2.get("auto_pool_multimesh", false), "pooling flags reset on new life")
	assert_eq(info2.get("amount_bullets", 0), 2, "volley info amount")
	assert_has(info2, "generation")
	assert_has(info2, "is_pooled")


func test_wake_alias_and_free_volley_deferred() -> void:
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(2))
	v3.disable_bullet(0)
	v3.wake_bullet(0)
	assert_true(v3.is_bullet_status_enabled(0), "wake_bullet revives slot")
	factory.free_volley_deferred(v3)
	assert_true(v3.is_queued_for_deletion(), "volley queued for deletion")
	await idle()
	assert_false(is_instance_valid(v3), "volley freed at the flush")


func test_nan_spawn_is_atomic() -> void:
	var before: int = factory.debug_get_total_bullets_amount(0)
	var nan_spawn := _data(2)
	nan_spawn.transforms = [Transform2D(0.0, Vector2(INF, 0)), Transform2D.IDENTITY]
	factory.spawn_directional_bullets(nan_spawn)
	expect_any_error()
	await physics()
	assert_eq(factory.debug_get_total_bullets_amount(0), before, "NaN spawn left no volley")


func test_live_ids_and_reset() -> void:
	var spawner := BulletSpawner2D.new()
	spawner.set_shooting_enabled(false)
	spawner.set_homing_enabled(false)
	add(spawner)
	await idle(1)
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(_data(2))
	assert_eq(factory.debug_get_live_volley_ids(spawner.get_instance_id()).size(), 0, "no live volleys for fresh spawner")
	factory.spawn_directional_bullets(_data(2))
	await idle()
	factory.reset()
	await physics()
	assert_eq(factory.debug_get_total_bullets_amount(0), 0, "reset drained volleys")
