extends BlastTest
## What the plugin hands the GPU is always sane: every float of every MultiMesh
## buffer it owns (bullet volleys, effect and trail shards, the collision
## debugger), every laser body vertex and every laser look pose is finite, in
## the demo's own setups (hit and kill sparks, the test lab's reflecting
## Lock On laser against a body, the collision debugger) and under hostile
## input (1e30 velocities, 8e35 teleports, rejected NaN/INF writes, huge
## shapes). Ordinary play also stays inside +-1e6 px. A non-finite vertex is
## what a GPU driver may hang on (a session crash once followed a GPU hang:
## the plugin's data was proven clean). The chaos fuzz runs the same check
## after every step (render_data_problems).

const SANE := 1.0e6


func _benchmark_laser() -> LaserData2D:
	var state := (load("res://benchmark_scene/benchmark.tscn") as PackedScene).get_state()
	for i in state.get_node_count():
		if state.get_node_name(i) == "LaserSpawner2D":
			for p in state.get_node_property_count(i):
				if state.get_node_property_name(i, p) == "laser_data":
					return state.get_node_property_value(i, p)
	return null


func test_the_demo_setups_hand_the_gpu_sane_data() -> void:
	handle_events(factory)
	factory.is_debugger_enabled = true
	var wall := make_wall(Vector2(400, 0), Vector2(40, 400), 5) # layers 1 and 3: a laser wall and a bullet target
	await idle(2)
	var d := H.make_volley_data(8, 600.0, 3.0)
	d.collision_mask = 4
	d.monitorable = true
	d.effect_layers = [BENCHMARK_GLOBALS.PLAYER_HIT_SPARK, BENCHMARK_GLOBALS.PLAYER_KILL_SPARK, BENCHMARK_GLOBALS.MUZZLE_FLASH]
	var problems: Array = []
	for shot in 6:
		factory.spawn_volley(d)
		for k in 8:
			await physics(1)
			problems.append_array(render_data_problems(factory, SANE))
	var laser_data := _benchmark_laser()
	assert_not_null(laser_data, "the test lab's laser data")
	var t: Array[Transform2D] = [Transform2D(0.3, Vector2.ZERO)]
	var laser := factory.spawn_laser(laser_data, t)
	laser.set_beam_target(0, wall)
	for k in 90:
		await physics(1)
		problems.append_array(render_data_problems(factory, SANE))
	assert_eq(problems.slice(0, 5), [], "sparks, the reflecting laser and the debugger stay finite and on screen")
	factory.is_debugger_enabled = false


func test_hostile_input_never_reaches_the_gpu_as_nan_or_inf() -> void:
	handle_events(factory)
	factory.is_debugger_enabled = true
	var d := H.make_volley_data(6, 1.0e30, 3.0)
	d.effect_layers = [BENCHMARK_GLOBALS.PLAYER_KILL_SPARK]
	var circle := CircleShape2D.new()
	circle.radius = 1.0e6
	d.collision_shape = circle
	var v := factory.spawn_volley(d)
	v.teleport_bullet(1, Vector2(8e35, -8e35))
	v.bullet_set_velocity(2, Vector2(1e38, -1e38))
	v.bullet_set_velocity(3, Vector2(NAN, 0))
	v.teleport_bullet(4, Vector2(INF, 0))
	v.set_bullet_scale(5, Vector2(NAN, 1))
	expect_error_sequence([
		"bullet_set_velocity: new_velocity magnitude is too large to hold (its square overflows), keeping the old value.",
		"bullet_set_velocity: new_velocity must be finite.",
		"teleport_bullet: new_global_pos must be finite (NaN/Inf is rejected).",
		"set_bullet_scale: new_scale must be finite and non-zero on both axes, keeping the old scale.",
	])
	var problems: Array = []
	for k in 20:
		await physics(1)
		problems.append_array(render_data_problems(factory, INF))
	assert_eq(problems.slice(0, 5), [], "finite everywhere, however far the bullets went")
	factory.is_debugger_enabled = false


## Laser looks that overflow when combined (each setter accepts its own
## finite value): width x body_width_scale past what a float holds, a
## subnormal width, a huge length, a huge warning width while charging. The
## body strip and the warning line stay finite (the body is clamped or not
## drawn); NaN vertices once reached canvas_item_add_triangle_array.
func test_overflowing_laser_looks_never_hand_the_gpu_nan() -> void:
	handle_events(factory)
	var cases: Array = []
	var wide := H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)
	wide.width = 12.0
	wide.body_width_scale = 1.0e38
	cases.append(wide)
	var thin := H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)
	thin.width = 1.0e-40
	cases.append(thin)
	var far := H.make_laser(0.0, 0.0, 0.0, 0.0, 3.0e38)
	cases.append(far)
	var warn := H.make_laser(1.0, 0.0, 0.0, 0.0, 200.0)
	warn.warning_width = 1.0e38
	cases.append(warn)
	var problems: Array = []
	for d in cases:
		var l := factory.spawn_laser(d, [Transform2D.IDENTITY] as Array[Transform2D])
		step_factory(2, 1.0 / 16.0)
		for p in (l.debug_get_body_mesh(1.0)["points"] as PackedVector2Array):
			if not p.is_finite():
				problems.append("width %s scale %s length %s: vertex %s" % [d.width, d.body_width_scale, d.length, p])
				break
		problems.append_array(render_data_problems(factory, INF))
		l.end_laser(false)
		step_factory(1, 1.0 / 16.0)
	assert_eq(problems, [], "every laser body vertex stays finite")


## A finite muzzle plus a finite length whose sum overflows real_t (a beam
## 1e38 px away and 3e38 px long): every accepted input is finite, so every
## beam point must stay finite too (the invariant check, the readouts and the
## charging warning polyline all carry them). A zero-length or huge-but-finite
## beam is fine; an INF vertex is not.
func test_a_huge_finite_muzzle_and_length_keep_the_beam_points_finite() -> void:
	handle_events(factory)
	var d := H.make_laser(0.5, 0.0, 0.0, 0.0, 3.0e38) # charging: the warning line draws the points
	var t: Array[Transform2D] = [Transform2D(0.0, Vector2(1.0e38, 0.0))]
	var l := factory.spawn_laser(d, t)
	assert_not_null(l, "the laser fired")
	_check_finite_beam(l, "right after the spawn")
	step_factory(1, 1.0 / 16.0)
	_check_finite_beam(l, "after a tick")
	assert_eq(l.debug_check_invariants(), "", "the beam geometry is a valid state")
	var books: Dictionary = factory.debug_assert_no_dangling()
	assert_true(books["ok"], str(books["error"]))
	l.end_laser(false)
	step_factory(1, 1.0 / 16.0)
	expect_no_errors()


func _check_finite_beam(l: LaserVolley2D, where: String) -> void:
	var bad: Array = []
	for p in l.get_beam_points(0):
		if not p.is_finite():
			bad.append(p)
	assert_eq(bad, [], "every beam point stays finite %s" % where)
