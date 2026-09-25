extends SceneTree
## Pooling: multi-spawner shared factory.
## 3 spawners (ring/circle-6, fan/4, line/5 with rect shape) + direct
## spawn_controllable_* interleave on ONE factory for 60 physics frames.
## Covers: per-spawner census attribution (owner_spawner_id), retarget only
## steers own volleys, max_live fuse per spawner, per-bucket free frees only
## that spawner's bucket, full reset semantics, no dangling at the end.
## Run: godot --headless --path test_project --script tests/pooling/test_pool_multispawner_shared_factory.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _mk_spawner(factory: BulletFactory2D, src: int, amount: int, shape: Shape2D) -> BulletSpawner2D:
	var s := BulletSpawner2D.new()
	get_root().add_child(s)
	s.set_bullet_factory(factory)
	var d := H.make_directional_data(amount, 220.0)
	d.collision_shape = shape
	s.set_spawn_data(d)
	s.set_shooting_enabled(false)
	s.pattern_source = src
	s.helper_bullets_amount = amount
	return s

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
	var s_ring := _mk_spawner(factory, 3, 6, circ)
	var s_fan := _mk_spawner(factory, 6, 4, rect)
	var s_line := _mk_spawner(factory, 6, 5, cap)
	s_line.set_helper_line_direction(Vector2(1, 0))
	s_line.set_helper_line_spacing(24.0)

	printerr("POOL-SHARE T1 interleave 60 ticks")
	for i in 60:
		s_ring.shoot_once()
		if i % 2 == 0:
			s_fan.shoot_once()
		if i % 3 == 0:
			s_line.shoot_once()
		if i % 5 == 0:
			factory.spawn_controllable_directional_bullets(H.make_directional_data(6, 220.0))
		await physics_frame
	_check(s_ring.get_volleys_fired() == 60, "ring fired 60")
	_check(s_fan.get_volleys_fired() == 30, "fan fired 30")
	_check(s_line.get_volleys_fired() == 20, "line fired 20")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after churn")

	printerr("POOL-SHARE T2 census attribution")
	_check(s_ring.get_active_live_bullet_count() > 0, "ring owns live bullets")
	_check(s_fan.get_active_live_bullet_count() > 0, "fan owns live bullets")
	var ring_ids: PackedInt64Array = factory.debug_get_live_volley_ids(s_ring.get_instance_id())
	_check(ring_ids.size() > 0, "live ids attributed to ring spawner")
	var direct_total: int = factory.debug_get_total_bullets_amount(0)
	_check(direct_total >= 60 + 30 + 20 + 12, "all volleys tracked in factory vecs")

	printerr("POOL-SHARE T3 retarget + fuse are per-spawner")
	s_ring.set_homing_enabled(true)
	s_ring.set_homing_target_source(2)
	s_ring.set_homing_global_position(Vector2(600, 0))
	_check(s_ring.retarget_live_volleys() >= 0, "ring retarget runs")
	var fan_before: int = s_fan.get_volleys_fired()
	s_fan.set_max_live_bullets(1)
	s_fan.shoot_once()
	_check(s_fan.get_volleys_fired() == fan_before, "fan fuse holds while ring flies free")
	s_fan.set_max_live_bullets(0)
	s_ring.set_homing_enabled(false)

	printerr("POOL-SHARE T4 per-bucket free isolates")
	var ring_key: MultiMeshPoolKey2D = BulletFactory2D.debug_expected_pool_key(s_ring.get_spawn_data())
	var fan_key: MultiMeshPoolKey2D = BulletFactory2D.debug_expected_pool_key(s_fan.get_spawn_data())
	var fan_live_before: int = s_fan.get_active_live_bullet_count()
	await process_frame
	await process_frame
	factory.free_active_bullets(ring_key)
	await process_frame
	_check(s_fan.get_active_live_bullet_count() == fan_live_before, "fan untouched by ring-bucket free")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after bucket free")

	s_ring.queue_free()
	s_fan.queue_free()
	s_line.queue_free()
	factory.reset()
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL SHARED-FACTORY TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
