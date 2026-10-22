extends BlastTest
## One SpriteFrames contract for every system that draws an animation (bullet
## volleys, play_sprite_animation, effect trails, one-shot and manual effects,
## the six laser animations; src/sprite/): the same animation is picked by the
## same name rule, timed by the same speed and duration rule and refused the
## same way. Every rule runs over the whole consumer table, so a system added
## later is covered by adding its row.

const BULLET := "bullet"
const PLAY := "play_sprite_animation"
const TRAIL := "trail layer"
const ONE_SHOT := "one-shot layer"
const MANUAL := "spawn_layer_effect"
const LASER_BODY := "laser body_frames"
const LASER_CHARGE := "laser charge_frames"
const LASER_MUZZLE := "laser muzzle_frames"
const LASER_TIP := "laser tip_frames"
const LASER_PIERCE := "laser pierce_frames"
const LASER_WALL := "laser wall_impact_frames"

## Every consumer: built from a SpriteFrames, its frame-0 texture read back.
const CONSUMERS := [BULLET, PLAY, TRAIL, ONE_SHOT, MANUAL, LASER_BODY, LASER_CHARGE, LASER_MUZZLE, LASER_TIP, LASER_PIERCE, LASER_WALL]
## Consumers that take an animation name (lasers play "default", else the
## first animation with frames).
const NAMED := [BULLET, PLAY, TRAIL, ONE_SHOT, MANUAL]
## Consumers that play an animation once and need playable time.
const ONE_SHOTS := [ONE_SHOT, MANUAL]
## Laser consumers: the debug_get_draw_state key of what they draw.
const LASER_KEYS := {
	LASER_BODY: "body",
	LASER_CHARGE: "charge",
	LASER_MUZZLE: "muzzle",
	LASER_TIP: "tip",
	LASER_PIERCE: "pierces",
	LASER_WALL: "impacts",
}
## Exact binary laser ticks (1/16 s): phases and frames land on their ticks.
const DT := 0.0625

## Per consumer: who its animation reports name.
const PREFIXES := {
	BULLET: "BulletVolley2D",
	PLAY: "BulletVolley2D",
	TRAIL: "BulletEffectLayerData2D",
	ONE_SHOT: "BulletEffectLayerData2D",
	MANUAL: "BulletEffectLayerData2D",
	LASER_BODY: "LaserData2D.body_frames",
	LASER_CHARGE: "LaserData2D.charge_frames",
	LASER_MUZZLE: "LaserData2D.muzzle_frames",
	LASER_TIP: "LaserData2D.tip_frames",
	LASER_PIERCE: "LaserData2D.pierce_frames",
	LASER_WALL: "LaserData2D.wall_impact_frames",
}


## A solid w x h texture.
func _tex(w: int, h: int, color := Color.WHITE) -> ImageTexture:
	var img := Image.create_empty(w, h, false, Image.FORMAT_RGBA8)
	img.fill(color)
	return ImageTexture.create_from_image(img)


## A SpriteFrames holding `anims` ({name: [textures]}); "default" stays empty
## unless listed (every fresh SpriteFrames has an empty "default").
func _sf(anims: Dictionary, fps := 4.0, loop := true) -> SpriteFrames:
	var sf := SpriteFrames.new()
	for a in anims:
		if not sf.has_animation(a):
			sf.add_animation(a)
		sf.set_animation_speed(a, fps)
		sf.set_animation_loop(a, loop)
		for t in anims[a]:
			sf.add_frame(a, t)
	return sf


func _volley_data(sf: SpriteFrames, anim: StringName) -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 0.0, 30.0)
	d.sprite_frames = sf
	d.animation = anim
	return d


func _layer(trigger: int, sf: SpriteFrames, anim: StringName) -> BulletEffectLayerData2D:
	var layer := BulletEffectLayerData2D.new()
	layer.trigger = trigger
	layer.sprite_frames = sf
	layer.animation = anim
	return layer


## The one-shot shard nodes of the factory (one per frame of every bake).
func _effect_shards() -> Array:
	var container := factory.get_node_or_null("SpriteEffectsContainer")
	return [] if container == null else container.get_children().filter(func(c): return c is MultiMeshInstance2D)


