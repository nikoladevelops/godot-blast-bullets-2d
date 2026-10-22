extends BlastTest
## One SpriteFrames contract for every system that draws an animation (bullet
## volleys, play_sprite_animation, effect trails, one-shot and manual effects,
## the six laser animations; src/sprite/): the same animation is picked by the
## same name rule, timed by the same speed and duration rule and refused the
## same way. Every rule runs over the whole consumer table, so a system added
## later is covered by adding its row.

const BULLET := "bullet"
const PLAY := "play_sprite_animation"
const PICKUP := "pickup"
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
const CONSUMERS := [BULLET, PLAY, PICKUP, TRAIL, ONE_SHOT, MANUAL, LASER_BODY, LASER_CHARGE, LASER_MUZZLE, LASER_TIP, LASER_PIERCE, LASER_WALL]
## Consumers that take an animation name and report a wrong one as an error
## (pickups warn: their own test; lasers play "default", else the first
## animation with frames).
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
	PICKUP: "BulletVolley2D",
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
		PICKUP:
			var pickup := BulletPickupData2D.new()
			pickup.sprite_frames = sf
			pickup.animation = anim
			var d := pickup.get_volley_data()
			d.transforms = [Transform2D.IDENTITY]
			out.volley = factory.spawn_volley(d)
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
	if kind in [BULLET, PLAY, PICKUP]:
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


func test_a_pickup_with_a_wrong_name_plays_the_first_animation_with_frames() -> void:
	# Pickups warn (a pickup always shows something): an unknown name plays
	# the first animation WITH frames, never the empty "default".
	var fire := _tex(5, 3, Color.RED)
	var built := await _build(PICKUP, _sf({"empty": [], "fire": [fire]}), &"nope")
	expect_warning_sequence(["BulletPickupData2D: animation \"nope\" does not exist in sprite_frames, playing the first animation."])
	assert_eq(_drawn(PICKUP, built), fire, "the first animation with frames")
	expect_no_errors()
	# An animation chosen by name but empty keeps the documented placeholder.
	built = await _build(PICKUP, _sf({"fire": [fire]}), &"default")
	assert_eq(_drawn(PICKUP, built), fire, "an unselected name skips the empty default silently")
	built = await _build(PICKUP, _sf({"empty": [], "fire": [fire]}), &"empty")
	expect_warning_sequence(["BulletPickupData2D: animation \"empty\" of sprite_frames has no frames, using the texture or the placeholder."])
	assert_ne(_drawn(PICKUP, built), fire, "the placeholder")
	# No frames anywhere: the placeholder, named after what was looked up.
	built = await _build(PICKUP, _sf({"empty": []}), &"nope")
	expect_warning_sequence([
		"BulletPickupData2D: animation \"nope\" does not exist in sprite_frames, playing the first animation.",
		"BulletPickupData2D: animation \"default\" of sprite_frames has no frames, using the texture or the placeholder.",
	])
	assert_not_null(_drawn(PICKUP, built), "the placeholder draws")
	expect_no_errors()


func test_a_frameless_sprite_frames_draws_nothing_with_one_error() -> void:
	for kind in CONSUMERS:
		if kind == PICKUP:
			continue # the placeholder (test above)
		var built := await _build(kind, _sf({"empty": []}), &"")
		if kind not in [BULLET, PLAY, PICKUP]:
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
		if kind not in [BULLET, PLAY, PICKUP] and _drawn(kind, built) != null:
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


func test_a_volley_without_sprite_frames_spawns_silently() -> void:
	# Frames are optional (a mesh or a shader draws): the spawn validation
	# warns once when nothing draws; the spawn itself never errors.
	var d := H.make_volley_data(1, 0.0, 30.0)
	d.sprite_frames = null
	d.mesh = QuadMesh.new()
	var v: BulletVolley2D = factory.spawn_volley(d)
	expect_no_errors("no animation, no error")
	assert_eq([v.get_texture() == null, v.get_sprite_frame_count(), v.get_sprite_frames() == null], [true, 0, true], "nothing baked")
	# Pool neutrality both ways: a framed life, then a frameless one.
	var a := _tex(4, 4, Color.RED)
	var framed := _volley_data(_sf({"default": [a]}), &"")
	factory.free_active_bullets()
	await idle(1)
	v = factory.spawn_volley(framed)
	assert_eq(v.get_texture(), a, "a framed life draws its frame")
	v.disable_bullet(0) # the last bullet out pools the volley
	await idle(1)
	var again: BulletVolley2D = factory.spawn_volley(d)
	assert_eq(again, v, "the pooled volley is reused")
	assert_eq([again.get_texture() == null, again.get_sprite_frame_count(), again.get_sprite_frames() == null], [true, 0, true], "the previous life's frames are gone")
	expect_no_errors()



