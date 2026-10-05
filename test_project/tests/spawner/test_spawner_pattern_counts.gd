extends BlastTest
## Count + visibility contract for every generated pattern:
##   1. collect_spawn_transforms() returns EXACTLY helper_bullets_amount
##      transforms (On Outline, Layers without a cap, Fill Inside);
##   2. no two bullets share a spot - a hidden duplicate reads as a missing
##      bullet (the reported flower bug: FAN wrapped its slots).
## Children/Self/Custom take their count from markers / the array (not the
## amount) and are covered elsewhere. Degenerate user input (radius 0,
## spacing 0, fan step offset 0) is out of scope by design.

const AMOUNTS := [1, 2, 3, 7, 10, 12, 20, 36, 60, 101]
const DISTINCT_AMOUNTS := [7, 10, 12, 20, 36, 60, 101]
## Two bullets closer than this count as the same spot (px).
const MIN_GAP := 0.5

const NOT_AMOUNT_SOURCES := [BulletSpawner2D.PATTERN_FROM_CHILDREN, BulletSpawner2D.PATTERN_FROM_SELF, BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM]
const OUTLINE_SOURCES := [
	BulletSpawner2D.PATTERN_FROM_HELPER_CIRCLE, BulletSpawner2D.PATTERN_FROM_HELPER_SQUARE, BulletSpawner2D.PATTERN_FROM_HELPER_RECTANGLE,
	BulletSpawner2D.PATTERN_FROM_HELPER_TRIANGLE, BulletSpawner2D.PATTERN_FROM_HELPER_DIAMOND, BulletSpawner2D.PATTERN_FROM_HELPER_TRAPEZOID,
	BulletSpawner2D.PATTERN_FROM_HELPER_POLYGON, BulletSpawner2D.PATTERN_FROM_HELPER_ELLIPSE, BulletSpawner2D.PATTERN_FROM_HELPER_RING,
	BulletSpawner2D.PATTERN_FROM_HELPER_STAR, BulletSpawner2D.PATTERN_FROM_HELPER_HEART, BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER,
	BulletSpawner2D.PATTERN_FROM_HELPER_ROSE, BulletSpawner2D.PATTERN_FROM_HELPER_LISSAJOUS,
]


static func generator_sources() -> Array:
	var out: Array = []
	for src in range(BulletSpawner2D.PATTERN_FROM_LAST):
		if not NOT_AMOUNT_SOURCES.has(src):
			out.append(src)
	return out


## Spawner for `src` with whatever outside wiring the source needs (aimed
## target, Path2D) and seeds for the random sources.
func _spawner(src: int) -> BulletSpawner2D:
	var sp := make_spawner(null, src, 10)
	sp.helper_scatter_seed = 7
	sp.helper_rain_seed = 7
	sp.helper_waterfall_seed = 7
	if src == BulletSpawner2D.PATTERN_FROM_HELPER_AIMED:
		var target := Node2D.new()
		target.position = Vector2(400, 0)
		add(target)
		sp.set_helper_aimed_target(target)
		sp.helper_aimed_step_offset = 24.0 # distinct spots (0 stacks by design)
	if src == BulletSpawner2D.PATTERN_FROM_HELPER_FAN:
		sp.helper_fan_step_offset = 24.0 # distinct spots (0 stacks by design)
	if src == BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D:
		sp.set_helper_path2d_node(_path([Vector2(-300, 0), Vector2(0, -200), Vector2(300, 0)]))
	return sp


func _path(points: Array) -> Path2D:
	var path := Path2D.new()
	var curve := Curve2D.new()
	for p in points:
		curve.add_point(p)
	path.curve = curve
	add(path)
	return path


func _origins(sp: BulletSpawner2D) -> PackedVector2Array:
	var pts := PackedVector2Array()
	for t in sp.collect_spawn_transforms():
		pts.append((t as Transform2D).origin)
	return pts


## Closest pair distance (O(n^2), n stays small here).
static func min_gap(pts: PackedVector2Array) -> float:
	var best := INF
	for i in pts.size():
		for j in range(i + 1, pts.size()):
			best = minf(best, pts[i].distance_to(pts[j]))
	return best


func _count_failures(sp: BulletSpawner2D, label: String) -> Array:
	var bad: Array = []
	for n in AMOUNTS:
		sp.helper_bullets_amount = n
		var got: int = sp.collect_spawn_transforms().size()
		if got != n:
			bad.append("%s amount %d -> %d" % [label, n, got])
	return bad


func _distinct_failures(sp: BulletSpawner2D, label: String, amounts: Array = DISTINCT_AMOUNTS) -> Array:
	var bad: Array = []
	for n in amounts:
		sp.helper_bullets_amount = n
		var pts := _origins(sp)
		var gap := min_gap(pts)
		if gap <= MIN_GAP:
			bad.append("%s amount %d: closest pair %.3f px" % [label, n, gap])
	return bad


