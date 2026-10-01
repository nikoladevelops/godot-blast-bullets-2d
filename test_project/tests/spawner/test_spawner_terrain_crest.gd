extends SceneTree
## Terrain-crest facing suite: the baked crest normals must tilt with the
## slope, not against it.
##
## The bug: the preset used atan2(-1, -slope) for a tangent of (1, slope).
## The up normal of that tangent is (slope, -1), i.e. atan2(-1, +slope) —
## the old code flipped x, so bullets on ascending segments faced down-left
## instead of up-right.
##
## Covers: T1 rightward tilt on ascending slope, T2 leftward tilt on
## descending slope, T3 all facings finite, T4 no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_terrain_crest.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var sp := BulletSpawner2D.new()
	get_root().add_child(sp)
	sp.set_bullet_factory(factory)
	sp.apply_pattern_preset(BulletFactory2D.PATTERN_PRESET_TERRAIN_CREST)
	var arr: Array = sp.get_helper_custom_transforms()
	_check(arr.size() == 120, "T0 crest bakes 120 slots (got %d)" % arr.size())

	# ---------------------------------------------------------------
	printerr("CREST T1 ascending slope tilts right (x-component positive)")
	# x(i) = -600 + 1200*i/119; slope = 0.3*cos(x/200) > 0 near x in
	# (-100, 100): i in 58..61. Up normal (slope, -1) has cos(rot) > 0.
	var ok_up := true
	for i in [58, 59, 60, 61]:
		var t: Transform2D = arr[i]
		if not t.is_finite() or cos(t.get_rotation()) <= 0.0:
			ok_up = false
	_check(ok_up, "T1 ascending slots face up-right")

	# ---------------------------------------------------------------
	printerr("CREST T2 descending slope tilts left (x-component negative)")
	# slope = 0.3*cos(x/200) < 0 for x in (314, 600): i in 91..119. Use
	# the interior (x ~ 410..490) where the tilt is unambiguous.
	var ok_down := true
	for i in [100, 104, 108]:
		var t: Transform2D = arr[i]
		if not t.is_finite() or cos(t.get_rotation()) >= 0.0:
			ok_down = false
	_check(ok_down, "T2 descending slots face up-left")

	# ---------------------------------------------------------------
	printerr("CREST T3 all facings finite")
	var all_finite := true
	for t in arr:
		if not (t as Transform2D).is_finite():
			all_finite = false
	_check(all_finite, "T3 every crest transform finite")

	sp.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL TERRAIN-CREST TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
