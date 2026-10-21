extends BlastTest
## Spawner randomization (group "Randomization", spin start phase, timing
## ranges): per-bullet scale / rotation / position / speed rolls applied at
## shot time on top of the pattern. Contracts: everything off is a
## bit-identical shot that draws nothing from Godot's global RNG; min/max
## pairs are order independent (normalized at use); bounds always hold;
## a seed makes shots reproducible and each channel independent; the preview,
## the bake cache and collect_spawn_transforms() never see the rolls; nothing
## leaks into pooled volleys.

const N := 200

var fired: Array = []


func _on_fired(volley: BulletVolley2D, _count: int) -> void:
	fired.append(volley)


func before_each() -> void:
	await super()
	fired.clear()


func _spawner(n := N, speed := 100.0) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(n, speed, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, n)
	sp.volley_fired.connect(_on_fired)
	return sp


func _shoot(sp: BulletSpawner2D) -> BulletVolley2D:
	fired.clear()
	assert_true(sp.shoot_once(), "shot fires")
	return fired.back()


func _poses(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		out.append(v.get_bullet_transform(i))
	return out


func _min_max(values: Array) -> Vector2:
	var lo := INF
	var hi := -INF
	for x in values:
		lo = minf(lo, x)
		hi = maxf(hi, x)
	return Vector2(lo, hi)


## Per-bullet scale ratios (shot basis length / reference basis length).
func _ratios(shot: Array, ref: Array, axis: int) -> Array:
	var out: Array = []
	for i in shot.size():
		var s: Transform2D = shot[i]
		var r: Transform2D = ref[i]
		out.append((s.x.length() / r.x.length()) if axis == 0 else (s.y.length() / r.y.length()))
	return out


# --- off = identical, and free ---------------------------------------------------

func test_everything_off_is_the_plain_shot_and_draws_no_global_random_numbers() -> void:
	var sp := _spawner()
	var ref: Array = sp.collect_spawn_transforms()
	seed(7)
	var expected := randf()
	seed(7)
	var v := _shoot(sp)
	assert_eq(randf(), expected, "no roll was drawn from the global RNG")
	var shot := _poses(v)
	for i in N:
		assert_true(shot[i].is_equal_approx(ref[i]), "bullet %d is the pattern's pose" % i)
	var again := _poses(_shoot(sp))
	assert_eq(again, shot, "and a second plain shot is exactly the same")


func test_defaults() -> void:
	var sp := make_spawner()
	assert_eq(sp.bullet_scale_mode, 0, "Off")
	assert_eq([sp.bullet_scale_min, sp.bullet_scale_max], [1.0, 1.0], "scale range 1..1")
	assert_eq([sp.bullet_scale_min_xy, sp.bullet_scale_max_xy], [Vector2.ONE, Vector2.ONE], "per-axis range 1..1")
	assert_eq([sp.bullet_rotation_min_degrees, sp.bullet_rotation_max_degrees], [0.0, 0.0], "no rotation jitter")
	assert_eq([sp.bullet_position_jitter_min, sp.bullet_position_jitter_max], [0.0, 0.0], "no position jitter")
	assert_eq([sp.bullet_speed_scale_min, sp.bullet_speed_scale_max], [1.0, 1.0], "speed x1")
	assert_eq(sp.bullet_random_seed, 0, "unseeded")
	assert_false(sp.spin_random_start_phase, "spin starts at 0")
	assert_eq([sp.shoot_interval_max_sec, sp.burst_interval_max_sec, sp.telegraph_max_sec], [0.0, 0.0, 0.0], "timing ranges off")


# --- scale -----------------------------------------------------------------------

func test_uniform_scale_stays_in_range_and_varies() -> void:
	var sp := _spawner()
	var ref: Array = sp.collect_spawn_transforms()
	sp.bullet_scale_mode = 1
	sp.bullet_scale_min = 0.5
	sp.bullet_scale_max = 2.0
	var v := _shoot(sp)
	var shot := _poses(v)
	var rx := _ratios(shot, ref, 0)
	var ry := _ratios(shot, ref, 1)
	var mm := _min_max(rx)
	assert_gte(mm.x, 0.5 - 0.001, "never below the minimum")
	assert_lte(mm.y, 2.0 + 0.001, "never above the maximum")
	assert_gt(mm.y - mm.x, 0.5, "and it really varies across 200 bullets")
	for i in N:
		assert_almost_eq(rx[i], ry[i], 0.001, "uniform: x and y scale together")
	for i in N:
		assert_true(shot[i].origin.is_equal_approx(ref[i].origin), "scale never moves the bullet")


func test_equal_min_and_max_is_exact() -> void:
	var sp := _spawner(10)
	var ref: Array = sp.collect_spawn_transforms()
	sp.bullet_scale_mode = 1
	sp.bullet_scale_min = 1.5
	sp.bullet_scale_max = 1.5
	for r in _ratios(_poses(_shoot(sp)), ref, 0):
		assert_almost_eq(r, 1.5, 0.0001, "exactly 1.5")


func test_inverted_range_is_normalized_at_use_and_assignment_order_is_free() -> void:
	var sp := _spawner(100)
	var ref: Array = sp.collect_spawn_transforms()
	sp.bullet_scale_mode = 1
	sp.bullet_scale_min = 3.0 # above the current max of 1.0: accepted, no error
	assert_eq(sp.bullet_scale_min, 3.0, "no pair rejection")
	sp.bullet_scale_max = 1.0
	var mm := _min_max(_ratios(_poses(_shoot(sp)), ref, 0))
	assert_gte(mm.x, 1.0 - 0.001, "inverted pair behaves as 1..3 (low end)")
	assert_lte(mm.y, 3.0 + 0.001, "(high end)")
	assert_gt(mm.y - mm.x, 0.5, "and varies")


func test_per_axis_scale_rolls_each_axis_in_its_own_range() -> void:
	var sp := _spawner()
	var ref: Array = sp.collect_spawn_transforms()
	sp.bullet_scale_mode = 2
	sp.bullet_scale_min_xy = Vector2(0.5, 2.0)
	sp.bullet_scale_max_xy = Vector2(1.0, 3.0)
	var shot := _poses(_shoot(sp))
	var mx := _min_max(_ratios(shot, ref, 0))
	var my := _min_max(_ratios(shot, ref, 1))
	assert_gte(mx.x, 0.5 - 0.001, "x low")
	assert_lte(mx.y, 1.0 + 0.001, "x high")
	assert_gte(my.x, 2.0 - 0.001, "y low")
	assert_lte(my.y, 3.0 + 0.001, "y high")
	var differ := 0
	var rx := _ratios(shot, ref, 0)
	var ry := _ratios(shot, ref, 1)
	for i in N:
		if absf(rx[i] - ry[i]) > 0.01:
			differ += 1
	assert_gt(differ, N / 2, "the axes are independent")


func test_mode_off_ignores_a_configured_range() -> void:
	var sp := _spawner(20)
	var plain := _poses(_shoot(sp))
	sp.bullet_scale_min = 0.1
	sp.bullet_scale_max = 9.0
	assert_eq(_poses(_shoot(sp)), plain, "mode Off: the range is inert")


func test_a_scaled_bullet_collides_with_its_scaled_shape() -> void:
	# Circle radius 6 at x=80 misses a wall whose face is at x=90; scaled x4 it reaches it.
	make_wall(Vector2(100, 0))
	var data := H.make_volley_data(1, 0.0, 30.0)
	data.set_collision_mask_from_array([3])
	data.collision_shape = H.make_circle_shape(6.0)
	data.bullet_max_collision_count = 1
	data.monitorable = true
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.helper_ring_radius = 80.0
	sp.volley_fired.connect(_on_fired)
	watch_signals(sp)
	var plain := _shoot(sp)
	await physics(4)
	assert_signal_not_emitted(sp, "body_entered", "an unscaled bullet misses the wall")
	plain.clear_all_bullets()
	sp.bullet_scale_mode = 1
	sp.bullet_scale_min = 4.0
	sp.bullet_scale_max = 4.0
	_shoot(sp)
	for i in 10:
		await physics(1)
		if get_signal_emit_count(sp, "body_entered") > 0:
			break
	assert_signal_emit_count(sp, "body_entered", 1, "the 4x bullet's bigger shape reaches the wall")


# --- rotation and position ----------------------------------------------------------

func test_rotation_jitter_stays_in_range() -> void:
	var sp := _spawner()
	var ref: Array = sp.collect_spawn_transforms()
	sp.bullet_rotation_min_degrees = -10.0
	sp.bullet_rotation_max_degrees = 30.0
	var shot := _poses(_shoot(sp))
	var deltas: Array = []
	for i in N:
		deltas.append(rad_to_deg(wrapf(shot[i].get_rotation() - ref[i].get_rotation(), -PI, PI)))
		assert_true(shot[i].origin.is_equal_approx(ref[i].origin), "rotation never moves the bullet")
	var mm := _min_max(deltas)
	assert_gte(mm.x, -10.0 - 0.01, "low end")
	assert_lte(mm.y, 30.0 + 0.01, "high end")
	assert_gt(mm.y - mm.x, 20.0, "varies")


func test_position_jitter_is_an_annulus() -> void:
	var sp := _spawner()
	var ref: Array = sp.collect_spawn_transforms()
	sp.bullet_position_jitter_min = 5.0
	sp.bullet_position_jitter_max = 20.0
	var shot := _poses(_shoot(sp))
	var radii: Array = []
	for i in N:
		radii.append(shot[i].origin.distance_to(ref[i].origin))
		assert_true(shot[i].x.is_equal_approx(ref[i].x) and shot[i].y.is_equal_approx(ref[i].y), "the basis is untouched")
	var mm := _min_max(radii)
	assert_gte(mm.x, 5.0 - 0.01, "never closer than the inner radius")
	assert_lte(mm.y, 20.0 + 0.01, "never farther than the outer radius")
	assert_gt(mm.y - mm.x, 5.0, "varies")


# --- speed -------------------------------------------------------------------------

func test_speed_multiplier_scales_speed_max_speed_and_acceleration() -> void:
	var data := H.make_volley_data(50, 100.0, 30.0)
	for s in data.all_bullet_speed_data:
		s.max_speed = 150.0
		s.acceleration = 1000.0
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 50)
	sp.volley_fired.connect(_on_fired)
	sp.bullet_speed_scale_min = 2.0
	sp.bullet_speed_scale_max = 2.0
	var v := _shoot(sp)
	for i in 50:
		assert_almost_eq(v.get_bullet_velocity(i).length(), 200.0, 0.01, "100 px/s x2 at spawn")
	await idle(60)
	for i in 50:
		assert_almost_eq(v.get_bullet_velocity(i).length(), 300.0, 0.5, "and it tops out at 150 x2")


