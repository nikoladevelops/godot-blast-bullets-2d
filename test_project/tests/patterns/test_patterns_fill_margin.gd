extends BlastTest
## Fill Inside with a fill_margin the shape cannot honor must fail fast.
## The old search halved the grid spacing down to extent * 1e-5, scanning
## up to 40M cells per step, before reporting "no room": a circle with
## margin 150 froze for ~23 s (the editor preview runs the same code, so
## dragging the slider across the range hung the editor). Contract: when the
## margin exceeds the largest clearance any interior point has, the answer
## is the same "no room" error, immediately; margins that fit are untouched.

const FILL_INSIDE := 2 # BulletPatterns2D.OUTLINE_FILL_INSIDE
const CIRCLE := 25
const ROSE := 20
const FLOWER := 8
## Generous wall-time bound: the bug took 10-23 s, the fix takes milliseconds.
const FAST_MS := 1500
const NO_ROOM := "fill inside found no room for bullets (fill_margin too large for the shape?)."


func _fill(shape: int, amount: int, margin: float) -> Array:
	return BulletPatterns2D.generate(shape, amount, Transform2D(), {"outline_placement": FILL_INSIDE, "outline_fill_margin": margin})


func _timed_fill(shape: int, amount: int, margin: float) -> Dictionary:
	var t0 := Time.get_ticks_msec()
	var v := _fill(shape, amount, margin)
	return {"n": v.size(), "ms": Time.get_ticks_msec() - t0}


func test_margin_larger_than_the_shape_fails_fast_with_the_same_error() -> void:
	# Circle radius 150: no point is 200 px from the edge.
	var r := _timed_fill(CIRCLE, 64, 200.0)
	expect_error_sequence(["helper_generate_transforms_circle: " + NO_ROOM])
	assert_eq(r["n"], 0, "no room: empty result")
	assert_lt(r["ms"], FAST_MS, "answered in %d ms (old search: ~11 s)" % r["ms"])


func test_margin_equal_to_the_radius_fails_fast() -> void:
	# The nastiest old case: the margin equals the inradius, so only the
	# exact center could qualify (the outline is a polygon: it never does).
	var r := _timed_fill(CIRCLE, 64, 150.0)
	expect_error_sequence(["helper_generate_transforms_circle: " + NO_ROOM])
	assert_eq(r["n"], 0, "empty result")
	assert_lt(r["ms"], FAST_MS, "answered in %d ms (old search: ~23 s)" % r["ms"])


func test_thin_shapes_fail_fast_when_the_margin_eats_them() -> void:
	for case in [[ROSE, 50.0, "rose"], [FLOWER, 140.0, "flower"]]:
		var r := _timed_fill(case[0], 64, case[1])
		expect_error_sequence(["helper_generate_transforms_%s: %s" % [case[2], NO_ROOM]], case[2])
		assert_eq(r["n"], 0, "%s: empty result" % case[2])
		assert_lt(r["ms"], FAST_MS, "%s answered in %d ms (old search: 4-13 s)" % [case[2], r["ms"]])


func test_a_margin_that_leaves_room_still_fills_exactly_n() -> void:
	# 140 leaves a disc of radius ~10 inside the circle: the search is still
	# needed and must still deliver every bullet, inside the allowed band.
	var v := _fill(CIRCLE, 64, 140.0)
	assert_eq(v.size(), 64, "all 64 bullets placed")
	for t in v:
		assert_lt((t as Transform2D).origin.length(), 10.5, "inside the shrunken disc")


func test_zero_and_small_margins_are_unchanged() -> void:
	for margin in [0.0, 1.0, 50.0, 100.0]:
		assert_eq(_fill(CIRCLE, 64, margin).size(), 64, "margin %s fills 64" % margin)


# --- fill_min_spacing: the finest grid the search may use ------------------

