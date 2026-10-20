extends BlastTest
## Every area_entered / body_entered callback of every node (a game's own
## hurtbox too) runs while Godot's physics server flushes its queries, and
## the server refuses shape and monitorable changes there. Bullets spawned,
## reused or cleared from such a handler still end up in the right physics
## state (core/physics_flush2d.hpp): reused bullets collide, monitorable
## bullets are seen, cleared bullets stop colliding, and nothing reports an
## error.


## A monitorable 3-bullet volley at the origin and a game Area2D that sees
## it: `on_flush` runs once inside the flush.
func _flush_trigger(on_flush: Callable) -> Array:
	var d := H.make_still_data(3)
	d.monitorable = true
	factory.spawn_volley(d)
	var hurtbox := make_area(Vector2.ZERO, Vector2(80, 80), 0)
	hurtbox.collision_mask = 0xFFFFFFFF
	var fired: Array = []
	hurtbox.area_shape_entered.connect(func(_rid: RID, _a: Area2D, _s: int, _l: int) -> void:
		if fired.is_empty():
			fired.append(true)
			on_flush.call())
	return fired


func _wait_for(fired: Array) -> void:
	for k in 30:
		if not fired.is_empty():
			break
		await physics(1)
	assert_false(fired.is_empty(), "the game area saw the bullets")


## Still 2-bullet data at `x` (the pool key the reuse test shares).
func _pair_at(x: float, monitorable := false) -> BulletVolleyData2D:
	var d := H.make_still_data(2)
	d.transforms = [Transform2D(0.0, Vector2(x, 0)), Transform2D(0.0, Vector2(x, 16))]
	d.monitorable = monitorable
	return d


## A detector Area2D at `pos`: returns its live area_shape_entered count.
func _detector(pos: Vector2) -> Array:
	var area := make_area(pos, Vector2(60, 60), 0)
	area.collision_mask = 0xFFFFFFFF
	var seen: Array = []
	area.area_shape_entered.connect(func(_rid: RID, _a: Area2D, _s: int, _l: int) -> void: seen.append(true))
	return seen


func test_a_volley_reused_inside_the_flush_collides() -> void:
	var pooled: BulletVolley2D = factory.spawn_volley(_pair_at(-500, true))
	pooled.clear_all_bullets()
	assert_true(pooled.is_pooled(), "back in the pool")
	var seen := _detector(Vector2(500, 0))
	var reused: Array = []
	var fired := _flush_trigger(func() -> void: reused.append(factory.spawn_volley(_pair_at(500, true))))
	await _wait_for(fired)
	assert_eq(reused[0], pooled, "the handler reused the pooled volley")
	for k in 10:
		if not seen.is_empty():
			break
		await physics(1)
	assert_false(seen.is_empty(), "the reused bullets collide (a game area sees them)")
	expect_no_errors()


func test_monitorable_bullets_spawned_inside_the_flush_are_seen() -> void:
	var seen := _detector(Vector2(-300, 0))
	var fired := _flush_trigger(func() -> void: factory.spawn_volley(_pair_at(-300, true)))
	await _wait_for(fired)
	for k in 10:
		if not seen.is_empty():
			break
		await physics(1)
	assert_false(seen.is_empty(), "a game area sees the monitorable bullets")
	expect_no_errors()


func test_bullets_cleared_inside_the_flush_stop_colliding() -> void:
	var cleared: BulletVolley2D = factory.spawn_volley(_pair_at(300, true))
	var fired := _flush_trigger(func() -> void: cleared.clear_all_bullets())
	await _wait_for(fired)
	assert_true(cleared.is_pooled(), "cleared into the pool")
	var seen := _detector(Vector2(300, 0)) # arrives after the clear
	for k in 6:
		await physics(1)
	assert_eq(seen.size(), 0, "nothing collides with the cleared bullets")
	expect_no_errors()
