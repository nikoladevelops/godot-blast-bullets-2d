extends BlastTest
## Laser drawing: debug_get_draw_state reports exactly what _draw uses. A
## straight beam is drawn from last tick's muzzle, angle and length lerped to
## this tick's (a sweeping beam stays on its muzzle mid-frame, a following one
## lerps its muzzle); a polyline with the same points lerps point by point.
## Charging draws the warning line and the charge frames; afterwards the body,
## muzzle and tip frames play from the growing start, each pierce from its own
## start, each wall impact from its block; one-shot animations hold their last
## frame. Every look draws headless without an error, interpolation on too.

const DT := 0.0625


func _frames(count: int, fps := 4.0, loop := true) -> SpriteFrames:
	var sf := SpriteFrames.new()
	sf.set_animation_speed(&"default", fps)
	sf.set_animation_loop(&"default", loop)
	for i in count:
		sf.add_frame(&"default", ImageTexture.create_from_image(Image.create(4 + i, 4, false, Image.FORMAT_RGBA8)))
	return sf


func _fire(data: LaserData2D, pose := Transform2D(0.0, Vector2.ZERO), anchor: Node2D = null) -> LaserVolley2D:
	var t: Array[Transform2D] = [pose]
	return factory.spawn_laser(data, t, anchor)


func _pts(l: LaserVolley2D, f: float) -> PackedVector2Array:
	return l.debug_get_draw_state(f)[0]["points"]


func test_a_sweeping_beam_stays_on_its_muzzle_mid_frame() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)
	d.sweep_speed_degrees = 160.0 # 10 deg per tick
	var l := _fire(d, Transform2D(0.0, Vector2(50, 50)))
	step_factory(2, DT)
	var before := _pts(l, 0.0)
	var mid := _pts(l, 0.5)
	var now := _pts(l, 1.0)
	assert_eq([before[0], mid[0], now[0]], [Vector2(50, 50), Vector2(50, 50), Vector2(50, 50)], "the muzzle never leaves its spot")
	assert_almost_eq(rad_to_deg((before[1] - before[0]).angle()), 10.0, 0.001, "last tick: 10 deg")
	assert_almost_eq(rad_to_deg((mid[1] - mid[0]).angle()), 15.0, 0.001, "half way: 15 deg")
	assert_almost_eq(rad_to_deg((now[1] - now[0]).angle()), 20.0, 0.001, "this tick: 20 deg")
	assert_almost_eq(mid[0].distance_to(mid[1]), 200.0, 0.01, "the full length mid-frame (no chord)")


func test_a_following_beam_lerps_its_muzzle() -> void:
	var anchor := Node2D.new()
	add(anchor)
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0, 100.0), Transform2D(0.0, Vector2.ZERO), anchor)
	step_factory(1, DT)
	anchor.position = Vector2(80, 0)
	step_factory(1, DT)
	assert_almost_eq(_pts(l, 0.5)[0].x, 40.0, 0.01, "half way between the ticks")


func test_a_polyline_lerps_point_by_point() -> void:
	make_wall(Vector2(200, 0), Vector2(20, 2000), 16)
	await idle(2)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 400.0)
	d.wall_mask = 16
	d.reflections = 1
	var anchor := Node2D.new()
	add(anchor)
	var l := _fire(d, Transform2D(deg_to_rad(30.0), Vector2.ZERO), anchor)
	step_factory(1, DT)
	var a := _pts(l, 1.0)
	anchor.position = Vector2(0, 20)
	step_factory(1, DT)
	var b := _pts(l, 1.0)
	var mid := _pts(l, 0.5)
	assert_eq([a.size(), b.size(), mid.size()], [3, 3, 3], "the same point count")
	for k in 3:
		assert_almost_eq(mid[k].distance_to(a[k].lerp(b[k], 0.5)), 0.0, 0.01, "point %d lerped" % k)


