extends SceneTree
## Rotation-presence-clear suite: clearing rotation data must clear the
## presence bits with it.
##
## The bug: clear_bullet_rotation_data() emptied the speed/max/accel vectors
## but left has_per_bullet_rotation_data stale. A later set_bullet_rotation_data
## on the emptied vectors then assigned zeros everywhere while the stale bits
## survived, so a subsequent shared fallback skipped those slots as if they
## held authored entries. (Calling set_bullet_rotation_data on empty vectors
## without a prior clear was always correct and stays so.)
##
## Covers: T1 clear + per-bullet zero + shared fills the other slot, T2 the
## explicit zero still wins over shared, T3 clear alone + shared full-seeds,
## T4 no dangling.
##
## Run: godot --headless --path test_project --script tests/volley/test_volley_rotation_presence_clear.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _rot(speed: float) -> BulletRotationData2D:
	var r := BulletRotationData2D.new()
	r.rotation_speed = speed
	r.max_rotation_speed = 100.0
	r.rotation_acceleration = 0.0
	return r

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2(16, 0))]
	var speeds: Array = []
	for i in 2:
		var s := BulletSpeedData2D.new()
		s.speed = 0.0
		s.max_speed = 3000.0
		speeds.append(s)
	d.all_bullet_speed_data = speeds
	d.max_life_time = 60.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	d.bullet_max_collision_count = 0
	var rots: Array = [_rot(0.0), _rot(9.0)]
	d.all_bullet_rotation_data = rots
	return d

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("ROTCLR T1 cleared bits let shared fill genuine gaps")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v.clear_bullet_rotation_data()
	v.set_bullet_rotation_data(0, _rot(0.0))
	v.set_shared_bullet_rotation_data(_rot(7.0))
	_check(absf(v.bullet_get_rotation_speed(1) - 7.0) < 0.01, "T1 gap slot filled by shared (got %.2f)" % v.bullet_get_rotation_speed(1))

	# ---------------------------------------------------------------
	printerr("ROTCLR T2 explicit zero still wins over shared")
	_check(absf(v.bullet_get_rotation_speed(0)) < 0.01, "T2 authored zero survives (got %.2f)" % v.bullet_get_rotation_speed(0))

	# ---------------------------------------------------------------
	printerr("ROTCLR T3 clear alone + shared full-seeds")
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v3.clear_bullet_rotation_data()
	v3.set_shared_bullet_rotation_data(_rot(5.0))
	_check(absf(v3.bullet_get_rotation_speed(0) - 5.0) < 0.01, "T3 slot 0 seeded (got %.2f)" % v3.bullet_get_rotation_speed(0))
	_check(absf(v3.bullet_get_rotation_speed(1) - 5.0) < 0.01, "T3 slot 1 seeded (got %.2f)" % v3.bullet_get_rotation_speed(1))

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL ROTATION-PRESENCE-CLEAR TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
