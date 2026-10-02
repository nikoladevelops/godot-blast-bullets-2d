extends BlastTest
## The spawner's no-Variant easing (src/shared/easing2d.hpp) must match
## Godot's Tween.interpolate_value for every TransitionType x EaseType, so a
## movement curve picked in the inspector moves exactly like a Tween would.

const SAMPLES := 33


func test_easing_matches_tween(trans: int = use_parameters(range(12))) -> void:
	var worst := 0.0
	var worst_at := ""
	for ease_type in 4:
		for k in SAMPLES:
			var t := float(k) / float(SAMPLES - 1)
			var want: float = Tween.interpolate_value(0.0, 1.0, t, 1.0, trans, ease_type)
			var got: float = BulletSpawner2D.debug_ease(t, trans, ease_type)
			var err := absf(want - got)
			if err > worst:
				worst = err
				worst_at = "ease %d t=%.3f want %.6f got %.6f" % [ease_type, t, want, got]
	assert_lt(worst, 0.0001, "transition %d matches Tween (%s)" % [trans, worst_at])


func test_endpoints_and_clamping() -> void:
	for trans in 12:
		for ease_type in 4:
			assert_almost_eq(BulletSpawner2D.debug_ease(0.0, trans, ease_type), 0.0, 0.002, "t=0 starts at 0 (%d,%d)" % [trans, ease_type])
			assert_almost_eq(BulletSpawner2D.debug_ease(1.0, trans, ease_type), 1.0, 0.002, "t=1 ends at 1 (%d,%d)" % [trans, ease_type])
			assert_almost_eq(BulletSpawner2D.debug_ease(-5.0, trans, ease_type), BulletSpawner2D.debug_ease(0.0, trans, ease_type), 0.000001, "t<0 clamps")
			assert_almost_eq(BulletSpawner2D.debug_ease(9.0, trans, ease_type), BulletSpawner2D.debug_ease(1.0, trans, ease_type), 0.000001, "t>1 clamps")
	assert_almost_eq(BulletSpawner2D.debug_ease(NAN, 0, 0), 0.0, 0.000001, "NaN t treated as 0")
	assert_almost_eq(BulletSpawner2D.debug_ease(0.25, 99, 0), 0.25, 0.000001, "invalid transition falls back to linear")
