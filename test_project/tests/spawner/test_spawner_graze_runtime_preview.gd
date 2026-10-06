extends BlastTest
## BulletGrazeZone2D.preview_during_runtime: off by default; when on, the
## FACTORY draws that zone's rings around its targets while the game runs,
## once however many spawners share it (a spawner's own runtime preview
## never doubles it), from spawners holding it (steady between shots) and
## from volleys armed with it (bullets of a freed spawner, factory-only
## volleys). Live zone edits and moving targets show on the next frame, the
## view keeps following while the factory is paused, and it hides and puts
## the factory's _process back to sleep once nothing flagged is left. The
## layer is internal, top-level and owner-less.

const LAYER := "~BlastBulletsGrazeRuntimePreview"


func _graze_spawner(zones: Array, at: Vector2 = Vector2.ZERO) -> BulletSpawner2D:
	var d := H.make_volley_data(1, 0.0, 60.0)
	d.collision_shape = H.make_circle_shape(4.0)
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.position = at
	sp.graze_zones = zones
	sp.graze_enabled = true
	sp.graze_node_group = &"graze_targets"
	return sp


func _flagged(radii: Array = [20.0, 40.0]) -> BulletGrazeZone2D:
	var z := H.make_graze_zone(radii)
	z.preview_during_runtime = true
	return z


func _drawn() -> Array:
	var out: Array = []
	for c in factory.debug_get_graze_runtime_preview()["circles"]:
		out.append([c["center"], c["radius"], c["ring_index"]])
	return out


func test_off_by_default_nothing_is_drawn() -> void:
	make_graze_target(Vector2(100, 0))
	assert_false(BulletGrazeZone2D.new().preview_during_runtime, "off by default")
	_graze_spawner([H.make_graze_zone([20.0])])
	await idle(3)
	var info: Dictionary = factory.debug_get_graze_runtime_preview()
	assert_false(info["awake"], "nothing flagged: the factory preview sleeps")
	assert_false(info["visible"], "nothing drawn")
	assert_eq(info["circles"], [], "no circles")
	assert_false(factory.is_processing(), "no interpolation, no preview: _process idles")


func test_a_flagged_zone_is_drawn_by_the_factory() -> void:
	make_graze_target(Vector2(100, 0))
	make_graze_target(Vector2(200, 50))
	var z := _flagged()
	z.preview_color = Color(0, 1, 0, 1)
	_graze_spawner([z])
	await idle(2)
	assert_eq(_drawn(), [
		[Vector2(100, 0), 20.0, 0], [Vector2(200, 50), 20.0, 0],
		[Vector2(100, 0), 40.0, 1], [Vector2(200, 50), 40.0, 1],
	], "every ring around every target")
	var circles: Array = factory.debug_get_graze_runtime_preview()["circles"]
	assert_true((circles[0]["color"] as Color).is_equal_approx(Color(0, 1, 0, 1)), "the zone's color")
	assert_true((circles[2]["color"] as Color).is_equal_approx(Color(0, 1, 0, 0.7)), "larger rings fainter")
	var info: Dictionary = factory.debug_get_graze_runtime_preview()
	assert_true(info["awake"] and info["visible"], "awake and visible")
	assert_eq(info["zones"], 1, "one zone")
	assert_true(factory.is_processing(), "the preview keeps _process running")


func test_a_zone_shared_by_many_spawners_draws_once() -> void:
	make_graze_target(Vector2(100, 0))
	var z := _flagged([30.0])
	for i in 3:
		_graze_spawner([z], Vector2(0, 50 * i))
	await idle(2)
	assert_eq(_drawn(), [[Vector2(100, 0), 30.0, 0]], "one circle, not three stacked copies")


func test_the_spawner_runtime_preview_never_doubles_a_flagged_zone() -> void:
	make_graze_target(Vector2(100, 0))
	var flagged := _flagged([30.0])
	var plain := H.make_graze_zone([50.0])
	var sp := _graze_spawner([flagged, plain])
	sp.graze_preview_during_runtime = true
	await idle(2)
	var own: Array = []
	for c in sp.debug_get_graze_preview_circles():
		own.append([c["radius"], c["zone_index"]])
	assert_eq(own, [[50.0, 1]], "the spawner draws only the zone the factory does not")
	assert_eq(_drawn(), [[Vector2(100, 0), 30.0, 0]], "the factory draws the flagged one")


