extends BlastTest
## LaserSpawner2D: PatternSpawner2D's patterns and cadence, firing one
## LaserVolley2D per shot (a beam per slot) instead of bullets. Its groups
## (Lasers, Laser Targeting) come first; a shot needs laser_data and respects
## max_live_lasers; the muzzles ride the spawner (moves, spin, teleports); the
## laser signals fire on the spawner, never the factory (grazes bubble on);
## Track / Lock On beams aim at the Laser Targeting group's targets (nearest
## per beam, filter group, path, the mouse every frame, a position, re-picked
## on an interval); orphaned_lasers decides what its lasers do when it is
## freed; pattern lists swap laser_data; emitter bullets can fire lasers.

const DT := 0.0625
const SCENE_PATH := "user://laser_spawner_round_trip.tscn"


func _laser(active := 0.0) -> LaserData2D:
	return H.make_laser(0.0, 0.0, active, 0.0, 200.0)


func _spawner(data: LaserData2D = null, amount := 4) -> LaserSpawner2D:
	var sp := LaserSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.laser_data = data if data != null else _laser()
	sp.pattern_source = LaserSpawner2D.PATTERN_FROM_HELPER_RING
	sp.helper_bullets_amount = amount
	sp.helper_ring_radius = 20.0
	add(sp)
	sp.set_bullet_factory(factory)
	return sp


func _fire(sp: LaserSpawner2D) -> LaserVolley2D:
	var got: Array = []
	sp.laser_fired.connect(func(l: LaserVolley2D, _n: int) -> void: got.append(l), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fires")
	return got[0] if got.size() > 0 else null


func test_its_groups_come_first_and_gate_the_targeting() -> void:
	var sp := _spawner()
	assert_true(sp is PatternSpawner2D, "a PatternSpawner2D")
	var groups: Array = []
	for p in sp.get_property_list():
		if int(p["usage"]) & PROPERTY_USAGE_GROUP:
			groups.append(str(p["name"]))
	assert_eq(groups.slice(0, 3), ["Lasers", "Laser Targeting", "Setup"], "its own groups, then the base's")
	assert_false("Homing" in groups, "no bullet groups")
	var shown := func() -> Array:
		var out: Array = []
		for n in [&"laser_target_group", &"laser_target_filter_group", &"laser_target_path", &"laser_target_position", &"laser_retarget_interval_sec"]:
			out.append(is_editor_visible(sp, n))
		return out
	assert_eq(shown.call(), [false, false, false, false, false], "None: nothing")
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_GROUP
	assert_eq(shown.call(), [true, true, false, false, true], "Node Group")
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_PATH
	assert_eq(shown.call(), [false, true, true, false, true], "Node Path")
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_GLOBAL_POSITION
	assert_eq(shown.call(), [false, false, false, true, false], "Global Position (a point never needs re-picking)")


func test_a_shot_fires_one_laser_with_a_beam_per_slot() -> void:
	var sp := _spawner(_laser(), 4)
	sp.position = Vector2(100, 100)
	var l := _fire(sp)
	assert_eq(l.get_beam_count(), 4, "a beam per ring slot")
	assert_eq(l.get_owner_spawner(), sp, "owned by the spawner")
	for i in 4:
		assert_almost_eq(l.get_beam_start(i).distance_to(Vector2(100, 100)), 20.0, 0.01, "beam %d on the ring" % i)
		assert_almost_eq(angle_difference((l.get_beam_start(i) - Vector2(100, 100)).angle(), l.get_beam_angle(i)), 0.0, 0.001, "pointing outward")
	assert_eq(sp.get_live_lasers(), [l], "the live readout")


func test_refusals_no_data_and_the_live_cap() -> void:
	var sp := _spawner()
	sp.laser_data = null
	watch_signals(sp)
	assert_false(sp.shoot_once(), "no data: no shot")
	expect_error_sequence(["LaserSpawner2D::shoot_once: no laser_data assigned."])
	assert_signal_emitted_with_parameters(sp, "volley_skipped", [&"no_laser_data"])
	sp.laser_data = _laser()
	sp.max_live_lasers = 1
	assert_true(sp.shoot_once(), "the first fires")
	assert_false(sp.shoot_once(), "the cap holds the second")
	assert_signal_emitted_with_parameters(sp, "volley_skipped", [&"over_budget"])
	sp.max_live_lasers = -1
	expect_error_sequence(["LaserSpawner2D: max_live_lasers must be >= 0 (0 = no limit), keeping the old value."])


func test_auto_fire_keeps_its_cadence() -> void:
	var sp := _spawner(H.make_laser(0.0, 0.0, 0.05, 0.0, 100.0), 1)
	sp.shoot_interval_sec = 0.25
	var fired: Array = []
	sp.laser_fired.connect(func(_l, n: int) -> void: fired.append(n))
	sp.set_shooting_enabled(true)
	await idle(62) # about a second
	assert_between(fired.size(), 4, 6, "every 0.25 s")


func test_the_muzzles_ride_the_spawner() -> void:
	var sp := _spawner(_laser(), 2)
	var l := _fire(sp)
	sp.position = Vector2(50, 0)
	sp.set_spin_angle_deg(90.0)
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_start(0).distance_to(Vector2(50, 20)), 0.0, 0.01, "moved and spun: slot 0 (+X) turned to +Y")
	assert_almost_eq(rad_to_deg(l.get_beam_angle(0)), 90.0, 0.01, "and pointing down")
	sp.teleport(Vector2(300, 300))
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_start(0).distance_to(Vector2(300, 320)), 0.0, 0.01, "a teleport takes the muzzles along")


