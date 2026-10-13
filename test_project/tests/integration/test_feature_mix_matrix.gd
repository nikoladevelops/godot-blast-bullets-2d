extends BlastTest
## Features mixed through the REAL spawner path, where bullet hell scenes
## combine them: random per-bullet sizes (the Randomization group) reach
## flocking, graze and homing reach bullet by bullet (each counts its OWN
## size, never the volley's largest), and burst shots (begin_burst) flock
## per shot, per spawner or shared exactly as flock_scope says.

const DT := 1.0 / 60.0


## A resting ring of `n` 16 px bullets (circle r4 hitbox) at `at`, each
## scaled by a seeded uniform roll in [lo, hi].
func _sized_spawner(n: int, at: Vector2, lo: float, hi: float, ring_radius := 60.0) -> BulletSpawner2D:
	var d := H.make_volley_data(n, 0.0, 60.0)
	d.texture_size = Vector2(16, 16)
	d.collision_shape = H.make_circle_shape(4.0)
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_HELPER_RING, n)
	sp.global_position = at
	sp.helper_ring_radius = ring_radius
	sp.bullet_scale_mode = 1 # Uniform
	sp.bullet_scale_min = lo
	sp.bullet_scale_max = hi
	sp.bullet_random_seed = 77
	return sp


func _fire(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fires")
	return got[0] if got.size() > 0 else null


func _scale_of(v: BulletVolley2D, i: int) -> float:
	var t: Transform2D = v.get_bullet_transform(i)
	return maxf(t.x.length(), t.y.length())


func test_random_sizes_reach_the_flock_bullet_by_bullet() -> void:
	var sp := _sized_spawner(12, Vector2.ZERO, 0.5, 3.0)
	sp.flock_enabled = true
	var v := _fire(sp)
	factory.debug_advance_time(DT)
	var sizes := {}
	var problems: Array = []
	for i in 12:
		var t: Transform2D = v.get_bullet_transform(i)
		var want := 8.0 * 0.5 * (t.x.length() + t.y.length()) # half the 16 px quad x its mean scale
		var got: float = v.debug_get_bullet_flock_info(i)["radius"]
		sizes[snappedf(want, 0.1)] = true
		if absf(got - want) > 1e-3:
			problems.append("bullet %d: flock radius %.3f, its size %.3f" % [i, got, want])
	assert_eq("\n".join(problems), "", "every bullet's own size")
	assert_gt(sizes.size(), 6, "the roll really varied the sizes")


func test_random_sizes_reach_graze_bullet_by_bullet() -> void:
	# A target in the middle of a resting ring (radius 60) of random-sized
	# bullets; ring 50 + each bullet's own radius (4 x its scale): bullets of
	# scale 2.5+ graze, smaller ones do not.
	make_graze_target(Vector2.ZERO, &"mix_targets")
	var sp := _sized_spawner(16, Vector2.ZERO, 1.0, 4.0)
	sp.graze_enabled = true
	sp.graze_zones = [H.make_graze_zone([50.0], true)]
	sp.graze_node_group = &"mix_targets"
	var v := _fire(sp)
	factory.debug_advance_time(DT)
	var problems: Array = []
	var inside := 0
	for i in 16:
		var dist: float = v.get_bullet_transform(i).origin.length()
		var reach := 50.0 + 4.0 * _scale_of(v, i)
		if absf(dist - reach) < 0.01:
			continue # on the boundary: rounding decides
		var want := dist <= reach
		inside += 1 if want else 0
		if v.is_bullet_inside_graze(i, 0) != want:
			problems.append("bullet %d (scale %.2f, %.1f px): inside %s, expected %s" % [i, _scale_of(v, i), dist, v.is_bullet_inside_graze(i, 0), want])
	assert_eq("\n".join(problems), "", "each bullet grazes by its own size")
	assert_between(inside, 1, 15, "some bullets graze and some do not")


func test_random_sizes_reach_homing_reach_bullet_by_bullet() -> void:
	# The same ring homing on its center with a 50 px reach counting size.
	var target := make_graze_target(Vector2.ZERO, &"mix_homing")
	var sp := _sized_spawner(16, Vector2.ZERO, 1.0, 4.0)
	sp.homing_node_group = &"mix_homing"
	sp.homing_distance_before_reached = 50.0
	sp.homing_reach_counts_bullet_size = true
	sp.set_homing_enabled(true)
	var reached := {}
	sp.volley_bullet_homing_target_reached.connect(func(_v, i: int, _t, _p) -> void: reached[i] = true)
	var v := _fire(sp)
	assert_true(v.homing_reach_counts_bullet_size, "the spawner hands the option to its volleys")
	await idle(3)
	var problems: Array = []
	for i in 16:
		var dist: float = v.get_bullet_transform(i).origin.distance_to(target.global_position)
		var reach := 50.0 + 4.0 * _scale_of(v, i)
		if absf(dist - reach) < 0.01:
			continue
		if reached.has(i) != (dist <= reach):
			problems.append("bullet %d (scale %.2f, %.1f px): reached %s" % [i, _scale_of(v, i), dist, reached.has(i)])
	assert_eq("\n".join(problems), "", "each bullet reaches by its own size")


func test_burst_shots_flock_as_the_scope_says(scope: int = use_parameters([BulletFlock2D.SCOPE_PER_VOLLEY, BulletFlock2D.SCOPE_PER_SPAWNER, BulletFlock2D.SCOPE_SHARED])) -> void:
	# Two spawners, each a 3-shot burst; the flocks a pass counts follow the
	# scope: 6 (every shot alone), 2 (one per spawner) or 1 (all together).
	var spawners: Array = []
	for k in 2:
		var sp := _sized_spawner(6, Vector2(400 * k, 0), 1.0, 1.0, 20.0)
		sp.flock_enabled = true
		sp.flock_scope = scope
		sp.burst_enabled = true
		sp.burst_count = 3
		sp.burst_interval_sec = 0.05
		spawners.append(sp)
	var finished := [0]
	for sp in spawners:
		sp.burst_finished.connect(func() -> void: finished[0] += 1)
		sp.begin_burst()
	for f in 60:
		await idle(1)
		if finished[0] == 2:
			break
	assert_eq(finished[0], 2, "both bursts finished")
	await idle(1)
	var stats: Dictionary = factory.debug_get_flock_stats()
	var want: int = {BulletFlock2D.SCOPE_PER_VOLLEY: 6, BulletFlock2D.SCOPE_PER_SPAWNER: 2, BulletFlock2D.SCOPE_SHARED: 1}[scope]
	assert_eq(stats["flocks"], want, "scope %d: %d flocks" % [scope, want])
	assert_eq(stats["members"], 36, "every bullet of every shot flocks")


func _line(lines: PackedStringArray, prefix: String) -> String:
	for l in lines:
		if l.begins_with(prefix):
			return l
	return ""


func test_debug_explain_features_says_what_each_group_does_and_why_not() -> void:
	var sp := _sized_spawner(6, Vector2.ZERO, 1.0, 1.0)
	var lines := sp.debug_explain_features()
	for prefix in ["Homing: off.", "Orbiting: off.", "Flocking: off.", "Graze: off."]:
		assert_ne(_line(lines, prefix), "", prefix)
	# Inert setups are explained.
	sp.set_homing_enabled(true)
	sp.homing_node_group = &"nobody_here"
	sp.orbiting_enabled = true
	sp.graze_enabled = true
	sp.flock_enabled = true
	lines = sp.debug_explain_features()
	assert_string_contains(_line(lines, "Homing:"), "0 target(s) found now. Bullets fly straight until a target appears")
	assert_string_contains(_line(lines, "Orbiting:"), "(no target found now)")
	assert_string_contains(_line(lines, "Graze:"), "graze_zones is empty: nothing grazes")
	assert_string_contains(_line(lines, "Flocking:"), "the factory's default flock (Swarm), scope Per Spawner. Turns at most 270 deg/s, eases in over 0.3 s.")
	assert_string_contains(_line(lines, "Flocking:"), "separation (10.0 rad/s) outpaces homing (5.0 rad/s)")
	assert_string_contains(_line(lines, "Flocking:"), "A bullet locked in an orbit is never turned by the flock.")
	# A working setup.
	make_graze_target(Vector2(500, 0), &"explain_targets")
	sp.homing_node_group = &"explain_targets"
	sp.graze_node_group = &"explain_targets"
	sp.graze_zones = [H.make_graze_zone([30.0], true)]
	sp.homing_reach_counts_bullet_size = true
	sp.bullet_scale_mode = 1
	sp.bullet_scale_max = 2.0
	var f := BulletFlock2D.create_preset(BulletFlock2D.PRESET_KEEP_DISTANCE)
	f.separation = 2.0
	sp.flock = f
	lines = sp.debug_explain_features()
	assert_string_contains(_line(lines, "Homing:"), "1 target(s) found now. Turns at 5.0 rad/s (homing_smoothing). Reached within 5.0 px of each bullet's edge.")
	assert_string_contains(_line(lines, "Graze:"), "1 zone(s), source Node Group, 1 target(s) found now; rings grow by each bullet's own size.")
	assert_string_contains(_line(lines, "Flocking:"), "its own BulletFlock2D (Custom)")
	assert_string_contains(_line(lines, "Flocking:"), "is weaker than homing (5.0 rad/s): bullets bunch up at the target.")
	assert_string_contains(_line(lines, "Randomization:"), "bullet sizes vary (1.0 to 2.0)")
	expect_no_errors("explaining pushes nothing")


const FEATURES := ["flock", "homing", "orbit", "graze", "wobble", "bounce", "random_scale", "burst", "pool"]
const PAIRS := ["flock+homing", "flock+orbit", "flock+graze", "flock+wobble", "flock+bounce", "flock+random_scale", "flock+burst", "flock+pool", "homing+orbit", "homing+graze", "homing+wobble", "homing+bounce", "homing+random_scale", "homing+burst", "homing+pool", "orbit+graze", "orbit+wobble", "orbit+bounce", "orbit+random_scale", "orbit+burst", "orbit+pool", "graze+wobble", "graze+bounce", "graze+random_scale", "graze+burst", "graze+pool", "wobble+bounce", "wobble+random_scale", "wobble+burst", "wobble+pool", "bounce+random_scale", "bounce+burst", "bounce+pool", "random_scale+burst", "random_scale+pool", "burst+pool"]


## One spawner with `features` on, firing a 12-bullet ring through real
## physics frames (bounce needs them), measured every frame.
func _fly_mix(features: Array) -> MotionProbe:
	var d := H.make_volley_data(12, 160.0, 30.0)
	d.texture_size = Vector2(32, 32)
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	if "wobble" in features:
		var w := BulletWobbleData2D.new()
		w.enabled = true
		d.shared_bullet_wobble_data = w
	if "bounce" in features:
		d.set_collision_mask_from_array([3]) # make_wall's layer
		d.set_bounce_mask_from_array([3])
		make_wall(Vector2(120, 0))
		make_wall(Vector2(-120, 0))
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 12)
	sp.helper_ring_radius = 20.0
	if "bounce" in features:
		sp.body_entered.connect(func(_b, _v, _i) -> void: pass) # the hits are handled
	make_graze_target(Vector2(260, 60), &"mix_pair")
	if "homing" in features or "orbit" in features:
		sp.homing_node_group = &"mix_pair"
		sp.set_homing_enabled(true)
	if "orbit" in features:
		sp.orbiting_enabled = true
		sp.orbiting_radius = 50.0
	if "graze" in features:
		sp.graze_zones = [H.make_graze_zone([40.0], true)]
		sp.graze_node_group = &"mix_pair"
		sp.graze_enabled = true
	if "flock" in features:
		sp.flock_enabled = true
	if "random_scale" in features:
		sp.bullet_scale_mode = 1 # Uniform
		sp.bullet_scale_min = 0.5
		sp.bullet_scale_max = 2.0
		sp.bullet_random_seed = 5
	var probe := MotionProbe.new(factory, [])
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: probe.add_volley(v, 0.0, Vector2(32, 32)))
	for shot in (3 if "burst" in features else 1):
		assert_true(sp.shoot_once(), "fires")
		for f in 9:
			await physics(1)
			probe.sample(1.0 / 60.0)
	for f in 80:
		await physics(1)
		probe.sample(1.0 / 60.0)
	if "pool" in features:
		await idle(1)
		factory.clear_active_bullets()
		await idle(2)
		assert_true(sp.shoot_once(), "fires from the pool")
		for f in 40:
			await physics(1)
			probe.sample(1.0 / 60.0)
	return probe


func test_every_feature_pair_flies_cleanly(pair: String = use_parameters(PAIRS)) -> void:
	var features := Array(pair.split("+"))
	var probe: MotionProbe = await _fly_mix(features)
	assert_eq(probe.nonfinite, 0, "%s: finite poses" % pair)
	assert_eq(probe.steer_snaps, 0, "%s: no steering snap (wall bounces aside): %s" % [pair, probe.summary()])
	var bad: Array = []
	for v in probe._volleys:
		if is_instance_valid(v):
			bad.append_array(broken_bullets(v))
	assert_eq("\n".join(bad.slice(0, 4)), "", "%s: finite, unit headings" % pair)
	assert_true(factory.debug_assert_no_dangling()["ok"], "%s: balanced books" % pair)
	expect_no_errors("%s: nothing pushed" % pair)
