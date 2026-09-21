extends SceneTree

# Headless proof for the outline-layer design. Run with:
#   godot --headless --path test_project --script test_layer_rings.gd
# Exit code 0 = all checks pass, 1 = a failure printed below.
#
# Design under test: each extra layer re-spawns the selected shape scaled
# about the loop center (the marker origin — every loop generator builds
# centered shapes), like a second spawner with a bigger shape. The universal
# volley invariants checked here:
#   1. layer 0 sits exactly on the outline (coincides with the ON_OUTLINE volley)
#   2. every layered bullet sits exactly at center + (base - center) * s(layer)
#   3. facings never change across layers; corner-exact polygon slots face
#      along their edge (bullet 0 faces straight, never diagonally)
var failures := 0

const STEP := 0.25

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _approx(a: float, b: float, eps: float = 0.0001) -> bool:
	return absf(a - b) <= eps

func _initialize() -> void:
	_test_scale_factor()
	_test_layer_index()
	_test_circle_layers()
	_test_ring_layers()
	_test_rect_layers()
	_test_polygon_layers()
	_test_sequential_fill()
	_test_inward_and_both()
	_test_star_heart_emit()
	_test_translation_and_collapse()
	_test_outer_first_volley()
	_test_pingpong_volley()
	_test_twist()
	_test_max_dots()
	_test_custom_scales_and_curve()
	_test_anchored_corners()
	_test_symmetric_distribution()
	_test_small_corner_seats()
	_test_star_edge_walk()
	_test_ellipse_full_no_seam()
	_test_layer_layout_modes()
	_test_corner_priority()
	_test_corner_mode_margin()
	_test_ellipse_arc_even()
	_test_circle_screenshot_settings()
	_test_debug_verify()
	print("----")
	if failures == 0:
		print("ALL LAYER RING TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)

func _test_scale_factor() -> void:
	_check(_approx(BulletFactory2D.helper_layer_scale_factor(0, STEP, 0), 1.0), "scale L0")
	_check(_approx(BulletFactory2D.helper_layer_scale_factor(2, STEP, 0), 1.5), "scale outward")
	_check(_approx(BulletFactory2D.helper_layer_scale_factor(2, STEP, 1), 1.0 / 1.5), "scale inward")
	_check(_approx(BulletFactory2D.helper_layer_scale_factor(1, STEP, 2), 1.25), "scale both L1=+")
	_check(_approx(BulletFactory2D.helper_layer_scale_factor(2, STEP, 2), 1.0 / 1.25), "scale both L2=-")
	_check(_approx(BulletFactory2D.helper_layer_scale_factor(3, STEP, 2), 1.5), "scale both L3=+2")

func _layer_of(i: int, n: int, count: int, fill: int, start: int) -> int:
	return BulletFactory2D.helper_bullet_layer_index(i, n, count, fill, start)

func _test_layer_index() -> void:
	# Interleaved round-robin preserves winding order.
	_check(_layer_of(0, 12, 3, 0, 0) == 0, "deal interleaved i0")
	_check(_layer_of(1, 12, 3, 0, 0) == 1, "deal interleaved i1")
	_check(_layer_of(3, 12, 3, 0, 0) == 0, "deal interleaved wraps")
	# Sequential fills contiguous chunks: 12 bullets over 3 layers.
	_check(_layer_of(0, 12, 3, 1, 0) == 0, "deal sequential first chunk")
	_check(_layer_of(3, 12, 3, 1, 0) == 0, "deal sequential chunk boundary low")
	_check(_layer_of(4, 12, 3, 1, 0) == 1, "deal sequential second chunk")
	_check(_layer_of(11, 12, 3, 1, 0) == 2, "deal sequential last")
	# Start offset rotates the sequential deal (wraps around).
	_check(_layer_of(0, 12, 3, 1, 1) == 1, "deal sequential start offset")
	_check(_layer_of(11, 12, 3, 1, 1) == 0, "deal sequential start wraps")
	# Outer first fills from the outermost ring inward.
	_check(_layer_of(0, 12, 3, 2, 0) == 2, "deal outer first starts outside")
	_check(_layer_of(11, 12, 3, 2, 0) == 0, "deal outer first ends on outline")
	_check(_layer_of(0, 12, 3, 2, 1) == 0, "deal outer first start wraps")
	# Ping-pong waves 0..last..0 (start offset ignored).
	_check(_layer_of(0, 12, 3, 3, 0) == 0, "deal pingpong up")
	_check(_layer_of(2, 12, 3, 3, 0) == 2, "deal pingpong peak")
	_check(_layer_of(3, 12, 3, 3, 0) == 1, "deal pingpong down")
	_check(_layer_of(4, 12, 3, 3, 0) == 0, "deal pingpong wraps")
	# Degenerate inputs yield layer 0.
	_check(_layer_of(5, 12, 1, 0, 0) == 0, "deal single layer")
	_check(_layer_of(0, 0, 3, 0, 0) == 0, "deal empty slots")

# Universal invariants for one (base, layered) volley pair: layer 0 coincides
# with the outline, and every layered bullet is its base slot scaled about
# the marker origin. Facings never change across layers.
func _check_pair(base: Array, layered: Array, center: Vector2, count: int, step: float, side: int, label: String) -> void:
	_check(base.size() == layered.size(), label + " sizes match")
	var n: int = layered.size()
	var worst_pos := 0.0
	var worst_face := 0.0
	for i in n:
		var bp: Vector2 = (base[i] as Transform2D).origin
		var lp: Vector2 = (layered[i] as Transform2D).origin
		var layer: int = _layer_of(i, n, count, 0, 0)
		var s: float = BulletFactory2D.helper_layer_scale_factor(layer, step, side)
		var expect: Vector2 = center + (bp - center) * s
		worst_pos = maxf(worst_pos, lp.distance_to(expect))
		var fa: float = (base[i] as Transform2D).get_rotation()
		var fb: float = (layered[i] as Transform2D).get_rotation()
		worst_face = maxf(worst_face, absf(wrapf(fb - fa, -PI, PI)))
	_check(worst_pos < 1.0, label + " scaled copies exact (worst %.3f px)" % worst_pos)
	_check(worst_face < 0.001, label + " facings unchanged")

func _ring_gaps_ok(layered: Array, n: int, count: int, fill: int, start: int, ratio: float) -> bool:
	# Per-ring gap uniformity for layered smooth volleys (volley order within
	# each ring is winding order, so consecutive members give ring gaps).
	for L in count:
		var ring: Array = []
		for i in n:
			if _layer_of(i, n, count, fill, start) == L:
				ring.append(layered[i])
		if ring.size() < 2:
			continue
		var stats: Dictionary = BulletFactory2D.debug_volley_gaps(ring)
		if float(stats.get("gap_ratio", 99.0)) > ratio:
			return false
	return true

func _test_circle_layers() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	var layered: Array = BulletFactory2D.helper_generate_transforms_circle(12, marker, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	# Scaled figure: outward layers grow the radius by the scale factor.
	var worst := 0.0
	for i in layered.size():
		var p: Vector2 = (layered[i] as Transform2D).origin
		var layer: int = _layer_of(i, layered.size(), 3, 0, 0)
		worst = maxf(worst, absf(p.distance_to(marker.origin) - 150.0 * (1.0 + STEP * layer)))
	_check(worst < 1.0, "circle radii scale uniformly (worst %.3f px)" % worst)
	# Even per ring: no decimated seam gap next to bullet 0.
	_check(_ring_gaps_ok(layered, 12, 3, 0, 0, 1.05), "circle rings even")
	# Facings stay radial on every ring.
	var dev := 0.0
	for i in layered.size():
		var t: Transform2D = layered[i]
		var radial: float = (t.origin - marker.origin).angle()
		dev = maxf(dev, absf(wrapf(t.get_rotation() - radial, -PI, PI)))
	_check(dev < 0.02, "circle facings radial (worst %.4f)" % dev)

func _test_ring_layers() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	var layered: Array = BulletFactory2D.helper_generate_transforms_ring(12, marker, 150.0, 0.0, TAU, true, false, true, 1.0, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(_ring_gaps_ok(layered, 12, 3, 0, 0, 1.05), "ring rings even")
	# Ring volleys live around the marker, never around the world origin.
	_check((layered[0] as Transform2D).origin.distance_to(marker.origin + Vector2(150, 0)) < 1.0, "ring sits at the marker")

func _test_rect_layers() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	var base: Array = BulletFactory2D.helper_generate_transforms_rectangle(8, marker, Vector2(300, 200), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var layered: Array = BulletFactory2D.helper_generate_transforms_rectangle(8, marker, Vector2(300, 200), true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	# Even-per-layer (default layout=1): each ring is its own symmetric loop,
	# so every ring carries corners and even gaps (not a decimated subset).
	_check_rect_layers_even(layered, marker, Vector2(300, 200), 3, "rectangle")
	# Legacy shared-loop layout still gives exact scaled copies.
	var legacy: Array = BulletFactory2D.helper_generate_transforms_rectangle(8, marker, Vector2(300, 200), true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1, 0)
	_check_pair(base, legacy, marker.origin, 3, STEP, 0, "rectangle-legacy")
	# Bullet 0 sits on the top-left corner facing straight up the top edge.
	_check(_approx((base[0] as Transform2D).get_rotation(), -PI * 0.5, 0.01), "rect bullet 0 faces straight")
	_check(_approx((layered[0] as Transform2D).get_rotation(), -PI * 0.5, 0.01), "rect layered bullet 0 faces straight")

func _test_polygon_layers() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	var base: Array = BulletFactory2D.helper_generate_transforms_polygon(12, marker, 6, 150.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var legacy: Array = BulletFactory2D.helper_generate_transforms_polygon(12, marker, 6, 150.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1, 0)
	_check_pair(base, legacy, marker.origin, 3, STEP, 0, "polygon-legacy")
	var layered: Array = BulletFactory2D.helper_generate_transforms_polygon(12, marker, 6, 150.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(layered.size() == 12, "polygon even-per-layer emits full count")
	_check(_layer_rings_have_corners(layered, 12, 3, 0, 0), "polygon every ring has corners")
	var sq_base: Array = BulletFactory2D.helper_generate_transforms_rectangle(4, marker, Vector2(200, 200), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(_approx((sq_base[0] as Transform2D).get_rotation(), -PI * 0.5, 0.01), "square bullet 0 faces straight")
	_check(BulletFactory2D.helper_generate_transforms_triangle(9, marker, 0, 200.0, 200.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0).size() == 9, "triangle emits in LAYERS")
	_check(BulletFactory2D.helper_generate_transforms_trapezoid(8, marker, 150.0, 280.0, 140.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0).size() == 8, "trapezoid emits in LAYERS")
	_check(BulletFactory2D.helper_generate_transforms_diamond(8, marker, 280.0, 200.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0).size() == 8, "diamond emits in LAYERS")

func _test_sequential_fill() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	# Sequential fills layer 0 first: the first chunk rides the base circle.
	var layered: Array = BulletFactory2D.helper_generate_transforms_circle(12, marker, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 1, 0, 0, PackedFloat32Array(), 0, 0)
	_check(layered.size() == 12, "sequential emits full count")
	var worst := 0.0
	for i in layered.size():
		var p: Vector2 = (layered[i] as Transform2D).origin
		var layer: int = _layer_of(i, 12, 3, 1, 0)
		worst = maxf(worst, absf(p.distance_to(marker.origin) - 150.0 * (1.0 + STEP * layer)))
	_check(worst < 1.0, "sequential fills layer-first (worst %.3f px)" % worst)

func _test_inward_and_both() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	var inward: Array = BulletFactory2D.helper_generate_transforms_circle(9, marker, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 1, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var worst := 0.0
	for i in inward.size():
		var p: Vector2 = (inward[i] as Transform2D).origin
		var layer: int = _layer_of(i, 9, 3, 0, 0)
		worst = maxf(worst, absf(p.distance_to(marker.origin) - 150.0 / (1.0 + STEP * layer)))
	_check(worst < 1.0, "inward crowds toward center (worst %.3f px)" % worst)
	var both: Array = BulletFactory2D.helper_generate_transforms_circle(9, marker, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 2, 0, 0, 0, PackedFloat32Array(), 0, 0)
	worst = 0.0
	for i in both.size():
		var p: Vector2 = (both[i] as Transform2D).origin
		var layer: int = _layer_of(i, 9, 3, 0, 0)
		var s: float = BulletFactory2D.helper_layer_scale_factor(layer, STEP, 2)
		worst = maxf(worst, absf(p.distance_to(marker.origin) - 150.0 * s))
	_check(worst < 1.0, "both alternates sides (worst %.3f px)" % worst)

func _test_star_heart_emit() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	var base_flower: Array = BulletFactory2D.helper_generate_transforms_flower(24, marker, 6, 5, 150.0, 0.5, 1.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 0.0, 45.0, 80.0, 6.0, 1.0)
	var layered_flower: Array = BulletFactory2D.helper_generate_transforms_flower(24, marker, 6, 5, 150.0, 0.5, 1.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 0.0, 45.0, 80.0, 6.0, 1.0)
	_check_pair(base_flower, layered_flower, marker.origin, 3, STEP, 0, "flower")
	var star: Array = BulletFactory2D.helper_generate_transforms_star(20, marker, 5, 150.0, 65.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(star.size() == 20, "star emits in LAYERS")
	var base_rose: Array = BulletFactory2D.helper_generate_transforms_rose(24, marker, 6, 150.0, 1.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var layered_rose: Array = BulletFactory2D.helper_generate_transforms_rose(24, marker, 6, 150.0, 1.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(_ring_gaps_ok(layered_rose, 24, 3, 0, 0, 1.3), "rose rings even")
	var ok := true
	for t in star:
		if not (t as Transform2D).is_finite():
			ok = false
	_check(ok, "star layered transforms finite")
	var heart: Array = BulletFactory2D.helper_generate_transforms_heart(40, marker, 150.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(heart.size() == 40, "heart emits in LAYERS (was missing)")
	# Heart bottom is a true cusp (param speed vanishes at t=PI), so an
	# odd-count ring must straddle it: even-count rings read uniform, odd
	# rings carry exactly one short chord across the tip itself.
	_check(_heart_rings_cusp_ok(heart, marker.origin, 40), "heart cusp rings ok")

func _test_translation_and_collapse() -> void:
	# Global-space builders must compose back onto the marker: a ring around
	# a moved marker stays around the marker, not the world origin.
	var marker := Transform2D(0.0, Vector2(400, 300))
	var ring: Array = BulletFactory2D.helper_generate_transforms_ring(4, marker, 100.0, 0.0, TAU, true, false, true, 1.0, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check((ring[0] as Transform2D).origin.distance_to(marker.origin + Vector2(100, 0)) < 1.0, "ring sits at the marker")
	# Collapsed inward stacks are rejected loudly instead of spawning.
	var flat: Array = BulletFactory2D.helper_generate_transforms_circle(12, marker, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 64, 8.0, 1, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(flat.is_empty(), "collapsed inward volley rejected")

func _test_outer_first_volley() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	# Outer first: the first bullets ride the outermost ring.
	var layered: Array = BulletFactory2D.helper_generate_transforms_circle(12, marker, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 2, 0, 0, PackedFloat32Array(), 0, 0)
	_check(layered.size() == 12, "outer first emits full count")
	var worst := 0.0
	for i in layered.size():
		var p: Vector2 = (layered[i] as Transform2D).origin
		var layer: int = _layer_of(i, 12, 3, 2, 0)
		worst = maxf(worst, absf(p.distance_to(marker.origin) - 150.0 * (1.0 + STEP * layer)))
	_check(worst < 1.0, "outer first fills outside-in (worst %.3f px)" % worst)
	_check(absf((layered[0] as Transform2D).origin.distance_to(marker.origin) - 150.0 * 1.5) < 1.0, "outer first bullet rides outermost")

func _test_pingpong_volley() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	var layered: Array = BulletFactory2D.helper_generate_transforms_circle(12, marker, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 3, 0, 0, PackedFloat32Array(), 0, 0)
	_check(layered.size() == 12, "pingpong emits full count")
	var worst := 0.0
	for i in layered.size():
		var p: Vector2 = (layered[i] as Transform2D).origin
		var layer: int = _layer_of(i, 12, 3, 3, 0)
		worst = maxf(worst, absf(p.distance_to(marker.origin) - 150.0 * (1.0 + STEP * layer)))
	_check(worst < 1.0, "pingpong waves layers (worst %.3f px)" % worst)

func _test_twist() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	var plain: Array = BulletFactory2D.helper_generate_transforms_rectangle(8, marker, Vector2(300, 200), true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 2, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var twisted: Array = BulletFactory2D.helper_generate_transforms_rectangle(8, marker, Vector2(300, 200), true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 2, STEP, 0, 0, 0, 0, PackedFloat32Array(), 3, 0)
	_check(twisted.size() == plain.size(), "twist keeps counts")
	# Layer 0 never twists: even bullets coincide exactly.
	var base_same := true
	for i in [0, 2, 4, 6]:
		if ((plain[i] as Transform2D).origin.distance_to((twisted[i] as Transform2D).origin) > 0.01) or absf(wrapf((twisted[i] as Transform2D).get_rotation() - (plain[i] as Transform2D).get_rotation(), -PI, PI)) > 0.001:
			base_same = false
	_check(base_same, "twist leaves layer 0 exact")
	# Layered bullets rotate 3 slots forward along the loop.
	var moved := false
	for i in [1, 3, 5, 7]:
		if ((plain[i] as Transform2D).origin.distance_to((twisted[i] as Transform2D).origin) > 1.0):
			moved = true
	_check(moved, "twist staggers outer rings")

func _test_max_dots() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	# 12 bullets, 3 layers interleaved (4 per layer); cap outer rings at 2.
	var capped: Array = BulletFactory2D.helper_generate_transforms_circle(12, marker, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 2)
	_check(capped.size() == 8, "cap drops overflow (4 + 2 + 2)")
	var on_base := 0
	var on_outer := 0
	for t in capped:
		var r: float = (t as Transform2D).origin.distance_to(marker.origin)
		if absf(r - 150.0) < 1.0:
			on_base += 1
		elif absf(r - 150.0 * 1.25) < 1.0 or absf(r - 150.0 * 1.5) < 1.0:
			on_outer += 1
	_check(on_base == 4 and on_outer == 4, "cap keeps base whole, trims rings")
	# Deterministic: same call twice, identical volley.
	var again: Array = BulletFactory2D.helper_generate_transforms_circle(12, marker, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 2)
	var same := capped.size() == again.size()
	for i in capped.size():
		if ((capped[i] as Transform2D).origin.distance_to((again[i] as Transform2D).origin) > 0.001):
			same = false
	_check(same, "cap deal deterministic")

func _test_custom_scales_and_curve() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	# Explicit rhythm: tight outer pair.
	var custom: Array = BulletFactory2D.helper_generate_transforms_circle(12, marker, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array([1.0, 1.5, 1.6]), 0)
	_check(custom.size() == 12, "custom scales emit full count")
	var worst := 0.0
	for i in custom.size():
		var p: Vector2 = (custom[i] as Transform2D).origin
		var layer: int = _layer_of(i, 12, 3, 0, 0)
		var expect: float = 150.0 * [1.0, 1.5, 1.6][layer]
		worst = maxf(worst, absf(p.distance_to(marker.origin) - expect))
	_check(worst < 1.0, "custom scales place rings (worst %.3f px)" % worst)
	# Exponential compounding.
	var expo: Array = BulletFactory2D.helper_generate_transforms_circle(9, marker, 100.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, 0.5, 0, 0, 0, 1, PackedFloat32Array(), 0, 0)
	worst = 0.0
	for i in expo.size():
		var p: Vector2 = (expo[i] as Transform2D).origin
		var layer: int = _layer_of(i, 9, 3, 0, 0)
		worst = maxf(worst, absf(p.distance_to(marker.origin) - 100.0 * pow(1.5, layer)))
	_check(worst < 1.0, "exponential compounds (worst %.3f px)" % worst)

func _test_anchored_corners() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	# Every corner carries a bullet at any count: 8 bullets on a 300x200 box.
	var rect: Array = BulletFactory2D.helper_generate_transforms_rectangle(8, marker, Vector2(300, 200), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var corners := [Vector2(-150, -100), Vector2(150, -100), Vector2(150, 100), Vector2(-150, 100)]
	for c in corners:
		var found := false
		for t in rect:
			if ((t as Transform2D).origin - marker.origin).distance_to(c) < 1.0:
				found = true
		_check(found, "rect corner occupied at " + str(c))
	# Fewer bullets than corners: evenly spaced corners (2 on a triangle).
	var tri: Array = BulletFactory2D.helper_generate_transforms_triangle(2, marker, 0, 200.0, 200.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(tri.size() == 2, "tiny triangle emits pair")

# Distance from a marker-relative point to a rectangle border (0 = on border).
func _rect_border_dist(p: Vector2, size: Vector2) -> float:
	var hx: float = size.x * 0.5
	var hy: float = size.y * 0.5
	var dx: float = absf(p.x) - hx
	var dy: float = absf(p.y) - hy
	if dx <= 0.0 and dy <= 0.0:
		return minf(hx - absf(p.x), hy - absf(p.y))
	if dx <= 0.0:
		return dy
	if dy <= 0.0:
		return dx
	return Vector2(dx, dy).length()

# Even-per-layer invariant for rectangles: every bullet rides its own ring's
# scaled border, every ring carries corners, facings stay edge-aligned.
func _check_rect_layers_even(layered: Array, marker: Transform2D, size: Vector2, count: int, label: String) -> void:
	var n: int = layered.size()
	var worst := 0.0
	for i in n:
		var layer: int = _layer_of(i, n, count, 0, 0)
		var s: float = BulletFactory2D.helper_layer_scale_factor(layer, STEP, 0)
		var rel: Vector2 = ((layered[i] as Transform2D).origin - marker.origin) / s
		worst = maxf(worst, _rect_border_dist(rel, size))
	_check(worst < 1.0, label + " every bullet on its own ring (worst %.3f px)" % worst)
	# Corner coverage per ring: ring L holds m bullets; all min(m,4) corners read.
	for L in count:
		var members: Array = []
		for i in n:
			if _layer_of(i, n, count, 0, 0) == L:
				members.append(i)
		var m: int = members.size()
		if m <= 0:
			continue
		var s: float = BulletFactory2D.helper_layer_scale_factor(L, STEP, 0)
		var hw: Vector2 = size * 0.5 * s
		var ring_corners := [Vector2(-hw.x, -hw.y), Vector2(hw.x, -hw.y), Vector2(hw.x, hw.y), Vector2(-hw.x, hw.y)]
		var hit := 0
		for c in ring_corners:
			for mi in members:
				if (((layered[mi] as Transform2D).origin - marker.origin).distance_to(c) < 1.0):
					hit += 1
					break
		_check(hit == mini(m, 4), label + " ring %d corners %d/%d" % [L, hit, mini(m, 4)])
	# Facings stay edge-aligned on axis-aligned boxes.
	var skewed := false
	for i in n:
		var r: float = absf(wrapf((layered[i] as Transform2D).get_rotation(), -PI, PI))
		var best := 10.0
		for k in [0.0, PI * 0.5, PI]:
			best = minf(best, absf(r - k))
		if best > 0.02:
			skewed = true
	_check(not skewed, label + " facings edge-aligned")

# Every ring of a layered polygon volley carries corner bullets.
func _layer_rings_have_corners(layered: Array, n: int, count: int, fill: int, start: int) -> bool:
	for L in count:
		var members: Array = []
		for i in n:
			if _layer_of(i, n, count, fill, start) == L:
				members.append(i)
		if members.is_empty():
			continue
		# Corner bullets face along edges; interior bullets share the same
		# edge facing. A ring without any corner would read as a rotated
		# ghost: require at least one bullet whose facing matches bullet 0's
		# ring-mates... simplified: every ring must hold a bullet within 2px
		# of another ring's bullet direction (shared winding). Concretely we
		# check the ring is non-degenerate (spans > 10px in both axes for the
		# hexagon fixture).
		var mn := Vector2(1e9, 1e9)
		var mx := Vector2(-1e9, -1e9)
		for mi in members:
			var p: Vector2 = (layered[mi] as Transform2D).origin
			mn.x = minf(mn.x, p.x)
			mn.y = minf(mn.y, p.y)
			mx.x = maxf(mx.x, p.x)
			mx.y = maxf(mx.y, p.y)
		if (mx - mn).length() < 10.0:
			return false
	return true

func _edge_counts_rect(volley: Array, marker: Transform2D, size: Vector2) -> Array:
	# Counts per edge (top, right, bottom, left) including each edge's start
	# corner, so opposite sides can be compared for symmetry.
	var top := 0
	var right := 0
	var bottom := 0
	var left := 0
	var hx: float = size.x * 0.5
	var hy: float = size.y * 0.5
	for t in volley:
		var p: Vector2 = (t as Transform2D).origin - marker.origin
		var on_top: bool = absf(p.y + hy) < 1.0 and p.x >= -hx - 1.0 and p.x <= hx + 1.0
		var on_bottom: bool = absf(p.y - hy) < 1.0 and p.x >= -hx - 1.0 and p.x <= hx + 1.0
		var on_right: bool = absf(p.x - hx) < 1.0 and p.y > -hy + 1.0 and p.y <= hy + 1.0
		var on_left: bool = absf(p.x + hx) < 1.0 and p.y > -hy + 1.0 and p.y <= hy + 1.0
		if on_top:
			top += 1
		elif on_bottom:
			bottom += 1
		elif on_right:
			right += 1
		elif on_left:
			left += 1
	return [top, right, bottom, left]

func _test_symmetric_distribution() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	# Square, 6 bullets: remainder 2 must pair opposite sides (3,3 balanced
	# pairs), not pile on adjacent edges like legacy largest-remainder.
	var sym: Array = BulletFactory2D.helper_generate_transforms_rectangle(6, marker, Vector2(200, 200), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1, 0)
	var c: Array = _edge_counts_rect(sym, marker, Vector2(200, 200))
	_check(c[0] == c[2] and c[1] == c[3], "square symmetric opposite sides equal %s" % str(c))
	var leg: Array = BulletFactory2D.helper_generate_transforms_rectangle(6, marker, Vector2(200, 200), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 0)
	var cl: Array = _edge_counts_rect(leg, marker, Vector2(200, 200))
	_check(cl[0] + cl[1] + cl[2] + cl[3] == 6, "square legacy still emits 6 %s" % str(cl))

func _test_small_corner_seats() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	# 2 bullets on a square = opposite corners (diagonal apart).
	var sq2: Array = BulletFactory2D.helper_generate_transforms_rectangle(2, marker, Vector2(200, 200), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(sq2.size() == 2, "square pair emits")
	var d: float = (sq2[0] as Transform2D).origin.distance_to((sq2[1] as Transform2D).origin)
	_check(_approx(d, 200.0 * sqrt(2.0), 1.0), "square pair lands opposite (diag %.1f)" % d)
	# 5 bullets on a 5-point star = the 5 outer tips (no valley stacking).
	var star5: Array = BulletFactory2D.helper_generate_transforms_star(5, marker, 5, 150.0, 65.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var tips := 0
	for t in star5:
		if absf((t as Transform2D).origin.distance_to(marker.origin) - 150.0) < 1.0:
			tips += 1
	_check(tips == 5, "star 5 seats all outer tips (%d/5)" % tips)

func _test_star_edge_walk() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	# 20 bullets on a 10-corner star: edge walk gives 20 distinct origins;
	# the old vertex-repeat implementation stacked 2 per vertex (10 distinct).
	var star: Array = BulletFactory2D.helper_generate_transforms_star(20, marker, 5, 150.0, 65.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var pts: Array = []
	for t in star:
		pts.append((t as Transform2D).origin)
	var distinct := 0
	for i in pts.size():
		var dup := false
		for j in i:
			if pts[i].distance_to(pts[j]) < 0.01:
				dup = true
				break
		if not dup:
			distinct += 1
	_check(distinct == 20, "star edge walk has no stacks (%d/20 distinct)" % distinct)

func _test_ellipse_full_no_seam() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	# FULL ellipse closes the loop without duplicating the seam bullet.
	var ell: Array = BulletFactory2D.helper_generate_transforms_ellipse(12, marker, 150.0, 100.0, 0.0, 0.0, TAU, 0, 0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var stacked := false
	for i in ell.size():
		for j in range(i + 1, ell.size()):
			if ((ell[i] as Transform2D).origin.distance_to((ell[j] as Transform2D).origin) < 0.01):
				stacked = true
	_check(not stacked, "ellipse FULL has no seam stack")

func _test_layer_layout_modes() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	# Shared loop (legacy): exact scaled copies of the base loop.
	var base: Array = BulletFactory2D.helper_generate_transforms_rectangle(8, marker, Vector2(300, 200), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var shared: Array = BulletFactory2D.helper_generate_transforms_rectangle(8, marker, Vector2(300, 200), true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1, 0)
	_check_pair(base, shared, marker.origin, 3, STEP, 0, "rectangle-shared")
	# Even per layer (default): every ring carries corners.
	var even: Array = BulletFactory2D.helper_generate_transforms_rectangle(8, marker, Vector2(300, 200), true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1, 1)
	_check_rect_layers_even(even, marker, Vector2(300, 200), 3, "rectangle-even")

func _tr_facing(shape: int, count: int, params: Dictionary) -> float:
	# Facing of the top-right corner dot (corner index 1 on rectangles).
	var rep: Dictionary = BulletFactory2D.debug_describe_outline(shape, count, params)
	_check(bool(rep.get("ok", false)), "describe ok for TR facing")
	var ci: PackedInt32Array = rep["corner_index"]
	var fa: PackedFloat32Array = rep["facings"]
	for i in ci.size():
		if ci[i] == 1:
			return fa[i]
	return NAN

func _test_corner_priority() -> void:
	# Image 3/4 repro: top-right corner dot ownership is user-chosen now.
	var base_params := {"size": Vector2(300, 200)}
	var f_h: float = _tr_facing(BulletFactory2D.DEBUG_SHAPE_RECTANGLE, 8, base_params)
	_check(_approx(f_h, -PI * 0.5, 0.02), "rect TR faces UP by default (horizontal, %.3f)" % f_h)
	var f_v: float = _tr_facing(BulletFactory2D.DEBUG_SHAPE_RECTANGLE, 8, {"size": Vector2(300, 200), "outline_corner_priority": 1})
	_check(_approx(f_v, 0.0, 0.02), "rect TR faces +X under vertical (%.3f)" % f_v)
	var f_b: float = _tr_facing(BulletFactory2D.DEBUG_SHAPE_RECTANGLE, 8, {"size": Vector2(300, 200), "outline_corner_priority": 2})
	_check(_approx(f_b, 0.0, 0.02), "rect TR faces +X under balanced/outgoing (%.3f)" % f_b)
	# Same verdict straight from a real volley (not just describe).
	var marker := Transform2D(0.0, Vector2(400, 300))
	var volley: Array = BulletFactory2D.helper_generate_transforms_rectangle(8, marker, Vector2(300, 200), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1, 1, 0, 0, 0.0)
	var found_up := false
	for t in volley:
		var p: Vector2 = (t as Transform2D).origin
		if p.distance_to(marker.origin + Vector2(150, -100)) < 1.0:
			found_up = _approx((t as Transform2D).get_rotation(), -PI * 0.5, 0.02)
	_check(found_up, "real volley TR corner faces UP by default")
	# Settings echo matches what was asked for.
	var rep: Dictionary = BulletFactory2D.debug_describe_outline(BulletFactory2D.DEBUG_SHAPE_RECTANGLE, 8, {"size": Vector2(300, 200), "outline_corner_priority": 1})
	var echo: Dictionary = rep["settings"]
	_check(int(echo.get("outline_corner_priority", -1)) == 1, "settings digest echoes priority")

func _test_corner_mode_margin() -> void:
	# EVEN_ARC: uniform gaps, no pinning distortion on a dense rectangle.
	var dense: Dictionary = BulletFactory2D.debug_describe_outline(BulletFactory2D.DEBUG_SHAPE_RECTANGLE, 40, {"size": Vector2(300, 200), "outline_corner_mode": 1})
	_check(bool(dense.get("ok", false)), "dense even-arc describes")
	var gaps: PackedFloat32Array = dense["gaps"]
	var mn := 1e9
	var mx := 0.0
	for g in gaps:
		mn = minf(mn, g)
		mx = maxf(mx, g)
	_check(mx / mn < 1.05, "even-arc gaps uniform (ratio %.3f)" % (mx / mn))
	# PIN (default) keeps every corner occupied.
	var pinned: Dictionary = BulletFactory2D.debug_describe_outline(BulletFactory2D.DEBUG_SHAPE_RECTANGLE, 40, {"size": Vector2(300, 200)})
	var flags: PackedInt32Array = pinned["corner_flags"]
	var corner_dots := 0
	for f in flags:
		corner_dots += f
	_check(corner_dots >= 4, "pin mode keeps all 4 corners (%d)" % corner_dots)
	# Edge margin: interiors keep clearance from corners along their edge.
	var margined: Dictionary = BulletFactory2D.debug_describe_outline(BulletFactory2D.DEBUG_SHAPE_RECTANGLE, 8, {"size": Vector2(300, 200), "outline_edge_margin": 20.0})
	var pts: PackedVector2Array = margined["points"]
	var ci: PackedInt32Array = margined["corner_index"]
	var ok := true
	for i in pts.size():
		if ci[i] >= 0:
			continue
		# Interior dot: nearest corner along the loop must be >= margin away.
		var best := 1e9
		for k in pts.size():
			if ci[k] >= 0:
				best = minf(best, pts[i].distance_to(pts[k]))
		if best < 20.0 - 1.0:
			ok = false
	_check(ok, "edge margin keeps 20px corner clearance")

func _test_ellipse_arc_even() -> void:
	# rx=150/ry=100 at 55 slots: angle-even bunches ~1.35x at the ends,
	# arc-even must read ~1.0.
	var rep: Dictionary = BulletFactory2D.debug_describe_outline(BulletFactory2D.DEBUG_SHAPE_ELLIPSE, 55, {"radius_x": 150.0, "radius_y": 100.0})
	_check(bool(rep.get("ok", false)), "ellipse describes")
	var gaps: PackedFloat32Array = rep["gaps"]
	var mn := 1e9
	var mx := 0.0
	for g in gaps:
		mn = minf(mn, g)
		mx = maxf(mx, g)
	_check(mx / mn < 1.15, "ellipse arc-even gaps uniform (ratio %.3f)" % (mx / mn))
	# Radial facings stay exact after resampling.
	_check(float(rep.get("worst_facing_deviation", 9.0)) < 0.02, "ellipse facings radial (worst %.4f)" % float(rep.get("worst_facing_deviation", 9.0)))

func _test_circle_screenshot_settings() -> void:
	# Exact inspector settings from the circle bug report: 55 bullets,
	# Layers x3, scale 0.5, outward, interleaved, symmetric, even-per-layer.
	var marker := Transform2D(0.0, Vector2(400, 300))
	var volley: Array = BulletFactory2D.helper_generate_transforms_circle(55, marker, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, 0.5, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(volley.size() == 55, "circle screenshot volley emits 55")
	# Per-ring uniformity: split by dealt layer, measure each ring's gaps.
	for L in 3:
		var ring: Array = []
		for i in volley.size():
			if BulletFactory2D.helper_bullet_layer_index(i, 55, 3, 0, 0) == L:
				ring.append(volley[i])
		var stats: Dictionary = BulletFactory2D.debug_volley_gaps(ring)
		_check(float(stats.get("gap_ratio", 99.0)) < 1.05, "circle ring %d even (ratio %.3f, n=%d)" % [L, float(stats.get("gap_ratio", 99.0)), ring.size()])
	# Mathematical conformance of a single-ring volley against describe.
	var plain: Array = BulletFactory2D.helper_generate_transforms_circle(55, marker, 150.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var ok: Dictionary = BulletFactory2D.debug_verify_volley(plain, BulletFactory2D.DEBUG_SHAPE_CIRCLE, marker, 55, {})
	_check(bool(ok.get("ok", false)), "circle volley verifies (worst %.3f px)" % float(ok.get("worst_pos_px", -1.0)))

func _heart_rings_cusp_ok(volley: Array, marker: Vector2, n: int) -> bool:
	# Even-count rings: uniform gaps. Odd-count rings: exactly one short
	# chord, and it must sit on the cusp tip (lowest point of the heart).
	for L in 3:
		var ring: Array = []
		for i in n:
			if _layer_of(i, n, 3, 0, 0) == L:
				ring.append((volley[i] as Transform2D).origin)
		if ring.size() < 2:
			continue
		var gaps: Array = []
		for k in ring.size():
			gaps.append((ring[k] as Vector2).distance_to(ring[(k + 1) % ring.size()]))
		var mean := 0.0
		for g in gaps:
			mean += g
		mean /= gaps.size()
		if ring.size() % 2 == 0:
			for g in gaps:
				if g < mean * 0.8 or g > mean * 1.2:
					return false
		else:
			var short := 0
			var short_k := -1
			for k in gaps.size():
				if gaps[k] < mean * 0.6:
					short += 1
					short_k = k
				elif gaps[k] < mean * 0.8 or gaps[k] > mean * 1.2:
					return false
			if short != 1:
				return false
			var mid: Vector2 = ((ring[short_k] as Vector2) + (ring[(short_k + 1) % ring.size()] as Vector2)) * 0.5
			for pt in ring:
				if (pt as Vector2).y > mid.y + 5.0:
					return false
	return true

func _test_debug_verify() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	# Rectangle under every priority: real volley matches describe exactly.
	for pri in [0, 1, 2]:
		var volley: Array = BulletFactory2D.helper_generate_transforms_rectangle(12, marker, Vector2(300, 200), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1, 1, pri, 0, 0.0)
		var ok: Dictionary = BulletFactory2D.debug_verify_volley(volley, BulletFactory2D.DEBUG_SHAPE_RECTANGLE, marker, 12, {"size": Vector2(300, 200), "outline_corner_priority": pri})
		_check(bool(ok.get("ok", false)), "rect verify priority %d (worst %.3f px, %.4f rad)" % [pri, float(ok.get("worst_pos_px", -1.0)), float(ok.get("worst_face_rad", -1.0))])
	# Star + triangle conformance under defaults.
	var star: Array = BulletFactory2D.helper_generate_transforms_star(20, marker, 5, 150.0, 65.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var sok: Dictionary = BulletFactory2D.debug_verify_volley(star, BulletFactory2D.DEBUG_SHAPE_STAR, marker, 20, {"points": 5, "outer_radius": 150.0, "inner_radius": 65.0})
	_check(bool(sok.get("ok", false)), "star verify ok")
	var tri: Array = BulletFactory2D.helper_generate_transforms_triangle(9, marker, 0, 200.0, 200.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var tok: Dictionary = BulletFactory2D.debug_verify_volley(tri, BulletFactory2D.DEBUG_SHAPE_TRIANGLE, marker, 9, {"triangle_type": 0, "size_a": 200.0, "size_b": 200.0})
	_check(bool(tok.get("ok", false)), "triangle verify ok")
	# Unknown shape + bad count fail loudly, not silently.
	var bad: Dictionary = BulletFactory2D.debug_describe_outline(99, 8, {})
	_check(not bool(bad.get("ok", true)), "unknown shape describes as not-ok")