# ---- Reading frames as Godot draws them (frame cache) --------------------------------

## An 8x8 sheet: blue, a red 3x2 block at (2, 2), a green pixel at (0, 0).
func _sheet_image() -> Image:
	var img := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
	img.fill(Color.BLUE)
	img.fill_rect(Rect2i(2, 2, 3, 2), Color.RED)
	img.set_pixel(0, 0, Color.GREEN)
	return img


func _atlas(sheet: Texture2D, region: Rect2, margin := Rect2()) -> AtlasTexture:
	var a := AtlasTexture.new()
	a.atlas = sheet
	a.region = region
	a.margin = margin
	return a


## The pixels of `img` that differ from `expect(x, y)`, as "x,y: got want".
func _pixel_misses(img: Image, size: Vector2i, expect: Callable) -> Array:
	if img == null:
		return ["no image"]
	if img.get_size() != size:
		return ["size %s instead of %s" % [img.get_size(), size]]
	var misses: Array = []
	for y in size.y:
		for x in size.x:
			var want: Color = expect.call(x, y)
			if not img.get_pixel(x, y).is_equal_approx(want):
				misses.append("%d,%d: %s %s" % [x, y, img.get_pixel(x, y), want])
	return misses


func test_an_atlas_frame_reads_exactly_its_region() -> void:
	var sheet := ImageTexture.create_from_image(_sheet_image())
	var red := func(_x, _y): return Color.RED
	var shown := factory.debug_read_frame(_atlas(sheet, Rect2(2, 2, 3, 2)))
	assert_eq(_pixel_misses(shown.get_image(), Vector2i(3, 2), red), [], "the red block only")
	# The margin box: the region at margin.position, transparent around it.
	var boxed := factory.debug_read_frame(_atlas(sheet, Rect2(2, 2, 3, 2), Rect2(1, 2, 4, 5)))
	var in_box := func(x, y): return Color.RED if x >= 1 and x < 4 and y >= 2 and y < 4 else Color(0, 0, 0, 0)
	assert_eq(_pixel_misses(boxed.get_image(), Vector2i(7, 7), in_box), [], "region + margin box, region at the margin's offset")
	assert_eq(boxed.get_size(), _atlas(sheet, Rect2(2, 2, 3, 2), Rect2(1, 2, 4, 5)).get_size(), "the size Godot draws it at")
	# A zero region size is the whole atlas (per axis, like Godot).
	var whole := factory.debug_read_frame(_atlas(sheet, Rect2()))
	var sheet_px := func(x, y): return _sheet_image().get_pixel(x, y)
	assert_eq(_pixel_misses(whole.get_image(), Vector2i(8, 8), sheet_px), [], "the whole sheet")
	var rows := factory.debug_read_frame(_atlas(sheet, Rect2(0, 2, 0, 2)))
	assert_eq(_pixel_misses(rows.get_image(), Vector2i(8, 2), func(x, y): return _sheet_image().get_pixel(x, y + 2)), [], "full width, two rows")
	# An atlas of an atlas reads the inner one as drawn (its margin box too).
	var inner := _atlas(sheet, Rect2(1, 1, 4, 4), Rect2(1, 0, 2, 0))
	var nested := factory.debug_read_frame(_atlas(inner, Rect2(2, 1, 3, 2)))
	assert_eq(_pixel_misses(nested.get_image(), Vector2i(3, 2), red), [], "the red block through two atlases")
	expect_no_errors()


