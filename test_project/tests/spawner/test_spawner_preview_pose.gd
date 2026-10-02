extends BlastTest
## Zero-cost preview pose: spin and rigid generator motion re-pose the layer
## NODES (O(1)) instead of rebuilding geometry or re-issuing draw commands.
## Regression pins for P1 (track spun twice), P2 (spin hid other changes),
## P3 (pose one frame behind), mirrored-generator pose, P8/P9 inspector.

const HOLDER := "~BlastBulletsPatternPreview"


func _layers(n: Node) -> Array:
	var holder: Node = n.get_node_or_null(HOLDER)
	if holder == null:
		return []
	return [holder.get_node_or_null("Dots"), holder.get_node_or_null("Arrows")]


func _seg_dist(p: Vector2, a: Vector2, b: Vector2) -> float:
	var ab := b - a
	var denom := ab.length_squared()
	if denom <= 0.00000001:
		return p.distance_to(a)
	var t: float = clampf((p - a).dot(ab) / denom, 0.0, 1.0)
	return p.distance_to(a + ab * t)


func _max_dev(dots: PackedVector2Array, track: PackedVector2Array) -> float:
	var worst := 0.0
	for d in dots:
		var best := 1e30
		for k in range(track.size() - 1):
			if track[k].is_finite() and track[k + 1].is_finite():
				best = minf(best, _seg_dist(d, track[k], track[k + 1]))
		if best < 1e29:
			worst = maxf(worst, best)
	return worst


func _spinning_preview(src: int, n: int) -> BulletSpawner2D:
	var sp := make_preview_spawner(src, n)
	sp.position = Vector2(400, 300)
	sp.spin_enabled = true
	sp.spin_speed_deg_per_sec = 90.0
	return sp


func test_spin_never_rebuilds_or_redraws() -> void:
	var sp := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_HEART, 1500)
	await idle(4)
	var s0: Dictionary = sp.debug_get_preview_stats()
	assert_gt(int(s0["dots_draws"]), 0, "preview drew once")
	await idle(30)
	var s1: Dictionary = sp.debug_get_preview_stats()
	assert_eq(int(s1["rebuilds"]), int(s0["rebuilds"]), "30 spinning frames: zero rebuilds")
	assert_eq(int(s1["dots_draws"]), int(s0["dots_draws"]), "30 spinning frames: zero dot redraws")
	assert_eq(int(s1["arrows_draws"]), int(s0["arrows_draws"]), "30 spinning frames: zero arrow redraws")
	var dots: Node2D = _layers(sp)[0]
	assert_almost_eq(angle_difference(dots.rotation, deg_to_rad(sp.get_spin_angle_deg())), 0.0, 0.0001, "layer pose IS the current spin (P3: no one-frame lag)")


func test_pose_matches_the_volley() -> void:
	var sp := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 10)
	await idle(20)
	var layer: Node2D = _layers(sp)[0]
	var shots: Array = sp.collect_spawn_transforms()
	var dots: PackedVector2Array = sp.debug_get_preview_dot_points()
	assert_eq(dots.size(), shots.size())
	for i in dots.size():
		var drawn: Vector2 = layer.get_global_transform() * dots[i]
		assert_almost_eq(drawn, (shots[i] as Transform2D).origin, Vector2(0.05, 0.05), "dot %d drawn where bullet %d spawns" % [i, i])


func test_mirrored_generator_pose_follows_global_spin() -> void:
	var sp := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 10)
	sp.scale = Vector2(-1, 1)
	await idle(20)
	var layer: Node2D = _layers(sp)[0]
	var shots: Array = sp.collect_spawn_transforms()
	var dots: PackedVector2Array = sp.debug_get_preview_dot_points()
	for i in dots.size():
		assert_almost_eq(layer.get_global_transform() * dots[i], (shots[i] as Transform2D).origin, Vector2(0.05, 0.05), "mirrored: dot %d on its bullet" % i)


