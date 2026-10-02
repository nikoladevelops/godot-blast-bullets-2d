extends BlastTest
## Path2D preview gating: the curve resample runs sparingly, yet node motion
## reflects within frames and in-place curve edits within the ~15-tick bound.


func test_gizmo_tracks_motion_and_edits() -> void:
	var path: Path2D = add(Path2D.new())
	var curve := Curve2D.new()
	curve.add_point(Vector2(-100, 0))
	curve.add_point(Vector2(0, -60))
	curve.add_point(Vector2(100, 0))
	path.curve = curve
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D, 8)
	sp.set_helper_path2d_node(path)
	sp.set_helper_path2d_space(BulletSpawner2D.PATH2D_SPACE_AT_PATH2D)
	await idle(8)
	var base := sp.debug_get_preview_dot_points()
	assert_eq(base.size(), 8, "eight dots tracked")
	path.position = Vector2(200, 0)
	var moved := false
	for i in 10:
		await idle(1)
		var now := sp.debug_get_preview_dot_points()
		if now.size() == 8 and now[0].distance_to(base[0]) > 5.0:
			moved = true
			break
	assert_true(moved, "gizmo follows node motion")
	path.position = Vector2.ZERO
	await idle(8)
	var calm := sp.debug_get_preview_dot_points()
	curve.set_point_position(1, Vector2(0, 60))
	var edited := false
	for i in 30:
		await idle(1)
		var now2 := sp.debug_get_preview_dot_points()
		if now2.size() == 8 and now2[4].distance_to(calm[4]) > 5.0:
			edited = true
			break
	assert_true(edited, "gizmo reflects in-place curve edits")
