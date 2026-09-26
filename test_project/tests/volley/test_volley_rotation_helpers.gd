extends SceneTree
## Rotation-helper + live-mutation suite: the new per-bullet rotation API
## (get/set/range/clear) crossed with shared fallback, curves, tick spin,
## pool reuse, and hostile input. Mirrors the speed-data API contract:
## read a live triple, tweak it, write it back, fan it, clear it.
## Run: godot --headless --path test_project --script tests/volley/test_volley_rotation_helpers.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _rot(spd: float, mx: float = 1000.0, acc: float = 0.0) -> BulletRotationData2D:
	var r := BulletRotationData2D.new()
	r.rotation_speed = spd
	r.max_rotation_speed = mx
	r.rotation_acceleration = acc
	return r

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("ROT T1 get/set round-trip per slot")
	var d1 := H.make_directional_data(3, 100.0)
	d1.all_bullet_rotation_data = [_rot(2.0), _rot(4.0), _rot(6.0)]
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d1)
	var got: BulletRotationData2D = v1.get_bullet_rotation_data(1)
	_check(absf(got.rotation_speed - 4.0) < 0.01, "get reads live triple")
	v1.set_bullet_rotation_data(1, _rot(40.0))
	_check(absf(v1.get_bullet_rotation_data(1).rotation_speed - 40.0) < 0.01, "set writes live triple")
	_check(absf(v1.get_bullet_rotation_data(0).rotation_speed - 2.0) < 0.01, "sibling untouched")
	_check(absf(v1.bullet_get_rotation_speed(1) - 40.0) < 0.01, "speed getter agrees with triple")

	printerr("ROT T2 range fan + OOB guards")
	v1.all_bullets_set_rotation_data(_rot(11.0), 0, 1)
	_check(absf(v1.get_bullet_rotation_data(0).rotation_speed - 11.0) < 0.01, "range fan slot 0")
	_check(absf(v1.get_bullet_rotation_data(1).rotation_speed - 11.0) < 0.01, "range fan slot 1")
	_check(absf(v1.get_bullet_rotation_data(2).rotation_speed - 6.0) < 0.01, "range spares slot 2")
	var arr: Array = v1.all_bullets_get_rotation_data()
	_check(arr.size() == 3, "range get size")
	v1.set_bullet_rotation_data(-1, _rot(1.0))
	v1.set_bullet_rotation_data(99, _rot(1.0))
	v1.set_bullet_rotation_data(0, null)
	_check(absf(v1.get_bullet_rotation_data(0).rotation_speed - 11.0) < 0.01, "null write rejected")
	var oob: BulletRotationData2D = v1.get_bullet_rotation_data(99)
	_check(absf(oob.rotation_speed) < 0.01, "OOB read returns zeroed resource")

	printerr("ROT T3 tick spins from live writes (no reseed needed)")
	var d3 := H.make_directional_data(2, 100.0)
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d3)
	_check(not v3.is_rotation_data_active(), "rotation off fresh")
	v3.set_bullet_rotation_data(0, _rot(30.0))
	v3.set_bullet_rotation_data(1, _rot(30.0))
	_check(v3.is_rotation_data_active(), "live write activates rotation")
	for i in 20:
		await physics_frame
	_check(absf(v3.get_bullet_texture_rotation_radians(0)) > 1.0, "live-seeded spin advances texture")
	v3.clear_bullet_rotation_data()
	_check(not v3.is_rotation_data_active(), "clear turns rotation off")
	var frozen: float = v3.get_bullet_texture_rotation_radians(0)
	for i in 10:
		await physics_frame
	_check(absf(v3.get_bullet_texture_rotation_radians(0) - frozen) < 0.01, "cleared volley holds yaw")

	printerr("ROT T4 shared fallback + live write precedence")
	var d4 := H.make_directional_data(2, 100.0)
	d4.all_bullet_rotation_data = [_rot(5.0), _rot(5.0)]
	d4.shared_bullet_rotation_data = _rot(50.0)
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4)
	_check(absf(v4.get_bullet_rotation_data(0).rotation_speed - 5.0) < 0.01, "per-bullet wins at seed")
	v4.set_bullet_rotation_data(0, _rot(7.0))
	_check(absf(v4.get_bullet_rotation_data(0).rotation_speed - 7.0) < 0.01, "live write wins over shared")
	var sh := _rot(60.0)
	v4.set_shared_bullet_rotation_data(sh)
	_check(absf(v4.get_bullet_rotation_data(0).rotation_speed - 7.0) < 0.01, "shared reseed spares live slot")

	printerr("ROT T5 max/accel clamp honored on live triples")
	var d5 := H.make_directional_data(1, 0.0)
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5)
	var ramp := _rot(0.0, 2.0, 100.0)
	v5.set_bullet_rotation_data(0, ramp)
	for i in 30:
		await physics_frame
	var snap: Dictionary = v5.debug_get_bullet_info(0)
	_check(absf((snap.get("rotation_speed", 99.0) as float)) <= 2.01, "accel clamps at max (%.2f)" % (snap.get("rotation_speed", 0.0) as float))

	printerr("ROT T5b negative spin goes backwards, clamps at -max")
	var d5b := H.make_directional_data(2, 0.0)
	var neg := _rot(-3.0, 5.0, 0.0)
	var neg_over := _rot(-50.0, 5.0, 0.0)
	d5b.all_bullet_rotation_data = [neg, neg_over]
	var v5b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5b)
	_check(absf(v5b.bullet_get_rotation_speed(0) + 3.0) < 0.01, "negative speed seeds")
	var yaw5: float = v5b.get_bullet_texture_rotation_radians(0)
	for i in 20:
		await physics_frame
	_check(v5b.get_bullet_texture_rotation_radians(0) - yaw5 < -0.2, "negative spin yaws backwards (%.2f rad)" % (v5b.get_bullet_texture_rotation_radians(0) - yaw5))
	_check(absf(v5b.bullet_get_rotation_speed(1) + 5.0) < 0.01, "negative overspeed clamps at -max (%.2f)" % v5b.bullet_get_rotation_speed(1))
	_check(v5b.get_bullet_transform(0).is_finite() and v5b.get_bullet_transform(1).is_finite(), "reverse spin stays finite")

	printerr("ROT T5c stop flag freezes at max, false keeps spinning")
	for stop in [true, false]:
		var ds := H.make_directional_data(1, 0.0)
		ds.stop_rotation_when_max_reached = stop
		ds.all_bullet_rotation_data = [_rot(5.0, 5.0, 0.0)]
		var vs: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(ds)
		var ys: float = vs.get_bullet_texture_rotation_radians(0)
		for i in 20:
			await physics_frame
		var moved: float = absf(vs.get_bullet_texture_rotation_radians(0) - ys)
		if stop:
			_check(moved < 0.01, "stop=true holds yaw at max (%.3f)" % moved)
		else:
			_check(moved > 0.2, "stop=false spins through max (%.2f)" % moved)

	printerr("ROT T5d rotate_only_textures: instance spins either way")
	for rot_only in [true, false]:
		var dr := H.make_directional_data(1, 0.0)
		dr.rotate_only_textures = rot_only
		dr.all_bullet_rotation_data = [_rot(4.0, 100.0, 0.0)]
		var vr: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dr)
		var yr: float = vr.get_bullet_texture_rotation_radians(0)
		for i in 10:
			await physics_frame
		_check(absf(vr.get_bullet_texture_rotation_radians(0) - yr) > 0.05, "rotate_only=%s instance spins" % str(rot_only))

	printerr("ROT T5e adjust flag steers from texture, skip under rotate_only")
	var d7 := H.make_directional_data(1, 200.0)
	d7.adjust_direction_based_on_rotation = true
	d7.rotate_only_textures = false
	d7.all_bullet_rotation_data = [_rot(6.0, 100.0, 0.0)]
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d7)
	v7.set_bullet_texture_rotation_radians(0, 1.0)
	var dir0: Vector2 = v7.get_bullet_direction(0)
	for i in 30:
		await physics_frame
	_check((v7.get_bullet_direction(0) - dir0).length() > 0.5, "adjust=true steers direction from spin")
	var d7d := H.make_directional_data(1, 200.0)
	d7d.adjust_direction_based_on_rotation = true
	d7d.rotate_only_textures = true
	var v7d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d7d)
	v7d.set_bullet_texture_rotation_radians(0, 1.0)
	var dir0d: Vector2 = v7d.get_bullet_direction(0)
	for i in 30:
		await physics_frame
	_check((v7d.get_bullet_direction(0) - dir0d).length() < 0.01, "rotate_only=true skips adjust steering")

	printerr("ROT T5f live order both ways + remove persistence + getters")
	var e1 := H.make_directional_data(2, 100.0)
	e1.all_bullet_rotation_data = []
	var o1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(e1)
	var z := _rot(0.0, 0.0, 0.0)
	var sh9 := _rot(9.0, 100.0, 0.0)
	o1.set_bullet_rotation_data(0, z)
	o1.set_shared_bullet_rotation_data(sh9)
	_check(absf(o1.bullet_get_rotation_speed(0) - 9.0) < 0.01, "per-zero then shared fills rotation gap")
	var e2 := H.make_directional_data(2, 100.0)
	e2.all_bullet_rotation_data = []
	var o2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(e2)
	o2.set_shared_bullet_rotation_data(sh9)
	o2.set_bullet_rotation_data(0, z)
	_check(absf(o2.bullet_get_rotation_speed(0)) < 0.01, "shared then per-zero stays zero (fill-once)")
	o2.remove_shared_bullet_rotation_data()
	_check(not o2.has_shared_bullet_rotation_data(), "shared rotation removed")
	_check(absf(o2.bullet_get_rotation_speed(1) - 9.0) < 0.01, "rotation ballistics persist after clear")
	_check(o2.get_shared_bullet_rotation_data() == null, "shared rotation getter null after clear")
	var e3 := H.make_directional_data(2, 100.0)
	e3.all_bullet_rotation_data = [_rot(3.0), _rot(4.0)]
	var shs := _rot(1.0, 100.0, 0.0)
	e3.shared_bullet_rotation_data = shs
	var o3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(e3)
	_check(absf(o3.get_bullet_rotation_data(0).rotation_speed - 3.0) < 0.01, "seed order keeps per-bullet")
	_check(o3.get_shared_bullet_rotation_data() == shs, "shared rotation getter returns stored resource")
	_check(o3.get_shared_bullet_speed_data() == null, "shared speed getter null when unset")
	var inv: Array = o3.all_bullets_get_rotation_data(1, 0)
	_check(inv.is_empty(), "inverted rotation range reads empty (start clamped to 1, end to 0 -> empty)")
	var oob_range: Array = o3.all_bullets_get_rotation_data(99, 99)
	_check(oob_range.size() == 2, "OOB range clamps to full volley (documented range behavior)")

	printerr("ROT T6 pool reuse drops live rotation edits")
	v5.set_bullet_rotation_data(0, _rot(77.0))
	for i in 1:
		v5.disable_bullet(i)
	await physics_frame
	var v6: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 100.0))
	_check(not v6.is_rotation_data_active(), "reuse has no rotation")
	_check(absf(v6.bullet_get_rotation_speed(0)) < 0.01, "reuse speed zeroed")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL ROTATION HELPER TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
