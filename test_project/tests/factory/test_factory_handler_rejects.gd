extends BlastTest
## Structural calls made from inside a hit handler (i.e. inside the physics
## frame) are refused, and the refusal must tell the user what works: the
## matching *_deferred twin, which runs on the next idle frame. A plain
## call_deferred() would still flush inside the physics frame (pinned in
## integration/test_engine_facts.gd), so recommending it is wrong.

var calls: Array[Callable] = []
var hit_volleys: Array = []


func _hit_data() -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 300.0, 8.0)
	d.transforms = [Transform2D(0.0, Vector2(0, 0))]
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0 # never dies: the handler keeps running
	return d


func _on_body(_body: Object, volley: BulletVolley2D, _idx: int) -> void:
	if hit_volleys.is_empty():
		hit_volleys.append(volley)
		for c in calls:
			c.call(volley)


func before_each() -> void:
	await super()
	calls.clear()
	hit_volleys.clear()
	factory.body_entered.connect(_on_body)
	make_wall(Vector2(200, 0))
	await physics()


func _hit_once() -> void:
	factory.spawn_volley(_hit_data())
	for i in 90:
		await physics()
		if not hit_volleys.is_empty():
			break
	assert_eq(hit_volleys.size(), 1, "the handler ran")


func test_reset_inside_handler_points_at_reset_deferred() -> void:
	calls.append(func(_v): factory.reset())
	await _hit_once()
	expect_error_sequence(["BulletFactory2D::reset cannot run inside a physics frame or while bullets are being processed (e.g. inside area_entered/body_entered/life_time_over handlers). Use reset_deferred() instead: it runs on the next idle frame (a plain call_deferred() still runs inside the physics frame)."])


func test_free_calls_inside_handler_point_at_their_deferred_twins() -> void:
	calls.append(func(_v): factory.free_active_bullets())
	calls.append(func(_v): factory.free_disabled_bullets())
	calls.append(func(_v): factory.free_bullets_pool())
	calls.append(func(_v): factory.free_attachments_pool())
	await _hit_once()
	expect_error_sequence([
		"Use free_active_bullets_deferred() instead",
		"Use free_disabled_bullets_deferred() instead",
		"Use free_bullets_pool_deferred() instead",
		"Use free_attachments_pool_deferred() instead",
	])


func test_shape_change_inside_handler_points_at_its_deferred_twin() -> void:
	calls.append(func(v): v.set_collision_shape_runtime(RectangleShape2D.new()))
	await _hit_once()
	expect_error_sequence(["Use set_collision_shape_runtime_deferred() instead"])


var shape_types_in_handler: Array = []


func test_deferred_shape_change_applies_on_the_next_idle_frame() -> void:
	shape_types_in_handler.clear()
	calls.append(func(v):
		v.set_collision_shape_runtime_deferred(RectangleShape2D.new())
		shape_types_in_handler.append(int(v.debug_get_shape_state().get("type", -1))))
	await _hit_once()
	var v: BulletVolley2D = hit_volleys[0]
	assert_eq(shape_types_in_handler, [PhysicsServer2D.SHAPE_CIRCLE], "still a circle inside the physics frame (queued, not applied)")
	await idle(2)
	assert_eq(int(v.debug_get_shape_state().get("type", -1)), PhysicsServer2D.SHAPE_RECTANGLE, "rectangle after the idle flush")
	expect_no_errors()


func test_reset_deferred_inside_handler_resets_on_the_next_idle_frame() -> void:
	calls.append(func(_v): factory.reset_deferred())
	await _hit_once()
	await idle(2)
	assert_eq(factory.debug_get_active_bullets_amount(), 0, "reset ran on the idle frame")
	expect_no_errors()
