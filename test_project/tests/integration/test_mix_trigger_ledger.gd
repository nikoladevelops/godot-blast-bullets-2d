extends BlastTest
## Every way a bullet leaves, and every event it lives through, fires exactly
## its own sound trigger and effect trigger once per bullet (checked against
## the factory's bullet event log, sound offer log and effect log through
## common/mix_ledger.gd): a killing hit On Destroy, a survived hit On Hit,
## an expiry On Lifetime Over, a despawn On Despawn, every clear On Clear, a
## pickup conversion On Clear only when it fires clear effects, a split On
## Split (plus the death trigger unless the stage silences it), a bounce, a
## homing reach and a graze their own; a script's disable_bullet, a reset and
## a free stay silent. Spawner shots and warnings offer On Shot / On
## Telegraph once each, through bursts, telegraphs, pattern lists and
## fire_n_volleys.

const N := 3

var ledger: MixLedger


func before_each() -> void:
	await super()
	ledger = MixLedger.new()
	ledger.attach(factory)


func after_each() -> void:
	ledger.detach()
	await super()


func _volley(speed := 0.0, lifetime := 30.0, setup := Callable()) -> BulletVolley2D:
	var d := H.make_volley_data(N, speed, lifetime)
	var arr: Array = []
	for i in N:
		arr.append(Transform2D(0.0, Vector2(0, 30.0 * i)))
	d.transforms = arr
	d.collision_shape = H.make_circle_shape(4.0)
	var layers: Array = []
	for trigger in [BulletEffectLayerData2D.EFFECT_ON_SPAWN, BulletEffectLayerData2D.EFFECT_ON_HIT, BulletEffectLayerData2D.EFFECT_ON_DESTROY, BulletEffectLayerData2D.EFFECT_ON_BOUNCE, BulletEffectLayerData2D.EFFECT_ON_LIFETIME_OVER, BulletEffectLayerData2D.EFFECT_ON_CLEAR, BulletEffectLayerData2D.EFFECT_ON_DESPAWN, BulletEffectLayerData2D.EFFECT_ON_SPLIT]:
		layers.append(H.make_effect_layer(trigger))
	d.effect_layers = layers
	if setup.is_valid():
		setup.call(d)
	var v: BulletVolley2D = factory.spawn_volley(d)
	var sounds: Array = []
	for trigger in 13:
		# On Flight is left out: the ledger counts it per physics frame, and
		# step_factory runs many ticks in one (the feature mix covers it).
		if trigger != BulletSoundData2D.SOUND_ON_FLIGHT:
			sounds.append(H.make_sound(trigger, 8))
	v.sound_set_effects(sounds)
	ledger.watch_volley(v)
	return v


func _count(kind: String) -> int:
	ledger.pull()
	return int(ledger.counts.get(kind, 0))


## Exactly these counts of offers ("s<trigger>"), effects ("e<trigger>")
## and disables ("d<cause>"); every other death trigger 0.
func _expect(label: String, want: Dictionary) -> void:
	var problems: Array = ledger.problems()
	var death_sounds := [BulletSoundData2D.SOUND_ON_DESTROY, BulletSoundData2D.SOUND_ON_LIFETIME_OVER, BulletSoundData2D.SOUND_ON_CLEAR, BulletSoundData2D.SOUND_ON_DESPAWN, BulletSoundData2D.SOUND_ON_HIT, BulletSoundData2D.SOUND_ON_BOUNCE, BulletSoundData2D.SOUND_ON_SPLIT, BulletSoundData2D.SOUND_ON_HOMING_TARGET_REACHED]
	var death_effects := [BulletEffectLayerData2D.EFFECT_ON_DESTROY, BulletEffectLayerData2D.EFFECT_ON_LIFETIME_OVER, BulletEffectLayerData2D.EFFECT_ON_CLEAR, BulletEffectLayerData2D.EFFECT_ON_DESPAWN, BulletEffectLayerData2D.EFFECT_ON_HIT, BulletEffectLayerData2D.EFFECT_ON_BOUNCE, BulletEffectLayerData2D.EFFECT_ON_SPLIT]
	var causes := ["hit", "lifetime", "despawn", "clear", "split", "pickup", "collect", "manual"]
	for t in death_sounds:
		var got := _count("offer:%d" % t)
		var exp := int(want.get("s%d" % t, 0))
		if got != exp:
			problems.append("%s: %d sound offers of trigger %d, expected %d" % [label, got, t, exp])
	for t in death_effects:
		var got := _count("effect:%d" % t)
		var exp := int(want.get("e%d" % t, 0))
		if got != exp:
			problems.append("%s: %d effects of trigger %d, expected %d" % [label, got, t, exp])
	for c in causes:
		var got := _count("disable:" + c)
		var exp := int(want.get("d" + c, 0))
		if got != exp:
			problems.append("%s: %d bullets left by '%s', expected %d" % [label, got, c, exp])
	assert_eq("\n".join(problems), "", label)


