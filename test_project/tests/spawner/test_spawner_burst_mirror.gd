extends BlastTest
## burst_alternate_mirror flips CHIRALITY (spiral-family winding reverses on
## mirrored shots, not just the emitter spin), alternates every other shot,
## and leaves non-spiral patterns untouched. DISTRIBUTE with the default
## homing_max_targets=1 still fires (and warns); with 2+ it spreads. A
## spinning 200-dot preview survives many frames.

var _vols: Array = []


func _on_volley_fired(volley: Object, _amount: int) -> void:
	_vols.append(volley)


func _spawner(src: int, amount: int) -> BulletSpawner2D:
	var s := make_spawner(H.make_directional_data(1, 60.0, 60.0), src, amount)
	s.volley_fired.connect(_on_volley_fired)
	watch_signals(s)
	return s


func _winding_sign(v: DirectionalBullets2D) -> int:
	var total := 0.0
	for i in range(1, v.get_amount_bullets()):
		total += angle_difference(v.get_bullet_transform(i - 1).get_origin().angle(), v.get_bullet_transform(i).get_origin().angle())
	if absf(total) < 0.01:
		return 0
	return 1 if total > 0.0 else -1


## Fires a 2-shot alternate-mirror burst -> {mirrored, plain}, paired by the
## burst_shot_fired flag (authoritative, not capture order).
func _fire_burst_pair(sp: BulletSpawner2D) -> Dictionary:
	_vols.clear()
	sp.set_burst_alternate_mirror(true)
	sp.set_burst_enabled(true)
	sp.set_burst_count(2)
	sp.set_burst_interval_sec(0.05)
	sp.begin_burst()
	for i in 80:
		await physics()
		if get_signal_emit_count(sp, "burst_shot_fired") >= 2 and _vols.size() >= 2:
			break
	sp.set_burst_enabled(false)
	var out := {"mirrored": null, "plain": null}
	for i in mini(_vols.size(), get_signal_emit_count(sp, "burst_shot_fired")):
		out["mirrored" if bool(get_signal_parameters(sp, "burst_shot_fired", i)[1]) else "plain"] = _vols[i]
	return out


func test_mirror_flag_alternates() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.set_burst_alternate_mirror(true)
	sp.set_burst_enabled(true)
	sp.set_burst_count(4)
	sp.set_burst_interval_sec(0.05)
	sp.begin_burst()
	for i in 60:
		await physics()
		if get_signal_emit_count(sp, "burst_shot_fired") >= 4:
			break
	var n: int = get_signal_emit_count(sp, "burst_shot_fired")
	assert_eq(n, 4, "burst fired 4 shots")
	for i in n:
		# First shot plain, then alternate (same rhythm for odd and even counts).
		assert_eq(bool(get_signal_parameters(sp, "burst_shot_fired", i)[1]), i % 2 == 1, "shot %d mirrored iff odd index" % i)


func test_spiral_winding_reverses() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_SPIRAL, 12)
	sp.helper_spiral_start_radius = 20.0
	sp.helper_spiral_radius_step = 14.0
	sp.helper_spiral_angle_step = 0.7
	var pair: Dictionary = await _fire_burst_pair(sp)
	assert_not_null(pair["plain"], "plain spiral captured")
	assert_not_null(pair["mirrored"], "mirrored spiral captured")
	if pair["plain"] != null and pair["mirrored"] != null:
		var pw := _winding_sign(pair["plain"])
		var mw := _winding_sign(pair["mirrored"])
		assert_ne(pw, 0, "plain winding measurable")
		assert_ne(mw, 0, "mirrored winding measurable")
		assert_ne(pw, mw, "mirrored shot reverses the winding")


func test_multispiral_winding_reverses() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_MULTISPIRAL, 12)
	sp.helper_multispiral_arms = 2
	sp.helper_multispiral_start_radius = 20.0
	sp.helper_multispiral_radius_step = 14.0
	sp.helper_multispiral_angle_step = 0.6
	var pair: Dictionary = await _fire_burst_pair(sp)
	assert_true(pair["plain"] != null and pair["mirrored"] != null, "both captured")
	if pair["plain"] != null and pair["mirrored"] != null:
		assert_ne(_winding_sign(pair["plain"]), _winding_sign(pair["mirrored"]), "multispiral winding reverses")


func test_ring_unaffected_by_mirroring() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8)
	var still := H.make_directional_data(1, 0.0, 60.0)
	still.all_bullet_speed_data = []
	sp.set_spawn_data(still)
	sp.helper_ring_radius = 60.0
	var pair: Dictionary = await _fire_burst_pair(sp)
	for key in ["plain", "mirrored"]:
		var v: DirectionalBullets2D = pair[key]
		assert_not_null(v, "%s ring captured" % key)
		if v == null:
			continue
		assert_eq(v.get_amount_bullets(), 8)
		var radii: Array = []
		for i in v.get_amount_bullets():
			radii.append(v.get_bullet_transform(i).get_origin().length())
		assert_lt(radii.max() - radii.min(), 1.0, "%s ring stays circular" % key)


func test_distribute_degenerate_and_real_pools() -> void:
	var s5 := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	s5.set_homing_enabled(true)
	s5.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_DISTRIBUTE)
	s5.set_homing_max_targets(1)
	assert_eq(s5.get_homing_max_targets(), 1, "degenerate default reachable")
	assert_true(s5.shoot_once(), "DISTRIBUTE shot still fires with a 1-target pool")
	var s6 := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	s6.set_homing_enabled(true)
	s6.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_DISTRIBUTE)
	s6.set_homing_max_targets(4)
	assert_true(s6.shoot_once(), "DISTRIBUTE with a real pool fires")


func test_spin_preview_survives() -> void:
	var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_SPIRAL, 200)
	sp.show_preview_during_runtime = true
	sp.set_show_pattern_preview(true)
	sp.set_spin_enabled(true)
	sp.set_spin_speed_deg_per_sec(90.0)
	await idle(120)
	assert_eq(sp.debug_get_preview_dot_points().size(), 200, "200-dot spinning preview intact after 120 frames")
	assert_gt(absf(sp.get_spin_angle_deg()), 1.0, "spin advanced")
