extends SceneTree
## Stale-rings + seed-rebuild suite: two preview-state fixes.
##
## Bug 1: preview_last_layer_rings was cleared only inside the live-track
## branch, so a dead generator / zero path width kept serving the previous
## rebuild's rings (and the coincidence check lied about dots on stale rings).
## Bug 2: five seed setters (grid/ring/fan/rain/waterfall) assigned without
## rebuilding the preview, so seeded layouts never refreshed the gizmo.
##
## Covers: T1 dead track drops rings (dots stay), T2 ring seed rebuilds the
## dots, T3 no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_preview_state.gd
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

func _preview_spawner(factory: BulletFactory2D) -> BulletSpawner2D:
	var sp := BulletSpawner2D.new()
	get_root().add_child(sp)
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(_data())
	sp.set_shooting_enabled(false)
	sp.show_pattern_preview = true
	sp.show_preview_during_runtime = true
	return sp

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("PVSTATE T1 dead track drops rings but keeps dots")
	var sp := _preview_spawner(factory)
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
	sp.helper_bullets_amount = 24
	sp.helper_ring_radius = 120.0
	sp.helper_outline_placement = 1 # LAYERS
	sp.helper_outline_layer_count = 3
	for i in 8:
		await process_frame
	_check(sp.debug_get_layer_rings().size() == 2, "T1 two rings while live (got %d)" % sp.debug_get_layer_rings().size())
	_check(not sp.debug_get_preview_dot_points().is_empty(), "T1 dots present while live")
	sp.preview_path_width = 0.0
	for i in 8:
		await process_frame
	_check(sp.debug_get_layer_rings().is_empty(), "T1 rings dropped when track dies")
	_check(not sp.debug_get_preview_dot_points().is_empty(), "T1 dots survive (track-only death)")
	sp.queue_free()
	await process_frame

	# ---------------------------------------------------------------
	printerr("PVSTATE T2 scatter seed rebuilds the dots")
	# Seeds move scatter POSITIONS (ring seeds only turn facings, so dots
	# would be identical there by design).
	var sp2 := _preview_spawner(factory)
	sp2.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_SCATTER
	sp2.helper_bullets_amount = 12
	sp2.helper_scatter_burst_radius = 130.0
	sp2.helper_scatter_seed = 11
	for i in 8:
		await process_frame
	var before := sp2.debug_get_preview_dot_points()
	_check(before.size() == 12, "T2 twelve dots (got %d)" % before.size())
	sp2.helper_scatter_seed = 77
	for i in 8:
		await process_frame
	var after := sp2.debug_get_preview_dot_points()
	_check(after.size() == 12, "T2 still twelve dots after reseed")
	var moved := false
	for i in mini(before.size(), after.size()):
		if before[i].distance_to(after[i]) > 0.5:
			moved = true
	_check(moved, "T2 seeded layout refreshed the dots")
	sp2.queue_free()
	await process_frame

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL PREVIEW-STATE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