func test_signals_fire_on_the_spawner_never_the_factory() -> void:
	make_area(Vector2(100, 0), Vector2(20, 60))
	await idle(2)
	var sp := _spawner(H.make_laser(0.0, 0.0, 0.125, 0.0, 200.0), 1)
	var spawner_log: Array = []
	var factory_log: Array = []
	for s in ["laser_phase_changed", "laser_pierce_started", "laser_hit", "laser_pierce_ended", "laser_finished"]:
		var sig: String = s
		var n := 0
		for d in sp.get_signal_list():
			if str(d.name) == sig:
				n = (d.args as Array).size()
		sp.connect(sig, (func() -> void: spawner_log.append(sig)).unbind(n))
		factory.connect(sig, (func() -> void: factory_log.append(sig)).unbind(n))
	_fire(sp)
	step_factory(4, DT)
	assert_eq(spawner_log, ["laser_phase_changed", "laser_phase_changed", "laser_pierce_started", "laser_hit", "laser_phase_changed", "laser_phase_changed", "laser_pierce_ended", "laser_finished"], "every signal on the spawner")
	assert_eq(factory_log, [], "the factory stays silent")


func test_targeting_aims_track_and_lock_on_beams() -> void:
	var near := Node2D.new()
	near.position = Vector2(0, 100)
	near.add_to_group(&"laser_foes")
	add(near)
	var far := Node2D.new()
	far.position = Vector2(0, -300)
	far.add_to_group(&"laser_foes")
	add(far)
	var d := _laser()
	d.aim = LaserData2D.LASER_AIM_LOCK_ON
	var sp := _spawner(d, 1)
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_GROUP
	sp.laser_target_group = &"laser_foes"
	assert_eq(sp.resolve_laser_targets(), [near, far], "every member (THE target filter)")
	var l := _fire(sp)
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_end(0).distance_to(Vector2(0, 100)), 0.0, 0.01, "locked onto the nearest member")
	far.add_to_group(&"vip")
	sp.laser_target_filter_group = &"vip"
	assert_eq(sp.retarget_live_lasers(), 1, "one laser retargeted")
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_angle(0), (far.position - l.get_beam_start(0)).angle(), 0.0001, "the filter group leaves only the far one")
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_GLOBAL_POSITION
	sp.laser_target_position = Vector2(-50, 0)
	sp.retarget_live_lasers()
	step_factory(1, DT)
	assert_almost_eq(l.get_beam_end(0).distance_to(Vector2(-50, 0)), 0.0, 0.01, "a position")
	sp.laser_target_position = Vector2(INF, 0)
	expect_error_sequence(["LaserSpawner2D: laser_target_position must be finite, keeping the old value."])


