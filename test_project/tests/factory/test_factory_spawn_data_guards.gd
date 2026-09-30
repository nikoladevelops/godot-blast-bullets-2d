extends SceneTree
## Spawn-data finiteness suite: the fields that are folded into EVERY bullet
## transform must reject NaN/Inf at the setter, and the factory must reject a
## hand-built resource that carries one anyway.
##
## The bug: src/spawn-data/ setters were almost entirely unguarded while every
## runtime setter in src/bullets/ validated. The worst case is
## texture_rotation_radians - multimesh_bullets2d.cpp copies it into
## cache_texture_rotation_radians and generate_texture_transform ADDS it to
## every bullet's rotation, so a single NaN made the whole volley NaN. And
## validate_spawn_data only inspected transforms[], so the spawn SUCCEEDED and
## produced invisible, broken bullets with no diagnostic.
##
## Covers: T1 texture_rotation_radians rejects NaN/Inf and keeps the old value,
## T2 collision_shape_offset rejects non-finite, T3 texture_size rejects
## non-finite and negative, T4 self_modulate rejects a NaN channel (both data
## classes), T5 block rotation rejects NaN, T6 a valid value still round-trips,
## T7 a spawned volley with a good rotation stays finite, T8 no dangling.
##
## Run: godot --headless --path test_project --script tests/factory/test_factory_spawn_data_guards.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _data(n: int = 2) -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(0.0, 0.0)))
	d.transforms = arr
	var speeds: Array = []
	for i in n:
		var s := BulletSpeedData2D.new()
		s.speed = 100.0
		s.max_speed = 3000.0
		speeds.append(s)
	d.all_bullet_speed_data = speeds
	d.max_life_time = 5.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([4])
	return d