func test_a_killing_and_a_survived_hit() -> void:
	make_wall(Vector2(100, 30), Vector2(20, 100), 4)
	var kill := func(d: BulletVolleyData2D) -> void:
		d.monitorable = true
		d.set_collision_mask_from_array([3])
		d.bullet_max_collision_count = 1
	_volley(300.0, 30.0, kill)
	for f in 40:
		await physics(1)
	_expect("killing hits", {"s%d" % BulletSoundData2D.SOUND_ON_DESTROY: N, "e%d" % BulletEffectLayerData2D.EFFECT_ON_DESTROY: N, "dhit": N})


func test_a_survived_hit() -> void:
	make_wall(Vector2(100, 30), Vector2(20, 100), 4)
	var survive := func(d: BulletVolleyData2D) -> void:
		d.monitorable = true
		d.set_collision_mask_from_array([3])
		d.bullet_max_collision_count = 2
	_volley(300.0, 0.6, survive)
	for f in 50:
		await physics(1)
	_expect("survived hits, then the expiry", {"s%d" % BulletSoundData2D.SOUND_ON_HIT: N, "e%d" % BulletEffectLayerData2D.EFFECT_ON_HIT: N, "s%d" % BulletSoundData2D.SOUND_ON_LIFETIME_OVER: N, "e%d" % BulletEffectLayerData2D.EFFECT_ON_LIFETIME_OVER: N, "dlifetime": N})


func test_an_expiry() -> void:
	_volley(50.0, 0.2)
	step_factory(20)
	_expect("expiry", {"s%d" % BulletSoundData2D.SOUND_ON_LIFETIME_OVER: N, "e%d" % BulletEffectLayerData2D.EFFECT_ON_LIFETIME_OVER: N, "dlifetime": N})


func test_a_despawn() -> void:
	factory.despawn_bounds_rect = Rect2(-50, -50, 150, 150)
	factory.despawn_bounds_enabled = true
	_volley(600.0)
	step_factory(20)
	_expect("despawn", {"s%d" % BulletSoundData2D.SOUND_ON_DESPAWN: N, "e%d" % BulletEffectLayerData2D.EFFECT_ON_DESPAWN: N, "ddespawn": N})


func test_every_clear(how: String = use_parameters(["clear_bullet", "clear_all_bullets", "circle", "rect", "factory_clear"])) -> void:
	var v := _volley()
	step_factory(2)
	match how:
		"clear_bullet":
			for i in N:
				v.clear_bullet(i)
		"clear_all_bullets":
			v.clear_all_bullets()
		"circle":
			factory.clear_bullets_in_circle(Vector2(0, 30), 100.0)
		"rect":
			factory.clear_bullets_in_rect(Rect2(-50, -50, 100, 200))
		"factory_clear":
			factory.clear_active_bullets()
	_expect(how, {"s%d" % BulletSoundData2D.SOUND_ON_CLEAR: N, "e%d" % BulletEffectLayerData2D.EFFECT_ON_CLEAR: N, "dclear": N})


func test_a_pickup_conversion_fires_clear_effects_only_when_asked(fire: bool = use_parameters([true, false])) -> void:
	_volley()
	step_factory(2)
	var coin := BulletPickupData2D.new()
	coin.fire_clear_effects = fire
	factory.convert_all_bullets_to_pickups(coin, Vector2(5000, 5000))
	var want := {"dpickup": N}
	if fire:
		want["s%d" % BulletSoundData2D.SOUND_ON_CLEAR] = N
		want["e%d" % BulletEffectLayerData2D.EFFECT_ON_CLEAR] = N
	_expect("conversion (fire_clear_effects %s)" % fire, want)


func test_a_split_death(death_effects: bool = use_parameters([true, false])) -> void:
	var v := _volley(0.0, 0.2)
	var stage := BulletSplitStage2D.new()
	stage.count = 2
	stage.fire_death_effects = death_effects
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	v.split_set_data(chain, H.make_volley_data(2, 0.0, 30.0))
	for f in 20:
		await physics(1)
	var want := {"s%d" % BulletSoundData2D.SOUND_ON_SPLIT: N, "e%d" % BulletEffectLayerData2D.EFFECT_ON_SPLIT: N, "dlifetime": N}
	if death_effects:
		want["s%d" % BulletSoundData2D.SOUND_ON_LIFETIME_OVER] = N
		want["e%d" % BulletEffectLayerData2D.EFFECT_ON_LIFETIME_OVER] = N
	_expect("split at the expiry (fire_death_effects %s)" % death_effects, want)


