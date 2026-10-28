extends BlastTest
## Scale-in (BulletVolleyData2D.scale_in_sec / scale_in_from): bullets spawn
## at scale_in_from of their size and grow linearly to full size over
## scale_in_sec of the volley's (time-scaled) clock, landing on it exactly;
## the hitbox, a rotated or randomized basis and graze rings grow with them;
## nothing of the window survives the pool; 0 seconds (default) is off.

const DT := 0.0625 # exact binary ticks: the window lands on its steps exactly


func _data(n := 2, sec := 0.25, from := 0.2) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 0.0, 5.0)
	d.scale_in_sec = sec
	d.scale_in_from = from
	return d


func _scales(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		out.append(snappedf(v.get_bullet_transform(i).x.length(), 0.0001))
	return out


func test_knobs_and_their_checks() -> void:
	var d := BulletVolleyData2D.new()
	assert_eq([d.scale_in_sec, d.scale_in_from], [0.0, 0.1], "off by default, 10% when on")
	d.scale_in_sec = -1.0
	expect_error_sequence(["BulletVolleyData2D: scale_in_sec must be finite and >= 0, keeping the old value."])
	d.scale_in_sec = INF
	expect_error_sequence(["BulletVolleyData2D: scale_in_sec must be finite and >= 0, keeping the old value."])
	for bad in [0.0, 0.005, 1.5, NAN]:
		d.scale_in_from = bad
		expect_error_sequence(["BulletVolleyData2D: scale_in_from must be between 0.01 and 1, keeping the old value."])
	assert_eq([d.scale_in_sec, d.scale_in_from], [0.0, 0.1], "every refusal kept the old values")


func test_bullets_grow_linearly_and_land_on_full_size() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(2, 0.25, 0.2))
	assert_eq(_scales(v), [0.2, 0.2], "spawned at 20%")
	var seen: Array = []
	for i in 5:
		step_factory(1, DT)
		seen.append(_scales(v)[0])
	assert_eq(seen, [0.4, 0.6, 0.8, 1.0, 1.0], "a quarter of the way per 1/16 s tick, then full size for good")
	assert_almost_eq(v.get_bullet_transform(1).x.length(), 1.0, 1e-6, "lands on full size")


func test_off_spawns_full_size() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(2, 0.0, 0.2))
	assert_eq(_scales(v), [1.0, 1.0], "0 s: off")
	var full: BulletVolley2D = factory.spawn_volley(_data(2, 0.5, 1.0))
	assert_eq(_scales(full), [1.0, 1.0], "from 1: nothing to grow")


func test_a_rotated_and_scaled_bullet_keeps_its_shape_and_its_hitbox_grows() -> void:
	var d := _data(1, 0.25, 0.5)
	d.transforms = [Transform2D(0.7, Vector2(3, 2), 0.0, Vector2(10, 20))]
	var v: BulletVolley2D = factory.spawn_volley(d)
	var start := v.get_bullet_transform(0)
	assert_almost_eq(start.x.length(), 1.5, 1e-5, "x axis at 50% of 3")
	assert_almost_eq(start.y.length(), 1.0, 1e-5, "y axis at 50% of 2")
	assert_almost_eq(start.get_rotation(), 0.7, 1e-5, "the rotation is untouched")
	step_factory(4, DT)
	var end := v.get_bullet_transform(0)
	assert_almost_eq(end.x.length(), 3.0, 1e-5, "x lands on 3")
	assert_almost_eq(end.y.length(), 2.0, 1e-5, "y lands on 2")
	assert_almost_eq(end.get_rotation(), 0.7, 1e-5, "still 0.7 rad")


func test_the_hitbox_grows_with_the_bullet(rotate_only: bool = use_parameters([false, true])) -> void:
	# A 4 px circle on a 20x bullet spawned at 10%: 8 px of reach at spawn,
	# 80 px at full size. The wall's near edge is 60 px away.
	var d := _data(1, 0.25, 0.1)
	d.transforms = [Transform2D(0.0, Vector2(20, 20), 0.0, Vector2(300, 300))]
	d.collision_shape = H.make_circle_shape(4.0)
	d.set_collision_mask_from_array([3])
	d.monitorable = true
	d.bullet_max_collision_count = 0
	d.rotate_only_textures = rotate_only # the shape keeps its own basis: it must grow too
	var hits: Array = []
	factory.body_entered.connect(func(_b, _v, _i) -> void: hits.append(1))
	var wall := make_wall(Vector2(370, 300))
	await physics()
	factory.spawn_volley(d)
	for i in 4:
		await physics()
	assert_eq(hits.size(), 0, "still small: the wall 60 px away is not touched")
	for i in 30:
		await physics()
		if hits.size() > 0:
			break
	assert_gt(hits.size(), 0, "grown to 80 px of reach it touches the wall")
	wall.queue_free()


func test_graze_rings_follow_the_growth() -> void:
	make_graze_target(Vector2(30, 0))
	var log := H.record_graze(factory)
	var d := _data(1, 0.25, 0.25)
	d.transforms = [Transform2D(0.0, Vector2(4, 4), 0.0, Vector2.ZERO)]
	d.collision_shape = H.make_circle_shape(4.0)
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.graze_set_zones([H.make_graze_zone([20.0], true)], &"graze_targets")
	# Ring 20 + 4 px x scale: 1x at spawn (24 px, out of reach), 4x at the
	# end of the window (36 px, in reach of the target 30 px away).
	step_factory(1, DT)
	assert_eq(H.graze_kinds(log), [], "still small: out of reach")
	step_factory(3, DT)
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "grown: the ring reaches the target")


