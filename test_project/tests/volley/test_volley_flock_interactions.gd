extends BlastTest
## Flocking next to every other steering feature: homing turns first and the
## flock bends the result (a homing swarm still reaches its target, spread
## out instead of stacked, with no heading snap when homing_delay_sec ends),
## direction curves in Override mode win and Additive ones compose, a
## rotation that owns the heading (adjust_direction_based_on_rotation) still
## lets the flock turn, face_heading turns the sprite with the heading (or
## leaves it alone), a locked orbit is never turned, gravity is drift (an
## aligned falling flock flies straight), bounced bullets keep flocking, a
## frozen bullet world turns nothing, interpolation stays finite, and
## despawned bullets leave the flock.

const DT := 1.0 / 60.0


func _volley(data: BulletVolleyData2D, flock: BulletFlock2D = null) -> BulletVolley2D:
	var v: BulletVolley2D = factory.spawn_volley(data)
	if flock != null:
		assert_true(v.flock_join(flock), "joins")
	return v


func _step(n: int) -> void:
	for k in n:
		assert_true(factory.debug_advance_time(DT), "tick %d runs" % k)


func _origins(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		out.append(v.get_bullet_transform(i).origin)
	return out


func _min_pair_distance(points: Array) -> float:
	var best := INF
	for a in points.size():
		for b in range(a + 1, points.size()):
			best = minf(best, (points[a] as Vector2).distance_to(points[b]))
	return best


func _mean_nearest_distance(points: Array) -> float:
	var total := 0.0
	for a in points.size():
		var best := INF
		for b in points.size():
			if a != b:
				best = minf(best, (points[a] as Vector2).distance_to(points[b]))
		total += best
	return total / points.size()


func _stream(count: int, seed_value: int) -> Array:
	seed(seed_value)
	var tr: Array = []
	for k in count:
		tr.append(Transform2D(-PI * 0.5 + randf_range(-0.5, 0.5), Vector2(randf_range(-25, 25), randf_range(-25, 25))))
	return tr


func _homing_data(tr: Array, smoothing := 3.0) -> BulletVolleyData2D:
	var d := H.make_flight_data(tr, 220.0, 1000.0)
	d.homing_smoothing = smoothing
	d.homing_take_control_of_texture_rotation = true
	return d


func test_a_homing_swarm_reaches_its_target_spread_out() -> void:
	var target := Vector2(600, 200)
	var tr := _stream(24, 3)
	var swarm := _volley(_homing_data(tr), BulletFlock2D.new())
	var plain := _volley(_homing_data(tr))
	swarm.shared_homing_deque_push_back_global_position_target(target)
	plain.shared_homing_deque_push_back_global_position_target(target)
	var reached := {}
	var problems: Array = []
	var plain_min := INF
	for tick in 300:
		_step(1)
		for i in 24:
			if swarm.get_bullet_transform(i).origin.distance_to(target) < 40.0:
				reached[i] = true
		if tick in [40, 80, 120, 160]: # the stream is in flight
			var swarm_mean := _mean_nearest_distance(_origins(swarm))
			var plain_mean := _mean_nearest_distance(_origins(plain))
			var swarm_min := _min_pair_distance(_origins(swarm))
			plain_min = _min_pair_distance(_origins(plain))
			if swarm_mean < plain_mean * 1.8 or swarm_min < 4.0:
				problems.append("tick %d: swarm mean %.1f min %.1f vs plain mean %.1f" % [tick, swarm_mean, swarm_min, plain_mean])
	assert_eq(reached.size(), 24, "every flocking bullet still reaches the target")
	assert_eq("\n".join(problems), "", "the flock keeps the stream apart all the way")
	assert_lt(plain_min, 1.5, "while plain homing stacks it on one line")


func test_no_heading_snap_when_the_homing_delay_ends() -> void:
	# During the delay the flock turns the pair toward each other; homing then
	# starts from that turned heading (face_heading off on purpose).
	var d := _homing_data([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 80))], 2.0)
	d.homing_delay_sec = 0.5
	var f := H.make_flock(0.0, 0.0, 4.0)
	f.face_heading = false
	var v := _volley(d, f)
	v.shared_homing_deque_push_back_global_position_target(Vector2(-400, 40))
	var problems: Array = []
	var turned_in_delay := 0.0
	for tick in 60:
		var before: Vector2 = v.get_bullet_direction(0)
		_step(1)
		var step := absf(before.angle_to(v.get_bullet_direction(0)))
		if tick < 28:
			turned_in_delay += step
		if step > 0.12:
			problems.append("tick %d jumped %.3f rad" % [tick, step])
	assert_gt(turned_in_delay, 0.3, "the flock really turned the bullet during the delay")
	assert_eq("\n".join(problems), "", "the heading never jumps (homing picks up the flocked heading)")


