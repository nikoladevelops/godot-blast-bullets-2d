extends BlastTest
## End-to-end proof that preview dots sit on the layer rings: a live spawner
## with runtime preview draws every outline-layout shape and
## debug_check_layer_coincidence pairs dots to rings. Tolerance 2.0 px
## (2.5 for rose/star-dense): sharp shapes read exact, dense sweeps sub-pixel;
## weaves read higher where adjacent lobes pass near each other and the
## nearest-segment pairing crosses over (a debug-metric artifact, not a layout
## mismatch: the volley-side scaling in test_factory_layer_rings is exact).
## The 14-shape sweep is parameterized (one GUT entry per shape) so a
## regression names its shape instead of hiding in a loop.


func _layers_spawner(src: int, amount: int = 24) -> BulletSpawner2D:
	var sp := make_preview_spawner(src, amount)
	sp.helper_outline_placement = 1 # LAYERS
	sp.helper_outline_layer_count = 3
	sp.helper_outline_layer_scale = 0.25
	sp.helper_outline_layer_side = 0
	sp.helper_outline_layer_fill = 0
	return sp


func _check_coincidence(sp: BulletSpawner2D, tol: float, label: String) -> void:
	var res: Dictionary = sp.debug_check_layer_coincidence(tol)
	assert_true(res.get("checked", false), label + " checked")
	assert_true(bool(res.get("ok", false)), label + " dots on rings (worst %.3f px)" % float(res.get("max_deviation_px", -1.0)))


func test_all_shapes_coincide(src: int = use_parameters([3, 9, 15, 16, 25, 26, 27, 28, 30, 31, 32, 8, 20, 23])) -> void:
	var sp := _layers_spawner(src)
	await idle(6)
	assert_eq(sp.debug_get_layer_rings().size(), 2, "src %d draws 2 rings" % src)
	_check_coincidence(sp, 2.5 if src == 20 else 2.0, "src %d" % src)


func test_fill_deals_coincide() -> void:
	var sp := _layers_spawner(25)
	await idle(6)
	sp.helper_outline_layer_fill = 1
	sp.helper_outline_layer_side = 1
	await idle(6)
	_check_coincidence(sp, 2.0, "circle sequential+inward")
	sp.helper_outline_layer_fill = 2
	sp.helper_outline_layer_side = 0
	await idle(6)
	_check_coincidence(sp, 2.0, "circle outer-first")
	sp.helper_outline_layer_fill = 3
	await idle(6)
	_check_coincidence(sp, 2.0, "circle pingpong")


func test_twist_caps_scales_curve() -> void:
	var sp := _layers_spawner(25)
	await idle(6)
	sp.helper_outline_layer_twist = 3
	await idle(6)
	_check_coincidence(sp, 2.0, "circle twist")
	sp.helper_outline_layer_twist = 0
	sp.helper_outline_layer_max_dots = 2
	await idle(6)
	_check_coincidence(sp, 2.0, "circle capped")
	sp.helper_outline_layer_max_dots = 0
	sp.helper_outline_layer_scales = PackedFloat32Array([1.0, 1.5, 1.6])
	await idle(6)
	_check_coincidence(sp, 2.0, "circle custom scales")
	sp.helper_outline_layer_scales = PackedFloat32Array()
	sp.helper_outline_layer_scale_curve = 1
	await idle(6)
	_check_coincidence(sp, 2.0, "circle exponential")


func test_side_start_layout_distribution() -> void:
	var sp := _layers_spawner(26)
	sp.helper_outline_layer_count = 4
	sp.helper_outline_layer_scale = 0.2
	await idle(6)
	for src in [26, 15, 25]:
		sp.pattern_source = src
		await idle(6)
		for side in [0, 1, 2]:
			sp.helper_outline_layer_side = side
			await idle(6)
			_check_coincidence(sp, 2.0, "src %d side %d" % [src, side])
	sp.helper_outline_layer_side = 0
	sp.helper_outline_layer_count = 3
	for start_off in [0, 2]:
		sp.helper_outline_layer_start_offset = start_off
		await idle(6)
		_check_coincidence(sp, 2.0, "rect start_offset %d" % start_off)
	sp.helper_outline_layer_start_offset = 0
	for layout in [0, 1]:
		sp.helper_outline_layer_layout = layout
		await idle(6)
		_check_coincidence(sp, 2.0, "rect layout %d" % layout)
	sp.helper_outline_layer_layout = 1
	for dist in [0, 1]:
		sp.helper_outline_distribution = dist
		await idle(6)
		_check_coincidence(sp, 2.0, "rect distribution %d" % dist)


func test_corner_knobs() -> void:
	var sp := _layers_spawner(26)
	await idle(6)
	for pri in [0, 1, 2]:
		sp.helper_outline_corner_priority = pri
		await idle(6)
		_check_coincidence(sp, 2.0, "rect priority %d" % pri)
	sp.helper_outline_corner_priority = 0
	for facing in [0, 1, 2]:
		sp.helper_outline_corner_facing = facing
		await idle(6)
		_check_coincidence(sp, 2.0, "rect facing %d" % facing)
	sp.helper_outline_corner_facing = 0
	for mode in [0, 1]:
		sp.helper_outline_corner_mode = mode
		await idle(6)
		_check_coincidence(sp, 2.0, "rect corner mode %d" % mode)
	sp.helper_outline_corner_mode = 0
	sp.helper_outline_edge_margin = 20.0
	await idle(6)
	_check_coincidence(sp, 2.0, "rect margin")
	sp.helper_outline_edge_margin = 0.0
	sp.pattern_source = 30
	sp.helper_outline_corner_facing = 1
	await idle(6)
	_check_coincidence(sp, 2.0, "triangle miter")
	sp.helper_outline_corner_facing = 0
	sp.pattern_source = 15
	sp.helper_outline_corner_facing = 2
	await idle(6)
	_check_coincidence(sp, 2.5, "star smooth")


func test_dense_star() -> void:
	var sp := _layers_spawner(15)
	sp.helper_star_points = 25
	sp.helper_bullets_amount = 55
	await idle(6)
	_check_coincidence(sp, 2.5, "star 25pt dense")