func test_the_retarget_interval_and_the_mouse_follow_their_targets() -> void:
	var foe := Node2D.new()
	foe.position = Vector2(100, 0)
	foe.add_to_group(&"laser_foes")
	add(foe)
	var d := _laser()
	d.aim = LaserData2D.LASER_AIM_LOCK_ON
	var sp := _spawner(d, 1)
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_GROUP
	sp.laser_target_group = &"laser_foes"
	sp.laser_retarget_interval_sec = 0.1
	var l := _fire(sp)
	var other := Node2D.new()
	other.position = Vector2(0, 30)
	other.add_to_group(&"laser_foes")
	add(other)
	await idle(10) # past the interval: the nearer newcomer is picked
	assert_almost_eq(l.get_beam_end(0).distance_to(Vector2(0, 30)), 0.0, 0.5, "re-picked on the interval")
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_MOUSE
	mouse_to(Vector2(0, -120))
	await idle(2)
	assert_almost_eq(l.get_beam_end(0).distance_to(Vector2(0, -120)), 0.0, 0.5, "the mouse, every frame")


func test_orphan_policies(policy: int = use_parameters([0, 1, 2, 3])) -> void:
	var sp := _spawner(H.make_laser(0.0, 0.0, 0.0, 0.25), 1)
	sp.orphaned_lasers = policy
	var l := _fire(sp)
	var heard: Array = []
	factory.laser_phase_changed.connect(func(_l, p: int) -> void: heard.append(p))
	sp.free()
	step_factory(1, DT)
	match policy:
		LaserSpawner2D.ORPHANED_LASERS_FADE_OUT:
			assert_eq(l.get_phase(), LaserVolley2D.PHASE_FADING, "fades out")
		LaserSpawner2D.ORPHANED_LASERS_KEEP_FIRING:
			assert_eq(l.get_phase(), LaserVolley2D.PHASE_ACTIVE, "keeps firing")
			assert_eq(heard, [], "silently")
		LaserSpawner2D.ORPHANED_LASERS_HAND_TO_FACTORY:
			assert_eq(l.get_phase(), LaserVolley2D.PHASE_ACTIVE, "keeps firing")
			assert_eq(heard, [LaserVolley2D.PHASE_GROWING, LaserVolley2D.PHASE_ACTIVE], "signalling on the factory (its first tick ran after the free)")
		LaserSpawner2D.ORPHANED_LASERS_REMOVE:
			assert_true(l.is_pooled(), "gone at once")


func test_grazes_bubble_from_the_spawner_to_the_factory() -> void:
	var t := Node2D.new()
	t.position = Vector2(100, 40) # 40 px beside a +X beam
	t.add_to_group(&"laser_grazers")
	add(t)
	var sp := _spawner(_laser(), 1)
	sp.graze_enabled = true
	sp.graze_zones = [H.make_graze_zone([50.0, 30.0])]
	sp.graze_node_group = &"laser_grazers"
	var order: Array = []
	sp.laser_grazed.connect(func(_t, _l, _b, _z, ring: int) -> void: order.append("spawner:%d" % ring))
	factory.laser_grazed.connect(func(_t, _l, _b, _z, ring: int) -> void: order.append("factory:%d" % ring))
	var l := _fire(sp)
	step_factory(2, DT)
	assert_eq(order, ["spawner:0", "factory:0"], "the 50 px ring once (the 30 px ring is out of reach), spawner first")
	assert_true(l.is_graze_armed(), "armed at the shot")


func test_sound_entries_offer_on_hits() -> void:
	make_area(Vector2(100, 0), Vector2(20, 60))
	await idle(2)
	factory.debug_set_sound_offer_log_enabled(true)
	var sp := _spawner(_laser(), 1)
	sp.sound_enabled = true
	sp.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_HIT)]
	_fire(sp)
	step_factory(1, DT)
	var offers: Array = factory.debug_get_sound_offer_log()
	assert_eq(offers.size(), 1, "one offer for the pierce")
	assert_eq(offers[0]["trigger"], BulletSoundData2D.SOUND_ON_HIT, "On Hit")


