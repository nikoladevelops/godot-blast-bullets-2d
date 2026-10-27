extends BlastTest
## Laser sounds (LaserSoundData2D): every laser event offers its own trigger
## exactly once per event, where the event happened: On Charge once at the
## muzzle when the laser charges, On Fire when the beams start, On Hit /
## On Pierce Start / On Wall Hit / On Graze / On Graze Exit once per signal at
## the signal's position, On Pierce End once per laser_pierce_ended, On Active
## once per beam per hitting tick (at the tip or the muzzle: follow_point),
## On Finish when the phases run out and On Ended Early instead when
## end_laser / a clear cut it short, On Despawn once per head leaving the
## despawn bounds. A followed hum keeps one voice per beam and fades when the
## laser ends. Every flag restarts per life (pool reuse). Each spawner type
## arms its own sound class only (exact texts).

const DT := 0.0625 # exact binary ticks: phases end exactly on their tick
const GROUP := &"laser_sound_grazers"

var events := {} # trigger -> [position or null, ...] from the signals


func before_each() -> void:
	await super()
	events.clear()
	factory.debug_set_sound_offer_log_enabled(true)
	factory.laser_hit.connect(func(_t, _l, _b: int, p: Vector2) -> void: _event(LaserSoundData2D.LASER_SOUND_ON_HIT, p))
	factory.laser_pierce_started.connect(func(_t, _l, _b: int, p: Vector2) -> void: _event(LaserSoundData2D.LASER_SOUND_ON_PIERCE_START, p))
	factory.laser_pierce_ended.connect(func(_t, _l, _b: int) -> void: _event(LaserSoundData2D.LASER_SOUND_ON_PIERCE_END, null))
	factory.laser_wall_hit.connect(func(_l, _b: int, p: Vector2, _n: Vector2) -> void: _event(LaserSoundData2D.LASER_SOUND_ON_WALL_HIT, p))
	factory.laser_grazed.connect(func(_t, _l, _b: int, _z, _r: int) -> void: _event(LaserSoundData2D.LASER_SOUND_ON_GRAZE, null))
	factory.laser_graze_exited.connect(func(_t, _l, _b: int, _z, _r: int) -> void: _event(LaserSoundData2D.LASER_SOUND_ON_GRAZE_EXIT, null))


func _event(trigger: int, at: Variant) -> void:
	if not events.has(trigger):
		events[trigger] = []
	events[trigger].append(at)


## One entry per laser trigger the laser itself fires (2..13).
func _every_trigger() -> Array:
	var out: Array = []
	for trigger in range(LaserSoundData2D.LASER_SOUND_ON_CHARGE, LaserSoundData2D.LASER_SOUND_ON_DESPAWN + 1):
		out.append(H.make_laser_sound(trigger, 32))
	return out


## Positions of the laser offers on `trigger` since the log was cleared.
func _offers(trigger: int) -> Array:
	var out: Array = []
	for o in factory.debug_get_sound_offer_log():
		if String(o.get("sound_class", "")) == "LaserSoundData2D" and int(o["trigger"]) == trigger:
			out.append(o)
	return out


func _positions(trigger: int) -> Array:
	var out: Array = []
	for o in _offers(trigger):
		out.append((o["position"] as Vector2).round())
	return out


func _rounded(list: Array) -> Array:
	var out: Array = []
	for p in list:
		out.append((p as Vector2).round())
	return out


func _fire(data: LaserData2D, poses: Array[Transform2D], effects: Array) -> LaserVolley2D:
	var l := factory.spawn_laser(data, poses)
	assert_not_null(l, "fired")
	assert_true(l.sound_set_effects(effects), "sounds armed")
	return l