## A laser firing along +x for as long as it lives, `sf` on the consumer's
## property (a charge never ends; pierce and wall get their target).
func _laser(kind: String, sf: SpriteFrames) -> LaserVolley2D:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0)
	match kind:
		LASER_BODY:
			d.body_frames = sf
		LASER_CHARGE:
			d.charge_sec = 1000.0
			d.charge_frames = sf
		LASER_MUZZLE:
			d.muzzle_frames = sf
		LASER_TIP:
			d.tip_frames = sf
		LASER_PIERCE:
			d.pierce_frames = sf
		LASER_WALL:
			d.wall_mask = 16
			d.wall_impact_frames = sf
	var t: Array[Transform2D] = [Transform2D.IDENTITY]
	var l := factory.spawn_laser(d, t)
	step_factory(2, DT)
	return l


## Builds `sf` / `anim` through one consumer; returns what draws it:
## {"volley": the bullet volley, "shards": effect shard nodes in frame order,
## "laser": the laser}.
func _build(kind: String, sf: SpriteFrames, anim: StringName) -> Dictionary:
	var before := _effect_shards()
	var out := {"volley": null, "shards": [], "laser": null}
	match kind:
		BULLET:
			out.volley = factory.spawn_volley(_volley_data(sf, anim))
		PLAY:
			out.volley = factory.spawn_volley(_volley_data(H.make_sprite_frames(), &""))
			out.volley.play_sprite_animation(sf, anim)
		TRAIL:
			var d := _volley_data(H.make_sprite_frames(), &"")
			d.effect_layers = [_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW, sf, anim)]
			out.volley = factory.spawn_volley(d)
			out.shards = out.volley.get_children().filter(func(c): return c is MultiMeshInstance2D)
		ONE_SHOT:
			var d := _volley_data(H.make_sprite_frames(), &"")
			d.effect_layers = [_layer(BulletEffectLayerData2D.EFFECT_ON_SPAWN, sf, anim)]
			out.volley = factory.spawn_volley(d)
		MANUAL:
			factory.spawn_layer_effect(_layer(BulletEffectLayerData2D.EFFECT_ON_SPAWN, sf, anim), Transform2D.IDENTITY)
		LASER_PIERCE:
			make_area(Vector2(100, 0), Vector2(20, 60))
			await idle(2) # a new target is found by queries after one physics step
			out.laser = _laser(kind, sf)
		LASER_WALL:
			make_wall(Vector2(300, 0), Vector2(20, 200), 16)
			await idle(2)
			out.laser = _laser(kind, sf)
		_:
			out.laser = _laser(kind, sf)
	if kind in ONE_SHOTS:
		out.shards = _effect_shards().filter(func(c): return not before.has(c))
	return out


## The texture `built` (from _build) draws now: the volley's texture, the
## first effect shard's, or the laser's (the first pierce or impact).
func _drawn(kind: String, built: Dictionary) -> Texture2D:
	if kind in [BULLET, PLAY]:
		return (built.volley as BulletVolley2D).get_texture()
	if kind in LASER_KEYS:
		var state: Dictionary = (built.laser as LaserVolley2D).debug_get_draw_state(1.0)[0]
		var shown = state[LASER_KEYS[kind]]
		if shown is Array:
			return null if shown.is_empty() else shown[0][1]
		return shown
	return null if built.shards.is_empty() else (built.shards[0] as MultiMeshInstance2D).texture


func test_an_empty_default_never_hides_a_populated_animation() -> void:
	# Every fresh SpriteFrames has an empty "default": a populated animation
	# next to it is what an unselected (empty or "default") name plays.
	var problems: Array = []
	for kind in CONSUMERS:
		for name in [&"", &"default"]:
			var fire := _tex(5, 3, Color.RED)
			var built := await _build(kind, _sf({"fire": [fire]}), name)
			var shown := _drawn(kind, built)
			if shown != fire:
				problems.append("%s '%s': %s instead of the fire frame" % [kind, name, shown])
			expect_no_errors("%s '%s' resolves silently" % [kind, name])
	assert_eq("\n".join(problems), "", "the populated animation plays")