func test_pattern_lists_swap_the_laser_data() -> void:
	var sp := _spawner(_laser(), 1)
	var long := _laser()
	long.length = 500.0
	var fired: Array = []
	sp.laser_fired.connect(func(l: LaserVolley2D, _n: int) -> void: fired.append(l.get_laser_data()))
	assert_true(sp.shoot_once_with({"laser_data": long}), "an override shot")
	assert_eq(fired, [long], "fired with the entry's data")
	assert_ne(sp.laser_data, long, "restored afterwards")
	sp.shoot_once_with({"laser_dat": long})
	expect_error_sequence(["LaserSpawner2D::shoot_once_with: unknown entry key 'laser_dat' (did you mean 'laser_data'?); valid keys: preset, pattern_source, helper_bullets_amount, laser_data. Ignoring it."])


func test_setup_warnings_and_explain() -> void:
	var sp := _spawner()
	sp.laser_data = null
	assert_true("laser_data is empty: the spawner fires nothing. Assign a LaserData2D." in sp.get_setup_warnings(), "no data")
	var d := _laser()
	d.aim = LaserData2D.LASER_AIM_TRACK
	sp.laser_data = d
	assert_true("laser_data aims with Track or Lock On but laser_target_source is None: the beams keep their pattern direction." in sp.get_setup_warnings(), "no source")
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_GROUP
	assert_true("laser_target_source is Node Group but laser_target_group is empty: the lasers have no target." in sp.get_setup_warnings(), "no group")
	var lines: PackedStringArray = sp.debug_explain_features()
	assert_true("Lasers: every shot fires a beam per pattern slot, 200 px long, active until ended." in lines, str(lines))
	assert_true("Laser Targeting: each beam turns toward the nearest member of the target group." in lines, str(lines))


func test_a_saved_scene_keeps_its_settings() -> void:
	var sp := _spawner()
	sp.orphaned_lasers = LaserSpawner2D.ORPHANED_LASERS_REMOVE
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_PATH
	sp.laser_retarget_interval_sec = 0.5
	sp.max_live_lasers = 3
	var holder := Node2D.new()
	add(holder)
	sp.reparent(holder)
	sp.owner = holder
	var scene := PackedScene.new()
	assert_eq(scene.pack(holder), OK, "packed")
	assert_eq(ResourceSaver.save(scene, SCENE_PATH), OK, "saved")
	var loaded := ResourceLoader.load(SCENE_PATH, "", ResourceLoader.CACHE_MODE_IGNORE) as PackedScene
	DirAccess.remove_absolute(ProjectSettings.globalize_path(SCENE_PATH))
	var copy := loaded.instantiate()
	var back := copy.get_child(0) as LaserSpawner2D
	assert_not_null(back, "the spawner came back")
	assert_eq([back.orphaned_lasers, back.laser_target_source, back.laser_retarget_interval_sec, back.max_live_lasers], [LaserSpawner2D.ORPHANED_LASERS_REMOVE, LaserSpawner2D.LASER_TARGET_NODE_PATH, 0.5, 3], "settings")
	assert_eq(back.laser_data.length, 200.0, "its laser data")
	copy.free()


