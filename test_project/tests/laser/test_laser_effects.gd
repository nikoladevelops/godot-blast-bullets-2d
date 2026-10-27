extends BlastTest
## Laser effects (LaserData2D.effects, LaserEffectData2D): each entry plays a
## one-shot BulletEffectLayerData2D on one laser event (the ids of the laser
## sound triggers) exactly once per event: at the event's point (Event), or
## the muzzle or tip of the event's beam, turned with the beam there while
## align_to_beam. Laser-wide events play once per beam (charge and fire at
## each muzzle, finish and ended early at each tip). Effects play without any
## sound armed, read the data at every event (edits reach lasers in flight)
## and outlive the laser (the factory owns them). A trail layer, or a layer
## that cannot play, is refused once (per edit), never once per event.

const DT := 0.0625 # exact binary ticks


func before_each() -> void:
	await super()
	factory.debug_set_effect_log_enabled(true)


## A one-shot layer of two frames (0.5 s at 4 fps).
func _layer() -> BulletEffectLayerData2D:
	var l := BulletEffectLayerData2D.new()
	l.trigger = BulletEffectLayerData2D.EFFECT_ON_HIT
	var sf := SpriteFrames.new()
	sf.set_animation_speed(&"default", 4.0)
	sf.set_animation_loop(&"default", false)
	for i in 2:
		sf.add_frame(&"default", ImageTexture.create_from_image(Image.create(4, 4, false, Image.FORMAT_RGBA8)))
	l.sprite_frames = sf
	return l


func _entry(trigger: int, at := 0) -> LaserEffectData2D:
	var e := LaserEffectData2D.new()
	e.trigger = trigger
	e.at = at
	e.effect = _layer()
	return e


## The laser effect log rows on `trigger`.
func _played(trigger: int) -> Array:
	return factory.debug_get_effect_log().filter(func(f: Dictionary) -> bool: return f.has("laser") and int(f["trigger"]) == trigger)


func _positions(trigger: int) -> Array:
	var out: Array = []
	for f in _played(trigger):
		out.append((f["position"] as Vector2).round())
	out.sort()
	return out


func _fire(d: LaserData2D, poses: Array[Transform2D]) -> LaserVolley2D:
	var l := factory.spawn_laser(d, poses)
	assert_not_null(l, "fired")
	return l


## The live manual effect poses, rounded: [[origin, rotation], ...].
func _live_poses() -> Array:
	var out: Array = []
	for bake in factory.debug_get_effect_state().get("manual", []):
		for row in bake.get("live_slots", []):
			var t: Transform2D = row["fixed"]
			out.append([t.origin.round(), snappedf(t.get_rotation(), 0.001)])
	out.sort()
	return out


func test_the_entry_is_bound_and_refuses_bad_input() -> void:
	var e := LaserEffectData2D.new()
	for m in ["set_trigger", "set_at", "set_effect", "set_align_to_beam", "set_enabled"]:
		assert_true(e.has_method(m), "LaserEffectData2D.%s is bound" % m)
	assert_eq([LaserEffectData2D.LASER_EFFECT_ON_CHARGE, LaserEffectData2D.LASER_EFFECT_ON_HIT, LaserEffectData2D.LASER_EFFECT_ON_DESPAWN], [LaserSoundData2D.LASER_SOUND_ON_CHARGE, LaserSoundData2D.LASER_SOUND_ON_HIT, LaserSoundData2D.LASER_SOUND_ON_DESPAWN], "the laser sound trigger ids")
	assert_eq([LaserEffectData2D.AT_EVENT, LaserEffectData2D.AT_MUZZLE, LaserEffectData2D.AT_TIP], [0, 1, 2], "serialized points")
	assert_eq([e.trigger, e.at, e.align_to_beam, e.enabled], [LaserEffectData2D.LASER_EFFECT_ON_HIT, LaserEffectData2D.AT_EVENT, true, true], "defaults")
	e.trigger = 1
	e.trigger = 14
	e.at = 3
	var trail := _layer()
	trail.trigger = BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW
	e.effect = trail
	expect_error_sequence([
		"LaserEffectData2D: trigger must be 2 (On Charge), 3 (On Fire), 4 (On Active), 5 (On Hit), 6 (On Pierce Start), 7 (On Pierce End), 8 (On Wall Hit), 9 (On Graze), 10 (On Graze Exit), 11 (On Finish), 12 (On Ended Early) or 13 (On Despawn), keeping the old value.",
		"LaserEffectData2D: trigger must be 2 (On Charge), 3 (On Fire), 4 (On Active), 5 (On Hit), 6 (On Pierce Start), 7 (On Pierce End), 8 (On Wall Hit), 9 (On Graze), 10 (On Graze Exit), 11 (On Finish), 12 (On Ended Early) or 13 (On Despawn), keeping the old value.",
		"LaserEffectData2D: at must be 0 (Event), 1 (Muzzle) or 2 (Tip), keeping the old value.",
		"LaserEffectData2D: effect must be a one-shot layer (a trail follows a bullet), keeping the old value.",
	])
	assert_eq([e.trigger, e.at], [LaserEffectData2D.LASER_EFFECT_ON_HIT, LaserEffectData2D.AT_EVENT], "old values kept")
	assert_null(e.effect, "no trail taken")
	var d := LaserData2D.new()
	d.effects = [e, null]
	assert_eq(d.effects.size(), 2, "entries and nulls")
	d.effects = [LaserSoundData2D.new()]
	expect_error_sequence(["LaserData2D: effects entries must be LaserEffectData2D or null, keeping the old value."])
	assert_eq(d.effects.size(), 2, "old value kept")