func test_live_edits_and_moving_targets_show_on_the_next_frame() -> void:
	var t := make_graze_target(Vector2(100, 0))
	var z := _flagged([30.0])
	_graze_spawner([z])
	await idle(2)
	z.ring_1_radius = 45.0
	t.position = Vector2(150, 20)
	await idle(1)
	assert_eq(_drawn(), [[Vector2(150, 20), 45.0, 0]], "new size, new place, one frame later")
	z.ring_count = 2
	z.ring_2_radius = 10.0
	await idle(1)
	assert_eq(_drawn(), [[Vector2(150, 20), 45.0, 0], [Vector2(150, 20), 10.0, 1]], "a ring added at runtime shows")


func test_turning_the_flag_on_and_off_at_runtime() -> void:
	make_graze_target(Vector2(100, 0))
	var z := H.make_graze_zone([30.0])
	_graze_spawner([z])
	await idle(2)
	assert_eq(_drawn(), [], "off")
	z.preview_during_runtime = true
	await idle(2)
	assert_eq(_drawn().size(), 1, "ticking the flag at runtime shows it")
	z.preview_during_runtime = false
	await idle(2)
	var info: Dictionary = factory.debug_get_graze_runtime_preview()
	assert_eq(_drawn(), [], "unticking hides it")
	assert_false(info["awake"], "and the factory preview sleeps")
	assert_false(factory.is_processing(), "_process idles again")


func test_disabled_zones_and_graze_off_draw_nothing() -> void:
	make_graze_target(Vector2(100, 0))
	var z := _flagged([30.0])
	var sp := _graze_spawner([z])
	await idle(2)
	assert_eq(_drawn().size(), 1, "drawn")
	z.enabled = false
	await idle(2)
	assert_eq(_drawn(), [], "a disabled zone tests nothing, so it draws nothing")
	z.enabled = true
	sp.graze_enabled = false
	await idle(2)
	assert_eq(_drawn(), [], "a spawner with graze off does not hold the zone")
	assert_false(sp.is_in_group("_blast_bullets_graze_spawners"), "it left the internal group")
	sp.graze_enabled = true
	sp.graze_node_group = &"graze_targets"
	await idle(2)
	assert_eq(_drawn().size(), 1, "graze back on: drawn again")


func test_volleys_keep_a_zone_drawn_without_any_spawner() -> void:
	make_graze_target(Vector2(100, 0))
	var z := _flagged([30.0])
	var sp := _graze_spawner([z])
	sp.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING
	sp.shoot_once()
	await idle(2)
	sp.free()
	await idle(2)
	assert_eq(_drawn().size(), 1, "the orphaned volley still carries the zone: still drawn")
	factory.clear_active_bullets()
	await idle(3)
	assert_eq(_drawn(), [], "no bullet and no spawner holds it: hidden")
	assert_false(factory.debug_get_graze_runtime_preview()["awake"], "asleep")
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	v.graze_set_zones([z], &"graze_targets")
	await idle(3)
	assert_eq(_drawn().size(), 1, "a factory-only volley armed with it draws it too")


func test_keeps_following_while_the_factory_is_paused() -> void:
	var t := make_graze_target(Vector2(100, 0))
	_graze_spawner([_flagged([30.0])])
	await idle(2)
	factory.is_factory_processing_bullets = false
	t.position = Vector2(300, 300)
	await idle(2)
	assert_eq(_drawn(), [[Vector2(300, 300), 30.0, 0]], "paused bullets, live rings")
	factory.is_factory_processing_bullets = true


func test_a_spawner_ready_before_its_factory_is_drawn() -> void:
	make_graze_target(Vector2(100, 0))
	var late := BulletFactory2D.new()
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.set_homing_enabled(false)
	sp.graze_zones = [_flagged([30.0])]
	sp.graze_enabled = true
	sp.graze_node_group = &"graze_targets"
	sp.set_bullet_factory(late)
	add(sp) # its wake finds no ready factory
	add(late) # the factory looks once when it becomes ready
	await idle(3)
	var circles: Array = late.debug_get_graze_runtime_preview()["circles"]
	assert_eq(circles.size(), 1, "the late factory found the spawner's flagged zone")
	assert_eq(_drawn(), [], "the other factory draws nothing for a spawner that is not its own")


func test_the_layer_is_internal_top_level_and_owner_less() -> void:
	make_graze_target(Vector2(100, 0))
	_graze_spawner([_flagged([30.0])])
	await idle(2)
	var layer := factory.get_node_or_null(LAYER) as GrazePreviewLayer2D
	assert_not_null(layer, "found by name")
	assert_true(layer.is_set_as_top_level(), "top-level")
	assert_null(layer.owner, "owner-less")
	assert_false(factory.get_children().has(layer), "internal")
	factory.reset()
	await idle(2)
	assert_eq(_drawn().size(), 1, "a factory reset leaves the spawner-held zone drawn")
	assert_eq(factory.get_node_or_null(LAYER), layer, "same layer")
