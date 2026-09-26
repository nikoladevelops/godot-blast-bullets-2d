extends SceneTree
## Multi-spawner precedence suite: three spawners (and direct factory volleys)
## with DIFFERENT per-bullet/shared mixes on one shared factory, proving the
## same rule everywhere: per-bullet (valid) > shared (valid) > default, entry
## i drives bullet i only, tile_* opts into wrap per array, custom data never
## falls back, pool reuse never leaks a sibling spawner's flavor.
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_precedence_mixes.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _speed(v: float) -> BulletSpeedData2D:
	var sp := BulletSpeedData2D.new()
	sp.speed = v
	sp.max_speed = 3000.0
	sp.acceleration = 0.0
	return sp

func _rot(v: float) -> BulletRotationData2D:
	var r := BulletRotationData2D.new()
	r.rotation_speed = v
	r.max_rotation_speed = 1000.0
	r.rotation_acceleration = 0.0
	return r

func _mk_spawner(factory: BulletFactory2D, data: DirectionalBulletsData2D) -> BulletSpawner2D:
	var sp := BulletSpawner2D.new()
	get_root().add_child(sp)
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(data)
	sp.set_shooting_enabled(false)
	sp.set_helper_custom_transforms([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(24, 0)), Transform2D(0.0, Vector2(48, 0)), Transform2D(0.0, Vector2(72, 0))])
	sp.pattern_source = 24
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(2)
	sp.set_homing_global_position(Vector2(600, -100))
	return sp

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("MIXSPAWN T1 three spawners, three flavors, one factory")
	# A: per-bullet speeds win over its shared.
	var da := H.make_directional_data(4, 0.0)
	da.all_bullet_speed_data = [_speed(100.0), _speed(200.0), _speed(300.0), _speed(400.0)]
	da.shared_bullet_speed_data = _speed(999.0)
	# B: short per-bullet + shared fallback for the tail.
	var db := H.make_directional_data(4, 0.0)
	db.all_bullet_speed_data = [_speed(111.0)]
	db.shared_bullet_speed_data = _speed(222.0)
	# C: tiled wrap + negative speed on slot 0 (retreats) + custom separation.
	var dc := H.make_directional_data(4, 0.0)
	dc.all_bullet_speed_data = [_speed(-250.0), _speed(250.0)]
	dc.tile_all_bullet_speed_data = true
	var rc := Resource.new()
	var sc := Resource.new()
	dc.all_bullets_custom_data = [rc]
	dc.shared_bullets_custom_data = sc
	var sa := _mk_spawner(factory, da)
	var sb := _mk_spawner(factory, db)
	var scsp := _mk_spawner(factory, dc)
	_check(sa.shoot_once() and sb.shoot_once() and scsp.shoot_once(), "all three spawners fire")
	for i in 10:
		await physics_frame
	var la: Array = sa.get_live_volleys()
	var lb: Array = sb.get_live_volleys()
	var lc: Array = scsp.get_live_volleys()
	_check(la[0].get_amount_bullets() == 4 and lb[0].get_amount_bullets() == 4 and lc[0].get_amount_bullets() == 4, "custom transforms preserve all 4 spawn-data slots")
	var va: DirectionalBullets2D = la[0]
	var vb: DirectionalBullets2D = lb[0]
	var vc: DirectionalBullets2D = lc[0]
	_check(va != null and vb != null and vc != null, "live volleys tracked per owner")
	_check(absf(va.get_bullet_speed_data(0).speed - 100.0) < 0.01 and absf(va.get_bullet_speed_data(3).speed - 400.0) < 0.01, "A: per-bullet wins on every slot")
	_check(absf(vb.get_bullet_speed_data(0).speed - 111.0) < 0.01 and absf(vb.get_bullet_speed_data(2).speed - 222.0) < 0.01, "B: covered wins, tail falls to shared")
	_check(absf(vc.get_bullet_speed_data(2).speed + 250.0) < 0.01 and absf(vc.get_bullet_speed_data(3).speed - 250.0) < 0.01, "C: tiled wrap [-250,250,-250,250]")
	_check(vc.bullet_get_custom_data(0) == rc and vc.bullet_get_custom_data(1) == null, "C: custom strict, slot 1 null never shared")

	printerr("MIXSPAWN T2 rotation + wobble mixes stay per-owner")
	var ra := H.make_directional_data(3, 150.0)
	ra.all_bullet_rotation_data = [_rot(3.0), _rot(3.0), _rot(3.0)]
	var rb := H.make_directional_data(3, 150.0)
	rb.shared_bullet_rotation_data = _rot(7.0)
	var wob := BulletWobbleData2D.new()
	wob.enabled = true
	wob.amplitude = 22.0
	rb.shared_bullet_wobble_data = wob
	var sra := _mk_spawner(factory, ra)
	var srb := _mk_spawner(factory, rb)
	_check(sra.shoot_once() and srb.shoot_once(), "rotation/wobble spawners fire")
	for i in 10:
		await physics_frame
	var vra: DirectionalBullets2D = sra.get_live_volleys()[0]
	var vrb: DirectionalBullets2D = srb.get_live_volleys()[0]
	_check(absf(vra.bullet_get_rotation_speed(2) - 3.0) < 0.01, "per-bullet rotation owner keeps 3.0")
	_check(absf(vrb.bullet_get_rotation_speed(2) - 7.0) < 0.01, "shared-only owner fans 7.0 to slot 2")
	_check(absf(vrb.bullet_get_wobble_amplitude(0) - 22.0) < 0.01, "shared wobble reaches other owner")
	_check(absf(vra.bullet_get_wobble_amplitude(0)) < 0.01, "no wobble leaks into rotation owner")

	printerr("MIXSPAWN T3 homing flavors: per vs shared converge independently")
	var ha := H.make_directional_data(2, 250.0)
	var hb := H.make_directional_data(2, 250.0)
	var spa := _mk_spawner(factory, ha)
	var spb := _mk_spawner(factory, hb)
	_check(spa.shoot_once() and spb.shoot_once(), "homing spawners fire")
	for i in 5:
		await physics_frame
	var vha: DirectionalBullets2D = spa.get_live_volleys()[0]
	var vhb: DirectionalBullets2D = spb.get_live_volleys()[0]
	vha.set_homing_smoothing(5.0)
	vhb.set_homing_smoothing(5.0)
	vha.set_homing_take_control_of_texture_rotation(true)
	vhb.set_homing_take_control_of_texture_rotation(true)
	vha.bullet_homing_push_back_global_position_target(0, Vector2(-2000, 200))
	vhb.shared_homing_deque_push_back_global_position_target(Vector2(2000, 200))
	# The spawner pre-queues its own shared target (600,-100), so absolute
	# positions are meaningless. What matters is relative steering: slot 0
	# (own left target) points left of slot 1 (shared right) on the same
	# volley, and the shared owner's slot points right.
	for i in 30:
		await physics_frame
	var ha0: Vector2 = vha.get_bullet_direction(0)
	var ha1: Vector2 = vha.get_bullet_direction(1)
	var hb1: Vector2 = vhb.get_bullet_direction(1)
	_check(ha0.x < ha1.x - 0.3, "per-bullet homing steers its slot left of its sibling (%.2f vs %.2f)" % [ha0.x, ha1.x])
	_check(hb1.x > 0.0, "shared homing steers its owner right (%.2f)" % hb1.x)
	_check(vha.get_bullet_transform(0).is_finite() and vhb.get_bullet_transform(1).is_finite(), "both homing flights finite")

	printerr("MIXSPAWN T4 gravity mixes + pool reuse neutrality")
	var ga := H.make_directional_data(2, 250.0)
	ga.all_bullet_gravity = [Vector2(0, 500), Vector2(0, 500)]
	var gb := H.make_directional_data(2, 250.0)
	var spa2 := _mk_spawner(factory, ga)
	var spb2 := _mk_spawner(factory, gb)
	_check(spa2.shoot_once() and spb2.shoot_once(), "gravity spawners fire")
	for i in 30:
		await physics_frame
	var vga: DirectionalBullets2D = spa2.get_live_volleys()[0]
	var vgb: DirectionalBullets2D = spb2.get_live_volleys()[0]
	_check(vga.bullet_get_fall_speed(0) > 1.0, "gravity owner integrates fall speed")
	_check(vgb.bullet_get_fall_speed(0) < 0.01, "clean owner stays ballistic")
	# Drain both volleys, then prove the pool hands back a neutral volley.
	for k in 2:
		vga.disable_bullet(k)
		vgb.disable_bullet(k)
	await physics_frame
	var reuse: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 300.0))
	_check(reuse.bullet_get_fall_speed(0) < 0.01, "pool hit has no stale fall speed")
	_check(not reuse.get_is_wobble_enabled(), "pool hit has no stale wobble")
	_check(absf(reuse.bullet_get_homing_smoothing(0)) < 0.001, "pool hit has no latched smoothing")

	printerr("MIXSPAWN T5 spawn-data pattern paths tile across owners")
	var ppath := Path2D.new()
	var pcurve := Curve2D.new()
	pcurve.add_point(Vector2(0, 0))
	pcurve.add_point(Vector2(150, 0))
	ppath.curve = pcurve
	get_root().add_child(ppath)
	await process_frame
	var pa := H.make_directional_data(4, 200.0)
	pa.all_bullet_movement_pattern_paths = [ppath.get_path()]
	pa.tile_all_bullet_movement_pattern_paths = true
	var spa3 := _mk_spawner(factory, pa)
	_check(spa3.shoot_once(), "pattern spawner fires")
	await physics_frame
	var vpa: DirectionalBullets2D = spa3.get_live_volleys()[0]
	_check(vpa.get_amount_bullets() == 4, "pattern volley keeps 4 slots")
	_check(str(vpa.debug_get_pattern_info(0)["src"]) == "per" and str(vpa.debug_get_pattern_info(3)["src"]) == "per", "tiled spawn paths cover slots 0+3")
	# Strict twin through the factory: slot 3 must fall back, not wrap.
	var pb := H.make_directional_data(4, 200.0)
	pb.all_bullet_movement_pattern_paths = [ppath.get_path()]
	var vpb: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(pb)
	_check(str(vpb.debug_get_pattern_info(0)["src"]) == "per" and str(vpb.debug_get_pattern_info(3)["src"]) == "none", "strict spawn path covers slot 0 only")
	ppath.queue_free()
	spa3.queue_free()

	printerr("MIXSPAWN T6 spawner per-bullet smoothing fan + wobble random gen + speed range")
	var spf := BulletSpawner2D.new()
	get_root().add_child(spf)
	spf.set_bullet_factory(factory)
	spf.set_spawn_data(H.make_directional_data(3, 200.0))
	spf.set_shooting_enabled(false)
	var farr: Array = []
	for i in 4:
		farr.append(Transform2D(0.0, Vector2(24.0 * i, 0.0)))
	spf.set_helper_custom_transforms(farr)
	spf.pattern_source = 24
	spf.set_homing_enabled(true)
	spf.set_homing_target_source(2)
	spf.set_homing_global_position(Vector2(600, -100))
	spf.set_homing_per_bullet_smoothing_enabled(true)
	spf.set_homing_smoothing_start(1.0)
	spf.set_homing_smoothing_step(2.0)
	_check(spf.shoot_once(), "smoothing-fan spawner fires")
	for i in 5:
		await physics_frame
	var vpf: DirectionalBullets2D = spf.get_live_volleys()[0]
	_check(absf(vpf.bullet_get_homing_smoothing(0) - 1.0) < 0.001 and absf(vpf.bullet_get_homing_smoothing(1) - 3.0) < 0.001, "fan start+step*i slots 0-1 (%.1f/%.1f)" % [vpf.bullet_get_homing_smoothing(0), vpf.bullet_get_homing_smoothing(1)])
	_check(absf(vpf.bullet_get_homing_smoothing(2) - 5.0) < 0.001 and absf(vpf.bullet_get_homing_smoothing(3) - 7.0) < 0.001, "fan slots 2-3 (%.1f/%.1f)" % [vpf.bullet_get_homing_smoothing(2), vpf.bullet_get_homing_smoothing(3)])
	var rnd: Array = BulletWobbleData2D.generate_random_data(3, 10.0, 40.0, 1.0, 4.0)
	_check(rnd.size() == 3 and (rnd[0] as BulletWobbleData2D).enabled, "random wobble gen size 3, enabled")
	_check((rnd[0] as BulletWobbleData2D).amplitude >= 10.0 and (rnd[0] as BulletWobbleData2D).amplitude <= 40.0, "random amplitude in band (%.1f)" % (rnd[0] as BulletWobbleData2D).amplitude)
	_check(BulletWobbleData2D.generate_random_data(0, 10.0, 40.0, 1.0, 4.0).is_empty(), "random gen amount 0 rejected")
	_check(BulletWobbleData2D.generate_random_data(2, 40.0, 10.0, 1.0, 4.0).is_empty(), "random gen inverted band rejected")
	var vr2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(3, 100.0))
	var ns := BulletSpeedData2D.new()
	ns.speed = 555.0
	ns.max_speed = 3000.0
	vr2.all_bullets_set_speed_data(ns, 0, 1)
	_check(absf(vr2.get_bullet_speed_data(0).speed - 555.0) < 0.01 and absf(vr2.get_bullet_speed_data(1).speed - 555.0) < 0.01, "speed range fan 0-1")
	_check(absf(vr2.get_bullet_speed_data(2).speed - 100.0) < 0.01, "speed range spares slot 2")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	sa.queue_free()
	sb.queue_free()
	scsp.queue_free()
	sra.queue_free()
	srb.queue_free()
	spa.queue_free()
	spb.queue_free()
	spa2.queue_free()
	spb2.queue_free()
	spf.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL MULTI-SPAWNER PRECEDENCE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
