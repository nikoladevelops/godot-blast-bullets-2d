extends BlastTest
## Flocking mixed with every other feature; each test flies a flock next
## to one feature and asserts BOTH still do their job, with finite poses,
## unit headings and a dangling-free factory throughout.
## Steering: homing turns first and the flock bends it (a homing swarm
## reaches its target spread out; no heading snap when homing_delay_sec
## ends), Override direction curves win their axis and Additive ones
## compose, a rotation that owns the heading still lets the flock turn,
## face_heading turns the sprite (or leaves a spinning one alone), a locked
## orbit is never turned, gravity is drift, bounced bullets keep flocking, a
## frozen world turns nothing, interpolation stays finite, despawned bullets
## leave the flock.
## Everything else: graze reports, flight sounds play, attachments ride,
## trails bake, per-bullet homing closes in, orbits lock, wobble weaves,
## teleports keep the flock, velocity and direction setters are honored,
## freeze/wake churn, pickups, area clears, randomized speeds, two
## factories, a paused factory, a reset from a handler.

const DT := 1.0 / 60.0
const P := preload("res://tests/common/pickup_test_helpers.gd")


func _volley(data: BulletVolleyData2D, flock: BulletFlock2D = null) -> BulletVolley2D:
	var v: BulletVolley2D = factory.spawn_volley(data)
	if flock != null:
		assert_true(v.flock_join(flock), "joins")
	return v


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
	# The default flock at the spawner's default homing turn rate (5): the
	# separation (10, faster than homing) spreads the arrival (bullets
	# queue around the target) but every bullet still gets there.
	var target := Vector2(600, 200)
	var tr := _stream(24, 3)
	var swarm := _volley(_homing_data(tr, 5.0), BulletFlock2D.new())
	var plain := _volley(_homing_data(tr, 5.0))
	swarm.shared_homing_deque_push_back_global_position_target(target)
	plain.shared_homing_deque_push_back_global_position_target(target)
	var reached := {}
	var problems: Array = []
	var plain_min := INF
	for tick in 480: # 8 s: a strong default separation spreads the arrival
		step_factory(1)
		for i in 24:
			if swarm.get_bullet_transform(i).origin.distance_to(target) < 40.0:
				reached[i] = true
		if tick in [40, 80, 120, 160]: # the stream is in flight
			var swarm_mean := _mean_nearest_distance(origins(swarm))
			var plain_mean := _mean_nearest_distance(origins(plain))
			var swarm_min := min_pair_distance(origins(swarm))
			plain_min = min_pair_distance(origins(plain))
			# Separation is the MEAN push (calm, never saturating): a homing
			# stream widens steadily instead of exploding apart.
			if swarm_mean < plain_mean * 1.3 or swarm_min < 4.0:
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
		step_factory(1)
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
		step_factory(30)
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
	step_factory(20)
	assert_gt(v.get_bullet_direction(0).y, 0.05, "bullet 0 turned toward bullet 1")
	var basis: Vector2 = v.get_bullet_transform(0).x.normalized()
	assert_almost_eq(basis, v.get_bullet_direction(0), Vector2(1e-4, 1e-4), "the rotation turned with it (it derives the heading)")