func test_speed_range_stays_in_bounds() -> void:
	var sp := _spawner(N, 100.0)
	sp.bullet_speed_scale_min = 0.5
	sp.bullet_speed_scale_max = 2.0
	var v := _shoot(sp)
	var speeds: Array = []
	for i in N:
		speeds.append(v.get_bullet_velocity(i).length())
	var mm := _min_max(speeds)
	assert_gte(mm.x, 50.0 - 0.01, "low")
	assert_lte(mm.y, 200.0 + 0.01, "high")
	assert_gt(mm.y - mm.x, 40.0, "varies")


# --- seeds and channels ---------------------------------------------------------------

func _configure_all(sp: BulletSpawner2D, seed_value: int) -> void:
	sp.bullet_random_seed = seed_value
	sp.bullet_scale_mode = 1
	sp.bullet_scale_min = 0.5
	sp.bullet_scale_max = 2.0
	sp.bullet_rotation_min_degrees = -20.0
	sp.bullet_rotation_max_degrees = 20.0
	sp.bullet_position_jitter_max = 15.0


func test_a_seed_reproduces_shots_and_other_seeds_and_shots_differ() -> void:
	var a := _spawner(40)
	var b := _spawner(40)
	var c := _spawner(40)
	_configure_all(a, 4242)
	_configure_all(b, 4242)
	_configure_all(c, 777)
	var pa := _poses(_shoot(a))
	var pb := _poses(_shoot(b))
	var pc := _poses(_shoot(c))
	assert_eq(pa, pb, "same seed, same first shot")
	assert_ne(pa, pc, "another seed rolls differently")
	var pa2 := _poses(_shoot(a))
	assert_ne(pa, pa2, "the next shot of the same spawner rolls anew")
	var pb2 := _poses(_shoot(b))
	assert_eq(pa2, pb2, "but reproducibly")


