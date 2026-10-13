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