func test_face_heading_turns_the_sprite_with_the_heading() -> void:
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 40))]
	var on := _volley(H.make_flight_data(tr, 100.0, 1000.0), H.make_flock(0.0, 0.0, 3.0))
	var off_flock := H.make_flock(0.0, 0.0, 3.0)
	off_flock.face_heading = false
	var off := _volley(H.make_flight_data(tr, 100.0, 1000.0), off_flock)
	step_factory(20)
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
		step_factory(1)
		if volleys[0].bullet_is_orbiting_locked(0) and volleys[0].bullet_is_orbiting_locked(1):
			break
	assert_true(volleys[0].bullet_is_orbiting_locked(0) and volleys[0].bullet_is_orbiting_locked(1), "locked")
	assert_eq(origins(volleys[0]), origins(volleys[1]), "twins so far")
	volleys[0].flock_join(H.make_flock(3.0, 2.0, 3.0))
	step_factory(30)
	assert_gt(volleys[0].debug_get_bullet_flock_neighbors(0), 0, "the orbiters see each other")
	assert_eq(origins(volleys[0]), origins(volleys[1]), "the orbit owns the bullets: the flock turns nothing")
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
	step_factory(40)
	assert_eq(origins(flocked), origins(control), "alignment reads headings, not the fall: an aligned falling flock flies straight")


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
	step_factory(1)
	factory.time_scale = 0.0
	var headings := [v.get_bullet_direction(0), v.get_bullet_direction(1)]
	step_factory(10)
	assert_eq([v.get_bullet_direction(0), v.get_bullet_direction(1)], headings, "time scale 0: no turn")
	assert_eq(factory.debug_get_flock_stats()["grid_members"], 0, "and no pass work")
	factory.time_scale = 1.0
	step_factory(1)
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
	step_factory(1)
	assert_eq(factory.debug_get_flock_stats()["grid_members"], 3, "three in the flock")
	for tick in 60: # bullet 2 leaves the bounds
		step_factory(1)
		if not v.is_bullet_status_enabled(2):
			break
	assert_false(v.is_bullet_status_enabled(2), "culled")
	step_factory(1)
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
		step_factory(1)
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
	step_factory(40)
	assert_ne(flocked.get_bullet_direction(0), control.get_bullet_direction(0), "the flock turned the heading")
	for i in 2:
		assert_almost_eq(flocked.get_bullet_transform(i).get_rotation(), control.get_bullet_transform(i).get_rotation(), 1e-5, "bullet %d spins exactly like the unflocked one" % i)


func _cluster(count: int, at := Vector2.ZERO, seed_value := 3, spread := 40.0) -> Array:
	seed(seed_value)
	var tr: Array = []
	for k in count:
		tr.append(Transform2D(randf_range(-0.6, 0.6), at + Vector2(randf_range(0, spread), randf_range(0, spread))))
	return tr


func _flock_volley(tr: Array, flock: BulletFlock2D = null, speed := 120.0) -> BulletVolley2D:
	var v: BulletVolley2D = factory.spawn_volley(H.make_flight_data(tr, speed, 1000.0))
	v.flock_join(flock if flock != null else BulletFlock2D.new())
	return v


## Finite pose, unit heading for every live bullet + a consistent factory.
func _broken(v: BulletVolley2D) -> Array:
	var bad: Array = []
	if not is_instance_valid(v) or v.debug_get_life_state() != "active":
		return bad
	for i in v.get_amount_bullets():
		if not v.is_bullet_status_enabled(i):
			continue
		var d: Vector2 = v.get_bullet_direction(i)
		if not v.get_bullet_transform(i).is_finite() or absf(d.length() - 1.0) > 1e-4:
			bad.append("bullet %d: %s %s" % [i, v.get_bullet_transform(i).origin, d])
	var dangling: Dictionary = factory.debug_assert_no_dangling()
	if not dangling["ok"]:
		bad.append(dangling["error"])
	return bad


func _steered(v: BulletVolley2D) -> bool:
	for i in v.get_amount_bullets():
		if v.debug_get_bullet_flock_steering(i) != Vector2():
			return true
	return false


