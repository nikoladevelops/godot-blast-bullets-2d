extends BlastTest
## Engine interpolation opt-out: every CanvasItem the factory creates for
## bullets, effects, attachments and the debugger must not be engine
## interpolated (the plugin runs its own interpolation). With tree
## physics_interpolation on, Godot 4.6+ MultiMeshInstance2D inherits the mode
## and every instance write would lerp over a tick.


func before_each() -> void:
	await super()
	get_tree().physics_interpolation = true


func after_each() -> void:
	get_tree().physics_interpolation = false
	await super()


func _interpolated_nodes(root: Node) -> Array:
	var out: Array = []
	_collect_interpolated(root, out)
	return out


func _collect_interpolated(n: Node, out: Array) -> void:
	for c in n.get_children(true):
		if c is CanvasItem and (c as CanvasItem).is_physics_interpolated_and_enabled():
			out.append(str(n.get_path_to(c)))
		_collect_interpolated(c, out)


func test_one_shot_effect_shards_are_not_engine_interpolated() -> void:
	var data := H.make_volley_data(1, 0.0, 30.0)
	data.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_SPAWN, 4)]
	var v: BulletVolley2D = factory.spawn_volley(data)
	assert_not_null(v, "volley spawned")
	await physics(2)
	assert_eq(_interpolated_nodes(factory), [], "no interpolated canvas items under factory")


func test_trail_and_manual_hatch_shards_are_not_engine_interpolated() -> void:
	var data := H.make_volley_data(1, 0.0, 30.0)
	data.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW, 4)]
	var v: BulletVolley2D = factory.spawn_volley(data)
	assert_not_null(v, "volley spawned")
	var manual := H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_SPAWN, 2)
	assert_gt(factory.spawn_layer_effect(manual, Transform2D()), -1, "manual hatch fired")
	await physics(2)
	assert_eq(_interpolated_nodes(factory), [], "no interpolated canvas items under factory")


func test_every_factory_canvas_item_opts_out() -> void:
	var data := H.make_volley_data(1, 0.0, 30.0)
	data.effect_layers = [
		H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW, 4),
		H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_SPAWN, 4),
	]
	data.shared_bullet_attachment = make_probe_scene()
	var v: BulletVolley2D = factory.spawn_volley(data)
	assert_not_null(v, "volley spawned")
	factory.set_is_debugger_enabled(true)
	await physics(2)
	assert_eq(_interpolated_nodes(factory), [], "no interpolated canvas items under factory")
