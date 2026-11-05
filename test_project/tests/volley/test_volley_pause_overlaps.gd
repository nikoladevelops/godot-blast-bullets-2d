extends BlastTest
## Overlaps around a factory pause (set_is_factory_processing_bullets):
## an overlap that starts while paused registers exactly once on resume; one
## that starts AND ends during the pause is cancelled; overlaps already
## counted before the pause are never re-reported; parked records never
## survive into a pooled volley's next life.

var hits := 0
var arena_wall: StaticBody2D


func _on_body(_b: Object, _v: BulletVolley2D, _i: int) -> void:
	hits += 1


func _data(max_hits := 0) -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 0.0, 30.0)
	d.transforms = [Transform2D(0.0, Vector2(-300, 0))]
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = max_hits
	return d


func before_each() -> void:
	await super()
	hits = 0
	factory.body_entered.connect(_on_body)
	arena_wall = make_wall(Vector2(0, 0), Vector2(40, 40))
	await physics()


func _pause() -> void:
	factory.set_is_factory_processing_bullets(false)


func _resume() -> void:
	factory.set_is_factory_processing_bullets(true)


func test_overlap_started_while_paused_hits_once() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	await physics(2)
	_pause()
	v.teleport_bullet(0, Vector2(0, 0)) # into the wall while paused
	await physics(5)
	assert_eq(hits, 0, "nothing drains while paused")
	_resume()
	await physics(5)
	assert_eq(hits, 1, "exactly one hit after resume")
	assert_eq(v.get_bullet_collision_count(0), 1)


func test_overlap_started_and_ended_while_paused_is_cancelled() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	await physics(2)
	_pause()
	v.teleport_bullet(0, Vector2(0, 0))
	await physics(3)
	v.teleport_bullet(0, Vector2(-300, 0)) # left again during the pause
	await physics(3)
	_resume()
	await physics(5)
	assert_eq(hits, 0, "a pause-only overlap that ended is never reported")


func test_overlap_counted_before_the_pause_is_not_recounted() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	await physics(2)
	v.teleport_bullet(0, Vector2(0, 0))
	await physics(5)
	assert_eq(hits, 1, "hit before the pause")
	_pause()
	await physics(5)
	_resume()
	await physics(5)
	assert_eq(hits, 1, "the steady overlap is not counted again after resume")


func test_parked_records_never_reach_the_next_life() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1))
	await physics(2)
	_pause()
	v.teleport_bullet(0, Vector2(0, 0))
	await physics(3)
	v.clear_all_bullets() # the volley dies (pooled) while the record is parked
	await idle(2)
	var reused: BulletVolley2D = factory.spawn_volley(_data(1))
	_resume()
	await physics(5)
	assert_eq(hits, 0, "the dead life's parked overlap was not replayed into the new life")
	assert_true(reused.is_bullet_status_enabled(0), "the reused bullet is untouched")


## No hidden cap on parked overlaps: every bullet that starts touching a
## target during the pause registers on resume, however many there are.
func test_thousands_of_overlaps_started_while_paused_all_hit() -> void:
	const N := 5000 # past the old 4096 park cap
	var d := _data()
	var transforms: Array[Transform2D] = []
	for i in N:
		transforms.append(Transform2D(0.0, Vector2(-300, (i % 50) * 2.0)))
	d.transforms = transforms
	var v: BulletVolley2D = factory.spawn_volley(d)
	await physics(2)
	_pause()
	for i in N:
		v.teleport_bullet(i, Vector2(0, 0)) # all into the wall while paused
	await physics(4)
	assert_eq(hits, 0, "nothing drains while paused")
	_resume()
	await physics(4)
	assert_eq(hits, N, "every parked overlap hits once after the resume")


## One body, two shapes. A parked overlap is keyed per (bullet, target, kind),
## so a partial shape exit must not cancel it: the pair still overlapping
## replays exactly once on resume.
func _two_shape_wall(origin: Vector2) -> StaticBody2D:
	var wall := StaticBody2D.new()
	wall.position = origin
	wall.collision_layer = 4
	wall.collision_mask = 0
	for offset in [Vector2(-10, 0), Vector2(10, 0)]:
		var cs := CollisionShape2D.new()
		var box := RectangleShape2D.new()
		box.size = Vector2(24, 24)
		cs.shape = box
		cs.position = offset
		wall.add_child(cs)
	add(wall)
	return wall


func test_a_partial_multishape_exit_keeps_the_parked_overlap() -> void:
	var wall := _two_shape_wall(Vector2(500, 0))
	var v: BulletVolley2D = factory.spawn_volley(_data())
	await physics(2)
	_pause()
	v.teleport_bullet(0, Vector2(500, 0)) # inside BOTH shapes
	await physics(5)
	wall.position = Vector2(520, 0) # the +10 shape leaves; the -10 one still overlaps
	await physics(5)
	_resume()
	await physics(8)
	assert_eq(hits, 1, "the overlap that still exists replays exactly once")
	assert_eq(v.get_bullet_collision_count(0), 1, "and counts it")


## The queue readout sees what the flush queued, one frame before the drain:
## the record names the bullet, the target, its kind (0 area / 1 body) and the
## queue-time collision epoch (0 is a valid epoch, so only the row matters).
func test_the_collision_queue_readout_exposes_the_queued_record() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	await physics(2)
	v.teleport_bullet(0, Vector2(0, 0)) # into the arena wall
	var queued: Dictionary = {}
	for i in 6:
		await physics(1)
		var q: Dictionary = v.debug_get_collision_queue()
		if int(q.get("size", 0)) > 0:
			queued = q
			break
	assert_eq(int(queued.get("size", 0)), 1, "one record queued before the drain")
	var rows: Array = queued.get("rows", [])
	assert_eq(rows.size(), 1, "one row")
	if rows.size() == 1:
		assert_eq(int(rows[0]["bullet"]), 0, "bullet index")
		assert_eq(int(rows[0]["target_id"]), arena_wall.get_instance_id(), "target id")
		assert_eq(int(rows[0]["type"]), 1, "a body (0 = area, 1 = body)")
		assert_gte(int(rows[0]["epoch"]), 0, "the queue-time epoch is readable")
	await physics(2)
	assert_eq(hits, 1, "the queued record drained into exactly one hit")
	assert_eq(int(v.debug_get_collision_queue().get("size", -1)), 0, "the queue is empty after the drain")


## A paused factory parks overlaps instead of queueing them; the readout shows
## the parked set (with its shape-pair count) and the replay empties it.
func test_the_paused_overlaps_readout_matches_the_parked_set() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	await physics(2)
	_pause()
	v.teleport_bullet(0, Vector2(0, 0))
	await physics(3)
	var rows: Array = v.debug_get_paused_overlaps()
	assert_eq(rows.size(), 1, "one parked overlap")
	if rows.size() == 1:
		assert_eq(int(rows[0]["bullet"]), 0, "bullet index")
		assert_eq(int(rows[0]["target_id"]), arena_wall.get_instance_id(), "target id")
		assert_eq(int(rows[0]["type"]), 1, "a body")
		assert_eq(int(rows[0]["pairs"]), 1, "one shape pair")
	assert_eq(int(v.debug_get_collision_queue().get("size", -1)), 0, "a parked overlap is not queued")
	_resume()
	await physics(3)
	assert_eq(v.debug_get_paused_overlaps().size(), 0, "the replay empties the parked set")
	assert_eq(hits, 1, "and reports the overlap once")
