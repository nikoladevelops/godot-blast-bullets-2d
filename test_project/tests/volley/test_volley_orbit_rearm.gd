extends BlastTest
## Re-running all_bullets_enable_orbiting_linear updates EVERY passed
## parameter on already-orbiting bullets (not just the radius); OrbitRandom
## keeps each bullet's rolled direction (re-rolling mid-flight is wrong).


func test_rearm_updates_every_parameter() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_still_data(2))
	v.all_bullets_enable_orbiting_linear(40.0, 10.0, DirectionalBullets2D.OrbitRight, DirectionalBullets2D.FaceTarget)
	assert_eq(v.bullet_get_orbiting_direction(0), DirectionalBullets2D.OrbitRight, "armed OrbitRight")
	v.all_bullets_enable_orbiting_linear(50.0, 5.0, DirectionalBullets2D.OrbitLeft, DirectionalBullets2D.FaceOppositeTarget, 0, 1, DirectionalBullets2D.FollowDeadzone, 12.0, 0, false)
	assert_eq(v.bullet_get_orbiting_direction(0), DirectionalBullets2D.OrbitLeft, "direction re-armed")
	assert_eq(v.bullet_get_orbiting_direction(1), DirectionalBullets2D.OrbitLeft, "second bullet re-armed too")
	assert_eq(v.bullet_get_orbiting_texture_rotation(0), DirectionalBullets2D.FaceOppositeTarget, "texture rotation re-armed")
	assert_almost_eq(v.bullet_get_orbiting_radius(0), 50.0, 0.01, "radius updated")
	assert_almost_eq(v.bullet_get_orbiting_radius(1), 55.0, 0.01, "linear fan kept")


func test_orbit_random_never_rerolls() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_still_data(2))
	v.all_bullets_enable_orbiting_linear(40.0, 0.0, DirectionalBullets2D.OrbitRandom, DirectionalBullets2D.FaceTarget)
	var d0: int = v.bullet_get_orbiting_direction(0)
	var d1: int = v.bullet_get_orbiting_direction(1)
	assert_true(d0 == DirectionalBullets2D.OrbitLeft or d0 == DirectionalBullets2D.OrbitRight, "rolled a concrete direction")
	v.all_bullets_enable_orbiting_linear(60.0, 0.0, DirectionalBullets2D.OrbitRandom, DirectionalBullets2D.FaceTarget)
	assert_eq(v.bullet_get_orbiting_direction(0), d0, "no re-roll on re-arm")
	assert_eq(v.bullet_get_orbiting_direction(1), d1, "second bullet stable too")
	assert_almost_eq(v.bullet_get_orbiting_radius(0), 60.0, 0.01, "radius still updates under Random")