func test_silent_ways_out(how: String = use_parameters(["disable_bullet", "free_active_bullets", "reset", "queue_free"])) -> void:
	var v := _volley()
	step_factory(2)
	match how:
		"disable_bullet":
			for i in N:
				v.disable_bullet(i)
		"free_active_bullets":
			await idle(1)
			factory.free_active_bullets()
		"reset":
			await idle(1)
			factory.reset()
		"queue_free":
			v.queue_free()
	await idle(2)
	_expect(how, {"dmanual": N} if how == "disable_bullet" else {})


func test_a_bounce_a_homing_reach_and_a_graze() -> void:
	make_wall(Vector2(100, 30), Vector2(20, 100), 8)
	var t := make_graze_target(Vector2(30, 300), &"ledger_targets")
	var bounce := func(d: BulletVolleyData2D) -> void:
		d.monitorable = true
		d.set_collision_mask_from_array([4])
		d.set_bounce_mask_from_array([4])
		d.bounce_mode = 1
		d.bullet_max_collision_count = 0
		d.homing_smoothing = 0.0
		d.homing_take_control_of_texture_rotation = true
		d.homing_distance_before_reached = 12.0
	var v := _volley(300.0, 30.0, bounce)
	v.graze_set_zones([H.make_graze_zone([40.0])], &"ledger_targets")
	for f in 30:
		await physics(1)
	assert_eq(_count("signal:bounce_body_entered"), N, "every bullet bounced once")
	v.shared_homing_deque_push_back_node2d_target(t)
	for f in 120:
		await physics(1)
		if _count("signal:bullet_homing_target_reached") >= N:
			break
	var reached := _count("signal:bullet_homing_target_reached")
	var grazed := _count("signal:bullet_grazed")
	var exits := _count("signal:bullet_graze_exited")
	assert_eq(reached, N, "every bullet reached the target")
	assert_gt(grazed, 0, "the bullets grazed it")
	_expect("bounce, reach, graze", {
		"s%d" % BulletSoundData2D.SOUND_ON_BOUNCE: N, "e%d" % BulletEffectLayerData2D.EFFECT_ON_BOUNCE: N,
		"s%d" % BulletSoundData2D.SOUND_ON_HOMING_TARGET_REACHED: reached,
	})
	assert_eq(_count("offer:%d" % BulletSoundData2D.SOUND_ON_GRAZE), grazed, "one On Graze per graze")
	assert_eq(_count("offer:%d" % BulletSoundData2D.SOUND_ON_GRAZE_EXIT), exits, "one On Graze Exit per exit")


func test_spawner_shots_and_warnings(mode: String = use_parameters(["auto", "burst", "telegraph", "burst_telegraph", "pattern_list", "fire_n_volleys"])) -> void:
	var sp := make_spawner(H.make_volley_data(2, 100.0, 0.3), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	ledger.watch_spawner(sp)
	sp.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_SHOT, 8), H.make_sound(BulletSoundData2D.SOUND_ON_TELEGRAPH, 8)]
	sp.sound_enabled = true
	sp.shoot_interval_sec = 0.2
	sp.max_volleys = 6
	if mode.contains("burst"):
		sp.burst_count = 3
		sp.burst_interval_sec = 0.05
		sp.burst_enabled = true
	if mode.contains("telegraph"):
		sp.telegraph_sec = 0.1
		sp.telegraph_enabled = true
	match mode:
		"pattern_list":
			sp.spawn_pattern_list([{"helper_bullets_amount": 3}, {"helper_bullets_amount": 4}, {}], false, 0.05)
		"fire_n_volleys":
			sp.fire_n_volleys(4)
			sp.shooting_enabled = true
		_:
			sp.shooting_enabled = true
	for f in 180:
		await idle(1)
	var fired := _count("signal:volley_fired")
	var warned := _count("signal:volley_telegraphed")
	assert_gt(fired, 0, "%s: it fired" % mode)
	assert_eq(_count("offer:%d" % BulletSoundData2D.SOUND_ON_SHOT), fired, "%s: one On Shot per fired volley" % mode)
	assert_eq(_count("offer:%d" % BulletSoundData2D.SOUND_ON_TELEGRAPH), warned, "%s: one On Telegraph per warning" % mode)
	if mode.contains("telegraph"):
		assert_gt(warned, 0, "%s: it warned" % mode)
	assert_eq("\n".join(ledger.problems()), "", mode)