func test_graze_reports_while_the_flock_steers() -> void:
	make_graze_target(Vector2(150, 20))
	var log := H.record_graze(factory)
	var v := _flock_volley(_cluster(8))
	v.graze_set_zones([H.make_graze_zone([80.0])], &"graze_targets") # the default flock spreads the cluster
	step_factory(90)
	assert_gt(log.size(), 0, "the flock grazed the target")
	assert_true(_steered(v), "and kept flocking")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_flight_sounds_play_while_the_flock_steers() -> void:
	var v := _flock_volley(_cluster(6))
	v.sound_set_effects([H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT)])
	var before := int(factory.debug_get_sound_stats()["plays_total"])
	step_factory(30)
	assert_gt(int(factory.debug_get_sound_stats()["plays_total"]), before, "flight sounds played")
	assert_true(_steered(v), "and the flock steered")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_attachments_ride_their_flocking_bullets() -> void:
	var v := _flock_volley(_cluster(5))
	v.all_bullets_set_attachment(make_probe_scene(), Vector2.ZERO, true)
	step_factory(45)
	assert_true(_steered(v), "the flock steered")
	for i in 5:
		var att: Node2D = v.bullet_get_attachment(i)
		assert_not_null(att, "bullet %d keeps its attachment" % i)
		if att != null:
			assert_almost_eq(att.global_position, v.get_bullet_transform(i).origin, Vector2(0.01, 0.01), "attachment %d sits on its turned bullet" % i)
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_trails_bake_behind_flocking_bullets() -> void:
	var d := H.make_flight_data(_cluster(6), 120.0, 1000.0)
	d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW, 2)]
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.flock_join(BulletFlock2D.new())
	step_factory(30)
	assert_gt(int(v.debug_get_effect_layers_info().get("trail_bake_count", 0)), 0, "trails bake")
	assert_true(_steered(v), "the flock steered")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_per_bullet_homing_closes_in_while_flocking() -> void:
	var d := H.make_flight_data(_cluster(6), 150.0, 1000.0)
	d.homing_smoothing = 4.0
	d.homing_take_control_of_texture_rotation = true
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.flock_join(BulletFlock2D.new())
	var targets := [Vector2(400, -200), Vector2(400, 260)]
	var start: Array = []
	for i in 6:
		v.bullet_homing_push_back_global_position_target(i, targets[i % 2])
		start.append(v.get_bullet_transform(i).origin.distance_to(targets[i % 2]))
	step_factory(90) # 1.5 s: the default separation pushes the pairs sideways first
	for i in 6:
		assert_lt(v.get_bullet_transform(i).origin.distance_to(targets[i % 2]), start[i] - 100.0, "bullet %d closes in on its own target" % i)
	assert_true(_steered(v), "while the flock steers")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_orbiting_bullets_still_lock_onto_their_ring() -> void:
	var d := H.make_flight_data([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 30)), Transform2D(0.0, Vector2(0, -30))], 300.0, 1000.0)
	d.homing_smoothing = 30.0
	d.homing_take_control_of_texture_rotation = true
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.flock_join(H.make_flock(2.0, 1.0, 1.0))
	v.shared_homing_deque_push_back_global_position_target(Vector2(250, 0))
	for b in 3:
		v.bullet_enable_orbiting(b, 50.0, BulletVolley2D.OrbitRight, BulletVolley2D.FaceTarget)
	var locked := false
	for tick in 300: # early break once every bullet locked
		step_factory(1)
		locked = v.bullet_is_orbiting_locked(0) and v.bullet_is_orbiting_locked(1) and v.bullet_is_orbiting_locked(2)
		if locked:
			break
	assert_true(locked, "the flock never keeps an orbit from locking")
	step_factory(30)
	for b in 3:
		assert_almost_eq(v.get_bullet_transform(b).origin.distance_to(Vector2(250, 0)), 50.0, 1.5, "bullet %d rides its ring" % b)
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_wobble_weaves_on_top_of_the_flock() -> void:
	var d := H.make_flight_data(_cluster(6), 120.0, 1000.0)
	var wobble := BulletWobbleData2D.new()
	wobble.enabled = true
	wobble.amplitude = 25.0
	wobble.frequency_hz = 2.0
	wobble.face_movement_direction = true
	d.shared_bullet_wobble_data = wobble
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.flock_join(BulletFlock2D.new())
	var problems: Array = []
	for tick in 90:
		step_factory(1)
		problems.append_array(_broken(v))
	assert_eq(problems.slice(0, 4), [], "finite, unit, consistent every tick")
	assert_true(_steered(v), "the flock steered")


