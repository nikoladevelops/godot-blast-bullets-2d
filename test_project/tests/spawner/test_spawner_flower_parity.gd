extends SceneTree
## Flower preview-parity suite: yellow rings must never bridge the strips the
## blue track splits.
##
## The bug: push_track_dict forwarded INF separators (petal arcs, grid rows,
## spiral arms) to the drawn track but NOT to shape_loop, which feeds the
## layer rings. The rings therefore built one merged run across strips and
## drew jump-chords the volley never flies, while the blue track correctly
## split them.
##
## Covers: T1 FAN rings stay within petal arcs (no chords), T2 dots sit on
## track and rings, T3 grid rows never bridge in rings, T4 unlayered flower
## unchanged, T5 no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_flower_parity.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

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
				var a := run[k]
				var b := run[k + 1]
				if a.is_finite() and b.is_finite():
					best = minf(best, _seg_dist(p, a, b))
		if best < 1e29:
			worst = maxf(worst, best)
	return worst

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
	printerr("FLOWER T1 FAN rings never bridge petal arcs")
	var sp := _preview_spawner(factory)
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER
	sp.helper_flower_type = 0 # FAN
	sp.helper_flower_petals = 6
	sp.helper_flower_bullets_per_petal = 5
	sp.helper_bullets_amount = 30
	sp.helper_flower_radius = 140.0
	sp.helper_outline_placement = 1 # LAYERS
	sp.helper_outline_layer_count = 4
	for i in 8:
		await process_frame
	var track := sp.debug_get_preview_track_points()
	var track_runs := _runs(track)
	_check(track_runs.size() >= 6, "T1 track splits six petal arcs (runs=%d)" % track_runs.size())
	var rings: Array = sp.debug_get_layer_rings()
	_check(rings.size() == 3, "T1 three extra layer rings (got %d)" % rings.size())
	# Rings are scaled copies of the base loop: compare each ring against
	# the track scaled by its own layer factor, so size never reads as drift.
	var bridged := false
	for li in rings.size():
		var f: float = BulletFactory2D.helper_layer_scale_factor(li + 1, sp.helper_outline_layer_scale, sp.helper_outline_layer_side, sp.helper_outline_layer_scale_curve, sp.helper_outline_layer_scales)
		var scaled: Array = []
		for r in track_runs:
			var run: PackedVector2Array = r
			var srun := PackedVector2Array()
			for q in run:
				srun.append(q * f)
			scaled.append(srun)
		# Every ring run must lie on some scaled track run: a bridging chord
		# leaves its midpoint far from every petal arc.
		if _worst_to_runs(rings[li], scaled) > 4.0:
			bridged = true
	_check(not bridged, "T1 no ring bridges across petal arcs")

	# ---------------------------------------------------------------
	printerr("FLOWER T2 dots sit on track (unlayered volley)")
	var sp2 := _preview_spawner(factory)
	sp2.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER
	sp2.helper_flower_type = 0
	sp2.helper_flower_petals = 6
	sp2.helper_flower_bullets_per_petal = 5
	sp2.helper_bullets_amount = 30
	sp2.helper_flower_radius = 140.0
	sp2.helper_outline_placement = 0
	for i in 8:
		await process_frame
	var dots := sp2.debug_get_preview_dot_points()
	var dots_track := sp2.debug_get_preview_track_points()
	_check(dots.size() == 30, "T2 thirty dots (got %d)" % dots.size())
	var dev_track := 0.0
	var dot_runs := _runs(dots_track)
	for d in dots:
		if d.is_finite():
			dev_track = maxf(dev_track, _worst_to_runs(PackedVector2Array([d]), dot_runs))
	_check(dev_track < 3.0, "T2 dots on track (dev=%.2f)" % dev_track)
	sp2.queue_free()
	await process_frame
	sp.queue_free()
	await process_frame

	# ---------------------------------------------------------------
	printerr("FLOWER T3 grid rows split; inward FAN rings stay on arcs")
	var sp3 := _preview_spawner(factory)
	sp3.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_GRID
	sp3.helper_bullets_amount = 12
	sp3.helper_grid_rows_per_column = 4
	for i in 8:
		await process_frame
	var track3 := sp3.debug_get_preview_track_points()
	var runs3 := _runs(track3)
	# 12 bullets / 4 rows = 3 columns x 4 rows: one strip per row.
	_check(runs3.size() == 4, "T3 track splits four rows (runs=%d)" % runs3.size())
	sp3.queue_free()
	await process_frame
	var sp3b := _preview_spawner(factory)
	sp3b.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER
	sp3b.helper_flower_type = 0
	sp3b.helper_flower_petals = 5
	sp3b.helper_flower_bullets_per_petal = 4
	sp3b.helper_bullets_amount = 20
	sp3b.helper_flower_radius = 120.0
	sp3b.helper_outline_placement = 1
	sp3b.helper_outline_layer_count = 3
	sp3b.helper_outline_layer_side = 1 # INWARD: rings shrink, same arcs
	for i in 8:
		await process_frame
	var track3b := sp3b.debug_get_preview_track_points()
	var runs3b := _runs(track3b)
	_check(runs3b.size() >= 5, "T3b five petal arcs tracked (runs=%d)" % runs3b.size())
	var rings3b: Array = sp3b.debug_get_layer_rings()
	_check(rings3b.size() == 2, "T3b two inward rings (got %d)" % rings3b.size())
	var bridged3 := false
	for li in rings3b.size():
		var f3: float = BulletFactory2D.helper_layer_scale_factor(li + 1, sp3b.helper_outline_layer_scale, sp3b.helper_outline_layer_side, sp3b.helper_outline_layer_scale_curve, sp3b.helper_outline_layer_scales)
		var scaled3: Array = []
		for r in runs3b:
			var run: PackedVector2Array = r
			var srun := PackedVector2Array()
			for q in run:
				srun.append(q * f3)
			scaled3.append(srun)
		if _worst_to_runs(rings3b[li], scaled3) > 4.0:
			bridged3 = true
	_check(not bridged3, "T3b inward rings never bridge petals")
	sp3b.queue_free()
	await process_frame

	# ---------------------------------------------------------------
	printerr("FLOWER T4 unlayered flower unchanged")
	var sp4 := _preview_spawner(factory)
	sp4.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER
	sp4.helper_flower_type = 0
	sp4.helper_flower_petals = 6
	sp4.helper_flower_bullets_per_petal = 5
	sp4.helper_bullets_amount = 30
	sp4.helper_flower_radius = 140.0
	sp4.helper_outline_placement = 0 # ON_OUTLINE
	for i in 8:
		await process_frame
	_check(sp4.debug_get_preview_dot_points().size() == 30, "T4 dots complete without layers")
	_check(sp4.debug_get_layer_rings().is_empty(), "T4 no rings without layers")
	sp4.queue_free()
	await process_frame

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL FLOWER-PARITY TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
