extends SceneTree
## Spawner muzzle-offset suite: spawn_position_offset used to be silently
## ignored for plain spawners.
##
## The bug: shoot_once() applied the offset only inside
## apply_volley_homing_and_orbiting(), which early-returns when homing and
## orbiting are both off — the most common configuration. The offset must
## apply to every volley, exactly once, before homing/orbit seeding.
##
## Covers: T1 plain spawner shifts every bullet by the offset, T2 homing
## spawner still shifts exactly once (no double nudge), T3 zero offset is a
## no-op, T4 no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_muzzle_offset.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0
var _spawner_vols: Array = []

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_volley(volley: Object, _idx: int) -> void:
	if volley is DirectionalBullets2D:
		_spawner_vols.append(volley)

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	s.speed = 0.0
	s.max_speed = 3000.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = 60.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	d.bullet_max_collision_count = 0
	return d

func _spawner(factory: BulletFactory2D) -> BulletSpawner2D:
	var sp := BulletSpawner2D.new()
	get_root().add_child(sp)
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(_data())
	sp.set_shooting_enabled(false)
	sp.volley_fired.connect(_on_volley)
	return sp

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("MUZZLE T1 plain spawner applies the offset")
	var sp := _spawner(factory)
	sp.spawn_position_offset = Vector2(30, -12)
	_spawner_vols.clear()
	_check(sp.shoot_once(), "T1 shot fires")
	_check(_spawner_vols.size() == 1, "T1 volley observed")
	var v1: DirectionalBullets2D = _spawner_vols[0]
	var p1: Vector2 = v1.get_bullet_transform(0).get_origin()
	var base: Vector2 = sp.global_position
	_check(p1.distance_to(base + Vector2(30, -12)) < 0.5, "T1 bullet sits at spawner + offset (got %s)" % str(p1))
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("MUZZLE T2 homing spawner shifts exactly once")
	sp.homing_enabled = true
	sp.homing_target_source = BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION
	sp.homing_global_position = Vector2(500, 0)
	_spawner_vols.clear()
	_check(sp.shoot_once(), "T2 homing shot fires")
	var v2: DirectionalBullets2D = _spawner_vols[0]
	var p2: Vector2 = v2.get_bullet_transform(0).get_origin()
	_check(p2.distance_to(sp.global_position + Vector2(30, -12)) < 0.5, "T2 single offset under homing (got %s)" % str(p2))
	sp.homing_enabled = false
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("MUZZLE T3 zero offset is a no-op")
	sp.spawn_position_offset = Vector2.ZERO
	_spawner_vols.clear()
	_check(sp.shoot_once(), "T3 shot fires")
	var v3: DirectionalBullets2D = _spawner_vols[0]
	var p3: Vector2 = v3.get_bullet_transform(0).get_origin()
	_check(p3.distance_to(sp.global_position) < 0.5, "T3 bullet on the spawner (got %s)" % str(p3))
	factory.free_active_bullets()
	await process_frame

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	sp.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL MUZZLE-OFFSET TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
