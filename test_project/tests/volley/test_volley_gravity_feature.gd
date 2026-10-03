extends BlastTest
## Opt-in gravity for sideview shells. Default is zero gravity (straight
## top-down flight). Shared fan-out, per-bullet vectors, delay/duration
## windows, strength curve, NaN rejects, pooled reuse neutrality, homing mix,
## spawner seeding and strict/tiled per-bullet indexing.


func _still(n: int = 1) -> BulletVolleyData2D:
	return H.make_volley_data(n, 0.0)


func _y(v: BulletVolley2D, i: int) -> Vector2:
	return v.get_bullet_global_transform(i).origin


func test_default_off_flies_straight() -> void:
	var v0: BulletVolley2D = quick_volley(2, 200.0)
	var g0: Dictionary = v0.debug_get_gravity_info(0)
	assert_eq(g0.get("vector", Vector2.ONE), Vector2.ZERO, "default vector zero")
	assert_eq(float(g0.get("fall_speed", 1.0)), 0.0, "default fall speed zero")
	assert_true(g0.get("window_active", false), "window open with zero windows")
	var p0 := _y(v0, 0)
	await physics(30)
	assert_almost_eq(_y(v0, 0).y, p0.y, 1.0, "no vertical drift without gravity")
	assert_almost_eq(v0.bullet_get_fall_speed(0), 0.0, 0.01, "fall speed stays zero")


func test_shared_fan_out() -> void:
	var d := _still(3)
	d.gravity = Vector2(0, 2000)
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_eq(v.bullet_get_gravity(0), Vector2(0, 2000), "shared fans to slot 0")
	assert_eq(v.bullet_get_gravity(2), Vector2(0, 2000), "shared fans to slot 2")
	assert_eq(v.get_gravity(), Vector2(0, 2000), "shared member mirrors data")
	var q0 := _y(v, 1)
	await physics(60)
	assert_between(_y(v, 1).y - q0.y, 700.0, 1400.0, "shared gravity falls")


func test_per_bullet_vectors() -> void:
	var d := _still(3)
	d.all_bullet_gravity = [Vector2.ZERO, Vector2(1000, 0), Vector2.ZERO]
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_eq(v.bullet_get_gravity(1), Vector2(1000, 0), "per-bullet vector stored")
	assert_eq(v.bullet_get_gravity(0), Vector2.ZERO, "sibling keeps zero")
	var r0 := _y(v, 1)
	await physics(30)
	assert_gt(_y(v, 1).x, r0.x + 50.0, "per-bullet gravity pulls +X")


func test_delay_postpones_duration_stops() -> void:
	var d := _still(1)
	d.gravity = Vector2(0, 3000)
	d.gravity_delay_sec = 0.5
	d.gravity_duration_sec = 0.4
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_eq(v.get_gravity_delay_sec(), 0.5, "delay seeded")
	assert_almost_eq(v.get_gravity_duration_sec(), 0.4, 0.001, "duration seeded")
	await physics(15) # ~0.25 s < delay
	assert_lt(v.bullet_get_fall_speed(0), 1.0, "no fall inside the delay window")
	var f0: float = v.bullet_get_fall_speed(0)
	await physics(30)
	assert_gt(v.bullet_get_fall_speed(0), f0 + 100.0, "falls inside the window")
	await physics(15) # past delay + duration (0.9 s)
	var f1: float = v.bullet_get_fall_speed(0)
	await physics(30)
	assert_almost_eq(v.bullet_get_fall_speed(0), f1, f1 * 0.05 + 1.0, "fall speed frozen past duration")
	assert_false(v.debug_get_gravity_info(0).get("window_active", true), "window reports closed")


func test_strength_curve_scales_fall() -> void:
	var d := _still(1)
	d.gravity = Vector2(0, 2000)
	var gc := BulletCurvesData2D.new()
	var ramp := Curve.new()
	ramp.add_point(Vector2(0, 0))
	ramp.add_point(Vector2(1, 1))
	gc.gravity_strength_curve = ramp
	gc.gravity_use_unit_curve = false
	d.shared_bullet_curves_data = gc
	var v: BulletVolley2D = factory.spawn_volley(d)
	var s0 := _y(v, 0)
	await physics(60)
	assert_gt(_y(v, 0).y, s0.y + 100.0, "curved gravity still falls")
	assert_gt(float(v.debug_get_gravity_info(0).get("curve_scale", 0.0)), 0.0, "curve scale sampled live")