## Asserts `bad` is empty and prints EVERY entry (GUT clips arrays in its
## own got/expected text, the custom message is shown in full).
func _assert_none(bad: Array, what: String) -> void:
	assert_true(bad.is_empty(), "%s - %d problem(s):\n  %s" % [what, bad.size(), "\n  ".join(bad)])


func test_every_generator_draws_exactly_amount() -> void:
	var bad: Array = []
	for src in generator_sources():
		bad.append_array(_count_failures(_spawner(src), "source %d" % src))
	_assert_none(bad, "count == helper_bullets_amount")


func test_outline_layers_and_fill_draw_exactly_amount() -> void:
	var bad: Array = []
	for src in OUTLINE_SOURCES:
		var sp := _spawner(src)
		sp.helper_outline_placement = BulletPatterns2D.OUTLINE_LAYERS
		sp.helper_outline_layer_count = 3
		bad.append_array(_count_failures(sp, "source %d layers" % src))
		sp.helper_outline_placement = BulletPatterns2D.OUTLINE_FILL_INSIDE
		bad.append_array(_count_failures(sp, "source %d fill" % src))
	_assert_none(bad, "count == helper_bullets_amount for Layers and Fill Inside")


func test_every_generator_places_bullets_on_distinct_spots() -> void:
	var bad: Array = []
	for src in generator_sources():
		bad.append_array(_distinct_failures(_spawner(src), "source %d" % src))
	_assert_none(bad, "no two bullets share a spot")


func test_outline_layers_and_fill_place_distinct_spots() -> void:
	var bad: Array = []
	for src in OUTLINE_SOURCES:
		var sp := _spawner(src)
		sp.helper_outline_placement = BulletPatterns2D.OUTLINE_LAYERS
		sp.helper_outline_layer_count = 3
		bad.append_array(_distinct_failures(sp, "source %d layers" % src))
		sp.helper_outline_placement = BulletPatterns2D.OUTLINE_FILL_INSIDE
		bad.append_array(_distinct_failures(sp, "source %d fill" % src))
	_assert_none(bad, "no shared spots in Layers / Fill Inside")


# --- Flower: the reported bug and its sibling degenerations ---------------

func test_flower_fan_respects_amount_and_fills_every_petal() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER)
	sp.helper_flower_type = BulletPatterns2D.FLOWER_FAN
	sp.helper_flower_petals = 6
	for n in [6, 10, 13, 30, 45, 60, 100]:
		sp.helper_bullets_amount = n
		var pts := _origins(sp)
		assert_eq(pts.size(), n, "FAN draws the amount (%d)" % n)
		assert_gt(min_gap(pts), MIN_GAP, "FAN never stacks slots (%d)" % n)
		# Every petal gets floor(n/6) or ceil(n/6) bullets: bin by angle.
		var bins := [0, 0, 0, 0, 0, 0]
		for p in pts:
			var k := int(round(fposmod(p.angle(), TAU) / (TAU / 6.0))) % 6
			bins[k] += 1
		assert_eq(bins.min(), n / 6, "fullest/emptiest petal differ by at most one (%d: %s)" % [n, bins])
		assert_lte(bins.max(), int(ceil(n / 6.0)), "remainder spread evenly (%d: %s)" % [n, bins])


func test_flower_types_distinct_spots() -> void:
	var bad: Array = []
	for kind in [0, 1, 2, 3, 4]:
		var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER)
		sp.helper_flower_type = kind
		bad.append_array(_distinct_failures(sp, "flower type %d" % kind))
	_assert_none(bad, "every flower type keeps bullets apart")


func test_flower_degenerate_parameters_still_distinct() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER)
	var bad: Array = []
	sp.helper_flower_type = BulletPatterns2D.FLOWER_RHODONEA
	sp.helper_flower_inner_radius_scale = 0.0 # waists touch the centre
	bad.append_array(_distinct_failures(sp, "rhodonea inner 0"))
	sp.helper_flower_type = BulletPatterns2D.FLOWER_SPIROGRAPH
	sp.helper_flower_spiro_pen = 0.0 # plain circle: must not be traced 3x
	bad.append_array(_distinct_failures(sp, "spirograph pen 0"))
	sp.helper_flower_spiro_pen = 80.0
	sp.helper_flower_spiro_roller = 75.0 # roller = R/2 with pen > 0: an ellipse
	bad.append_array(_distinct_failures(sp, "spirograph half roller"))
	_assert_none(bad, "degenerate-looking flower inputs keep every bullet visible")