func test_emitter_bullets_fire_lasers() -> void:
	var tpl := _spawner(H.make_laser(0.0, 0.0, 0.05, 0.0, 50.0), 1)
	var lasers: Array = []
	tpl.laser_fired.connect(func(l: LaserVolley2D, _n: int) -> void: lasers.append(l))
	var bullets := make_spawner(H.make_volley_data(1, 160.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	var timing := BulletEmitterData2D.new()
	timing.interval_sec = 0.25
	bullets.emitter_enabled = true
	bullets.emitter = timing
	bullets.emitter_spawner_path = bullets.get_path_to(tpl)
	var fired: Array = [] # a lambda captures locals by value: collect through an Array
	bullets.volley_fired.connect(func(vol: BulletVolley2D, _n: int) -> void: fired.append(vol), CONNECT_ONE_SHOT)
	bullets.shoot_once()
	var v: BulletVolley2D = fired[0]
	step_factory(1, DT)
	assert_eq(lasers.size(), 1, "the flying bullet fired the laser spawner's pattern")
	assert_almost_eq(lasers[0].get_beam_start(0).distance_to(v.get_bullet_transform(0).origin), 20.0, 0.5, "around the bullet, not the template")
	assert_null(lasers[0].get_anchor(), "an emitter shot never follows the template")


func test_the_preview_draws_each_slot_as_its_beam() -> void:
	var d := _laser() # 200 px
	var sp := _spawner(d, 4)
	sp.show_pattern_preview = true
	sp.show_preview_during_runtime = true
	await idle(6) # the deferred preview rebuild
	var arrows: Dictionary = sp.debug_get_preview_arrows()
	assert_eq((arrows["tails"] as PackedVector2Array).size(), 4, "one beam per slot")
	assert_almost_eq(float(arrows["length"]), 200.0, 0.001, "drawn at the laser's length")
	var l := _fire(sp)
	step_factory(1, DT)
	var dots := sp.debug_get_preview_dot_points()
	var dirs: PackedVector2Array = arrows["dirs"]
	var off: Array = []
	for i in 4:
		if not dots[i].is_equal_approx(l.get_beam_start(i)) or not is_equal_approx(angle_difference(dirs[i].angle(), l.get_beam_angle(i)), 0.0):
			off.append(i)
	assert_eq(off, [], "every preview beam starts and points where its beam fires")
	d.length = 350.0
	await idle(6)
	assert_almost_eq(float(sp.debug_get_preview_arrows()["length"]), 350.0, 0.001, "a laser_data edit redraws")
	sp.laser_data = LaserData2D.create_preset(LaserData2D.PRESET_WALL_BEAM)
	await idle(6)
	assert_almost_eq(float(sp.debug_get_preview_arrows()["length"]), 1200.0, 0.001, "a new laser_data redraws")
	sp.laser_data = null
	await idle(6)
	assert_almost_eq(float(sp.debug_get_preview_arrows()["length"]), sp.preview_arrow_length, 0.001, "no laser: the plain facing arrows")
	var bullets := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	await idle(6)
	assert_almost_eq(float(bullets.debug_get_preview_arrows()["length"]), bullets.preview_arrow_length, 0.001, "bullet spawners keep preview_arrow_length")


func test_a_teleport_snaps_following_beams_without_a_slide() -> void:
	var sp := _spawner(_laser(), 1)
	var l := _fire(sp)
	step_factory(2, DT)
	var start := l.get_beam_start(0)
	sp.teleport(Vector2(500, 300))
	var jump := Vector2(500, 300)
	var now: PackedVector2Array = l.debug_get_draw_state(0.5)[0]["points"]
	assert_true(now[0].is_equal_approx(start + jump), "drawn at the new spot at once (mid-frame too)")
	step_factory(1, DT)
	var mid: PackedVector2Array = l.debug_get_draw_state(0.5)[0]["points"]
	assert_true(mid[0].is_equal_approx(start + jump), "the next tick never lerps across the jump")


func test_teleport_carry_moves_lasers_that_do_not_follow() -> void:
	var d := _laser()
	d.follow_spawner = false
	var sp := _spawner(d, 1)
	var l := _fire(sp)
	step_factory(1, DT)
	var start := l.get_beam_start(0)
	sp.teleport(Vector2(100, 0))
	step_factory(1, DT)
	assert_eq(l.get_beam_start(0), start, "without carry a laser that does not follow stays")
	sp.teleport(Vector2(100, 50), true)
	assert_true(l.get_beam_start(0).is_equal_approx(start + Vector2(0, 50)), "carried at once")
	step_factory(1, DT)
	assert_true(l.get_beam_start(0).is_equal_approx(start + Vector2(0, 50)), "and it stays carried")
	var mid: PackedVector2Array = l.debug_get_draw_state(0.5)[0]["points"]
	assert_true(mid[0].is_equal_approx(start + Vector2(0, 50)), "no slide")


func test_teleport_carry_moves_flying_bolts() -> void:
	var d := _laser()
	d.type = LaserData2D.LASER_BOLT
	d.speed = 320.0 # 20 px per tick
	var sp := _spawner(d, 1)
	var l := _fire(sp)
	step_factory(3, DT)
	var head := l.get_beam_end(0)
	sp.teleport(Vector2(0, 200))
	assert_eq(l.get_beam_end(0), head, "a flying bolt does not follow a teleport")
	sp.teleport(Vector2(0, 260), true)
	assert_true(l.get_beam_end(0).is_equal_approx(head + Vector2(0, 60)), "carry moves it")
	step_factory(1, DT)
	assert_true(l.get_beam_end(0).is_equal_approx(head + Vector2(20, 60)), "and it flies on from there")


func test_apply_to_live_lasers_arms_disarms_and_retargets() -> void:
	var t := Node2D.new()
	t.position = Vector2(100, 40)
	t.add_to_group(&"laser_grazers")
	add(t)
	var d := _laser()
	var sp := _spawner(d, 2)
	var l := _fire(sp)
	assert_false(l.is_graze_armed(), "fired unarmed")
	sp.graze_zones = [H.make_graze_zone([50.0])]
	sp.graze_node_group = &"laser_grazers"
	sp.graze_enabled = true
	assert_false(l.is_graze_armed(), "spawner settings alone never reach a laser in flight")
	assert_eq(sp.apply_to_live_lasers(LaserSpawner2D.LIVE_GRAZE), 1, "armed now")
	assert_true(l.is_graze_armed(), "armed")
	assert_eq(sp.apply_to_live_lasers(LaserSpawner2D.LIVE_GRAZE), 0, "armed alike: untouched")
	var grazes: Array = []
	sp.laser_grazed.connect(func(_t, _l, b: int, _z, _r) -> void: grazes.append(b))
	step_factory(1, DT)
	assert_eq(grazes, [0], "and it grazes")
	sp.sound_enabled = true
	sp.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_HIT)]
	assert_eq(sp.apply_to_live_lasers(LaserSpawner2D.LIVE_SOUND), 1, "sound armed")
	assert_true(l.is_sound_armed(), "sound armed")
	sp.graze_enabled = false
	assert_eq(sp.apply_to_live_lasers(), 1, "LIVE_ALL: graze disarmed, sound already alike")
	assert_eq([l.is_graze_armed(), l.is_sound_armed()], [false, true], "only graze changed")
	d.aim = LaserData2D.LASER_AIM_LOCK_ON
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_GLOBAL_POSITION
	sp.laser_target_position = Vector2(0, 300)
	assert_eq(sp.apply_to_live_lasers(LaserSpawner2D.LIVE_HOMING), 1, "retargeted")
	step_factory(1, DT)
	var off := angle_difference((Vector2(0, 300) - l.get_beam_start(0)).angle(), l.get_beam_angle(0))
	assert_almost_eq(off, 0.0, 0.0001, "the lock-on beam aims at the new target")
	l.end_laser(false)
	step_factory(1, DT)
	assert_eq(sp.apply_to_live_lasers(), 0, "no live laser: nothing to apply")


