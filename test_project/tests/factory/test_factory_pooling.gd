extends BlastTest
## Factory pooling + DX: spawner duplicate cache (swap + in-place edit),
## retarget-phase validation, debugger budget defaults/clamps/cycle, preview
## collision-ring overlay, pre-populated pool hits, size-mismatch misses.

var spawner: BulletSpawner2D


func _data(n: int = 3) -> BulletVolleyData2D:
	var data := H.make_volley_data(n, 250.0, 5.0)
	data.texture_size = Vector2(12, 12)
	return data


func before_each() -> void:
	await super()
	spawner = add(BulletSpawner2D.new())
	await idle(1)
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(_data(3))
	spawner.set_shooting_enabled(false)
	spawner.set_homing_enabled(false)
	spawner.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
	spawner.helper_bullets_amount = 4


func test_spawner_duplicate_cache() -> void:
	assert_true(spawner.shoot_once(), "first cached shot fires")
	var fired: int = spawner.get_volleys_fired()
	assert_true(spawner.shoot_once(), "second cached shot fires")
	assert_eq(spawner.get_volleys_fired(), fired + 1, "cached shot counted")
	spawner.set_spawn_data(_data(5))
	assert_true(spawner.shoot_once(), "shot after resource swap fires")
	var d: BulletVolleyData2D = spawner.get_spawn_data()
	d.max_life_time = 6.0
	await idle(1)
	assert_true(spawner.shoot_once(), "shot after in-place edit fires")


func test_retarget_phase_validation() -> void:
	var s2: BulletSpawner2D = add(BulletSpawner2D.new())
	s2.set_shooting_enabled(false)
	await idle(1)
	s2.set_homing_retarget_phase(0.0)
	spawner.set_homing_retarget_phase(0.0)
	spawner.get_parent().remove_child(spawner)
	add_child(spawner)
	await idle(1)
	spawner.set_homing_retarget_phase(0.25)
	assert_almost_eq(spawner.get_homing_retarget_phase(), 0.25, 0.0001, "explicit phase respected")
	spawner.set_homing_retarget_phase(-1.0)
	expect_error_sequence(["BulletSpawner2D: homing_retarget_phase must be finite and >= 0, keeping the old value."])
	assert_almost_eq(spawner.get_homing_retarget_phase(), 0.25, 0.0001, "negative phase rejected")


func test_debugger_budget_and_preview_rings() -> void:
	assert_eq(factory.get_debugger_max_providers(), 0, "budget default unlimited")
	assert_true(factory.get_debugger_draw_inactive(), "draw inactive default true")
	factory.set_debugger_max_providers(2)
	assert_eq(factory.get_debugger_max_providers(), 2, "budget set")
	factory.set_debugger_max_providers(-5)
	assert_eq(factory.get_debugger_max_providers(), 0, "negative budget clamps to unlimited")
	factory.set_debugger_draw_inactive(false)
	assert_false(factory.get_debugger_draw_inactive(), "draw inactive toggle")
	factory.set_debugger_draw_inactive(true)
	factory.set_is_debugger_enabled(true)
	factory.spawn_volley(_data(3))
	await physics()
	factory.set_is_debugger_enabled(false)
	assert_false(factory.get_is_debugger_enabled(), "debugger cycle survived")
	spawner.set_preview_draw_collision_rings(true)
	assert_true(spawner.get_preview_draw_collision_rings(), "rings toggle")
	spawner.set_preview_collision_ring_width(2.0)
	assert_almost_eq(spawner.get_preview_collision_ring_width(), 2.0, 0.0001, "ring width set")
	var ring_data := _data(3)
	ring_data.collision_shape = H.make_circle_shape(8.0)
	spawner.set_spawn_data(ring_data)
	spawner.show_preview_during_runtime = true
	spawner.show_pattern_preview = true
	await idle(1)
	assert_eq(spawner.collect_spawn_transforms().size(), 4, "rings preview collects")


func test_pool_hit_observability() -> void:
	factory.debug_reset_pool_stats()
	var pop_data := _data(4)
	factory.populate_bullets_pool(BulletFactory2D.debug_expected_pool_key(pop_data), pop_data, 2)
	assert_eq(factory.debug_get_bullets_pool_amount(), 2, "pre-populated 2")
	factory.spawn_volley(pop_data)
	await idle(1)
	assert_eq(factory.debug_get_pool_hit_stats().get("hits", 0), 1, "reuse counted as hit")
	var carved := _data(4)
	carved.transforms = [Transform2D.IDENTITY, Transform2D(0.0, Vector2(10, 0))]
	factory.spawn_volley(carved)
	await idle(1)
	assert_eq(factory.debug_get_pool_hit_stats().get("misses", 0), 1, "size mismatch counted as miss")