func test_override_direction_curves_win_and_additive_ones_compose() -> void:
	# The pair pulls together along y; the curves run after the flock.
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 30))]
	for mode in [BulletCurvesData2D.Override, BulletCurvesData2D.Additive]:
		var curves := BulletCurvesData2D.new()
		curves.y_direction_curve = H.make_flat_curve(0.0)
		curves.y_direction_curve_mode = mode
		var d := H.make_flight_data(tr, 100.0, 1000.0)
		d.shared_bullet_curves_data = curves
		var flocked := _volley(d, H.make_flock(0.0, 0.0, 3.0))
		_step(30)
		if mode == BulletCurvesData2D.Override:
			assert_eq(flocked.get_bullet_direction(0), Vector2(1, 0), "Override y = 0 wins its axis: the flock cannot turn the bullet off the line")
			assert_eq(flocked.get_bullet_transform(1).origin.y - flocked.get_bullet_transform(0).origin.y, 30.0, "the gap never closes")
		else:
			assert_gt(flocked.get_bullet_direction(0).y, 0.05, "an Additive 0 adds nothing: the flock turns the bullet")
		factory.reset()
		await idle(1)


func test_a_rotation_that_owns_the_heading_still_lets_the_flock_turn() -> void:
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 40))]
	var d := H.make_flight_data(tr, 100.0, 1000.0)
	d.adjust_direction_based_on_rotation = true
	d.rotate_only_textures = false
	d.shared_bullet_rotation_data = H.make_rotation(0.0, 0.0)
	var f := H.make_flock(0.0, 0.0, 3.0)
	f.face_heading = false
	var v := _volley(d, f)
	_step(20)
	assert_gt(v.get_bullet_direction(0).y, 0.05, "bullet 0 turned toward bullet 1")
	var basis: Vector2 = v.get_bullet_transform(0).x.normalized()
	assert_almost_eq(basis, v.get_bullet_direction(0), Vector2(1e-4, 1e-4), "the rotation turned with it (it derives the heading)")


func test_face_heading_turns_the_sprite_with_the_heading() -> void:
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 40))]
	var on := _volley(H.make_flight_data(tr, 100.0, 1000.0), H.make_flock(0.0, 0.0, 3.0))
	var off_flock := H.make_flock(0.0, 0.0, 3.0)
	off_flock.face_heading = false
	var off := _volley(H.make_flight_data(tr, 100.0, 1000.0), off_flock)
	_step(20)
	assert_gt(on.get_bullet_direction(0).y, 0.05, "turned")
	assert_almost_eq(on.get_bullet_transform(0).get_rotation(), on.get_bullet_direction(0).angle(), 1e-4, "face_heading: the sprite faces the heading")
	assert_gt(off.get_bullet_direction(0).y, 0.05, "turned too")
	assert_eq(off.get_bullet_transform(0).get_rotation(), 0.0, "face_heading off: the sprite keeps its rotation")