func test_every_event_plays_its_effect_once_where_it_happened() -> void:
	make_wall(Vector2(300, 0), Vector2(20, 400), 16)
	make_area(Vector2(150, 0), Vector2(20, 60))
	await idle(2)
	var hits: Array = []
	var walls: Array = []
	factory.laser_hit.connect(func(_t, _l, _b, p: Vector2) -> void: hits.append(p.round()))
	factory.laser_wall_hit.connect(func(_l, _b, p: Vector2, _n) -> void: walls.append(p.round()))
	var d := H.make_laser(0.125, 0.0, 0.25, 0.0, 400.0)
	d.hit_while_growing = false
	d.wall_mask = 16
	var entries: Array = []
	for trigger in [LaserEffectData2D.LASER_EFFECT_ON_CHARGE, LaserEffectData2D.LASER_EFFECT_ON_FIRE, LaserEffectData2D.LASER_EFFECT_ON_HIT, LaserEffectData2D.LASER_EFFECT_ON_PIERCE_START, LaserEffectData2D.LASER_EFFECT_ON_PIERCE_END, LaserEffectData2D.LASER_EFFECT_ON_WALL_HIT, LaserEffectData2D.LASER_EFFECT_ON_FINISH, LaserEffectData2D.LASER_EFFECT_ON_ENDED_EARLY]:
		entries.append(_entry(trigger))
	d.effects = entries
	# Two beams: +x into the target and the wall, -x into nothing.
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO), Transform2D(PI, Vector2.ZERO)]
	var l := _fire(d, poses)
	step_factory(10, DT)
	assert_true(l.is_pooled(), "finished")
	assert_eq(_positions(LaserEffectData2D.LASER_EFFECT_ON_CHARGE), [Vector2.ZERO, Vector2.ZERO], "On Charge once per beam, at its muzzle")
	assert_eq(_positions(LaserEffectData2D.LASER_EFFECT_ON_FIRE), [Vector2.ZERO, Vector2.ZERO], "On Fire once per beam, at its muzzle")
	assert_eq(_positions(LaserEffectData2D.LASER_EFFECT_ON_HIT), hits, "On Hit once per laser_hit, at the contact")
	assert_eq(_positions(LaserEffectData2D.LASER_EFFECT_ON_PIERCE_START), [Vector2(150, 0)], "On Pierce Start at the contact")
	assert_eq(_played(LaserEffectData2D.LASER_EFFECT_ON_PIERCE_END).size(), 1, "On Pierce End once (the contact ends with the laser)")
	assert_eq(_positions(LaserEffectData2D.LASER_EFFECT_ON_WALL_HIT), walls, "On Wall Hit once per block")
	assert_eq(walls.size(), 1, "one block")
	assert_eq(_positions(LaserEffectData2D.LASER_EFFECT_ON_FINISH), [Vector2(-400, 0), Vector2(290, 0)], "On Finish once per beam, at its tip")
	assert_eq(_played(LaserEffectData2D.LASER_EFFECT_ON_ENDED_EARLY).size(), 0, "never ended early")
	assert_eq(_played(LaserEffectData2D.LASER_EFFECT_ON_FINISH).map(func(f): return int(f["beam"])).reduce(func(a, b): return a + b, 0), 1, "naming beams 0 and 1")


func test_effects_play_at_the_chosen_point_turned_with_the_beam() -> void:
	make_area(Vector2(100, 0), Vector2(20, 60))
	await idle(2)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)
	var at_event := _entry(LaserEffectData2D.LASER_EFFECT_ON_HIT, LaserEffectData2D.AT_EVENT)
	var at_muzzle := _entry(LaserEffectData2D.LASER_EFFECT_ON_HIT, LaserEffectData2D.AT_MUZZLE)
	var at_tip := _entry(LaserEffectData2D.LASER_EFFECT_ON_HIT, LaserEffectData2D.AT_TIP)
	var unaligned := _entry(LaserEffectData2D.LASER_EFFECT_ON_HIT, LaserEffectData2D.AT_EVENT)
	unaligned.align_to_beam = false
	d.effects = [at_event, at_muzzle, at_tip, unaligned]
	var poses: Array[Transform2D] = [Transform2D(PI / 2.0, Vector2(100, -100))] # straight down through the target
	_fire(d, poses)
	step_factory(1, DT)
	assert_eq(_live_poses(), [[Vector2(100, -100), 1.571], [Vector2(100, 0), 0.0], [Vector2(100, 0), 1.571], [Vector2(100, 100), 1.571]], "muzzle, the contact (aligned and not), tip")