## The collision debugger draws every shape where the physics shape is, in
## the debugger color: its multimesh buffer (8 transform + 4 color floats per
## instance, uploaded in one set_buffer per volley per tick) holds each
## bullet's global pose, frame after frame.
func test_debugger_draws_every_shape_where_it_is() -> void:
	factory.debugger_color = Color(1, 0, 0, 0.5)
	factory.is_debugger_enabled = true
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(5, 100.0, 30.0))
	for k in 2:
		await physics(3)
		var dbg: Node = factory.get_node("BulletVolleysDebugger")
		assert_eq(dbg.get_child_count(true), 1, "one debug multimesh per volley")
		var buf: PackedFloat32Array = (dbg.get_child(0, true) as MultiMeshInstance2D).multimesh.buffer
		assert_eq(buf.size(), 5 * 12, "transform + color per shape")
		var bad := ""
		for i in 5:
			var b := i * 12
			var got := Vector2(buf[b + 3], buf[b + 7])
			var want: Vector2 = v.get_bullet_transform(i).origin
			if got.distance_to(want) > 0.01:
				bad += "shape %d at %s, bullet at %s; " % [i, got, want]
			if Color(buf[b + 8], buf[b + 9], buf[b + 10], buf[b + 11]) != Color(1, 0, 0, 0.5):
				bad += "shape %d color; " % i
		assert_eq(bad, "", "pass %d: every shape drawn on its bullet in the debugger color" % k)


## A 2D canvas item culls by the bounds it had when it was drawn, while the
## plugin's instances move every tick: every multimesh the plugin draws
## (bullets, trail and one-shot effect shards, debugger shapes) carries fixed
## bounds wider than any world, so nothing is culled by stale bounds and no
## bullet vanishes far from the origin. (The debugger had none: its shapes
## disappeared from moved volleys; the old +-100000 px box hid far bullets.)
func test_every_plugin_multimesh_is_never_culled() -> void:
	factory.is_debugger_enabled = true
	var d := H.make_volley_data(3, 100.0, 30.0)
	d.effect_layers = [H.make_effect_layer(EffectLayerData2D.EFFECT_TRAIL_FOLLOW, 2), H.make_effect_layer(EffectLayerData2D.EFFECT_ON_CLEAR, 2)]
	var v: BulletVolley2D = factory.spawn_volley(d)
	await physics(2)
	v.clear_bullet(0) # the On Clear flash makes the one-shot effect shards
	await physics(2)
	var meshes: Array = [v.multimesh]
	for c in v.get_children(true):
		if c is MultiMeshInstance2D:
			meshes.append(c.multimesh)
	for path in ["BulletVolleysDebugger", "SpriteEffectsContainer"]:
		for c in factory.get_node(path).get_children(true):
			if c is MultiMeshInstance2D:
				meshes.append(c.multimesh)
	assert_gt(meshes.size(), 4, "volley, trail shards, effect shards and debugger nodes found")
	var far := AABB(Vector3(-1e9, -1e9, 0), Vector3(2e9, 2e9, 0))
	var bad := 0
	for mm in meshes:
		if not (mm as MultiMesh).custom_aabb.encloses(far):
			bad += 1
	assert_eq(bad, 0, "every multimesh's bounds cover +-1e9 px")


## Pool buckets are read out as data: {key: count}, each key naming its
## (amount_bullets, shape_type, use_bullet_colors) triple. Returns the count
## of the bucket matching that triple (-1 when no such bucket exists).
func _bucket_count(info: Dictionary, amount: int, shape_type: int, colors: bool) -> int:
	for k in info:
		var key: VolleyPoolKey2D = k
		if key.amount_bullets == amount and key.shape_type == shape_type and key.use_bullet_colors == colors:
			return int(info[k])
	return -1


func _pooled_data(n: int) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 0.0, 30.0)
	d.collision_shape = H.make_circle_shape(6.0)
	return d