# --- Other curve generators ------------------------------------------------

func test_rose_odd_and_even_petals_distinct() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_ROSE)
	var bad: Array = []
	for k in [2, 3, 4, 5, 6, 7]:
		sp.helper_rose_petals = k
		bad.append_array(_distinct_failures(sp, "rose %d petals" % k))
	_assert_none(bad, "odd roses are not traced twice; petals do not stack at the centre")


func test_lissajous_ratios_distinct() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_LISSAJOUS)
	var bad: Array = []
	for cfg in [[3.0, 2.0, 0.0], [3.0, 3.0, 0.0], [2.0, 4.0, 0.7], [5.0, 4.0, 0.0], [1.0, 2.0, 1.2]]:
		sp.helper_lissajous_freq_x = cfg[0]
		sp.helper_lissajous_freq_y = cfg[1]
		sp.helper_lissajous_phase = cfg[2]
		bad.append_array(_distinct_failures(sp, "lissajous %s" % str(cfg)))
	_assert_none(bad, "shared-factor ratios are not retraced; the centre crossing is not doubled")


func test_spiral_arm_strides_distinct() -> void:
	var bad: Array = []
	var multi := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_MULTISPIRAL)
	for cfg in [[3, 1], [3, 2], [3, 3], [3, 6], [4, 4], [4, 9]]:
		multi.helper_multispiral_arms = cfg[0]
		multi.helper_multispiral_arm_stride = cfg[1]
		bad.append_array(_distinct_failures(multi, "multispiral arms %d stride %d" % cfg))
	var counter := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_COUNTER_SPIRAL)
	for cfg in [[2, 1], [2, 2], [2, 3], [4, 8]]:
		counter.helper_counter_spiral_arms = cfg[0]
		counter.helper_counter_spiral_arm_stride = cfg[1]
		bad.append_array(_distinct_failures(counter, "counter spiral arms %d stride %d" % cfg))
	_assert_none(bad, "grouped strides never repeat an (arm, step) slot")


func test_full_turn_arcs_have_no_seam_duplicate() -> void:
	var bad: Array = []
	var ell := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_ELLIPSE)
	ell.helper_ellipse_mode = BulletPatterns2D.ELLIPSE_ARC # default arc = TAU
	bad.append_array(_distinct_failures(ell, "ellipse ARC at TAU"))
	var ring := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING)
	ring.helper_ring_arc = 6.28 # typed by hand, a hair short of TAU
	bad.append_array(_distinct_failures(ring, "ring arc 6.28"))
	_assert_none(bad, "a full-turn arc never doubles its seam bullet")


func test_self_closed_path_has_no_seam_duplicate() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D)
	sp.set_helper_path2d_node(_path([Vector2(-200, -200), Vector2(200, -200), Vector2(200, 200), Vector2(-200, 200), Vector2(-200, -200)]))
	sp.helper_path2d_closed = true
	var bad := _count_failures(sp, "closed path")
	bad.append_array(_distinct_failures(sp, "closed path"))
	_assert_none(bad, "a curve that already ends on its start is a clean ring")


func test_cross_overflow_shrinks_spacing() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_CROSS)
	sp.helper_bullets_amount = 60 # 15 per arm, but only 150/32 = 4 fit at 32 px
	var pts := _origins(sp)
	assert_eq(pts.size(), 60, "count kept")
	assert_gt(min_gap(pts), MIN_GAP, "no bullets piled on the tips")
	var far := 0.0
	for p in pts:
		far = maxf(far, p.length())
	assert_almost_eq(far, sp.helper_cross_arm_length, 0.01, "arms still end at arm_length")
	sp.helper_bullets_amount = 8 # fits: spacing honored exactly
	var near := INF
	for p in _origins(sp):
		near = minf(near, p.length())
	assert_almost_eq(near, sp.helper_cross_spacing, 0.01, "spacing kept when everything fits")


func test_fill_inside_overflow_shrinks_spacing() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_CIRCLE)
	sp.helper_outline_placement = BulletPatterns2D.OUTLINE_FILL_INSIDE
	sp.helper_bullets_amount = 300 # ~69 cells fit at 32 px spacing
	var pts := _origins(sp)
	assert_eq(pts.size(), 300, "every bullet gets a cell")
	assert_gt(min_gap(pts), MIN_GAP, "cells stay distinct")
	for p in pts:
		assert_lte(p.length(), sp.helper_circle_radius + 0.01, "still inside the circle")


# --- Gap patterns: exactly amount bullets, none inside a gap ---------------