func test_a_compressed_or_mipmapped_sheet_reads_its_region() -> void:
	var packed := _sheet_image()
	packed.compress(Image.COMPRESS_S3TC)
	assert_true(packed.is_compressed(), "a VRAM-compressed sheet")
	var shown := factory.debug_read_frame(_atlas(ImageTexture.create_from_image(packed), Rect2(4, 4, 4, 4)))
	assert_eq(_pixel_misses(shown.get_image(), Vector2i(4, 4), func(_x, _y): return Color.BLUE), [], "decompressed (S3TC keeps solid colors exact)")
	var mipped := _sheet_image()
	mipped.generate_mipmaps()
	var with_mips := factory.debug_read_frame(_atlas(ImageTexture.create_from_image(mipped), Rect2(2, 2, 3, 2)))
	assert_true(with_mips.get_image().has_mipmaps(), "mipmaps when the sheet has them")
	expect_no_errors()


func test_an_unreadable_atlas_frame_reads_null() -> void:
	var sheet := ImageTexture.create_from_image(_sheet_image())
	var bad := {
		"no atlas": AtlasTexture.new(),
		"NaN region": _atlas(sheet, Rect2(NAN, 0, 4, 4)),
		"INF size": _atlas(sheet, Rect2(0, 0, INF, 4)),
		"partly outside": _atlas(sheet, Rect2(6, 6, 4, 4)),
		"negative position": _atlas(sheet, Rect2(-1, 0, 4, 4)),
		"int overflow": _atlas(sheet, Rect2(2147483520, 0, 256, 4)),
		"negative size": _atlas(sheet, Rect2(0, 0, -4, 4)),
		"NaN margin": _atlas(sheet, Rect2(0, 0, 4, 4), Rect2(0, 0, NAN, 0)),
		"empty box": _atlas(sheet, Rect2(0, 0, 4, 4), Rect2(0, 0, -4, 0)),
		"huge margin": _atlas(sheet, Rect2(0, 0, 4, 4), Rect2(0, 0, 1e9, 0)),
		"empty sheet": _atlas(ImageTexture.new(), Rect2(0, 0, 4, 4)),
	}
	var problems: Array = []
	for name in bad:
		var shown := factory.debug_read_frame(bad[name])
		if shown != null:
			problems.append("%s: %s" % [name, shown])
	assert_eq(problems, [], "every unreadable frame reads null")
	expect_no_errors("unreadable is not an error here: the consumers report it")


func test_the_frame_cache_reads_a_sheet_once_and_follows_edits() -> void:
	var sheet := ImageTexture.create_from_image(_sheet_image())
	var frames: Array = []
	for i in 4:
		frames.append(_atlas(sheet, Rect2(i * 2, 0, 2, 2)))
	var stats: Dictionary = factory.debug_get_frame_cache_stats()
	var d := _volley_data(_sf({"default": frames}), &"")
	d.override_frame_color = true
	factory.spawn_volley(d)
	var after: Dictionary = factory.debug_get_frame_cache_stats()
	assert_eq(after.sheet_reads - stats.sheet_reads, 1, "four regions of one sheet: one readback")
	assert_eq(after.builds - stats.builds, 4, "one whitened frame per region")
	factory.spawn_volley(d)
	var again: Dictionary = factory.debug_get_frame_cache_stats()
	assert_eq([again.sheet_reads, again.builds, again.hits - after.hits], [after.sheet_reads, after.builds, 4], "a second spawn reads nothing")
	# An edited region rebuilds (the old frame never serves).
	frames[0].region = Rect2(2, 2, 2, 2)
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_eq(factory.debug_get_frame_cache_stats().builds - again.builds, 1, "only the edited frame rebuilds")
	assert_eq(v.get_texture().get_image().get_size(), Vector2i(2, 2), "the edited region")
	# A full reset drops every cached frame (art repainted in place re-bakes).
	factory.reset()
	await idle(2)
	assert_eq(factory.debug_get_frame_cache_stats().entries, 0, "reset clears the cache")
	expect_no_errors()