func test_each_knob_rolls_its_own_numbers() -> void:
	# With one seed every knob must draw from its own stream: if two knobs
	# shared roll numbers, a big scale would always come with a big rotation.
	var sp := _spawner(N)
	var ref: Array = sp.collect_spawn_transforms()
	sp.bullet_random_seed = 99
	sp.bullet_scale_mode = 2
	sp.bullet_scale_min_xy = Vector2(1.0, 1.0)
	sp.bullet_scale_max_xy = Vector2(2.0, 2.0)
	sp.bullet_rotation_min_degrees = 0.0
	sp.bullet_rotation_max_degrees = 40.0
	sp.bullet_position_jitter_min = 0.0
	sp.bullet_position_jitter_max = 10.0
	sp.bullet_speed_scale_min = 1.0
	sp.bullet_speed_scale_max = 2.0
	var v := _shoot(sp)
	var shot := _poses(v)
	var scale_x := _ratios(shot, ref, 0)
	var scale_y := _ratios(shot, ref, 1)
	var same := { "x~y": 0, "x~rotation": 0, "x~speed": 0, "rotation~speed": 0 }
	for i in N:
		var tx: float = scale_x[i] - 1.0
		var ty: float = scale_y[i] - 1.0
		var tr: float = rad_to_deg(wrapf(shot[i].get_rotation() - ref[i].get_rotation(), -PI, PI)) / 40.0
		var ts: float = v.get_bullet_velocity(i).length() / 100.0 - 1.0
		if absf(tx - ty) < 0.001:
			same["x~y"] += 1
		if absf(tx - tr) < 0.001:
			same["x~rotation"] += 1
		if absf(tx - ts) < 0.001:
			same["x~speed"] += 1
		if absf(tr - ts) < 0.001:
			same["rotation~speed"] += 1
	for pair in same:
		assert_lt(int(same[pair]), 5, "%s: rolled together for %d of %d bullets" % [pair, same[pair], N])