func test_rejects_at_every_layer() -> void:
	var d := _still(1)
	d.gravity = Vector2(NAN, 0)
	assert_eq(d.gravity, Vector2.ZERO, "spawn-data NaN rejected")
	d.all_bullet_gravity = [Vector2(INF, INF)]
	d.gravity = Vector2(0, 1000)
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_eq(v.bullet_get_gravity(0), Vector2.ZERO, "non-finite per-bullet entry fails open to zero")
	v.bullet_set_gravity(0, Vector2(NAN, 1))
	assert_eq(v.bullet_get_gravity(0), Vector2.ZERO, "runtime NaN rejected")
	v.set_gravity_delay_sec(-1.0)
	assert_eq(v.get_gravity_delay_sec(), 0.0, "negative delay rejected")
	v.set_gravity_duration_sec(-2.0)
	assert_eq(v.get_gravity_duration_sec(), 0.0, "negative duration rejected")
	d.gravity_delay_sec = -1.0
	assert_eq(d.gravity_delay_sec, 0.0, "spawn-data negative delay rejected")
	expect_error_sequence(["BulletVolleyData2D: gravity must be finite, keeping the old value.", "BulletVolleyData2D all_bullet_gravity[0] is not finite, using (0, 0) for bullet index 0.", "BulletVolley2D.bullet_set_gravity: value must be finite, keeping the old value.", "BulletVolley2D.set_gravity_delay_sec: value must be finite and >= 0, keeping the old value.", "BulletVolley2D.set_gravity_duration_sec: value must be finite and >= 0 (0 = infinite), keeping the old value.", "BulletVolleyData2D: gravity_delay_sec must be finite and >= 0, keeping the old value."], "every non-finite or negative gravity input fails loud")


func test_reuse_neutral_and_homing_mix() -> void:
	var d := _still(1)
	d.gravity = Vector2(0, 1000)
	var v6: BulletVolley2D = factory.spawn_volley(d)
	v6.all_bullets_set_gravity(Vector2(0, 1500))
	assert_eq(v6.bullet_get_gravity(0), Vector2(0, 1500), "all_bullets edit fans out")
	v6.disable_bullet(0)
	await physics()
	var v7: BulletVolley2D = quick_volley(1, 200.0)
	assert_eq(v7.bullet_get_gravity(0), Vector2.ZERO, "reuse after a gravity volley is neutral")
	assert_eq(v7.bullet_get_fall_speed(0), 0.0, "no inherited fall speed")
	var tgt: Node2D = add(Node2D.new())
	tgt.position = Vector2(400, -200)
	v7.set_homing_smoothing(5.0)
	v7.set_homing_take_control_of_texture_rotation(true)
	v7.bullet_homing_push_back_node2d_target(0, tgt)
	v7.bullet_set_gravity(0, Vector2(0, 800))
	await physics(30)
	assert_true(v7.get_bullet_transform(0).is_finite(), "homing + gravity compose without NaN")
	assert_gt(v7.bullet_get_fall_speed(0), 10.0, "fall accumulates while homing steers")


func test_spawner_seeding_and_indexing() -> void:
	var sg := _still(2)
	sg.gravity = Vector2(0, 1200)
	var spawner := make_spawner(sg, 1)
	assert_true(spawner.shoot_once(), "gravity spawner shot fires")
	var live: Array = spawner.get_live_volleys()
	if live.is_empty():
		# Plain (non-homing) volleys are untracked by design.
		assert_gte(factory.debug_get_total_bullets_amount(), 1, "gravity volley exists in factory")
	else:
		assert_eq((live[0] as BulletVolley2D).bullet_get_gravity(0), Vector2(0, 1200), "spawner volley carries gravity")
	var fb := _still(3)
	fb.all_bullet_gravity = [Vector2(500, 0), Vector2(0, 500)]
	var vf: BulletVolley2D = factory.spawn_volley(fb)
	assert_eq(vf.bullet_get_gravity(0), Vector2(500, 0), "strict slot 0 reads entry 0")
	assert_eq(vf.bullet_get_gravity(1), Vector2(0, 500), "strict slot 1 reads entry 1")
	assert_eq(vf.bullet_get_gravity(2), Vector2.ZERO, "strict uncovered slot reads default")
	var fbt := _still(3)
	fbt.all_bullet_gravity = [Vector2(500, 0), Vector2(0, 500)]
	fbt.tile_all_bullet_gravity = true
	var vft: BulletVolley2D = factory.spawn_volley(fbt)
	assert_eq(vft.bullet_get_gravity(2), Vector2(500, 0), "tiled slot 2 wraps to entry 0")
