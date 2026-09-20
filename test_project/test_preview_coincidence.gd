extends SceneTree

# End-to-end proof that preview dots sit on the yellow layer rings.
# Run with:
#   godot --headless --path test_project --script test_preview_coincidence.gd
# Exit code 0 = all shapes pass, 1 = a failure printed below.
#
# Drives a real BulletSpawner2D (runtime preview on) through every
# outline-layout shape and asks debug_check_layer_coincidence whether the
# drawn dots match the drawn rings. Each extra layer re-spawns the selected
# shape scaled about the loop center (blue outline, yellow repeats).
# Tolerance 2.0 px (2.5 for rose): sharp shapes read exact (~0.00), dense
# sweeps sub-pixel; weaves (rose/lissajous) read higher where adjacent lobes
# pass near each other and the nearest-segment pairing crosses over (a
# debug-metric artifact, not a layout mismatch: the volley-side scaling in
# test_layer_rings.gd is exact).
var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	factory.name = "Factory"
	get_root().add_child(factory)
	var spawner := BulletSpawner2D.new()
	spawner.name = "Spawner"
	get_root().add_child(spawner)
	await process_frame
	await process_frame
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(DirectionalBulletsData2D.new())
	spawner.show_preview_during_runtime = true
	spawner.show_pattern_preview = true
	spawner.helper_outline_placement = 1 # LAYERS
	spawner.helper_outline_layer_count = 3
	spawner.helper_outline_layer_scale = 0.25
	spawner.helper_outline_layer_side = 0
	spawner.helper_outline_layer_fill = 0
	spawner.helper_bullets_amount = 24
	await process_frame
	# pattern_source ids: ring=3, ellipse=9, star=15, heart=16, circle=25,
	# rectangle=26, square=27, polygon=28, triangle=30, trapezoid=31,
	# diamond=32, flower=8, rose=20, lissajous=23.
	for src in [3, 9, 15, 16, 25, 26, 27, 28, 30, 31, 32, 8, 20, 23]:
		spawner.pattern_source = src
		await process_frame
		var rings: Array = spawner.debug_get_layer_rings()
		# Rose weaves all lobes through the center, so the nearest-segment
		# pairing can cross to an adjacent lobe near the middle (debug-metric
		# artifact; the volley-side scaling in test_layer_rings.gd is exact).
		var tol := 2.5 if src == 20 else 2.0
		var res: Dictionary = spawner.debug_check_layer_coincidence(tol)
		_check(rings.size() == 2, "src %d draws 2 rings" % src)
		_check(res.get("checked", false), "src %d coincidence checked" % src)
		if res.get("checked", false):
			_check(bool(res.get("ok", false)), "src %d dots on rings (worst %.3f px)" % [src, float(res.get("max_deviation_px", -1.0))])
	# Sequential fill + inward side still coincide.
	spawner.pattern_source = 25
	spawner.helper_outline_layer_fill = 1
	spawner.helper_outline_layer_side = 1
	await process_frame
	var res2: Dictionary = spawner.debug_check_layer_coincidence(2.0)
	_check(res2.get("checked", false) and bool(res2.get("ok", false)), "circle sequential+inward ok")
	print("----")
	if failures == 0:
		print("ALL PREVIEW COINCIDENCE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