func test_unseeded_rolls_use_the_global_rng() -> void:
	var sp := _spawner(30)
	_configure_all(sp, 0)
	seed(5)
	var a := _poses(_shoot(sp))
	seed(5)
	var b := _poses(_shoot(sp))
	assert_eq(a, b, "seed(5) in the game reproduces an unseeded spawner's shot")
	seed(6)
	assert_ne(a, _poses(_shoot(sp)), "another global seed differs")


# --- isolation -------------------------------------------------------------------------

func test_preview_cache_and_script_collect_never_see_the_rolls() -> void:
	var sp := _spawner(30)
	var before: Array = sp.collect_spawn_transforms()
	var stats_before: Dictionary = sp.debug_get_preview_stats()
	_configure_all(sp, 5)
	sp.bullet_speed_scale_max = 3.0
	assert_eq(sp.collect_spawn_transforms(), before, "collect_spawn_transforms() is the unrandomized pattern")
	await idle(3)
	assert_eq(int(sp.debug_get_preview_stats().get("rebuilds", 0)), int(stats_before.get("rebuilds", 0)), "no preview rebuild: these are not pattern knobs")
	_shoot(sp)
	assert_eq(sp.collect_spawn_transforms(), before, "and shooting does not change it")


func test_nothing_leaks_into_a_pooled_volley() -> void:
	var sp := _spawner(30, 100.0)
	var ref: Array = sp.collect_spawn_transforms()
	_configure_all(sp, 0)
	sp.bullet_speed_scale_min = 3.0
	sp.bullet_speed_scale_max = 3.0
	var v := _shoot(sp)
	await idle(2)
	v.clear_all_bullets()
	await idle(1)
	sp.bullet_scale_mode = 0
	sp.bullet_rotation_min_degrees = 0.0
	sp.bullet_rotation_max_degrees = 0.0
	sp.bullet_position_jitter_max = 0.0
	sp.bullet_speed_scale_min = 1.0
	sp.bullet_speed_scale_max = 1.0
	factory.debug_reset_pool_stats()
	var v2 := _shoot(sp)
	assert_eq(int(factory.debug_get_pool_hit_stats()["hits"]), 1, "the volley came from the pool")
	var plain_poses := _poses(v2)
	for i in 30:
		assert_true(plain_poses[i].is_equal_approx(ref[i]), "plain pose %d again" % i)
	for i in 30:
		assert_almost_eq(v2.get_bullet_velocity(i).length(), 100.0, 0.01, "plain speed again")