func test_rigid_parent_move_never_rebuilds() -> void:
	var ship: Node2D = add(Node2D.new())
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.set_homing_enabled(false)
	sp.set_spawn_data(H.make_directional_data(1, 0.0, 60.0))
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
	sp.helper_bullets_amount = 500
	sp.show_pattern_preview = true
	sp.show_preview_during_runtime = true
	ship.add_child(sp)
	sp.set_bullet_factory(factory)
	await idle(4)
	var r0: int = sp.debug_get_preview_stats()["rebuilds"]
	for i in 20:
		ship.position += Vector2(7, 3)
		ship.rotation += 0.05
		await idle(1)
	assert_eq(int(sp.debug_get_preview_stats()["rebuilds"]), r0, "rigidly moving parent: no rebuild (canvas items ride along)")
	var layer: Node2D = _layers(sp)[0]
	var shots: Array = sp.collect_spawn_transforms()
	var dots: PackedVector2Array = sp.debug_get_preview_dot_points()
	assert_almost_eq(layer.get_global_transform() * dots[7], (shots[7] as Transform2D).origin, Vector2(0.05, 0.05), "still exactly where the shot spawns")


func test_world_anchored_source_still_rebuilds_on_move() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_AIMED, 5)
	var tgt: Node2D = add(Node2D.new())
	tgt.position = Vector2(800, 0)
	sp.set_helper_aimed_target(tgt)
	await idle(4)
	var r0: int = sp.debug_get_preview_stats()["rebuilds"]
	sp.position += Vector2(0, 100)
	await idle(2)
	assert_gt(int(sp.debug_get_preview_stats()["rebuilds"]), r0, "aimed (target-dependent) pattern rebuilds when the spawner moves")


func test_spin_does_not_hide_target_moves() -> void:
	# P2: the spin branch used to return early from the dirty check.
	var sp := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_AIMED, 5)
	var tgt: Node2D = add(Node2D.new())
	tgt.position = Vector2(800, 300)
	sp.set_helper_aimed_target(tgt)
	await idle(4)
	var r0: int = sp.debug_get_preview_stats()["rebuilds"]
	tgt.position = Vector2(400, 900)
	await idle(2)
	assert_gt(int(sp.debug_get_preview_stats()["rebuilds"]), r0, "target move detected while spinning")


func test_rebuild_mid_spin_keeps_track_under_dots() -> void:
	# P1: a rebuild during spin used to spin the track at build time AND at
	# draw time. Both snapshots are now unspun holder-local data.
	var sp := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 10)
	sp.helper_star_points = 5
	await idle(23) # ~34 deg into the spin: not a multiple of the 72 deg symmetry
	sp.preview_dot_radius = sp.preview_dot_radius + 1.0 # forces a rebuild now
	await idle(1)
	assert_lt(_max_dev(sp.debug_get_preview_dot_points(), sp.debug_get_preview_track_points()), 1.0, "dots sit on the star outline after a mid-spin rebuild")


func test_inspector_refresh_signals() -> void:
	# P8: setters whose values gate other properties' visibility must emit
	# property_list_changed so the inspector refreshes.
	var sp := make_spawner()
	watch_signals(sp)
	sp.helper_flower_type = 1
	assert_signal_emit_count(sp, "property_list_changed", 1, "flower type refreshes the inspector")
	sp.homing_target_selection = 1
	assert_signal_emit_count(sp, "property_list_changed", 2, "target selection refreshes the inspector")
	sp.show_pattern_preview = true
	assert_signal_emit_count(sp, "property_list_changed", 3, "preview toggle refreshes the inspector")
	sp.show_preview_during_runtime = true
	assert_signal_emit_count(sp, "property_list_changed", 4, "runtime preview toggle refreshes the inspector")


func test_layer_start_offset_hidden_outside_outline_sources() -> void:
	# P9: missing `relevant &&` guard leaked the knob into every mode.
	var sp := make_spawner(null, BulletSpawner2D.PATTERN_FROM_HELPER_GRID)
	sp.helper_outline_placement = 1
	sp.helper_outline_layer_fill = 1
	assert_false(is_editor_visible(sp, &"helper_outline_layer_start_offset"), "hidden for GRID")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_CIRCLE
	assert_true(is_editor_visible(sp, &"helper_outline_layer_start_offset"), "shown for an outline source in LAYERS + sequential")