func test_effects_play_without_sounds_and_edits_reach_lasers_in_flight() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 100.0)
	var l := _fire(d, [Transform2D(0.0, Vector2.ZERO)] as Array[Transform2D])
	step_factory(1, DT)
	assert_eq(_played(LaserEffectData2D.LASER_EFFECT_ON_ACTIVE).size(), 0, "no entry yet")
	assert_false(l.is_sound_armed(), "no sound armed")
	d.effects = [_entry(LaserEffectData2D.LASER_EFFECT_ON_ACTIVE, LaserEffectData2D.AT_TIP)]
	step_factory(3, DT)
	assert_eq(_positions(LaserEffectData2D.LASER_EFFECT_ON_ACTIVE), [Vector2(100, 0), Vector2(100, 0), Vector2(100, 0)], "once per hitting tick at the tip, added while it fires")
	(d.effects[0] as LaserEffectData2D).enabled = false
	step_factory(1, DT)
	assert_eq(_played(LaserEffectData2D.LASER_EFFECT_ON_ACTIVE).size(), 3, "a disabled entry plays nothing")
	l.end_laser(false)
	step_factory(1, DT)
	assert_true(l.is_pooled(), "pooled")
	assert_gt(int(factory.debug_get_effect_state()["manual_active"]), 0, "its effects keep playing (the factory owns them)")


func test_ending_early_plays_on_ended_early_at_each_tip() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 50.0)
	d.effects = [_entry(LaserEffectData2D.LASER_EFFECT_ON_FINISH), _entry(LaserEffectData2D.LASER_EFFECT_ON_ENDED_EARLY)]
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO), Transform2D(PI / 2.0, Vector2.ZERO)]
	var l := _fire(d, poses)
	step_factory(2, DT)
	factory.clear_active_lasers()
	step_factory(1, DT)
	assert_true(l.is_pooled(), "cleared")
	assert_eq(_positions(LaserEffectData2D.LASER_EFFECT_ON_ENDED_EARLY), [Vector2(0, 50), Vector2(50, 0)], "once per beam, at its tip")
	assert_eq(_played(LaserEffectData2D.LASER_EFFECT_ON_FINISH).size(), 0, "never On Finish")


func test_a_bolt_leaving_the_bounds_plays_on_despawn_where_it_left() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 40.0)
	d.type = LaserData2D.LASER_BOLT
	d.speed = 1600.0 # 100 px per tick
	d.effects = [_entry(LaserEffectData2D.LASER_EFFECT_ON_DESPAWN)]
	factory.despawn_bounds_rect = Rect2(-50, -50, 300, 100)
	factory.despawn_bounds_enabled = true
	var poses: Array[Transform2D] = [Transform2D(PI / 2.0, Vector2.ZERO)]
	var l := _fire(d, poses)
	step_factory(4, DT)
	assert_true(l.is_pooled(), "spent and pooled")
	var played := _played(LaserEffectData2D.LASER_EFFECT_ON_DESPAWN)
	assert_eq(played.size(), 1, "once")
	assert_gt((played[0]["position"] as Vector2).y, 50.0, "outside the bounds, where the head left")


func test_a_layer_that_cannot_play_reports_once_per_edit() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 100.0)
	var held := _entry(LaserEffectData2D.LASER_EFFECT_ON_ACTIVE)
	held.effect.sprite_frames.set_animation_speed(&"default", 0.0) # speed 0: no playable time
	d.effects = [held]
	var l := _fire(d, [Transform2D.IDENTITY] as Array[Transform2D])
	step_factory(4, DT)
	expect_error_sequence(["BulletEffectLayerData2D: baked zero playable time, layer renders nothing."]) # the layer's own report, once over four events
	assert_eq(_played(LaserEffectData2D.LASER_EFFECT_ON_ACTIVE).size(), 0, "nothing played")
	held.effect.sprite_frames = _layer().sprite_frames # a bake edit: the layer is tried again
	step_factory(1, DT)
	assert_eq(_played(LaserEffectData2D.LASER_EFFECT_ON_ACTIVE).size(), 1, "plays after the edit")
	var trail := _layer()
	held.effect = trail
	trail.trigger = BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW # turned into a trail after it was assigned
	step_factory(3, DT)
	expect_warning_sequence(["LaserEffectData2D: effect is a trail layer (a trail follows a bullet), nothing plays."])
	assert_eq(_played(LaserEffectData2D.LASER_EFFECT_ON_ACTIVE).size(), 1, "a trail never plays")
	l.end_laser(false)
