extends BlastTest
## Per-bullet colors: BulletVolleyData2D.use_bullet_colors / bullet_colors /
## tile_bullet_colors switch the volley's MultiMesh to a color format (12
## floats per instance: the 2D transform, then the Color). Entry i tints
## bullet i (strict, white where nothing covers a bullet; the tile box wraps
## a short array); the colors survive every transform upload, interpolation
## pass and parked wake, never leak through the pool (the format is part of
## the pool key), and split children wear their parent bullet's color. Off
## (the default) the MultiMesh carries no color data at all.


const RED := Color(1, 0, 0, 1)
const GREEN := Color(0, 1, 0, 1)
const BLUE := Color(0, 0, 1, 0.5)
const WHITE := Color(1, 1, 1, 1)


func _colored(n: int, colors: Array, tile := false, lifetime := 5.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 100.0, lifetime)
	d.bullet_colors = PackedColorArray(colors)
	d.tile_bullet_colors = tile
	return d


## The color drawn for instance i (floats 8..11 of a 12-float instance).
static func _drawn(v: BulletVolley2D, i: int) -> Color:
	var buf: PackedFloat32Array = v.multimesh.buffer
	var b := i * 12 + 8
	return Color(buf[b], buf[b + 1], buf[b + 2], buf[b + 3])


func _drawn_all(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		out.append(_drawn(v, i))
	return out


func _read_all(v: BulletVolley2D) -> Array:
	return Array(v.all_bullets_get_color())


func test_the_color_api_is_bound() -> void:
	var v := quick_volley(2)
	for m in ["is_using_bullet_colors", "get_bullet_color", "set_bullet_color", "all_bullets_get_color", "all_bullets_set_color", "all_bullets_set_colors"]:
		assert_true(v.has_method(m), "BulletVolley2D.%s is bound" % m)
	var d := BulletVolleyData2D.new()
	for p in ["use_bullet_colors", "bullet_colors", "tile_bullet_colors"]:
		assert_true(p in d, "BulletVolleyData2D.%s exists" % p)
	assert_false(d.use_bullet_colors, "off by default")
	assert_eq(d.bullet_colors.size(), 0, "no colors by default")
	assert_true("use_bullet_colors" in VolleyPoolKey2D.new(), "the pool key names the color format")


func test_off_by_default_the_multimesh_has_no_colors() -> void:
	var v := quick_volley(3)
	assert_false(v.is_using_bullet_colors(), "plain data: no per-bullet colors")
	assert_false(v.multimesh.use_colors, "the MultiMesh carries no color data")
	assert_eq(v.multimesh.buffer.size(), 3 * 8, "8 floats per instance")
	assert_eq(v.get_bullet_color(1), WHITE, "a bullet without a color reads white")
	v.set_bullet_color(0, RED)
	expect_error_sequence(["set_bullet_color: this volley has no per-bullet colors (spawn it with BulletVolleyData2D.use_bullet_colors on), keeping the old color."])
	v.all_bullets_set_color(RED)
	expect_error_sequence(["all_bullets_set_color: this volley has no per-bullet colors (spawn it with BulletVolleyData2D.use_bullet_colors on), keeping the old colors."])
	v.all_bullets_set_colors(PackedColorArray([RED]))
	expect_error_sequence(["all_bullets_set_colors: this volley has no per-bullet colors (spawn it with BulletVolleyData2D.use_bullet_colors on), keeping the old colors."])


func test_entry_i_tints_bullet_i_and_reaches_the_drawn_buffer() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_colored(3, [RED, GREEN, BLUE]))
	assert_true(v.is_using_bullet_colors(), "a color array turns the format on")
	assert_true(v.multimesh.use_colors, "the MultiMesh carries colors")
	assert_eq(v.multimesh.buffer.size(), 3 * 12, "12 floats per instance")
	assert_eq(_read_all(v), [RED, GREEN, BLUE], "entry i = bullet i")
	assert_eq(_drawn_all(v), [RED, GREEN, BLUE], "drawn at spawn")
	step_factory(3)
	assert_eq(_drawn_all(v), [RED, GREEN, BLUE], "every transform upload keeps the colors")
	# The transforms still decode where the bullets are (the stride moved).
	var buf: PackedFloat32Array = v.multimesh.buffer
	var inv := v.get_global_transform().affine_inverse()
	for i in 3:
		var b := i * 12
		var drawn := Vector2(buf[b + 3], buf[b + 7])
		assert_almost_eq(drawn.distance_to((inv * v.get_bullet_transform(i)).origin), 0.0, 0.01, "bullet %d drawn where it is" % i)


func test_the_switch_alone_draws_every_bullet_white() -> void:
	var d := H.make_volley_data(4)
	d.use_bullet_colors = true
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_true(v.is_using_bullet_colors(), "the switch alone turns the format on")
	assert_eq(_read_all(v), [WHITE, WHITE, WHITE, WHITE], "no entries: white")
	assert_eq(_drawn_all(v), [WHITE, WHITE, WHITE, WHITE], "drawn white")


