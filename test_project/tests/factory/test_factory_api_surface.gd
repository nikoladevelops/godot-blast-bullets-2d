extends BlastTest
## Locks the public surface after the volley refactor: one bullet class
## (BulletVolley2D) with one spawn-data resource (BulletVolleyData2D), one
## factory spawn entry (spawn_volley), factory signals with the same names
## and payloads as the spawner's, no bullet-type parameters, exact stats keys
## and one volley container + one collision-shape debugger under the factory.
## Every removed name is asserted absent so it cannot quietly come back.

const VOLLEY_SIGNALS := ["area_entered", "body_entered", "life_time_over", "bounce_area_entered", "bounce_body_entered"]


func _signal(obj: Object, name: String) -> Dictionary:
	for s in obj.get_signal_list():
		if str(s["name"]) == name:
			return s
	return {}


func _method(cls: String, name: String) -> Dictionary:
	for m in ClassDB.class_get_method_list(cls, true):
		if str(m["name"]) == name:
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
	for counter in ["debug_get_total_bullets_amount", "debug_get_active_bullets_amount", "debug_get_bullets_pool_amount", "debug_get_bullets_pool_info"]:
		assert_eq(_arg_names(_method("BulletFactory2D", counter)), [], counter + " takes no bullet type")


func test_spawn_volley_returns_the_volley_or_null() -> void:
	var v: Variant = factory.spawn_volley(H.make_volley_data(3))
	assert_true(v is BulletVolley2D, "spawn_volley returns the live BulletVolley2D")
	assert_eq((v as BulletVolley2D).get_amount_bullets(), 3, "one bullet per transform")
	var refused: Variant = factory.spawn_volley(null)
	expect_error_sequence(["Error when trying to spawn bullets in spawn_volley. No spawn_data or no transforms were provided. Ignoring the request"])
	assert_null(refused, "a refused request returns null")


func test_factory_and_spawner_declare_the_same_volley_signals() -> void:
	var spawner := make_spawner()
	for owner in [factory, spawner]:
		for name in VOLLEY_SIGNALS:
			var s := _signal(owner, name)
			assert_false(s.is_empty(), "%s declares %s" % [owner.get_class(), name])
			var args: Array = s.get("args", [])
			var names: Array = []
			for a in args:
				names.append(str(a["name"]))
			var volley_arg: Dictionary = args[1] if name != "life_time_over" else args[0]
			assert_eq(str(volley_arg["name"]), "volley", "%s.%s names its volley argument 'volley'" % [owner.get_class(), name])
			assert_eq(str(volley_arg["class_name"]), "BulletVolley2D", "%s.%s types its volley argument" % [owner.get_class(), name])
			if name == "life_time_over":
				assert_eq(names, ["volley", "bullet_indexes"], name + " payload")
			else:
				assert_eq(names.slice(1), ["volley", "bullet_index"], name + " payload")
	for gone in ["directional_area_entered", "directional_body_entered", "directional_life_time_over", "directional_bounce_area_entered", "directional_bounce_body_entered", "block_area_entered", "block_body_entered", "block_life_time_over"]:
		assert_false(factory.has_signal(gone), gone + " is gone")


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
