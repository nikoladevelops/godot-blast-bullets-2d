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

func _test_circle_layers() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	var base: Array = BulletFactory2D.helper_generate_transforms_circle(12, marker, 150.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var layered: Array = BulletFactory2D.helper_generate_transforms_circle(12, marker, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check_pair(base, layered, marker.origin, 3, STEP, 0, "circle")
	# Scaled figure: outward layers grow the radius by the scale factor.
	var worst := 0.0
	for i in layered.size():
		var p: Vector2 = (layered[i] as Transform2D).origin
		var layer: int = _layer_of(i, layered.size(), 3, 0, 0)
		worst = maxf(worst, absf(p.distance_to(marker.origin) - 150.0 * (1.0 + STEP * layer)))
	_check(worst < 1.0, "circle radii scale uniformly (worst %.3f px)" % worst)

func _test_ring_layers() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	var base: Array = BulletFactory2D.helper_generate_transforms_ring(12, marker, 150.0, 0.0, TAU, true, false, true, 1.0, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var layered: Array = BulletFactory2D.helper_generate_transforms_ring(12, marker, 150.0, 0.0, TAU, true, false, true, 1.0, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check_pair(base, layered, marker.origin, 3, STEP, 0, "ring")
	# Ring volleys live around the marker, never around the world origin.
	_check((layered[0] as Transform2D).origin.distance_to(marker.origin + Vector2(150, 0)) < 1.0, "ring sits at the marker")

func _test_rect_layers() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	var base: Array = BulletFactory2D.helper_generate_transforms_rectangle(8, marker, Vector2(300, 200), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var layered: Array = BulletFactory2D.helper_generate_transforms_rectangle(8, marker, Vector2(300, 200), true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check_pair(base, layered, marker.origin, 3, STEP, 0, "rectangle")
	# Bullet 0 sits on the top-left corner facing straight up the top edge.
	_check(_approx((base[0] as Transform2D).get_rotation(), -PI * 0.5, 0.01), "rect bullet 0 faces straight")
	_check(_approx((layered[0] as Transform2D).get_rotation(), -PI * 0.5, 0.01), "rect layered bullet 0 faces straight")
	# Same shape, bigger: a layered top-edge slot keeps its direction from
	# the center, only farther out.
	var p: Vector2 = (layered[1] as Transform2D).origin - marker.origin
	var q: Vector2 = (base[1] as Transform2D).origin - marker.origin
	_check(_approx((p - q * 1.25).length(), 0.0, 1.0), "rect layer scales about center")

func _test_polygon_layers() -> void:
	var marker := Transform2D(0.0, Vector2(400, 300))
	var base: Array = BulletFactory2D.helper_generate_transforms_polygon(12, marker, 6, 150.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	var layered: Array = BulletFactory2D.helper_generate_transforms_polygon(12, marker, 6, 150.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check_pair(base, layered, marker.origin, 3, STEP, 0, "polygon")
	var sq_base: Array = BulletFactory2D.helper_generate_transforms_rectangle(4, marker, Vector2(200, 200), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(_approx((sq_base[0] as Transform2D).get_rotation(), -PI * 0.5, 0.01), "square bullet 0 faces straight")
	_check(BulletFactory2D.helper_generate_transforms_triangle(9, marker, 0, 200.0, 200.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0).size() == 9, "triangle emits in LAYERS", 0, PackedFloat32Array(), 0, 0)
	_check(BulletFactory2D.helper_generate_transforms_trapezoid(8, marker, 150.0, 280.0, 140.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0).size() == 8, "trapezoid emits in LAYERS", 0, PackedFloat32Array(), 0, 0)
	_check(BulletFactory2D.helper_generate_transforms_diamond(8, marker, 280.0, 200.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0).size() == 8, "diamond emits in LAYERS", 0, PackedFloat32Array(), 0, 0)

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
	_check_pair(base_rose, layered_rose, marker.origin, 3, STEP, 0, "rose")
	var ok := true
	for t in star:
		if not (t as Transform2D).is_finite():
			ok = false
	_check(ok, "star layered transforms finite")
	var heart: Array = BulletFactory2D.helper_generate_transforms_heart(40, marker, 150.0, 0.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(heart.size() == 40, "heart emits in LAYERS (was missing)")
	var base_heart: Array = BulletFactory2D.helper_generate_transforms_heart(40, marker, 150.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check_pair(base_heart, heart, marker.origin, 3, STEP, 0, "heart")

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
	# Fewer bullets than corners: first corners in winding order.
	var tri: Array = BulletFactory2D.helper_generate_transforms_triangle(2, marker, 0, 200.0, 200.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, STEP, 0, 0, 0, 0, PackedFloat32Array(), 0, 0)
	_check(tri.size() == 2, "tiny triangle emits pair")