func test_a_frozen_volley_clock_holds_the_growth() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1, 0.25, 0.2))
	factory.time_scale = 0.0
	step_factory(4, DT)
	assert_eq(_scales(v), [0.2], "time scale 0: no growth")
	factory.time_scale = 1.0
	step_factory(4, DT)
	assert_eq(_scales(v), [1.0], "resumed: full size")


func test_a_handler_pause_mid_tick_never_stalls_the_growth() -> void:
	# A graze handler pauses the factory: the tick that grew and moved the
	# bullet returns early (its lifetime pass is skipped). The window runs on
	# the ticks that grew it, so the next tick grows by its full step.
	make_graze_target(Vector2(10, 0))
	var d := _data(1, 0.25, 0.2)
	d.transforms = H.transforms_at([Vector2.ZERO])
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.graze_set_zones([H.make_graze_zone([40.0])], &"graze_targets")
	var paused := [false]
	factory.bullet_grazed.connect(func(_t: Node2D, _v: BulletVolley2D, _i: int, _z: GrazeZone2D, _r: int) -> void:
		if not paused[0]:
			paused[0] = true
			factory.is_factory_processing_bullets = false)
	step_factory(1, DT)
	assert_true(paused[0], "the first tick grazed and paused")
	factory.is_factory_processing_bullets = true
	var seen: Array = [_scales(v)[0]]
	for i in 4:
		step_factory(1, DT)
		seen.append(_scales(v)[0])
	assert_eq(seen, [0.4, 0.6, 0.8, 1.0, 1.0], "one quarter per tick that moved the bullet, the paused one included")


func test_nothing_of_the_window_survives_the_pool() -> void:
	var d := _data(2, 0.25, 0.2)
	d.max_life_time = DT
	var first: BulletVolley2D = factory.spawn_volley(d)
	step_factory(2, DT)
	await idle(1)
	assert_true(first.is_pooled(), "expired mid-window into the pool")
	var again: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 0.0, 5.0))
	assert_eq(again, first, "the same volley, a new life")
	assert_eq(_scales(again), [1.0, 1.0], "a plain life spawns full size")
	step_factory(4, DT)
	assert_eq(_scales(again), [1.0, 1.0], "and never grows")


func test_a_bullet_split_while_growing_gives_full_size_children() -> void:
	var d := _data(1, 1.0, 0.2) # a 1 s window: 40 % grown at 0.25 s
	var stage := BulletSplitStage2D.new()
	stage.split_on_lifetime_over = false
	stage.split_after_sec = 0.25
	stage.count = 2
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	var children: Array = []
	factory.bullet_split.connect(func(_pos, _cause, child: BulletVolley2D, _depth) -> void: children.append(child))
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.split_set_data(chain, d)
	step_factory(6, DT)
	assert_eq(children.size(), 1, "split once")
	step_factory(20, DT) # past the children's own 1 s window
	assert_eq(_scales(children[0]), [1.0, 1.0], "full size (x scale_multiplier 1), not the parent's 40 %")


func test_set_bullet_scale_while_growing_sets_the_full_grown_size() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1, 0.25, 0.2))
	step_factory(2, DT) # half way: 60 %
	v.set_bullet_scale(0, Vector2(2, 2))
	assert_eq(_scales(v), [1.2], "drawn at 2 x the growth so far")
	assert_almost_eq(v.get_bullet_scale(0).x, 1.2, 0.0001, "get_bullet_scale reports the drawn size")
	step_factory(4, DT)
	assert_eq(_scales(v), [2.0], "lands on the size it was given")
	v.set_bullet_scale(0, Vector2(3, 3))
	assert_eq(_scales(v), [3.0], "after the window a set is plain")


func test_set_bullet_transform_while_growing_sets_the_full_grown_size() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1, 0.25, 0.2))
	step_factory(2, DT)
	v.set_bullet_transform(0, Transform2D(0.5, Vector2(3, 3), 0.0, Vector2(40, 10)))
	assert_eq(_scales(v), [1.8], "drawn at 3 x the growth so far (60 %)")
	step_factory(4, DT)
	var t := v.get_bullet_transform(0)
	assert_eq([snappedf(t.x.length(), 0.0001), snappedf(t.get_rotation(), 0.0001)], [3.0, 0.5], "lands on the given scale, turned as given")


func test_the_hitbox_of_a_bullet_scaled_while_growing_matches_it(rotate_only: bool = use_parameters([false, true])) -> void:
	var d := _data(1, 0.25, 0.2)
	d.rotate_only_textures = rotate_only
	d.collision_shape = H.make_circle_shape(10.0)
	var v: BulletVolley2D = factory.spawn_volley(d)
	step_factory(2, DT)
	v.set_bullet_scale(0, Vector2(2, 2))
	step_factory(4, DT)
	assert_almost_eq(v.debug_get_bullet_shape_transform(0).x.length(), 2.0, 0.0001, "the hitbox lands on 2 x too")


func test_a_bullet_converted_while_growing_becomes_a_full_size_pickup() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1, 1.0, 0.2))
	step_factory(4, DT) # 25 % into the window: drawn at 40 %
	var pickup := BulletPickupData2D.new() # scale_multiplier 1
	factory.convert_all_bullets_to_pickups(pickup, Vector2(0, 500))
	var pickups: Array = factory.get_active_volleys().filter(func(x: BulletVolley2D) -> bool: return x != v and not x.is_pooled())
	assert_eq(pickups.size(), 1, "one pickup volley")
	assert_almost_eq(pickups[0].get_bullet_transform(0).x.length(), 1.0, 0.0001, "the pickup is full size, not 40 % of it")
