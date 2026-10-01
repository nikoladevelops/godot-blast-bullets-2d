extends SceneTree
## Curves-baseline characterization suite: pins the CURRENT curve-clear and
## direction-accumulation semantics numerically.
##
## Context: clearing shared/per-bullet curves keeps the last sampled
## ballistics (no baseline restore), and Additive direction curves accumulate
## onto the persistent direction each tick (steering-like). Whether that is
## intended steering or a drift bug is a product decision; a base/effective
## refactor would change observable behavior. This suite documents the current
## contract so any future change (or accidental drift) fails loudly instead
## of shipping silently.
##
## Covers: T1 shared speed curve drives speed, T2 clearing shared curves
## freezes the last sampled speed (pinned), T3 per-bullet clear freezes too,
## T4 constant Additive x-offset steers measurably over ticks (pinned
## cumulative), T5 direction stays finite/normalized, T6 no dangling.
##
## Run: godot --headless --path test_project --script tests/volley/test_volley_curves_baseline.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _flat_curve(value: float) -> Curve:
	var c := Curve.new()
	# Curve clamps points into [min_value, max_value] (default max 1.0):
	# raise the ceiling first or big speeds clamp to 1.
	c.min_value = -10000.0
	c.max_value = 10000.0
	c.add_point(Vector2(0, value))
	c.add_point(Vector2(1, value))
	return c

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	# Fly +Y so a constant +X offset visibly steers the heading.
	d.transforms = [Transform2D(PI / 2.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	s.speed = 200.0
	s.max_speed = 3000.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = 60.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	return d

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("CURVEBASE T1 shared speed curve drives the bullet")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	var cc := BulletCurvesData2D.new()
	cc.movement_speed_curve = _flat_curve(400.0)
	v.set_shared_bullet_curves_data(cc)
	for i in 10:
		await physics_frame
	var driven: float = v.get_bullet_speed_data(0).speed
	_check(absf(driven - 400.0) < 5.0, "T1 curve drives speed to 400 (got %.1f)" % driven)

	# ---------------------------------------------------------------
	printerr("CURVEBASE T2 clearing shared curves freezes last speed (pinned)")
	v.remove_shared_bullet_curves_data()
	for i in 10:
		await physics_frame
	var frozen: float = v.get_bullet_speed_data(0).speed
	_check(absf(frozen - driven) < 5.0, "T2 speed frozen at last sample (got %.1f, was %.1f)" % [frozen, driven])
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("CURVEBASE T3 per-bullet clear freezes too (pinned)")
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	var c0 := BulletCurvesData2D.new()
	c0.movement_speed_curve = _flat_curve(500.0)
	v3.bullet_set_curves_data(0, c0)
	for i in 10:
		await physics_frame
	var driven3: float = v3.get_bullet_speed_data(0).speed
	_check(absf(driven3 - 500.0) < 5.0, "T3 per-bullet curve drives (got %.1f)" % driven3)
	v3.bullet_set_curves_data(0, null)
	for i in 10:
		await physics_frame
	_check(absf(v3.get_bullet_speed_data(0).speed - driven3) < 5.0, "T3 per-bullet clear freezes (got %.1f)" % v3.get_bullet_speed_data(0).speed)
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("CURVEBASE T4 constant Additive x-offset steers cumulatively (pinned)")
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	var dc := BulletCurvesData2D.new()
	dc.x_direction_curve = _flat_curve(0.5)
	dc.x_direction_curve_strength = 1.0
	dc.x_direction_curve_mode = 0 # Additive
	v4.set_shared_bullet_curves_data(dc)
	for i in 5:
		await physics_frame
	var early: float = v4.get_bullet_direction(0).angle()
	for i in 30:
		await physics_frame
	var late: float = v4.get_bullet_direction(0).angle()
	_check(absf(angle_difference(early, late)) > 0.05, "T4 heading steered over ticks (%.3f -> %.3f)" % [early, late])
	_check(v4.get_bullet_direction(0).is_normalized(), "T5 direction stays normalized")
	_check(v4.get_bullet_direction(0).is_finite(), "T5 direction stays finite")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL CURVES-BASELINE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
