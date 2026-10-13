extends BlastTest
## Every orbiting bullet circles ITS OWN front target on every tick. The
## per-tick orbit center comes from the deque that bullet steered by, never
## from another bullet's target, including zero-delta ticks (where homing
## steering is skipped): a zero step moves nothing, and each bullet stays on
## its own ring.
##
## Re-running all_bullets_enable_orbiting_linear updates EVERY passed
## parameter on already-orbiting bullets (not just the radius); OrbitRandom
## keeps each bullet's rolled direction (re-rolling mid-flight is wrong).

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


func test_rearm_updates_every_parameter() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_still_data(2))
	v.all_bullets_enable_orbiting_linear(40.0, 10.0, BulletVolley2D.OrbitRight, BulletVolley2D.FaceTarget)
	assert_eq(v.bullet_get_orbiting_direction(0), BulletVolley2D.OrbitRight, "armed OrbitRight")
	v.all_bullets_enable_orbiting_linear(50.0, 5.0, BulletVolley2D.OrbitLeft, BulletVolley2D.FaceOppositeTarget, 0, 1, BulletVolley2D.FollowDeadzone, 12.0, 0, false)
	assert_eq(v.bullet_get_orbiting_direction(0), BulletVolley2D.OrbitLeft, "direction re-armed")
	assert_eq(v.bullet_get_orbiting_direction(1), BulletVolley2D.OrbitLeft, "second bullet re-armed too")
	assert_eq(v.bullet_get_orbiting_texture_rotation(0), BulletVolley2D.FaceOppositeTarget, "texture rotation re-armed")
	assert_almost_eq(v.bullet_get_orbiting_radius(0), 50.0, 0.01, "radius updated")
	assert_almost_eq(v.bullet_get_orbiting_radius(1), 55.0, 0.01, "linear fan kept")


func test_orbit_random_never_rerolls() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_still_data(2))
	v.all_bullets_enable_orbiting_linear(40.0, 0.0, BulletVolley2D.OrbitRandom, BulletVolley2D.FaceTarget)
	var d0: int = v.bullet_get_orbiting_direction(0)
	var d1: int = v.bullet_get_orbiting_direction(1)
	assert_true(d0 == BulletVolley2D.OrbitLeft or d0 == BulletVolley2D.OrbitRight, "rolled a concrete direction")
	v.all_bullets_enable_orbiting_linear(60.0, 0.0, BulletVolley2D.OrbitRandom, BulletVolley2D.FaceTarget)
	assert_eq(v.bullet_get_orbiting_direction(0), d0, "no re-roll on re-arm")
	assert_eq(v.bullet_get_orbiting_direction(1), d1, "second bullet stable too")
	assert_almost_eq(v.bullet_get_orbiting_radius(0), 60.0, 0.01, "radius still updates under Random")
