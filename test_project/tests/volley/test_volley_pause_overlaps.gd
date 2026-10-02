extends BlastTest
## Overlaps around a factory pause (set_is_factory_processing_bullets):
## an overlap that starts while paused registers exactly once on resume; one
## that starts AND ends during the pause is cancelled; overlaps already
## counted before the pause are never re-reported; parked records never
## survive into a pooled volley's next life.

var hits := 0


func _on_body(_b: Object, _v: DirectionalBullets2D, _i: int) -> void:
	hits += 1


func _data(max_hits := 0) -> DirectionalBulletsData2D:
	var d := H.make_directional_data(1, 0.0, 30.0)
	d.transforms = [Transform2D(0.0, Vector2(-300, 0))]
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = max_hits
	return d


func before_each() -> void:
	await super()
	hits = 0
	factory.directional_body_entered.connect(_on_body)
	make_wall(Vector2(0, 0), Vector2(40, 40))
	await physics()


func _pause() -> void:
	factory.set_is_factory_processing_bullets(false)


func _resume() -> void:
	factory.set_is_factory_processing_bullets(true)


func test_overlap_started_while_paused_hits_once() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
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
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
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
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
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
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(1))
	await physics(2)
	_pause()
	v.teleport_bullet(0, Vector2(0, 0))
	await physics(3)
	v.clear_all_bullets() # the volley dies (pooled) while the record is parked
	await idle(2)
	var reused: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(1))
	_resume()
	await physics(5)
	assert_eq(hits, 0, "the dead life's parked overlap was not replayed into the new life")
	assert_true(reused.is_bullet_status_enabled(0), "the reused bullet is untouched")
