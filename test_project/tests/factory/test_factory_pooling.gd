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
	d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW, 2), H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_CLEAR, 2)]
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
