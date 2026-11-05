extends BlastTest
## collision_dedup_by_object (default true) collapses the per-SHAPE overlap
## records of one target into one logical hit per (bullet, target) per drain
## window; legacy shape mode reports every shape; the window resets between
## drains and volleys; different bullets each count. The spawn data seeds the
## mode per life (BulletVolleyData2D.collision_dedup_by_object).

var hits := 0


func _on_body(_body: Object, _volley: BulletVolley2D, _idx: int) -> void:
	hits += 1


## Flies into the wall at 900 px/s (a stationary bullet never triggers
## AREA_BODY_ADDED). max 0 = infinite hits so the counter is under test.
func _bodied_data(n := 1) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 900.0, 30.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(0, 4 * i)))
	d.transforms = arr
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	return d


## Three boxes along the travel axis on ONE body, so a crossing bullet
## overlaps three shapes of the same target.
func _multishape_wall(origin: Vector2) -> StaticBody2D:
	var wall := StaticBody2D.new()
	wall.position = origin
	wall.collision_layer = 4
	wall.collision_mask = 2
	for offset in [Vector2(-30, 0), Vector2(0, 0), Vector2(30, 0)]:
		var cs := CollisionShape2D.new()
		var box := RectangleShape2D.new()
		box.size = Vector2(20, 120)
		cs.shape = box
		cs.position = offset
		wall.add_child(cs)
	add(wall)
	return wall


func before_each() -> void:
	await super()
	hits = 0
	factory.body_entered.connect(_on_body)
	_multishape_wall(Vector2(200, 0))
	await physics()


func _wait(cond: Callable, frames := 20) -> void:
	for i in frames:
		await physics()
		if cond.call():
			return


func test_object_mode_counts_once() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data())
	assert_true(v.get_collision_dedup_by_object(), "collision_dedup_by_object defaults to true")
	await _wait(func(): return hits > 0)
	assert_eq(hits, 1, "three-shape target reports exactly 1 hit")
	assert_eq(v.get_bullet_collision_count(0), 1, "collision count is 1")
	await physics(20)
	# A per-shape implementation would produce >= 9 here.
	assert_lte(v.get_bullet_collision_count(0), 3, "count bounded by overlaps, not shapes")
	assert_lte(hits, 3, "signal count bounded the same way")


func test_shape_mode_reports_every_shape() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data())
	v.set_collision_dedup_by_object(false)
	assert_false(v.get_collision_dedup_by_object(), "property round-trips to false")
	await _wait(func(): return hits >= 3)
	assert_gte(hits, 3, "shape mode reports >= 3 hits for a 3-shape body")


func test_window_resets_between_volleys() -> void:
	var v1: BulletVolley2D = factory.spawn_volley(_bodied_data())
	await _wait(func(): return v1.get_bullet_collision_count(0) >= 1)
	assert_eq(v1.get_bullet_collision_count(0), 1, "first volley hits")
	hits = 0
	_multishape_wall(Vector2(400, 0))
	await physics()
	var v2: BulletVolley2D = factory.spawn_volley(_bodied_data())
	await _wait(func(): return v2.get_bullet_collision_count(0) >= 1)
	assert_eq(v2.get_bullet_collision_count(0), 1, "a later volley on the same wall still registers")
	assert_lte(v2.get_bullet_collision_count(0), 3, "bounded by overlaps")


func test_different_bullets_each_count() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data(2))
	await _wait(func(): return v.get_bullet_collision_count(0) >= 1 and v.get_bullet_collision_count(1) >= 1)
	assert_eq(v.get_bullet_collision_count(0), 1, "bullet 0 registered a hit")
	assert_eq(v.get_bullet_collision_count(1), 1, "bullet 1 registered a hit too")
	assert_lte(v.get_bullet_collision_count(0), 3, "bullet 0 bounded by overlaps")
	assert_lte(v.get_bullet_collision_count(1), 3, "bullet 1 bounded by overlaps")


func test_property_runtime_round_trip() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data())
	assert_true(v.get_collision_dedup_by_object(), "new volley defaults to true")
	v.set_collision_dedup_by_object(false)
	assert_false(v.get_collision_dedup_by_object(), "setter applies")
	v.set_collision_dedup_by_object(true)
	assert_true(v.get_collision_dedup_by_object(), "setter applies back")


func test_the_spawn_data_seeds_the_mode_every_life() -> void:
	var d := _bodied_data()
	assert_true(d.collision_dedup_by_object, "the data defaults to object mode")
	d.collision_dedup_by_object = false
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_false(v.get_collision_dedup_by_object(), "a shape-mode life from the data")
	await _wait(func(): return hits >= 3)
	assert_gte(hits, 3, "and it reports every shape")
	v.clear_all_bullets()
	await idle(1)
	var again: BulletVolley2D = factory.spawn_volley(_bodied_data())
	assert_eq(again, v, "the same volley, a new life")
	assert_true(again.get_collision_dedup_by_object(), "object mode again from the next data")