func test_a_short_array_leaves_white_or_wraps_with_tile() -> void:
	var strict: BulletVolley2D = factory.spawn_volley(_colored(4, [RED, GREEN]))
	expect_warning_sequence(["BulletVolley2D: bullet_colors size (2) != bullets (4); uncovered bullets are white (check tile_bullet_colors to wrap, or provide one entry per bullet)."])
	assert_eq(_read_all(strict), [RED, GREEN, WHITE, WHITE], "strict: uncovered bullets white")
	var tiled: BulletVolley2D = factory.spawn_volley(_colored(5, [RED, GREEN], true))
	expect_warning_sequence(["BulletVolley2D: bullet_colors size (2) != bullets (5); uncovered bullets are white (tiling on: wrapping short array)."])
	assert_eq(_read_all(tiled), [RED, GREEN, RED, GREEN, RED], "tile wraps the short array")
	var long: BulletVolley2D = factory.spawn_volley(_colored(2, [RED, GREEN, BLUE]))
	expect_warning_sequence(["BulletVolley2D: bullet_colors size (3) != bullets (2); uncovered bullets are white (check tile_bullet_colors to wrap, or provide one entry per bullet)."])
	assert_eq(_read_all(long), [RED, GREEN], "extra entries are ignored")


func test_runtime_setters_write_read_and_draw() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_colored(4, [WHITE], true))
	expect_warning_sequence(["BulletVolley2D: bullet_colors size (1) != bullets (4); uncovered bullets are white (tiling on: wrapping short array)."])
	v.set_bullet_color(2, RED)
	assert_eq(v.get_bullet_color(2), RED, "set then get")
	v.all_bullets_set_color(GREEN, 0, 1)
	assert_eq(_read_all(v), [GREEN, GREEN, RED, WHITE], "a range writes only its bullets")
	v.all_bullets_set_colors(PackedColorArray([BLUE]))
	assert_eq(_read_all(v), [BLUE, GREEN, RED, WHITE], "strict array: uncovered bullets keep their color")
	v.all_bullets_set_colors(PackedColorArray([RED, BLUE]), true)
	assert_eq(_read_all(v), [RED, BLUE, RED, BLUE], "tile wraps")
	step_factory(1)
	assert_eq(_drawn_all(v), [RED, BLUE, RED, BLUE], "the next upload draws them")


func test_setters_reject_and_keep() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_colored(2, [RED, GREEN]))
	v.set_bullet_color(0, Color(NAN, 0, 0, 1))
	expect_error_sequence(["set_bullet_color: new_color must be finite, keeping the old color."])
	v.set_bullet_color(5, BLUE)
	expect_error_sequence(["Invalid bullet index in set_bullet_color: 5 (amount_bullets: 2)"])
	v.all_bullets_set_color(Color(0, INF, 0, 1))
	expect_error_sequence(["all_bullets_set_color: new_color must be finite, keeping the old colors."])
	v.all_bullets_set_colors(PackedColorArray([BLUE, Color(0, 0, 0, NAN)]))
	expect_error_sequence(["all_bullets_set_colors: new_colors must hold finite colors, keeping the old colors."])
	assert_eq(_read_all(v), [RED, GREEN], "every refusal kept the colors")
	assert_eq(v.get_bullet_color(-1), WHITE, "an invalid index reads white")
	expect_error_sequence(["Invalid bullet index in get_bullet_color: -1 (amount_bullets: 2)"])
	var d := BulletVolleyData2D.new()
	d.bullet_colors = PackedColorArray([RED])
	d.bullet_colors = PackedColorArray([GREEN, Color(INF, 0, 0, 1)])
	expect_error_sequence(["BulletVolleyData2D: bullet_colors must hold finite colors, keeping the old value."])
	assert_eq(Array(d.bullet_colors), [RED], "the data kept its colors")


