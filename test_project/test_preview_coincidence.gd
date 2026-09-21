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
	# New deals coincide too (debug pairs geometrically, deal-agnostic).
	spawner.helper_outline_layer_fill = 2
	spawner.helper_outline_layer_side = 0
	await process_frame
	var res3: Dictionary = spawner.debug_check_layer_coincidence(2.0)
	_check(res3.get("checked", false) and bool(res3.get("ok", false)), "circle outer-first ok")
	spawner.helper_outline_layer_fill = 3
	await process_frame
	var res4: Dictionary = spawner.debug_check_layer_coincidence(2.0)
	_check(res4.get("checked", false) and bool(res4.get("ok", false)), "circle pingpong ok")
	# Twist, caps, custom scales and exponential curve only move dots along
	# the same rings.
	spawner.helper_outline_layer_fill = 0
	spawner.helper_outline_layer_twist = 3
	await process_frame
	var res5: Dictionary = spawner.debug_check_layer_coincidence(2.0)
	_check(res5.get("checked", false) and bool(res5.get("ok", false)), "circle twist ok")
	spawner.helper_outline_layer_twist = 0
	spawner.helper_outline_layer_max_dots = 2
	await process_frame
	var res6: Dictionary = spawner.debug_check_layer_coincidence(2.0)
	_check(res6.get("checked", false) and bool(res6.get("ok", false)), "circle capped ok")
	spawner.helper_outline_layer_max_dots = 0
	spawner.helper_outline_layer_scales = PackedFloat32Array([1.0, 1.5, 1.6])
	await process_frame
	var res7: Dictionary = spawner.debug_check_layer_coincidence(2.0)
	_check(res7.get("checked", false) and bool(res7.get("ok", false)), "circle custom scales ok")
	spawner.helper_outline_layer_scales = PackedFloat32Array()
	spawner.helper_outline_layer_scale_curve = 1
	await process_frame
	var res8: Dictionary = spawner.debug_check_layer_coincidence(2.0)
	_check(res8.get("checked", false) and bool(res8.get("ok", false)), "circle exponential ok")
	# Every layers-related knob keeps dots on rings (rect + star + circle).
	spawner.helper_outline_layer_scale_curve = 0
	spawner.helper_outline_layer_count = 4
	spawner.helper_outline_layer_scale = 0.2
	for src in [26, 15, 25]:
		spawner.pattern_source = src
		await process_frame
		for side in [0, 1, 2]:
			spawner.helper_outline_layer_side = side
			await process_frame
			var rsd: Dictionary = spawner.debug_check_layer_coincidence(2.0)
			_check(rsd.get("checked", false) and bool(rsd.get("ok", false)), "src %d side %d ok" % [src, side])
	spawner.helper_outline_layer_side = 0
	spawner.helper_outline_layer_count = 3
	for start_off in [0, 2]:
		spawner.helper_outline_layer_start_offset = start_off
		await process_frame
		var rso: Dictionary = spawner.debug_check_layer_coincidence(2.0)
		_check(rso.get("checked", false) and bool(rso.get("ok", false)), "rect start_offset %d ok" % start_off)
	spawner.helper_outline_layer_start_offset = 0
	for layout in [0, 1]:
		spawner.helper_outline_layer_layout = layout
		await process_frame
		var rlo: Dictionary = spawner.debug_check_layer_coincidence(2.0)
		_check(rlo.get("checked", false) and bool(rlo.get("ok", false)), "rect layout %d ok" % layout)
	spawner.helper_outline_layer_layout = 1
	for dist in [0, 1]:
		spawner.helper_outline_distribution = dist
		await process_frame
		var rdi: Dictionary = spawner.debug_check_layer_coincidence(2.0)
		_check(rdi.get("checked", false) and bool(rdi.get("ok", false)), "rect distribution %d ok" % dist)
	spawner.helper_outline_distribution = 1
	# Corner knobs (priority/mode/facing/margin) never move dots off rings.
	spawner.pattern_source = 26
	for pri in [0, 1, 2]:
		spawner.helper_outline_corner_priority = pri
		await process_frame
		var rpr: Dictionary = spawner.debug_check_layer_coincidence(2.0)
		_check(rpr.get("checked", false) and bool(rpr.get("ok", false)), "rect priority %d ok" % pri)
	spawner.helper_outline_corner_priority = 0
	for facing in [0, 1, 2]:
		spawner.helper_outline_corner_facing = facing
		await process_frame
		var rfa: Dictionary = spawner.debug_check_layer_coincidence(2.0)
		_check(rfa.get("checked", false) and bool(rfa.get("ok", false)), "rect facing %d ok" % facing)
	spawner.helper_outline_corner_facing = 0
	for mode in [0, 1]:
		spawner.helper_outline_corner_mode = mode
		await process_frame
		var rmo: Dictionary = spawner.debug_check_layer_coincidence(2.0)
		_check(rmo.get("checked", false) and bool(rmo.get("ok", false)), "rect corner mode %d ok" % mode)
	spawner.helper_outline_corner_mode = 0
	spawner.helper_outline_edge_margin = 20.0
	await process_frame
	var rma: Dictionary = spawner.debug_check_layer_coincidence(2.0)
	_check(rma.get("checked", false) and bool(rma.get("ok", false)), "rect margin ok")
	spawner.helper_outline_edge_margin = 0.0
	# Triangle apex MITER + star SMOOTH still coincide (facings don't move dots).
	spawner.pattern_source = 30
	spawner.helper_outline_corner_facing = 1
	await process_frame
	var rtm: Dictionary = spawner.debug_check_layer_coincidence(2.0)
	_check(rtm.get("checked", false) and bool(rtm.get("ok", false)), "triangle miter ok")
	spawner.helper_outline_corner_facing = 0
	spawner.pattern_source = 15
	spawner.helper_outline_corner_facing = 2
	await process_frame
	var rss: Dictionary = spawner.debug_check_layer_coincidence(2.5)
	_check(rss.get("checked", false) and bool(rss.get("ok", false)), "star smooth ok")
	spawner.helper_outline_corner_facing = 0
	# User-reported star density (25 points, 55 bullets, layers): dense
	# starburst outline still coincides.
	spawner.helper_star_points = 25
	spawner.helper_bullets_amount = 55
	await process_frame
	var rstar: Dictionary = spawner.debug_check_layer_coincidence(2.5)
	_check(rstar.get("checked", false) and bool(rstar.get("ok", false)), "star 25pt dense ok (worst %.3f)" % float(rstar.get("max_deviation_px", -1.0)))
	spawner.helper_star_points = 5
	spawner.helper_bullets_amount = 24
	print("----")
	if failures == 0:
		print("ALL PREVIEW COINCIDENCE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
