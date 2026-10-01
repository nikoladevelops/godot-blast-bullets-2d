extends SceneTree
## Singular-marker suite: generators that invert the marker must reject
## singular markers loudly instead of emitting garbage.
##
## The gap: validators checked finite-only. A (0, 1)-scale marker passes
## every finite check but its determinant is 0, so affine_inverse() (used by
## the outline layout and the Path2D linker) is garbage. Non-inverting
## generators (grid/fan/spiral/line) degrade gracefully to zero-size patterns
## and keep working — the split contract is pinned here too.
##
## Covers: T1 ring rejects a singular marker, T2 danmaku-head generators
## reject it (circle), T3 grid with a singular marker stays finite
## (no-inverse path degrades), T4 no dangling.
##
## Run: godot --headless --path test_project --script tests/factory/test_factory_singular_marker.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _finite_volley(volley: Array) -> bool:
	for t in volley:
		if not (t as Transform2D).is_finite():
			return false
	return true

func _initialize() -> void:
	var singular := Transform2D(Vector2(0, 0), Vector2(0, 1), Vector2(100, 50))
	_check(singular.is_finite(), "setup singular marker is finite (det=0, passes old guards)")

	# ---------------------------------------------------------------
	printerr("MARKER T1 ring rejects a singular marker")
	var ring = BulletFactory2D.helper_generate_transforms_ring(12, singular, 60.0, 0.0, TAU, true, false, true)
	_check(ring.is_empty(), "T1 ring volley empty on singular marker (got %d)" % ring.size())

	# ---------------------------------------------------------------
	printerr("MARKER T2 circle rejects a singular marker")
	var circ = BulletFactory2D.helper_generate_transforms_circle(12, singular, 60.0, true, 0.0)
	_check(circ.is_empty(), "T2 circle volley empty on singular marker (got %d)" % circ.size())

	# ---------------------------------------------------------------
	printerr("MARKER T3 grid with a singular marker stays finite")
	var grid = BulletFactory2D.helper_generate_transforms_grid(6, singular, 3, 4, 32.0, 32.0, true, false, 0.0, 0)
	_check(grid.size() == 6, "T3 grid still emits (got %d)" % grid.size())
	_check(_finite_volley(grid), "T3 grid slots finite (graceful zero-size)")

	# ---------------------------------------------------------------
	printerr("MARKER T4 sane markers unaffected")
	var good := Transform2D(0.3, Vector2(100, 50))
	var ring_ok = BulletFactory2D.helper_generate_transforms_ring(12, good, 60.0, 0.0, TAU, true, false, true)
	_check(ring_ok.size() == 12, "T4 ring emits on a sane marker")
	_check(_finite_volley(ring_ok), "T4 ring slots finite")

	print("----")
	if failures == 0:
		print("ALL SINGULAR-MARKER TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