func test_charging_draws_the_warning_and_the_charge_frames() -> void:
	var d := H.make_laser(0.25, 0.0, 0.0, 0.0)
	d.charge_frames = _frames(2)
	d.body_frames = _frames(1)
	var l := _fire(d)
	step_factory(1, DT)
	var s: Dictionary = l.debug_get_draw_state(1.0)[0]
	assert_true(s["warning"], "the warning line")
	assert_eq(s["width"], 0.0, "no body while charging")
	assert_eq(s["charge"], d.charge_frames.get_frame_texture(&"default", 0), "the charge frames")
	assert_null(s["body"], "no body frame")
	step_factory(4, DT) # 0.3125 s: active
	s = l.debug_get_draw_state(1.0)[0]
	assert_false(s["warning"], "no warning once it fires")
	assert_null(s["charge"], "no charge frames")
	assert_eq(s["body"], d.body_frames.get_frame_texture(&"default", 0), "the body frame")


func test_animations_play_on_their_own_clocks() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0)
	d.body_frames = _frames(2, 4.0, true) # 0.25 s per frame, looping
	d.muzzle_frames = _frames(2, 4.0, false) # one-shot
	d.tip_frames = _frames(3, 4.0, true)
	var l := _fire(d)
	var body: Array = []
	var muzzle: Array = []
	var tip: Array = []
	for i in 9:
		step_factory(1, DT)
		var s: Dictionary = l.debug_get_draw_state(1.0)[0]
		body.append(int((s["body"] as Texture2D).get_size().x) - 4)
		muzzle.append(int((s["muzzle"] as Texture2D).get_size().x) - 4)
		tip.append(int((s["tip"] as Texture2D).get_size().x) - 4)
	# Ticks at 0.0625 .. 0.5625 s from the growing start (the first tick).
	assert_eq(body, [0, 0, 0, 1, 1, 1, 1, 0, 0], "looping: frame 1 from 0.25 s, frame 0 again from 0.5 s")
	assert_eq(muzzle, [0, 0, 0, 1, 1, 1, 1, 1, 1], "one-shot: holds its last frame")
	assert_eq(tip, [0, 0, 0, 1, 1, 1, 1, 2, 2], "its own frame count")


func test_pierce_frames_start_per_contact_and_impacts_per_block() -> void:
	var a := make_area(Vector2(100, 0), Vector2(20, 60))
	make_wall(Vector2(300, 0), Vector2(20, 200), 16)
	await idle(2)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0)
	d.wall_mask = 16
	d.pierce_frames = _frames(2)
	d.wall_impact_frames = _frames(2)
	var l := _fire(d)
	step_factory(5, DT) # 0.3125 s into both
	var s: Dictionary = l.debug_get_draw_state(1.0)[0]
	assert_eq(s["pierces"].size(), 1, "one pierce")
	assert_almost_eq((s["pierces"][0][0] as Vector2).x, 100.0, 0.01, "at the target on the beam")
	assert_eq(s["pierces"][0][1], d.pierce_frames.get_frame_texture(&"default", 1), "0.25 s+ into its own clock: frame 1")
	assert_eq(s["impacts"].size(), 1, "one wall impact")
	assert_almost_eq((s["impacts"][0][0] as Vector2).x, 290.0, 0.01, "on the wall's face")
	assert_ne(a, null)


func test_every_look_draws_headless_with_interpolation_on() -> void:
	factory.physics_interpolation = BulletFactory2D.INTERPOLATION_ON
	var d := H.make_laser(0.0625, 0.0625, 0.0, 0.0)
	d.body_frames = _frames(2)
	d.body_mode = LaserData2D.BODY_TILE
	d.body_scroll_speed = 200.0
	d.color_ramp = Gradient.new()
	d.material = CanvasItemMaterial.new()
	d.charge_frames = _frames(2)
	d.muzzle_frames = _frames(2)
	d.tip_frames = _frames(2)
	d.pierce_frames = _frames(2)
	d.wall_impact_frames = _frames(2)
	d.reflections = 2
	d.wall_mask = 16
	make_wall(Vector2(150, 0), Vector2(20, 400), 16)
	make_area(Vector2(60, 0), Vector2(20, 60))
	var t: Array[Transform2D] = [Transform2D(0.3, Vector2.ZERO), Transform2D(2.0, Vector2.ZERO)]
	var l := factory.spawn_laser(d, t)
	for i in 8:
		await idle(1)
	assert_true(factory.is_physics_interpolation_active(), "interpolation on")
	assert_eq(l.debug_check_invariants(), "", "finite geometry")
	assert_eq(l.get_texture_repeat(), CanvasItem.TEXTURE_REPEAT_ENABLED, "a tiled body repeats its texture")
