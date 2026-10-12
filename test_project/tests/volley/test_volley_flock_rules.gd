extends BlastTest
## Flock rules (BulletFlock2D through BulletVolley2D.flock_join): each rule
## alone does what boids do (cohesion closes gaps, alignment lines headings
## up, separation opens gaps, stacked bullets part), only flockmates in range
## count, zero weights and calm flocks are bit-identical to no flock, the
## max_neighbors cap is exact and unbiased, the turn cap holds every tick,
## only the direction turns (|direction| stays 1, the speed stays the speed
## data's), results are deterministic and close across frame rates and
## volley orders, and a negative speed flies with its flock.

const DT := 1.0 / 60.0


func _volley(transforms: Array, speed := 100.0) -> BulletVolley2D:
	var d := H.make_flight_data(transforms, speed, 1000.0)
	return factory.spawn_volley(d)


func _flocked(transforms: Array, flock: BulletFlock2D, speed := 100.0) -> BulletVolley2D:
	var v := _volley(transforms, speed)
	assert_true(v.flock_join(flock), "joins")
	return v


func _step(n: int, delta := DT) -> void:
	for k in n:
		assert_true(factory.debug_advance_time(delta), "tick %d runs" % k)


func _origins(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		out.append(v.get_bullet_transform(i).origin)
	return out


func _headings(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		out.append(v.get_bullet_direction(i))
	return out


func _polarization(v: BulletVolley2D) -> float:
	var sum := Vector2()
	for h in _headings(v):
		sum += h
	return sum.length() / v.get_amount_bullets()


func _min_pair_distance(points: Array) -> float:
	var best := INF
	for a in points.size():
		for b in range(a + 1, points.size()):
			best = minf(best, (points[a] as Vector2).distance_to(points[b]))
	return best


func test_cohesion_turns_flockmates_toward_each_other() -> void:
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 30))]
	var v := _flocked(tr, H.make_flock(0.0, 0.0, 1.0))
	var control := _volley(tr)
	_step(1)
	var s0: Vector2 = v.debug_get_bullet_flock_steering(0)
	assert_gt(s0.y, 0.0, "bullet 0 is pulled toward bullet 1 (+y)")
	assert_almost_eq(s0.x, 0.0, 1e-6, "straight toward the center")
	assert_almost_eq(s0.y, 30.0 / 100.0, 1e-5, "cohesion = mean offset / neighbor_range")
	_step(20)
	var p: Array = _origins(v)
	assert_lt((p[0] as Vector2).distance_to(p[1]), 25.0, "the gap closes")
	assert_gt(v.get_bullet_direction(0).y, 0.0, "bullet 0 heads down")
	assert_lt(v.get_bullet_direction(1).y, 0.0, "bullet 1 heads up")
	assert_eq(_origins(control)[1] - _origins(control)[0], Vector2(0, 30), "the unflocked control keeps its gap")


func test_alignment_lines_the_headings_up() -> void:
	seed(7)
	var tr: Array = []
	for k in 9:
		tr.append(Transform2D(randf_range(-1.2, 1.2), Vector2((k % 3) * 12.0, (k / 3) * 12.0)))
	var v := _flocked(tr, H.make_flock(0.0, 2.0, 0.0, 200.0, 0.0))
	var before := _polarization(v)
	var last := before
	var problems: Array = []
	for check in 12:
		_step(10)
		var now := _polarization(v)
		if now < last - 1e-4:
			problems.append("check %d: %.5f < %.5f" % [check, now, last])
		last = now
	assert_eq("\n".join(problems), "", "polarization never drops")
	assert_gt(last, 0.995, "the headings line up (from %.3f)" % before)


func test_a_calm_aligned_flock_flies_bit_identical_to_no_flock() -> void:
	var tr: Array = []
	for k in 6:
		tr.append(Transform2D(0.3, Vector2(k * 15.0, k * 4.0)))
	var v := _flocked(tr, H.make_flock(0.0, 1.0, 0.0))
	var control := _volley(tr)
	_step(30)
	assert_eq(_origins(v), _origins(control), "every heading already matches the flock: nothing turns")
	assert_eq(_headings(v), _headings(control), "headings untouched")