func test_the_resource_is_bound_with_serialized_triggers() -> void:
	var s := LaserSoundData2D.new()
	for m in ["set_trigger", "get_trigger", "set_follow_beam", "get_follow_beam", "set_follow_point", "get_follow_point", "set_ring_index", "set_zone_index"]:
		assert_true(s.has_method(m), "LaserSoundData2D.%s is bound" % m)
	assert_eq([LaserSoundData2D.LASER_SOUND_ON_SHOT, LaserSoundData2D.LASER_SOUND_ON_TELEGRAPH, LaserSoundData2D.LASER_SOUND_ON_CHARGE, LaserSoundData2D.LASER_SOUND_ON_FIRE, LaserSoundData2D.LASER_SOUND_ON_ACTIVE, LaserSoundData2D.LASER_SOUND_ON_HIT, LaserSoundData2D.LASER_SOUND_ON_PIERCE_START, LaserSoundData2D.LASER_SOUND_ON_PIERCE_END, LaserSoundData2D.LASER_SOUND_ON_WALL_HIT, LaserSoundData2D.LASER_SOUND_ON_GRAZE, LaserSoundData2D.LASER_SOUND_ON_GRAZE_EXIT, LaserSoundData2D.LASER_SOUND_ON_FINISH, LaserSoundData2D.LASER_SOUND_ON_ENDED_EARLY, LaserSoundData2D.LASER_SOUND_ON_DESPAWN], range(14), "serialized trigger ids")
	assert_eq([LaserSoundData2D.FOLLOW_TIP, LaserSoundData2D.FOLLOW_MUZZLE], [0, 1], "serialized follow points")
	assert_eq([LaserSoundData2D.STREAM_SHUFFLE, LaserSoundData2D.SOUND_AUDIBILITY_CAMERA, LaserSoundData2D.STEAL_QUIETEST, LaserSoundData2D.SOUND_LIMIT_SKIP_NEW], [BulletSoundData2D.STREAM_SHUFFLE, BulletSoundData2D.SOUND_AUDIBILITY_CAMERA, BulletSoundData2D.STEAL_QUIETEST, BulletSoundData2D.SOUND_LIMIT_SKIP_NEW], "the shared mix enums are spelled on the laser class too")
	assert_eq(s.trigger, LaserSoundData2D.LASER_SOUND_ON_HIT, "On Hit by default")
	assert_eq(s.follow_point, LaserSoundData2D.FOLLOW_TIP, "the tip by default")
	s.trigger = 14
	expect_error_sequence(["LaserSoundData2D: trigger must be 0 (On Shot), 1 (On Telegraph), 2 (On Charge), 3 (On Fire), 4 (On Active), 5 (On Hit), 6 (On Pierce Start), 7 (On Pierce End), 8 (On Wall Hit), 9 (On Graze), 10 (On Graze Exit), 11 (On Finish), 12 (On Ended Early) or 13 (On Despawn), keeping the old value."])
	s.follow_point = 2
	expect_error_sequence(["LaserSoundData2D: follow_point must be 0 (Tip) or 1 (Muzzle), keeping the old value."])
	assert_eq([s.trigger, s.follow_point], [LaserSoundData2D.LASER_SOUND_ON_HIT, LaserSoundData2D.FOLLOW_TIP], "old values kept")


func test_ring_filters_show_only_for_the_graze_triggers() -> void:
	var s := LaserSoundData2D.new()
	var visible := func(name: String) -> bool:
		for p in s.get_property_list():
			if p["name"] == name:
				return int(p["usage"]) & PROPERTY_USAGE_EDITOR != 0
		return false
	assert_false(visible.call("ring_index"), "hidden for On Hit")
	s.trigger = LaserSoundData2D.LASER_SOUND_ON_GRAZE_EXIT
	assert_true(visible.call("ring_index") and visible.call("zone_index"), "shown for On Graze Exit")


