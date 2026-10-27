extends BlastTest
## Preview parity under rotation: dots sit on the rotated track, rotated
## corners land in global space, the fan cone centers on the marker's
## rotation + direction, and the unrotated layout stays exact.


func _seg_dist(p: Vector2, a: Vector2, b: Vector2) -> float:
	var ab := b - a
	var denom := ab.length_squared()
	if denom <= 0.00000001:
		return p.distance_to(a)
	var t: float = clampf((p - a).dot(ab) / denom, 0.0, 1.0)
	return p.distance_to(a + ab * t)


func _max_dev(dots: PackedVector2Array, track: PackedVector2Array) -> float:
	var worst := 0.0
	for d in dots:
		if not d.is_finite():
			continue
		var best := 1e30
		for k in range(track.size() - 1):
			if track[k].is_finite() and track[k + 1].is_finite():
				best = minf(best, _seg_dist(d, track[k], track[k + 1]))
		if best < 1e29:
			worst = maxf(worst, best)
	return worst


func test_rotated_rectangle_dots_on_track() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RECTANGLE, 4)
	sp.helper_rectangle_size = Vector2(300, 200)
	sp.rotation = PI / 4.0
	await idle(8)
	var dots := sp.debug_get_preview_dot_points()
	var track := sp.debug_get_preview_track_points()
	assert_eq(dots.size(), 4, "four dots snapshotted")
	assert_gte(track.size(), 4, "track snapshotted")
	assert_lt(_max_dev(dots, track), 2.0, "dots sit on the rotated track")
	var holder: Node2D = sp.get_node_or_null("~BlastBulletsPatternPreview")
	assert_not_null(holder, "preview holder present")
	var expected: Vector2 = sp.global_transform * Vector2(150, 100)
	var best := 1e30
	for q in track:
		if q.is_finite():
			best = minf(best, (holder.global_transform * q).distance_to(expected))
	assert_lt(best, 2.0, "rotated corner on the global track")


func test_rotated_star_dots_on_track() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 10)
	sp.rotation = PI / 3.0
	await idle(8)
	var dots := sp.debug_get_preview_dot_points()
	var track := sp.debug_get_preview_track_points()
	assert_eq(dots.size(), 10)
	assert_gte(track.size(), 10)
	assert_lt(_max_dev(dots, track), 3.0, "star dots on the rotated track")


func test_fan_cone_centered_in_holder_frame() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_FAN, 5)
	sp.helper_fan_spread = 1.0
	sp.helper_fan_direction_angle = 0.0
	sp.rotation = PI / 4.0
	await idle(8)
	var worst := 0.0
	var counted := 0
	for q in sp.debug_get_preview_track_points():
		if q.is_finite() and q.length() >= 140.0:
			counted += 1
			worst = maxf(worst, absf(angle_difference(q.angle(), 0.0)))
	assert_gt(counted, 0, "cone rim sampled")
	assert_lt(worst, 0.61, "cone centered on holder-frame 0")


func test_unrotated_layout_exact() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RECTANGLE, 4)
	sp.helper_rectangle_size = Vector2(300, 200)
	await idle(8)
	var track := sp.debug_get_preview_track_points()
	assert_lt(_max_dev(sp.debug_get_preview_dot_points(), track), 2.0, "unrotated coincidence exact")
	var nearest := 1e30
	for q in track:
		if q.is_finite():
			nearest = minf(nearest, q.distance_to(Vector2(150, 100)))
	assert_lt(nearest, 2.0, "axis corner on the track")


const ROTATION_SHAPES := ["GRID", "RING", "FAN", "SPIRAL", "LINE", "FLOWER", "ELLIPSE", "RAIN", "STAR_POLYGON",
	"MULTISPIRAL", "CROSS", "STAR", "HEART", "WAVE", "WATERFALL", "LATTICE", "ROSE", "COUNTER_SPIRAL", "LISSAJOUS",
	"CIRCLE", "RECTANGLE", "SQUARE", "POLYGON", "TRIANGLE", "TRAPEZOID", "DIAMOND"]


