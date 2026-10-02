extends BlastTest
## Layer rings never bridge the strips the blue track splits (INF separators
## reach shape_loop): FAN petal arcs (outward and inward layers), grid rows;
## dots sit on the track; an unlayered flower has no rings.


func _seg_dist(p: Vector2, a: Vector2, b: Vector2) -> float:
	var ab := b - a
	var denom := ab.length_squared()
	if denom <= 0.00000001:
		return p.distance_to(a)
	var t: float = clampf((p - a).dot(ab) / denom, 0.0, 1.0)
	return p.distance_to(a + ab * t)


func _runs(pts: PackedVector2Array) -> Array:
	var out: Array = []
	var cur := PackedVector2Array()
	for q in pts:
		if not q.is_finite():
			if cur.size() >= 2:
				out.append(cur)
			cur = PackedVector2Array()
			continue
		cur.append(q)
	if cur.size() >= 2:
		out.append(cur)
	return out


func _worst_to_runs(pts: PackedVector2Array, runs: Array) -> float:
	var worst := 0.0
	for p in pts:
		if not p.is_finite():
			continue
		var best := 1e30
		for r in runs:
			var run: PackedVector2Array = r
			for k in range(run.size() - 1):
				if run[k].is_finite() and run[k + 1].is_finite():
					best = minf(best, _seg_dist(p, run[k], run[k + 1]))
		if best < 1e29:
			worst = maxf(worst, best)
	return worst


func _flower(petals: int, per_petal: int, radius: float, placement: int) -> BulletSpawner2D:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER, petals * per_petal)
	sp.helper_flower_type = 0 # FAN
	sp.helper_flower_petals = petals
	sp.helper_flower_bullets_per_petal = per_petal
	sp.helper_flower_radius = radius
	sp.helper_outline_placement = placement
	return sp


func _assert_rings_on_scaled_track(sp: BulletSpawner2D, track_runs: Array) -> void:
	var rings: Array = sp.debug_get_layer_rings()
	for li in rings.size():
		var f: float = BulletFactory2D.helper_layer_scale_factor(li + 1, sp.helper_outline_layer_scale, sp.helper_outline_layer_side, sp.helper_outline_layer_scale_curve, sp.helper_outline_layer_scales)
		var scaled: Array = []
		for r in track_runs:
			var srun := PackedVector2Array()
			for q in (r as PackedVector2Array):
				srun.append(q * f)
			scaled.append(srun)
		assert_lt(_worst_to_runs(rings[li], scaled), 4.0, "ring %d never bridges petal arcs" % li)


func test_fan_rings_never_bridge_petals() -> void:
	var sp := _flower(6, 5, 140.0, 1)
	sp.helper_outline_layer_count = 4
	await idle(8)
	var track_runs := _runs(sp.debug_get_preview_track_points())
	assert_gte(track_runs.size(), 6, "track splits six petal arcs")
	assert_eq(sp.debug_get_layer_rings().size(), 3, "three extra layer rings")
	_assert_rings_on_scaled_track(sp, track_runs)


func test_inward_rings_stay_on_arcs() -> void:
	var sp := _flower(5, 4, 120.0, 1)
	sp.helper_outline_layer_count = 3
	sp.helper_outline_layer_side = 1 # INWARD
	await idle(8)
	var runs := _runs(sp.debug_get_preview_track_points())
	assert_gte(runs.size(), 5, "five petal arcs tracked")
	assert_eq(sp.debug_get_layer_rings().size(), 2, "two inward rings")
	_assert_rings_on_scaled_track(sp, runs)


func test_dots_on_track_unlayered() -> void:
	var sp := _flower(6, 5, 140.0, 0)
	await idle(8)
	var dots := sp.debug_get_preview_dot_points()
	assert_eq(dots.size(), 30, "thirty dots")
	assert_lt(_worst_to_runs(dots, _runs(sp.debug_get_preview_track_points())), 3.0, "dots on the track")
	assert_true(sp.debug_get_layer_rings().is_empty(), "no rings without layers")


func test_grid_rows_split() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_GRID, 12)
	sp.helper_grid_rows_per_column = 4
	await idle(8)
	assert_eq(_runs(sp.debug_get_preview_track_points()).size(), 4, "one strip per grid row")
