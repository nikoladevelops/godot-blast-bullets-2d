extends BlastTest
## Teleporting mixes with every feature that rides or reads a bullet's pose.
## A teleport (teleport_bullet, teleport_shift_bullet, teleport_shift_all_
## bullets on the volley and the factory, set_bullet_transform) moves the
## bullet without travel: its trail layers and attachment move WITH it at
## once (running or paused, interpolation on or off), the interpolation
## history starts at the new pose (no streak), the jump is never a swept
## segment (no graze, no homing reach, no wall hit along it), a locked orbit
## keeps the new slot, homing steers from the new spot, a frozen bullet wakes
## where it was teleported, and a pooled handle refuses it.

const MOVES := ["teleport_bullet", "teleport_shift_bullet", "volley_shift_all", "factory_shift_all", "set_bullet_transform"]


func _trail_data(n := 2, speed := 100.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, speed, 30.0)
	d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW)]
	return d


## Moves bullet 0 of `v` by (40, 30) through `how`.
func _move(v: BulletVolley2D, how: String) -> void:
	var at: Vector2 = v.get_bullet_transform(0).origin
	match how:
		"teleport_bullet":
			v.teleport_bullet(0, at + Vector2(40, 30))
		"teleport_shift_bullet":
			v.teleport_shift_bullet(0, Vector2(40, 30))
		"volley_shift_all":
			v.teleport_shift_all_bullets(Vector2(40, 30))
		"factory_shift_all":
			factory.teleport_shift_all_bullets(Vector2(40, 30))
		"set_bullet_transform":
			v.set_bullet_transform(0, Transform2D(0.0, at + Vector2(40, 30)), false)


func _trail_problem(v: BulletVolley2D, label: String) -> String:
	for i in v.get_amount_bullets():
		var trail: Vector2 = v.debug_get_trail_transform(0, i).origin
		var at: Vector2 = v.get_bullet_transform(i).origin
		if trail.distance_to(at) > 0.01:
			return "%s: bullet %d at %s, its trail at %s" % [label, i, at, trail]
	return ""


func test_the_trail_moves_with_the_teleport(how: String = use_parameters(MOVES)) -> void:
	var problems: Array = []
	for paused in [false, true]:
		for interpolation in [false, true]:
			factory.use_physics_interpolation = interpolation
			var v: BulletVolley2D = factory.spawn_volley(_trail_data())
			await physics(3)
			await idle(1)
			factory.set_is_factory_processing_bullets(not paused)
			_move(v, how)
			var label := "%s paused=%s interpolation=%s" % [how, paused, interpolation]
			var p := _trail_problem(v, label)
			if p != "":
				problems.append(p)
			if interpolation and v.debug_get_previous_origin(0).distance_to(v.get_bullet_transform(0).origin) > 0.01:
				problems.append("%s: the interpolation history did not restart (streak from %s)" % [label, v.debug_get_previous_origin(0)])
			factory.set_is_factory_processing_bullets(true)
			v.clear_all_bullets()
			await idle(1)
	assert_eq("\n".join(problems), "", how)


## One bullet at `from` flying +X at `speed`, circle r4 hitbox, hitting
## make_wall's layer 3 (static bodies need monitorable) without ever dying
## of it.
func _flyer(from: Vector2, speed := 60.0) -> BulletVolley2D:
	var d := H.make_volley_data(1, speed, 30.0)
	d.transforms = [Transform2D(0.0, from)]
	d.collision_shape = H.make_circle_shape(4.0)
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.bullet_max_collision_count = 0
	return factory.spawn_volley(d)


func test_a_teleport_is_never_a_swept_segment() -> void:
	# A graze target and a homing target sit right on the jump line: neither
	# rings nor reaches (the next tick's segment starts where the bullet
	# landed), while a bullet flying the same line does both.
	var mid := make_graze_target(Vector2(200, 0), &"jump_targets")
	for teleport in [true, false]:
		var v := _flyer(Vector2(0, 0), 60.0 if teleport else 12000.0)
		v.graze_set_zones([H.make_graze_zone([10.0])], &"jump_targets")
		v.homing_take_control_of_texture_rotation = true
		v.homing_distance_before_reached = 10.0
		v.shared_homing_deque_push_back_node2d_target(mid)
		var grazes := [0]
		var reaches := [0]
		factory.bullet_grazed.connect(func(_t, vv, _i, _z, _r) -> void:
			if vv == v:
				grazes[0] += 1)
		v.bullet_homing_target_reached.connect(func(_vv, _i, _t, _p) -> void: reaches[0] += 1)
		if teleport:
			v.teleport_bullet(0, Vector2(400, 0))
		step_factory(1)
		var label := "teleport" if teleport else "one fast tick across"
		assert_eq(grazes[0], 0 if teleport else 1, "%s: grazes on the line" % label)
		assert_eq(reaches[0], 0 if teleport else 1, "%s: reaches on the line" % label)
		v.clear_all_bullets()
		await idle(1)


