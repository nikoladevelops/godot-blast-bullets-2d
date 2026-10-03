extends BlastTest
## collision_dedup_by_object (default true) collapses the per-SHAPE overlap
## records of one target into one logical hit per (bullet, target) per drain
## window; legacy shape mode reports every shape; the window resets between
## drains and volleys; different bullets each count.

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
	assert_gte(v1.get_bullet_collision_count(0), 1, "first volley hits")
	hits = 0
	_multishape_wall(Vector2(400, 0))
	await physics()
	var v2: BulletVolley2D = factory.spawn_volley(_bodied_data())
	await _wait(func(): return v2.get_bullet_collision_count(0) >= 1)
	assert_gte(v2.get_bullet_collision_count(0), 1, "a later volley on the same wall still registers")
	assert_lte(v2.get_bullet_collision_count(0), 3, "bounded by overlaps")


func test_different_bullets_each_count() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data(2))
	await _wait(func(): return v.get_bullet_collision_count(0) >= 1 and v.get_bullet_collision_count(1) >= 1)
	assert_gte(v.get_bullet_collision_count(0), 1, "bullet 0 registered a hit")
	assert_gte(v.get_bullet_collision_count(1), 1, "bullet 1 registered a hit too")
	assert_lte(v.get_bullet_collision_count(0), 3, "bullet 0 bounded by overlaps")
	assert_lte(v.get_bullet_collision_count(1), 3, "bullet 1 bounded by overlaps")


func test_property_runtime_round_trip() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data())
	assert_true(v.get_collision_dedup_by_object(), "new volley defaults to true")
	v.set_collision_dedup_by_object(false)
	assert_false(v.get_collision_dedup_by_object(), "setter applies")
	v.set_collision_dedup_by_object(true)
	assert_true(v.get_collision_dedup_by_object(), "setter applies back")
