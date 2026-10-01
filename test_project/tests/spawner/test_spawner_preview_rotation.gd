extends SceneTree
## Preview rotation-parity suite: the gizmo must rotate with the marker.
##
## The bug: for marker-local shapes (rectangle, square, polygon, triangle,
## trapezoid, diamond, star) the volley composes marker.xform(local) but the
## preview track drew track_origin + offset (translation only), so rotating
## the marker spun the dots and left the blue track behind. The fan cone had
## the same defect (missing marker rotation in its center angle). Layer rings
## inherited it via the unrotated shape loop.
##
## Covers: T1 rectangle dots sit on the rotated track + rotated corner present,
## T2 star track follows marker rotation, T3 fan cone centers on
## marker rotation + direction (holder frame), T4 unrotated marker still
## exact (no regression), T5 no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_preview_rotation.gd
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

func _max_dev(dots: PackedVector2Array, track: PackedVector2Array) -> float:
	var worst := 0.0
	for d in dots:
		if not d.is_finite():
			continue
		var best := 1e30
		for k in range(track.size() - 1):
			var a := track[k]
			var b := track[k + 1]
			if not a.is_finite() or not b.is_finite():
				continue
			best = minf(best, _seg_dist(d, a, b))
		if best < 1e29:
			worst = maxf(worst, best)
	return worst

func _nearest(track: PackedVector2Array, p: Vector2) -> float:
	var best := 1e30
	for q in track:
		if q.is_finite():
			best = minf(best, p.distance_to(q))
	return best

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

func _settle() -> void:
	for i in 8:
		await process_frame

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("PREVIEWROT T1 rectangle track follows a rotated marker")
	var sp := _preview_spawner(factory)
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RECTANGLE
	sp.helper_rectangle_size = Vector2(300, 200)
	sp.helper_bullets_amount = 4
	sp.rotation = PI / 4.0
	await _settle()
	var dots := sp.debug_get_preview_dot_points()
	var track := sp.debug_get_preview_track_points()
	_check(dots.size() == 4, "T1 four dots snapshotted (got %d)" % dots.size())
	_check(track.size() >= 4, "T1 track snapshotted (got %d)" % track.size())
	_check(_max_dev(dots, track) < 2.0, "T1 dots sit on the rotated track (dev=%.2f)" % _max_dev(dots, track))
	# Rotation parity in GLOBAL space (holder frame cancels the marker
	# rotation for both sides, so it cannot discriminate): the track must
	# carry the same marker rotation as the volley geometry.
	var holder: Node = sp.get_node_or_null("~BlastBulletsPatternPreview")
	_check(holder != null, "T1 preview holder present")
	var expected: Vector2 = sp.global_transform * Vector2(150, 100)
	var best := 1e30
	for q in track:
		if q.is_finite():
			best = minf(best, (holder.global_transform * q).distance_to(expected))
	_check(best < 2.0, "T1 rotated corner on global track (miss=%.2f)" % best)
	sp.queue_free()
	await process_frame

	# ---------------------------------------------------------------
	printerr("PREVIEWROT T2 star track follows a rotated marker")
	var sp2 := _preview_spawner(factory)
	sp2.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_STAR
	sp2.helper_bullets_amount = 10
	sp2.rotation = PI / 3.0
	await _settle()
	var dots2 := sp2.debug_get_preview_dot_points()
	var track2 := sp2.debug_get_preview_track_points()
	_check(dots2.size() == 10 and track2.size() >= 10, "T2 star snapshot complete")
	_check(_max_dev(dots2, track2) < 3.0, "T2 star dots on rotated track (dev=%.2f)" % _max_dev(dots2, track2))
	sp2.queue_free()
	await process_frame

	# ---------------------------------------------------------------
	printerr("PREVIEWROT T3 fan cone centers on marker rotation + direction")
	var sp3 := _preview_spawner(factory)
	sp3.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_FAN
	sp3.helper_bullets_amount = 5
	sp3.helper_fan_spread = 1.0
	sp3.helper_fan_direction_angle = 0.0
	sp3.rotation = PI / 4.0
	await _settle()
	var track3 := sp3.debug_get_preview_track_points()
	var worst_ang := 0.0
	var counted := 0
	for q in track3:
		if not q.is_finite() or q.length() < 140.0:
			continue
		counted += 1
		worst_ang = maxf(worst_ang, absf(angle_difference(q.angle(), 0.0)))
	_check(counted > 0, "T3 cone rim sampled (%d)" % counted)
	_check(worst_ang < 0.61, "T3 cone centered on holder-frame 0 (worst=%.2f rad)" % worst_ang)
	sp3.queue_free()
	await process_frame

	# ---------------------------------------------------------------
	printerr("PREVIEWROT T4 unrotated marker stays exact")
	var sp4 := _preview_spawner(factory)
	sp4.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RECTANGLE
	sp4.helper_rectangle_size = Vector2(300, 200)
	sp4.helper_bullets_amount = 4
	sp4.rotation = 0.0
	await _settle()
	_check(_max_dev(sp4.debug_get_preview_dot_points(), sp4.debug_get_preview_track_points()) < 2.0, "T4 unrotated coincidence exact")
	_check(_nearest(sp4.debug_get_preview_track_points(), Vector2(150, 100)) < 2.0, "T4 axis corner on track")
	sp4.queue_free()
	await process_frame

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL PREVIEW-ROTATION TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