func test_a_wrong_name_falls_back_to_the_first_animation_with_frames_with_one_error() -> void:
	var problems: Array = []
	for kind in NAMED:
		var fire := _tex(5, 3, Color.RED)
		# "empty" exists without frames: it never counts as a fallback.
		var built := await _build(kind, _sf({"empty": [], "fire": [fire]}), &"nope")
		if _drawn(kind, built) != fire:
			problems.append("%s: %s instead of the fire frame" % [kind, _drawn(kind, built)])
		expect_error_sequence([PREFIXES[kind] + ": missing animation 'nope', falling back to 'fire'."], kind)
	assert_eq("\n".join(problems), "", "the first animation with frames plays")


func test_a_frameless_sprite_frames_draws_nothing_with_one_error() -> void:
	for kind in CONSUMERS:
		var built := await _build(kind, _sf({"empty": []}), &"")
		if kind not in [BULLET, PLAY]:
			assert_null(_drawn(kind, built), "%s draws nothing" % kind)
		expect_error_sequence([PREFIXES[kind] + ": sprite_frames has no animation with frames."], kind)


func test_speed_zero_holds_the_first_frame_silently() -> void:
	# Like AnimatedSprite2D: speed 0 never advances (no error, never finishes).
	var a := _tex(4, 4, Color.RED)
	var b := _tex(4, 4, Color.BLUE)
	var v: BulletVolley2D = factory.spawn_volley(_volley_data(_sf({"default": [a, b]}, 0.0, false), &""))
	watch_signals(v)
	for i in 8:
		factory.debug_advance_time(0.25)
	assert_eq([v.get_sprite_frame(), v.get_texture() == a, v.is_sprite_animation_finished()], [0, true, false], "frame 0 holds")
	assert_signal_emit_count(v, "sprite_animation_finished", 0, "a held one-shot never finishes")
	expect_no_errors("speed 0 is a valid speed")
	# Seeking still works and holds the chosen frame.
	assert_true(v.set_sprite_frame(1), "seek")
	factory.debug_advance_time(0.25)
	assert_eq([v.get_sprite_frame(), v.get_texture() == b], [1, true], "the chosen frame holds")


func test_speed_zero_holds_every_laser_animation_on_its_first_frame() -> void:
	var problems: Array = []
	for kind in LASER_KEYS:
		var a := _tex(4, 4, Color.RED)
		var built := await _build(kind, _sf({"default": [a, _tex(4, 4, Color.BLUE)]}, 0.0), &"")
		step_factory(20, DT) # 1.25 s: five frames at the 4 fps the other tests use
		if _drawn(kind, built) != a:
			problems.append("%s: %s" % [kind, _drawn(kind, built)])
	assert_eq("\n".join(problems), "", "frame 0 holds")
	expect_no_errors("speed 0 is a valid speed")


func test_speed_zero_shows_a_trail_on_its_first_frame() -> void:
	var a := _tex(4, 4, Color.RED)
	var b := _tex(4, 4, Color.BLUE)
	var built := await _build(TRAIL, _sf({"default": [a, b]}, 0.0), &"")
	assert_eq(built.shards.size(), 2, "both frames bake")
	for i in 8:
		factory.debug_advance_time(0.25)
	assert_eq([built.shards[0].visible, built.shards[1].visible], [true, false], "frame 0 shows, frame 1 never does")
	assert_eq(built.volley.debug_get_trail_transform(0, 0).origin, built.volley.get_bullet_transform(0).origin, "the trail follows its bullet")
	expect_no_errors("speed 0 is a valid trail speed")


func test_speed_zero_one_shots_have_no_playable_time() -> void:
	# A one-shot lives one pass of its animation: speed 0 gives it none.
	for kind in ONE_SHOTS:
		var built := await _build(kind, _sf({"default": [_tex(4, 4), _tex(4, 4)]}, 0.0), &"")
		assert_eq(built.shards.size(), 0, "%s: nothing baked" % kind)
		expect_error_sequence(["BulletEffectLayerData2D: baked zero playable time, layer renders nothing."], kind)


func test_a_non_finite_speed_reports_one_error_in_every_consumer() -> void:
	for kind in CONSUMERS:
		await _build(kind, _sf({"default": [_tex(4, 4)]}, NAN), &"")
		expect_error_sequence([PREFIXES[kind] + ": animation 'default' has invalid speed, using 1 fps."], kind)


