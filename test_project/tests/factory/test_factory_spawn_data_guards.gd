extends BlastTest
## Spawn-data finiteness: fields folded into EVERY bullet transform reject
## NaN/Inf at the setter and keep the old value (a single NaN
## texture_rotation_radians used to turn a whole volley NaN), and a volley with
## a valid non-zero texture rotation stays finite.


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
	expect_any_error()
	assert_almost_eq(d.texture_rotation_radians, 0.5, 0.0001, "NaN refused, old value kept")
	d.texture_rotation_radians = INF
	expect_any_error()
	assert_almost_eq(d.texture_rotation_radians, 0.5, 0.0001, "INF refused, old value kept")


func test_collision_shape_offset_rejects_non_finite() -> void:
	var d := _data()
	d.collision_shape_offset = Vector2(4, 6)
	assert_eq(d.collision_shape_offset, Vector2(4, 6))
	d.collision_shape_offset = Vector2(NAN, 0)
	expect_any_error()
	assert_eq(d.collision_shape_offset, Vector2(4, 6), "NaN offset refused")
	d.collision_shape_offset = Vector2(0, INF)
	expect_any_error()
	assert_eq(d.collision_shape_offset, Vector2(4, 6), "Inf offset refused")


func test_texture_size_rejects_non_finite_and_negative() -> void:
	var d := _data()
	d.texture_size = Vector2(32, 32)
	d.texture_size = Vector2(NAN, 32)
	expect_any_error()
	assert_eq(d.texture_size, Vector2(32, 32), "NaN size refused")
	d.texture_size = Vector2(-4, 32)
	expect_any_error()
	assert_eq(d.texture_size, Vector2(32, 32), "negative size refused")
	d.texture_size = Vector2(0, 0)
	assert_eq(d.texture_size, Vector2(0, 0), "zero stays legal (derive-from-texture sentinel)")


func test_self_modulate_rejects_nan_channel() -> void:
	var d := _data()
	d.self_modulate = Color(1, 0.5, 0.25, 1)
	d.self_modulate = Color(NAN, 0, 0, 1)
	expect_any_error()
	assert_eq(d.self_modulate, Color(1, 0.5, 0.25, 1), "NaN channel refused on spawn data")
	d.self_modulate = Color(0, INF, 0, 1)
	expect_any_error()
	assert_eq(d.self_modulate, Color(1, 0.5, 0.25, 1), "Inf channel refused on spawn data")
	var layer := BulletEffectLayerData2D.new()
	layer.self_modulate = Color(0, 1, 0, 1)
	layer.self_modulate = Color(0, 0, NAN, 1)
	expect_any_error()
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
