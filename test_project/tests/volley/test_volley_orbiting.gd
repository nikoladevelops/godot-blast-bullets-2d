extends SceneTree
## Volley orbiting suite: rings, modes, lock policies, mixed with homing.
## Covers: enable without homing (warns, no-op), per-bullet + all_bullets +
## linear shells, radius/direction/texture-rotation/follow/deadzone/lock/rigid
## setters, locked state over frames, disable clears, retarget-policy mix,
## orbit centers translate with moving target (rigid follow).
## Run: godot --headless --path test_project --script tests/volley/test_volley_orbiting.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var target := Node2D.new()
	target.position = Vector2(400, 0)
	get_root().add_child(target)
	await process_frame

	printerr("ORBIT T1 arming without target is safe, steering waits")
	var lone: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 200.0))
	var p_before: Vector2 = lone.get_bullet_global_transform(0).origin
	lone.bullet_enable_orbiting(0, 64.0, 2, 0)
	_check(lone.bullet_is_orbiting_enabled(0), "orbit arms even targetless (flag set)")
	for i in 10:
		await physics_frame
	_check(lone.get_bullet_global_transform(0).is_finite(), "targetless orbit never NaNs")
	_check(not lone.bullet_is_orbiting_locked(0), "targetless orbit never locks")
	lone.bullet_homing_push_back_node2d_target(0, target)
	for i in 20:
		await physics_frame
	_check(lone.bullet_is_orbiting_enabled(0), "orbit stays armed once target arrives")

	printerr("ORBIT T2 setters")
	lone.bullet_set_orbiting_radius(0, 96.0)
	_check(absf(lone.bullet_get_orbiting_radius(0) - 96.0) < 0.01, "radius set")
	lone.bullet_set_orbiting_radius(0, -5.0)
	_check(absf(lone.bullet_get_orbiting_radius(0) - 0.01) < 0.001, "negative radius clamped to 0.01")
	lone.bullet_set_orbiting_direction(0, 1)
	lone.bullet_set_orbiting_texture_rotation(0, 0)
	lone.bullet_set_orbiting_follow_mode(0, 0)
	lone.bullet_set_orbiting_follow_deadzone(0, 8.0)
	lone.bullet_set_orbiting_lock_policy(0, 1)
	lone.bullet_set_orbiting_rigid_follow(0, true)
	_check(lone.bullet_get_orbiting_follow_deadzone(0) == 8.0, "deadzone stored")

	printerr("ORBIT T3 all_bullets + linear shells")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(3, 250.0))
	v.set_homing_smoothing(6.0)
	v.set_homing_take_control_of_texture_rotation(true)
	v.all_bullets_push_back_homing_target(target)
	v.all_bullets_enable_orbiting_linear(48.0, 16.0, 2, 0)
	_check(v.bullet_is_orbiting_enabled(0) and v.bullet_is_orbiting_enabled(2), "linear shells arm all")
	_check(absf(v.bullet_get_orbiting_radius(0) - 48.0) < 0.5, "shell 0 radius")
	_check(absf(v.bullet_get_orbiting_radius(2) - 80.0) < 0.5, "shell 2 radius stepped")
	for i in 30:
		await physics_frame
	_check(v.bullet_is_orbiting_locked(0) or v.bullet_is_orbiting_enabled(0), "ring alive after 30 ticks")
	var c0: Vector2 = v.bullet_get_orbiting_center(0)
	_check(c0.distance_to(target.global_position) < 120.0, "orbit center near target")

	printerr("ORBIT T4 rigid follow translates ring with target")
	target.position = Vector2(500, 100)
	for i in 15:
		await physics_frame
	var c1: Vector2 = v.bullet_get_orbiting_center(0)
	_check(c1.distance_to(Vector2(500, 100)) < 160.0, "ring followed target")

	printerr("ORBIT T5 disable clears")
	v.all_bullets_disable_orbiting()
	_check(not v.bullet_is_orbiting_enabled(0), "disable clears all")
	v.bullet_disable_orbiting(1)
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after orbit ops")

	printerr("ORBIT T6 freed target never crashes, lock follows policy")
	var dying := Node2D.new()
	dying.position = Vector2(300, 0)
	get_root().add_child(dying)
	await process_frame
	var w: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 250.0, 30.0))
	w.set_homing_smoothing(6.0)
	w.set_homing_take_control_of_texture_rotation(true)
	w.all_bullets_push_back_homing_target(dying)
	w.all_bullets_enable_orbiting(64.0, 2, 0)
	for i in 60:
		await physics_frame
		if w.bullet_is_orbiting_locked(0):
			break
	_check(w.bullet_is_orbiting_locked(0), "ring locks before target death")
	dying.queue_free()
	await process_frame
	for i in 5:
		await physics_frame
	_check(w.get_bullet_transform(0).is_finite() and w.get_bullet_velocity(0).is_finite(), "RelockAlways volley finite after target freed")
	_check(not w.bullet_is_orbiting_locked(0), "RelockAlways unlocks on empty queue")
	_check(w.bullet_is_orbiting_enabled(0), "RelockAlways stays armed for next target")

	var anchor2 := Node2D.new()
	anchor2.position = Vector2(300, 50)
	get_root().add_child(anchor2)
	await process_frame
	var s: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 250.0, 30.0))
	s.set_homing_smoothing(6.0)
	s.set_homing_take_control_of_texture_rotation(true)
	s.bullet_homing_push_back_node2d_target(0, anchor2)
	s.bullet_enable_orbiting(0, 64.0, 2, 0, 0, 8.0, 1, true)
	for i in 60:
		await physics_frame
		if s.bullet_is_orbiting_locked(0):
			break
	_check(s.bullet_is_orbiting_locked(0), "StayLocked ring locks before target death")
	var held_center: Vector2 = s.bullet_get_orbiting_center(0)
	anchor2.queue_free()
	await process_frame
	for i in 5:
		await physics_frame
	_check(s.get_bullet_transform(0).is_finite(), "StayLocked volley finite after target freed")
	_check(s.bullet_is_orbiting_locked(0), "StayLocked holds lock across the gap")
	_check(s.bullet_get_orbiting_center(0).distance_to(held_center) < 8.0, "StayLocked center rides out the gap")

	printerr("ORBIT T7 spawner is_orbit_armed matches config")
	var sp := BulletSpawner2D.new()
	get_root().add_child(sp)
	await process_frame
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(H.make_directional_data(2, 200.0, 5.0))
	_check(not sp.is_orbit_armed(), "unarmed with both switches off")
	sp.set_orbiting_enabled(true)
	_check(not sp.is_orbit_armed(), "orbiting alone is not armed")
	sp.set_homing_enabled(true)
	_check(sp.is_orbit_armed(), "homing plus orbiting is armed")
	sp.queue_free()
	await process_frame

	target.queue_free()
	await process_frame
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL ORBITING TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
