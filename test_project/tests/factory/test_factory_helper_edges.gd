extends SceneTree
## Helper hostile-input suite: every helper_generate_transforms_* hammered
## with negative/huge/NaN/Inf/zero inputs plus the new caps (8192 per call,
## grid-product guard, gap/points/vertices caps, scale-factor band). Helpers
## must fail loud with empty arrays — never hang, OOM, or emit NaN/Inf slots.
## Run: godot --headless --path test_project --script tests/factory/test_factory_helper_edges.gd
## Exit code 0 = all pass.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _finite_arr(a: Array) -> bool:
	for t in a:
		var tr: Transform2D = t
		if not tr.is_finite():
			return false
	return true

func _marker() -> Transform2D:
	return Transform2D(0.0, Vector2.ZERO)

func _initialize() -> void:
	printerr("HELPER T1 amount caps (no hang/OOM)")
	_check(BulletFactory2D.helper_generate_transforms_grid(20000, _marker(), 10).is_empty(), "grid 20k rejected")
	_check(BulletFactory2D.helper_generate_transforms_ring(20000, _marker()).is_empty(), "ring 20k rejected")
	_check(BulletFactory2D.helper_generate_transforms_fan(-5, _marker(), 1.0).is_empty(), "fan negative rejected")
	_check(BulletFactory2D.helper_generate_transforms_line(0, _marker(), Vector2.RIGHT, 10.0).is_empty(), "line 0 returns empty")
	_check(BulletFactory2D.helper_generate_transforms_circle(1, _marker()).size() == 1, "circle 1 returns one slot")

	printerr("HELPER T2 grid-product + gap/points/vertices caps")
	_check(BulletFactory2D.helper_generate_transforms_waterfall(10, _marker(), 100000, 8.0, 100000, 8.0).is_empty(), "waterfall 100kx100k rejected")
	_check(BulletFactory2D.helper_generate_transforms_lattice(10, _marker(), 100000, 100000).is_empty(), "lattice 100kx100k rejected")
	_check(BulletFactory2D.helper_generate_transforms_star(10, _marker(), 100000).is_empty(), "star 100k points rejected")
	_check(BulletFactory2D.helper_generate_transforms_polygon(10, _marker(), 100000).is_empty(), "polygon 100k vertices rejected")

	printerr("HELPER T3 NaN/Inf inputs rejected loud")
	var nan_mark := Transform2D(0.0, Vector2(NAN, 0))
	_check(BulletFactory2D.helper_generate_transforms_grid(4, nan_mark, 2).is_empty(), "grid NaN marker rejected")
	_check(BulletFactory2D.helper_generate_transforms_ring(8, _marker(), NAN).is_empty(), "ring NaN radius rejected")
	_check(BulletFactory2D.helper_generate_transforms_fan(4, _marker(), INF, 0.0).is_empty(), "fan Inf spread rejected")
	_check(BulletFactory2D.helper_generate_transforms_line(4, _marker(), Vector2.ZERO, 10.0).is_empty(), "line zero dir rejected")
	_check(BulletFactory2D.helper_generate_transforms_wave(8, _marker(), 100.0, 10.0, 2.0, Vector2(NAN, 0)).is_empty(), "wave NaN dir rejected")

	printerr("HELPER T4 huge-but-finite inputs clamp, never NaN")
	var far: Array = BulletFactory2D.helper_generate_transforms_line(8, _marker(), Vector2.RIGHT, 1e30, true)
	_check(far.size() == 8 and _finite_arr(far), "line 1e30 spacing clamps finite")
	var wide: Array = BulletFactory2D.helper_generate_transforms_grid(9, _marker(), 3, 0, 1e30, 1e30, false)
	_check(wide.size() == 9 and _finite_arr(wide), "grid 1e30 offsets clamp finite")
	var big_ring: Array = BulletFactory2D.helper_generate_transforms_ring(16, _marker(), 1e30)
	_check(big_ring.size() == 16 and _finite_arr(big_ring), "ring 1e30 radius stays finite")

	printerr("HELPER T5 scale-factor band + skip-indices guards")
	_check(not is_finite(BulletFactory2D.helper_layer_scale_factor(100000, 0.5, 0, 1, PackedFloat32Array())), "scale factor stays Inf on absurd layers (collapse precheck sees it)")
	_check(BulletFactory2D.helper_layer_scale_factor(-3, 0.5, 0, 1, PackedFloat32Array()) == 1.0, "negative layer returns 1")
	var base: Array = BulletFactory2D.helper_generate_transforms_line(5, _marker(), Vector2.RIGHT, 10.0, true)
	var skipped: Array = BulletFactory2D.helper_apply_skip_indices(base, [1, 99, -2])
	_check(skipped.size() == 4, "skip ignores OOB indices")
	_check(BulletFactory2D.helper_bullet_layer_index(3, 10, 4, 0, 0) >= 0, "layer index sane")

	printerr("HELPER T6 ellipse gap cap + scatter fallbacks")
	_check(BulletFactory2D.helper_generate_transforms_ellipse(10, _marker(), 60.0, 40.0, 0.0, 0.0, TAU, 2, 100000).is_empty(), "ellipse 100k gaps rejected")

	print("----")
	if failures == 0:
		print("ALL HELPER EDGE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