func _named(n: String, pos: Vector2, parent: Node = null) -> Node2D:
	var node := Node2D.new()
	node.name = n
	node.position = pos
	if parent != null:
		parent.add_child(node)
	else:
		add(node)
	return node


## Two beams (+X from (20, 0), -X from (-20, 0)), Lock On, 1000 px.
func _lock_on_spawner() -> LaserSpawner2D:
	var d := _laser()
	d.length = 1000.0
	d.aim = LaserData2D.LASER_AIM_LOCK_ON
	return _spawner(d, 2)


func test_the_node_name_source_aims_each_beam_at_the_nearest_named_node() -> void:
	var a := _named("EnemyA", Vector2(300, 0))
	var b := _named("EnemyB", Vector2(0, 300))
	_named("Other", Vector2(25, 0))
	var sp := _lock_on_spawner()
	_named("EnemyMarker", Vector2(-20, 5), sp) # under the spawner: never a target
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_NAME
	sp.laser_target_node_name = "enemy" # Contains, case-insensitive (the defaults)
	assert_eq(sp.resolve_laser_targets(), [a, b], "the named nodes, in tree order")
	var l := _fire(sp)
	step_factory(1, DT)
	assert_true(l.get_beam_end(0).distance_to(a.position) < 0.01 and l.get_beam_end(1).distance_to(b.position) < 0.01, "each beam ends on its nearest: %s" % [[l.get_beam_end(0), l.get_beam_end(1)]])
	sp.laser_target_name_case_sensitive = true
	assert_eq(sp.resolve_laser_targets(), [], "case-sensitive: \"enemy\" matches nothing")
	sp.laser_target_name_match_mode = PatternSpawner2D.HOMING_NAME_MATCH_EXACT
	sp.laser_target_name_case_sensitive = false
	sp.laser_target_node_name = "enemya"
	assert_eq(sp.resolve_laser_targets(), [a], "Exact")
	sp.laser_target_name_match_mode = 9
	expect_error_sequence(["LaserSpawner2D: invalid laser_target_name_match_mode, keeping the old value."])
	assert_eq(sp.laser_target_name_match_mode, PatternSpawner2D.HOMING_NAME_MATCH_EXACT, "kept")


