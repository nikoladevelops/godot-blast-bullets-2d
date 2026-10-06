extends BlastTest
## Every orbiting bullet circles ITS OWN front target on every tick. The
## per-tick orbit center comes from the deque that bullet steered by, never
## from another bullet's target, including zero-delta ticks (where homing
## steering is skipped): a zero step moves nothing, and each bullet stays on
## its own ring.

const RADIUS := 40.0


func _orbit_data() -> BulletVolleyData2D:
	var d := H.make_volley_data(2, 300.0, 30.0)
	d.transforms = [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 30))]
	d.homing_smoothing = 30.0
	d.homing_take_control_of_texture_rotation = true
	return d


func _locked_volley() -> BulletVolley2D:
	var v: BulletVolley2D = factory.spawn_volley(_orbit_data())
	v.bullet_homing_push_back_global_position_target(0, Vector2(250, 0))
	v.bullet_homing_push_back_global_position_target(1, Vector2(-250, 0))
	for b in 2:
		v.bullet_enable_orbiting(b, RADIUS, BulletVolley2D.OrbitRight, BulletVolley2D.FaceTarget)
	for i in 240: # ~4 s of flight: both bullets reach their ring and lock
		factory.debug_advance_time(1.0 / 60.0)
		if v.bullet_is_orbiting_locked(0) and v.bullet_is_orbiting_locked(1):
			break
	assert_true(v.bullet_is_orbiting_locked(0) and v.bullet_is_orbiting_locked(1), "both bullets locked onto their rings")
	return v


func test_a_zero_delta_tick_moves_no_orbiting_bullet() -> void:
	var v := _locked_volley()
	await idle(1)
	var before := [v.get_bullet_transform(0).origin, v.get_bullet_transform(1).origin]
	assert_true(factory.debug_advance_time(0.0), "a zero step runs")
	for b in 2:
		assert_almost_eq(v.get_bullet_transform(b).origin, before[b], Vector2(0.01, 0.01), "bullet %d did not move on a zero step" % b)


func test_each_bullet_stays_on_its_own_ring_across_zero_and_real_ticks() -> void:
	var v := _locked_volley()
	await idle(1)
	var centers := [Vector2(250, 0), Vector2(-250, 0)]
	for step in [0.0, 1.0 / 60.0, 0.0, 1.0 / 60.0]:
		factory.debug_advance_time(step)
		for b in 2:
			var d: float = v.get_bullet_transform(b).origin.distance_to(centers[b])
			assert_almost_eq(d, RADIUS, 1.0, "bullet %d rides its own ring (step %s)" % [b, step])