func test_the_format_is_part_of_the_pool_key() -> void:
	var colored := _colored(3, [RED, GREEN, BLUE], false, 0.05)
	var plain := H.make_volley_data(3, 100.0, 0.05)
	assert_true(factory.debug_expected_pool_key(colored).use_bullet_colors, "colored data keys the colored bucket")
	assert_false(factory.debug_expected_pool_key(plain).use_bullet_colors, "plain data keys the plain bucket")
	assert_true(VolleyPoolKey2D.make(3, PhysicsServer2D.SHAPE_CIRCLE, true).use_bullet_colors, "make takes the format")
	assert_false(VolleyPoolKey2D.make(3, PhysicsServer2D.SHAPE_CIRCLE).use_bullet_colors, "make defaults to no colors")
	var first: BulletVolley2D = factory.spawn_volley(colored)
	step_factory(6) # 0.1 s: past the 0.05 s lifetime
	await idle(1)
	assert_true(factory.debug_get_pool_bucket(first).use_bullet_colors, "the expired volley waits in the colored bucket")
	var other: BulletVolley2D = factory.spawn_volley(plain)
	assert_ne(other, first, "plain data never takes a colored volley")
	assert_false(other.multimesh.use_colors, "and draws without colors")
	colored.bullet_colors = PackedColorArray([BLUE, BLUE])
	var again: BulletVolley2D = factory.spawn_volley(colored)
	expect_warning_sequence(["BulletVolley2D: bullet_colors size (2) != bullets (3); uncovered bullets are white (check tile_bullet_colors to wrap, or provide one entry per bullet)."])
	assert_eq(again, first, "colored data reuses the colored volley")
	assert_eq(_read_all(again), [BLUE, BLUE, WHITE], "the new life's colors, nothing of the last one")
	assert_eq(_drawn_all(again), [BLUE, BLUE, WHITE], "drawn on reuse")


func test_a_direct_enable_switches_the_format_both_ways() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(3, 100.0, 5.0))
	v.set_is_auto_pooling_enabled(false)
	v.clear_all_bullets()
	await idle(1)
	assert_true(v.enable_volley(_colored(3, [RED, GREEN, BLUE]), Vector2.ZERO, 0), "a parked plain volley enables with colors")
	assert_true(v.multimesh.use_colors, "the format switched")
	assert_eq(v.multimesh.instance_count, 3, "the instance count survived the switch")
	assert_eq(_drawn_all(v), [RED, GREEN, BLUE], "and the colors are drawn")
	assert_eq(v.debug_check_invariants(), "", "format and buffer agree")
	v.clear_all_bullets()
	await idle(1)
	assert_true(v.enable_volley(H.make_volley_data(3, 100.0, 5.0), Vector2.ZERO, 0), "and back without colors")
	assert_false(v.is_using_bullet_colors(), "colors off")
	assert_eq(v.multimesh.buffer.size(), 3 * 8, "8 floats per instance again")
	assert_eq(v.debug_check_invariants(), "", "format and buffer agree")


func test_a_parked_wake_keeps_its_colors() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_colored(2, [RED, GREEN]))
	v.set_is_auto_pooling_enabled(false)
	v.disable_bullet(0)
	v.disable_bullet(1)
	await idle(1)
	assert_true(v.is_parked(), "drained with pooling off: parked")
	v.enable_bullet(1)
	step_factory(1)
	assert_eq(v.get_bullet_color(1), GREEN, "the woken bullet kept its color")
	assert_eq(_drawn(v, 1), GREEN, "and draws it")
	# Drained by expiry (one bulk disable that zeroes the whole drawn buffer):
	# only the transforms go, the colors stay for a wake.
	var expiring: BulletVolley2D = factory.spawn_volley(_colored(2, [RED, BLUE], false, 0.05))
	expiring.set_is_auto_pooling_enabled(false)
	step_factory(6) # 0.1 s: past the 0.05 s lifetime
	await idle(1)
	assert_true(expiring.is_parked(), "expired with pooling off: parked")
	assert_eq(_drawn_all(expiring), [RED, BLUE], "the drained upload kept the colors")
	assert_eq(_read_all(expiring), [RED, BLUE], "and the readout")


func test_the_interpolation_pass_keeps_the_colors() -> void:
	factory.physics_interpolation = BulletFactory2D.INTERPOLATION_ON
	var v: BulletVolley2D = factory.spawn_volley(_colored(3, [RED, GREEN, BLUE]))
	step_factory(2)
	v.debug_run_interpolation_pass(0.5)
	assert_eq(_drawn_all(v), [RED, GREEN, BLUE], "the lerped upload carries the colors")


func test_split_children_wear_their_parent_color() -> void:
	var stage := BulletSplitStage2D.new()
	stage.count = 3
	stage.split_on_lifetime_over = true
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	var data := _colored(2, [RED, BLUE], false, 1.0 / 120.0)
	var parent: BulletVolley2D = factory.spawn_volley(data)
	assert_true(parent.split_set_data(chain, data), "armed")
	var children: Array = []
	factory.bullet_split.connect(func(_p, _c, child, _d) -> void: children.append(child))
	for i in 10:
		step_factory(1)
		await idle(1)
		if children.size() >= 2:
			break
	assert_eq(children.size(), 2, "each parent split once")
	var worn: Array = []
	for child in children:
		var c: BulletVolley2D = child
		assert_true(c.is_using_bullet_colors(), "children keep the color format")
		worn.append(_read_all(c))
	worn.sort_custom(func(a, b) -> bool: return a[0].r > b[0].r)
	assert_eq(worn, [[RED, RED, RED], [BLUE, BLUE, BLUE]], "every child wears its parent's color")
