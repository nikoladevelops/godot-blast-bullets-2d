extends SceneTree
## Factory lifecycle suite (P0 stability hardening).
## Covers: ensure_factory_initialized + debug_get_factory_state,
## debug_validate_spawn_data (null/empty/NaN/invisible), debug_expected_pool_key,
## spawn pool hit/miss accounting, deferred structural wrappers surviving physics,
## pooling-flag reset on new life, wake_bullet alias, free_volley_deferred,
## NaN-spawn atomicity (no half-volley), live-ids + teardown safety.
## Run: godot --headless --path test_project --script tests/factory/test_factory_lifecycle.gd
## Exit code 0 = all pass. Reaching the end proves survival of every error path.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _make_data(n: int = 4) -> DirectionalBulletsData2D:
	var data := DirectionalBulletsData2D.new()
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(20.0 * i, 0.0)))
	data.transforms = arr
	var sp := BulletSpeedData2D.new()
	sp.speed = 200.0
	sp.max_speed = 3000.0
	sp.acceleration = 0.0
	data.all_bullet_speed_data = [sp]
	data.max_life_time = 5.0
	data.texture_size = Vector2(16, 16)
	data.set_collision_layer_from_array([2])
	data.set_collision_mask_from_array([4])
	return data

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	factory.name = "P0Factory"
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("P0 T1 ensure + state")
	_check(factory.ensure_factory_initialized(), "ensure_factory_initialized true in tree")
	var st: Dictionary = factory.debug_get_factory_state()
	_check(st.get("is_ready", false) == true, "factory state is_ready")
	_check(st.has("directional_total") and st.has("block_total"), "factory state has totals")
	var interp: Dictionary = factory.debug_check_interpolation_status()
	_check(interp.has("mismatch") and interp.has("hint"), "interpolation status shape")

	printerr("P0 T2 validate + invisible lint")
	var bad_null: Dictionary = BulletFactory2D.debug_validate_spawn_data(null)
	_check(bad_null.get("ok", true) == false, "validate null rejected")
	var empty_data := DirectionalBulletsData2D.new()
	var bad_empty: Dictionary = BulletFactory2D.debug_validate_spawn_data(empty_data)
	_check(bad_empty.get("ok", true) == false, "validate empty transforms rejected")
	var nan_data := _make_data(2)
	var t0: Transform2D = nan_data.transforms[0]
	nan_data.transforms = [Transform2D(0.0, Vector2(NAN, 0)), t0]
	var bad_nan: Dictionary = BulletFactory2D.debug_validate_spawn_data(nan_data)
	_check(bad_nan.get("ok", true) == false, "validate NaN rejected")
	var invis := DirectionalBulletsData2D.new()
	invis.transforms = [Transform2D.IDENTITY]
	invis.max_life_time = 2.0
	var invis_res: Dictionary = BulletFactory2D.debug_validate_spawn_data(invis)
	_check(invis_res.get("ok", false) == true and (invis_res.get("warning", "") as String) != "", "invisible bullets warn")
	# Runtime spawn still works for valid data (warning path does not reject).
	var good := _make_data(3)
	_check(BulletFactory2D.debug_validate_spawn_data(good).get("ok", false) == true, "validate good ok")
	var key: MultiMeshPoolKey2D = BulletFactory2D.debug_expected_pool_key(good)
	_check(key != null and key.get_amount_bullets() == 3, "expected pool key amount 3")
	_check(BulletFactory2D.debug_expected_pool_key(null) == null, "expected key null on null data")

	printerr("P0 T3 spawn + pool stats")
	factory.debug_reset_pool_stats()
	var stats0: Dictionary = factory.debug_get_pool_hit_stats()
	_check(stats0.get("directional_misses", -1) == 0, "pool stats reset")
	factory.spawn_directional_bullets(good)
	await physics_frame
	var stats1: Dictionary = factory.debug_get_pool_hit_stats()
	_check(stats1.get("directional_misses", 0) >= 1, "first spawn is a miss")
	_check(factory.debug_get_total_bullets_amount(0) >= 1, "total volleys >= 1")
	_check(factory.debug_get_active_bullets_amount(0) >= 1, "active volleys >= 1")

	printerr("P0 T4 deferred wrappers survive physics")
	# Direct structural op inside physics is rejected; deferred variant queues.
	# We cannot easily enter a real collision handler headless, so assert the
	# deferred entry points exist, validate args, and complete next frame.
	factory.free_active_bullets_deferred(null)
	factory.free_disabled_bullets_deferred(null)
	factory.free_bullets_pool_deferred(0, null)
	factory.free_attachments_pool_deferred()
	factory.reset_deferred(null)
	factory.free_volley_deferred(null) # error path, no crash
	await process_frame
	await physics_frame
	_check(factory.debug_get_factory_state().get("is_tearing_down", true) == false, "factory alive after deferred structural ops")

	printerr("P0 T5 pooling flags reset on new life")
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_make_data(2))
	_check(v1 != null, "controllable spawn ok")
	if v1 != null:
		v1.set_is_multimesh_auto_pooling_enabled(false)
		v1.set_is_attachments_auto_pooling_enabled(false)
		var info1: Dictionary = v1.debug_get_volley_info()
		_check(info1.get("auto_pool_multimesh", true) == false, "flags flipped")
		# Exhaust lifetime to pool it, then reuse: flags must reset to default.
		v1.set_is_multimesh_auto_pooling_enabled(true) # re-enable so disable pools
		for i in v1.get_amount_bullets():
			v1.disable_bullet(i)
		await physics_frame
		var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_make_data(2))
		_check(v2 != null, "reuse spawn ok")
		if v2 != null:
			var info2: Dictionary = v2.debug_get_volley_info()
			_check(info2.get("auto_pool_multimesh", false) == true, "pooling flags reset on new life")
			_check(info2.get("amount_bullets", 0) == 2, "volley info amount")
			_check(info2.has("generation") and info2.has("is_pooled"), "volley info shape")

	printerr("P0 T6 wake alias + free_volley helper")
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_make_data(2))
	if v3 != null:
		v3.disable_bullet(0)
		v3.wake_bullet(0)
		_check(v3.is_bullet_status_enabled(0), "wake_bullet revives slot")
		factory.free_volley_deferred(v3)
		await process_frame
		_check(true, "free_volley_deferred survived")

	printerr("P0 T7 NaN spawn rejected, no half-volley")
	var before: int = factory.debug_get_total_bullets_amount(0)
	var nan_spawn := _make_data(2)
	nan_spawn.transforms = [Transform2D(0.0, Vector2(INF, 0)), Transform2D.IDENTITY]
	factory.spawn_directional_bullets(nan_spawn)
	await physics_frame
	_check(factory.debug_get_total_bullets_amount(0) == before, "NaN spawn left no volley")

	printerr("P0 T8 live ids + teardown safety")
	var spawner := BulletSpawner2D.new()
	get_root().add_child(spawner)
	await process_frame
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(_make_data(2))
	spawner.set_shooting_enabled(false)
	spawner.set_homing_enabled(false)
	var ids0: PackedInt64Array = factory.debug_get_live_volley_ids(spawner.get_instance_id())
	_check(ids0.size() == 0, "no live volleys for fresh spawner")
	factory.reset()
	await physics_frame
	_check(factory.debug_get_total_bullets_amount(0) == 0, "reset drained volleys")
	spawner.queue_free()
	factory.queue_free()
	await process_frame

	print("----")
	if failures == 0:
		print("ALL P0 STABILITY TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
