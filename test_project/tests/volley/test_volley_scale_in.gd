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
