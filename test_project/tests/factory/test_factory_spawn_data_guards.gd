extends BlastTest
## Spawn-data finiteness: fields folded into EVERY bullet transform reject
## NaN/Inf at the setter and keep the old value (a single NaN
## texture_rotation_radians used to turn a whole volley NaN), and a volley with
## a valid non-zero texture rotation stays finite. The four collision-shape
## fallbacks report once per spawn-data resource, never once per shot.


func _data(n: int = 2) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 100.0, 5.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2.ZERO))
	d.transforms = arr
	return d


func test_texture_rotation_rejects_nan_inf() -> void:
	var d := _data()
	d.texture_rotation_radians = 0.5
	assert_almost_eq(d.texture_rotation_radians, 0.5, 0.0001, "valid rotation stored")
	d.texture_rotation_radians = NAN
	expect_error_sequence(["BulletVolleyData2D: texture_rotation_radians must be finite, keeping the old value."])
	assert_almost_eq(d.texture_rotation_radians, 0.5, 0.0001, "NaN refused, old value kept")
	d.texture_rotation_radians = INF
	expect_error_sequence(["BulletVolleyData2D: texture_rotation_radians must be finite, keeping the old value."])
	assert_almost_eq(d.texture_rotation_radians, 0.5, 0.0001, "INF refused, old value kept")


func test_collision_shape_offset_rejects_non_finite() -> void:
	var d := _data()
	d.collision_shape_offset = Vector2(4, 6)
	assert_eq(d.collision_shape_offset, Vector2(4, 6))
	d.collision_shape_offset = Vector2(NAN, 0)
	expect_error_sequence(["BulletVolleyData2D: collision_shape_offset must be finite, keeping the old value."])
	assert_eq(d.collision_shape_offset, Vector2(4, 6), "NaN offset refused")
	d.collision_shape_offset = Vector2(0, INF)
	expect_error_sequence(["BulletVolleyData2D: collision_shape_offset must be finite, keeping the old value."])
	assert_eq(d.collision_shape_offset, Vector2(4, 6), "Inf offset refused")


func test_texture_size_rejects_non_finite_and_negative() -> void:
	var d := _data()
	d.texture_size = Vector2(32, 32)
	d.texture_size = Vector2(NAN, 32)
	expect_error_sequence(["BulletVolleyData2D: texture_size must be finite and >= 0, keeping the old value."])
	assert_eq(d.texture_size, Vector2(32, 32), "NaN size refused")
	d.texture_size = Vector2(-4, 32)
	expect_error_sequence(["BulletVolleyData2D: texture_size must be finite and >= 0, keeping the old value."])
	assert_eq(d.texture_size, Vector2(32, 32), "negative size refused")
	d.texture_size = Vector2(0, 0)
	assert_eq(d.texture_size, Vector2(0, 0), "zero stays legal (derive-from-texture sentinel)")


func test_self_modulate_rejects_nan_channel() -> void:
	var d := _data()
	d.self_modulate = Color(1, 0.5, 0.25, 1)
	d.self_modulate = Color(NAN, 0, 0, 1)
	expect_error_sequence(["BulletVolleyData2D: self_modulate must be finite, keeping the old value."])
	assert_eq(d.self_modulate, Color(1, 0.5, 0.25, 1), "NaN channel refused on spawn data")
	d.self_modulate = Color(0, INF, 0, 1)
	expect_error_sequence(["BulletVolleyData2D: self_modulate must be finite, keeping the old value."])
	assert_eq(d.self_modulate, Color(1, 0.5, 0.25, 1), "Inf channel refused on spawn data")
	var layer := EffectLayerData2D.new()
	layer.self_modulate = Color(0, 1, 0, 1)
	layer.self_modulate = Color(0, 0, NAN, 1)
	expect_error_sequence(["EffectLayerData2D: self_modulate must be finite, keeping the old value."])
	assert_eq(layer.self_modulate, Color(0, 1, 0, 1), "NaN channel refused on effect layer data")


func test_fresh_data_validates() -> void:
	var good := _data()
	var res: Dictionary = BulletFactory2D.debug_validate_spawn_data(good)
	assert_true(res.get("ok", false), "good data validates")
	assert_eq(str(res.get("error", "")), "", "good data has no error")
	assert_true(is_finite(good.texture_rotation_radians))
	assert_true(good.texture_size.is_finite())
	assert_true(good.collision_shape_offset.is_finite())


func test_volley_with_texture_rotation_stays_finite() -> void:
	var d := _data(3)
	d.texture_rotation_radians = 1.25
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_not_null(v)
	for i in 3:
		assert_true(v.get_bullet_transform(i).is_finite(), "bullet %d transform finite" % i)
	await physics(2)
	for i in 3:
		assert_true((v.debug_get_bullet_info(i)["velocity"] as Vector2).is_finite(), "bullet %d velocity finite" % i)


