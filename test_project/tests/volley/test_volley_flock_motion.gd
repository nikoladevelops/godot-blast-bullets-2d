extends BlastTest
## How flocking bullets MOVE (MotionProbe: turn rate, turn-side flips,
## facing error, edge gaps), out of the box:
## - a bullet keeps its speed, so the flock turns it at the SIDEWAYS part of
##   its steering, capped by max_turn_rate_degrees: a crowd or a burst stack
##   never snaps a bullet around (the old model flipped 180 deg per tick);
## - a flockmate dead ahead turns a bullet aside, never back;
## - a new volley's steering fades in over blend_in_sec (spawn and pool
##   reuse alike), so a burst stacked at the muzzle opens up first;
## - the sprite stays on the heading (face_heading), random bullet sizes
##   included.

const DT := 1.0 / 60.0


## A ring of n bullets (radius 12, facing outward) at `at`, drawn `quad` px,
## each scaled by a seeded 1..scale_max.
func _ring(n: int, at: Vector2, quad: float, scale_max: float, seed_value: int, texture_rotation := 0.0) -> BulletVolleyData2D:
	seed(seed_value)
	var tr: Array = []
	for k in n:
		var a := TAU * k / n
		var s := randf_range(1.0, scale_max)
		tr.append(Transform2D(a, at + Vector2(cos(a), sin(a)) * 12.0).scaled_local(Vector2(s, s)))
	var d := H.make_flight_data(tr, 300.0, 1000.0)
	d.texture_size = Vector2(quad, quad)
	d.texture_rotation_radians = texture_rotation
	return d


## Three volleys of 24 fired 0.15 s apart from one muzzle into one flock,
## measured for 3 s.
func _burst(flock: BulletFlock2D, quad: float, scale_max: float, homing := false, texture_rotation := 0.0) -> MotionProbe:
	var probe := MotionProbe.new(factory, [])
	for shot in 3:
		var d := _ring(24, Vector2.ZERO, quad, scale_max, 7 + shot, texture_rotation)
		if homing:
			d.homing_smoothing = 5.0
			d.homing_take_control_of_texture_rotation = true
		var v: BulletVolley2D = factory.spawn_volley(d)
		v.flock_join(flock, BulletFlock2D.SCOPE_SHARED)
		if homing:
			v.shared_homing_deque_push_back_global_position_target(Vector2(700, 150))
		probe.add_volley(v, texture_rotation, Vector2(quad, quad))
		probe.run(9, DT, true)
	probe.run(153, DT, true)
	return probe


func test_a_burst_stack_of_big_random_sized_bullets_never_spins() -> void:
	# 72 bullets of 128 px at 1..2x stacked at the muzzle: the old model
	# flipped them 180 deg per tick (10000+ deg/s, 14 side flips per second).
	var probe := _burst(BulletFlock2D.new(), 128.0, 2.0)
	assert_lte(probe.max_turn_deg_per_sec, 270.0 * 1.001, "never faster than the default turn cap: " + probe.summary())
	assert_lt(probe.flips_per_bullet_sec(), 4.0, "no left-right jitter: " + probe.summary())
	assert_lt(probe.mean_turn_deg_per_sec(), 150.0, "calm on average: " + probe.summary())
	assert_eq(probe.snaps, 0, "no bullet ever whips 20 deg in one tick: " + probe.summary())
	assert_lt(probe.max_facing_error_deg, 0.01, "the sprite stays on the heading: " + probe.summary())
	assert_eq(probe.nonfinite, 0, "finite")


func test_small_bullets_burst_apart_with_clear_gaps() -> void:
	var probe := _burst(BulletFlock2D.new(), 16.0, 1.0)
	var g := probe.gaps()
	assert_gt(float(g["min_gap"]), 8.0, "every pair keeps a visible gap: " + probe.summary())
	assert_lt(probe.flips_per_bullet_sec(), 1.0, "smooth: " + probe.summary())


func test_a_homing_swarm_buzzes_but_never_spins() -> void:
	# Homing (5 rad/s) and the flock (270 deg/s) add up; a crowd circling
	# its target buzzes, it never snaps around.
	var probe := _burst(BulletFlock2D.new(), 128.0, 2.0, true, 0.7)
	assert_lte(probe.max_turn_deg_per_sec, (270.0 + rad_to_deg(5.0)) * 1.001, "flock cap + homing rate at most: " + probe.summary())
	# A crowd at its target turns a lot (separation outranks homing there),
	# but smoothly: no snaps, few side changes.
	assert_lt(probe.mean_turn_deg_per_sec(), 180.0, "calm on average: " + probe.summary())
	assert_lt(probe.flips_per_bullet_sec(), 5.0, "no jitter: " + probe.summary())
	assert_eq(probe.snaps, 0, "no snap: " + probe.summary())
	assert_lt(probe.max_facing_error_deg, 0.01, "the sprite (texture rotation stripped) stays on the heading: " + probe.summary())


