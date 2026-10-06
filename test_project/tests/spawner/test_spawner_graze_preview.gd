extends BlastTest
## Graze ring preview (runtime path: graze_preview_during_runtime; the
## editor path runs the same refresh): every ring of every enabled zone is
## drawn around every resolved target (the runtime's own filter, the spawner
## excluded) in the zone's color with outer rings fainter; targets moving
## redraw the rings without a pattern rebuild and a still scene never
## redraws; freed targets drop out; zone edits redraw at once; the toggles
## hide the layer; the layer is top-level, internal, owner-less and tagged,
## so it is never saved, never a pattern marker, never a homing candidate;
## a stray layer is dropped at _ready unless the runtime preview is on; a
## reparented spawner keeps a working preview.

const LAYER := "~BlastBulletsGrazePreview"


func _preview_spawner(zones: Array) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.graze_zones = zones
	sp.graze_enabled = true
	sp.graze_preview_during_runtime = true
	return sp


func _circles(sp: BulletSpawner2D) -> Array:
	var out: Array = []
	for c in sp.debug_get_graze_preview_circles():
		out.append([c["center"], c["radius"], c["zone_index"], c["ring_index"]])
	return out


func test_every_ring_of_every_zone_around_every_target() -> void:
	make_graze_target(Vector2(100, 0))
	make_graze_target(Vector2(200, 0))
	make_graze_target(Vector2(0, 300), &"graze_b")
	var a := H.make_graze_zone([10.0, 30.0])
	a.preview_color = Color(1, 0, 0, 1)
	var b := H.make_graze_zone([5.0], &"graze_b")
	var off := H.make_graze_zone([50.0])
	off.enabled = false
	var sp := _preview_spawner([a, null, b, off])
	await idle(2)
	assert_eq(_circles(sp), [
		[Vector2(100, 0), 10.0, 0, 0], [Vector2(200, 0), 10.0, 0, 0],
		[Vector2(100, 0), 30.0, 0, 1], [Vector2(200, 0), 30.0, 0, 1],
		[Vector2(0, 300), 5.0, 2, 0],
	], "zone by zone, ring by ring, target by target; disabled zones and empty slots draw nothing")
	var colors: Array = []
	for c in sp.debug_get_graze_preview_circles():
		colors.append(c["color"])
	assert_true((colors[0] as Color).is_equal_approx(Color(1, 0, 0, 1)), "the innermost ring in the zone's color")
	assert_true((colors[2] as Color).is_equal_approx(Color(1, 0, 0, 0.7)), "each larger ring fainter")
	var stats: Dictionary = sp.debug_get_graze_preview_stats()
	assert_true(stats["active"] and stats["visible"], "active and visible")
	assert_eq(stats["circles"], 5, "five circles")


func test_preview_targets_are_the_runtime_targets() -> void:
	var a := make_graze_target(Vector2(100, 0))
	var b := make_graze_target(Vector2(200, 0))
	var dying := make_graze_target(Vector2(300, 0))
	dying.queue_free()
	var sp := _preview_spawner([H.make_graze_zone([10.0])])
	sp.add_to_group(&"graze_targets") # never its own target
	await idle(2)
	var ids: Array = []
	for c in sp.debug_get_graze_preview_circles():
		ids.append(c["target_id"])
	var runtime: Array = []
	for t in sp.resolve_graze_targets(0):
		runtime.append((t as Node).get_instance_id())
	assert_eq(ids, [a.get_instance_id(), b.get_instance_id()], "live targets only, the spawner excluded")
	assert_eq(ids, runtime, "exactly what the runtime tests")


func test_moving_targets_redraw_without_a_pattern_rebuild() -> void:
	var t := make_graze_target(Vector2(100, 0))
	var sp := _preview_spawner([H.make_graze_zone([10.0])])
	sp.show_pattern_preview = true
	sp.show_preview_during_runtime = true
	await idle(3)
	var rebuilds: int = sp.debug_get_preview_stats()["rebuilds"]
	var draws: int = sp.debug_get_graze_preview_stats()["draws"]
	await idle(5)
	assert_eq(sp.debug_get_graze_preview_stats()["draws"], draws, "a still scene never redraws the rings")
	for i in 5:
		t.position = Vector2(100 + 10 * (i + 1), 0)
		await idle(1)
	assert_eq(_circles(sp), [[Vector2(150, 0), 10.0, 0, 0]], "the rings follow the target")
	assert_eq(sp.debug_get_graze_preview_stats()["draws"], draws + 5, "one redraw per move")
	assert_eq(sp.debug_get_preview_stats()["rebuilds"], rebuilds, "the pattern preview never rebuilt")


func test_a_freed_target_drops_its_rings() -> void:
	var a := make_graze_target(Vector2(100, 0))
	make_graze_target(Vector2(200, 0))
	var sp := _preview_spawner([H.make_graze_zone([10.0])])
	await idle(2)
	assert_eq(_circles(sp).size(), 2, "two targets")
	a.free()
	await idle(2)
	assert_eq(_circles(sp), [[Vector2(200, 0), 10.0, 0, 0]], "only the live target keeps rings")


