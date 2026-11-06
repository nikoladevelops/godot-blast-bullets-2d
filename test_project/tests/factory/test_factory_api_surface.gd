extends BlastTest
## Locks the public surface after the volley refactor: one bullet class
## (BulletVolley2D) with one spawn-data resource (BulletVolleyData2D), one
## factory spawn entry (spawn_volley), factory signals with the same names
## and payloads as the spawner's, no bullet-type parameters, exact stats keys
## and one volley container + one collision-shape debugger under the factory.
## Every removed name is asserted absent so it cannot quietly come back.

const VOLLEY_SIGNALS := ["area_entered", "body_entered", "life_time_over", "bounce_area_entered", "bounce_body_entered"]


func _signal(obj: Object, signal_name: String) -> Dictionary:
	for s in obj.get_signal_list():
		if str(s["name"]) == signal_name:
			return s
	return {}


func _method(cls: String, method_name: String) -> Dictionary:
	for m in ClassDB.class_get_method_list(cls, true):
		if str(m["name"]) == method_name:
			return m
	return {}


func _arg_names(m: Dictionary) -> Array:
	var out: Array = []
	for a in m.get("args", []):
		out.append(str(a["name"]))
	return out


func test_volley_classes_exist_and_old_ones_are_gone() -> void:
	for cls in ["BulletVolley2D", "BulletVolleyData2D", "VolleyPoolKey2D", "BulletVolleyDebugger2D"]:
		assert_true(ClassDB.class_exists(cls), cls + " is registered")
	for cls in ["DirectionalBullets2D", "DirectionalBulletsData2D", "MultiMeshBullets2D", "MultiMeshBulletsData2D", "BlockBullets2D", "BlockBulletsData2D", "MultiMeshPoolKey2D", "MultiMeshBulletsDebugger2D"]:
		assert_false(ClassDB.class_exists(cls), cls + " is gone")
	assert_eq(str(ClassDB.get_parent_class("BulletVolley2D")), "MultiMeshInstance2D", "a volley is one MultiMeshInstance2D")
	assert_eq(str(ClassDB.get_parent_class("BulletVolleyData2D")), "Resource", "spawn data is a plain Resource")


func test_factory_has_one_spawn_entry_and_no_bullet_type() -> void:
	var spawn := _method("BulletFactory2D", "spawn_volley")
	assert_eq(_arg_names(spawn), ["spawn_data", "inherited_velocity_offset", "spawner_id"], "spawn_volley signature")
	for gone in ["spawn_directional_bullets", "spawn_controllable_directional_bullets", "spawn_block_bullets", "reactivate_multimesh_instance", "get_directional_bullets_debugger_color", "get_block_bullets_debugger_color"]:
		assert_false(ClassDB.class_has_method("BulletFactory2D", gone, true), gone + " is gone")
	assert_false(ClassDB.class_has_enum("BulletFactory2D", "BulletType", true), "no BulletType enum")
	assert_false(ClassDB.class_has_integer_constant("BulletFactory2D", "DIRECTIONAL_BULLETS"), "no DIRECTIONAL_BULLETS")
	assert_false(ClassDB.class_has_integer_constant("BulletFactory2D", "BLOCK_BULLETS"), "no BLOCK_BULLETS")
	assert_eq(_arg_names(_method("BulletFactory2D", "free_bullets_pool")), ["key"], "free_bullets_pool takes only a key")
	assert_eq(_arg_names(_method("BulletFactory2D", "free_bullets_pool_deferred")), ["key"], "deferred twin takes only a key")
	assert_eq(_arg_names(_method("BulletFactory2D", "populate_bullets_pool")), ["key", "spawn_data", "instance_count"], "populate_bullets_pool signature")
	for counter in ["debug_get_total_bullets_amount", "debug_get_active_volleys_amount", "debug_get_bullets_pool_amount", "debug_get_bullets_pool_info"]:
		assert_eq(_arg_names(_method("BulletFactory2D", counter)), [], counter + " takes no bullet type")


func test_spawn_volley_returns_the_volley_or_null() -> void:
	var v: Variant = factory.spawn_volley(H.make_volley_data(3))
	assert_true(v is BulletVolley2D, "spawn_volley returns the live BulletVolley2D")
	assert_eq((v as BulletVolley2D).get_amount_bullets(), 3, "one bullet per transform")
	var refused: Variant = factory.spawn_volley(null)
	expect_error_sequence(["Error when trying to spawn bullets in spawn_volley. No spawn_data or no transforms were provided. Ignoring the request"])
	assert_null(refused, "a refused request returns null")