func test_a_teleport_moves_the_whole_flock_without_breaking_it() -> void:
	var v := _flock_volley(_cluster(8))
	step_factory(5)
	var before: Array = []
	for i in 8:
		before.append(v.debug_get_bullet_flock_neighbors(i))
	var pos0: Vector2 = v.get_bullet_transform(0).origin
	factory.teleport_shift_all_bullets(Vector2(5000, -3000))
	assert_almost_eq(v.get_bullet_transform(0).origin, pos0 + Vector2(5000, -3000), Vector2(0.01, 0.01), "shifted")
	step_factory(1)
	var after: Array = []
	for i in 8:
		after.append(v.debug_get_bullet_flock_neighbors(i))
	assert_eq(after, before, "the flock moved as a whole: same flockmates")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_velocity_and_direction_setters_are_honored() -> void:
	var v := _flock_volley(_cluster(6), H.make_flock(1.0, 1.0, 1.0))
	step_factory(5)
	v.bullet_set_velocity(0, Vector2(0, -150))
	assert_almost_eq(v.get_bullet_velocity(0), Vector2(0, -150), Vector2(0.01, 0.01), "the exact velocity that was set")
	v.set_bullet_direction(1, Vector2(-1, 0))
	assert_almost_eq(v.get_bullet_direction(1), Vector2(-1, 0), Vector2(1e-5, 1e-5), "the direction that was set")
	step_factory(1)
	assert_gt(v.get_bullet_direction(0).dot(Vector2(0, -1)), 0.9, "the flock turns on from the set heading, it does not snap back")
	assert_gt(v.get_bullet_direction(1).dot(Vector2(-1, 0)), 0.9, "same for the direction setter")
	assert_almost_eq(v.get_bullet_velocity(0).length(), 150.0, 0.01, "the flock never changes the speed")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_freeze_and_wake_churn() -> void:
	var v := _flock_volley(_cluster(10), H.make_flock(1.5, 1.0, 1.0))
	v.set_is_auto_pooling_enabled(false)
	seed(77)
	var problems: Array = []
	for tick in 120:
		var i := randi() % 10
		if v.is_bullet_status_enabled(i):
			v.disable_bullet(i)
		else:
			v.enable_bullet(i)
		step_factory(1)
		problems.append_array(_broken(v))
		for j in 10:
			if not v.is_bullet_status_enabled(j) and v.debug_get_bullet_flock_neighbors(j) != 0:
				problems.append("tick %d: frozen bullet %d was gathered" % [tick, j])
	assert_eq(problems.slice(0, 4), [], "frozen bullets never flock, the rest stays consistent")


func test_converting_a_flock_to_pickups_leaves_no_flocking_behind() -> void:
	var target := Node2D.new()
	target.position = Vector2(-300, 0)
	add(target)
	var v := _flock_volley(_cluster(6))
	step_factory(5)
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 1, "armed")
	var pickups: Array = v.convert_to_pickups(P.coin(), target)
	assert_gt(pickups.size(), 0, "converted into pickup volleys")
	for pv in pickups:
		assert_null((pv as BulletVolley2D).flock_get(), "a pickup volley never flocks")
	step_factory(3)
	var stats: Dictionary = factory.debug_get_flock_stats()
	assert_eq(stats["armed_volleys"], 0, "the converted volley left the pool with its flock dropped")
	assert_eq(stats["grid_members"], 0, "pickups never flock")
	assert_true(factory.debug_assert_no_dangling()["ok"], "consistent")


