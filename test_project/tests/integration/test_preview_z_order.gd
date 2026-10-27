extends BlastTest
## Every preview the plugin draws takes its z order from a property, never a
## hidden constant: a spawner's dots, arrows and runtime graze rings
## (preview_z_index, preview_z_as_relative), the factory's despawn-bounds
## rectangle (despawn_bounds_preview_z_index) and its collision debugger
## (debugger_z_index). Defaults keep the old 4000 / 999; edits reach live
## layers at once and layers created later; out-of-range values are refused.

const HOLDER := "~BlastBulletsPatternPreview"
const GRAZE_LAYER := "~BlastBulletsGrazePreview"
const DESPAWN_LAYER := "~BlastBulletsDespawnPreview"


func _preview_spawner() -> BulletSpawner2D:
	make_graze_target(Vector2(100, 0))
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.graze_zones = [H.make_graze_zone([10.0])]
	sp.graze_enabled = true
	sp.graze_node_group = &"graze_targets"
	sp.graze_preview_during_runtime = true
	return sp


func _spawner_layers(sp: BulletSpawner2D) -> Array:
	var holder: Node = sp.get_node_or_null(HOLDER)
	var out: Array = []
	if holder != null:
		out.append(holder.get_node_or_null("Dots"))
		out.append(holder.get_node_or_null("Arrows"))
	out.append(sp.get_node_or_null(GRAZE_LAYER))
	return out


func _orders(layers: Array) -> Array:
	var out: Array = []
	for l in layers:
		out.append([(l as CanvasItem).z_index, (l as CanvasItem).z_as_relative] if l != null else null)
	return out


func test_spawner_preview_layers_follow_the_z_settings() -> void:
	var sp := _preview_spawner()
	await idle(6)
	assert_eq(sp.preview_z_index, 4000, "default z")
	assert_true(sp.preview_z_as_relative, "relative by default")
	assert_eq(_orders(_spawner_layers(sp)), [[4000, true], [4000, true], [4000, true]], "dots, arrows and graze rings at the default")
	sp.preview_z_index = -20
	sp.preview_z_as_relative = false
	assert_eq(_orders(_spawner_layers(sp)), [[-20, false], [-20, false], [-20, false]], "an edit reaches the live layers at once")
	sp.helper_bullets_amount = 6 # a rebuild keeps them
	await idle(6)
	assert_eq(_orders(_spawner_layers(sp)), [[-20, false], [-20, false], [-20, false]], "a rebuild keeps the order")
	sp.preview_z_index = 5000
	expect_error_sequence(["BulletSpawner2D: preview_z_index must be between -4096 and 4096, keeping the old value."])
	assert_eq(sp.preview_z_index, -20, "kept")


func test_a_laser_spawner_preview_takes_its_z_too() -> void:
	var sp := LaserSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.laser_data = H.make_laser()
	sp.preview_z_index = 7
	sp.show_pattern_preview = true
	sp.show_preview_during_runtime = true
	add(sp)
	await idle(6)
	var holder: Node = sp.get_node(HOLDER)
	assert_eq([(holder.get_node("Dots") as CanvasItem).z_index, (holder.get_node("Arrows") as CanvasItem).z_index], [7, 7], "set before the preview existed")


func test_factory_despawn_preview_and_debugger_follow_their_z() -> void:
	assert_eq([factory.despawn_bounds_preview_z_index, factory.debugger_z_index], [4000, 999], "defaults")
	factory.despawn_bounds_rect = Rect2(0, 0, 300, 300)
	factory.despawn_bounds_preview_during_runtime = true
	factory.despawn_bounds_enabled = true
	await idle(2)
	var layer: CanvasItem = null
	for c in factory.get_children(true):
		if c.name == DESPAWN_LAYER:
			layer = c
	assert_not_null(layer, "the despawn preview exists")
	assert_eq(layer.z_index, 4000, "at the default")
	factory.despawn_bounds_preview_z_index = -3
	assert_eq(layer.z_index, -3, "an edit reaches it at once")
	factory.despawn_bounds_preview_z_index = -9999
	expect_error_sequence(["BulletFactory2D: despawn_bounds_preview_z_index must be between -4096 and 4096, keeping the old value."])
	factory.debugger_z_index = 12
	factory.is_debugger_enabled = true
	quick_volley(3)
	await physics(2)
	var debugger: Node = factory.get_node("BulletVolleysDebugger")
	var zs: Array = []
	for c in debugger.get_children(true):
		if c is MultiMeshInstance2D:
			zs.append((c as CanvasItem).z_index)
	assert_false(zs.is_empty(), "the debugger draws the volley")
	assert_eq(zs, zs.map(func(_z): return 12), "set before the debugger drew anything")
	factory.debugger_z_index = 40
	zs.clear()
	for c in debugger.get_children(true):
		if c is MultiMeshInstance2D:
			zs.append((c as CanvasItem).z_index)
	assert_eq(zs, zs.map(func(_z): return 40), "an edit reaches the drawn shapes at once")
	factory.debugger_z_index = 4097
	expect_error_sequence(["BulletFactory2D: debugger_z_index must be between -4096 and 4096, keeping the old value."])
	factory.is_debugger_enabled = false