func _min_pair_distance(v: Array) -> float:
	var best := INF
	for i in v.size():
		for j in range(i + 1, v.size()):
			best = minf(best, ((v[i] as Transform2D).origin).distance_to((v[j] as Transform2D).origin))
	return best


func test_min_spacing_knob_exists_with_the_documented_default() -> void:
	var sp := make_spawner(null, BulletSpawner2D.PATTERN_FROM_HELPER_CIRCLE, 8)
	assert_true("helper_outline_fill_min_spacing" in sp, "property exists")
	assert_almost_eq(sp.get("helper_outline_fill_min_spacing"), 0.5, 0.0001, "default 0.5 px (the no-two-bullets-closer-than-0.5 invariant)")
	assert_true(BulletPatterns2D.helper_generate_transforms_circle(8, Transform2D()).size() == 8, "plain generator call unaffected")


func test_min_spacing_rejects_bad_values_and_keeps_the_old_one() -> void:
	var sp := make_spawner(null, BulletSpawner2D.PATTERN_FROM_HELPER_CIRCLE, 8)
	for bad in [0.0, -1.0, NAN, INF]:
		sp.set("helper_outline_fill_min_spacing", bad)
		expect_error_sequence(["BulletSpawner2D: helper_outline_fill_min_spacing must be finite and > 0, keeping the old value."], str(bad))
		assert_almost_eq(sp.get("helper_outline_fill_min_spacing"), 0.5, 0.0001, "kept after %s" % str(bad))


func test_sliver_region_answers_fast_instead_of_searching_for_minutes() -> void:
	# Margin 149.9 on a radius-150 circle leaves a disc ~0.1 px wide: the old
	# search refined to ~0.003 px (23 s for 64 bullets, 104 s for 3).
	for amount in [3, 64]:
		var t0 := Time.get_ticks_msec()
		var v := _fill(CIRCLE, amount, 149.9)
		var ms := Time.get_ticks_msec() - t0
		expect_no_errors("a sliver is not an error: the grid holds one cell")
		assert_lt(ms, FAST_MS, "%d bullets answered in %d ms" % [amount, ms])
		assert_eq(v.size(), 1, "%d asked: the one cell that fits at the 0.5 px floor" % amount)


func _small_disc_fill(amount: int, extra: Dictionary = {}) -> Array:
	# Radius-20 circle, margin 14: a usable disc of radius ~6 (area ~113),
	# which holds ~450 bullets 0.5 px apart.
	var params := {"outline_placement": FILL_INSIDE, "circle_radius": 20.0, "outline_fill_margin": 14.0}
	params.merge(extra, true)
	return BulletPatterns2D.generate(CIRCLE, amount, Transform2D(), params)


func test_tiny_usable_region_is_bounded_by_the_min_spacing() -> void:
	# 500 bullets would need ~0.47 px between them: below the 0.5 px floor, so
	# the search stops at 0.5 and returns what fits, never stacked.
	var v := _small_disc_fill(500)
	assert_gt(v.size(), 300, "a real fill, not the no-room error")
	assert_lt(v.size(), 500, "fewer than asked: the floor won")
	assert_gt(_min_pair_distance(v), 0.49, "no two bullets closer than the floor")


func test_lowering_min_spacing_restores_the_full_count() -> void:
	var v := _small_disc_fill(500, {"outline_fill_min_spacing": 0.2})
	assert_eq(v.size(), 500, "all 500 fit once the floor allows 0.2 px")
	assert_gt(_min_pair_distance(v), 0.19, "and respect the lowered floor")


func test_min_spacing_never_coarsens_a_finer_requested_spacing() -> void:
	# fill_spacing 0.3 is already below the 0.5 floor: the developer asked for
	# it explicitly, so the floor only stops the AUTOMATIC refinement.
	var v: Array = BulletPatterns2D.generate(CIRCLE, 20, Transform2D(), {"outline_placement": FILL_INSIDE, "outline_fill_spacing": 0.3, "circle_radius": 5.0})
	assert_eq(v.size(), 20, "the requested spacing is honored")
