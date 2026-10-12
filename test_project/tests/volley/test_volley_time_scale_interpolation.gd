extends BlastTest
## Physics interpolation under a time scale (factory, spawner, scale changes):
## what is drawn never runs ahead of the simulation, never steps backwards
## (no jitter on any scale), trails the simulation by at most two ticks of
## the scaled step, and settles exactly on the bullet once it stops (scale
## 0). The headless runner has no sub-tick fraction (always 0), so these are
## the invariants that hold for any fraction.

const SPEED := 600.0
const FPS := 60.0


## X of instance 0 in the raw multimesh buffer ([x.x, y.x, 0, o.x, ...]).
func _drawn_x(v: BulletVolley2D) -> float:
	var buf: PackedFloat32Array = v.multimesh.buffer
	return buf[3]


func _sim_x(v: BulletVolley2D) -> float:
	return v.get_bullet_global_transform(0).origin.x


## Runs `frames` frames and asserts the invariants against `step` px per tick.
func _walk(v: BulletVolley2D, frames: int, step: float, label: String) -> void:
	var last := _drawn_x(v)
	for i in frames:
		await idle(1)
		var drawn := _drawn_x(v)
		var sim := _sim_x(v)
		assert_lte(drawn, sim + 0.01, "%s frame %d: never ahead of the simulation" % [label, i])
		assert_gte(drawn, last - 0.01, "%s frame %d: never steps backwards (%.3f -> %.3f)" % [label, i, last, drawn])
		assert_lte(sim - drawn, 2.0 * step + 0.01, "%s frame %d: at most two ticks behind" % [label, i])
		last = drawn


func _flyer() -> BulletVolley2D:
	factory.use_physics_interpolation = true
	return factory.spawn_volley(H.make_volley_data(1, SPEED, 60.0))


func test_factory_scales_keep_the_drawn_pose_smooth() -> void:
	var v := _flyer()
	await idle(6) # warm-up: the drawn pose starts at the spawn pose
	var previous := 1.0
	for scale in [1.0, 0.5, 0.25, 4.0, 1.0]:
		factory.time_scale = scale
		# Right after a change the drawn pose still trails in the old steps.
		await _walk(v, 12, SPEED * maxf(scale, previous) / FPS, "factory scale %s" % scale)
		previous = scale


func test_scale_zero_settles_exactly_and_stays_put() -> void:
	var v := _flyer()
	await idle(6)
	factory.time_scale = 0.0
	await idle(4)
	var sim := _sim_x(v)
	assert_almost_eq(_drawn_x(v), sim, 0.001, "settled on the frozen bullet")
	await idle(30)
	assert_almost_eq(_drawn_x(v), sim, 0.001, "and it never drifts")
	assert_eq(_sim_x(v), sim, "the bullet itself did not move")
	factory.time_scale = 1.0
	await idle(3)
	assert_gt(_sim_x(v), sim, "it moves again after the freeze")


func test_spawner_scale_keeps_the_drawn_pose_smooth() -> void:
	factory.use_physics_interpolation = true
	var sp := make_spawner(H.make_volley_data(1, SPEED, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot")
	var v: BulletVolley2D = get_signal_parameters(sp, "volley_fired", 0)[0]
	await idle(6)
	var previous := 1.0
	for scale in [0.5, 0.0, 2.0]:
		sp.time_scale = scale
		await _walk(v, 10, SPEED * maxf(scale, previous) / FPS, "spawner scale %s" % scale)
		previous = scale


func test_interpolation_toggle_mid_slow_motion_stays_consistent() -> void:
	var v := _flyer()
	factory.time_scale = 0.25
	await idle(5)
	factory.use_physics_interpolation = false
	await idle(2)
	assert_almost_eq(_drawn_x(v), _sim_x(v), 0.5, "interpolation off: drawn at the simulated pose")
	factory.use_physics_interpolation = true
	await idle(1)
	await _walk(v, 8, SPEED * 0.25 / FPS, "after turning it back on")
