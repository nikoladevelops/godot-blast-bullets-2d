extends SceneTree
## Factory pooling + DX suite (P1 perf traps).
## Covers: spawner duplicate cache (swap + in-place edit), retarget-phase
## validation, debugger budget defaults/clamps/cycle, preview collision-ring
## overlay, pre-populate pool hits, skip-carved size misses.
## Run: godot --headless --path test_project --script tests/factory/test_factory_pooling.gd
## Exit code 0 = all pass.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _make_data(n: int = 3) -> DirectionalBulletsData2D:
	var data := DirectionalBulletsData2D.new()
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(30.0 * i, 0.0)))
	data.transforms = arr
	var sp := BulletSpeedData2D.new()
	sp.speed = 250.0
	data.all_bullet_speed_data = [sp]
	data.max_life_time = 5.0
	data.texture_size = Vector2(12, 12)
	return data

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	var spawner := BulletSpawner2D.new()
	get_root().add_child(spawner)
	await process_frame
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(_make_data(3))
	spawner.set_shooting_enabled(false)
	spawner.set_homing_enabled(false)
	spawner.pattern_source = 3 # ring
	spawner.helper_bullets_amount = 4

	printerr("P1 T1 spawner duplicate cache")
	_check(spawner.shoot_once(), "first cached shot fires")
	var f1: int = spawner.get_volleys_fired()
	_check(spawner.shoot_once(), "second cached shot fires")
	_check(spawner.get_volleys_fired() == f1 + 1, "cached shot counted")
	# Swap resource invalidates cache.
	spawner.set_spawn_data(_make_data(5))
	_check(spawner.shoot_once(), "shot after resource swap fires")
	# In-place edit invalidates via changed signal.
	var d: DirectionalBulletsData2D = spawner.get_spawn_data()
	d.max_life_time = 6.0
	await process_frame
	_check(spawner.shoot_once(), "shot after in-place edit fires")

	printerr("P1 T2 retarget stagger deterministic")
	var s2 := BulletSpawner2D.new()
	s2.set_shooting_enabled(false)
	get_root().add_child(s2)
	await process_frame
	# Both default phase 0: _ready staggers by instance id, so two spawners
	# with the same interval must not share the same countdown.
	s2.set_homing_retarget_phase(0.0)
	spawner.set_homing_retarget_phase(0.0)
	# Force re-ready stagger by re-entering tree.
	spawner.get_parent().remove_child(spawner)
	get_root().add_child(spawner)
	await process_frame
	# Cannot read private countdown directly; assert setters hold + no crash.
	spawner.set_homing_retarget_phase(0.25)
	_check(absf(spawner.get_homing_retarget_phase() - 0.25) < 0.0001, "explicit phase respected")
	spawner.set_homing_retarget_phase(-1.0)
	_check(absf(spawner.get_homing_retarget_phase() - 0.25) < 0.0001, "negative phase rejected")
	s2.queue_free()

	printerr("P1 T3 debugger budget defaults preserve behavior")
	_check(factory.get_debugger_max_providers() == 0, "budget default unlimited")
	_check(factory.get_debugger_draw_inactive() == true, "draw inactive default true")
	factory.set_debugger_max_providers(2)
	_check(factory.get_debugger_max_providers() == 2, "budget set")
	factory.set_debugger_max_providers(-5)
	_check(factory.get_debugger_max_providers() == 0, "negative budget clamps to unlimited")
	factory.set_debugger_draw_inactive(false)
	_check(factory.get_debugger_draw_inactive() == false, "draw inactive toggle")
	factory.set_debugger_draw_inactive(true)
	factory.set_debugger_max_providers(0)
	factory.set_is_debugger_enabled(true)
	await physics_frame
	factory.set_is_debugger_enabled(false)
	_check(true, "debugger budget cycle survived")
	# P2 preview collision rings: circle shape + overlay on, no crash.
	spawner.set_preview_draw_collision_rings(true)
	_check(spawner.get_preview_draw_collision_rings() == true, "rings toggle")
	spawner.set_preview_collision_ring_width(2.0)
	_check(absf(spawner.get_preview_collision_ring_width() - 2.0) < 0.0001, "ring width set")
	var ring_data := _make_data(3)
	var circ := CircleShape2D.new()
	circ.radius = 8.0
	ring_data.collision_shape = circ
	spawner.set_spawn_data(ring_data)
	spawner.show_preview_during_runtime = true
	spawner.show_pattern_preview = true
	await process_frame
	_check(spawner.collect_spawn_transforms().size() >= 1, "rings preview collects")

	printerr("P1 T4 pool-hit observability")
	# Structural ops must run outside the physics frame: `await physics_frame`
	# resumes *inside* physics, so park on idle frames first.
	await process_frame
	await process_frame
	factory.debug_reset_pool_stats()
	var pop_data := _make_data(4)
	var pop_key: MultiMeshPoolKey2D = BulletFactory2D.debug_expected_pool_key(pop_data)
	factory.populate_bullets_pool(pop_key, pop_data, 2)
	_check(factory.debug_get_bullets_pool_amount(0) == 2, "pre-populated 2")
	factory.spawn_directional_bullets(pop_data)
	await process_frame
	var stats: Dictionary = factory.debug_get_pool_hit_stats()
	_check(stats.get("directional_hits", 0) >= 1, "reuse counted as hit")
	# Skip-carved size misses the bucket: allocate-new (miss), no crash.
	var carved := _make_data(4)
	carved.transforms = [Transform2D.IDENTITY, Transform2D(0.0, Vector2(10, 0))]
	factory.spawn_directional_bullets(carved)
	await process_frame
	var stats2: Dictionary = factory.debug_get_pool_hit_stats()
	_check(stats2.get("directional_misses", 0) >= 1, "carved size counted as miss")

	factory.reset_deferred(null)
	await process_frame
	await process_frame
	spawner.queue_free()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL P1 DX TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