## A target whose script getter (read while the overlap is queued: a bouncing
## volley reads the target's velocity) takes the bullet out leaves no dedup key
## behind: the key is marked only together with the record it guards. A stale
## key once swallowed the bullet's later, real re-entry for good (an empty
## drain never cleared it).
class DisablingTarget extends StaticBody2D:
	var volley: BulletVolley2D
	var armed := true

	func _get(property: StringName) -> Variant:
		if armed and volley != null and property in [&"linear_velocity", &"velocity", &"constant_linear_velocity"]:
			armed = false
			volley.disable_bullet(0)
		return null


func test_a_getter_that_takes_the_bullet_out_leaves_no_stale_dedup_key() -> void:
	var target := DisablingTarget.new()
	target.collision_layer = 4
	target.collision_mask = 0
	var cs := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = Vector2(160, 160)
	cs.shape = box
	target.add_child(cs)
	target.position = Vector2(600, 0)
	add(target)
	await idle(2)
	var d := H.make_volley_data(2, 0.0, 30.0)
	d.monitorable = true
	d.collision_mask = 12 # 4 = the target | 8 = the bounce mask (nothing on it)
	d.bounce_mask = 8 # a bouncing volley reads the target's motion while queueing
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	d.transforms = [Transform2D(0.0, Vector2(600, 0)), Transform2D(0.0, Vector2(1200, 0))]
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.set_is_auto_pooling_enabled(false)
	target.volley = v
	for k in 6:
		await physics(1)
		if not v.is_bullet_status_enabled(0):
			break
	assert_false(v.is_bullet_status_enabled(0), "the getter took bullet 0 out")
	assert_eq(v.get_bullet_collision_count(0), 0, "no hit was counted")
	for k in 3:
		await physics(1) # empty drains
	v.enable_bullet(0) # woken inside the target steps later: Godot reports a new entry
	for k in 10:
		await physics(1)
	assert_eq(v.get_bullet_collision_count(0), 1, "the re-entry after the wake is a real hit")
	v.disable_bullet(0)
	v.disable_bullet(1)


## The undrained-window half of the dedup contract: keys describe ONE drain
## window. A recorded overlap whose window never drains (the volley parks
## first) leaves stale keys behind; a wake bumps the bullet's collision epoch,
## so the new entry must not be gated by a key from the old life. A still
## bullet spawned inside the wall queues the overlap; parking the volley keeps
## that window undrained.
func test_a_wake_inside_a_target_after_a_park_reports_the_hit() -> void:
	var d := H.make_volley_data(1, 0.0, 30.0)
	d.transforms = [Transform2D(0.0, Vector2(200, 0))] # inside the suite's wall
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.set_is_auto_pooling_enabled(false)
	for k in 20: # wait for the queued window (a mark exists), before any drain
		await physics(1)
		if int(v.debug_dedup_stats().get("used", 0)) > 0:
			break
	assert_gt(int(v.debug_dedup_stats().get("used", 0)), 0, "the overlap is queued before the drain")
	v.disable_bullet(0) # the last bullet out parks the volley; the window stays undrained
	await physics(2)
	assert_true(v.is_parked(), "parked with the queued window")
	v.enable_bullet(0) # a wake inside the target: Godot reports a new entry
	for k in 10:
		await physics(1)
		if hits > 0:
			break
	assert_eq(hits, 1, "the woken bullet reports the overlap exactly once")
	assert_eq(v.get_bullet_collision_count(0), 1, "and counts it")
	v.disable_bullet(0)


## The same contract around a factory pause: the new entry waits in the parked
## set, and the resume replay must not consult keys left over from the
## pre-pause window (the disable/wake inside the pause moved the epoch).
func test_a_wake_inside_a_target_while_paused_replays_on_resume() -> void:
	var d := H.make_volley_data(1, 0.0, 30.0)
	d.transforms = [Transform2D(0.0, Vector2(200, 0))] # inside the suite's wall
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.set_is_auto_pooling_enabled(false)
	for k in 20: # wait for the queued window (a mark exists), before any drain
		await physics(1)
		if int(v.debug_dedup_stats().get("used", 0)) > 0:
			break
	assert_gt(int(v.debug_dedup_stats().get("used", 0)), 0, "the overlap is queued before the pause")
	factory.set_is_factory_processing_bullets(false)
	v.disable_bullet(0)
	await physics(1) # the disabled shape leaves the world before the wake
	assert_true(v.is_parked(), "parked while paused")
	v.enable_bullet(0) # a new entry during the pause waits in the parked set
	await physics(4)
	assert_eq(hits, 0, "nothing drains while paused")
	factory.set_is_factory_processing_bullets(true)
	for k in 10:
		await physics(1)
		if hits > 0:
			break
	assert_eq(hits, 1, "the parked overlap replays exactly once")
	assert_eq(v.get_bullet_collision_count(0), 1, "and counts it")
