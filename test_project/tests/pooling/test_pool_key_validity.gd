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



## A volley that pools itself inside its own tick stays pooled (the pop skips
## it), and the pop must then skip a queued-for-deletion entry below it too:
## handing that one out made a valid spawn fail ("queued for deletion").
func test_pop_skips_a_dying_entry_below_the_ticked_volley() -> void:
	var plain := H.make_volley_data(2, 0.0, 30.0)
	plain.collision_shape = H.make_circle_shape(6.0)
	var moving := H.make_volley_data(2, 600.0, 30.0)
	moving.transforms = [Transform2D(), Transform2D(0.0, Vector2(0, 20))]
	moving.monitorable = true
	moving.set_collision_mask_from_array([3])
	moving.collision_shape = H.make_circle_shape(6.0)
	make_wall(Vector2(60, 0))
	await physics()
	# Two pooled entries; x takes the top one, the other is then queue_free'd
	# and stays at the bottom of the bucket.
	var a: BulletVolley2D = factory.spawn_volley(plain)
	var b: BulletVolley2D = factory.spawn_volley(plain)
	await idle(1)
	factory.clear_active_bullets()
	await idle(1)
	assert_true(a.is_pooled() and b.is_pooled(), "both pooled")
	var x: BulletVolley2D = factory.spawn_volley(moving)
	assert_true(x == a or x == b, "x reused a pooled entry")
	var dying: BulletVolley2D = b if x == a else a
	var spawned: Array = []
	factory.body_entered.connect(func(_b, v: BulletVolley2D, _i) -> void:
		if not spawned.is_empty():
			return
		# The bottom entry starts dying (deleted at the end of this frame),
		# then both bullets drain: x pools itself while its own drain runs.
		dying.queue_free()
		v.disable_bullet(0)
		v.disable_bullet(1)
		spawned.append(factory.spawn_volley(plain)))
	for k in 30:
		await physics(1)
		if not spawned.is_empty():
			break
	assert_eq(spawned.size(), 1, "the handler ran")
	expect_no_errors()
	assert_true(spawned[0] != null and spawned[0] != x and spawned[0] != dying, "the spawn got a usable volley")


func _bucket_count(info: Dictionary, amount: int, shape_type: int, colors: bool) -> int:
	for k in info:
		var key: VolleyPoolKey2D = k
		if key.amount_bullets == amount and key.shape_type == shape_type and key.use_bullet_colors == colors:
			return int(info[k])
	return -1


func _hits() -> int:
	return int(factory.debug_get_pool_hit_stats().get("hits", 0))


## A warm bucket hands back the SAME instance life after life (this is what
## "pool reuse" means: no allocation), counting exactly one hit per life, and
## a second, unrelated bucket is never touched by any of those lives.
func test_a_warm_bucket_reuses_one_instance_for_every_life() -> void:
	var capsule_data := _shaped(3, _capsule())
	var circle_data := _shaped(4, circ)
	factory.populate_bullets_pool(VolleyPoolKey2D.make(3, PhysicsServer2D.SHAPE_CAPSULE), capsule_data, 1)
	factory.populate_bullets_pool(VolleyPoolKey2D.make(4, PhysicsServer2D.SHAPE_CIRCLE), circle_data, 1)
	factory.debug_reset_pool_stats()
	var v: BulletVolley2D = factory.spawn_volley(capsule_data)
	assert_eq(_hits(), 1, "the first life is a hit")
	for life in 3:
		for i in 3:
			v.disable_bullet(i)
		assert_true(v.is_pooled(), "life %d pooled itself" % (life + 1))
		var again: BulletVolley2D = factory.spawn_volley(capsule_data)
		assert_same(again, v, "life %d is the very same instance" % (life + 2))
		assert_eq(_hits(), life + 2, "life %d counted one more hit" % (life + 2))
		v = again
	assert_eq(factory.debug_get_pool_hit_stats().get("misses", 0), 0, "no miss in any of the four lives")
	var info: Dictionary = factory.debug_get_bullets_pool_info()
	assert_eq(_bucket_count(info, 4, PhysicsServer2D.SHAPE_CIRCLE, false), 1, "the other bucket still holds its one entry")
	assert_eq(_bucket_count(info, 3, PhysicsServer2D.SHAPE_CAPSULE, false), -1, "this bucket is empty while the life flies")
	for i in 3:
		v.disable_bullet(i)
	assert_eq(v.debug_get_life_state(), "pooled", "the last life pooled itself")
	info = factory.debug_get_bullets_pool_info()
	assert_eq(_bucket_count(info, 3, PhysicsServer2D.SHAPE_CAPSULE, false), 1, "and its bucket holds it again")
	assert_eq(_bucket_count(info, 4, PhysicsServer2D.SHAPE_CIRCLE, false), 1, "while the other bucket never moved")


## A pooled volley a script queue_frees is a dead pool entry: the pool drops
## it and the next spawn of that key allocates a different, live instance (a
## miss), never the freed one.
func test_a_queue_freed_pooled_entry_is_dropped_and_the_next_spawn_allocates() -> void:
	var d := _shaped(2, circ)
	var key := VolleyPoolKey2D.make(2, PhysicsServer2D.SHAPE_CIRCLE)
	factory.populate_bullets_pool(key, d, 1)
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_eq(_hits(), 1, "the spawn reused the warm entry")
	for i in 2:
		v.disable_bullet(i)
	assert_true(v.is_pooled(), "it pooled itself again")
	factory.debug_reset_pool_stats()
	v.queue_free()
	await idle(2)
	assert_false(is_instance_valid(v), "the pooled volley is gone")
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "the pool dropped the dead entry")
	var w: BulletVolley2D = factory.spawn_volley(d)
	assert_not_same(w, v, "a different instance")
	assert_true(is_instance_valid(w), "a live instance")
	assert_false(w.is_queued_for_deletion(), "not queued for deletion")
	assert_true(w.is_volley_active(), "and it is active")
	assert_eq(factory.debug_get_pool_hit_stats().get("misses", 0), 1, "the spawn was a miss")
	assert_eq(_hits(), 0, "not a hit")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books ok")


## The pool key is (amount, shape type, per-bullet colors): every combination
## spawns into the bucket its own data maps to, and that bucket is what the
## volley reports.
func test_every_shape_and_color_combination_buckets_by_its_own_key() -> void:
	var cases: Array = [
		[PhysicsServer2D.SHAPE_CIRCLE, circ],
		[PhysicsServer2D.SHAPE_RECTANGLE, _rect()],
		[PhysicsServer2D.SHAPE_CAPSULE, _capsule()],
	]
	for case in cases:
		for colors in [false, true]:
			var d := _shaped(2, case[1])
			d.use_bullet_colors = colors
			if colors:
				d.bullet_colors = [Color.RED, Color.BLUE]
			var v: BulletVolley2D = factory.spawn_volley(d)
			var expected: VolleyPoolKey2D = BulletFactory2D.debug_expected_pool_key(d)
			var bucket: VolleyPoolKey2D = factory.debug_get_pool_bucket(v)
			var where := "shape %d, colors %s" % [case[0], colors]
			assert_eq(expected.shape_type, case[0], "%s: the data maps to its shape" % where)
			assert_eq(expected.use_bullet_colors, colors, "%s: the data maps to its color mode" % where)
			assert_eq(bucket.amount_bullets, 2, "%s: amount" % where)
			assert_true(bucket.equals(expected), "%s: the spawn's bucket is the expected key" % where)
			v.clear_all_bullets()
			await idle(1)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books ok")
