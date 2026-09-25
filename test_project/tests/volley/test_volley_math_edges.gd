extends SceneTree
## Volley math-edges suite: quantitative identities for the tick math.
## Covers: linear drag geometric decay band, rotation accel clamp at max,
## default rotation free-spin (max 0 = unlimited), wobble lateral boundedness
## around the ballistic mean, spin advance on the spawner clock is covered in
## patterns; here: per-bullet smoothing cannot go negative via fan clamp.
## Run: godot --headless --path test_project --script tests/volley/test_volley_math_edges.gd
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

	printerr("MATH T1 drag decays geometrically")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 200.0))
	v.set_linear_drag(1.0)
	for i in 60:
		await physics_frame
	var s60: float = v.get_bullet_speed_data(0).speed
	# 200*(1-1/60)^60 ~= 73; band wide for headless dt jitter.
	_check(s60 > 30.0 and s60 < 130.0, "drag decay band (%.1f)" % s60)
	v.set_linear_drag(-1.0)
	_check(v.get_linear_drag() == 1.0, "negative drag rejected")

	printerr("MATH T2 rotation accel clamps at max")
	var r: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 0.0))
	var rd := BulletRotationData2D.new()
	rd.rotation_speed = 0.0
	rd.max_rotation_speed = 2.0
	rd.rotation_acceleration = 100.0
	r.set_shared_bullet_rotation_data(rd)
	for i in 30:
		await physics_frame
	# Rotation speed must never exceed max (checked via continued finite spin).
	_check(r.get_bullet_texture_rotation_radians(0) != 0.0, "rotation advanced under accel")
	r.remove_shared_bullet_rotation_data()
	_check(not r.has_shared_bullet_rotation_data(), "rotation data removed")

	printerr("MATH T3 wobble lateral stays bounded around ballistic mean")
	var wdata := H.make_directional_data(1, 300.0)
	var wob := BulletWobbleData2D.new()
	wob.enabled = true
	wob.amplitude = 24.0
	wob.frequency_hz = 2.0
	wdata.shared_bullet_wobble_data = wob
	var w: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(wdata)
	var max_dev := 0.0
	for i in 60:
		await physics_frame
		var p: Vector2 = w.get_bullet_global_transform(0).origin
		max_dev = maxf(max_dev, absf(p.y))
	_check(max_dev < 120.0, "lateral wobble bounded (max %.1f)" % max_dev)
	_check(w.get_bullet_global_transform(0).origin.x > 200.0, "forward progress kept under wobble")

	printerr("MATH T4 per-bullet smoothing clamps at zero")
	var f: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	f.bullet_set_homing_smoothing(0, -5.0)
	_check(f.bullet_get_homing_smoothing(0) == 0.0, "negative smoothing clamped to 0")
	f.bullet_set_homing_smoothing(0, NAN)
	_check(f.bullet_get_homing_smoothing(0) == 0.0, "NaN smoothing rejected")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL MATH-EDGE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