func test_whitened_frames_honor_the_margin_and_the_compressed_sheet() -> void:
	var packed := _sheet_image()
	packed.compress(Image.COMPRESS_S3TC)
	# [frame, size as drawn, a pixel that is white and opaque, one that is clear (or none)]
	var cases := {
		"margin": [_atlas(ImageTexture.create_from_image(_sheet_image()), Rect2(2, 2, 3, 2), Rect2(1, 1, 2, 2)), Vector2i(5, 4), Vector2i(1, 1), Vector2i(0, 0)],
		"zero region": [_atlas(ImageTexture.create_from_image(_sheet_image()), Rect2()), Vector2i(8, 8), Vector2i(7, 7), null],
		"compressed": [_atlas(ImageTexture.create_from_image(packed), Rect2(4, 4, 4, 4)), Vector2i(4, 4), Vector2i(3, 3), null],
	}
	var problems: Array = []
	for name in cases:
		var c: Array = cases[name]
		var d := _volley_data(_sf({"default": [c[0]]}), &"")
		d.override_frame_color = true
		var img: Image = factory.spawn_volley(d).get_texture().get_image()
		if img.get_size() != c[1]:
			problems.append("%s: size %s" % [name, img.get_size()])
			continue
		if not img.get_pixelv(c[2]).is_equal_approx(Color.WHITE):
			problems.append("%s: %s at %s, not opaque white" % [name, img.get_pixelv(c[2]), c[2]])
		if c[3] != null and img.get_pixelv(c[3]).a != 0.0:
			problems.append("%s: %s at %s, not clear" % [name, img.get_pixelv(c[3]), c[3]])
	assert_eq(problems, [], "whitened as drawn")
	expect_no_errors()


# ---- Drawing AtlasTexture frames (every consumer) --------------------------------------

func test_every_consumer_draws_an_atlas_frame_as_its_region() -> void:
	# A mesh draw hands the engine texture.get_rid(), and an AtlasTexture's
	# RID is its whole atlas: every consumer draws the baked region instead.
	var problems: Array = []
	for kind in CONSUMERS:
		var sheet := ImageTexture.create_from_image(_sheet_image())
		var frame := _atlas(sheet, Rect2(2, 2, 3, 2))
		var built := await _build(kind, _sf({"default": [frame]}), &"")
		var shown := _drawn(kind, built)
		if shown == null or shown == frame or shown == sheet:
			problems.append("%s draws %s" % [kind, shown])
			continue
		var misses := _pixel_misses(shown.get_image(), Vector2i(3, 2), func(_x, _y): return Color.RED)
		if not misses.is_empty():
			problems.append("%s: %s" % [kind, misses])
	assert_eq("\n".join(problems), "", "the red block, nothing else of the sheet")
	expect_no_errors()


func test_two_regions_of_one_sheet_animate_from_one_readback() -> void:
	var sheet := ImageTexture.create_from_image(_sheet_image())
	var stats: Dictionary = factory.debug_get_frame_cache_stats()
	var d := _volley_data(_sf({"default": [_atlas(sheet, Rect2(2, 2, 3, 2)), _atlas(sheet, Rect2(5, 5, 3, 3))]}), &"")
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_eq(factory.debug_get_frame_cache_stats().sheet_reads - stats.sheet_reads, 1, "one readback for both regions")
	assert_eq(_pixel_misses(v.get_texture().get_image(), Vector2i(3, 2), func(_x, _y): return Color.RED), [], "frame 0: the red block")
	factory.debug_advance_time(0.25) # 4 fps
	assert_eq(_pixel_misses(v.get_texture().get_image(), Vector2i(3, 3), func(_x, _y): return Color.BLUE), [], "frame 1: a blue corner")
	factory.debug_advance_time(0.25)
	assert_eq(v.get_texture().get_image().get_size(), Vector2i(3, 2), "looping back to frame 0")
	expect_no_errors()


func test_a_frame_with_a_margin_sizes_the_quad_as_drawn() -> void:
	# Trimmed sheets: the quad is the frame's whole box (Sprite2D's size),
	# the region sits at its margin offset inside it.
	var frame := _atlas(ImageTexture.create_from_image(_sheet_image()), Rect2(2, 2, 3, 2), Rect2(1, 2, 4, 5))
	var d := _volley_data(_sf({"default": [frame]}), &"")
	d.texture_size = Vector2.ZERO # derive it from the first frame
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_eq((v.multimesh.mesh as QuadMesh).size, Vector2(7, 7), "the bullet quad")
	assert_eq(v.get_texture().get_size(), Vector2(7, 7), "the baked box")
	var built := await _build(MANUAL, _sf({"default": [frame]}), &"")
	var shard: MultiMeshInstance2D = built.shards[0]
	assert_eq((shard.multimesh.mesh as QuadMesh).size, Vector2(7, 7), "an effect shard's quad")
	var pickup := BulletPickupData2D.new()
	pickup.sprite_frames = _sf({"default": [frame]})
	assert_eq(pickup.get_volley_data().texture_size, Vector2(7, 7), "a pickup's size")
	expect_no_errors()