func test_every_event_offers_its_trigger_once_where_it_happened() -> void:
	make_wall(Vector2(300, 0), Vector2(20, 200), 16)
	var target := make_area(Vector2(150, 0), Vector2(20, 60))
	var grazer := Node2D.new()
	grazer.position = Vector2(100, 15)
	add(grazer)
	grazer.add_to_group(GROUP)
	await idle(2)
	var d := H.make_laser(0.125, 0.0, 0.25, 0.0625, 400.0) # charge 2 ticks, active 4, fade 1
	d.hit_while_growing = false
	d.wall_mask = 16
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO)]
	var l := _fire(d, poses, _every_trigger())
	assert_true(l.graze_set_zones([H.make_graze_zone([30.0])], GROUP), "graze armed")
	var phases: Array = []
	factory.laser_phase_changed.connect(func(_l, p: int) -> void: phases.append(p))
	var active_expected := 0
	for i in 4:
		step_factory(1, DT)
		active_expected += 1 if l.get_phase() == LaserVolley2D.PHASE_ACTIVE else 0
		if i == 3:
			target.position = Vector2(150, 400) # leaves the beam: one pierce end
			grazer.position = Vector2(100, 400) # leaves the zone: one graze exit
	for i in 6:
		step_factory(1, DT)
		if not l.is_pooled():
			active_expected += 1 if l.get_phase() == LaserVolley2D.PHASE_ACTIVE else 0
	assert_true(l.is_pooled(), "the laser finished")
	assert_eq(_positions(LaserSoundData2D.LASER_SOUND_ON_CHARGE), [Vector2.ZERO], "On Charge once, at the muzzle")
	assert_eq(phases.count(LaserVolley2D.PHASE_GROWING), 1, "the beam started once")
	assert_eq(_positions(LaserSoundData2D.LASER_SOUND_ON_FIRE), [Vector2.ZERO], "On Fire once, at the muzzle")
	for trigger in [LaserSoundData2D.LASER_SOUND_ON_HIT, LaserSoundData2D.LASER_SOUND_ON_PIERCE_START, LaserSoundData2D.LASER_SOUND_ON_WALL_HIT]:
		assert_eq(events.get(trigger, []).size(), 1, "trigger %d: one event" % trigger)
		assert_eq(_positions(trigger), _rounded(events.get(trigger, [])), "trigger %d: one offer per event, at its position" % trigger)
	for trigger in [LaserSoundData2D.LASER_SOUND_ON_PIERCE_END, LaserSoundData2D.LASER_SOUND_ON_GRAZE, LaserSoundData2D.LASER_SOUND_ON_GRAZE_EXIT]:
		assert_eq(events.get(trigger, []).size(), 1, "trigger %d: one event" % trigger)
		assert_eq(_offers(trigger).size(), 1, "trigger %d: one offer per event" % trigger)
	assert_eq(_positions(LaserSoundData2D.LASER_SOUND_ON_PIERCE_START), [Vector2(150, 0)], "the pierce starts at the target projected onto the beam")
	assert_eq(_positions(LaserSoundData2D.LASER_SOUND_ON_WALL_HIT), [Vector2(290, 0)], "the wall hit sits on the wall's face")
	assert_gt(active_expected, 0, "the beam was active")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_ACTIVE).size(), active_expected, "On Active once per hitting tick")
	for o in _offers(LaserSoundData2D.LASER_SOUND_ON_ACTIVE):
		assert_eq(int(o["beam"]), 0, "names its beam")
		assert_eq((o["position"] as Vector2).round(), Vector2(290, 0), "at the tip (the wall stops it)")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_FINISH).size(), 1, "On Finish once")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_ENDED_EARLY).size(), 0, "never ended early")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_DESPAWN).size(), 0, "a beam never despawns")


func test_a_laser_without_charge_never_offers_on_charge() -> void:
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO)]
	var l := _fire(H.make_laser(0.0, 0.0, 0.125, 0.0), poses, _every_trigger())
	step_factory(4, DT)
	assert_true(l.is_pooled(), "finished")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_CHARGE).size(), 0, "no charge, no On Charge")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_FIRE).size(), 1, "On Fire still once")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_FINISH).size(), 1, "and On Finish")


func test_ending_early_replaces_on_finish() -> void:
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO)]
	var fading := _fire(H.make_laser(0.0, 0.0, 0.0, 0.125), poses, _every_trigger())
	step_factory(2, DT)
	fading.end_laser(true)
	step_factory(4, DT)
	assert_true(fading.is_pooled(), "faded out")
	assert_eq([_offers(LaserSoundData2D.LASER_SOUND_ON_ENDED_EARLY).size(), _offers(LaserSoundData2D.LASER_SOUND_ON_FINISH).size()], [1, 0], "end_laser (fading): On Ended Early only")
	factory.debug_clear_sound_offer_log()
	var cleared := _fire(H.make_laser(0.25, 0.0, 0.0, 0.0), poses, _every_trigger())
	step_factory(1, DT)
	factory.clear_active_lasers()
	step_factory(2, DT)
	assert_true(cleared.is_pooled(), "cleared while charging")
	assert_eq([_offers(LaserSoundData2D.LASER_SOUND_ON_ENDED_EARLY).size(), _offers(LaserSoundData2D.LASER_SOUND_ON_FINISH).size(), _offers(LaserSoundData2D.LASER_SOUND_ON_FIRE).size()], [1, 0, 0], "a clear before it fired: On Ended Early, never On Fire")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_CHARGE).size(), 1, "it did charge")


