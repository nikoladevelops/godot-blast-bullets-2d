extends BlastTest
## The README and QUICKSTART examples of lasers, emitter bullets, colors,
## lifetimes and scale-in, run as written (node names become test nodes): each
## does what its comment promises. A docs change that breaks an example, or an
## API change that breaks the docs, fails here.

const DT := 0.0625


## A LaserSpawner2D firing one beam (a 1-slot ring) at the spawner's spot.
func _laser_spawner(data: LaserData2D) -> LaserSpawner2D:
	var sp := LaserSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.pattern_source = LaserSpawner2D.PATTERN_FROM_HELPER_RING
	sp.helper_bullets_amount = 1
	sp.helper_ring_radius = 0.0
	sp.laser_data = data
	add(sp)
	sp.set_bullet_factory(factory)
	return sp


## The direction (radians) of the spawner's single slot.
func _slot_angle(sp: PatternSpawner2D) -> float:
	var t: Transform2D = sp.collect_spawn_transforms()[0]
	return t.x.angle()


func test_the_laser_example_burns_a_target_with_repeating_hits() -> void:
	var laser := LaserData2D.create_preset(LaserData2D.PRESET_CHARGED_SWEEP)
	laser.collision_mask = 2 # what it hits (bodies and areas)
	laser.wall_mask = 1 # what stops it
	laser.hit_interval_sec = 0.1 # laser_hit repeats while a target stays in the beam
	var boss_lasers := _laser_spawner(laser)
	var log: Array = []
	boss_lasers.laser_hit.connect(func(_target, _l, _beam_index, _position) -> void: log.append("hit"))
	boss_lasers.laser_pierce_started.connect(func(_target, _l, _beam_index, _position) -> void: log.append("start"))
	boss_lasers.laser_pierce_ended.connect(func(_target, _l, _beam_index) -> void: log.append("end"))
	# A long target across the beam 300 px out: the 90 deg sweep (+-45) never leaves it.
	var a := _slot_angle(boss_lasers)
	var enemy := make_area(Vector2.from_angle(a) * 300.0, Vector2(40, 2000), 2)
	enemy.rotation = a
	await idle(2)
	assert_true(boss_lasers.shoot_once(), "fires")
	var finished := []
	boss_lasers.laser_finished.connect(func(_l) -> void: finished.append(true))
	for i in 80: # charge 1 s + grow 0.15 + active 2 + fade 0.3 = 3.45 s of 5
		step_factory(1, DT)
		if not finished.is_empty():
			break
	assert_eq(finished.size(), 1, "the laser ran its course")
	assert_eq([log.count("start"), log.count("end"), log.front(), log.back()], [1, 1, "start", "end"], "one burn: started, then ended when the laser stopped")
	# A hit about every 0.1 s (every second 1/16 s tick) over the 2.15 s it hits.
	assert_between(log.count("hit"), 15, 23, "laser_hit repeats like damage ticks")


func test_the_lock_on_example_ends_right_on_the_player() -> void:
	var snipers := _laser_spawner(LaserData2D.create_preset(LaserData2D.PRESET_LOCK_ON))
	snipers.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_GROUP
	snipers.laser_target_group = &"player"
	var player := Node2D.new()
	player.position = Vector2(180, 140)
	add(player)
	player.add_to_group(&"player")
	var decoy := Node2D.new()
	decoy.position = Vector2(-600, 0) # farther: each beam aims at the nearest member
	add(decoy)
	decoy.add_to_group(&"player")
	var got: Array = []
	snipers.laser_fired.connect(func(l: LaserVolley2D, _n: int) -> void: got.append(l))
	assert_true(snipers.shoot_once(), "fires")
	var l: LaserVolley2D = got[0]
	for i in 30: # the 0.6 s warning, then the beam
		step_factory(1, DT)
		if l.get_phase() == LaserVolley2D.PHASE_ACTIVE:
			break
	assert_eq(l.get_phase(), LaserVolley2D.PHASE_ACTIVE, "firing")
	assert_true(l.get_beam_end(0).is_equal_approx(player.position), "the beam ends on the player")
	player.position = Vector2(-50, 200)
	step_factory(1, DT)
	assert_true(l.get_beam_end(0).is_equal_approx(player.position), "and stays on it")


func test_the_script_laser_example_follows_its_muzzle_and_fades_out() -> void:
	var laser := LaserData2D.create_preset(LaserData2D.PRESET_INSTANT_BEAM)
	var muzzle := Node2D.new()
	muzzle.position = Vector2(100, 100)
	add(muzzle)
	var beams: Array[Transform2D] = [Transform2D(0.0, muzzle.global_position)]
	var beam := factory.spawn_laser(laser, beams, muzzle)
	assert_not_null(beam, "spawned")
	step_factory(2, DT)
	muzzle.position = Vector2(160, 40)
	step_factory(1, DT)
	assert_eq(beam.get_beam_start(0), muzzle.position, "the beam rides its anchor")
	beam.end_laser() # fade out now
	step_factory(1, DT)
	assert_eq(beam.get_phase(), LaserVolley2D.PHASE_FADING, "fading")
	step_factory(4, DT) # the 0.15 s fade
	assert_true(beam.is_pooled(), "gone and pooled")
	var now := factory.spawn_laser(laser, beams, muzzle)
	step_factory(1, DT)
	now.end_laser(false) # at once
	step_factory(1, DT)
	assert_true(now.is_pooled(), "end_laser(false) ends at once")


