extends BlastTest
## One pinned check per fix from the v4 stability audit (spawn-in-handler and
## the lifetime hold have their own suites).

var timer_fires := 0


func _on_timer() -> void:
	timer_fires += 1


func _bounce_data() -> DirectionalBulletsData2D:
	var d := H.make_directional_data(1, 200.0, 30.0)
	d.transforms = [Transform2D(0.0, Vector2(100, 300))]
	d.monitorable = true
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	d.set_bounce_mask_from_array([4])
	d.bounce_mode = 1
	return d


func test_texture_rotation_round_trip_with_offset() -> void:
	var d := H.make_directional_data(1, 0.0)
	d.texture_rotation_radians = 0.7
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	var r0: float = v.get_bullet_texture_rotation_radians(0)
	assert_almost_eq(r0, 0.0, 0.001, "fresh bullet reads 0 (offset excluded)")
	v.set_bullet_texture_rotation_radians(0, v.get_bullet_texture_rotation_radians(0))
	v.set_bullet_texture_rotation_radians(0, v.get_bullet_texture_rotation_radians(0))
	assert_almost_eq(v.get_bullet_texture_rotation_radians(0), r0, 0.001, "set(get()) is a no-op")
	v.set_bullet_texture_rotation_degrees(0, 30.0)
	assert_almost_eq(v.get_bullet_texture_rotation_degrees(0), 30.0, 0.01, "degrees round-trip")
	assert_almost_eq(angle_difference(v.get_bullet_transform(0).get_rotation(), deg_to_rad(30.0) + 0.7), 0.0, 0.001, "instance still carries the offset")


func test_repeating_timer_keeps_period() -> void:
	timer_fires = 0
	var v: DirectionalBullets2D = spawn_dir(1, 0.0, 30.0)
	await idle(1)
	v.multimesh_attach_time_based_function(0.1, _on_timer, true, true)
	await physics(Engine.physics_ticks_per_second)
	await idle(1)
	assert_between(timer_fires, 9, 11, "0.1 s repeater fires ~10x per second")
	v.multimesh_detach_all_time_based_functions()


func test_block_shared_spin_survives_bullet_zero() -> void:
	var bd := H.make_block_data(4, 0.0, 30.0)
	var rot := BulletRotationData2D.new()
	rot.rotation_speed = 6.0
	bd.all_bullet_rotation_data = [rot]
	var container := factory.get_node("BlockBulletsContainer")
	factory.spawn_block_bullets(bd)
	await idle(1)
	var block: BlockBullets2D = null
	for c in container.get_children():
		if c is BlockBullets2D and c.debug_get_volley_info().get("is_active", false) and c.get_amount_bullets() == 4:
			block = c
	assert_not_null(block, "block volley found")
	if block == null:
		return
	block.disable_bullet(0)
	var y0: float = block.get_bullet_transform(1).get_rotation()
	await physics(10)
	assert_gt(absf(angle_difference(block.get_bullet_transform(1).get_rotation(), y0)), 0.3, "remaining bullets keep spinning")


func test_pool_key_inspector_enum() -> void:
	var hint := ""
	for p in MultiMeshPoolKey2D.new().get_property_list():
		if p["name"] == "shape_type":
			hint = p["hint_string"]
	assert_string_contains(hint, "Circle:3")
	assert_string_contains(hint, "Rectangle:4")
	assert_string_contains(hint, "Capsule:5")


func test_transforms_scale_keeps_mirroring() -> void:
	var marker: Node2D = add(Node2D.new())
	marker.scale = Vector2(-1, 1)
	var sp := make_spawner(null, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.set_transforms_generator(marker)
	sp.transforms_scale = 1.0
	var plain = sp.collect_spawn_transforms()
	sp.transforms_scale = 2.0
	var scaled = sp.collect_spawn_transforms()
	assert_eq(plain.size(), 4)
	assert_eq(scaled.size(), 4)
	for i in mini(plain.size(), scaled.size()):
		var a: Transform2D = plain[i]
		var b: Transform2D = scaled[i]
		assert_lt(a.determinant(), 0.0, "plain stays mirrored [%d]" % i)
		assert_lt(b.determinant(), 0.0, "scaled stays mirrored [%d]" % i)
		assert_almost_eq(b.x, a.x * 2.0, Vector2(0.01, 0.01), "x scaled exactly [%d]" % i)
		assert_almost_eq(b.y, a.y * 2.0, Vector2(0.01, 0.01), "y scaled exactly [%d]" % i)
		assert_almost_eq(b.origin, a.origin, Vector2(0.01, 0.01), "origin unchanged [%d]" % i)


func test_bounce_normal_on_scaled_slope() -> void:
	var wall := StaticBody2D.new()
	wall.position = Vector2(250, 300)
	wall.collision_layer = 8
	wall.collision_mask = 2
	var col := CollisionShape2D.new()
	var seg := SegmentShape2D.new()
	seg.a = Vector2(-40, -40)
	seg.b = Vector2(40, 40)
	col.shape = seg
	col.scale = Vector2(1, 3)
	wall.add_child(col)
	add(wall)
	await physics()
	var bv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data())
	for i in 120:
		await physics()
		if bv.bullet_get_bounce_count(0) >= 1:
			break
	assert_gte(bv.bullet_get_bounce_count(0), 1, "bounced off the scaled slope")
	var n: Vector2 = bv.debug_get_bounce_info(0).get("last_normal", Vector2.ZERO)
	# World segment direction (1, 3): the true normal is +-(3, -1)/sqrt(10).
	assert_almost_eq(absf(n.dot(Vector2(3, -1).normalized())), 1.0, 0.01, "normal is the true perpendicular (n=%s)" % n)


func test_free_volley_deferred_rejects_non_volleys() -> void:
	var bystander: Node2D = add(Node2D.new())
	factory.free_volley_deferred(bystander)
	expect_any_error("non-volley is rejected loudly")
	await idle(1)
	assert_true(is_instance_valid(bystander) and not bystander.is_queued_for_deletion(), "non-volley left alone")


func test_distance_phased_wobble_stays_bounded() -> void:
	var wd := H.make_directional_data(1, 300.0, 30.0)
	var wob := BulletWobbleData2D.new()
	wob.enabled = true
	wob.mode = 0
	wob.amplitude = 40.0
	wob.frequency_hz = 1.0
	wob.distance_phased = true
	wob.face_movement_direction = false
	wd.shared_bullet_wobble_data = wob
	var wv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(wd, Vector2(0, 150))
	var start: float = wv.get_bullet_direction(0).angle()
	var max_dev := 0.0
	for i in 240:
		await physics()
		max_dev = maxf(max_dev, absf(angle_difference(wv.get_bullet_direction(0).angle(), start)))
	assert_lt(absf(angle_difference(wv.get_bullet_direction(0).angle(), start)), 1.0, "heading ends near launch heading")
	assert_lt(max_dev, 1.6, "heading oscillation bounded")