func test_an_unreadable_atlas_frame_draws_its_original_with_one_warning() -> void:
	# The region is outside its atlas: nothing to bake; the frame draws as it
	# is (its whole atlas), and the data is reported once, never per spawn.
	var problems: Array = []
	for kind in CONSUMERS:
		var frame := _atlas(ImageTexture.create_from_image(_sheet_image()), Rect2(6, 6, 4, 4))
		var sf := _sf({"default": [frame]})
		for shot in 2:
			var built := await _build(kind, sf, &"")
			if _drawn(kind, built) != frame:
				problems.append("%s draws %s" % [kind, _drawn(kind, built)])
		expect_warning_sequence([PREFIXES[kind] + ": animation 'default' frame 0 is an AtlasTexture whose region cannot be read, drawing its whole atlas."], kind)
	assert_eq("\n".join(problems), "", "the original frame")
	expect_no_errors()


func test_a_pooled_volley_never_keeps_a_previous_life_frames() -> void:
	var plain := _tex(4, 4, Color.GREEN)
	var atlas_data := _volley_data(_sf({"default": [_atlas(ImageTexture.create_from_image(_sheet_image()), Rect2(2, 2, 3, 2))]}), &"")
	var v: BulletVolley2D = factory.spawn_volley(atlas_data)
	var baked := v.get_texture()
	v.disable_bullet(0)
	await idle(1)
	var again: BulletVolley2D = factory.spawn_volley(_volley_data(_sf({"default": [plain]}), &""))
	assert_eq(again, v, "the pooled volley is reused")
	assert_eq(again.get_texture(), plain, "the plain frame draws itself (identity kept)")
	again.disable_bullet(0)
	await idle(1)
	var third: BulletVolley2D = factory.spawn_volley(atlas_data)
	assert_eq(third.get_texture(), baked, "the cached region again, no rebuild")
	expect_no_errors()


func test_populating_the_pool_reads_the_frames_ahead_of_the_first_shot() -> void:
	var d := _volley_data(_sf({"default": [_atlas(ImageTexture.create_from_image(_sheet_image()), Rect2(2, 2, 3, 2))]}), &"")
	factory.populate_bullets_pool(VolleyPoolKey2D.make(1, PhysicsServer2D.SHAPE_CIRCLE), d, 1) # H data: a circle shape
	var warm: Dictionary = factory.debug_get_frame_cache_stats()
	assert_eq(warm.builds, 1, "the region baked while populating")
	factory.spawn_volley(d)
	assert_eq(factory.debug_get_frame_cache_stats().sheet_reads, warm.sheet_reads, "the first shot reads nothing")
	expect_no_errors()


func test_a_tiled_laser_body_repeats_its_region_not_its_sheet() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)
	d.body_frames = _sf({"default": [_atlas(ImageTexture.create_from_image(_sheet_image()), Rect2(0, 0, 4, 2))]})
	d.body_mode = LaserData2D.BODY_TILE
	var t: Array[Transform2D] = [Transform2D.IDENTITY]
	var l := factory.spawn_laser(d, t)
	step_factory(1, DT)
	var uvs: PackedVector2Array = l.debug_get_body_mesh(1.0)["uvs"]
	assert_almost_eq(uvs[2].x - uvs[0].x, 200.0 / 4.0, 0.0001, "one repeat per 4 px region (the sheet is 8 px wide)")
	var body: Texture2D = l.debug_get_draw_state(1.0)[0]["body"]
	assert_false(body is AtlasTexture, "a texture of its own: repeat wraps it, not the sheet")
	var sheet := _sheet_image()
	assert_eq(_pixel_misses(body.get_image(), Vector2i(4, 2), func(x, y): return sheet.get_pixel(x, y)), [], "the region's pixels")
	expect_no_errors()
