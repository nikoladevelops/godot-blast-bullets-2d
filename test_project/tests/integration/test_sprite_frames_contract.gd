extends BlastTest
## One SpriteFrames contract for every system that draws an animation (bullet
## volleys, play_sprite_animation, pickups, effect trails, one-shot and manual
## effects, laser bodies and point animations; src/sprite/): the same animation
## is picked by the same name rule, timed by the same speed and duration rule,
## and drawn as Godot draws its frames. Every rule runs over the whole consumer
## table, so a system added later is covered by adding its row.

const BULLET := "bullet"
const PLAY := "play_sprite_animation"

## Consumers whose drawn texture is read right after the build.
const CONSUMERS := [BULLET, PLAY]

## Per consumer: the error a wrong name prints before falling back to `fire`.
const FALLBACK_ERRORS := {
	BULLET: "BulletVolley2D: missing animation 'nope', falling back to 'fire'.",
	PLAY: "BulletVolley2D: missing animation 'nope', falling back to 'fire'.",
}

## Per consumer: the error prefix of its animation reports.
const PREFIXES := {
	BULLET: "BulletVolley2D",
	PLAY: "BulletVolley2D",
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


## Builds `sf` / `anim` through one consumer; returns what draws it.
func _build(kind: String, sf: SpriteFrames, anim: StringName) -> Object:
	match kind:
		BULLET:
			return factory.spawn_volley(_volley_data(sf, anim))
		PLAY:
			var v: BulletVolley2D = factory.spawn_volley(_volley_data(H.make_sprite_frames(), &""))
			v.play_sprite_animation(sf, anim)
			return v
	return null


## The texture `drawer` (from _build) shows right now.
func _drawn(kind: String, drawer: Object) -> Texture2D:
	match kind:
		BULLET, PLAY:
			return (drawer as BulletVolley2D).get_texture()
	return null


func test_an_empty_default_never_hides_a_populated_animation() -> void:
	# Every fresh SpriteFrames has an empty "default": a populated animation
	# next to it is what an unselected (empty or "default") name plays.
	var problems: Array = []
	for kind in CONSUMERS:
		for name in [&"", &"default"]:
			var fire := _tex(5, 3, Color.RED)
			var drawer := _build(kind, _sf({"fire": [fire]}), name)
			var shown := _drawn(kind, drawer)
			if shown != fire:
				problems.append("%s '%s': %s instead of the fire frame" % [kind, name, shown])
			expect_no_errors("%s '%s' resolves silently" % [kind, name])
	assert_eq("\n".join(problems), "", "the populated animation plays")


func test_a_wrong_name_falls_back_to_the_first_animation_with_frames_with_one_error() -> void:
	var problems: Array = []
	for kind in CONSUMERS:
		var fire := _tex(5, 3, Color.RED)
		# "empty" exists without frames: it never counts as a fallback.
		var drawer := _build(kind, _sf({"empty": [], "fire": [fire]}), &"nope")
		if _drawn(kind, drawer) != fire:
			problems.append("%s: %s instead of the fire frame" % [kind, _drawn(kind, drawer)])
		expect_error_sequence([FALLBACK_ERRORS[kind]], kind)
	assert_eq("\n".join(problems), "", "the first animation with frames plays")


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


func test_a_non_finite_duration_counts_zero_with_one_error() -> void:
	var a := _tex(4, 4, Color.RED)
	var b := _tex(4, 4, Color.BLUE)
	var sf := _sf({"default": []}, 4.0, false)
	sf.add_frame(&"default", a, INF)
	sf.add_frame(&"default", b, NAN)
	sf.add_frame(&"default", a, 1.0)
	factory.spawn_volley(_volley_data(sf, &""))
	expect_error_sequence([
		"BulletVolley2D: animation 'default' frame 0 has non-finite duration, using 0.",
		"BulletVolley2D: animation 'default' frame 1 has non-finite duration, using 0.",
	])


func test_a_null_frame_refuses_the_animation_with_one_error() -> void:
	var problems: Array = []
	for kind in CONSUMERS:
		var sf := _sf({"default": [_tex(4, 4)]})
		sf.add_frame(&"default", null)
		_build(kind, sf, &"")
		var texts := [PREFIXES[kind] + ": animation 'default' frame 1 has null texture."]
		expect_error_sequence(texts, kind)
	assert_eq("\n".join(problems), "", "refused")