## Every helper either follows its aim ONCE (each bullet turns by the aim's
## turn around the aim and faces that much more) or stays fixed in the world
## (rain: a world direction): nothing turns twice or half-way. The Circle used to put bullet 0 at twice the aim's rotation,
## facing twice as far, the Ellipse ignored the aim, and the Lissajous bullet
## on the center took its facing from the aim. An odd count and an uneven
## angle keep a symmetric ring from hiding it.
func test_every_helper_turns_with_its_aim_once_or_not_at_all() -> void:
	var turn := 0.7
	var problems: Array = []
	var followers: Array = []
	for shape in ROTATION_SHAPES:
		var source: int = ClassDB.class_get_integer_constant("PatternSpawner2D", "PATTERN_FROM_HELPER_" + shape)
		var sp := make_spawner(null, source, 7)
		sp.position = Vector2(300, 200)
		if "helper_%s_seed" % shape.to_lower() in sp:
			sp.set("helper_%s_seed" % shape.to_lower(), 7) # deterministic jitter
		var flat: Array = sp.collect_spawn_transforms()
		var aim0: Transform2D = sp.global_transform
		sp.rotation = turn
		var turned: Array = sp.collect_spawn_transforms()
		var aim1: Transform2D = sp.global_transform
		var follows := flat.size() == turned.size() and not flat.is_empty()
		var fixed := follows
		for i in flat.size():
			var expected: Transform2D = aim1 * aim0.affine_inverse() * (flat[i] as Transform2D)
			var got: Transform2D = turned[i]
			if got.origin.distance_to(expected.origin) > 0.05 or absf(angle_difference(got.get_rotation(), expected.get_rotation())) > 0.001:
				follows = false
			if got.origin.distance_to((flat[i] as Transform2D).origin) > 0.05 or absf(angle_difference(got.get_rotation(), (flat[i] as Transform2D).get_rotation())) > 0.001:
				fixed = false
		if follows:
			followers.append(shape)
		elif not fixed:
			problems.append("%s neither follows its aim once nor stays fixed" % shape)
		sp.free()
	assert_eq("\n".join(problems), "", "no helper turns twice or half-way")
	assert_has(followers, "CIRCLE", "the Circle follows its aim once")
	assert_has(followers, "ELLIPSE", "the Ellipse follows its aim once")
	assert_has(followers, "RING", "Ring follows its aim")
	for shape in ["HEART", "ROSE", "FLOWER", "LISSAJOUS"]:
		assert_has(followers, shape, "%s follows its aim once (rotate_with_marker)" % shape)


## Shapes with a rotate_with_marker knob: off, the shape stays in the world
## (only the marker's position moves it); on, a turned aim adds to the shape's
## own base rotation exactly (the same pose either way).
const ROTATING := {
	"ELLIPSE": "helper_ellipse_rotation",
	"HEART": "helper_heart_base_rotation",
	"ROSE": "helper_rose_base_rotation",
	"FLOWER": "helper_flower_base_rotation",
	"LISSAJOUS": "",
}


func _origins(sp: BulletSpawner2D) -> Array:
	return sp.collect_spawn_transforms().map(func(t): return (t as Transform2D).origin)


func test_rotate_with_marker_off_keeps_each_shape_world_fixed(shape: String = use_parameters(ROTATING.keys())) -> void:
	var source: int = ClassDB.class_get_integer_constant("PatternSpawner2D", "PATTERN_FROM_HELPER_" + shape)
	var sp := make_spawner(null, source, 9)
	var knob: String = "helper_%s_rotate_with_marker" % shape.to_lower()
	assert_true(knob in sp, "%s exists" % knob)
	assert_true(sp.get(knob), "on by default")
	sp.set(knob, false)
	var flat := _origins(sp)
	sp.rotation = 1.1
	var turned := _origins(sp)
	for i in flat.size():
		assert_almost_eq(turned[i], flat[i], Vector2(0.05, 0.05), "%s off: bullet %d stays put" % [shape, i])
	var base: String = ROTATING[shape]
	if base == "":
		return
	sp.set(knob, true)
	sp.rotation = 0.0
	sp.set(base, 0.3)
	var by_knob := _origins(sp)
	sp.set(base, 0.0)
	sp.rotation = 0.3
	var by_aim := _origins(sp)
	for i in by_knob.size():
		assert_almost_eq(by_aim[i], by_knob[i], Vector2(0.05, 0.05), "%s: the aim adds to %s (bullet %d)" % [shape, base, i])


