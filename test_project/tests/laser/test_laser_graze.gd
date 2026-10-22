extends BlastTest
## Laser graze: while a laser hits, every zone target within a ring's radius
## of a beam (its distance to the beam's polyline, + half the beam's width
## with count_bullet_size) grazes that ring: laser_grazed(target, laser,
## beam_index, zone, ring_index), rings touched in one tick largest first,
## each once per visit (Once) or again after leaving (After Exit); leaving
## fires laser_graze_exited once per visit that grazed, with its deepest ring.
## Charging and spent beams never graze. A handler that clears or re-arms the
## zones drops the rest of the batch; a freed target drops its events.
## graze_set_zones refuses an empty group, no zones, more than 4 zones and a
## finished laser, changing nothing.

const DT := 0.0625
const GROUP := &"laser_graze_targets"

var log: Array = [] # [kind, target name, beam, zone index, ring]
var zones: Array = []


func before_each() -> void:
	await super()
	log.clear()
	zones.clear()
	factory.laser_grazed.connect(func(t: Object, _l, b: int, z, r: int) -> void: log.append(["graze", str(t.name) if t != null else "", b, zones.find(z), r]))
	factory.laser_graze_exited.connect(func(t: Object, _l, b: int, z, r: int) -> void: log.append(["exit", str(t.name) if t != null else "", b, zones.find(z), r]))


func _target(n: String, pos: Vector2) -> Node2D:
	var t := Node2D.new()
	t.name = n
	t.position = pos
	add(t)
	t.add_to_group(GROUP)
	return t


## A +X beam of `length` from the origin, grazed by `zone_list`.
func _laser(zone_list: Array, length := 300.0, data: LaserData2D = null) -> LaserVolley2D:
	zones = zone_list
	var d := data if data != null else H.make_laser(0.0, 0.0, 0.0, 0.0, length)
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO)]
	var l := factory.spawn_laser(d, poses)
	assert_true(l.graze_set_zones(zone_list, GROUP), "armed")
	return l


func test_rings_fire_largest_first_once_per_visit_then_one_exit() -> void:
	var t := _target("A", Vector2(150, 10)) # 10 px beside the beam
	_laser([H.make_graze_zone([20.0, 50.0, 35.0])])
	step_factory(1, DT)
	assert_eq(log, [["graze", "A", 0, 0, 1], ["graze", "A", 0, 0, 2], ["graze", "A", 0, 0, 0]], "largest radius first, every ring once")
	step_factory(3, DT)
	assert_eq(log.size(), 3, "staying inside grazes nothing more")
	t.position = Vector2(150, 200)
	step_factory(1, DT)
	assert_eq(log.back(), ["exit", "A", 0, 0, 0], "one exit, naming the deepest ring (20 px)")
	t.position = Vector2(150, 10)
	step_factory(2, DT)
	assert_eq(log.size(), 4, "Once: a second visit grazes nothing")


func test_after_exit_rearms_the_rings() -> void:
	var t := _target("A", Vector2(150, 30))
	var zone := H.make_graze_zone([50.0])
	zone.regraze = BulletGrazeZone2D.REGRAZE_AFTER_EXIT
	_laser([zone])
	step_factory(1, DT)
	t.position = Vector2(150, 200)
	step_factory(1, DT)
	t.position = Vector2(150, 30)
	step_factory(1, DT)
	assert_eq(log, [["graze", "A", 0, 0, 0], ["exit", "A", 0, 0, 0], ["graze", "A", 0, 0, 0]], "graze, exit, graze again")


func test_count_bullet_size_adds_half_the_beam_width() -> void:
	_target("A", Vector2(150, 54)) # the ring reaches 50, the 12 px beam's edge 6 more
	_laser([H.make_graze_zone([50.0]), H.make_graze_zone([50.0], true)])
	step_factory(1, DT)
	assert_eq(log, [["graze", "A", 0, 1, 0]], "only the zone that counts the beam's width")


func test_the_distance_is_to_the_beam_not_its_end() -> void:
	_target("A", Vector2(320, 0)) # 20 px past the end of a 300 px beam
	_target("B", Vector2(-30, 0)) # 30 px behind the muzzle
	_laser([H.make_graze_zone([25.0])])
	step_factory(1, DT)
	assert_eq(log, [["graze", "A", 0, 0, 0]], "measured to the nearest point of the segment")


func test_every_beam_grazes_on_its_own_index() -> void:
	_target("A", Vector2(0, 150)) # beside a downward beam
	zones = [H.make_graze_zone([30.0])]
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO), Transform2D(PI / 2.0, Vector2(10, 0))]
	var l := factory.spawn_laser(H.make_laser(0.0, 0.0, 0.0, 0.0, 300.0), poses)
	l.graze_set_zones(zones, GROUP)
	step_factory(1, DT)
	assert_eq(log, [["graze", "A", 1, 0, 0]], "beam 1 (10 px away), not beam 0 (150 px)")


func test_charging_never_grazes() -> void:
	_target("A", Vector2(150, 10))
	_laser([H.make_graze_zone([50.0])], 300.0, H.make_laser(0.25, 0.0, 0.0, 0.0, 300.0))
	step_factory(3, DT)
	assert_eq(log, [], "the warning line is no hazard")
	step_factory(2, DT)
	assert_eq(log, [["graze", "A", 0, 0, 0]], "grazes once it fires")


