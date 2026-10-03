extends BlastTest
## Orbiting: rings, modes, lock policies, mixed with homing. Enable without a
## target arms safely, per-bullet + all_bullets + linear shells, setters,
## rigid follow, disable clears, freed-target lock policies, spawner arming.

var target: Node2D


func before_each() -> void:
	await super()
	target = add(Node2D.new())
	target.position = Vector2(400, 0)
	await idle(1)


func _homing_volley(n: int, target_node: Node2D) -> DirectionalBullets2D:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(n, 250.0, 30.0))
	v.set_homing_smoothing(6.0)
	v.set_homing_take_control_of_texture_rotation(true)
	if n == 1:
		v.bullet_homing_push_back_node2d_target(0, target_node)
	else:
		v.all_bullets_push_back_homing_target(target_node)
	return v


func _wait_locked(v: DirectionalBullets2D, frames := 60) -> void:
	for i in frames:
		await physics()
		if v.bullet_is_orbiting_locked(0):
			return


func test_targetless_arm_then_setters() -> void:
	var lone: DirectionalBullets2D = spawn_dir(1, 200.0)
	lone.bullet_enable_orbiting(0, 64.0, 2, 0)
	assert_true(lone.bullet_is_orbiting_enabled(0), "orbit arms even targetless")
	await physics(10)
	assert_true(lone.get_bullet_global_transform(0).is_finite(), "targetless orbit never NaNs")
	assert_false(lone.bullet_is_orbiting_locked(0), "targetless orbit never locks")
	lone.bullet_homing_push_back_node2d_target(0, target)
	await physics(20)
	assert_true(lone.bullet_is_orbiting_enabled(0), "orbit stays armed once the target arrives")
	lone.bullet_set_orbiting_radius(0, 96.0)
	assert_almost_eq(lone.bullet_get_orbiting_radius(0), 96.0, 0.01, "radius set")
	lone.bullet_set_orbiting_radius(0, -5.0)
	expect_error("Orbiting radius must be >= 0.01")
	assert_almost_eq(lone.bullet_get_orbiting_radius(0), 0.01, 0.001, "negative radius clamped to 0.01")
	lone.bullet_set_orbiting_direction(0, 1)
	lone.bullet_set_orbiting_texture_rotation(0, 0)
	lone.bullet_set_orbiting_follow_mode(0, 0)
	lone.bullet_set_orbiting_follow_deadzone(0, 8.0)
	lone.bullet_set_orbiting_lock_policy(0, 1)
	lone.bullet_set_orbiting_rigid_follow(0, true)
	assert_eq(lone.bullet_get_orbiting_follow_deadzone(0), 8.0, "deadzone stored")


func test_linear_shells_follow_and_disable() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(3, 250.0))
	v.set_homing_smoothing(6.0)
	v.set_homing_take_control_of_texture_rotation(true)
	v.all_bullets_push_back_homing_target(target)
	v.all_bullets_enable_orbiting_linear(48.0, 16.0, 2, 0)
	assert_true(v.bullet_is_orbiting_enabled(0) and v.bullet_is_orbiting_enabled(2), "linear shells arm all")
	assert_almost_eq(v.bullet_get_orbiting_radius(0), 48.0, 0.5, "shell 0 radius")
	assert_almost_eq(v.bullet_get_orbiting_radius(2), 80.0, 0.5, "shell 2 radius stepped")
	await physics(30)
	assert_true(v.bullet_is_orbiting_locked(0) or v.bullet_is_orbiting_enabled(0), "ring alive after 30 ticks")
	assert_lt(v.bullet_get_orbiting_center(0).distance_to(target.global_position), 120.0, "orbit center near target")
	target.position = Vector2(500, 100)
	await physics(15)
	assert_lt(v.bullet_get_orbiting_center(0).distance_to(Vector2(500, 100)), 160.0, "ring followed target")
	v.all_bullets_disable_orbiting()
	assert_false(v.bullet_is_orbiting_enabled(0), "disable clears all")
	v.bullet_disable_orbiting(1)


func test_relock_always_on_freed_target() -> void:
	var dying: Node2D = add(Node2D.new())
	dying.position = Vector2(300, 0)
	await idle(1)
	var w := _homing_volley(2, dying)
	w.all_bullets_enable_orbiting(64.0, 2, 0)
	await _wait_locked(w)
	assert_true(w.bullet_is_orbiting_locked(0), "ring locks before target death")
	dying.queue_free()
	await idle(1)
	await physics(5)
	assert_true(w.get_bullet_transform(0).is_finite() and w.get_bullet_velocity(0).is_finite(), "finite after target freed")
	assert_false(w.bullet_is_orbiting_locked(0), "RelockAlways unlocks on empty queue")
	assert_true(w.bullet_is_orbiting_enabled(0), "RelockAlways stays armed for the next target")


func test_stay_locked_rides_out_the_gap() -> void:
	var anchor: Node2D = add(Node2D.new())
	anchor.position = Vector2(300, 50)
	await idle(1)
	var s := _homing_volley(1, anchor)
	s.bullet_enable_orbiting(0, 64.0, 2, 0, 0, 8.0, 1, true)
	await _wait_locked(s)
	assert_true(s.bullet_is_orbiting_locked(0), "StayLocked ring locks before target death")
	var held_center: Vector2 = s.bullet_get_orbiting_center(0)
	anchor.queue_free()
	await idle(1)
	await physics(5)
	assert_true(s.get_bullet_transform(0).is_finite(), "finite after target freed")
	assert_true(s.bullet_is_orbiting_locked(0), "StayLocked holds lock across the gap")
	assert_lt(s.bullet_get_orbiting_center(0).distance_to(held_center), 8.0, "center rides out the gap")


func test_spawner_orbit_without_homing_is_a_setup_warning() -> void:
	const WARNING := "orbiting_enabled needs homing_enabled"
	var sp := make_spawner(H.make_directional_data(2, 200.0, 5.0))
	sp.set_orbiting_enabled(false)
	assert_false(Array(sp.get_setup_warnings()).any(func(w): return WARNING in w), "no warning with both switches off")
	sp.set_orbiting_enabled(true)
	assert_true(Array(sp.get_setup_warnings()).any(func(w): return WARNING in w), "orbiting alone warns (it locks onto homing targets)")
	sp.set_homing_enabled(true)
	assert_false(Array(sp.get_setup_warnings()).any(func(w): return WARNING in w), "homing plus orbiting is configured")
