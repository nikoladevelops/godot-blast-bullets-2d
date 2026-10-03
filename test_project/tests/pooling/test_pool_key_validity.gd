extends BlastTest
## Pool key validity (when reuse is legal): exact (amount + shape) hits;
## different amount / shape -> miss + allocate (never silent reuse); capsule
## bucket isolated; per-bucket free drains only that bucket and is
## double-free safe; enable_volley with a mismatched size refuses cleanly.

var circ := H.make_circle_shape(6.0)


func _shaped(n: int, shape: Shape2D) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 200.0)
	d.collision_shape = shape
	return d


func _rect() -> RectangleShape2D:
	var r := RectangleShape2D.new()
	r.size = Vector2(14, 10)
	return r


func _capsule() -> CapsuleShape2D:
	var c := CapsuleShape2D.new()
	c.radius = 4.0
	c.height = 20.0
	return c


func test_exact_match_hits_and_mismatches_miss() -> void:
	var d4c := _shaped(4, circ)
	factory.populate_bullets_pool(VolleyPoolKey2D.make(4, PhysicsServer2D.SHAPE_CIRCLE), d4c, 2)
	assert_eq(factory.debug_get_bullets_pool_amount(), 2, "populated 2 in (4, circle)")
	factory.debug_reset_pool_stats()
	factory.spawn_volley(d4c)
	var s1: Dictionary = factory.debug_get_pool_hit_stats()
	assert_eq(s1.get("hits", 0), 1, "exact spawn hits")
	assert_eq(s1.get("misses", 0), 0)
	factory.debug_reset_pool_stats()
	factory.spawn_volley(_shaped(4, _rect()))
	var s2: Dictionary = factory.debug_get_pool_hit_stats()
	assert_eq(s2.get("misses", 0), 1, "rect misses the circle bucket")
	assert_eq(s2.get("hits", 0), 0)
	factory.debug_reset_pool_stats()
	factory.spawn_volley(_shaped(5, circ))
	assert_eq(factory.debug_get_pool_hit_stats().get("misses", 0), 1, "5 bullets miss the 4-bucket")


func test_capsule_bucket_isolated() -> void:
	factory.populate_bullets_pool(VolleyPoolKey2D.make(4, PhysicsServer2D.SHAPE_CIRCLE), _shaped(4, circ), 1)
	var d3k := _shaped(3, _capsule())
	factory.populate_bullets_pool(VolleyPoolKey2D.make(3, PhysicsServer2D.SHAPE_CAPSULE), d3k, 1)
	assert_eq(factory.debug_get_bullets_pool_amount(), 2, "capsule bucket added")
	factory.debug_reset_pool_stats()
	factory.spawn_volley(d3k)
	assert_eq(factory.debug_get_pool_hit_stats().get("hits", 0), 1, "capsule spawn hits the capsule bucket")


func test_per_bucket_free_isolates() -> void:
	var key4c := VolleyPoolKey2D.make(4, PhysicsServer2D.SHAPE_CIRCLE)
	factory.populate_bullets_pool(key4c, _shaped(4, circ), 2)
	factory.populate_bullets_pool(VolleyPoolKey2D.make(3, PhysicsServer2D.SHAPE_CAPSULE), _shaped(3, _capsule()), 1)
	factory.free_bullets_pool(key4c)
	await idle()
	assert_eq(factory.debug_get_bullets_pool_amount(), 1, "only the (4, circle) bucket drained")
	factory.free_bullets_pool(key4c)
	await idle()
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "double free of an empty bucket is safe")


func test_enable_size_mismatch_refuses_cleanly() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_shaped(2, circ))
	for i in 2:
		v.disable_bullet(i)
	await idle()
	var before: Dictionary = v.debug_get_volley_info()
	assert_false(v.enable_volley(_shaped(5, circ), Vector2.ZERO, 0), "mismatched enable returns false")
	expect_error("must match amount_bullets")
	assert_eq(v.debug_get_volley_info().get("amount_bullets", -1), before.get("amount_bullets", -2), "amount unchanged")
	assert_false(v.debug_get_volley_info().get("is_active", true), "still disabled")