func test_a_teleport_through_a_wall_never_hits_into_a_wall_hits_once() -> void:
	make_wall(Vector2(200, 0), Vector2(20, 200), 4)
	var hits := {}
	factory.body_entered.connect(func(_b, v, _i) -> void: hits[v] = int(hits.get(v, 0)) + 1)
	var through := _flyer(Vector2(0, 0), 0.0)
	var into := _flyer(Vector2(0, 40), 0.0)
	await physics(2)
	through.teleport_bullet(0, Vector2(400, 0))
	into.teleport_bullet(0, Vector2(200, 40))
	for f in 6:
		await physics(1)
	assert_eq(int(hits.get(through, 0)), 0, "a jump through the wall never touches it")
	assert_eq(int(hits.get(into, 0)), 1, "landing inside the wall is one hit")


func test_a_frozen_bullet_wakes_where_it_was_teleported() -> void:
	var v := _flyer(Vector2(0, 0), 60.0)
	v.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW)]
	step_factory(2)
	v.set_is_auto_pooling_enabled(false)
	v.disable_bullet(0, false)
	v.teleport_bullet(0, Vector2(300, 100))
	v.enable_bullet(0)
	assert_eq(v.get_bullet_transform(0).origin, Vector2(300, 100), "woken at the teleported pose")
	assert_lt(v.debug_get_trail_transform(0, 0).origin.distance_to(Vector2(300, 100)), 0.01, "its trail too")
	step_factory(1)
	assert_almost_eq(v.get_bullet_transform(0).origin, Vector2(301, 100), Vector2(0.01, 0.01), "and flies on from there")


func test_a_stuck_attachment_rides_the_teleport() -> void:
	var d := H.make_volley_data(1, 60.0, 30.0)
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	d.shared_bullet_attachment = make_probe_scene()
	var v: BulletVolley2D = factory.spawn_volley(d)
	step_factory(2)
	var attachment: Node2D = v.bullet_get_attachment(0)
	assert_not_null(attachment, "the bullet carries its attachment")
	var offset: Vector2 = attachment.global_position - v.get_bullet_transform(0).origin
	v.teleport_bullet(0, Vector2(250, -80))
	assert_almost_eq(attachment.global_position - v.get_bullet_transform(0).origin, offset, Vector2(0.01, 0.01), "the attachment keeps its offset")


func test_a_movement_pattern_continues_from_the_teleported_spot() -> void:
	var v := _flyer(Vector2(0, 0), 120.0)
	var c := Curve2D.new()
	for p in [Vector2(0, 0), Vector2(30, 10), Vector2(60, -10), Vector2(90, 0)]:
		c.add_point(p)
	v.set_shared_movement_pattern_curve(c)
	step_factory(10)
	v.teleport_bullet(0, Vector2(500, 500))
	step_factory(1)
	assert_lt(v.get_bullet_transform(0).origin.distance_to(Vector2(500, 500)), 120.0 / 60.0 + 0.01, "one step from the new spot (never pulled back along the pattern)")


func test_a_locked_orbit_holds_a_teleport_along_its_ring() -> void:
	var target := make_graze_target(Vector2(120, 0), &"orbit_jump")
	var sp := make_spawner(H.make_volley_data(1, 160.0, 10.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.helper_ring_radius = 10.0
	sp.homing_node_group = &"orbit_jump"
	sp.homing_smoothing = 6.0
	sp.homing_take_control_of_texture_rotation = true
	sp.homing_retarget_interval_sec = 1000.0
	sp.set_homing_enabled(true)
	sp.orbiting_radius = 40.0
	sp.orbiting_enabled = true
	var got: Array = []
	sp.volley_fired.connect(func(v, _n) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fires")
	var v: BulletVolley2D = got[0]
	for f in 120:
		await physics(1)
	assert_true(v.bullet_is_orbiting_locked(0), "circling")
	var radial: Vector2 = (v.get_bullet_transform(0).origin - target.global_position).normalized()
	var to: Vector2 = target.global_position + radial.rotated(0.8) * 40.0
	v.teleport_bullet(0, to)
	await physics(1)
	assert_almost_eq(v.get_bullet_transform(0).origin.distance_to(to), 160.0 / 60.0, 0.05, "one circling step on from the new slot")
	assert_true(v.bullet_is_orbiting_locked(0), "still locked")


## Pinned pending decision (asked in the stability pass 6 report): a teleport
## off the ring is pulled back onto the ring radius on the next tick (only
## the angle re-aims; the code comment in teleport_bullet_to says the radius
## would follow the move).
func test_a_locked_orbit_pulls_a_radial_teleport_back_onto_its_ring() -> void:
	var target := make_graze_target(Vector2(120, 0), &"orbit_radial")
	var sp := make_spawner(H.make_volley_data(1, 160.0, 10.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.helper_ring_radius = 10.0
	sp.homing_node_group = &"orbit_radial"
	sp.homing_smoothing = 6.0
	sp.homing_take_control_of_texture_rotation = true
	sp.homing_retarget_interval_sec = 1000.0
	sp.set_homing_enabled(true)
	sp.orbiting_radius = 40.0
	sp.orbiting_enabled = true
	var got: Array = []
	sp.volley_fired.connect(func(v, _n) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fires")
	var v: BulletVolley2D = got[0]
	for f in 120:
		await physics(1)
	var radial: Vector2 = (v.get_bullet_transform(0).origin - target.global_position).normalized()
	v.teleport_bullet(0, target.global_position + radial * 70.0)
	await physics(1)
	assert_almost_eq(v.get_bullet_transform(0).origin.distance_to(target.global_position), 40.0, 0.05, "back on the 40 px ring")
	assert_true(v.bullet_is_orbiting_locked(0), "still locked")