func test_zone_edits_redraw_at_once() -> void:
	make_graze_target(Vector2(100, 0))
	var z := H.make_graze_zone([10.0])
	var sp := _preview_spawner([z])
	await idle(2)
	z.ring_count = 2
	z.ring_2_radius = 33.0
	assert_eq(_circles(sp), [[Vector2(100, 0), 10.0, 0, 0], [Vector2(100, 0), 33.0, 0, 1]], "no frame needed: the zone's changed signal redraws")
	sp.graze_preview_line_width = 4.0
	var layer := sp.get_node(LAYER) as GrazePreviewLayer2D
	assert_not_null(layer, "the layer exists")


func test_toggles_hide_the_layer() -> void:
	make_graze_target(Vector2(100, 0))
	var sp := _preview_spawner([H.make_graze_zone([10.0])])
	await idle(2)
	assert_eq(_circles(sp).size(), 1, "shown")
	for toggle in ["graze_show_preview", "graze_preview_during_runtime", "graze_enabled"]:
		sp.set(toggle, false)
		assert_eq(_circles(sp), [], toggle + " off hides the rings")
		assert_false(sp.debug_get_graze_preview_stats()["visible"], toggle + " off hides the layer")
		sp.set(toggle, true)
		await idle(1)
		assert_eq(_circles(sp).size(), 1, toggle + " on shows them again")


func test_the_layer_is_top_level_internal_owner_less_and_tagged() -> void:
	make_graze_target(Vector2(100, 0))
	var sp := _preview_spawner([H.make_graze_zone([10.0])])
	sp.position = Vector2(500, 400)
	sp.rotation = 1.0
	await idle(2)
	var layer := sp.get_node(LAYER) as GrazePreviewLayer2D
	assert_not_null(layer, "found by name")
	assert_true(layer.is_set_as_top_level(), "top-level")
	assert_eq(layer.get_global_transform(), Transform2D(), "draws in canvas coordinates whatever the spawner's pose")
	assert_eq(_circles(sp)[0][0], Vector2(100, 0), "centered on the target's global position")
	assert_null(layer.owner, "owner-less: never saved")
	assert_true(layer.has_meta("blastbullets_pattern_preview"), "tagged like the pattern preview")
	assert_false(sp.get_children().has(layer), "internal: not listed by get_children()")
	var ps := PackedScene.new()
	assert_eq(ps.pack(sp), OK, "packs")
	var copy := ps.instantiate()
	assert_null(copy.get_node_or_null(LAYER), "a packed spawner carries no preview layer")
	copy.free()


func test_the_layer_is_never_a_pattern_marker_nor_a_homing_target() -> void:
	make_graze_target(Vector2(100, 0))
	var sp := _preview_spawner([H.make_graze_zone([10.0])])
	for p in [Vector2(10, 0), Vector2(20, 0)]:
		var m := Node2D.new()
		m.position = p
		sp.add_child(m)
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_CHILDREN
	await idle(2)
	assert_not_null(sp.get_node_or_null(LAYER), "the layer is there")
	assert_eq(sp.collect_spawn_transforms().size(), 2, "two markers, the layer is not one")
	sp.homing_target_source = BulletSpawner2D.HOMING_SOURCE_NODE_NAME
	sp.homing_node_name = "GrazePreview"
	assert_eq(sp.resolve_homing_targets(true), [], "a name scan never picks the layer")


func test_a_stray_layer_is_dropped_at_ready_unless_the_runtime_preview_is_on() -> void:
	for runtime_preview in [false, true]:
		var sp := BulletSpawner2D.new()
		sp.set_shooting_enabled(false)
		sp.set_homing_enabled(false)
		sp.graze_zones = [H.make_graze_zone([10.0])]
		sp.graze_enabled = true
		sp.graze_preview_during_runtime = runtime_preview
		var stray := GrazePreviewLayer2D.new()
		stray.name = LAYER
		sp.add_child(stray, false, Node.INTERNAL_MODE_BACK)
		add(sp)
		await idle(1)
		assert_eq(is_instance_valid(stray), runtime_preview, "runtime preview %s: the stray is %s" % [runtime_preview, "reused" if runtime_preview else "dropped"])


func test_a_reparented_spawner_keeps_a_working_preview() -> void:
	var t := make_graze_target(Vector2(100, 0))
	var sp := _preview_spawner([H.make_graze_zone([10.0])])
	await idle(2)
	var holder := Node2D.new()
	add(holder)
	sp.reparent(holder)
	t.position = Vector2(120, 0)
	await idle(2)
	assert_eq(_circles(sp), [[Vector2(120, 0), 10.0, 0, 0]], "still drawing, still following")
