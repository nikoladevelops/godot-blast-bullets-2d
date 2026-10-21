extends BlastTest
## Laser drawing: debug_get_draw_state reports exactly what _draw uses. A
## straight beam is drawn from last tick's muzzle, angle and length lerped to
## this tick's (a sweeping beam stays on its muzzle mid-frame, a following one
## lerps its muzzle); a polyline with the same points lerps point by point.
## Charging draws the warning line and the charge frames; afterwards the body,
## muzzle and tip frames play from the growing start, each pierce from its own
## start, each wall impact from its block; one-shot animations hold their last
## frame. Every look draws headless without an error, interpolation on too.
## The bodies of a laser are ONE triangle array (debug_get_body_mesh reports
## exactly what _draw hands over): a strip of left/right vertex pairs along
## each beam, mitered where it turns by at most 90 degrees and split where it
## turns sharper, covering the beam's width and nothing beyond it; 8 color
## ramp pieces over the length; tiled UVs run along the length.

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


func _in_triangle(p: Vector2, a: Vector2, b: Vector2, c: Vector2) -> bool:
	var d1 := (p - b).cross(a - b)
	var d2 := (p - c).cross(b - c)
	var d3 := (p - a).cross(c - a)
	var neg := d1 < -1e-3 or d2 < -1e-3 or d3 < -1e-3
	var pos := d1 > 1e-3 or d2 > 1e-3 or d3 > 1e-3
	return not (neg and pos)


func _covered(mesh: Dictionary, p: Vector2) -> bool:
	var pts: PackedVector2Array = mesh["points"]
	var idx: PackedInt32Array = mesh["indices"]
	for t in range(0, idx.size(), 3):
		if _in_triangle(p, pts[idx[t]], pts[idx[t + 1]], pts[idx[t + 2]]):
			return true
	return false


## Every point of the beam's width is drawn (samples across each segment) and
## nothing past it (beside each segment's middle). Returns the misses.
func _coverage_misses(mesh: Dictionary, beam: PackedVector2Array, half: float) -> Array:
	var misses: Array = []
	for k in range(1, beam.size()):
		var a := beam[k - 1]
		var c := beam[k]
		var n := (c - a).normalized().orthogonal()
		for q in range(1, 10):
			var at := a.lerp(c, q / 10.0)
			for side in [-0.9, 0.0, 0.9]:
				if not _covered(mesh, at + n * half * side):
					misses.append(["uncovered", k, q, side])
		var mid := a.lerp(c, 0.5)
		for side in [-1.3, 1.3]:
			if _covered(mesh, mid + n * half * side):
				misses.append(["drawn beyond the width", k, side])
	return misses


func _corridor_beam() -> LaserVolley2D:
	make_wall(Vector2(400, -50), Vector2(1200, 20), 16)
	make_wall(Vector2(400, 50), Vector2(1200, 20), 16)
	await idle(2)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 2000.0)
	d.wall_mask = 16
	d.reflections = 8
	var l := _fire(d, Transform2D(deg_to_rad(45.0), Vector2.ZERO))
	step_factory(1, DT)
	return l


func test_a_straight_body_is_one_quad_of_the_beam_width() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0), Transform2D(0.0, Vector2(50, 50)))
	step_factory(1, DT)
	var mesh: Dictionary = l.debug_get_body_mesh(1.0)
	assert_eq(Array(mesh["points"]), [Vector2(50, 56), Vector2(50, 44), Vector2(250, 56), Vector2(250, 44)], "left/right at the muzzle and the end, half the width (12) off the beam")
	assert_eq(Array(mesh["indices"]), [0, 1, 2, 1, 3, 2], "two triangles")
	assert_eq(Array(mesh["uvs"]), [Vector2(0, 0), Vector2(0, 1), Vector2(1, 0), Vector2(1, 1)], "stretched: u along the length, v across")
	assert_eq(Array(mesh["colors"]), [Color.WHITE, Color.WHITE, Color.WHITE, Color.WHITE], "the modulate")


func test_a_color_ramp_samples_the_body_in_eight_pieces() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)
	d.color_ramp = Gradient.new() # black -> white
	d.modulate = Color(1.0, 0.5, 0.25)
	var l := _fire(d)
	step_factory(1, DT)
	var mesh: Dictionary = l.debug_get_body_mesh(1.0)
	var colors: PackedColorArray = mesh["colors"]
	var uvs: PackedVector2Array = mesh["uvs"]
	assert_eq(colors.size(), 18, "9 vertex pairs: 8 pieces")
	var bad: Array = []
	for q in 9:
		var want := d.modulate * d.color_ramp.sample(q / 8.0)
		if not colors[2 * q].is_equal_approx(want) or not colors[2 * q + 1].is_equal_approx(want) or not is_equal_approx(uvs[2 * q].x, q / 8.0):
			bad.append(q)
	assert_eq(bad, [], "piece q: modulate x ramp(q / 8) on both sides, u = q / 8")