func test_knobs_survive_a_scene_round_trip() -> void:
	var sp := _spawner(5)
	_configure_all(sp, 31)
	sp.bullet_scale_mode = 2
	sp.bullet_scale_min_xy = Vector2(0.5, 0.25)
	sp.bullet_scale_max_xy = Vector2(2.0, 4.0)
	sp.bullet_speed_scale_min = 0.5
	sp.bullet_speed_scale_max = 1.5
	sp.spin_random_start_phase = true
	sp.shoot_interval_max_sec = 2.5
	sp.burst_interval_max_sec = 0.75
	sp.telegraph_max_sec = 0.9
	var holder := Node2D.new()
	holder.add_child(sp.duplicate())
	var scene := PackedScene.new()
	holder.get_child(0).owner = holder
	assert_eq(scene.pack(holder), OK, "packs")
	var copy: Node = scene.instantiate()
	var loaded: BulletSpawner2D = copy.get_child(0)
	for prop in ["bullet_random_seed", "bullet_scale_mode", "bullet_scale_min", "bullet_scale_max", "bullet_scale_min_xy", "bullet_scale_max_xy", "bullet_rotation_min_degrees", "bullet_rotation_max_degrees", "bullet_position_jitter_min", "bullet_position_jitter_max", "bullet_speed_scale_min", "bullet_speed_scale_max", "spin_random_start_phase", "shoot_interval_max_sec", "burst_interval_max_sec", "telegraph_max_sec"]:
		assert_eq(loaded.get(prop), sp.get(prop), prop + " survives")
	copy.free()
	holder.free()


# --- setters ---------------------------------------------------------------------------

func test_setters_reject_bad_values_and_keep_the_old_one() -> void:
	var sp := make_spawner()
	var cases := {
		"bullet_scale_min": [NAN, INF, 0.0, -1.0],
		"bullet_scale_max": [NAN, INF, 0.0, -1.0],
		"bullet_rotation_min_degrees": [NAN, INF],
		"bullet_rotation_max_degrees": [NAN, -INF],
		"bullet_position_jitter_min": [NAN, INF, -1.0],
		"bullet_position_jitter_max": [NAN, INF, -1.0],
		"bullet_speed_scale_min": [NAN, INF, -1.0],
		"bullet_speed_scale_max": [NAN, INF, -1.0],
		"shoot_interval_max_sec": [NAN, INF, -1.0],
		"burst_interval_max_sec": [NAN, INF, -1.0],
		"telegraph_max_sec": [NAN, INF, -1.0],
	}
	for prop in cases:
		var old = sp.get(prop)
		for bad in cases[prop]:
			sp.set(prop, bad)
			expect_errors_containing("BulletSpawner2D: " + prop + " must be", 1, prop + " rejects " + str(bad))
		assert_eq(sp.get(prop), old, prop + " keeps the old value")
	sp.bullet_scale_min_xy = Vector2(NAN, 1)
	expect_errors_containing("BulletSpawner2D: bullet_scale_min_xy must be", 1, "NaN component")
	sp.bullet_scale_max_xy = Vector2(1, 0)
	expect_errors_containing("BulletSpawner2D: bullet_scale_max_xy must be", 1, "zero component")
	assert_eq(sp.bullet_scale_min_xy, Vector2.ONE, "old per-axis value kept")
	sp.bullet_scale_mode = 7
	expect_errors_containing("BulletSpawner2D: bullet_scale_mode must be", 1, "unknown mode")
	assert_eq(sp.bullet_scale_mode, 0, "old mode kept")
	sp.bullet_random_seed = -5
	assert_eq(sp.bullet_random_seed, -5, "any int is a valid seed")


# --- spin start phase ---------------------------------------------------------------------

func test_spin_start_phase_is_random_but_seedable() -> void:
	var sp := make_spawner()
	sp.spin_enabled = true
	sp.reset_spin_angle()
	assert_eq(sp.get_spin_angle_deg(), 0.0, "flag off: starts at 0")
	sp.spin_random_start_phase = true
	sp.bullet_random_seed = 1234
	sp.reset_spin_angle()
	var first := sp.get_spin_angle_deg()
	assert_between(first, 0.0, 360.0, "a continuous spin starts anywhere on the circle")
	assert_ne(first, 0.0, "and not at 0 for this seed")
	sp.reset_spin_angle()
	assert_eq(sp.get_spin_angle_deg(), first, "a seeded start repeats")
	sp.bullet_random_seed = 999
	sp.reset_spin_angle()
	assert_ne(sp.get_spin_angle_deg(), first, "another seed starts elsewhere")


func test_oscillating_spin_starts_at_a_random_phase_inside_its_amplitude() -> void:
	var sp := make_spawner()
	sp.spin_enabled = true
	sp.spin_mode = 1
	sp.spin_amplitude_deg = 40.0
	sp.spin_frequency_hz = 1.0
	sp.spin_random_start_phase = true
	sp.bullet_random_seed = 77
	sp.reset_spin_angle()
	await idle(2)
	assert_lte(absf(sp.get_spin_angle_deg()), 40.0 + 0.01, "inside the amplitude")


# --- timing ranges ----------------------------------------------------------------------------

