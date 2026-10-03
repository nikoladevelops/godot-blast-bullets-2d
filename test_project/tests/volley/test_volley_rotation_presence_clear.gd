extends BlastTest
## clear_bullet_rotation_data() clears the presence bits with the values: a
## later shared fallback fills genuine gaps, an explicit zero still wins,
## and clear alone + shared full-seeds.


func _data() -> BulletVolleyData2D:
	var d := H.make_still_data(2)
	d.all_bullet_rotation_data = [H.make_rotation(0.0), H.make_rotation(9.0)]
	return d


func test_cleared_bits_let_shared_fill_gaps() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	v.clear_bullet_rotation_data()
	v.set_bullet_rotation_data(0, H.make_rotation(0.0))
	v.set_shared_bullet_rotation_data(H.make_rotation(7.0))
	assert_almost_eq(v.bullet_get_rotation_speed(1), 7.0, 0.01, "gap slot filled by shared")
	assert_almost_eq(v.bullet_get_rotation_speed(0), 0.0, 0.01, "authored zero survives")


func test_clear_alone_then_shared_full_seeds() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	v.clear_bullet_rotation_data()
	v.set_shared_bullet_rotation_data(H.make_rotation(5.0))
	assert_almost_eq(v.bullet_get_rotation_speed(0), 5.0, 0.01, "slot 0 seeded")
	assert_almost_eq(v.bullet_get_rotation_speed(1), 5.0, 0.01, "slot 1 seeded")