func test_corridor_places_amount_outside_the_gap() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_CORRIDOR)
	var bad: Array = []
	for n in AMOUNTS:
		if n < 2:
			continue # one bullet cannot cover two walls
		sp.helper_bullets_amount = n
		var pts := _origins(sp)
		if pts.size() != n:
			bad.append("amount %d -> %d" % [n, pts.size()])
		for p in pts:
			# Aim (0, 1): the wall runs along x, the door is |x| < gap / 2.
			if absf(p.x) < sp.helper_corridor_gap_width * 0.5 - 0.01:
				bad.append("amount %d: bullet in the gap at %s" % [n, p])
	_assert_none(bad, "corridor: amount on the walls, the door stays clear")


func test_ellipse_wall_places_amount_outside_the_gaps() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_ELLIPSE)
	sp.helper_ellipse_mode = BulletPatterns2D.ELLIPSE_WALL
	var bad: Array = []
	var span: float = sp.helper_ellipse_arc
	for n in AMOUNTS:
		sp.helper_bullets_amount = n
		var pts := _origins(sp)
		if pts.size() != n:
			bad.append("amount %d -> %d" % [n, pts.size()])
		for p in pts:
			var t := atan2(p.y / sp.helper_ellipse_radius_y, p.x / sp.helper_ellipse_radius_x)
			for g in sp.helper_ellipse_gap_count:
				var center: float = sp.helper_ellipse_start_angle + span * (g + 0.5) / sp.helper_ellipse_gap_count
				var d := absf(fposmod(t - center + PI, TAU) - PI)
				if d < sp.helper_ellipse_gap_width * 0.5 - 0.001:
					bad.append("amount %d: bullet in gap %d" % [n, g])
	_assert_none(bad, "ellipse wall: amount on the arcs, the gaps stay clear")


# --- Fixes found while auditing the counts ---------------------------------

func test_rotated_ellipse_faces_its_true_normal() -> void:
	# The outward normal used to divide ROTATED coordinates by the unrotated
	# radii: facings were wrong whenever rotation != 0 and rx != ry.
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_ELLIPSE)
	sp.helper_ellipse_radius_x = 200.0
	sp.helper_ellipse_radius_y = 80.0
	sp.helper_ellipse_rotation = 0.6
	sp.helper_bullets_amount = 24
	var worst := 0.0
	for t in sp.collect_spawn_transforms():
		var tr := t as Transform2D
		var local := tr.origin.rotated(-0.6) # spawner at the origin
		var n_local := Vector2(local.x / (200.0 * 200.0), local.y / (80.0 * 80.0))
		var want := n_local.rotated(0.6).angle()
		worst = maxf(worst, absf(angle_difference(tr.get_rotation(), want)))
	assert_lt(worst, 0.001, "every bullet faces the ellipse's outward normal (worst %.5f rad)" % worst)


func test_rain_rows_are_layered_sheets() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RAIN)
	sp.helper_rain_jitter = 0.0
	sp.helper_bullets_amount = 40 # 13 drops per row (600 / 48 + 1) -> 4 rows
	var rows := {}
	for p in _origins(sp):
		var key := snappedf(p.y, 0.01)
		if not rows.has(key):
			rows[key] = []
		rows[key].append(p.x)
	assert_eq(rows.size(), 4, "40 drops stack into 4 sheets")
	for key in rows:
		var xs: Array = rows[key]
		if xs.size() == 13:
			assert_almost_eq(xs.min(), -300.0, 0.01, "full sheet spans the band (left)")
			assert_almost_eq(xs.max(), 300.0, 0.01, "full sheet spans the band (right)")
	var ys: Array = rows.keys()
	ys.sort()
	assert_almost_eq(ys[1] - ys[0], sp.helper_rain_drop_spacing, 0.01, "sheets are drop_spacing apart")


func test_flower_fan_track_skips_petals_without_bullets() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER, 3)
	sp.helper_flower_type = BulletPatterns2D.FLOWER_FAN
	sp.helper_flower_petals = 6
	await idle(6)
	# 3 bullets over 6 petals light petals 1, 3 and 5 (symmetric spread).
	var lit := [1, 3, 5]
	var seen := {}
	for p in sp.debug_get_preview_track_points():
		if not p.is_finite():
			continue
		var petal := int(round(fposmod(p.angle(), TAU) / (TAU / 6.0))) % 6
		seen[petal] = true
	assert_eq(_sorted(seen.keys()), lit, "the track draws exactly the petals that fire")
	var dots := PackedVector2Array()
	for t in sp.collect_spawn_transforms():
		dots.append((t as Transform2D).origin)
	for d in dots:
		var petal := int(round(fposmod(d.angle(), TAU) / (TAU / 6.0))) % 6
		assert_has(lit, petal, "every dot sits on a drawn petal")


static func _sorted(a: Array) -> Array:
	var b := a.map(func(k): return int(k))
	b.sort()
	return b