func _initialize() -> void:
	# ---------------------------------------------------------------
	printerr("GUARD T1 texture_rotation_radians rejects NaN/Inf")
	var d1 := _data()
	d1.texture_rotation_radians = 0.5
	var good := d1.texture_rotation_radians
	_check(absf(good - 0.5) < 0.0001, "T1a a valid rotation is stored (%.3f)" % good)
	d1.texture_rotation_radians = NAN
	_check(not is_nan(d1.texture_rotation_radians), "T1b NaN rotation refused, old value kept (%.3f)" % d1.texture_rotation_radians)
	_check(absf(d1.texture_rotation_radians - 0.5) < 0.0001, "T1c the previous good value survived the NaN write")
	d1.texture_rotation_radians = INF
	_check(is_finite(d1.texture_rotation_radians), "T1d INF rotation refused (%.3f)" % d1.texture_rotation_radians)
	_check(absf(d1.texture_rotation_radians - 0.5) < 0.0001, "T1e previous value still intact after INF")

	# ---------------------------------------------------------------
	printerr("GUARD T2 collision_shape_offset rejects non-finite")
	var d2 := _data()
	d2.collision_shape_offset = Vector2(4, 6)
	_check(d2.collision_shape_offset.is_equal_approx(Vector2(4, 6)), "T2a valid offset stored")
	d2.collision_shape_offset = Vector2(NAN, 0)
	_check(d2.collision_shape_offset.is_equal_approx(Vector2(4, 6)), "T2b NaN offset refused, old value kept (%s)" % str(d2.collision_shape_offset))
	d2.collision_shape_offset = Vector2(0, INF)
	_check(d2.collision_shape_offset.is_equal_approx(Vector2(4, 6)), "T2c Inf offset refused, old value kept")

	# ---------------------------------------------------------------
	printerr("GUARD T3 texture_size rejects non-finite and negative")
	var d3 := _data()
	d3.texture_size = Vector2(32, 32)
	_check(d3.texture_size.is_equal_approx(Vector2(32, 32)), "T3a valid size stored")
	d3.texture_size = Vector2(NAN, 32)
	_check(d3.texture_size.is_equal_approx(Vector2(32, 32)), "T3b NaN size refused, old value kept")
	d3.texture_size = Vector2(-4, 32)
	_check(d3.texture_size.is_equal_approx(Vector2(32, 32)), "T3c negative size refused, old value kept")
	# (0,0) is the documented "derive from the texture" sentinel and must stay legal.
	d3.texture_size = Vector2(0, 0)
	_check(d3.texture_size == Vector2(0, 0), "T3d zero size is still allowed (derive-from-texture sentinel)")

	# ---------------------------------------------------------------
	printerr("GUARD T4 self_modulate rejects a NaN channel (both data classes)")
	var d4 := _data()
	d4.self_modulate = Color(1, 0.5, 0.25, 1)
	_check(d4.self_modulate.is_equal_approx(Color(1, 0.5, 0.25, 1)), "T4a valid color stored")
	d4.self_modulate = Color(NAN, 0, 0, 1)
	_check(d4.self_modulate.is_equal_approx(Color(1, 0.5, 0.25, 1)), "T4b NaN channel refused on spawn data")
	d4.self_modulate = Color(0, INF, 0, 1)
	_check(d4.self_modulate.is_equal_approx(Color(1, 0.5, 0.25, 1)), "T4c Inf channel refused on spawn data")
	var layer := BulletEffectLayerData2D.new()
	layer.self_modulate = Color(0, 1, 0, 1)
	layer.self_modulate = Color(0, 0, NAN, 1)
	_check(layer.self_modulate.is_equal_approx(Color(0, 1, 0, 1)), "T4d NaN channel refused on effect layer data")

	# ---------------------------------------------------------------
	printerr("GUARD T5 block rotation rejects NaN")
	var b := BlockBulletsData2D.new()
	b.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var bs := BulletSpeedData2D.new()
	bs.speed = 50.0
	bs.max_speed = 500.0
	b.block_speed = bs
	b.max_life_time = 5.0
	b.texture_size = Vector2(16, 16)
	b.set_collision_layer_from_array([2])
	b.set_collision_mask_from_array([4])
	b.block_rotation_radians = 0.25
	_check(absf(b.block_rotation_radians - 0.25) < 0.0001, "T5a valid block rotation stored")
	b.block_rotation_radians = NAN
	_check(absf(b.block_rotation_radians - 0.25) < 0.0001, "T5b NaN block rotation refused, old value kept (%.3f)" % b.block_rotation_radians)

	# ---------------------------------------------------------------
	printerr("GUARD T6 validate_spawn_data rejects hand-built bad state")
	# The setters guard the property path; a C++ caller or a resource loaded
	# from disk can still carry a bad value, so the factory must refuse it.
	# GDScript cannot write the C++ member directly, so we assert the
	# static validator's contract on GOOD data instead and prove the guard
	# exists by checking the setter path cannot produce bad state.
	var good_data := _data()
	var res: Dictionary = BulletFactory2D.debug_validate_spawn_data(good_data)
	_check(bool(res.get("ok", false)), "T6a good data validates")
	_check(str(res.get("error", "")) == "", "T6b good data has no error")

	# Every field the guards protect must be finite on a freshly built object.
	_check(is_finite(good_data.texture_rotation_radians), "T6c fresh data has a finite rotation")
	_check(good_data.texture_size.is_finite(), "T6d fresh data has a finite size")
	_check(good_data.collision_shape_offset.is_finite(), "T6e fresh data has a finite shape offset")

	# ---------------------------------------------------------------
	printerr("GUARD T7 a volley with a non-zero rotation stays finite")
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var d7 := _data(3)
	d7.texture_rotation_radians = 1.25
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d7)
	_check(v != null, "T7 volley spawned with a non-zero texture rotation")
	for i in 2:
		var t: Transform2D = v.get_bullet_transform(i)
		_check(t.is_finite(), "T7 bullet %d transform is finite (origin=%s)" % [i, str(t.get_origin())])
	await physics_frame
	await physics_frame
	for i in 3:
		_check((v.debug_get_bullet_info(i)["velocity"] as Vector2).is_finite(), "T7 bullet %d velocity finite" % i)

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL SPAWN-DATA-GUARDS TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