func test_every_flower_type_follows_its_aim_once(flower_type: int = use_parameters([0, 1, 2, 3, 4])) -> void:
	var sp := make_spawner(null, BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER, 11)
	sp.helper_flower_type = flower_type
	sp.position = Vector2(300, 200)
	var flat: Array = sp.collect_spawn_transforms()
	var aim0: Transform2D = sp.global_transform
	sp.rotation = 0.7
	var aim1: Transform2D = sp.global_transform
	var turned: Array = sp.collect_spawn_transforms()
	for i in flat.size():
		var expected: Transform2D = aim1 * aim0.affine_inverse() * (flat[i] as Transform2D)
		assert_almost_eq((turned[i] as Transform2D).origin, expected.origin, Vector2(0.05, 0.05), "type %d bullet %d moves around the aim" % [flower_type, i])
		assert_almost_eq(angle_difference((turned[i] as Transform2D).get_rotation(), expected.get_rotation()), 0.0, 0.001, "type %d bullet %d turns once" % [flower_type, i])


## base_rotation turns the rose and the rhodonea bloom: it used to shift the
## curve's parameter inside the radius as well, which traced the identical
## curve (the property did nothing).
func test_base_rotation_turns_the_rose_and_the_rhodonea_bloom() -> void:
	for setup in [["ROSE", "helper_rose_base_rotation", {"helper_rose_petals": 3}], ["ROSE", "helper_rose_base_rotation", {"helper_rose_petals": 4}], ["FLOWER", "helper_flower_base_rotation", {"helper_flower_type": 1, "helper_flower_petals": 5}]]:
		var source: int = ClassDB.class_get_integer_constant("PatternSpawner2D", "PATTERN_FROM_HELPER_" + setup[0])
		var sp := make_spawner(null, source, 15)
		for k in setup[2]:
			sp.set(k, setup[2][k])
		var flat := _origins(sp)
		sp.set(setup[1], 0.4)
		var turned := _origins(sp)
		var off: Array = []
		for i in flat.size():
			if (turned[i] as Vector2).distance_to((flat[i] as Vector2).rotated(0.4)) > 0.05:
				off.append(i)
		assert_eq(off, [], "%s %s: every bullet turned by base_rotation" % [setup[0], setup[2]])
		sp.free()


func test_ellipse_stays_world_fixed_with_rotate_with_marker_off() -> void:
	var sp := make_spawner(null, BulletSpawner2D.PATTERN_FROM_HELPER_ELLIPSE, 7)
	sp.helper_ellipse_rotate_with_marker = false
	sp.helper_ellipse_rotation = 0.3
	var flat: Array = sp.collect_spawn_transforms()
	sp.rotation = 1.1
	var turned: Array = sp.collect_spawn_transforms()
	for i in flat.size():
		assert_almost_eq((turned[i] as Transform2D).origin, (flat[i] as Transform2D).origin, Vector2(0.05, 0.05), "bullet %d stays put" % i)
	sp.rotation = 0.0
	sp.helper_ellipse_rotate_with_marker = true
	sp.helper_ellipse_rotation = 0.0
	var aligned: Array = sp.collect_spawn_transforms()
	sp.helper_ellipse_rotation = 0.3
	var by_knob: Array = sp.collect_spawn_transforms()
	sp.helper_ellipse_rotation = 0.0
	sp.rotation = 0.3
	var by_aim: Array = sp.collect_spawn_transforms()
	for i in aligned.size():
		assert_almost_eq((by_aim[i] as Transform2D).origin, (by_knob[i] as Transform2D).origin, Vector2(0.05, 0.05), "the aim adds to ellipse_rotation, bullet %d" % i)


func test_circle_and_ellipse_reuse_their_bake_on_a_turning_aim() -> void:
	for source in [BulletSpawner2D.PATTERN_FROM_HELPER_CIRCLE, BulletSpawner2D.PATTERN_FROM_HELPER_ELLIPSE, BulletSpawner2D.PATTERN_FROM_HELPER_HEART, BulletSpawner2D.PATTERN_FROM_HELPER_ROSE, BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER, BulletSpawner2D.PATTERN_FROM_HELPER_LISSAJOUS]:
		var sp := make_spawner(null, source, 16)
		sp.collect_spawn_transforms()
		var info: Dictionary = sp.debug_get_pattern_cache_info()
		assert_eq(int(info["shot_class"]), 2, "source %d is classified RIGID (2)" % source)
		var hits0: int = info["hits"]
		for k in 5:
			sp.rotation = 0.37 * (k + 1)
			sp.collect_spawn_transforms()
		info = sp.debug_get_pattern_cache_info()
		assert_eq(int(info["hits"]) - hits0, 5, "source %d: every turned shot is served from the bake" % source)
		sp.free()
