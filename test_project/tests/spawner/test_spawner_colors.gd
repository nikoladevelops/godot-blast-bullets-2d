extends BlastTest
## The spawner's Bullet Colors group: bullet_color_mode picks one color per
## bullet at shot time from bullet_color_gradient (empty = a red-to-magenta
## rainbow) - across the pattern (with an optional shift per shot), at random
## (seeded by bullet_random_seed), or one color per shot stepping along the
## gradient every bullet_color_cycle_shots. The colors reach the volley next
## to the transforms: spawn_data is never touched, a mode beats the data's
## own colors, Off fires the data's colors (or none). Colored shots pool in
## the colored bucket (pre-warm it with a colors key), split children wear
## their parent's color, the settings round-trip through a saved scene.


const SCENE_PATH := "user://colors_spawner_round_trip.tscn"
const RED := Color(1, 0, 0, 1)
const BLUE := Color(0, 0, 1, 1)
const GREEN := Color(0, 1, 0, 1)


func _red_to_blue() -> Gradient:
	var g := Gradient.new()
	g.offsets = PackedFloat32Array([0.0, 1.0])
	g.colors = PackedColorArray([RED, BLUE])
	return g


func _spawner(amount := 5) -> BulletSpawner2D:
	return make_spawner(H.make_volley_data(amount, 100.0, 5.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, amount)


func _fire(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fires")
	return got[0] if got.size() > 0 else null


func _colors(v: BulletVolley2D) -> Array:
	return Array(v.all_bullets_get_color())


## Joins every mismatch into one line (the runner prints one assert per test).
func _assert_colors(got: Array, want: Array, label: String) -> void:
	assert_eq(got.size(), want.size(), label + ": count")
	var bad: Array = []
	for i in mini(got.size(), want.size()):
		var a: Color = got[i]
		var b: Color = want[i]
		if absf(a.r - b.r) > 0.002 or absf(a.g - b.g) > 0.002 or absf(a.b - b.b) > 0.002 or absf(a.a - b.a) > 0.002:
			bad.append("%d: %s != %s" % [i, a, b])
	assert_eq(", ".join(bad), "", label)


func test_the_group_sits_after_bullets_and_gates_by_mode() -> void:
	var sp := _spawner()
	var group := ""
	var groups: Array = []
	var found := {}
	for p in sp.get_property_list():
		var name := str(p["name"])
		if int(p["usage"]) & PROPERTY_USAGE_GROUP:
			group = name
			groups.append(name)
		elif name.begins_with("bullet_color_"):
			found[name] = [group, str(p["hint_string"])]
	var at := groups.find("Bullet Colors")
	assert_eq(groups.slice(at - 1, at + 2), ["Randomization", "Bullet Colors", "Homing"], "the first of the bullet groups, after where and when it fires")
	assert_eq(found.get("bullet_color_mode", ["", ""]), ["Bullet Colors", "Off,Gradient Across Pattern,Random From Gradient,Gradient Per Shot"], "mode hint")
	assert_eq(found.get("bullet_color_gradient", ["", ""]), ["Bullet Colors", "Gradient"], "gradient type")
	assert_eq([BulletSpawner2D.COLOR_MODE_OFF, BulletSpawner2D.COLOR_MODE_GRADIENT_ACROSS_PATTERN, BulletSpawner2D.COLOR_MODE_RANDOM_FROM_GRADIENT, BulletSpawner2D.COLOR_MODE_GRADIENT_PER_SHOT], [0, 1, 2, 3], "serialized ids")
	assert_eq(sp.bullet_color_mode, BulletSpawner2D.COLOR_MODE_OFF, "off by default")
	assert_null(sp.bullet_color_gradient, "no gradient by default (the rainbow)")
	assert_eq([sp.bullet_color_shift_per_shot, sp.bullet_color_cycle_shots], [0.0, 6], "knob defaults")
	var shown := func() -> Array:
		var out: Array = []
		for n in [&"bullet_color_gradient", &"bullet_color_shift_per_shot", &"bullet_color_cycle_shots"]:
			out.append(is_editor_visible(sp, n))
		return out
	assert_eq(shown.call(), [false, false, false], "Off shows only the mode")
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_ACROSS_PATTERN
	assert_eq(shown.call(), [true, true, false], "Across: gradient + shift")
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_RANDOM_FROM_GRADIENT
	assert_eq(shown.call(), [true, false, false], "Random: gradient")
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_PER_SHOT
	assert_eq(shown.call(), [true, false, true], "Per Shot: gradient + cycle")


func test_off_fires_plain_volleys() -> void:
	var v := _fire(_spawner())
	assert_false(v.is_using_bullet_colors(), "no mode, no data colors: no color format")


func test_across_pattern_spreads_the_gradient_end_to_end() -> void:
	var sp := _spawner(5)
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_ACROSS_PATTERN
	sp.bullet_color_gradient = _red_to_blue()
	var v := _fire(sp)
	assert_true(v.is_using_bullet_colors(), "a colored volley")
	var want: Array = []
	for i in 5:
		want.append(RED.lerp(BLUE, i / 4.0))
	_assert_colors(_colors(v), want, "bullet i samples i / (n - 1)")
	assert_false(sp.spawn_data.use_bullet_colors, "spawn_data untouched (switch)")
	assert_eq(sp.spawn_data.bullet_colors.size(), 0, "spawn_data untouched (colors)")


func test_an_empty_gradient_is_a_rainbow() -> void:
	var sp := _spawner(4)
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_ACROSS_PATTERN
	var want: Array = []
	for i in 4:
		want.append(Color.from_hsv(i / 3.0 * 5.0 / 6.0, 1.0, 1.0))
	_assert_colors(_colors(_fire(sp)), want, "red through magenta")
	_assert_colors([sp.sample_bullet_color(0.0), sp.sample_bullet_color(1.0), sp.sample_bullet_color(-3.0), sp.sample_bullet_color(NAN)], [RED, Color.from_hsv(5.0 / 6.0, 1, 1), RED, RED], "sample clamps and survives NaN")


func test_a_shift_scrolls_the_colors_every_shot() -> void:
	var sp := _spawner(4)
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_ACROSS_PATTERN
	sp.bullet_color_gradient = _red_to_blue()
	sp.bullet_color_shift_per_shot = 0.25
	var first := _colors(_fire(sp))
	var second := _colors(_fire(sp))
	var want_first: Array = []
	for i in 4:
		want_first.append(RED.lerp(BLUE, i / 4.0))
	_assert_colors(first, want_first, "a shift wraps the gradient: bullet i samples i / n")
	_assert_colors(second, [first[1], first[2], first[3], first[0]], "the next shot moved a quarter along")
	sp.bullet_color_shift_per_shot = 1e300 # a huge shift stays finite
	for c in _colors(_fire(sp)):
		assert_true(is_finite(c.r) and is_finite(c.b), "finite colors under a huge shift")


func test_random_is_seeded_and_previewable() -> void:
	var a := _spawner(6)
	var b := _spawner(6)
	for sp in [a, b]:
		sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_RANDOM_FROM_GRADIENT
		sp.bullet_color_gradient = _red_to_blue()
		sp.bullet_random_seed = 77
	var preview: Array = Array(a.collect_shot_colors(6))
	var shot := _colors(_fire(a))
	_assert_colors(shot, preview, "collect_shot_colors = the next seeded shot")
	_assert_colors(_colors(_fire(b)), shot, "the same seed, the same colors")
	assert_ne(_colors(_fire(a)), shot, "the next shot rolls new colors")
	for c in shot:
		assert_almost_eq(c.r + c.b, 1.0, 0.002, "every color sits on the gradient")
	assert_eq(Array(_spawner().collect_shot_colors(3)), [], "Off previews nothing")


func test_per_shot_steps_one_color_per_shot() -> void:
	var sp := _spawner(3)
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_PER_SHOT
	sp.bullet_color_gradient = _red_to_blue()
	sp.bullet_color_cycle_shots = 3
	var purple := RED.lerp(BLUE, 0.5)
	for want in [RED, purple, BLUE, RED]:
		_assert_colors(_colors(_fire(sp)), [want, want, want], "shot k samples (k % 3) / 2")
	sp.bullet_color_cycle_shots = 1
	_assert_colors(_colors(_fire(sp)), [RED, RED, RED], "a cycle of 1 holds the start")


func test_a_mode_beats_the_data_colors_and_off_brings_them_back() -> void:
	var data := H.make_volley_data(3, 100.0, 5.0)
	data.bullet_colors = PackedColorArray([GREEN, GREEN, GREEN])
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 3)
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_PER_SHOT
	sp.bullet_color_gradient = _red_to_blue()
	_assert_colors(_colors(_fire(sp)), [RED, RED, RED], "the mode wins")
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_OFF
	_assert_colors(_colors(_fire(sp)), [GREEN, GREEN, GREEN], "Off: the data's own colors")


func test_setters_reject_and_keep() -> void:
	var sp := _spawner()
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_PER_SHOT
	sp.set("bullet_color_mode", 7)
	expect_error_sequence(["BulletSpawner2D: bullet_color_mode must be 0 (Off), 1 (Gradient Across Pattern), 2 (Random From Gradient) or 3 (Gradient Per Shot), keeping the old value."])
	sp.bullet_color_shift_per_shot = 0.5
	sp.bullet_color_shift_per_shot = NAN
	expect_error_sequence(["BulletSpawner2D: bullet_color_shift_per_shot must be finite, keeping the old value."])
	sp.bullet_color_cycle_shots = 0
	expect_error_sequence(["BulletSpawner2D: bullet_color_cycle_shots must be >= 1, keeping the old value."])
	assert_eq([sp.bullet_color_mode, sp.bullet_color_shift_per_shot, sp.bullet_color_cycle_shots], [BulletSpawner2D.COLOR_MODE_GRADIENT_PER_SHOT, 0.5, 6], "old values kept")


func test_colored_shots_use_the_colored_bucket_and_can_be_prewarmed() -> void:
	var sp := _spawner(4)
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_ACROSS_PATTERN
	var plain := H.make_volley_data(4, 100.0, 5.0)
	factory.populate_bullets_pool(VolleyPoolKey2D.make(4, PhysicsServer2D.SHAPE_CIRCLE, true), plain, 2)
	expect_no_errors()
	factory.debug_reset_pool_stats()
	var v := _fire(sp)
	assert_eq(factory.debug_get_pool_hit_stats()["hits"], 1, "the shot took a pre-warmed colored volley")
	assert_true(factory.debug_get_pool_bucket(v).use_bullet_colors, "it belongs to the colored bucket")
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_OFF
	var off := _fire(sp)
	assert_false(off.is_using_bullet_colors(), "Off never takes a colored volley")
	var colored_data := H.make_volley_data(4, 100.0, 5.0)
	colored_data.use_bullet_colors = true
	factory.populate_bullets_pool(VolleyPoolKey2D.make(4, PhysicsServer2D.SHAPE_CIRCLE), colored_data, 1)
	expect_error_sequence(["populate_bullets_pool key mismatch: the spawn data draws per-bullet colors but the key has use_bullet_colors off. No instances were created."])


func test_split_children_wear_the_spawner_color() -> void:
	var sp := make_spawner(H.make_volley_data(2, 0.0, 1.0 / 120.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_ACROSS_PATTERN
	sp.bullet_color_gradient = _red_to_blue()
	var stage := BulletSplitStage2D.new()
	stage.count = 2
	stage.split_on_lifetime_over = true
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	sp.split_data = chain
	sp.split_enabled = true
	var children: Array = []
	sp.bullet_split.connect(func(_p, _c, child, _d) -> void: children.append(child))
	_fire(sp)
	for i in 10:
		step_factory(1)
		await idle(1)
		if children.size() >= 2:
			break
	assert_eq(children.size(), 2, "each parent split")
	var worn: Array = []
	for child in children:
		worn.append(_colors(child))
	worn.sort_custom(func(a, b) -> bool: return a[0].r > b[0].r)
	assert_eq(worn, [[RED, RED], [BLUE, BLUE]], "children wear their parent's color")


func test_a_saved_scene_keeps_the_color_settings() -> void:
	var sp := _spawner()
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_PER_SHOT
	sp.bullet_color_gradient = _red_to_blue()
	sp.bullet_color_cycle_shots = 9
	sp.bullet_color_shift_per_shot = 0.125
	var holder := Node2D.new()
	add(holder)
	sp.reparent(holder)
	sp.owner = holder
	var scene := PackedScene.new()
	assert_eq(scene.pack(holder), OK, "packed")
	assert_eq(ResourceSaver.save(scene, SCENE_PATH), OK, "saved")
	var loaded := ResourceLoader.load(SCENE_PATH, "", ResourceLoader.CACHE_MODE_IGNORE) as PackedScene
	DirAccess.remove_absolute(ProjectSettings.globalize_path(SCENE_PATH))
	var copy := loaded.instantiate()
	var back := copy.get_child(0) as BulletSpawner2D
	assert_not_null(back, "the spawner came back")
	assert_eq([back.bullet_color_mode, back.bullet_color_cycle_shots, back.bullet_color_shift_per_shot], [BulletSpawner2D.COLOR_MODE_GRADIENT_PER_SHOT, 9, 0.125], "mode and knobs")
	assert_eq(Array(back.bullet_color_gradient.colors), [RED, BLUE], "gradient")
	copy.free()
