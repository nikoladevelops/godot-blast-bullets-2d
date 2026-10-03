extends BlastTest
## Several spawners with DIFFERENT per-bullet/shared mixes on one factory:
## per-bullet (valid) > shared (valid) > default everywhere, entry i drives
## bullet i only, tile_* opts into wrap, custom data never falls back, homing
## flavors converge independently, pool reuse never leaks a sibling's flavor.

const CUSTOM_SLOTS := 4


func _speed(v: float) -> BulletSpeedData2D:
	var s := BulletSpeedData2D.new()
	s.speed = v
	s.max_speed = 3000.0
	return s


func _rot(v: float) -> BulletRotationData2D:
	var r := BulletRotationData2D.new()
	r.rotation_speed = v
	r.max_rotation_speed = 1000.0
	return r


## Custom 4-slot spawner with homing toward a global point (tracked volleys).
func _mk(data: DirectionalBulletsData2D) -> BulletSpawner2D:
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM, CUSTOM_SLOTS)
	var slots: Array = []
	for i in CUSTOM_SLOTS:
		slots.append(Transform2D(0.0, Vector2(24.0 * i, 0)))
	sp.set_helper_custom_transforms(slots)
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	sp.set_homing_global_position(Vector2(600, -100))
	return sp


func _live(sp: BulletSpawner2D) -> DirectionalBullets2D:
	return sp.get_live_volleys()[0]


func test_three_speed_flavors_one_factory() -> void:
	var da := H.make_directional_data(4, 0.0)
	da.all_bullet_speed_data = [_speed(100.0), _speed(200.0), _speed(300.0), _speed(400.0)]
	da.shared_bullet_speed_data = _speed(999.0)
	var db := H.make_directional_data(4, 0.0)
	db.all_bullet_speed_data = [_speed(111.0)]
	db.shared_bullet_speed_data = _speed(222.0)
	var dc := H.make_directional_data(4, 0.0)
	dc.all_bullet_speed_data = [_speed(-250.0), _speed(250.0)]
	dc.tile_all_bullet_speed_data = true
	var rc := Resource.new()
	dc.all_bullets_custom_data = [rc]
	dc.shared_bullets_custom_data = Resource.new()
	var sa := _mk(da)
	var sb := _mk(db)
	var sc := _mk(dc)
	assert_true(sa.shoot_once() and sb.shoot_once() and sc.shoot_once(), "all three spawners fire")
	swallow_warnings_about_sizes()
	await physics(10)
	var va := _live(sa)
	var vb := _live(sb)
	var vc := _live(sc)
	for v in [va, vb, vc]:
		assert_eq(v.get_amount_bullets(), 4, "custom transforms keep all 4 slots")
	assert_almost_eq(va.get_bullet_speed_data(0).speed, 100.0, 0.01, "A: per-bullet slot 0")
	assert_almost_eq(va.get_bullet_speed_data(3).speed, 400.0, 0.01, "A: per-bullet slot 3")
	assert_almost_eq(vb.get_bullet_speed_data(0).speed, 111.0, 0.01, "B: covered slot wins")
	assert_almost_eq(vb.get_bullet_speed_data(2).speed, 222.0, 0.01, "B: tail falls to shared")
	assert_almost_eq(vc.get_bullet_speed_data(2).speed, -250.0, 0.01, "C: tiled wrap slot 2")
	assert_almost_eq(vc.get_bullet_speed_data(3).speed, 250.0, 0.01, "C: tiled wrap slot 3")
	assert_eq(vc.bullet_get_custom_data(0), rc, "C: per-bullet custom data")
	assert_null(vc.bullet_get_custom_data(1), "C: slot 1 null, never the shared value")


func swallow_warnings_about_sizes() -> void:
	for err in get_errors():
		if not err.handled and err.is_push_warning():
			err.handled = true


func test_rotation_and_wobble_stay_per_owner() -> void:
	var ra := H.make_directional_data(4, 150.0)
	ra.all_bullet_rotation_data = [_rot(3.0), _rot(3.0), _rot(3.0), _rot(3.0)]
	var rb := H.make_directional_data(4, 150.0)
	rb.shared_bullet_rotation_data = _rot(7.0)
	var wob := BulletWobbleData2D.new()
	wob.enabled = true
	wob.amplitude = 22.0
	rb.shared_bullet_wobble_data = wob
	var sra := _mk(ra)
	var srb := _mk(rb)
	assert_true(sra.shoot_once() and srb.shoot_once())
	await physics(10)
	assert_almost_eq(_live(sra).bullet_get_rotation_speed(2), 3.0, 0.01, "per-bullet rotation owner keeps 3.0")
	assert_almost_eq(_live(srb).bullet_get_rotation_speed(2), 7.0, 0.01, "shared-only owner fans 7.0 to slot 2")
	assert_almost_eq(_live(srb).bullet_get_wobble_amplitude(0), 22.0, 0.01, "shared wobble reaches its owner")
	assert_almost_eq(_live(sra).bullet_get_wobble_amplitude(0), 0.0, 0.01, "no wobble leaks into the rotation owner")