func _shot_frames(sp: BulletSpawner2D, frames: int) -> Array:
	var out: Array = []
	var tick := [0]
	sp.volley_fired.connect(func(_v, _c): out.append(tick[0]))
	for i in frames:
		await idle(1)
		tick[0] += 1
	return out


func test_shoot_interval_range_draws_each_wait_between_base_and_max() -> void:
	var sp := _spawner(1, 0.0)
	sp.shoot_interval_sec = 0.1
	sp.shoot_interval_max_sec = 0.4
	sp.reload_jitter_seed = 3
	sp.set_shooting_enabled(true)
	var at := await _shot_frames(sp, 600)
	assert_gt(at.size(), 15, "a steady stream of shots")
	var gaps: Array = []
	for i in range(1, at.size()):
		gaps.append(at[i] - at[i - 1])
	var mm := _min_max(gaps)
	assert_gte(mm.x, 6 - 1, "never faster than the base interval (6 frames)")
	assert_lte(mm.y, 24 + 1, "never slower than the maximum (24 frames)")
	assert_gt(mm.y - mm.x, 6, "and the waits really vary")


func test_telegraph_range_draws_each_warning_between_base_and_max() -> void:
	var sp := _spawner(1, 0.0)
	sp.shoot_interval_sec = 0.05
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.1
	sp.telegraph_max_sec = 0.5
	sp.reload_jitter_seed = 11
	sp.set_shooting_enabled(true)
	var at := await _shot_frames(sp, 900)
	assert_gt(at.size(), 8, "several shots")
	var gaps: Array = []
	for i in range(1, at.size()):
		gaps.append(at[i] - at[i - 1])
	var mm := _min_max(gaps)
	assert_gt(mm.y - mm.x, 6, "the warning lengths vary")
	assert_lte(mm.y, 30 + 6, "no warning past the maximum (plus the wait)")


## The timing maxima feed a float random range: past FLT_MAX the next wait
## became INF and the spawner never fired again. They refuse such a value
## (the other randomization ranges are handled at the shot and stay open).
func test_timing_maxima_refuse_values_past_float_range() -> void:
	var sp := make_spawner()
	for prop in ["shoot_interval_max_sec", "burst_interval_max_sec", "telegraph_max_sec"]:
		var before: float = sp.get(prop)
		sp.set(prop, 1e39)
		expect_error_sequence(["BulletSpawner2D: %s must be finite and >= 0 (0 = off), keeping the old value." % prop])
		assert_eq(sp.get(prop), before, "%s kept" % prop)
		sp.set(prop, 1e30)
		assert_eq(sp.get(prop), 1e30, "%s: a large float-range value is fine" % prop)


## reload_jitter_seed 0 is non-deterministic even after a seeded run: two
## spawners with the same seeded history then roll their timing ranges
## independently (no shared stream continues from the last seeded roll).
func test_jitter_seed_zero_randomizes_again_after_a_seeded_run() -> void:
	var gaps := [[], []]
	var last := [0, 0]
	var frame := [0]
	var sps: Array = []
	for k in 2:
		var sp := make_spawner(H.make_volley_data(1, 50.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
		sp.shoot_interval_sec = 0.02
		sp.shoot_interval_max_sec = 0.4
		sp.volley_fired.connect(func(_v, _n) -> void:
			gaps[k].append(frame[0] - last[k])
			last[k] = frame[0])
		sps.append(sp)
	var run := func(frames: int) -> void:
		for i in frames:
			await idle(1)
			frame[0] += 1
	for sp in sps: # seed 0 first: each rolls its own stream
		sp.set_shooting_enabled(true)
	await run.call(30)
	for k in 2: # then the same seeded history (shot counts aligned)
		sps[k].reload_jitter_seed = 5
		sps[k].reset_shooting()
		last[k] = frame[0]
		gaps[k] = []
	await run.call(60)
	assert_eq(gaps[0], gaps[1], "the same seed rolls the same timing")
	for k in 2:
		sps[k].reload_jitter_seed = 0
		gaps[k] = []
	await run.call(200)
	assert_gt(gaps[0].size(), 6, "both kept firing")
	assert_ne(gaps[0].slice(0, 6), gaps[1].slice(0, 6), "seed 0 rolls independently")


func test_bullet_scale_mode_constants_are_bound() -> void:
	for c in ["BULLET_SCALE_OFF", "BULLET_SCALE_UNIFORM", "BULLET_SCALE_PER_AXIS"]:
		assert_true(ClassDB.class_has_integer_constant("BulletSpawner2D", c), c + " is usable from scripts (the README uses it)")
