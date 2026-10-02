extends BlastTest
## Preview parity under rotation: dots sit on the rotated track, rotated
## corners land in global space, the fan cone centers on the marker's
## rotation + direction, and the unrotated layout stays exact.


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
		if not d.is_finite():
			continue
		var best := 1e30
		for k in range(track.size() - 1):
			if track[k].is_finite() and track[k + 1].is_finite():
				best = minf(best, _seg_dist(d, track[k], track[k + 1]))
		if best < 1e29:
			worst = maxf(worst, best)
	return worst


func test_rotated_rectangle_dots_on_track() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RECTANGLE, 4)
	sp.helper_rectangle_size = Vector2(300, 200)
	sp.rotation = PI / 4.0
	await idle(8)
	var dots := sp.debug_get_preview_dot_points()
	var track := sp.debug_get_preview_track_points()
	assert_eq(dots.size(), 4, "four dots snapshotted")
	assert_gte(track.size(), 4, "track snapshotted")
	assert_lt(_max_dev(dots, track), 2.0, "dots sit on the rotated track")
	var holder: Node2D = sp.get_node_or_null("~BlastBulletsPatternPreview")
	assert_not_null(holder, "preview holder present")
	var expected: Vector2 = sp.global_transform * Vector2(150, 100)
	var best := 1e30
	for q in track:
		if q.is_finite():
			best = minf(best, (holder.global_transform * q).distance_to(expected))
	assert_lt(best, 2.0, "rotated corner on the global track")


func test_rotated_star_dots_on_track() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 10)
	sp.rotation = PI / 3.0
	await idle(8)
	var dots := sp.debug_get_preview_dot_points()
	var track := sp.debug_get_preview_track_points()
	assert_eq(dots.size(), 10)
	assert_gte(track.size(), 10)
	assert_lt(_max_dev(dots, track), 3.0, "star dots on the rotated track")


func test_fan_cone_centered_in_holder_frame() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_FAN, 5)
	sp.helper_fan_spread = 1.0
	sp.helper_fan_direction_angle = 0.0
	sp.rotation = PI / 4.0
	await idle(8)
	var worst := 0.0
	var counted := 0
	for q in sp.debug_get_preview_track_points():
		if q.is_finite() and q.length() >= 140.0:
			counted += 1
			worst = maxf(worst, absf(angle_difference(q.angle(), 0.0)))
	assert_gt(counted, 0, "cone rim sampled")
	assert_lt(worst, 0.61, "cone centered on holder-frame 0")


func test_unrotated_layout_exact() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RECTANGLE, 4)
	sp.helper_rectangle_size = Vector2(300, 200)
	await idle(8)
	var track := sp.debug_get_preview_track_points()
	assert_lt(_max_dev(sp.debug_get_preview_dot_points(), track), 2.0, "unrotated coincidence exact")
	var nearest := 1e30
	for q in track:
		if q.is_finite():
			nearest = minf(nearest, q.distance_to(Vector2(150, 100)))
	assert_lt(nearest, 2.0, "axis corner on the track")
