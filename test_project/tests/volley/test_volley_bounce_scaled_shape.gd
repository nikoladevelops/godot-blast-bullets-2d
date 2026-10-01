extends SceneTree
## Bounce scaled-shape suite: a singular CollisionShape2D must fall back to
## the radial normal instead of inverting garbage.
##
## The gap: the precise-normal path gated the shape node's global transform
## on finite-only. A (0, 1)-scaled shape passes finite checks while its
## affine_inverse() is garbage, warping the contact normal. Now the path
## requires an invertible shape transform and otherwise returns false, so
## the caller falls back to the radial normal.
##
## Covers: T1 singular-scaled shape still bounces (radial fallback), T2
## post-bounce velocity finite and separating, T3 sane shape bounces
## precisely, T4 no dangling.
##
## Run: godot --headless --path test_project --script tests/volley/test_volley_bounce_scaled_shape.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2(100, 300))]
	var s := BulletSpeedData2D.new()
	s.speed = 200.0
	s.max_speed = 3000.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = 30.0
	d.texture_size = Vector2(16, 16)
	d.monitorable = true
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([4])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	d.bullet_max_collision_count = 0
	d.set_bounce_mask_from_array([4])
	d.bounce_mode = 1 # precise: exercises the shape-normal path
	return d

func _wall(shape_scale: Vector2) -> StaticBody2D:
	var wall := StaticBody2D.new()
	wall.position = Vector2(250, 300)
	wall.collision_layer = 8
	wall.collision_mask = 2
	var col := CollisionShape2D.new()
	var circ := CircleShape2D.new()
	circ.radius = 12.0
	col.shape = circ
	col.scale = shape_scale
	wall.add_child(col)
	get_root().add_child(wall)
	return wall

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("SCALESHAPE T1 zero-scaled shape bounces finite via fallback")
	# A (0, 1) shape still reports the overlap: the precise path rejects the
	# singular contact and the radial fallback produces a clean bounce.
	# Pinned contract: finite, separating, and never double-counted as a hit.
	var wall := _wall(Vector2(0, 1))
	await physics_frame
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	for i in 60:
		await physics_frame
		if v.bullet_get_bounce_count(0) >= 1:
			break
	_check(v.bullet_get_bounce_count(0) >= 1, "T1 degenerate shape bounces via fallback")
	_check(v.get_bullet_collision_count(0) == 0, "T1 bounce not double-counted as a hit")
	_check(v.is_bullet_status_enabled(0), "T1 bullet alive after fallback bounce")
	var vel1: Vector2 = v.get_bullet_velocity(0)
	_check(vel1.is_finite(), "T1 velocity finite")
	_check(vel1.x < 0.0, "T1 bounce separates (vx=%.1f)" % vel1.x)
	wall.queue_free()
	await process_frame
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("SCALESHAPE T2 near-singular shape still bounces finite")
	var wall2 := _wall(Vector2(0.01, 1))
	await physics_frame
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	for i in 120:
		await physics_frame
		if v2.bullet_get_bounce_count(0) >= 1:
			break
	_check(v2.bullet_get_bounce_count(0) >= 1, "T2 sliver shape bounces")
	var vel2: Vector2 = v2.get_bullet_velocity(0)
	_check(vel2.is_finite(), "T2 post-bounce velocity finite")
	_check(vel2.x < 0.0, "T2 bounce separates (vx=%.1f)" % vel2.x)
	wall2.queue_free()
	await process_frame
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("SCALESHAPE T3 sane shape bounces precisely")
	var wall3 := _wall(Vector2.ONE)
	await physics_frame
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	for i in 120:
		await physics_frame
		if v3.bullet_get_bounce_count(0) >= 1:
			break
	_check(v3.bullet_get_bounce_count(0) >= 1, "T3 sane bounce registered")
	_check(v3.get_bullet_velocity(0).is_finite(), "T3 sane post-bounce finite")
	wall3.queue_free()
	await process_frame
	factory.free_active_bullets()
	await process_frame

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL BOUNCE-SCALED-SHAPE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
