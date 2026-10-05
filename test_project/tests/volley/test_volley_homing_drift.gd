extends BlastTest
## Homing aims THROUGH the inherited drift. inherited_velocity_offset (the
## spawner's momentum with inherit_movement_velocity) rides on every bullet
## for its whole life, so steering only the bullet's own heading at the
## target let the drift carry it past (bullets crabbed sideways and missed,
## worse the faster the spawner moved at fire time). Homing now solves the
## crab angle: own heading x speed + drift points at the target. A bullet too
## slow to cancel the drift leans fully against it. Zero drift aims exactly
## as before.

const TARGET := Vector2(600, 0)


func _homing_data(speed: float, n := 1) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, speed, 10.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(0, 30 * i)))
	d.transforms = arr
	d.set_collision_mask_from_array([])
	d.homing_smoothing = 20.0
	d.homing_take_control_of_texture_rotation = true
	d.homing_distance_before_reached = 12.0
	return d


## Degrees between the bullet's ground velocity and the line to the target.
func _ground_error_deg(v: BulletVolley2D, i: int, target: Vector2) -> float:
	var p: Vector2 = v.get_bullet_global_transform(i).origin
	return absf(rad_to_deg(v.get_bullet_velocity(i).angle_to(target - p)))


func test_ground_velocity_points_at_the_target_despite_a_crosswind() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_homing_data(200.0), Vector2(0, 150))
	v.shared_homing_deque_push_back_global_position_target(TARGET)
	await idle(30) # 0.5 s: smoothing 20 rad/s settles the heading long before
	assert_lt(_ground_error_deg(v, 0, TARGET), 2.0, "the drift is cancelled across the line of sight")
	assert_almost_eq(v.get_inherited_velocity_offset(), Vector2(0, 150), Vector2(0.001, 0.001), "the inherited momentum itself is kept")


func test_drifting_bullets_reach_the_target() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_homing_data(200.0, 3), Vector2(0, 150))
	watch_signals(v)
	v.shared_homing_deque_push_back_global_position_target(TARGET)
	for i in 480: # ~132 px/s ground speed through the drift: 600 px in < 5 s
		await idle(1)
		if get_signal_emit_count(v, "bullet_homing_target_reached") >= 3:
			break
	assert_eq(get_signal_emit_count(v, "bullet_homing_target_reached"), 3, "every drifting bullet reaches the target")


func test_a_bullet_too_slow_for_the_drift_leans_fully_against_it() -> void:
	# Drift 300 px/s across, own speed 200: it cannot cancel, so its own
	# heading points straight against the across-drift (the best it can do).
	var v: BulletVolley2D = factory.spawn_volley(_homing_data(200.0), Vector2(0, 300))
	v.shared_homing_deque_push_back_global_position_target(TARGET)
	await idle(30)
	var own: Vector2 = v.get_bullet_velocity(0) - v.get_inherited_velocity_offset()
	assert_true(own.is_finite(), "finite")
	assert_almost_eq(own.normalized().y, -1.0, 0.05, "leans straight against the drift")


func test_zero_drift_homes_exactly_as_before() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_homing_data(200.0))
	v.shared_homing_deque_push_back_global_position_target(TARGET)
	await idle(30)
	assert_lt(_ground_error_deg(v, 0, TARGET), 0.5, "straight at the target")


func test_a_moving_spawner_with_inherited_velocity_hits_with_every_shot() -> void:
	# The spawner sweeps along X at 150 px/s and its bullets inherit that
	# momentum; the target sits straight below the path, so the inherited
	# velocity is a pure crosswind for every shot.
	var target := Node2D.new()
	target.position = Vector2(-250, 700)
	target.add_to_group("homing_drift_target")
	add(target)
	var path := Path2D.new()
	var c := Curve2D.new()
	for p in [Vector2(-400, 0), Vector2(-100, 0)]:
		c.add_point(p)
	path.curve = c
	add(path)
	var d := _homing_data(200.0)
	d.transforms = [Transform2D()]
	d.all_bullet_speed_data = []
	d.shared_bullet_speed_data = H.make_speed(200.0) # every ring bullet flies
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_HELPER_CIRCLE, 8)
	sp.homing_enabled = true
	sp.homing_target_source = BulletSpawner2D.HOMING_SOURCE_NODE_GROUP
	sp.homing_node_group = &"homing_drift_target"
	sp.homing_smoothing = 20.0
	sp.homing_distance_before_reached = 12.0
	sp.movement_path = sp.get_path_to(path)
	sp.movement_loop_mode = BulletSpawner2D.MOVEMENT_LOOP_PING_PONG
	sp.movement_timing = BulletSpawner2D.MOVEMENT_TIMING_SPEED
	sp.movement_speed = 150.0 # a strong crosswind the 200 px/s bullets can still beat
	sp.inherit_movement_velocity = true
	sp.movement_enabled = true
	var reached := {}
	var fired: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n): fired.append(v))
	sp.volley_bullet_homing_target_reached.connect(func(v: BulletVolley2D, i: int, _t, _p): reached["%d_%d" % [v.get_instance_id(), i]] = true)
	await idle(5) # the spawner is moving at full speed
	for shot in 3:
		sp.shoot_once()
		await idle(10)
	assert_eq(fired.size(), 3, "three shots")
	for v in fired:
		assert_gt(absf((v as BulletVolley2D).get_inherited_velocity_offset().x), 100.0, "the shot really inherited the spawner's sideways momentum")
	await idle(20) # every shot is >= 0.33 s old: headings have settled
	var worst := 0.0
	for v in fired:
		for i in (v as BulletVolley2D).get_amount_bullets():
			worst = maxf(worst, _ground_error_deg(v, i, target.global_position))
	assert_lt(worst, 3.0, "every bullet's ground velocity points at the target")
	for i in 480:
		await idle(1)
		if reached.size() >= 3 * 8:
			break
	assert_eq(reached.size(), 3 * 8, "every bullet of every shot reaches the target")