func test_the_turn_never_exceeds_max_turn_rate_degrees(cap: float = use_parameters([45.0, 270.0, 1440.0])) -> void:
	var f := BulletFlock2D.new()
	f.max_turn_rate_degrees = cap
	f.turn_smoothing_sec = 0.0 # instant turns: the cap alone limits them
	var probe := _burst(f, 128.0, 2.0)
	assert_lte(probe.max_turn_deg_per_sec, cap * 1.001, "cap %.0f deg/s: %s" % [cap, probe.summary()])
	assert_gt(probe.max_turn_deg_per_sec, cap * 0.5, "the cap is actually reached in a stack")


func test_a_flockmate_dead_ahead_turns_a_bullet_aside_never_around() -> void:
	# A convoy: five overlapping bullets in one line along their heading.
	var tr: Array = []
	for k in 5:
		tr.append(Transform2D(0.0, Vector2(k * 8.0, 0.0)))
	var v: BulletVolley2D = factory.spawn_volley(H.make_flight_data(tr, 120.0, 1000.0))
	v.flock_join(BulletFlock2D.new())
	var probe := MotionProbe.new(factory, [v])
	var worst := 0.0
	for tick in 120:
		probe.run(1)
		for i in 5:
			worst = maxf(worst, absf(v.get_bullet_direction(i).angle()))
	assert_lt(rad_to_deg(worst), 90.0, "no bullet ever turned around (worst %.1f deg off)" % rad_to_deg(worst))
	assert_gt(float(probe.gaps()["min_gap"]), 0.0, "the convoy parted sideways: " + probe.summary())


func test_steering_fades_in_over_blend_in_sec() -> void:
	# Two identical pairs, one flock eases in: on the first tick its turn is
	# exactly smoothstep(dt / blend_in_sec) of the other's.
	var eased := H.make_flock(1.0, 0.0, 0.0, 100.0, 40.0)
	eased.blend_in_sec = 0.3
	var sharp := H.make_flock(1.0, 0.0, 0.0, 100.0, 40.0)
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 20))]
	var a: BulletVolley2D = factory.spawn_volley(H.make_flight_data(tr, 100.0))
	var b: BulletVolley2D = factory.spawn_volley(H.make_flight_data([Transform2D(0.0, Vector2(0, 900)), Transform2D(0.0, Vector2(0, 920))], 100.0))
	a.flock_join(eased)
	b.flock_join(sharp)
	factory.debug_advance_time(DT)
	var x := DT / 0.3
	var fade := x * x * (3.0 - 2.0 * x)
	var turn_eased := a.get_bullet_direction(0).angle()
	var turn_sharp := b.get_bullet_direction(0).angle()
	assert_ne(turn_sharp, 0.0, "the sharp flock turns at once")
	assert_almost_eq(turn_eased / turn_sharp, fade, 1e-3, "the first tick turns smoothstep(dt / 0.3) as far")
	for k in 20:
		factory.debug_advance_time(DT)
	var before := a.get_bullet_direction(0).angle()
	factory.debug_advance_time(DT)
	var after_eased := a.get_bullet_direction(0).angle() - before
	assert_ne(after_eased, 0.0, "past blend_in_sec the full turn applies")


func test_a_pooled_volley_eases_in_again() -> void:
	var f := BulletFlock2D.new()
	f.blend_in_sec = 0.5
	var d := H.make_flight_data([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 10))], 100.0, 1000.0)
	var first: BulletVolley2D = factory.spawn_volley(d)
	first.flock_join(f)
	for k in 60:
		factory.debug_advance_time(DT)
	factory.clear_active_bullets()
	await idle(1)
	var again: BulletVolley2D = factory.spawn_volley(d)
	assert_eq(again, first, "the pool handed the same volley back")
	again.flock_join(f)
	factory.debug_advance_time(DT)
	var eased := absf(again.get_bullet_direction(0).angle())
	var fresh: BulletVolley2D = factory.spawn_volley(H.make_flight_data([Transform2D(0.0, Vector2(0, 900)), Transform2D(0.0, Vector2(0, 910))], 100.0, 1000.0))
	var g := BulletFlock2D.new()
	g.blend_in_sec = 0.0
	fresh.flock_join(g)
	factory.debug_advance_time(DT)
	assert_lt(eased, absf(fresh.get_bullet_direction(0).angle()) * 0.05, "the reused volley's clock restarted: it eases in again")