func test_a_non_finite_speed_plays_at_one_fps_with_one_error() -> void:
	for speed in [NAN, INF]:
		var a := _tex(4, 4, Color.RED)
		var b := _tex(4, 4, Color.BLUE)
		var v: BulletVolley2D = factory.spawn_volley(_volley_data(_sf({"default": [a, b]}, speed, false), &""))
		expect_error_sequence(["BulletVolley2D: animation 'default' has invalid speed, using 1 fps."], str(speed))
		# 1 fps, durations 1: frame 1 after one second (0.25 s ticks are exact).
		for i in 3:
			factory.debug_advance_time(0.25)
		assert_eq(v.get_sprite_frame(), 0, "%s: still frame 0 at 0.75 s" % speed)
		factory.debug_advance_time(0.25)
		assert_eq(v.get_sprite_frame(), 1, "%s: frame 1 at 1 s" % speed)


func test_a_laser_plays_one_second_per_frame_at_a_non_finite_speed() -> void:
	var a := _tex(4, 4, Color.RED)
	var b := _tex(4, 4, Color.BLUE)
	var built := await _build(LASER_BODY, _sf({"default": [a, b]}, INF), &"")
	expect_error_sequence(["LaserData2D.body_frames: animation 'default' has invalid speed, using 1 fps."])
	step_factory(12, DT) # 0.875 s into the body clock
	assert_eq(_drawn(LASER_BODY, built), a, "frame 0 before one second")
	step_factory(4, DT)
	assert_eq(_drawn(LASER_BODY, built), b, "frame 1 after one second")


func test_a_one_shot_plays_one_second_per_frame_at_a_non_finite_speed() -> void:
	var built := await _build(MANUAL, _sf({"default": [_tex(4, 4), _tex(4, 4)]}, INF), &"")
	expect_error_sequence(["BulletEffectLayerData2D: animation 'default' has invalid speed, using 1 fps."])
	assert_eq(built.shards.size(), 2, "two frames")
	for i in 7:
		factory.debug_advance_time(0.25)
	assert_eq(factory.get_active_effect_count(), 1, "still playing at 1.75 s")
	for i in 2:
		factory.debug_advance_time(0.25)
	assert_eq(factory.get_active_effect_count(), 0, "two frames of one second each end by 2.25 s")


func test_a_non_finite_duration_counts_zero_with_one_error() -> void:
	# An INF frame never ends a one-shot; it counts 0 like a NaN one.
	for kind in CONSUMERS:
		var a := _tex(4, 4, Color.RED)
		var sf := _sf({"default": []}, 4.0, false)
		sf.add_frame(&"default", a, INF)
		sf.add_frame(&"default", a, NAN)
		sf.add_frame(&"default", a, 1.0)
		await _build(kind, sf, &"")
		expect_error_sequence([
			PREFIXES[kind] + ": animation 'default' frame 0 has non-finite duration, using 0.",
			PREFIXES[kind] + ": animation 'default' frame 1 has non-finite duration, using 0.",
		], kind)


func test_a_null_frame_refuses_the_animation_with_one_error() -> void:
	var problems: Array = []
	for kind in CONSUMERS:
		var sf := _sf({"default": [_tex(4, 4)]})
		sf.add_frame(&"default", null)
		var built := await _build(kind, sf, &"")
		if kind not in [BULLET, PLAY] and _drawn(kind, built) != null:
			problems.append("%s still draws %s" % [kind, _drawn(kind, built)])
		expect_error_sequence([PREFIXES[kind] + ": animation 'default' frame 1 has null texture."], kind)
	assert_eq("\n".join(problems), "", "a refused animation draws nothing")


func test_a_laser_reports_a_sprite_frames_problem_once() -> void:
	# A laser bakes per shot: the same broken SpriteFrames reports once per
	# property, never once per shot.
	var sf := _sf({"default": [_tex(4, 4)]}, NAN)
	sf.add_frame(&"default", null)
	for i in 3:
		await _build(LASER_BODY, sf, &"")
	expect_error_sequence([
		"LaserData2D.body_frames: animation 'default' has invalid speed, using 1 fps.",
		"LaserData2D.body_frames: animation 'default' frame 1 has null texture.",
	])
	await _build(LASER_TIP, sf, &"")
	expect_error_sequence([
		"LaserData2D.tip_frames: animation 'default' has invalid speed, using 1 fps.",
		"LaserData2D.tip_frames: animation 'default' frame 1 has null texture.",
	], "another property reports its own")
