extends BlastTest
## Physics interpolation mixed with every way a bullet appears or moves
## without travel. With interpolation on, the drawn pose is the previous pose
## lerped toward the current one (the headless fraction is 0: the previous
## pose itself), so right after each event below the previous pose IS the new
## pose and the next drawn frame shows the bullet exactly there, never a
## streak from an earlier life, an old spot or the spawn point:
## a cold spawn, a pool reuse, a parked wake, a frozen bullet's wake, a
## spawner shot with a muzzle offset, split children, pickups, a bounce
## (drawn on its side of the wall), a lock onto an orbit. Disabled bullets are
## drawn nowhere (a zero transform).

const EVENTS := ["cold_spawn", "pool_reuse", "parked_wake", "frozen_wake", "muzzle_offset", "split_children", "pickups"]


static func _drawn(v: BulletVolley2D, i: int) -> Transform2D:
	var buf: PackedFloat32Array = v.multimesh.buffer
	var b := i * 8
	return Transform2D(Vector2(buf[b], buf[b + 4]), Vector2(buf[b + 1], buf[b + 5]), Vector2(buf[b + 3], buf[b + 7]))


## Every enabled bullet of `v` (or only those in `only`) drawn exactly at
## its pose by the next interpolation pass, every disabled one drawn as a
## zero transform.
func _drawn_problems(v: BulletVolley2D, label: String, only: Array = []) -> Array:
	v.debug_run_interpolation_pass()
	var inv := v.get_global_transform().affine_inverse()
	var out: Array = []
	for i in v.get_amount_bullets():
		if not only.is_empty() and not only.has(i):
			continue
		var got := _drawn(v, i)
		if v.is_bullet_status_enabled(i):
			var want: Vector2 = (inv * v.get_bullet_global_transform(i)).origin
			if got.origin.distance_to(want) > 0.01:
				out.append("%s: bullet %d drawn at %s, it is at %s (previous pose %s)" % [label, i, got.origin, want, v.debug_get_previous_origin(i)])
		elif got.x != Vector2.ZERO or got.y != Vector2.ZERO:
			out.append("%s: disabled bullet %d is still drawn (%s)" % [label, i, got])
	return out


func _data(n := 3, speed := 200.0, lifetime := 5.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, speed, lifetime)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.3 * i, Vector2(30.0 * i, 10.0 * i)))
	d.transforms = arr
	return d


func _moved(d: BulletVolleyData2D, by: Vector2) -> BulletVolleyData2D:
	var out := d.duplicate() as BulletVolleyData2D
	var arr: Array = []
	for t in d.transforms:
		arr.append((t as Transform2D).translated(by))
	out.transforms = arr
	return out


func test_every_new_or_moved_bullet_is_drawn_where_it_is(event: String = use_parameters(EVENTS)) -> void:
	factory.use_physics_interpolation = true
	var problems: Array = []
	match event:
		"cold_spawn":
			var v: BulletVolley2D = factory.spawn_volley(_data())
			problems.append_array(_drawn_problems(v, event))
		"pool_reuse":
			var first: BulletVolley2D = factory.spawn_volley(_data())
			step_factory(20)
			first.clear_all_bullets()
			await idle(1)
			factory.debug_reset_pool_stats()
			var v: BulletVolley2D = factory.spawn_volley(_moved(_data(), Vector2(400, 300)))
			assert_eq(int(factory.debug_get_pool_hit_stats()["hits"]), 1, "a pool hit")
			problems.append_array(_drawn_problems(v, event))
		"parked_wake":
			var v: BulletVolley2D = factory.spawn_volley(_data(3, 200.0, 0.2))
			v.set_is_auto_pooling_enabled(false)
			step_factory(20) # expires: parked
			assert_true(v.is_parked(), "parked")
			v.teleport_shift_all_bullets(Vector2(150, 0))
			v.enable_bullet(0)
			problems.append_array(_drawn_problems(v, event))
		"frozen_wake":
			var v: BulletVolley2D = factory.spawn_volley(_data())
			step_factory(5)
			v.disable_bullet(1, false)
			step_factory(10)
			v.enable_bullet(1)
			problems.append_array(_drawn_problems(v, event, [1])) # the others flew this tick: drawn at their previous pose
		"muzzle_offset":
			var sp := make_spawner(_data(), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 3)
			sp.spawn_position_offset = Vector2(60, -20)
			var got: Array = []
			sp.volley_fired.connect(func(vv, _n) -> void: got.append(vv), CONNECT_ONE_SHOT)
			assert_true(sp.shoot_once(), "fires")
			problems.append_array(_drawn_problems(got[0], event))
		"split_children":
			var d := _data(2, 150.0)
			var stage := BulletSplitStage2D.new()
			stage.count = 3
			stage.split_after_sec = 0.1
			var chain := BulletSplitData2D.new()
			chain.stages = [stage]
			var v: BulletVolley2D = factory.spawn_volley(d)
			v.split_set_data(chain, _data(3, 150.0)) # a factory volley names its children's data
			var children: Array = []
			# Checked in the handler: the children's first move is the next sweep.
			factory.bullet_split.connect(func(_p, _c, ch, _d) -> void:
				children.append(ch)
				problems.append_array(_drawn_problems(ch, event)))
			await physics(12) # the split queue flushes in the factory's physics step
			assert_gt(children.size(), 0, "split")
		"pickups":
			var v: BulletVolley2D = factory.spawn_volley(_data())
			step_factory(10)
			var made: Array = []
			factory.bullets_converted.connect(func(_n, list) -> void: made.append_array(list))
			var coin := BulletPickupData2D.new()
			factory.convert_all_bullets_to_pickups(coin, Vector2(500, 500))
			assert_gt(made.size(), 0, "converted")
			for p in made:
				problems.append_array(_drawn_problems(p, event))
			problems.append_array(_drawn_problems(v, event + " (the converted source)"))
	assert_eq("\n".join(problems.slice(0, 6)), "", event)


func test_a_bounce_is_drawn_on_its_side_of_the_wall() -> void:
	# The bounce tick keeps the drawn path continuous: what is drawn after a
	# bounce never sits behind the wall face.
	factory.use_physics_interpolation = true
	make_wall(Vector2(220, 0), Vector2(20, 400), 8)
	var d := H.make_volley_data(1, 600.0, 5.0)
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	d.collision_shape = H.make_circle_shape(4.0)
	d.monitorable = true
	d.set_collision_mask_from_array([4])
	d.set_bounce_mask_from_array([4])
	d.bounce_mode = 1
	d.bullet_max_collision_count = 0
	var v: BulletVolley2D = factory.spawn_volley(d)
	var worst := -INF
	var bounced := false
	for f in 60:
		await physics(1)
		await idle(1) # the render pass ran
		bounced = bounced or v.bullet_get_bounce_count(0) > 0
		if bounced:
			worst = maxf(worst, _drawn(v, 0).origin.x)
	assert_true(bounced, "it bounced")
	assert_lt(worst, 210.0 + 0.01, "never drawn inside or behind the wall face (x 210)")
