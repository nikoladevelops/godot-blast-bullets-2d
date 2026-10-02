extends BlastTest
## Terrain-crest preset: baked crest normals tilt WITH the slope (up-right on
## ascending segments, up-left on descending), all finite. The old preset
## used atan2(-1, -slope) and flipped x.

var arr: Array


func before_each() -> void:
	await super()
	var sp := make_spawner()
	sp.apply_pattern_preset(BulletFactory2D.PATTERN_PRESET_TERRAIN_CREST)
	arr = sp.get_helper_custom_transforms()


func test_crest_bakes_120_slots() -> void:
	assert_eq(arr.size(), 120)


func test_ascending_slots_face_up_right() -> void:
	# x(i) = -600 + 1200*i/119; slope = 0.3*cos(x/200) > 0 for i in 58..61.
	for i in [58, 59, 60, 61]:
		var t: Transform2D = arr[i]
		assert_true(t.is_finite(), "slot %d finite" % i)
		assert_gt(cos(t.get_rotation()), 0.0, "slot %d faces up-right" % i)


func test_descending_slots_face_up_left() -> void:
	# slope < 0 for x in (314, 600): interior slots 100..108.
	for i in [100, 104, 108]:
		var t: Transform2D = arr[i]
		assert_lt(cos(t.get_rotation()), 0.0, "slot %d faces up-left" % i)


func test_all_facings_finite() -> void:
	assert_true(H.finite_volley(arr))
