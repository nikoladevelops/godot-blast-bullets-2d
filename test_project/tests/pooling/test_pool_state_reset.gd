extends BlastTest
## State-reset matrix (the no-leak proof): volley A seeded with EVERYTHING
## (homing, orbit, curves, pattern, custom data, timer, rotation, gravity,
## pooling flags off) is drained and reused as B, which must be neutral.
## Then a runtime shape change re-buckets, and a new amount never reuses.

var a: DirectionalBullets2D
var target: Node2D


func _seeded_data() -> DirectionalBulletsData2D:
	var d := H.make_directional_data(3, 250.0)
	d.collision_shape = H.make_circle_shape(6.0)
	return d


func before_each() -> void:
	await super()
	target = add(Node2D.new())
	target.position = Vector2(500, 0)
	a = factory.spawn_controllable_directional_bullets(_seeded_data())
	a.set_homing_smoothing(5.0)
	a.set_homing_take_control_of_texture_rotation(true)
	a.all_bullets_push_back_homing_target(target)
	a.all_bullets_enable_orbiting(48.0, DirectionalBullets2D.OrbitRight, DirectionalBullets2D.FaceTarget)
	var curv := BulletCurvesData2D.new()
	var sc := Curve.new()
	sc.add_point(Vector2(0, 0))
	sc.add_point(Vector2(1, 1))
	curv.movement_speed_curve = sc
	a.set_shared_bullet_curves_data(curv)
	var curve2d := Curve2D.new()
	curve2d.add_point(Vector2(0, 0))
	curve2d.add_point(Vector2(50, 50))
	a.all_bullets_set_movement_pattern_from_curve(curve2d, false, true)
	a.bullet_set_custom_data(0, Resource.new())
	a.multimesh_attach_time_based_function(5.0, func() -> void: pass)
	var rot := BulletRotationData2D.new()
	rot.rotation_speed = 3.0
	a.set_shared_bullet_rotation_data(rot)
	a.set_gravity(Vector2(0, 500))


func _drain_and_pool() -> void:
	for i in 3:
		a.disable_bullet(i)
	await idle(1)


func test_seeded_volley_has_everything() -> void:
	assert_false(a.get_is_wobble_enabled(), "wobble seeds from spawn data only")
	assert_true(a.bullet_check_has_homing_targets(0), "A has homing")
	assert_true(a.bullet_is_orbiting_enabled(0), "A orbiting")
	assert_true(a.has_shared_bullet_curves_data(), "A has curves")
	assert_true(a.has_bullet_movement_pattern(0), "A has a pattern")
	assert_eq(a.debug_get_timer_count(), 1, "A has a timer")


func test_pooling_off_volley_is_not_pooled() -> void:
	a.set_is_multimesh_auto_pooling_enabled(false)
	await _drain_and_pool()
	assert_eq(factory.debug_get_bullets_pool_amount(0), 0, "pooling-off volley not pooled")


func test_reuse_is_neutral() -> void:
	var gen_a: int = a.debug_get_volley_info().get("generation", -1)
	a.set_is_multimesh_auto_pooling_enabled(false)
	a.set_is_attachments_auto_pooling_enabled(false)
	await _drain_and_pool()
	a.set_is_multimesh_auto_pooling_enabled(true)
	a.set_is_attachments_auto_pooling_enabled(true)
	for i in 3:
		a.wake_bullet(i)
	await _drain_and_pool()
	assert_gte(factory.debug_get_bullets_pool_amount(0), 1, "pooled after flags restored")
	var b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_seeded_data())
	assert_eq(b, a, "B IS the pooled A (identity reuse)")
	assert_false(b.bullet_check_has_homing_targets(0), "no homing")
	assert_false(b.bullet_is_orbiting_enabled(0), "not orbiting")
	assert_false(b.has_shared_bullet_curves_data(), "no curves")
	assert_false(b.has_bullet_movement_pattern(0), "no pattern")
	assert_null(b.bullet_get_custom_data(0), "custom data null (strict)")
	assert_eq(b.debug_get_timer_count(), 0, "no timers")
	assert_false(b.has_shared_bullet_rotation_data(), "rotation cleared")
	assert_eq(b.get_gravity(), Vector2.ZERO, "gravity zero")
	assert_lt(b.get_bullet_velocity(0).y, 1.0, "no inherited fall speed")
	var info_b: Dictionary = b.debug_get_volley_info()
	assert_true(info_b.get("auto_pool_multimesh", false), "flags default")
	assert_eq(info_b.get("owner_spawner_id", -1), 0, "factory-owned")
	assert_gt(int(info_b.get("generation", 0)), gen_a, "generation bumped")


func test_shape_change_rebuckets() -> void:
	var rect := RectangleShape2D.new()
	rect.size = Vector2(20, 10)
	a.set_collision_shape_runtime(rect)
	assert_eq(a.debug_get_shape_state().get("type", -1), PhysicsServer2D.SHAPE_RECTANGLE, "now a rectangle")
	await _drain_and_pool()
	assert_gte(factory.debug_get_bullets_pool_amount(0), 1, "re-bucketed volley pooled under the rect key")
	factory.free_bullets_pool(0, MultiMeshPoolKey2D.make(3, PhysicsServer2D.SHAPE_CIRCLE))
	await idle()
	assert_gte(factory.debug_get_bullets_pool_amount(0), 1, "freeing the stale circle bucket keeps the rect-pooled volley")


func test_new_amount_never_reuses() -> void:
	await _drain_and_pool()
	factory.debug_reset_pool_stats()
	factory.spawn_controllable_directional_bullets(H.make_directional_data(7, 200.0))
	assert_gte(factory.debug_get_pool_hit_stats().get("directional_misses", 0), 1, "new amount allocates (miss)")