func test_an_area_clear_removes_flockmates() -> void:
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(10, 0)), Transform2D(0.0, Vector2(0, 30)), Transform2D(0.0, Vector2(300, 0))]
	var v := _flock_volley(tr)
	step_factory(1)
	assert_eq(factory.debug_get_flock_stats()["grid_members"], 4, "four in the grid")
	factory.clear_bullets_in_circle(Vector2(0, 0), 15.0)
	step_factory(1)
	assert_eq(factory.debug_get_flock_stats()["grid_members"], 2, "the cleared pair left the flock")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_randomized_speeds_stay_owned_by_the_spawner() -> void:
	var sp := make_spawner(H.make_volley_data(8, 100.0, 1000.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8)
	sp.helper_ring_radius = 30.0
	sp.bullet_speed_scale_min = 0.5
	sp.bullet_speed_scale_max = 2.0
	sp.bullet_random_seed = 5
	sp.flock_enabled = true
	sp.flock = BulletFlock2D.new()
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v))
	sp.shoot_once()
	var v: BulletVolley2D = got[0]
	var speeds: Array = []
	for i in 8:
		speeds.append(v.get_bullet_velocity(i).length())
	assert_gt(speeds.max() - speeds.min(), 10.0, "the speeds really differ")
	step_factory(1)
	assert_true(_steered(v), "the flock steers the ring")
	step_factory(59)
	for i in 8:
		assert_almost_eq(v.get_bullet_velocity(i).length(), speeds[i], 0.01, "bullet %d keeps its randomized speed" % i)
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_two_factories_never_share_a_flock() -> void:
	var other := BulletFactory2D.new()
	add(other)
	await idle(1)
	var f := BulletFlock2D.new()
	var tr := _cluster(8)
	var here := _flock_volley(tr, f)
	var there: BulletVolley2D = other.spawn_volley(H.make_flight_data(tr, 120.0, 1000.0))
	there.flock_join(f)
	var solo := _flock_volley(tr, BulletFlock2D.new())
	solo.global_position = Vector2.ZERO
	for k in 30:
		factory.debug_advance_time(DT)
		other.debug_advance_time(DT)
	for i in 8:
		assert_eq(there.get_bullet_transform(i), solo.get_bullet_transform(i), "bullet %d: the other factory's flock flies exactly like a lone one" % i)
	assert_eq(other.debug_get_flock_stats()["flocks"], 1, "one flock over there")
	other.free_active_bullets()


func test_a_paused_factory_turns_nothing() -> void:
	var v := _flock_volley(_cluster(6), H.make_flock(0.0, 0.0, 5.0))
	step_factory(2)
	var headings: Array = []
	for i in 6:
		headings.append(v.get_bullet_direction(i))
	factory.set_is_factory_processing_bullets(false)
	for k in 5:
		factory.debug_advance_time(DT)
	for i in 6:
		assert_eq(v.get_bullet_direction(i), headings[i], "bullet %d holds while paused" % i)
	factory.set_is_factory_processing_bullets(true)
	step_factory(1)
	assert_true(_steered(v), "flocking resumes")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_a_reset_from_a_handler_leaves_nothing_armed() -> void:
	var trigger_data := H.make_flight_data([Transform2D(0.0, Vector2(-500, -500))], 0.0, 0.05)
	trigger_data.is_life_time_over_signal_enabled = true
	var trigger: BulletVolley2D = factory.spawn_volley(trigger_data)
	for k in 3:
		_flock_volley(_cluster(5, Vector2(k * 100, 0), k))
	var fired := [false]
	factory.life_time_over.connect(func(v: BulletVolley2D, _idx: Array) -> void:
		if v == trigger:
			fired[0] = true
			factory.reset_deferred()
	)
	for tick in 10:
		step_factory(1)
		if fired[0]:
			break
	await idle(2)
	assert_true(fired[0], "the handler ran")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "the deferred reset disarmed every flock")
	assert_true(factory.debug_assert_no_dangling()["ok"], "consistent")