func test_every_preset_flies_a_burst_calmly(preset: int = use_parameters([BulletFlock2D.PRESET_SWARM, BulletFlock2D.PRESET_FLY_TOGETHER, BulletFlock2D.PRESET_KEEP_DISTANCE, BulletFlock2D.PRESET_TIGHT_GROUP])) -> void:
	var f := BulletFlock2D.create_preset(preset)
	var probe := _burst(f, 64.0, 1.5)
	assert_lte(probe.max_turn_deg_per_sec, f.max_turn_rate_degrees * 1.001, "preset %d: within its cap: %s" % [preset, probe.summary()])
	assert_lt(probe.flips_per_bullet_sec(), 4.0, "preset %d: no jitter: %s" % [preset, probe.summary()])
	assert_eq(probe.snaps, 0, "preset %d: no snap" % preset)
	assert_lt(probe.max_facing_error_deg, 0.01, "preset %d: sprites on the heading" % preset)
	assert_eq(probe.nonfinite, 0, "finite")


## Three 24-bullet homing shots (scale 1..1.5) into one default flock, all
## chasing one point; the mean overlap fraction and nearest edge gap over the
## last 2.5 s (sampled every 15 ticks).
func _homing_swarm_spacing(f: BulletFlock2D, quad: float) -> Array:
	var probe := MotionProbe.new(factory, [])
	for shot in 3:
		var tr: Array = []
		seed(5 + shot)
		for k in 24:
			var a := TAU * k / 24
			tr.append(Transform2D(a, Vector2(cos(a), sin(a)) * 12.0).scaled_local(Vector2.ONE * randf_range(1.0, 1.5)))
		var d := H.make_flight_data(tr, 300.0, 1000.0)
		d.texture_size = Vector2(quad, quad)
		d.homing_smoothing = 5.0
		d.homing_take_control_of_texture_rotation = true
		var v: BulletVolley2D = factory.spawn_volley(d)
		v.flock_join(f, BulletFlock2D.SCOPE_SHARED)
		v.shared_homing_deque_push_back_global_position_target(Vector2(500, 150))
		probe.add_volley(v, 0.0, Vector2(quad, quad))
		probe.run(9)
	var overlap := 0.0
	var gap := 0.0
	for k in 10:
		probe.run(15)
		var g := probe.gaps()
		overlap += float(g["overlap_fraction"]) / 10.0
		gap += float(g["mean_nearest_gap"]) / 10.0
	var snaps := probe.steer_snaps
	factory.free_active_bullets()
	await idle(1)
	return [overlap, gap, snaps]


func test_a_homing_swarm_keeps_its_distance(quad: float = use_parameters([16.0, 64.0])) -> void:
	# Separation outranks homing: crowded bullets give back their homing turn
	# and steer away, so a swarm chasing ONE point keeps its spacing instead
	# of piling onto it (big bullets can only fit so many around a point).
	var swarm: Array = await _homing_swarm_spacing(BulletFlock2D.new(), quad)
	var no_separation := BulletFlock2D.new()
	no_separation.separation = 0.0
	var piled: Array = await _homing_swarm_spacing(no_separation, quad)
	var max_overlap := 0.2 if quad < 32.0 else 0.55
	var min_gap := 0.0 if quad < 32.0 else -20.0
	assert_lt(swarm[0], max_overlap, "%d px: few bullets overlap (%.2f; without separation %.2f)" % [quad, swarm[0], piled[0]])
	assert_gt(swarm[1], min_gap, "%d px: mean nearest edge gap %.1f px (without separation %.1f)" % [quad, swarm[1], piled[1]])
	assert_gt(piled[0] - swarm[0], 0.4, "%d px: the flock is what keeps them apart" % quad)
	assert_eq(swarm[2], 0, "%d px: no snap while keeping the distance" % quad)


func test_turn_inertia_eases_the_turn_rate() -> void:
	# The first tick of a turn with turn_smoothing_sec tau turns exactly
	# 1 - e^(-dt / tau) as far as an instant turn.
	var smooth := H.make_flock(1.0, 0.0, 0.0, 100.0, 40.0)
	smooth.turn_smoothing_sec = 0.1
	var instant := H.make_flock(1.0, 0.0, 0.0, 100.0, 40.0)
	var a: BulletVolley2D = factory.spawn_volley(H.make_flight_data([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 20))], 100.0))
	var b: BulletVolley2D = factory.spawn_volley(H.make_flight_data([Transform2D(0.0, Vector2(0, 900)), Transform2D(0.0, Vector2(0, 920))], 100.0))
	a.flock_join(smooth)
	b.flock_join(instant)
	factory.debug_advance_time(DT)
	var turned_smooth := a.get_bullet_direction(0).angle()
	var turned_instant := b.get_bullet_direction(0).angle()
	assert_ne(turned_instant, 0.0, "the instant flock turns at once")
	assert_almost_eq(turned_smooth / turned_instant, 1.0 - exp(-DT / 0.1), 1e-3, "eased by 1 - e^(-dt / tau)")