func test_every_flag_restarts_with_a_new_life() -> void:
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO)]
	var first := _fire(H.make_laser(0.0625, 0.0, 0.0, 0.0), poses, _every_trigger())
	step_factory(1, DT)
	first.end_laser(false)
	step_factory(2, DT)
	assert_true(first.is_pooled(), "pooled")
	factory.debug_clear_sound_offer_log()
	var second := _fire(H.make_laser(0.0625, 0.0, 0.0625, 0.0), poses, _every_trigger())
	assert_same(second, first, "the pooled laser is reused")
	step_factory(4, DT)
	assert_true(second.is_pooled(), "finished")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_CHARGE).size(), 1, "the new life charges audibly again")
	assert_eq([_offers(LaserSoundData2D.LASER_SOUND_ON_FINISH).size(), _offers(LaserSoundData2D.LASER_SOUND_ON_ENDED_EARLY).size()], [1, 0], "and finishes (the old early end is forgotten)")


func test_on_active_plays_per_beam_at_the_chosen_point() -> void:
	var tip := H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_ACTIVE)
	var muzzle := H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_ACTIVE)
	muzzle.follow_point = LaserSoundData2D.FOLLOW_MUZZLE
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2(10, 0)), Transform2D(PI / 2.0, Vector2(0, 10))]
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0, 100.0), poses, [tip, muzzle])
	step_factory(1, DT)
	var seen: Array = []
	for o in _offers(LaserSoundData2D.LASER_SOUND_ON_ACTIVE):
		seen.append([int(o["beam"]), (o["position"] as Vector2).round()])
	seen.sort()
	assert_eq(seen, [[0, Vector2(10, 0)], [0, Vector2(110, 0)], [1, Vector2(0, 10)], [1, Vector2(0, 110)]], "each beam, at its tip and at its muzzle")
	l.end_laser(false)


func test_a_followed_hum_keeps_one_voice_per_beam_and_fades_at_the_end() -> void:
	godot_listener_at(Vector2.ZERO)
	var hum := H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_ACTIVE, 8)
	hum.follow_beam = true
	hum.fade_out_sec = 0.25
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO), Transform2D(PI, Vector2.ZERO)]
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0, 100.0), poses, [hum])
	for i in 6:
		await physics(1)
	assert_eq(busy_voices().size(), 2, "one hum voice per beam, never one per tick")
	l.set_beam_angle(0, PI / 2.0)
	await physics(2)
	var at: Array = []
	for v in busy_voices():
		at.append((v["position"] as Vector2).round())
	at.sort()
	assert_eq(at, [Vector2(-100, 0), Vector2(0, 100)], "each voice rides its beam's tip")
	l.end_laser(false)
	await physics(2)
	for v in busy_voices():
		assert_true(bool(v.get("fading", false)), "an ended beam's hum fades")
	for i in 40:
		await physics(1)
		if busy_voices().is_empty():
			break
	assert_eq(busy_voices().size(), 0, "the fade released them")


func test_a_bolt_leaving_the_bounds_offers_on_despawn_once_per_head() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 40.0)
	d.type = LaserData2D.LASER_BOLT
	d.speed = 1600.0 # 100 px per tick
	factory.despawn_bounds_rect = Rect2(-50, -50, 300, 100)
	factory.despawn_bounds_enabled = true
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO), Transform2D(PI / 2.0, Vector2.ZERO)]
	var l := _fire(d, poses, _every_trigger())
	step_factory(2, DT)
	var despawned := _offers(LaserSoundData2D.LASER_SOUND_ON_DESPAWN)
	assert_eq(despawned.size(), 1, "the downward head left: one On Despawn")
	assert_eq(int(despawned[0]["beam"]), 1, "naming its beam")
	step_factory(6, DT)
	assert_true(l.is_pooled(), "every head spent")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_DESPAWN).size(), 2, "each head once")