func test_a_locked_orbit_is_never_turned() -> void:
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 30))]
	var volleys: Array = []
	for k in 2:
		var d := H.make_flight_data(tr, 300.0, 1000.0)
		d.homing_smoothing = 30.0
		d.homing_take_control_of_texture_rotation = true
		var v: BulletVolley2D = factory.spawn_volley(d)
		v.shared_homing_deque_push_back_global_position_target(Vector2(250, 0))
		for b in 2:
			v.bullet_enable_orbiting(b, 40.0, BulletVolley2D.OrbitRight, BulletVolley2D.FaceTarget)
		volleys.append(v)
	for tick in 240: # both reach the ring and lock
		_step(1)
		if volleys[0].bullet_is_orbiting_locked(0) and volleys[0].bullet_is_orbiting_locked(1):
			break
	assert_true(volleys[0].bullet_is_orbiting_locked(0) and volleys[0].bullet_is_orbiting_locked(1), "locked")
	assert_eq(_origins(volleys[0]), _origins(volleys[1]), "twins so far")
	volleys[0].flock_join(H.make_flock(3.0, 2.0, 3.0))
	_step(30)
	assert_gt(volleys[0].debug_get_bullet_flock_neighbors(0), 0, "the orbiters see each other")
	assert_eq(_origins(volleys[0]), _origins(volleys[1]), "the orbit owns the bullets: the flock turns nothing")
	assert_eq([volleys[0].get_bullet_direction(0), volleys[0].get_bullet_direction(1)], [volleys[1].get_bullet_direction(0), volleys[1].get_bullet_direction(1)], "headings untouched")


func test_gravity_is_drift_for_the_flock() -> void:
	var tr: Array = []
	for k in 5:
		tr.append(Transform2D(0.0, Vector2(k * 15.0, 0)))
	var d := H.make_flight_data(tr, 100.0, 1000.0)
	d.gravity = Vector2(0, 300)
	var flocked := _volley(d, H.make_flock(0.0, 1.0, 0.0))
	var d2 := H.make_flight_data(tr, 100.0, 1000.0)
	d2.gravity = Vector2(0, 300)
	var control := _volley(d2)
	_step(40)
	assert_eq(_origins(flocked), _origins(control), "alignment reads headings, not the fall: an aligned falling flock flies straight")


func test_bounced_bullets_keep_flocking() -> void:
	var wall := StaticBody2D.new()
	wall.position = Vector2(120, 20)
	wall.collision_layer = 8
	wall.collision_mask = 2
	var col := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = Vector2(20, 400)
	col.shape = box
	wall.add_child(col)
	add(wall)
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 20)), Transform2D(0.0, Vector2(0, 40))]
	var d := H.make_flight_data(tr, 200.0, 1000.0)
	d.set_collision_mask_from_array([4])
	d.set_bounce_mask_from_array([4])
	d.bounce_mode = BulletVolleyData2D.BOUNCE_PRECISE_SHAPE
	d.monitorable = true
	d.collision_shape = H.make_circle_shape(6.0)
	var bounces := [0]
	factory.bounce_body_entered.connect(func(_body: Node, _v: BulletVolley2D, _i: int) -> void: bounces[0] += 1)
	var v := _volley(d, H.make_flock(1.5, 1.0, 1.0))
	await physics()
	for frame in 120: # real frames: the physics server reports the wall
		await physics()
		if bounces[0] >= 3:
			break
	assert_gte(bounces[0], 3, "every bullet bounced off the wall")
	await physics(10)
	assert_gt(v.debug_get_bullet_flock_neighbors(0), 0, "still flocking after the bounce")
	for i in 3:
		assert_true(v.get_bullet_transform(i).is_finite() and v.get_bullet_direction(i).is_finite(), "bullet %d finite" % i)


func test_a_frozen_bullet_world_turns_nothing() -> void:
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 40))]
	var v := _volley(H.make_flight_data(tr, 100.0, 1000.0), H.make_flock(0.0, 0.0, 3.0))
	_step(1)
	factory.time_scale = 0.0
	var headings := [v.get_bullet_direction(0), v.get_bullet_direction(1)]
	_step(10)
	assert_eq([v.get_bullet_direction(0), v.get_bullet_direction(1)], headings, "time scale 0: no turn")
	assert_eq(factory.debug_get_flock_stats()["grid_members"], 0, "and no pass work")
	factory.time_scale = 1.0
	_step(1)
	assert_ne(v.get_bullet_direction(0), headings[0], "turning resumes with the clock")