func test_separation_opens_the_gap_up_to_the_separation_range() -> void:
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 10))]
	var v := _flocked(tr, H.make_flock(2.0, 0.0, 0.0, 100.0, 40.0))
	_step(1)
	var s0: Vector2 = v.debug_get_bullet_flock_steering(0)
	assert_lt(s0.y, 0.0, "bullet 0 is pushed away from bullet 1 (-y)")
	assert_almost_eq(s0.y, -2.0 * (1.0 - 10.0 / 40.0), 1e-5, "separation = weight x (1 - d / range) along the pair")
	var gap := 0.0
	for tick in 240: # early break once the pair left the separation range
		_step(1)
		var p: Array = _origins(v)
		gap = (p[0] as Vector2).distance_to(p[1])
		if gap > 41.0:
			break
	assert_gt(gap, 41.0, "pushed out of the separation range")
	assert_lt(v.get_bullet_direction(0).y, 0.0, "bullet 0 turned away (up)")
	assert_gt(v.get_bullet_direction(1).y, 0.0, "bullet 1 turned away (down)")
	# Out of range (the pass measures the gap before the move): no push, no turn.
	_step(1)
	var headings := _headings(v)
	_step(20)
	assert_eq(v.debug_get_bullet_flock_steering(0), Vector2(), "no push beyond the separation range")
	assert_eq(_headings(v), headings, "the headings stay where the push left them")


func test_stacked_bullets_part_deterministically() -> void:
	var runs: Array = []
	for run in 2:
		var tr: Array = []
		for k in 6:
			tr.append(Transform2D(0.0, Vector2(50, 50)))
		var v := _flocked(tr, H.make_flock(2.0, 0.0, 0.0, 100.0, 40.0))
		_step(60)
		var p := _origins(v)
		assert_gt(_min_pair_distance(p), 4.0, "run %d: every stacked pair parted" % run)
		runs.append(p)
		factory.reset()
		await idle(1)
	assert_eq(runs[0], runs[1], "the same stack parts the same way every time")


func test_flockmates_out_of_range_change_nothing() -> void:
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 150)), Transform2D(0.0, Vector2(150, 0))]
	var v := _flocked(tr, H.make_flock(2.0, 1.0, 1.0, 100.0, 40.0))
	var control := _volley(tr)
	_step(30)
	assert_eq(_origins(v), _origins(control), "150 px apart with a 100 px range: no flockmate in sight")
	for i in 3:
		assert_eq(v.debug_get_bullet_flock_neighbors(i), 0, "bullet %d sees nobody" % i)
		assert_eq(v.debug_get_bullet_flock_steering(i), Vector2(), "bullet %d has no steering" % i)


func test_zero_weights_are_bit_identical_to_no_flock() -> void:
	seed(3)
	var tr: Array = []
	for k in 12:
		tr.append(Transform2D(randf_range(-PI, PI), Vector2(randf_range(0, 40), randf_range(0, 40))))
	var v := _flocked(tr, H.make_flock(0.0, 0.0, 0.0))
	var control := _volley(tr)
	_step(30)
	assert_eq(_origins(v), _origins(control), "positions bit-identical")
	assert_eq(_headings(v), _headings(control), "headings bit-identical")
	assert_gt(v.debug_get_bullet_flock_neighbors(0), 0, "the flockmates were seen, just weightless")


func test_ranges_are_independent() -> void:
	# Separation reaching past the neighbor range still pushes.
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 30))]
	var v := _flocked(tr, H.make_flock(1.0, 0.0, 1.0, 20.0, 60.0))
	_step(1)
	var s0: Vector2 = v.debug_get_bullet_flock_steering(0)
	assert_almost_eq(s0.y, -1.0 * (1.0 - 30.0 / 60.0), 1e-5, "separation only: the pair is outside the 20 px neighbor range")
	assert_eq(v.debug_get_bullet_flock_neighbors(0), 1, "counted once")


func test_mirrored_flocks_fly_mirrored() -> void:
	seed(11)
	var tr: Array = []
	var mirrored: Array = []
	for k in 7:
		var angle := randf_range(-0.8, 0.8)
		var pos := Vector2(randf_range(0, 60), randf_range(5, 50))
		tr.append(Transform2D(angle, pos))
		mirrored.append(Transform2D(-angle, Vector2(pos.x, -pos.y) + Vector2(0, 1000)))
	var f := H.make_flock(1.5, 1.0, 1.0)
	var a := _flocked(tr, f)
	var b := _flocked(mirrored, H.make_flock(1.5, 1.0, 1.0))
	_step(60)
	var problems: Array = []
	for i in 7:
		var pa: Vector2 = a.get_bullet_transform(i).origin
		var pb: Vector2 = b.get_bullet_transform(i).origin
		var want := Vector2(pa.x, 1000.0 - pa.y)
		if pb.distance_to(want) > 0.01:
			problems.append("bullet %d: %s vs mirrored %s" % [i, pb, want])
	assert_eq("\n".join(problems), "", "the mirror image of a flock flies the mirror image")


