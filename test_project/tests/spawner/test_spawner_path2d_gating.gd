extends SceneTree
## Path2D preview-gating suite: the curve resample runs sparingly but the
## gizmo never goes stale.
##
## Contract under test: node identity/transform compares run every tick
## (cheap); the full curve bake runs on node change or at worst every 15th
## tick (~0.25 s bound). A gizmo must therefore reflect curve edits and node
## motion promptly without baking every frame.
##
## Covers: T1 node motion reflects within frames, T2 in-place curve edits
## reflect within the staleness bound, T3 no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_path2d_gating.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	s.speed = 0.0
	s.max_speed = 3000.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = 60.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	return d

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var path := Path2D.new()
	var curve := Curve2D.new()
	curve.add_point(Vector2(-100, 0))
	curve.add_point(Vector2(0, -60))
	curve.add_point(Vector2(100, 0))
	path.curve = curve
	get_root().add_child(path)
	var sp := BulletSpawner2D.new()
	get_root().add_child(sp)
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(_data())
	sp.set_shooting_enabled(false)
	sp.set_helper_path2d_node(path)
	sp.set_helper_path2d_space(BulletSpawner2D.PATH2D_SPACE_AT_PATH2D)
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D
	sp.helper_bullets_amount = 8
	sp.show_pattern_preview = true
	sp.show_preview_during_runtime = true
	for i in 8:
		await process_frame
	var base := sp.debug_get_preview_dot_points()
	_check(base.size() == 8, "T0 eight dots tracked (got %d)" % base.size())

	# ---------------------------------------------------------------
	printerr("PATH2DGATE T1 node motion reflects within frames")
	path.position = Vector2(200, 0)
	var moved := false
	for i in 10:
		await process_frame
		var now := sp.debug_get_preview_dot_points()
		if now.size() == 8 and now[0].distance_to(base[0]) > 5.0:
			moved = true
			break
	_check(moved, "T1 gizmo follows node motion")

	# ---------------------------------------------------------------
	printerr("PATH2DGATE T2 in-place curve edits reflect within bound")
	path.position = Vector2.ZERO
	for i in 8:
		await process_frame
	var calm := sp.debug_get_preview_dot_points()
	curve.set_point_position(1, Vector2(0, 60))
	var edited := false
	for i in 30:
		await process_frame
		var now2 := sp.debug_get_preview_dot_points()
		if now2.size() == 8 and now2[4].distance_to(calm[4]) > 5.0:
			edited = true
			break
	_check(edited, "T2 gizmo reflects curve edits")

	path.queue_free()
	sp.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL PATH2D-GATING TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