func test_interpolated_flocks_stay_finite() -> void:
	factory.use_physics_interpolation = true
	var tr := _stream(12, 4)
	var v := _volley(H.make_flight_data(tr, 150.0, 1000.0), BulletFlock2D.new())
	for frame in 30:
		await physics()
		await idle(1)
	for i in 12:
		assert_true(v.get_bullet_transform(i).is_finite(), "bullet %d finite" % i)
	assert_gt(v.debug_get_bullet_flock_neighbors(0), 0, "flocking")


func test_despawned_bullets_leave_the_flock() -> void:
	factory.despawn_bounds_enabled = true
	factory.despawn_bounds_rect = Rect2(-100, -100, 300, 300)
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 20)), Transform2D(0.0, Vector2(190, 50))]
	var v := _volley(H.make_flight_data(tr, 100.0, 1000.0), H.make_flock(0.0, 0.0, 0.5))
	_step(1)
	assert_eq(factory.debug_get_flock_stats()["grid_members"], 3, "three in the flock")
	for tick in 60: # bullet 2 leaves the bounds
		_step(1)
		if not v.is_bullet_status_enabled(2):
			break
	assert_false(v.is_bullet_status_enabled(2), "culled")
	_step(1)
	assert_eq(factory.debug_get_flock_stats()["grid_members"], 2, "the culled bullet left the flock")


func test_face_heading_keeps_facing_while_homing_snaps_the_heading() -> void:
	# A movement pattern makes homing snap the heading straight at the target
	# every tick (the sprite is not touched by that snap): face_heading must
	# still leave the sprite facing the flight, never drift away from it.
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 40)), Transform2D(0.0, Vector2(30, 20))]
	var v := _volley(H.make_flight_data(tr, 120.0, 1000.0), H.make_flock(2.0, 1.0, 3.0))
	var c := Curve2D.new()
	c.add_point(Vector2.ZERO)
	c.add_point(Vector2(100, 0))
	v.set_shared_movement_pattern_curve(c)
	v.shared_homing_deque_push_back_global_position_target(Vector2(800, 20))
	var worst := 0.0
	var turned := false
	for tick in 120:
		_step(1)
		for i in 3:
			var sprite := v.get_bullet_transform(i).get_rotation()
			var heading := v.get_bullet_direction(i).angle()
			if v.debug_get_bullet_flock_steering(i) != Vector2():
				turned = true
			worst = maxf(worst, absf(angle_difference(sprite, heading)))
	assert_true(turned, "the flock steered")
	assert_lt(worst, 0.02, "the sprite faces the flight heading every tick (worst %.4f rad)" % worst)


func test_face_heading_leaves_a_spinning_sprite_to_its_spin() -> void:
	# Rotation data owns the sprite (it spins): the flock turns the heading
	# but never adds its turns to the spinning sprite.
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 40))]
	var d := H.make_flight_data(tr, 100.0, 1000.0)
	d.shared_bullet_rotation_data = H.make_rotation(2.0, 10.0)
	var flocked := _volley(d, H.make_flock(0.0, 0.0, 3.0))
	var d2 := H.make_flight_data(tr, 100.0, 1000.0)
	d2.shared_bullet_rotation_data = H.make_rotation(2.0, 10.0)
	var control := _volley(d2)
	_step(40)
	assert_ne(flocked.get_bullet_direction(0), control.get_bullet_direction(0), "the flock turned the heading")
	for i in 2:
		assert_almost_eq(flocked.get_bullet_transform(i).get_rotation(), control.get_bullet_transform(i).get_rotation(), 1e-5, "bullet %d spins exactly like the unflocked one" % i)