func test_a_capped_lattice_has_no_net_pull() -> void:
	# One still bullet per grid cell (cells are half the neighbor range), so
	# every flockmate a capped bullet reads comes from the cell rings around
	# it: with the ring start rotating per bullet the pulls cancel out (a
	# fixed scan order would pull every bullet toward the same corner).
	var tr: Array = []
	for row in 20:
		for col in 20:
			tr.append(Transform2D(0.0, Vector2(25.0 + 50.0 * col, 25.0 + 50.0 * row)))
	var capped := H.make_flock(0.0, 0.0, 1.0)
	capped.max_neighbors = 3
	var v := _flocked(tr, capped, 0.0)
	_step(1)
	var net := Vector2()
	var total := 0.0
	for i in tr.size():
		var s: Vector2 = v.debug_get_bullet_flock_steering(i)
		net += s
		total += s.length()
		assert_eq(v.debug_get_bullet_flock_neighbors(i), 3, "bullet %d reads exactly the cap" % i)
	assert_gt(total, 10.0, "there is a pull to cancel")
	assert_lt(net.length() / total, 0.1, "no drift bias: |sum| / sum|.| = %.4f" % (net.length() / total))


func test_max_neighbors_is_exact_and_bounds_the_work(cap: int = use_parameters([0, 1, 5, 49, 100])) -> void:
	var tr: Array = []
	for k in 50:
		tr.append(Transform2D(0.0, Vector2(10, 10)))
	var f := H.make_flock(1.0, 1.0, 1.0)
	f.max_neighbors = cap
	var v := _flocked(tr, f, 0.0)
	_step(1)
	var want := 49 if cap == 0 or cap >= 49 else cap
	for i in 50:
		assert_eq(v.debug_get_bullet_flock_neighbors(i), want, "bullet %d reads %d flockmates (cap %d)" % [i, want, cap])
	var stats: Dictionary = factory.debug_get_flock_stats()
	assert_eq(stats["neighbor_checks"], 50 * want, "stacked: every check is a flockmate, so the work is exactly 50 x %d" % want)
	assert_eq(stats["capped_members"], 50 if cap > 0 and cap <= 49 else 0, "capped bullets counted")


func test_the_turn_cap_holds_every_tick() -> void:
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(PI, Vector2(0, 20))]
	var f := H.make_flock(0.0, 50.0, 50.0)
	f.max_turn_rate_degrees = 90.0
	var v := _flocked(tr, f)
	var free_v := _flocked(tr, H.make_flock(0.0, 50.0, 50.0))
	var limit := deg_to_rad(90.0) * DT
	var problems: Array = []
	var biggest_free := 0.0
	for tick in 40:
		var before: Vector2 = v.get_bullet_direction(0)
		var before_free: Vector2 = free_v.get_bullet_direction(0)
		_step(1)
		var turned := absf(before.angle_to(v.get_bullet_direction(0)))
		if turned > limit + 1e-5:
			problems.append("tick %d turned %.5f > %.5f" % [tick, turned, limit])
		biggest_free = maxf(biggest_free, absf(before_free.angle_to(free_v.get_bullet_direction(0))))
	assert_eq("\n".join(problems), "", "never more than 90 deg/s")
	assert_gt(biggest_free, limit * 4.0, "without the cap the same flock turns much faster")