## Pre-warming allocates exactly the requested bucket entries, and a spawn
## pops one of THAT bucket: the reported pool holds one entry less of exactly
## the key `debug_expected_pool_key(spawn_data)` names (amount 4, circle, no
## per-bullet colors), and the popped volley reports the same key as its
## bucket. This is the readout a game uses to verify its pre-warm actually
## matches the shots it fires.
func test_a_pre_warmed_bucket_pops_for_the_key_the_data_maps_to() -> void:
	var d := _pooled_data(4)
	var key: VolleyPoolKey2D = BulletFactory2D.debug_expected_pool_key(d)
	factory.populate_bullets_pool(key, d, 2)
	var info: Dictionary = factory.debug_get_bullets_pool_info()
	assert_eq(info.size(), 1, "one bucket")
	assert_eq(_bucket_count(info, 4, PhysicsServer2D.SHAPE_CIRCLE, false), 2, "pre-populated 2 entries")
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_not_null(v, "spawned from the warm bucket")
	info = factory.debug_get_bullets_pool_info()
	assert_eq(info.size(), 1, "still one bucket")
	assert_eq(_bucket_count(info, 4, PhysicsServer2D.SHAPE_CIRCLE, false), 1, "the spawn popped exactly one entry")
	var bucket: VolleyPoolKey2D = factory.debug_get_pool_bucket(v)
	assert_true(bucket.equals(key), "the live volley's bucket equals the data's expected key")
	assert_eq(bucket.amount_bullets, 4, "amount 4")
	assert_eq(bucket.shape_type, PhysicsServer2D.SHAPE_CIRCLE, "circle")
	assert_false(bucket.use_bullet_colors, "no per-bullet colors")
	assert_eq(factory.debug_get_pool_hit_stats().get("hits", 0), 1, "the spawn was a hit")


## Two distinct keys (a plain circle bucket and a colored circle bucket), both
## mapped from their own data: freeing one key drains exactly that bucket and
## leaves the other entry alone. Every key equals the key its data maps to.
func test_freeing_one_key_leaves_every_other_bucket_alone() -> void:
	var plain := _pooled_data(2)
	var colored := _pooled_data(2)
	colored.use_bullet_colors = true
	colored.bullet_colors = [Color.RED, Color.BLUE]
	var plain_key: VolleyPoolKey2D = BulletFactory2D.debug_expected_pool_key(plain)
	var colored_key: VolleyPoolKey2D = BulletFactory2D.debug_expected_pool_key(colored)
	assert_false(plain_key.use_bullet_colors, "the plain data maps to a plain key")
	assert_true(colored_key.use_bullet_colors, "the colored data maps to a colored key")
	factory.populate_bullets_pool(plain_key, plain, 2)
	factory.populate_bullets_pool(colored_key, colored, 1)
	assert_eq(factory.debug_get_bullets_pool_amount(), 3, "three entries in two buckets")
	factory.free_bullets_pool(plain_key)
	await idle()
	var info: Dictionary = factory.debug_get_bullets_pool_info()
	assert_eq(info.size(), 1, "only the colored bucket is left")
	assert_eq(_bucket_count(info, 2, PhysicsServer2D.SHAPE_CIRCLE, true), 1, "the colored bucket is untouched")
	assert_eq(_bucket_count(info, 2, PhysicsServer2D.SHAPE_CIRCLE, false), -1, "the plain bucket is gone")
	assert_eq(factory.debug_get_bullets_pool_amount(), 1, "one pooled volley left")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books ok")


## free_bullets_pool() with no key is the whole-pool teardown: every bucket is
## gone ({}) and the next spawn of any key is a MISS (nothing was silently
## kept alive).
func test_free_bullets_pool_without_a_key_empties_every_bucket() -> void:
	var plain := _pooled_data(4)
	var colored := _pooled_data(3)
	colored.use_bullet_colors = true
	colored.bullet_colors = [Color.RED, Color.GREEN, Color.BLUE]
	factory.populate_bullets_pool(BulletFactory2D.debug_expected_pool_key(plain), plain, 2)
	factory.populate_bullets_pool(BulletFactory2D.debug_expected_pool_key(colored), colored, 1)
	assert_eq(factory.debug_get_bullets_pool_amount(), 3, "two buckets warmed")
	factory.free_bullets_pool()
	await idle()
	assert_eq(factory.debug_get_bullets_pool_info(), {}, "every bucket drained")
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "nothing pooled")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books ok")
	factory.debug_reset_pool_stats()
	factory.spawn_volley(plain)
	assert_eq(factory.debug_get_pool_hit_stats().get("misses", 0), 1, "the next spawn of that key is a miss")
	assert_eq(factory.debug_get_pool_hit_stats().get("hits", 0), 0, "and no hit")