func test_a_freed_target_hands_its_visit_to_another_target_inside() -> void:
	var a := _target("A", Vector2(100, 10))
	var b := _target("B", Vector2(200, 10)) # as near: tree order picks A
	_laser([H.make_graze_zone([30.0])])
	step_factory(1, DT)
	assert_eq(log, [["graze", "A", 0, 0, 0]], "the nearest target grazes")
	a.queue_free()
	await idle(1)
	step_factory(2, DT)
	assert_eq(log.size(), 1, "Once: B joins the visit without a second graze")
	b.position = Vector2(200, 200)
	step_factory(1, DT)
	assert_eq(log.back(), ["exit", "B", 0, 0, 0], "the visit moved to B: its exit names B")


func test_a_freed_target_with_nobody_else_inside_ends_the_visit_silently() -> void:
	var a := _target("A", Vector2(100, 10))
	_laser([H.make_graze_zone([30.0])])
	step_factory(1, DT)
	a.queue_free()
	await idle(1)
	step_factory(3, DT)
	assert_eq(log, [["graze", "A", 0, 0, 0]], "no exit for a vanished target")


func test_a_handler_that_clears_the_zones_drops_the_rest_of_the_batch() -> void:
	_target("A", Vector2(150, 10))
	var l := _laser([H.make_graze_zone([50.0, 20.0]), H.make_graze_zone([40.0, 30.0])])
	factory.laser_grazed.connect(func(_t, laser: LaserVolley2D, _b, _z, _r) -> void: laser.graze_clear(), CONNECT_ONE_SHOT)
	step_factory(3, DT)
	expect_no_errors()
	assert_eq(log, [["graze", "A", 0, 0, 0]], "the first event ran its handler; the other three were dropped")
	assert_false(l.is_graze_armed(), "cleared")


func test_a_handler_that_rearms_the_zones_drops_the_batch_and_the_new_zone_grazes_next_tick() -> void:
	_target("A", Vector2(150, 10))
	var l := _laser([H.make_graze_zone([50.0, 20.0]), H.make_graze_zone([40.0, 30.0])])
	var fresh := H.make_graze_zone([60.0])
	factory.laser_grazed.connect(func(_t, laser: LaserVolley2D, _b, _z, _r) -> void:
		zones = [fresh]
		laser.graze_set_zones([fresh], GROUP), CONNECT_ONE_SHOT)
	step_factory(1, DT)
	assert_eq(log.size(), 1, "the old batch stops after its first event")
	step_factory(1, DT)
	expect_no_errors()
	assert_eq(log.back(), ["graze", "A", 0, 0, 0], "the new zone grazes from the next tick")
	assert_eq(l.get_graze_zones(), [fresh], "re-armed")


func test_graze_set_zones_refuses_bad_requests_and_changes_nothing() -> void:
	var z := H.make_graze_zone([30.0])
	var l := _laser([z])
	assert_false(l.graze_set_zones([z], &""), "no group")
	expect_error_sequence(["graze_set_zones: target_group is empty (name the group whose nodes graze this laser), nothing changed."])
	assert_false(l.graze_set_zones([null, 3], GROUP), "no zone")
	expect_error_sequence(["graze_set_zones: zones holds no BulletGrazeZone2D, nothing changed."])
	var five: Array = []
	for i in 5:
		five.append(H.make_graze_zone([10.0 + i]))
	assert_false(l.graze_set_zones(five, GROUP), "too many")
	expect_error_sequence(["graze_set_zones: at most 4 zones (got 5), nothing changed."])
	assert_eq(l.get_graze_zones(), [z], "the old zones kept")
	l.end_laser(false)
	step_factory(1, DT)
	assert_true(l.is_pooled(), "pooled")
	assert_false(l.graze_set_zones([z], GROUP), "a finished laser")
	expect_error_sequence(["graze_set_zones: this laser finished (it waits in the pool), so this handle is stale. Fire a new laser instead."])


func test_a_pooled_laser_is_disarmed_and_its_next_life_starts_unarmed() -> void:
	var z := H.make_graze_zone([30.0])
	var l := _laser([z])
	l.end_laser(false)
	step_factory(1, DT)
	assert_eq([l.is_graze_armed(), l.get_graze_zones()], [false, []], "the pool dropped the zones")
	var poses: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO)]
	var again := factory.spawn_laser(H.make_laser(0.0, 0.0, 0.0, 0.0, 300.0), poses)
	assert_eq(again, l, "the same laser from the pool")
	assert_false(again.is_graze_armed(), "unarmed")


## A target that leaves the graze group while a beam is inside it ends the
## visit silently (like a freed target, like bullet grazes).
func test_a_target_leaving_the_group_ends_the_visit_silently() -> void:
	var a := _target("A", Vector2(100, 10))
	_laser([H.make_graze_zone([30.0])])
	step_factory(1, DT)
	assert_eq(log, [["graze", "A", 0, 0, 0]], "grazed")
	a.remove_from_group(GROUP)
	step_factory(3, DT)
	assert_eq(log, [["graze", "A", 0, 0, 0]], "no exit for a target that left the group")


## A bolt that is spent (here: it left the despawn bounds) while grazing ends
## its visit silently: a spent beam is gone, like a killed bullet.
func test_a_spent_bolt_ends_its_visit_silently() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 40.0)
	d.type = LaserData2D.LASER_BOLT
	d.speed = 320.0 # 20 px per tick
	factory.despawn_bounds_enabled = true
	factory.despawn_bounds_rect = Rect2(-20, -20, 60, 40) # x up to 40: the bolt leaves soon
	_target("A", Vector2(40, 10))
	_laser([H.make_graze_zone([30.0])], 40.0, d)
	var spent := false
	for i in 10:
		step_factory(1, DT)
		if factory.get_active_laser_count() == 0:
			spent = true
			break
	assert_true(spent, "the bolt was spent and the laser finished")
	assert_eq(log, [["graze", "A", 0, 0, 0]], "grazed, then no exit for the spent bolt")
