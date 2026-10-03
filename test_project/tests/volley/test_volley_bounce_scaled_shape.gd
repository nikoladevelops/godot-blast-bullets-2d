extends BlastTest
## A singular CollisionShape2D must fall back to the radial normal instead of
## inverting garbage: the precise-normal path requires an invertible shape
## transform, otherwise the caller uses the radial normal.


func _data() -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 200.0, 30.0)
	d.transforms = [Transform2D(0.0, Vector2(100, 300))]
	d.monitorable = true
	d.collision_shape = H.make_circle_shape(6.0)
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
	col.shape = H.make_circle_shape(12.0)
	col.scale = shape_scale
	wall.add_child(col)
	add(wall)
	return wall


func _bounce_once(shape_scale: Vector2, max_frames: int) -> BulletVolley2D:
	_wall(shape_scale)
	await physics()
	var v: BulletVolley2D = factory.spawn_volley(_data())
	for i in max_frames:
		await physics()
		if v.bullet_get_bounce_count(0) >= 1:
			break
	return v


func test_zero_scaled_shape_bounces_via_fallback() -> void:
	var v := await _bounce_once(Vector2(0, 1), 60)
	assert_gte(v.bullet_get_bounce_count(0), 1, "degenerate shape bounces via fallback")
	assert_eq(v.get_bullet_collision_count(0), 0, "bounce not double-counted as a hit")
	assert_true(v.is_bullet_status_enabled(0), "bullet alive after fallback bounce")
	var vel: Vector2 = v.get_bullet_velocity(0)
	assert_true(vel.is_finite(), "velocity finite")
	assert_lt(vel.x, 0.0, "bounce separates")


func test_near_singular_shape_bounces_finite() -> void:
	var v := await _bounce_once(Vector2(0.01, 1), 120)
	assert_gte(v.bullet_get_bounce_count(0), 1, "sliver shape bounces")
	assert_true(v.get_bullet_velocity(0).is_finite(), "post-bounce velocity finite")
	assert_lt(v.get_bullet_velocity(0).x, 0.0, "bounce separates")


func test_sane_shape_bounces_precisely() -> void:
	var v := await _bounce_once(Vector2.ONE, 120)
	assert_gte(v.bullet_get_bounce_count(0), 1, "sane bounce registered")
	assert_true(v.get_bullet_velocity(0).is_finite(), "sane post-bounce finite")


func test_no_usable_shape_child_falls_back_to_radial() -> void:
	# Precise mode reads the first USABLE direct CollisionShape2D child. Here
	# the only CollisionShape2D has no shape (skipped by the null-shape guard)
	# and the real collider is a CollisionPolygon2D (never analytic), so the
	# precise path finds nothing and the radial fallback must bounce cleanly.
	var wall := StaticBody2D.new()
	wall.position = Vector2(250, 300)
	wall.collision_layer = 8
	wall.collision_mask = 2
	var empty_cs := CollisionShape2D.new() # shape == null on purpose
	wall.add_child(empty_cs)
	var poly := CollisionPolygon2D.new()
	poly.polygon = PackedVector2Array([Vector2(-12, -12), Vector2(12, -12), Vector2(12, 12), Vector2(-12, 12)])
	wall.add_child(poly)
	add(wall)
	await physics()
	var v: BulletVolley2D = factory.spawn_volley(_data())
	for i in 120:
		await physics()
		if v.bullet_get_bounce_count(0) >= 1:
			break
	assert_eq(v.bullet_get_bounce_count(0), 1, "polygon-only target bounces once via the radial fallback")
	var vel: Vector2 = v.get_bullet_velocity(0)
	assert_true(vel.is_finite(), "post-bounce velocity finite")
	assert_lt(vel.x, 0.0, "radial normal (center -> bullet) separates head-on")
	assert_eq(v.get_bullet_collision_count(0), 0, "bounce is not counted as a hit")
