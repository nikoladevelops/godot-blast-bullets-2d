extends BlastTest
## Preview state: a dead track (zero path width) drops the layer rings while
## the dots survive; seed setters rebuild the gizmo (scatter positions move).


func test_dead_track_drops_rings_keeps_dots() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 24)
	sp.helper_ring_radius = 120.0
	sp.helper_outline_placement = 1 # LAYERS
	sp.helper_outline_layer_count = 3
	await idle(8)
	assert_eq(sp.debug_get_layer_rings().size(), 2, "two rings while live")
	assert_false(sp.debug_get_preview_dot_points().is_empty(), "dots present while live")
	sp.preview_path_width = 0.0
	await idle(8)
	assert_true(sp.debug_get_layer_rings().is_empty(), "rings dropped when the track dies")
	assert_false(sp.debug_get_preview_dot_points().is_empty(), "dots survive")


func test_scatter_seed_rebuilds_dots() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_SCATTER, 12)
	sp.helper_scatter_burst_radius = 130.0
	sp.helper_scatter_seed = 11
	await idle(8)
	var before := sp.debug_get_preview_dot_points()
	assert_eq(before.size(), 12)
	sp.helper_scatter_seed = 77
	await idle(8)
	var after := sp.debug_get_preview_dot_points()
	assert_eq(after.size(), 12)
	var moved := false
	for i in mini(before.size(), after.size()):
		if before[i].distance_to(after[i]) > 0.5:
			moved = true
	assert_true(moved, "seeded layout refreshed the dots")