func test_the_node_children_source_aims_at_the_nearest_child() -> void:
	var squad := _named("Squad", Vector2(0, 0))
	var a := _named("A", Vector2(300, 0), squad)
	var b := _named("B", Vector2(0, 300), squad)
	var deep := _named("Deep", Vector2(0, 30), a) # a grandchild: (300, 30)
	var sp := _lock_on_spawner()
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_CHILDREN
	sp.laser_target_children_parent_path = sp.get_path_to(squad)
	assert_eq(sp.resolve_laser_targets(), [a, b], "the direct children")
	sp.laser_target_children_recursive = true
	assert_eq(sp.resolve_laser_targets(), [a, deep, b], "recursive: depth-first, tree order")
	var l := _fire(sp)
	step_factory(1, DT)
	assert_true(l.get_beam_end(0).distance_to(a.position) < 0.01, "the nearest child")
	sp.laser_target_children_parent_path = NodePath(".")
	assert_eq(sp.resolve_laser_targets(), [], "the spawner itself as the parent: nothing")
	deep.add_to_group(&"laser_squad")
	sp.laser_target_children_parent_path = sp.get_path_to(squad)
	sp.laser_target_filter_group = &"laser_squad"
	assert_eq(sp.resolve_laser_targets(), [deep], "the filter group applies")


func test_the_new_sources_gate_their_knobs_and_warn_when_empty() -> void:
	var sp := _lock_on_spawner()
	var shown := func() -> Array:
		var out: Array = []
		for n in [&"laser_target_node_name", &"laser_target_name_match_mode", &"laser_target_name_case_sensitive", &"laser_target_children_parent_path", &"laser_target_children_recursive", &"laser_target_filter_group", &"laser_retarget_interval_sec"]:
			out.append(is_editor_visible(sp, n))
		return out
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_NAME
	assert_eq(shown.call(), [true, true, true, false, false, true, true], "Node Name")
	assert_true("laser_target_source is Node Name but laser_target_node_name is empty: the lasers have no target." in sp.get_setup_warnings(), "empty name")
	sp.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_CHILDREN
	assert_eq(shown.call(), [false, false, false, true, true, true, true], "Node Children")
	assert_true("laser_target_source is Node Children but laser_target_children_parent_path is empty: the lasers have no target." in sp.get_setup_warnings(), "empty parent")
	assert_eq([LaserSpawner2D.LASER_TARGET_NODE_NAME, LaserSpawner2D.LASER_TARGET_NODE_CHILDREN], [5, 6], "serialized ids appended")
	sp.laser_target_source = 7
	expect_error_sequence(["LaserSpawner2D: laser_target_source must be 0 (None), 1 (Node Group), 2 (Node Path), 3 (Mouse), 4 (Global Position), 5 (Node Name) or 6 (Node Children), keeping the old value."])


## Every beam stays where it fired when the spawner it rode is gone: a lost
## anchor converts EVERY beam to its world pose, never only the first (the
## others used to fall back to their spawner-local slot, near the origin).
func test_every_beam_keeps_its_pose_when_the_spawner_is_freed(policy: int = use_parameters([0, 1, 2])) -> void:
	var sp := _spawner(H.make_laser(0.0, 0.0, 0.0, 0.5, 200.0), 4)
	sp.position = Vector2(300, 300)
	sp.orphaned_lasers = policy
	var l := _fire(sp)
	step_factory(1, DT)
	var before: Array = []
	for i in 4:
		before.append(l.get_beam_start(i))
	sp.free()
	step_factory(2, DT)
	var moved: Array = []
	for i in 4:
		if l.get_beam_start(i).distance_to(before[i]) > 0.01:
			moved.append([i, before[i], l.get_beam_start(i)])
	assert_eq(moved, [], "policy %d: every beam stays on its muzzle" % policy)