## debug_validate_spawn_data always returns {ok, error, warning}; the
## invisible-bullet warning matches the spawn-time lint: a mesh is enough to
## be visible.
func test_validate_spawn_data_keys_and_mesh_lint() -> void:
	for d in [null, BulletVolleyData2D.new(), _data()]:
		var res: Dictionary = BulletFactory2D.debug_validate_spawn_data(d)
		var keys := res.keys()
		keys.sort()
		assert_eq(keys, ["error", "ok", "warning"], "every result carries the three keys")
	var invisible := _data()
	invisible.sprite_frames = null
	invisible.texture_size = Vector2.ZERO
	assert_ne(str(BulletFactory2D.debug_validate_spawn_data(invisible)["warning"]), "", "nothing to draw: warned")
	expect_warning_sequence(["Warning in debug_validate_spawn_data: no sprite_frames/mesh/texture_size, bullets will be invisible. Assign SpriteFrames with art facing Vector2.RIGHT, or set texture_size/mesh."], "and said so in the log")
	invisible.mesh = QuadMesh.new()
	assert_eq(str(BulletFactory2D.debug_validate_spawn_data(invisible)["warning"]), "", "a mesh draws: no warning")


## A collision shape dimension must be finite and > 0 (Godot's shapes store
## NaN and INF as given): a NaN or infinite radius, size or height falls back
## to the default circle r16 with one error, at spawn and at a runtime shape
## change, and the volley's pool bucket agrees with what it caches (a NaN
## rectangle once cached a RECTANGLE under a circle key and fed rectangle data
## to a circle shape).
func test_a_non_finite_shape_dimension_falls_back_to_the_default_circle() -> void:
	var circle := CircleShape2D.new()
	circle.radius = NAN
	var rect := RectangleShape2D.new()
	rect.size = Vector2(NAN, 10.0)
	var capsule := CapsuleShape2D.new()
	capsule.radius = 4.0
	capsule.height = INF
	var wide := RectangleShape2D.new()
	wide.size = Vector2(INF, 10.0)
	var seen: Array = []
	for shape in [circle, rect, capsule, wide]:
		var d := H.make_volley_data(2, 0.0, 30.0)
		d.collision_shape = shape
		var v := factory.spawn_volley(d)
		var st: Dictionary = v.debug_get_shape_state()
		seen.append([st["type"], st["circle_radius"]])
		v.clear_all_bullets()
	assert_eq(seen, [[PhysicsServer2D.SHAPE_CIRCLE, 16.0], [PhysicsServer2D.SHAPE_CIRCLE, 16.0], [PhysicsServer2D.SHAPE_CIRCLE, 16.0], [PhysicsServer2D.SHAPE_CIRCLE, 16.0]], "every one falls back to the circle r16")
	expect_error_sequence([
		"CircleShape2D radius must be finite and > 0. Falling back to circle r16.",
		"RectangleShape2D size must be finite and > 0. Falling back to circle r16.",
		"CapsuleShape2D radius/height must be finite and > 0. Falling back to circle r16.",
		"RectangleShape2D size must be finite and > 0. Falling back to circle r16.",
	])
	await idle(1)
	var books: Dictionary = factory.debug_assert_no_dangling()
	assert_true(books.get("ok", false), "every pooled volley sits in its own bucket: %s" % books.get("error", ""))
	var v2 := factory.spawn_volley(H.make_volley_data(2, 0.0, 30.0))
	v2.set_collision_shape_runtime(rect)
	expect_error_sequence(["RectangleShape2D size must be finite and > 0. Falling back to circle r16."])
	assert_eq(v2.debug_get_shape_state()["type"], PhysicsServer2D.SHAPE_CIRCLE, "a runtime change falls back too")


## Every collision-shape fallback is latched per spawn-data resource, like the
## per-bullet array warnings: a spawner firing the same bad shape reports the
## fallback ONCE, not one line per volley life (both a cold spawn and a pooled
## reuse cache the shape). The four branches keep separate latches.
func test_every_collision_shape_fallback_reports_once_per_data() -> void:
	var zero_circle := CircleShape2D.new()
	zero_circle.radius = 0.0
	var zero_rect := RectangleShape2D.new()
	zero_rect.size = Vector2.ZERO
	var bad_capsule := CapsuleShape2D.new()
	bad_capsule.radius = 0.0
	var unsupported := ConvexPolygonShape2D.new()
	var cases: Array = [
		[zero_circle, "CircleShape2D radius must be finite and > 0. Falling back to circle r16."],
		[zero_rect, "RectangleShape2D size must be finite and > 0. Falling back to circle r16."],
		[bad_capsule, "CapsuleShape2D radius/height must be finite and > 0. Falling back to circle r16."],
		[unsupported, "Unsupported collision shape type: ConvexPolygonShape2D - only RectangleShape2D/CircleShape2D/CapsuleShape2D supported. Falling back to circle r16."],
	]
	for case in cases:
		var d := _data()
		d.collision_shape = case[0]
		for i in 10: # ten frame-separated volley lives off the same data resource
			var v := factory.spawn_volley(d)
			assert_not_null(v, "the fallback still spawns")
			await idle(1)
	expect_error_sequence([
		"CircleShape2D radius must be finite and > 0. Falling back to circle r16.",
		"RectangleShape2D size must be finite and > 0. Falling back to circle r16.",
		"CapsuleShape2D radius/height must be finite and > 0. Falling back to circle r16.",
		"Unsupported collision shape type: ConvexPolygonShape2D - only RectangleShape2D/CircleShape2D/CapsuleShape2D supported. Falling back to circle r16.",
	], "one report per data resource and branch, in case order")
	await idle(2)
	expect_no_errors()