func test_a_reflected_body_joins_at_miters_and_covers_exactly_the_beam() -> void:
	var l: LaserVolley2D = await _corridor_beam()
	var beam := l.get_beam_points(0)
	assert_eq(beam.size(), 10, "8 reflections (90 deg turns)")
	var mesh: Dictionary = l.debug_get_body_mesh(1.0)
	assert_eq([(mesh["points"] as PackedVector2Array).size(), (mesh["indices"] as PackedInt32Array).size()], [20, 54], "one pair per point (mitered, never split), two triangles per segment")
	var pts: PackedVector2Array = mesh["points"]
	var off: Array = []
	for k in range(1, 9):
		for v in [pts[2 * k], pts[2 * k + 1]]:
			for seg in [[beam[k - 1], beam[k]], [beam[k], beam[k + 1]]]:
				var axis: Vector2 = (seg[1] - seg[0]).normalized()
				var dist := absf(axis.cross(v - seg[0]))
				if not is_equal_approx(dist, 6.0):
					off.append([k, dist])
	assert_eq(off, [], "a miter vertex sits half the width from both segments")
	assert_eq(_coverage_misses(mesh, beam, 6.0), [], "the whole width drawn, nothing beyond it")


func test_a_sharp_reflection_splits_the_strip() -> void:
	make_wall(Vector2(200, 0), Vector2(20, 600), 16)
	await idle(2)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0)
	d.wall_mask = 16
	d.reflections = 1
	var l := _fire(d, Transform2D(deg_to_rad(30.0), Vector2.ZERO))
	step_factory(1, DT)
	var beam := l.get_beam_points(0)
	assert_eq(beam.size(), 3, "one reflection: a 120 deg turn")
	var mesh: Dictionary = l.debug_get_body_mesh(1.0)
	assert_eq(Array(mesh["indices"]), [0, 1, 2, 1, 3, 2, 4, 5, 6, 5, 7, 6], "two quads, no triangle across the split")
	var pts: PackedVector2Array = mesh["points"]
	assert_eq([pts[2].distance_to(beam[1]), pts[4].distance_to(beam[1])].map(func(x: float) -> float: return snappedf(x, 0.001)), [6.0, 6.0], "both ends at the corner, each square to its own segment")
	assert_eq(_coverage_misses(mesh, beam, 6.0), [], "the whole width drawn, nothing beyond it")


func test_a_curvy_body_is_one_smooth_strip() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 300.0)
	d.type = LaserData2D.LASER_CURVY
	d.speed = 800.0
	d.wave_amplitude = 40.0
	d.wave_frequency = 1.0
	var l := _fire(d)
	step_factory(10, DT)
	var beam := l.get_beam_points(0)
	assert_gt(beam.size(), 5, "a bent body")
	var mesh: Dictionary = l.debug_get_body_mesh(1.0)
	assert_eq([(mesh["points"] as PackedVector2Array).size(), (mesh["indices"] as PackedInt32Array).size()], [beam.size() * 2, (beam.size() - 1) * 6], "gentle bends are mitered: one pair per point")
	assert_eq(_coverage_misses(mesh, beam, 6.0), [], "the whole width drawn, nothing beyond it")


func test_a_tiled_body_runs_its_texture_along_the_length() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)
	var sf := SpriteFrames.new()
	sf.add_frame(&"default", ImageTexture.create_from_image(Image.create(16, 4, false, Image.FORMAT_RGBA8)))
	d.body_frames = sf
	d.body_mode = LaserData2D.BODY_TILE
	d.body_scroll_speed = 32.0
	var l := _fire(d)
	step_factory(1, DT)
	var uvs: PackedVector2Array = l.debug_get_body_mesh(1.0)["uvs"]
	assert_almost_eq(uvs[2].x - uvs[0].x, 200.0 / 16.0, 0.0001, "one texture repeat per 16 px")
	var first := uvs[0].x
	step_factory(1, DT)
	uvs = l.debug_get_body_mesh(1.0)["uvs"]
	assert_almost_eq(uvs[0].x - first, -32.0 * DT / 16.0, 0.0001, "scrolling 32 px/s")


func test_no_body_while_charging_or_for_a_spent_beam() -> void:
	var d := H.make_laser(0.25, 0.0, 0.0, 0.0, 200.0)
	var l := _fire(d)
	step_factory(1, DT)
	assert_eq((l.debug_get_body_mesh(1.0)["indices"] as PackedInt32Array).size(), 0, "charging draws the warning line only")
	factory.despawn_bounds_rect = Rect2(-50, -50, 300, 100)
	factory.despawn_bounds_enabled = true
	var b := H.make_laser(0.0, 0.0, 0.0, 0.0, 80.0)
	b.type = LaserData2D.LASER_BOLT
	b.speed = 1600.0
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO), Transform2D(PI / 2.0, Vector2.ZERO)]
	var bolts := factory.spawn_laser(b, poses)
	step_factory(2, DT)
	var state: Array = bolts.debug_get_draw_state(1.0)
	assert_eq([state[0]["spent"], state[1]["spent"]], [false, true], "the downward bolt left the bounds")
	assert_eq((bolts.debug_get_body_mesh(1.0)["points"] as PackedVector2Array).size(), 4, "only the live bolt's quad")


func test_the_body_follows_the_interpolated_beam() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)
	d.sweep_speed_degrees = 160.0 # 10 deg per tick
	var l := _fire(d, Transform2D(0.0, Vector2(50, 50)))
	step_factory(2, DT)
	var mid := _pts(l, 0.5)
	var pts: PackedVector2Array = l.debug_get_body_mesh(0.5)["points"]
	assert_true(((pts[0] + pts[1]) * 0.5).is_equal_approx(mid[0]), "the first pair centers on the muzzle")
	assert_true(((pts[2] + pts[3]) * 0.5).is_equal_approx(mid[1]), "the last on the lerped end (15 deg)")