func test_only_the_direction_turns() -> void:
	seed(5)
	var tr: Array = []
	for k in 6:
		tr.append(Transform2D(randf_range(-PI, PI), Vector2(randf_range(0, 30), randf_range(0, 30))))
	var v := _flocked(tr, H.make_flock(1.5, 1.0, 1.0), 120.0)
	var problems: Array = []
	for round in 100:
		_step(100)
		for i in 6:
			var d: Vector2 = v.get_bullet_direction(i)
			if absf(d.length() - 1.0) > 1e-5:
				problems.append("tick %d bullet %d: |direction| = %.7f" % [(round + 1) * 100, i, d.length()])
			if absf(v.get_bullet_velocity(i).length() - 120.0) > 1e-3:
				problems.append("tick %d bullet %d: |velocity| = %.5f" % [(round + 1) * 100, i, v.get_bullet_velocity(i).length()])
	assert_eq("\n".join(problems.slice(0, 8)), "", "10k ticks: unit direction, speed untouched")


func test_two_runs_are_bit_identical() -> void:
	var runs: Array = []
	for run in 2:
		seed(21)
		var tr: Array = []
		for k in 40:
			tr.append(Transform2D(randf_range(-PI, PI), Vector2(randf_range(0, 120), randf_range(0, 120))))
		var f := H.make_flock(1.5, 1.0, 1.0)
		f.max_neighbors = 8
		var v := _flocked(tr, f)
		_step(90)
		runs.append(_origins(v))
		factory.reset()
		await idle(1)
	assert_eq(runs[0], runs[1], "deterministic")


func test_volley_order_only_changes_rounding() -> void:
	seed(9)
	var first: Array = []
	var second: Array = []
	for k in 5:
		first.append(Transform2D(randf_range(-1, 1), Vector2(randf_range(0, 50), randf_range(0, 50))))
		second.append(Transform2D(randf_range(-1, 1), Vector2(randf_range(0, 50), randf_range(0, 50))))
	var results: Array = []
	for order in 2:
		var f := H.make_flock(1.5, 1.0, 1.0)
		var a: BulletVolley2D
		var b: BulletVolley2D
		if order == 0:
			a = _flocked(first, f)
			b = _flocked(second, f)
		else:
			b = _flocked(second, f)
			a = _flocked(first, f)
		_step(60)
		results.append(_origins(a) + _origins(b))
		factory.reset()
		await idle(1)
	var problems: Array = []
	for i in results[0].size():
		if (results[0][i] as Vector2).distance_to(results[1][i]) > 0.01:
			problems.append("bullet %d: %s vs %s" % [i, results[0][i], results[1][i]])
	assert_eq("\n".join(problems), "", "every bullet steers by where its flockmates stood before the sweep")


func test_frame_rate_changes_the_flight_only_slightly() -> void:
	var results: Array = []
	for rate in [60, 120]:
		var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.4, Vector2(10, 40)), Transform2D(-0.3, Vector2(30, 15))]
		var v := _flocked(tr, H.make_flock(1.5, 1.0, 1.0))
		_step(rate, 1.0 / rate)
		results.append([_origins(v), _headings(v)])
		factory.reset()
		await idle(1)
	var problems: Array = []
	for i in 3:
		var pa: Vector2 = results[0][0][i]
		var pb: Vector2 = results[1][0][i]
		if pa.distance_to(pb) > 1.5:
			problems.append("bullet %d position %s vs %s" % [i, pa, pb])
		if absf((results[0][1][i] as Vector2).angle_to(results[1][1][i])) > 0.05:
			problems.append("bullet %d heading %s vs %s" % [i, results[0][1][i], results[1][1][i]])
	assert_eq("\n".join(problems), "", "1 s at 60 Hz and at 120 Hz agree")


func test_a_negative_speed_flies_with_its_flock() -> void:
	var d := H.make_flight_data([Transform2D(0.0, Vector2(0, 0)), Transform2D(PI, Vector2(0, 20))], 100.0, 1000.0)
	d.all_bullet_speed_data = [H.make_speed(100.0), H.make_speed(-100.0, 3000.0)]
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.flock_join(H.make_flock(0.0, 1.0, 0.0))
	_step(30)
	assert_eq(v.get_bullet_direction(0), Vector2(1, 0), "bullet 0 already flies with bullet 1 (which reverses its facing)")
	assert_almost_eq(v.get_bullet_direction(1), Vector2(-1, 0), Vector2(1e-6, 1e-6), "bullet 1 flies +x on a negative speed: nothing to align")
	assert_eq(v.debug_get_bullet_flock_neighbors(1), 1, "the pair sees each other")
	assert_almost_eq(v.get_bullet_transform(1).origin.x - v.get_bullet_transform(0).origin.x, 0.0, 1e-3, "side by side")
