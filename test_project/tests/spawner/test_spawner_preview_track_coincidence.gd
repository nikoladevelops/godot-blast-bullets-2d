extends BlastTest
## Invariant 4 for EVERY pattern source: the preview's base track is drawn
## where the bullets sit. Each preview dot (holder space, the same pipeline as
## the volley) must lie on the drawn track within 1.5 px times pattern_scale
## (dense sweeps are sampled at a fixed density in pattern space). Runs split on INF separators; a run closes only
## when it is the sole run and the track is flagged closed. Checked at two
## amounts (an odd one leaves partial rows and arms) and two generator poses
## (identity, then rotated + pattern-scaled, so marker-local and global
## tracks are both exercised). Children/Self draw no track; Scatter's track is
## the extent ring of its burst disc (dots inside); jittered rain/waterfall
## dots stay within the jitter box of their row.

const TOL := 1.5
const POSED_SCALE := 1.5
const AMOUNTS := [24, 7]


func _configure(sp: BulletSpawner2D, src: int) -> void:
	if src == BulletSpawner2D.PATTERN_FROM_HELPER_AIMED or src == BulletSpawner2D.PATTERN_FROM_HELPER_CORRIDOR:
		var t := Node2D.new()
		t.position = Vector2(300, -200)
		add(t)
		sp.set_helper_aimed_target(t)
	elif src == BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM:
		sp.set_helper_custom_transforms([Transform2D(0.3, Vector2(10, 0)), Transform2D(-0.2, Vector2(0, 25)), Transform2D(1.0, Vector2(-15, -5))])
	elif src == BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D:
		var path := Path2D.new()
		var c := Curve2D.new()
		for p in [Vector2(0, 0), Vector2(100, 40), Vector2(200, -20), Vector2(320, 30)]:
			c.add_point(p)
		path.curve = c
		add(path)
		sp.set_helper_path2d_node(path)


func _runs(track: PackedVector2Array, closed: bool) -> Array:
	var runs: Array = []
	var cur := PackedVector2Array()
	for p in track:
		if not p.is_finite():
			if not cur.is_empty():
				runs.append(cur)
			cur = PackedVector2Array()
			continue
		cur.append(p)
	if not cur.is_empty():
		runs.append(cur)
	if closed and runs.size() == 1 and (runs[0] as PackedVector2Array).size() > 2:
		var loop: PackedVector2Array = runs[0] # packed arrays copy: write it back
		loop.append(loop[0])
		runs[0] = loop
	return runs


func _seg_dist(p: Vector2, a: Vector2, b: Vector2) -> float:
	var ab := b - a
	var t := 0.0
	if ab.length_squared() > 1e-12:
		t = clampf((p - a).dot(ab) / ab.length_squared(), 0.0, 1.0)
	return p.distance_to(a + ab * t)


func _worst(dots: PackedVector2Array, runs: Array) -> float:
	var worst := 0.0
	for d in dots:
		var best := INF
		for run in runs:
			var r: PackedVector2Array = run
			if r.size() == 1:
				best = minf(best, d.distance_to(r[0]))
			for i in r.size() - 1:
				best = minf(best, _seg_dist(d, r[i], r[i + 1]))
		worst = maxf(worst, best)
	return worst


func _measure(src: int, amount: int, posed: bool, jitter := 0.0) -> Dictionary:
	var sp := make_preview_spawner(src, amount)
	sp.helper_rain_jitter = jitter
	sp.helper_waterfall_jitter = jitter
	if posed:
		sp.rotation = 0.7
		sp.pattern_scale = POSED_SCALE
	_configure(sp, src)
	await idle(6)
	var dots: PackedVector2Array = sp.debug_get_preview_dot_points()
	var track: PackedVector2Array = sp.debug_get_preview_track_points()
	var runs := _runs(track, sp.debug_get_preview_track_closed())
	var out := {"dots": dots, "track": track, "runs": runs, "worst": _worst(dots, runs) if not runs.is_empty() else -1.0}
	sp.queue_free()
	return out


func test_every_source_draws_its_track_under_its_bullets() -> void:
	var problems: Array = []
	var measured := 0
	for s in BulletPatterns2D.get_shapes():
		var src: int = s["id"]
		if src == BulletSpawner2D.PATTERN_FROM_CHILDREN or src == BulletSpawner2D.PATTERN_FROM_SELF or src == BulletSpawner2D.PATTERN_FROM_HELPER_SCATTER:
			continue
		for amount in AMOUNTS:
			for posed in [false, true]:
				var m: Dictionary = await _measure(src, amount, posed)
				measured += 1
				if (m["dots"] as PackedVector2Array).is_empty() or (m["runs"] as Array).is_empty():
					problems.append("%s (amount %d, posed %s): %d dots, %d track points" % [s["name"], amount, posed, (m["dots"] as PackedVector2Array).size(), (m["track"] as PackedVector2Array).size()])
				elif float(m["worst"]) > TOL * (POSED_SCALE if posed else 1.0):
					problems.append("%s (amount %d, posed %s): a dot sits %.2f px off the track" % [s["name"], amount, posed, m["worst"]])
	assert_eq(measured, 30 * AMOUNTS.size() * 2, "30 track-drawing sources x 2 amounts x 2 poses")
	assert_eq(problems, [], "every preview dot lies on its drawn track")


func test_children_and_self_draw_no_track() -> void:
	for src in [BulletSpawner2D.PATTERN_FROM_CHILDREN, BulletSpawner2D.PATTERN_FROM_SELF]:
		var m: Dictionary = await _measure(src, 4, false)
		assert_eq((m["dots"] as PackedVector2Array).size(), 1, "one dot at the generator (source %d)" % src)
		assert_true((m["track"] as PackedVector2Array).is_empty(), "no track (source %d)" % src)


func test_scatter_dots_stay_inside_the_drawn_burst_ring() -> void:
	for posed in [false, true]:
		var m: Dictionary = await _measure(BulletSpawner2D.PATTERN_FROM_HELPER_SCATTER, 24, posed)
		var ring: PackedVector2Array = m["track"]
		var center := Vector2.ZERO
		for p in ring:
			center += p
		center /= ring.size()
		var radius := 0.0
		for p in ring:
			radius = maxf(radius, p.distance_to(center))
		for d in m["dots"]:
			assert_lte((d as Vector2).distance_to(center), radius + 0.5, "every scatter dot inside the ring (posed %s)" % posed)


func test_jittered_rain_and_waterfall_stay_within_the_jitter_of_their_row() -> void:
	for src in [BulletSpawner2D.PATTERN_FROM_HELPER_RAIN, BulletSpawner2D.PATTERN_FROM_HELPER_WATERFALL]:
		var m: Dictionary = await _measure(src, 24, false, 6.0)
		# Each axis jitters by up to 6 px: at most 6 * sqrt(2) off the row.
		assert_lte(float(m["worst"]), 6.0 * sqrt(2.0) + 0.01, "source %d dots within the jitter box" % src)