func test_graze_filters_pick_their_ring() -> void:
	var grazer := Node2D.new()
	grazer.position = Vector2(100, 15)
	add(grazer)
	grazer.add_to_group(GROUP)
	var any := H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_GRAZE)
	var outer := H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_GRAZE)
	outer.ring_index = 1
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO)]
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0, 300.0), poses, [any, outer])
	assert_true(l.graze_set_zones([H.make_graze_zone([20.0, 40.0])], GROUP), "graze armed")
	step_factory(1, DT)
	assert_eq(events.get(LaserSoundData2D.LASER_SOUND_ON_GRAZE, []).size(), 2, "both rings grazed")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_GRAZE).size(), 3, "the any-ring entry twice, the outer-ring entry once")
	l.end_laser(false)


func test_each_spawner_type_arms_only_its_own_sound_class() -> void:
	var ls := LaserSpawner2D.new()
	ls.set_shooting_enabled(false)
	ls.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_HIT)]
	expect_error_sequence(["LaserSpawner2D: sound_effects entries must be LaserSoundData2D or null, keeping the old value."])
	var bs := BulletSpawner2D.new()
	bs.set_shooting_enabled(false)
	bs.sound_effects = [H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_HIT)]
	expect_error_sequence(["BulletSpawner2D: sound_effects entries must be BulletSoundData2D or null, keeping the old value."])
	assert_eq([ls.sound_effects.size(), bs.sound_effects.size()], [0, 0], "old values kept")
	ls.sound_effects = [H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_HIT), null]
	bs.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_HIT), null]
	assert_eq([ls.sound_effects.size(), bs.sound_effects.size()], [2, 2], "their own class (and nulls) accepted")
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO)]
	var l := factory.spawn_laser(H.make_laser(), poses)
	assert_false(l.sound_set_effects([H.make_sound(BulletSoundData2D.SOUND_ON_HIT)]), "a factory laser refuses bullet sounds")
	expect_error_sequence(["sound_set_effects: every entry must be a LaserSoundData2D or null, nothing changed."])
	assert_false(l.is_sound_armed(), "nothing armed")
	ls.free()
	bs.free()


func test_a_laser_spawner_offers_its_own_shot_and_telegraph_triggers() -> void:
	var sp := LaserSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.laser_data = H.make_laser(0.0, 0.0, 0.125, 0.0)
	sp.pattern_source = PatternSpawner2D.PATTERN_FROM_HELPER_RING
	sp.helper_bullets_amount = 1
	sp.sound_enabled = true
	sp.position = Vector2(30, 40)
	add(sp)
	sp.set_bullet_factory(factory)
	assert_true("Sound is enabled but sound_effects is empty: no laser of this spawner plays sounds." in sp.get_setup_warnings(), "the warning names lasers")
	sp.sound_effects = [H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_SHOT), H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_TELEGRAPH)]
	assert_true(sp.shoot_once(), "fired")
	var shots := _offers(LaserSoundData2D.LASER_SOUND_ON_SHOT)
	assert_eq(shots.size(), 1, "On Shot once")
	assert_eq((shots[0]["position"] as Vector2).round(), Vector2(30, 40), "at the spawner")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_TELEGRAPH).size(), 0, "no telegraph, no On Telegraph")
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.1
	sp.shoot_interval_sec = 10.0
	sp.set_shooting_enabled(true) # telegraphs belong to auto fire
	for i in 30:
		await idle(1)
		if _offers(LaserSoundData2D.LASER_SOUND_ON_SHOT).size() == 2:
			break
	sp.set_shooting_enabled(false)
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_TELEGRAPH).size(), 1, "On Telegraph once when the telegraph begins")
	assert_eq(_offers(LaserSoundData2D.LASER_SOUND_ON_SHOT).size(), 2, "then On Shot when it fires")