func test_the_emitter_example_fires_the_template_twice_per_bullet_turning() -> void:
	var sparks := make_spawner(H.make_volley_data(4, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sparks.shooting_enabled = false # the template only fires from the bullets
	var boss := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	var timing := BulletEmitterData2D.new()
	timing.interval_sec = 0.3
	timing.max_shots = 2
	timing.spin_per_shot_degrees = 15.0 # each shot turns a little further: spirals
	boss.emitter = timing
	boss.emitter_spawner_path = boss.get_path_to(sparks)
	boss.emitter_enabled = true
	var shots: Array = []
	sparks.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: shots.append(v))
	assert_true(boss.shoot_once(), "fires")
	step_factory(24, DT) # 1.5 s: room for 5 shots, max_shots allows 2
	assert_eq(shots.size(), 2, "twice per bullet")
	assert_eq([shots[0].get_amount_bullets(), shots[1].get_amount_bullets()], [4, 4], "the template's whole pattern each time")
	var turn := rad_to_deg(angle_difference(shots[0].get_bullet_direction(0).angle(), shots[1].get_bullet_direction(0).angle()))
	assert_almost_eq(turn, 15.0, 0.001, "the second shot turned by spin_per_shot_degrees")


func test_an_emitter_template_can_be_a_laser_spawner() -> void:
	var lasers := _laser_spawner(LaserData2D.create_preset(LaserData2D.PRESET_INSTANT_BEAM))
	var boss := make_spawner(H.make_volley_data(2, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	var timing := BulletEmitterData2D.new()
	timing.max_shots = 1
	boss.emitter = timing
	boss.emitter_spawner_path = boss.get_path_to(lasers) # bullets that fire lasers
	boss.emitter_enabled = true
	assert_true(boss.shoot_once(), "fires")
	step_factory(10, DT) # the default 0.5 s interval
	assert_eq(lasers.get_live_laser_count(), 2, "every flying bullet fired a laser")


func test_the_colors_lifetimes_and_scale_in_example() -> void:
	var data := H.make_volley_data(8, 0.0, 4.0)
	data.scale_in_sec = 0.15 # bullets pop in from 10 % of their size
	var boss := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8)
	boss.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_ACROSS_PATTERN # a rainbow ring
	boss.bullet_color_shift_per_shot = 0.1 # and it turns from shot to shot
	boss.bullet_life_time_scale_min = 0.5 # every bullet lives 0.5x .. 1.5x max_life_time
	boss.bullet_life_time_scale_max = 1.5
	var fired: Array = []
	boss.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: fired.append(v))
	assert_true(boss.shoot_once(), "fires")
	var volley: BulletVolley2D = fired[0]
	var hues: Array = []
	var lives: Array = []
	for i in 8:
		hues.append(snappedf(volley.get_bullet_color(i).h, 0.001))
		lives.append(volley.get_bullet_life_time_left(i))
	var distinct := {}
	for h in hues:
		distinct[h] = true
	assert_eq(distinct.size(), 8, "a rainbow: every bullet its own hue")
	assert_true(lives.all(func(t: float) -> bool: return t >= 2.0 - 0.0001 and t <= 6.0 + 0.0001), "lifetimes within 0.5x .. 1.5x of 4 s: %s" % [lives])
	var spread := {}
	for t in lives:
		spread[snappedf(t, 0.001)] = true
	assert_gt(spread.size(), 4, "and spread out, one roll per bullet: %s" % [lives])
	assert_almost_eq(volley.get_bullet_transform(0).get_scale().x, 0.1, 0.0001, "spawned at 10 % size")
	step_factory(3, DT) # 0.1875 s > scale_in_sec
	assert_almost_eq(volley.get_bullet_transform(0).get_scale().x, 1.0, 0.0001, "full size")
	volley.set_bullet_color(0, Color.GOLD)
	assert_eq(volley.get_bullet_color(0), Color.GOLD, "a runtime color")
	volley.set_bullet_life_time_left(0, 2.0) # this bullet lives 2 more seconds
	assert_almost_eq(volley.get_bullet_life_time_left(0), 2.0, 0.0001, "a runtime lifetime")
	assert_true(boss.shoot_once(), "a second shot")
	var second: BulletVolley2D = fired[1]
	assert_ne(snappedf(second.get_bullet_color(0).h, 0.001), hues[0], "the next shot's rainbow turned by bullet_color_shift_per_shot")
	var plain := factory.spawn_volley(H.make_volley_data(2, 0.0, 4.0))
	plain.set_bullet_color(0, Color.GOLD)
	expect_error_sequence(["set_bullet_color: this volley has no per-bullet colors (spawn it with BulletVolleyData2D.use_bullet_colors on), keeping the old color."])
