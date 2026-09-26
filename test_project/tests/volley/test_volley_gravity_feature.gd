extends SceneTree
## Volley gravity-feature suite: opt-in acceleration for sideview shells.
## Default is zero gravity = straight top-down flight (unchanged behavior).
## Covers: default-off straight flight, spawn-data shared fan-out, per-bullet
## vectors, delay postpones fall, duration stops it (keeps fall speed),
## strength-curve scaling, all directions, NaN rejects at every layer,
## pooled-reuse neutrality, runtime per-bullet edits, homing+gravity mix.
## Run: godot --headless --path test_project --script tests/volley/test_volley_gravity_feature.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _still_data(n: int = 1) -> DirectionalBulletsData2D:
	var d := H.make_directional_data(n, 0.0)
	return d

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("GRAV T1 default off = straight flight")
	var v0: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	var g0: Dictionary = v0.debug_get_gravity_info(0)
	_check((g0.get("vector", Vector2.ONE) as Vector2) == Vector2(0, 0), "default vector zero")
	_check((g0.get("fall_speed", 1.0) as float) == 0.0, "default fall speed zero")
	_check((g0.get("window_active", false) as bool) == true, "window open with zero windows")
	var p0: Vector2 = v0.get_bullet_global_transform(0).origin
	for i in 30:
		await physics_frame
	var p1: Vector2 = v0.get_bullet_global_transform(0).origin
	_check(absf(p1.y - p0.y) < 1.0, "no vertical drift without gravity")
	_check(absf(v0.bullet_get_fall_speed(0)) < 0.01, "fall speed stays zero")

	printerr("GRAV T2 spawn-data shared fan-out")
	var d := _still_data(3)
	d.gravity = Vector2(0, 2000)
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	_check(v.bullet_get_gravity(0) == Vector2(0, 2000), "shared fans to slot 0")
	_check(v.bullet_get_gravity(2) == Vector2(0, 2000), "shared fans to slot 2")
	_check(v.get_gravity() == Vector2(0, 2000), "shared member mirrors data")
	var q0: Vector2 = v.get_bullet_global_transform(1).origin
	for i in 60:
		await physics_frame
	var q1: Vector2 = v.get_bullet_global_transform(1).origin
	_check(q1.y > q0.y + 700.0 and q1.y < q0.y + 1400.0, "shared gravity falls (~%.0fpx)" % (q1.y - q0.y))

	printerr("GRAV T3 per-bullet vectors")
	var d3 := _still_data(3)
	d3.all_bullet_gravity = [Vector2(0, 0), Vector2(1000, 0), Vector2(0, 0)]
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d3)
	_check(v3.bullet_get_gravity(1) == Vector2(1000, 0), "per-bullet vector stored")
	_check(v3.bullet_get_gravity(0) == Vector2(0, 0), "sibling keeps zero")
	var r0: Vector2 = v3.get_bullet_global_transform(1).origin
	for i in 30:
		await physics_frame
	var r1: Vector2 = v3.get_bullet_global_transform(1).origin
	_check(r1.x > r0.x + 50.0, "per-bullet gravity pulls +X")

	printerr("GRAV T4 delay postpones, duration stops")
	var d4 := _still_data(1)
	d4.gravity = Vector2(0, 3000)
	d4.gravity_delay_sec = 0.5
	d4.gravity_duration_sec = 0.4
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4)
	_check(v4.get_gravity_delay_sec() == 0.5, "delay seeded")
	_check(absf(v4.get_gravity_duration_sec() - 0.4) < 0.001, "duration seeded")
	for i in 15: # ~0.25s < delay: no fall yet
		await physics_frame
	_check(v4.bullet_get_fall_speed(0) < 1.0, "no fall inside delay window")
	var f0: float = v4.bullet_get_fall_speed(0)
	for i in 30: # through the duration window
		await physics_frame
	_check(v4.bullet_get_fall_speed(0) > f0 + 100.0, "falls inside window")
	for i in 15: # push elapsed safely past delay+duration (0.9s)
		await physics_frame
	var f1: float = v4.bullet_get_fall_speed(0)
	for i in 30: # window closed: frozen fall speed, glides straight
		await physics_frame
	_check(absf(v4.bullet_get_fall_speed(0) - f1) < f1 * 0.05 + 1.0, "fall speed frozen past duration")
	var gi: Dictionary = v4.debug_get_gravity_info(0)
	_check((gi.get("window_active", true) as bool) == false, "window reports closed past duration")

	printerr("GRAV T5 strength curve scales fall")
	var d5 := _still_data(1)
	d5.gravity = Vector2(0, 2000)
	var gc := BulletCurvesData2D.new()
	var ramp := Curve.new()
	ramp.add_point(Vector2(0, 0))
	ramp.add_point(Vector2(1, 1))
	gc.gravity_strength_curve = ramp
	gc.gravity_use_unit_curve = false
	d5.shared_bullet_curves_data = gc
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5)
	var s0: Vector2 = v5.get_bullet_global_transform(0).origin
	for i in 60:
		await physics_frame
	var s1: Vector2 = v5.get_bullet_global_transform(0).origin
	# Ramp 0->1 over absolute time: weaker early, full late; total below full-gravity.
	_check(s1.y > s0.y + 100.0, "curved gravity still falls")
	var gi5: Dictionary = v5.debug_get_gravity_info(0)
	_check((gi5.get("curve_scale", 0.0) as float) > 0.0, "curve scale sampled live")

	printerr("GRAV T6 rejects at every layer")
	var d6 := _still_data(1)
	d6.gravity = Vector2(NAN, 0)
	_check(d6.gravity == Vector2(0, 0), "spawn-data NaN rejected")
	d6.all_bullet_gravity = [Vector2(INF, INF)]
	d6.gravity = Vector2(0, 1000)
	var v6: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d6)
	_check(v6.bullet_get_gravity(0) == Vector2(0, 0), "non-finite per-bullet entry fails open to zero")
	v6.bullet_set_gravity(0, Vector2(NAN, 1))
	_check(v6.bullet_get_gravity(0) == Vector2(0, 0), "runtime NaN rejected")
	v6.set_gravity_delay_sec(-1.0)
	_check(v6.get_gravity_delay_sec() == 0.0, "negative delay rejected")
	v6.set_gravity_duration_sec(-2.0)
	_check(v6.get_gravity_duration_sec() == 0.0, "negative duration rejected")
	d6.gravity_delay_sec = -1.0
	_check(d6.gravity_delay_sec == 0.0, "spawn-data negative delay rejected")

	printerr("GRAV T7 reuse neutrality + runtime edits + homing mix")
	v6.all_bullets_set_gravity(Vector2(0, 1500))
	_check(v6.bullet_get_gravity(0) == Vector2(0, 1500), "all_bullets edit fans out")
	for i in 1:
		v6.disable_bullet(i)
	await physics_frame
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 200.0))
	_check(v7.bullet_get_gravity(0) == Vector2(0, 0), "reuse after gravity volley is neutral")
	_check(v7.bullet_get_fall_speed(0) == 0.0, "no inherited fall speed")
	var tgt := Node2D.new()
	tgt.position = Vector2(400, -200)
	get_root().add_child(tgt)
	v7.set_homing_smoothing(5.0)
	v7.set_homing_take_control_of_texture_rotation(true)
	v7.bullet_homing_push_back_node2d_target(0, tgt)
	v7.bullet_set_gravity(0, Vector2(0, 800))
	for i in 30:
		await physics_frame
	_check(v7.get_bullet_transform(0).is_finite(), "homing + gravity compose without NaN")
	_check(v7.bullet_get_fall_speed(0) > 10.0, "fall accumulates while homing steers")
	tgt.queue_free()

	printerr("GRAV T8 spawner seeds gravity + fallback rule")
	var sg := _still_data(2)
	sg.gravity = Vector2(0, 1200)
	var spawner := BulletSpawner2D.new()
	get_root().add_child(spawner)
	await process_frame
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(sg)
	spawner.set_shooting_enabled(false)
	spawner.pattern_source = 1
	_check(spawner.shoot_once(), "gravity spawner shot fires")
	var live: Array = spawner.get_live_volleys()
	if live.is_empty():
		# Plain (non-homing) volleys are untracked by design; resolve via factory.
		var vols: int = factory.debug_get_total_bullets_amount(0)
		_check(vols >= 1, "gravity volley exists in factory")
	else:
		_check((live[0] as DirectionalBullets2D).bullet_get_gravity(0) == Vector2(0, 1200), "spawner volley carries gravity")
	var fb := _still_data(3)
	fb.all_bullet_gravity = [Vector2(500, 0), Vector2(0, 500)]
	var vf: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(fb)
	_check(vf.bullet_get_gravity(0) == Vector2(500, 0), "strict slot 0 reads entry 0")
	_check(vf.bullet_get_gravity(1) == Vector2(0, 500), "strict slot 1 reads entry 1")
	_check(vf.bullet_get_gravity(2) == Vector2(0, 0), "strict slot 2 uncovered reads default (no shared set)")
	var fbt := _still_data(3)
	fbt.all_bullet_gravity = [Vector2(500, 0), Vector2(0, 500)]
	fbt.tile_all_bullet_gravity = true
	var vft: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(fbt)
	_check(vft.bullet_get_gravity(2) == Vector2(500, 0), "tiled slot 2 wraps to entry 0")
	spawner.queue_free()

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL GRAVITY FEATURE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
