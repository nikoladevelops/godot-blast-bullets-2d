extends BlastTest
## Spin + pattern scale fold into ONE matrix about the generator origin: a
## spun volley is a rigid rotation of the plain one, mirrored generators stay
## mirrored, sheared customs keep their shear, transforms_scale scales bases
## without un-mirroring, spin off leaves the layout exact.


func _line_spawner() -> BulletSpawner2D:
	var sp := make_spawner(H.make_directional_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_LINE, 3)
	sp.helper_line_direction = Vector2(1, 0)
	sp.helper_line_spacing = 40.0
	return sp


func _spin_to(sp: BulletSpawner2D, target_deg: float) -> float:
	sp.reset_spin_angle()
	sp.spin_enabled = true
	sp.spin_speed_deg_per_sec = 720.0
	for i in 200:
		await idle(1)
		if absf(sp.get_spin_angle_deg()) >= target_deg:
			break
	sp.spin_enabled = false
	return sp.get_spin_angle_deg()


func _rot(deg: float) -> Transform2D:
	var r := deg_to_rad(deg)
	return Transform2D(Vector2(cos(r), sin(r)), Vector2(-sin(r), cos(r)), Vector2.ZERO)


func test_spin_is_rigid_rotation() -> void:
	var sp := _line_spawner()
	sp.reset_spin_angle()
	var plain = sp.collect_spawn_transforms()
	var ang: float = await _spin_to(sp, 40.0)
	assert_gt(ang, 5.0, "spin advanced")
	var spun = sp.collect_spawn_transforms()
	assert_eq(spun.size(), plain.size(), "same slot count")
	for i in plain.size():
		var a: Transform2D = plain[i]
		var b: Transform2D = spun[i]
		assert_almost_eq(b.get_origin(), a.get_origin().rotated(deg_to_rad(ang)), Vector2(0.5, 0.5), "slot %d origin rotated" % i)
		assert_almost_eq(angle_difference(b.get_rotation(), a.get_rotation() + deg_to_rad(ang)), 0.0, 0.01, "slot %d facing rotated" % i)


func test_mirrored_generator_stays_mirrored_under_spin() -> void:
	var sp := _line_spawner()
	var marker: Node2D = add(Node2D.new())
	marker.scale = Vector2(-1, 1)
	sp.set_transforms_generator(marker)
	sp.reset_spin_angle()
	var mplain = sp.collect_spawn_transforms()
	var mang: float = await _spin_to(sp, 40.0)
	var mspun = sp.collect_spawn_transforms()
	assert_eq(mplain.size(), 3)
	assert_eq(mspun.size(), 3)
	var rot := _rot(mang)
	for i in mplain.size():
		var a: Transform2D = mplain[i]
		var b: Transform2D = mspun[i]
		assert_lt(a.determinant(), 0.0, "plain slot %d mirrored" % i)
		assert_lt(b.determinant(), 0.0, "spun slot %d still mirrored" % i)
		var expect: Transform2D = rot * a
		assert_almost_eq(b.x, expect.x, Vector2(0.05, 0.05), "slot %d basis x" % i)
		assert_almost_eq(b.y, expect.y, Vector2(0.05, 0.05), "slot %d basis y" % i)
		assert_almost_eq(b.get_origin(), expect.get_origin(), Vector2(0.5, 0.5), "slot %d origin" % i)


func test_shear_preserved_under_spin() -> void:
	var sp := make_spawner(H.make_directional_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM, 2)
	var shear := Transform2D(Vector2(1, 0.5), Vector2(0, 1), Vector2(60, 0))
	sp.set_helper_custom_transforms([shear, Transform2D(0.0, Vector2(-60, 0))])
	sp.reset_spin_angle()
	var cplain = sp.collect_spawn_transforms()
	var cang: float = await _spin_to(sp, 40.0)
	var cspun = sp.collect_spawn_transforms()
	assert_eq(cspun.size(), 2)
	var expect: Transform2D = _rot(cang) * (cplain[0] as Transform2D)
	assert_almost_eq((cspun[0] as Transform2D).x, expect.x, Vector2(0.01, 0.01), "shear basis x exact")
	assert_almost_eq((cspun[0] as Transform2D).y, expect.y, Vector2(0.01, 0.01), "shear basis y exact")


func test_transforms_scale_keeps_mirroring_and_shear() -> void:
	var sp := make_spawner(H.make_directional_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	var marker: Node2D = add(Node2D.new())
	marker.scale = Vector2(-1, 1)
	sp.set_transforms_generator(marker)
	sp.transforms_scale = 1.0
	var plain = sp.collect_spawn_transforms()
	sp.transforms_scale = 2.0
	var scaled = sp.collect_spawn_transforms()
	assert_eq(scaled.size(), 4)
	for i in plain.size():
		var a: Transform2D = plain[i]
		var b: Transform2D = scaled[i]
		assert_lt(b.determinant(), 0.0, "slot %d still mirrored" % i)
		assert_almost_eq(b.x, a.x * 2.0, Vector2(0.01, 0.01), "slot %d basis x doubled" % i)
		assert_almost_eq(b.y, a.y * 2.0, Vector2(0.01, 0.01), "slot %d basis y doubled" % i)
		assert_almost_eq(b.origin, a.origin, Vector2(0.01, 0.01), "slot %d origin untouched" % i)


func test_spin_off_layout_exact() -> void:
	var sp := _line_spawner()
	sp.reset_spin_angle()
	var v = sp.collect_spawn_transforms()
	assert_eq(v.size(), 3)
	assert_almost_eq((v[0] as Transform2D).get_origin(), Vector2(-40, 0), Vector2(0.5, 0.5), "unspun layout exact")