## Runtime state readouts a game may probe: a fresh factory is idle and alive,
## uses its World2D's physics space, and the space setter rejects an invalid
## RID with one error while keeping the old space.
func test_factory_runtime_state_readouts() -> void:
	assert_false(factory.get_is_factory_busy(), "idle")
	assert_false(factory.get_is_tearing_down(), "alive")
	var space: RID = factory.get_physics_space()
	assert_true(space.is_valid(), "a valid space")
	assert_true(space == factory.get_world_2d().get_space(), "the world's space")
	factory.set_physics_space(RID())
	expect_error_sequence(["set_physics_space: the provided RID is invalid. Set a valid physics space before spawning (it applies to subsequently spawned bullets; already spawned areas stay in their space)."])
	assert_true(factory.get_physics_space() == space, "the old space is kept")
	factory.set_physics_space(space)
	assert_true(factory.get_physics_space() == space, "a valid space is accepted")


func test_factory_and_spawner_declare_the_same_volley_signals() -> void:
	var spawner := make_spawner()
	for owner_node in [factory, spawner]:
		for signal_name in VOLLEY_SIGNALS:
			var s := _signal(owner_node, signal_name)
			assert_false(s.is_empty(), "%s declares %s" % [owner_node.get_class(), signal_name])
			var args: Array = s.get("args", [])
			var names: Array = []
			for a in args:
				names.append(str(a["name"]))
			var volley_arg: Dictionary = args[1] if signal_name != "life_time_over" else args[0]
			assert_eq(str(volley_arg["name"]), "volley", "%s.%s names its volley argument 'volley'" % [owner_node.get_class(), signal_name])
			assert_eq(str(volley_arg["class_name"]), "BulletVolley2D", "%s.%s types its volley argument" % [owner_node.get_class(), signal_name])
			if signal_name == "life_time_over":
				assert_eq(names, ["volley", "bullet_indexes"], signal_name + " payload")
			else:
				assert_eq(names.slice(1), ["volley", "bullet_index"], signal_name + " payload")
	for gone in ["directional_area_entered", "directional_body_entered", "directional_life_time_over", "directional_bounce_area_entered", "directional_bounce_body_entered", "block_area_entered", "block_body_entered", "block_life_time_over"]:
		assert_false(factory.has_signal(gone), gone + " is gone")


func test_factory_and_spawner_declare_the_same_graze_signals() -> void:
	var spawner := make_spawner()
	var expected := {
		"bullet_grazed": [["target", "Node2D"], ["volley", "BulletVolley2D"], ["bullet_index", ""], ["zone", "GrazeZone2D"], ["ring_index", ""]],
		"bullet_graze_exited": [["target", "Node2D"], ["volley", "BulletVolley2D"], ["bullet_index", ""], ["zone", "GrazeZone2D"], ["deepest_ring_index", ""]],
		"bullet_split": [["position", ""], ["cause", ""], ["children", "BulletVolley2D"], ["depth", ""]],
	}
	for owner_node in [factory, spawner]:
		for signal_name in expected:
			var s := _signal(owner_node, signal_name)
			assert_false(s.is_empty(), "%s declares %s" % [owner_node.get_class(), signal_name])
			var got: Array = []
			for a in s.get("args", []):
				got.append([str(a["name"]), str(a["class_name"])])
			assert_eq(got, expected[signal_name], "%s.%s payload" % [owner_node.get_class(), signal_name])


func test_stats_dictionaries_have_exact_keys() -> void:
	var hit_keys: Array = factory.debug_get_pool_hit_stats().keys()
	hit_keys.sort()
	assert_eq(hit_keys, ["hits", "misses"], "pool hit stats keys")
	var dangling_keys: Array = factory.debug_assert_no_dangling().keys()
	dangling_keys.sort()
	assert_eq(dangling_keys, ["error", "ok", "volleys_total"], "dangling check keys")
	var state_keys: Array = factory.debug_get_factory_state().keys()
	state_keys.sort()
	assert_eq(state_keys, ["attachments_pooled", "is_busy", "is_iterating", "is_ready", "is_tearing_down", "processing", "volleys_pooled", "volleys_total"], "factory state keys")
	var split_keys: Array = factory.debug_get_split_stats().keys()
	split_keys.sort()
	assert_eq(split_keys, ["carried_over_last", "children_last", "children_total", "dropped_total", "flushed_last", "flushed_total", "pending", "templates"], "split stats keys")