func test_homing_flavors_converge_independently() -> void:
	var spa := _mk(H.make_directional_data(4, 250.0))
	var spb := _mk(H.make_directional_data(4, 250.0))
	assert_true(spa.shoot_once() and spb.shoot_once())
	await physics(5)
	var vha := _live(spa)
	var vhb := _live(spb)
	for v in [vha, vhb]:
		v.set_homing_smoothing(5.0)
		v.set_homing_take_control_of_texture_rotation(true)
	vha.bullet_homing_push_back_global_position_target(0, Vector2(-2000, 200))
	vhb.shared_homing_deque_push_back_global_position_target(Vector2(2000, 200))
	await physics(30)
	# The spawner pre-queues its own shared target: compare relative steering.
	assert_lt(vha.get_bullet_direction(0).x, vha.get_bullet_direction(1).x - 0.3, "per-bullet homing steers its slot left of its sibling")
	assert_gt(vhb.get_bullet_direction(1).x, 0.0, "shared homing steers its owner right")
	assert_true(vha.get_bullet_transform(0).is_finite() and vhb.get_bullet_transform(1).is_finite())


func test_gravity_mix_and_neutral_reuse() -> void:
	var ga := H.make_directional_data(4, 250.0)
	ga.all_bullet_gravity = [Vector2(0, 500), Vector2(0, 500), Vector2(0, 500), Vector2(0, 500)]
	var spa := _mk(ga)
	var spb := _mk(H.make_directional_data(4, 250.0))
	assert_true(spa.shoot_once() and spb.shoot_once())
	await physics(30)
	var vga := _live(spa)
	var vgb := _live(spb)
	assert_gt(vga.bullet_get_fall_speed(0), 1.0, "gravity owner integrates fall speed")
	assert_lt(vgb.bullet_get_fall_speed(0), 0.01, "clean owner stays ballistic")
	for k in 4:
		vga.disable_bullet(k)
		vgb.disable_bullet(k)
	await idle(1)
	var reuse: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(4, 300.0))
	assert_lt(reuse.bullet_get_fall_speed(0), 0.01, "pool hit has no stale fall speed")
	assert_false(reuse.get_is_wobble_enabled(), "pool hit has no stale wobble")
	assert_almost_eq(reuse.bullet_get_homing_smoothing(0), 0.0, 0.001, "pool hit has no latched smoothing")


func test_spawn_data_pattern_paths_tile() -> void:
	var ppath: Path2D = add(Path2D.new())
	var pcurve := Curve2D.new()
	pcurve.add_point(Vector2(0, 0))
	pcurve.add_point(Vector2(150, 0))
	ppath.curve = pcurve
	await idle(1)
	var pa := H.make_directional_data(4, 200.0)
	pa.all_bullet_movement_pattern_paths = [ppath.get_path()]
	pa.tile_all_bullet_movement_pattern_paths = true
	var spa := _mk(pa)
	assert_true(spa.shoot_once())
	await physics()
	var vpa := _live(spa)
	assert_eq(str(vpa.debug_get_pattern_info(0)["src"]), "per", "tiled path covers slot 0")
	assert_eq(str(vpa.debug_get_pattern_info(3)["src"]), "per", "tiled path covers slot 3")
	var pb := H.make_directional_data(4, 200.0)
	pb.all_bullet_movement_pattern_paths = [ppath.get_path()]
	var vpb: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(pb)
	swallow_warnings_about_sizes()
	assert_eq(str(vpb.debug_get_pattern_info(0)["src"]), "per", "strict path covers slot 0")
	assert_eq(str(vpb.debug_get_pattern_info(3)["src"]), "none", "strict path never wraps")


func test_smoothing_fan_random_wobble_and_speed_range() -> void:
	var spf := _mk(H.make_directional_data(4, 200.0))
	spf.set_homing_per_bullet_smoothing_enabled(true)
	spf.set_homing_smoothing_start(1.0)
	spf.set_homing_smoothing_step(2.0)
	assert_true(spf.shoot_once())
	await physics(5)
	var vpf := _live(spf)
	for i in 4:
		assert_almost_eq(vpf.bullet_get_homing_smoothing(i), 1.0 + 2.0 * i, 0.001, "fan start+step*i slot %d" % i)
	var rnd: Array = BulletWobbleData2D.generate_random_data(3, 10.0, 40.0, 1.0, 4.0)
	assert_eq(rnd.size(), 3, "random wobble gen size 3")
	assert_true((rnd[0] as BulletWobbleData2D).enabled)
	assert_between((rnd[0] as BulletWobbleData2D).amplitude, 10.0, 40.0, "random amplitude in band")
	assert_true(BulletWobbleData2D.generate_random_data(0, 10.0, 40.0, 1.0, 4.0).is_empty(), "amount 0 rejected")
	expect_error_sequence(["generate_random_data: amount_to_generate must be > 0"])
	assert_true(BulletWobbleData2D.generate_random_data(2, 40.0, 10.0, 1.0, 4.0).is_empty(), "inverted band rejected")
	expect_error_sequence(["generate_random_data: every MIN must be <= its MAX"])
	var vr: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(3, 100.0))
	vr.all_bullets_set_speed_data(_speed(555.0), 0, 1)
	assert_almost_eq(vr.get_bullet_speed_data(0).speed, 555.0, 0.01, "range fan slot 0")
	assert_almost_eq(vr.get_bullet_speed_data(1).speed, 555.0, 0.01, "range fan slot 1")
	assert_almost_eq(vr.get_bullet_speed_data(2).speed, 100.0, 0.01, "range spares slot 2")
