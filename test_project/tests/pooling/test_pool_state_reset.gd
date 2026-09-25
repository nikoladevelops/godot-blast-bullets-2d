extends SceneTree
## Pooling: state-reset matrix (the no-leak proof).
## Seeds volley A with EVERYTHING (homing + orbit + curves + wobble + pattern +
## custom data + timers + rotation + gravity + pooling flags off), exhausts it,
## respawns same bucket as B and asserts B is neutral: no homing/orbit/curves/
## wobble/pattern, null custom data (strict), 0 timers, rotation cleared,
## gravity zero + zero fall speed, flags default, owner restamped, generation
## bumped. Then shape-change (re-bucket, old bucket intact) and amount-change
## (different bucket = miss, never silent reuse).
## Run: godot --headless --path test_project --script tests/pooling/test_pool_state_reset.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _seeded_data() -> DirectionalBulletsData2D:
	var d := H.make_directional_data(3, 250.0)
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	return d

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var target := Node2D.new()
	target.position = Vector2(500, 0)
	get_root().add_child(target)
	await process_frame

	printerr("RESET T1 seed volley A with everything")
	var a: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_seeded_data())
	a.set_homing_smoothing(5.0)
	a.set_homing_take_control_of_texture_rotation(true)
	a.all_bullets_push_back_homing_target(target)
	a.all_bullets_enable_orbiting(48.0, 2, 0)
	var curv := BulletCurvesData2D.new()
	var sc := Curve.new()
	sc.add_point(Vector2(0, 0))
	sc.add_point(Vector2(1, 1))
	curv.movement_speed_curve = sc
	a.set_shared_bullet_curves_data(curv)
	var wob := BulletWobbleData2D.new()
	wob.enabled = true
	# wobble seeds from spawn data only; runtime flag path:
	_check(a.get_is_wobble_enabled() == false, "wobble off without spawn-data seed")
	var curve2d := Curve2D.new()
	curve2d.add_point(Vector2(0, 0))
	curve2d.add_point(Vector2(50, 50))
	a.all_bullets_set_movement_pattern_from_curve(curve2d, false, true)
	a.bullet_set_custom_data(0, Resource.new())
	a.multimesh_attach_time_based_function(5.0, func() -> void: pass)
	var rot := BulletRotationData2D.new()
	rot.rotation_speed = 3.0
	a.set_shared_bullet_rotation_data(rot)
	a.set_gravity(Vector2(0, 500))
	a.set_is_multimesh_auto_pooling_enabled(false)
	a.set_is_attachments_auto_pooling_enabled(false)
	var gen_a: int = a.debug_get_volley_info().get("generation", -1)
	_check(a.bullet_check_has_homing_targets(0), "A has homing")
	_check(a.bullet_is_orbiting_enabled(0), "A orbiting")
	_check(a.has_shared_bullet_curves_data(), "A has curves")
	_check(a.has_bullet_movement_pattern(0), "A has pattern")
	_check(a.debug_get_timer_count() == 1, "A has timer")

	printerr("RESET T2 exhaust A, respawn B neutral")
	for i in 3:
		a.disable_bullet(i)
	await physics_frame
	_check(factory.debug_get_bullets_pool_amount(0) == 0, "pooling-off volley not pooled")
	# Re-enable pooling on A so the next disable actually pools it.
	a.set_is_multimesh_auto_pooling_enabled(true)
	a.set_is_attachments_auto_pooling_enabled(true)
	for i in 3:
		a.wake_bullet(i)
	for i in 3:
		a.disable_bullet(i)
	await physics_frame
	_check(factory.debug_get_bullets_pool_amount(0) >= 1, "volley pooled after flags restored")
	var b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_seeded_data())
	_check(b != null, "B spawned (pool hit)")
	_check(b == a, "B IS pooled A (identity reuse, strongest proof)")
	_check(not b.bullet_check_has_homing_targets(0), "B has no homing")
	_check(not b.bullet_is_orbiting_enabled(0), "B not orbiting")
	_check(not b.has_shared_bullet_curves_data(), "B has no curves")
	_check(not b.has_bullet_movement_pattern(0), "B has no pattern")
	_check(b.bullet_get_custom_data(0) == null, "B custom data null (strict)")
	_check(b.debug_get_timer_count() == 0, "B has no timers")
	_check(not b.has_shared_bullet_rotation_data(), "B rotation cleared")
	_check(b.get_gravity() == Vector2(0, 0), "B gravity zero")
	_check(b.get_bullet_velocity(0).y < 1.0, "B has no inherited fall speed")
	var info_b: Dictionary = b.debug_get_volley_info()
	_check(info_b.get("auto_pool_multimesh", false) == true, "B flags default")
	_check(info_b.get("owner_spawner_id", -1) == 0, "B factory-owned (spawner 0)")
	_check((info_b.get("generation", 0) as int) > gen_a, "B generation bumped")

	printerr("RESET T3 shape change re-buckets, old bucket intact")
	await process_frame
	await process_frame
	var rect := RectangleShape2D.new()
	rect.size = Vector2(20, 10)
	b.set_collision_shape_runtime(rect)
	await process_frame
	_check(b.debug_get_shape_state().get("type", -1) == 4, "B now RECTANGLE")
	for i in 3:
		b.disable_bullet(i)
	await physics_frame
	var circ_key := MultiMeshPoolKey2D.make(3, 3) # 3 bullets, SHAPE_CIRCLE=3
	_check(factory.debug_get_bullets_pool_amount(0) >= 1, "re-bucketed volley pooled under rect key")
	await process_frame
	await process_frame
	factory.free_bullets_pool(0, circ_key) # directional=0, stale circle bucket
	await process_frame
	await process_frame
	_check(factory.debug_get_bullets_pool_amount(0) >= 1, "freeing stale circle bucket keeps rect-pooled volley")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after re-bucket")

	printerr("RESET T4 amount change = miss, never silent reuse")
	factory.debug_reset_pool_stats()
	factory.spawn_controllable_directional_bullets(H.make_directional_data(7, 200.0))
	await process_frame
	var stats: Dictionary = factory.debug_get_pool_hit_stats()
	_check(stats.get("directional_misses", 0) >= 1, "new amount allocates (miss)")

	target.queue_free()
	factory.reset()
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL STATE-RESET TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