## The hit/miss counters describe every spawn: three spawns across three keys
## are three hits and no miss, each one draining exactly its own bucket, and
## the counters sum to the number of spawns (a spawn is never uncounted).
func test_every_spawn_counts_one_hit_or_miss_and_drains_its_own_bucket() -> void:
	var datas: Array[BulletVolleyData2D] = []
	for i in 3:
		var d := _pooled_data(2 + i) # 2, 3, 4 bullets: three distinct keys
		factory.populate_bullets_pool(BulletFactory2D.debug_expected_pool_key(d), d, 1)
		datas.append(d)
	assert_eq(factory.debug_get_bullets_pool_amount(), 3, "three warm buckets")
	factory.debug_reset_pool_stats()
	assert_eq(factory.debug_get_pool_hit_stats(), {"hits": 0, "misses": 0}, "the reset zeroes both counters")
	for i in 3:
		factory.spawn_volley(datas[i])
		var stats: Dictionary = factory.debug_get_pool_hit_stats()
		assert_eq(int(stats["hits"]), i + 1, "spawn %d counted one hit" % i)
		assert_eq(int(stats["misses"]), 0, "spawn %d counted no miss" % i)
		assert_eq(int(stats["hits"]) + int(stats["misses"]), i + 1, "hits + misses == the spawn count")
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "each hit drained exactly its own bucket")
	factory.spawn_volley(_pooled_data(5))
	assert_eq(factory.debug_get_pool_hit_stats().get("misses", 0), 1, "a key with no bucket misses")
	assert_eq(factory.debug_get_pool_hit_stats().get("hits", 0), 3, "and the hits stay")


func _hit_data_for_reset() -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 300.0, 8.0)
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0 # never dies: the handler runs once, the volley survives
	return d


## A reset_deferred() queued from inside a hit handler (the physics frame)
## runs later and must leave exactly ONE debugger node: the debugger is
## re-created at most once and its container is never duplicated, so the
## overlay of the next life-cycles keeps drawing.
func test_reset_deferred_from_a_hit_handler_keeps_one_debugger_node() -> void:
	factory.is_debugger_enabled = true
	make_wall(Vector2(200, 0))
	await physics()
	var hits: Array = [] # a lambda captures a bool by value: collect through an Array
	factory.body_entered.connect(func(_body: Object, _v: BulletVolley2D, _i: int) -> void:
		if hits.is_empty():
			hits.append(true)
			factory.reset_deferred())
	factory.spawn_volley(_hit_data_for_reset())
	for i in 90:
		await physics()
		if not hits.is_empty():
			break
	assert_eq(hits.size(), 1, "the hit handler ran")
	await idle(3)
	assert_eq(factory.debug_get_active_volleys_amount(), 0, "the deferred reset ran")
	assert_true(factory.get_is_debugger_enabled(), "the debugger switch survives the reset")
	var debuggers: Array = []
	for c in factory.get_children(true):
		if String(c.name).begins_with("BulletVolleysDebugger"):
			debuggers.append(c)
	assert_eq(debuggers.size(), 1, "exactly one debugger container")
	assert_eq((debuggers[0] as Node).get_child_count(true), 0, "the reset emptied it")
	factory.spawn_volley(_pooled_data(2))
	await physics(3)
	assert_eq(debuggers.size(), 1, "still one debugger container after the next spawn")
	assert_eq((debuggers[0] as Node).get_child_count(true), 1, "and exactly one debug mesh node for the new volley")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books ok")


## free_bullets_pool clears the bucket before deleting: the deletion runs
## tree_exiting handlers (user code) and a pool that still lists the dying
## bucket would hand the same instance out or erase the node under its loop.
func test_freeing_the_pool_clears_it_before_running_deletion_callbacks() -> void:
	var data := H.make_volley_data(1, 0.0, 30.0)
	var v: BulletVolley2D = factory.spawn_volley(data)
	var seen := []
	v.tree_exiting.connect(func() -> void:
		if seen.is_empty():
			seen.append(int(factory.debug_get_bullets_pool_amount())))
	v.disable_bullet(0)
	await idle(1)
	assert_true(v.is_pooled(), "pooled")
	assert_eq(int(factory.debug_get_bullets_pool_amount()), 1, "one pooled entry")
	factory.free_bullets_pool()
	assert_eq(seen, [0], "the bucket was cleared before the deletion callback ran")
	await idle(2)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books stay clean")
	expect_no_errors()
