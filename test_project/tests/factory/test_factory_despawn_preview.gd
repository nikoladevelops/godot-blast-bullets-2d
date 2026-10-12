extends BlastTest
## The despawn bounds preview: a translucent rectangle drawn by the factory
## while the bounds are enabled (always in the editor, at runtime only with
## despawn_bounds_preview_during_runtime). It is an internal, owner-less,
## tagged child, redraws only when its snapshot changes, and never saved.

const LAYER_NAME := "~BlastBulletsDespawnPreview"


func _info() -> Dictionary:
	return factory.debug_get_despawn_bounds_preview()


func test_defaults() -> void:
	assert_eq(factory.despawn_bounds_color, Color(1, 0, 0, 0.25), "translucent red by default")
	assert_true(factory.despawn_bounds_preview, "preview on by default (it still needs the bounds enabled)")
	assert_false(factory.despawn_bounds_preview_during_runtime, "hidden at runtime by default")
	assert_false(bool(_info()["visible"]), "nothing is drawn while the bounds are off")


func test_runtime_visibility_truth_table() -> void:
	# [enabled, preview, during_runtime] -> visible
	var cases := [
		[false, true, true, false],
		[true, false, true, false],
		[true, true, false, false],
		[true, true, true, true],
	]
	for c in cases:
		factory.despawn_bounds_enabled = c[0]
		factory.despawn_bounds_preview = c[1]
		factory.despawn_bounds_preview_during_runtime = c[2]
		assert_eq(bool(_info()["visible"]), c[3], "enabled=%s preview=%s runtime=%s" % [c[0], c[1], c[2]])


func test_layer_shows_the_rect_and_color() -> void:
	factory.despawn_bounds_rect = Rect2(10, 20, 300, 400)
	factory.despawn_bounds_color = Color(0, 1, 0, 0.5)
	factory.despawn_bounds_preview_during_runtime = true
	factory.despawn_bounds_enabled = true
	await idle(2)
	var info := _info()
	assert_true(bool(info["visible"]), "visible")
	assert_eq(info["rect"], Rect2(10, 20, 300, 400), "the layer draws the factory rect")
	assert_eq(info["color"], Color(0, 1, 0, 0.5), "and its color")
	assert_gt(int(info["draws"]), 0, "it was drawn")


func test_layer_follows_edits_and_hides_again() -> void:
	factory.despawn_bounds_preview_during_runtime = true
	factory.despawn_bounds_enabled = true
	factory.despawn_bounds_rect = Rect2(0, 0, 100, 100)
	assert_eq(_info()["rect"], Rect2(0, 0, 100, 100), "initial rect")
	factory.despawn_bounds_rect = Rect2(5, 5, 50, 50)
	assert_eq(_info()["rect"], Rect2(5, 5, 50, 50), "a rect edit reaches the layer at once")
	factory.despawn_bounds_rect = Rect2(50, 50, -50, -50) # negative size is normalized
	assert_eq(_info()["rect"], Rect2(0, 0, 50, 50), "normalized rect")
	factory.despawn_bounds_enabled = false
	assert_false(bool(_info()["visible"]), "disabling hides it")
	factory.despawn_bounds_enabled = true
	assert_true(bool(_info()["visible"]), "enabling shows it again")


func test_redraws_only_when_the_snapshot_changes() -> void:
	factory.despawn_bounds_preview_during_runtime = true
	factory.despawn_bounds_enabled = true
	await idle(3)
	var draws := int(_info()["draws"])
	await idle(10)
	assert_eq(int(_info()["draws"]), draws, "an idle layer never redraws")
	factory.despawn_bounds_rect = Rect2(1, 2, 300, 200)
	await idle(2)
	assert_eq(int(_info()["draws"]), draws + 1, "one redraw for one change")
	factory.despawn_bounds_rect = Rect2(1, 2, 300, 200)
	await idle(2)
	assert_eq(int(_info()["draws"]), draws + 1, "setting the same rect again does not redraw")


func test_layer_is_internal_ownerless_and_tagged() -> void:
	factory.despawn_bounds_preview_during_runtime = true
	factory.despawn_bounds_enabled = true
	var layer: Node = factory.get_node_or_null(LAYER_NAME)
	assert_not_null(layer, "the layer exists once visible")
	assert_false(layer in factory.get_children(), "internal: not in get_children()")
	assert_true(layer in factory.get_children(true), "internal children list it")
	assert_null(layer.owner, "owner-less: never saved with the scene")
	assert_true(layer.has_meta("blastbullets_pattern_preview"), "tagged so no scan or target filter takes it")
	assert_eq(factory.get_children(true).filter(func(n): return n.name == LAYER_NAME).size(), 1, "exactly one layer")


func test_color_setter_rejects_non_finite_and_keeps_the_old_value() -> void:
	factory.despawn_bounds_color = Color(NAN, 0, 0, 1)
	expect_error_sequence(["BulletFactory2D: despawn_bounds_color must be finite, keeping the old value."])
	factory.despawn_bounds_color = Color(0, 0, 0, INF)
	expect_error_sequence(["BulletFactory2D: despawn_bounds_color must be finite, keeping the old value."])
	assert_eq(factory.despawn_bounds_color, Color(1, 0, 0, 0.25), "old color kept")
	factory.despawn_bounds_color = Color(0.2, 0.4, 0.6, 0.8)
	assert_eq(factory.despawn_bounds_color, Color(0.2, 0.4, 0.6, 0.8), "valid color accepted")


func test_tree_reentry_keeps_one_visible_layer() -> void:
	factory.despawn_bounds_preview_during_runtime = true
	factory.despawn_bounds_enabled = true
	var parent := factory.get_parent()
	parent.remove_child(factory)
	parent.add_child(factory)
	assert_true(bool(_info()["visible"]), "visible again after re-entry")
	assert_eq(factory.get_children(true).filter(func(n): return n.name == LAYER_NAME).size(), 1, "no duplicate layer")


func test_preview_does_not_change_despawn_behavior() -> void:
	factory.despawn_bounds_rect = Rect2(0, 0, 100, 100)
	factory.despawn_bounds_enabled = true
	factory.despawn_bounds_preview = false
	var data := H.make_volley_data(1, 0.0, 30.0)
	data.transforms = [Transform2D(0.0, Vector2(500, 50))]
	var v: BulletVolley2D = factory.spawn_volley(data)
	v.bullet_set_velocity(0, Vector2(100, 0))
	await idle(1)
	factory.debug_advance_time(0.1)
	assert_eq(factory.get_active_bullet_count(), 0, "bounds cull whether or not the preview is shown")