func test_factory_owns_one_volley_container_and_one_debugger() -> void:
	assert_not_null(factory.get_node_or_null("BulletVolleysContainer"), "volley container node")
	assert_null(factory.get_node_or_null("DirectionalBulletsContainer"), "old container name is gone")
	var debuggers := 0
	for child in factory.get_children():
		if child is BulletVolleyDebugger2D:
			debuggers += 1
			assert_eq(str(child.name), "BulletVolleysDebugger", "debugger node name")
	assert_eq(debuggers, 1, "exactly one collision-shape debugger")
	var v: BulletVolley2D = quick_volley(2)
	assert_eq(v.get_parent(), factory.get_node("BulletVolleysContainer"), "volleys live under the container")


func test_debugger_color_round_trips_before_and_after_ready() -> void:
	var early := BulletFactory2D.new()
	early.debugger_color = Color(0.1, 0.9, 0.3, 0.5)
	assert_eq(early.debugger_color, Color(0.1, 0.9, 0.3, 0.5), "cached before the factory is ready")
	add(early)
	await idle(1)
	assert_eq(early.debugger_color, Color(0.1, 0.9, 0.3, 0.5), "applied to the debugger on ready")
	early.debugger_color = Color(1, 0, 0, 1)
	assert_eq(early.debugger_color, Color(1, 0, 0, 1), "runtime writes reach the debugger")
	early.queue_free()
	await idle(1)


## Spawn data with no art at all warns once, in plain text (no garbled
## characters).
func test_invisible_bullets_warn_once_in_plain_text() -> void:
	var d := H.make_volley_data(2, 0.0, 5.0)
	d.sprite_frames = null
	d.texture_size = Vector2.ZERO
	assert_not_null(factory.spawn_volley(d), "spawns")
	expect_warning_sequence(["Warning in spawn_volley: no sprite_frames/mesh/texture_size, bullets will be invisible. Assign SpriteFrames with art facing Vector2.RIGHT, or set texture_size/mesh."])
	factory.spawn_volley(d)
	expect_no_errors()


## spawn_volley / enable_volley take an owner id from scripts: it reroutes
## every signal and applies the owner's time scale, so it must be 0 or a live
## BulletSpawner2D (anything else used to be stamped silently).
func test_a_script_owner_id_must_name_a_live_bullet_spawner() -> void:
	var data := H.make_volley_data(2)
	var laser := LaserSpawner2D.new()
	add(laser)
	var node := Node2D.new()
	add(node)
	var gone := Node2D.new()
	var stale_id := gone.get_instance_id()
	gone.free()
	for bad in [stale_id, node.get_instance_id(), laser.get_instance_id()]:
		assert_null(factory.spawn_volley(data, Vector2.ZERO, bad), "refused: %d" % bad)
		expect_error_sequence(["spawn_volley: spawner_id must be 0 (factory-owned) or the instance id of a live BulletSpawner2D, nothing spawned."])
	assert_eq(factory.get_active_bullet_count(), 0, "nothing spawned")
	var sp := make_spawner(data)
	var owned: BulletVolley2D = factory.spawn_volley(data, Vector2.ZERO, sp.get_instance_id())
	assert_not_null(owned, "a live spawner id is accepted")
	assert_eq(owned.get_owner_spawner(), sp, "and owns the volley")
	owned.clear_all_bullets()
	assert_true(owned.is_pooled(), "pooled")
	assert_false(owned.enable_volley(data, Vector2.ZERO, node.get_instance_id()), "enable_volley refuses too")
	expect_error_sequence(["enable_volley: spawner_id must be 0 (factory-owned) or the instance id of a live BulletSpawner2D, nothing spawned."])
	assert_true(owned.is_pooled(), "the refused enable changed nothing")


func test_the_active_counters_count_what_they_say() -> void:
	var a: BulletVolley2D = factory.spawn_volley(H.make_volley_data(3))
	factory.spawn_volley(H.make_volley_data(5))
	assert_eq(factory.debug_get_active_volleys_amount(), 2, "two volleys")
	assert_eq(factory.get_active_bullet_count(), 8, "eight bullets")
	a.clear_all_bullets()
	assert_eq([factory.debug_get_active_volleys_amount(), factory.get_active_bullet_count()], [1, 5], "the pooled volley counts in neither")
