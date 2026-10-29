extends BlastTest
## Laser looks: each point animation of a laser (charge, muzzle, tip, pierce,
## wall impact) is a LaserSpriteLook2D in its LaserData2D slot. A look plays
## its SpriteFrames animation at its speed_scale, sits around its point
## (offset and rotation turned with the beam while align_to_beam, scale),
## tints by the laser's modulate times its own, and draws on its own canvas
## item (z_index, z_as_relative, material) under the laser; visible = false
## draws nothing. Edits inside a look reach lasers in flight, a look shared by
## two slots keeps passing its edits on while any slot holds it, a pooled
## laser keeps no look. body_width_scale widens only the drawn body (hits keep
## `width`). Scenes saved with the old per-look properties (charge_frames ..
## wall_impact_frames, effects_scale, material) load into the looks and never
## save them again.

const DT := 0.0625 # exact binary ticks
const SAVE_PATH := "user://laser_looks_migration.tres"


func after_each() -> void:
	if FileAccess.file_exists(SAVE_PATH):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(SAVE_PATH))
	await super()


## A SpriteFrames of `count` frames whose widths tell them apart (4, 5, ...).
func _frames(count: int, fps := 4.0) -> SpriteFrames:
	var sf := SpriteFrames.new()
	sf.set_animation_speed(&"default", fps)
	for i in count:
		sf.add_frame(&"default", ImageTexture.create_from_image(Image.create(4 + i, 4, false, Image.FORMAT_RGBA8)))
	return sf


func _look(count := 1) -> LaserSpriteLook2D:
	var look := LaserSpriteLook2D.new()
	look.frames = _frames(count)
	return look


## A beam of `length` from `origin` at `angle`, firing at once until ended.
func _fire(d: LaserData2D, angle := 0.0, origin := Vector2.ZERO) -> LaserVolley2D:
	var t: Array[Transform2D] = [Transform2D(angle, origin)]
	var l := factory.spawn_laser(d, t)
	assert_not_null(l, "fired")
	return l


func _marks(l: LaserVolley2D, slot: String) -> Array:
	return l.debug_get_look_draws(1.0).filter(func(m: Dictionary) -> bool: return m["slot"] == slot)


func test_the_look_is_bound_with_defaults_and_refuses_bad_input() -> void:
	var look := LaserSpriteLook2D.new()
	for m in ["set_frames", "set_animation", "set_speed_scale", "set_scale", "set_offset", "set_rotation_degrees", "set_align_to_beam", "set_modulate", "set_z_index", "set_z_as_relative", "set_material", "set_visible"]:
		assert_true(look.has_method(m), "LaserSpriteLook2D.%s is bound" % m)
	assert_eq([look.animation, look.speed_scale, look.scale, look.offset, look.rotation_degrees, look.align_to_beam, look.modulate, look.z_index, look.z_as_relative, look.visible], [&"", 1.0, Vector2.ONE, Vector2.ZERO, 0.0, true, Color.WHITE, 0, true, true], "defaults")
	watch_signals(look)
	look.speed_scale = -1.0
	look.speed_scale = NAN
	look.scale = Vector2(INF, 1)
	look.offset = Vector2(0, NAN)
	look.rotation_degrees = INF
	look.modulate = Color(NAN, 1, 1)
	look.z_index = 5000
	expect_error_sequence([
		"LaserSpriteLook2D: speed_scale must be finite and >= 0 (0 = holds the first frame), keeping the old value.",
		"LaserSpriteLook2D: speed_scale must be finite and >= 0 (0 = holds the first frame), keeping the old value.",
		"LaserSpriteLook2D: scale must be finite, keeping the old value.",
		"LaserSpriteLook2D: offset must be finite, keeping the old value.",
		"LaserSpriteLook2D: rotation_degrees must be finite, keeping the old value.",
		"LaserSpriteLook2D: modulate must be finite, keeping the old value.",
		"LaserSpriteLook2D: z_index must be between -4096 and 4096, keeping the old value.",
	])
	assert_eq([look.speed_scale, look.scale, look.offset, look.rotation_degrees, look.modulate, look.z_index], [1.0, Vector2.ONE, Vector2.ZERO, 0.0, Color.WHITE, 0], "old values kept")
	assert_signal_emit_count(look, "changed", 0, "a refused edit changes nothing")


func test_the_data_holds_five_look_slots_and_passes_their_edits_on() -> void:
	var d := LaserData2D.new()
	assert_eq([LaserData2D.LOOK_CHARGE, LaserData2D.LOOK_MUZZLE, LaserData2D.LOOK_TIP, LaserData2D.LOOK_PIERCE, LaserData2D.LOOK_WALL_IMPACT], [0, 1, 2, 3, 4], "serialized slot ids")
	var names := ["charge_look", "muzzle_look", "tip_look", "pierce_look", "wall_impact_look"]
	for slot in 5:
		var look := _look()
		d.set(names[slot], look)
		assert_same(d.get_look(slot), look, "%s is slot %d" % [names[slot], slot])
		var other := _look()
		d.set_look(slot, other)
		assert_same(d.get(names[slot]), other, "set_look writes %s" % names[slot])
	d.set_look(5, _look())
	expect_error_sequence(["LaserData2D: set_look slot must be one of the LookSlot values, nothing changed."])
	assert_null(d.get_look(5), "an out-of-range slot reads null, never past the slot array")
	assert_null(d.get_look(-1), "a negative slot reads null too")
	watch_signals(d)
	d.tip_look.scale = Vector2(2, 2)
	assert_signal_emit_count(d, "changed", 1, "an edit inside a look is a data change")


func test_a_shared_look_keeps_passing_edits_while_any_slot_holds_it() -> void:
	var d := LaserData2D.new()
	var look := _look()
	d.tip_look = look
	d.muzzle_look = look
	watch_signals(d)
	d.tip_look = null
	look.offset = Vector2(3, 0)
	assert_signal_emit_count(d, "changed", 2, "cleared one slot, the other still passes the edit on")
	d.muzzle_look = null
	look.offset = Vector2(4, 0)
	assert_signal_emit_count(d, "changed", 3, "held by no slot: its edits no longer reach the data")


func test_a_look_sits_around_its_point_and_turns_with_the_beam() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 100.0)
	d.modulate = Color(1, 0.5, 1, 1)
	var tip := _look()
	tip.offset = Vector2(10, 4)
	tip.rotation_degrees = 90.0
	tip.scale = Vector2(2, 3)
	tip.modulate = Color(0.5, 1, 1, 1)
	d.tip_look = tip
	var l := _fire(d, PI / 2.0, Vector2(20, 0))
	step_factory(1, DT)
	var marks := _marks(l, "tip")
	assert_eq(marks.size(), 1, "one tip")
	var pose: Transform2D = marks[0]["transform"]
	# The tip is at (20, 100); offset (10, 4) turned 90 degrees is (-4, 10).
	assert_almost_eq(pose.origin, Vector2(16, 110), Vector2(0.001, 0.001), "offset along / across the beam")
	assert_almost_eq(angle_difference(pose.get_rotation(), PI), 0.0, 0.0001, "the beam's angle plus rotation_degrees")
	assert_almost_eq(pose.get_scale(), Vector2(2, 3), Vector2(0.0001, 0.0001), "its scale (local axes)")
	assert_eq(marks[0]["color"], Color(0.5, 0.5, 1, 1), "the laser's modulate times the look's")
	assert_eq(marks[0]["texture"], tip.frames.get_frame_texture(&"default", 0), "its frame")
	tip.align_to_beam = false
	step_factory(1, DT)
	pose = _marks(l, "tip")[0]["transform"]
	assert_almost_eq(pose.origin, Vector2(30, 104), Vector2(0.001, 0.001), "unaligned: the offset is world-space")
	assert_almost_eq(pose.get_rotation(), PI / 2.0, 0.0001, "and only rotation_degrees turns it")
	tip.visible = false
	step_factory(1, DT)
	assert_eq(_marks(l, "tip").size(), 0, "invisible: nothing drawn")


func test_every_slot_draws_where_its_event_is() -> void:
	make_area(Vector2(100, 0), Vector2(20, 60))
	make_wall(Vector2(300, 0), Vector2(20, 200), 16)
	await idle(2)
	var d := H.make_laser(0.125, 0.0, 0.0, 0.0, 400.0) # charges 2 ticks
	d.wall_mask = 16
	for slot in 5:
		d.set_look(slot, _look())
	var l := _fire(d)
	step_factory(1, DT)
	var charging: Array = l.debug_get_look_draws(1.0)
	assert_eq(charging.map(func(m): return m["slot"]), ["charge"], "while charging: the charge look only")
	assert_almost_eq((charging[0]["transform"] as Transform2D).origin, Vector2.ZERO, Vector2(0.001, 0.001), "at the muzzle")
	step_factory(2, DT)
	var at := {}
	for m in l.debug_get_look_draws(1.0):
		at[m["slot"]] = (m["transform"] as Transform2D).origin.round()
	assert_eq(at, {"muzzle": Vector2(0, 0), "tip": Vector2(290, 0), "pierce": Vector2(100, 0), "wall_impact": Vector2(290, 0)}, "muzzle, tip at the wall, the pierced target, the wall impact")


func test_speed_scale_runs_a_look_faster_or_holds_it() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0)
	var tip := _look(4) # 4 fps: 0.25 s per frame
	tip.speed_scale = 2.0
	d.tip_look = tip
	var l := _fire(d)
	var frames: Array = []
	for i in 5:
		step_factory(1, DT)
		frames.append(int((_marks(l, "tip")[0]["texture"] as Texture2D).get_size().x) - 4)
	assert_eq(frames, [0, 1, 1, 2, 2], "twice as fast: a frame every 0.125 s")
	tip.speed_scale = 0.0
	step_factory(3, DT)
	assert_eq(int((_marks(l, "tip")[0]["texture"] as Texture2D).get_size().x) - 4, 0, "0 holds the first frame")


func test_each_look_has_its_own_canvas_item_and_settings() -> void:
	var plain := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0))
	step_factory(1, DT)
	assert_eq(plain.debug_get_look_layers().map(func(e): return e["created"]), [false, false, false, false, false], "no look: no extra canvas item")
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0)
	var mat := CanvasItemMaterial.new()
	var tip := _look()
	tip.z_index = -3
	tip.z_as_relative = false
	tip.material = mat
	d.tip_look = tip
	d.body_material = CanvasItemMaterial.new()
	var l := _fire(d)
	step_factory(1, DT)
	var layers: Array = l.debug_get_look_layers()
	assert_eq(layers.map(func(e): return e["created"]), [true, true, true, true, true], "the five items exist together")
	assert_eq([layers[2]["z_index"], layers[2]["z_as_relative"], layers[2]["material"], layers[2]["visible"]], [-3, false, mat, true], "the tip's own z order and material")
	assert_eq(layers[0]["visible"], false, "an empty slot draws nothing")
	assert_eq(l.material, d.body_material, "the node carries the body material only")
	tip.z_index = 9
	tip.material = null
	step_factory(1, DT)
	layers = l.debug_get_look_layers()
	assert_eq(layers[2]["z_index"], 9, "edits reach the laser in flight")
	assert_null(layers[2]["material"], "the material too")
	var replaced := _look()
	d.tip_look = replaced
	step_factory(1, DT)
	assert_eq(_marks(l, "tip")[0]["texture"], replaced.frames.get_frame_texture(&"default", 0), "a new look in the slot draws at once")


func test_a_pooled_laser_keeps_no_look() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0625, 0.0)
	var tip := _look()
	tip.material = CanvasItemMaterial.new()
	d.tip_look = tip
	var l := _fire(d)
	step_factory(3, DT)
	assert_true(l.is_pooled(), "finished and pooled")
	var layers: Array = l.debug_get_look_layers()
	assert_eq(layers.map(func(e): return [e["material"] == null, e["visible"]]), [[true, false], [true, false], [true, false], [true, false], [true, false]], "no look, no material kept alive")
	var again := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0))
	assert_same(again, l, "reused")
	step_factory(1, DT)
	assert_eq(again.debug_get_look_draws(1.0), [], "the next life draws no old look")
	var w: WeakRef = weakref(tip.material)
	tip.material = null
	assert_null(w.get_ref(), "the pool held no reference to the old material")


func test_body_width_scale_widens_only_the_drawn_body() -> void:
	var target := make_area(Vector2(100, 9), Vector2(20, 4)) # 7..11 px off the beam axis
	await idle(2)
	var hits: Array = []
	factory.laser_hit.connect(func(t, _l, _b, _p) -> void: hits.append(t))
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)
	d.width = 12.0 # reaches 6 px off the axis: misses the target
	d.body_width_scale = 2.0
	var l := _fire(d)
	step_factory(2, DT)
	var s: Dictionary = l.debug_get_draw_state(1.0)[0]
	assert_eq([s["width"], s["body_width"]], [12.0, 24.0], "the hit width and the drawn body")
	var ys: Array = []
	for p in (l.debug_get_body_mesh(1.0)["points"] as PackedVector2Array):
		ys.append(absf(p.y))
	assert_almost_eq(ys.max(), 12.0, 0.001, "the strip is 24 px wide")
	assert_eq(hits, [], "hits keep the 12 px width")
	d.body_width_scale = -1.0
	expect_error_sequence(["LaserData2D: body_width_scale must be finite and >= 0, keeping the old value."])
	d.body_width_scale = 0.0
	step_factory(1, DT)
	assert_eq((l.debug_get_body_mesh(1.0)["points"] as PackedVector2Array).size(), 0, "0 draws no body")
	assert_ne(target, null)


func test_scenes_saved_with_the_old_look_properties_load_into_the_looks() -> void:
	var sf := _frames(1)
	var mat := CanvasItemMaterial.new()
	# What a scene saved before the looks holds, in its saved order.
	var d := LaserData2D.new()
	d.set("muzzle_frames", sf)
	d.set("tip_frames", sf)
	d.set("wall_impact_frames", sf)
	d.set("effects_scale", 2.5)
	d.set("material", mat)
	expect_no_errors("the old names load silently")
	assert_not_null(d.muzzle_look, "muzzle_frames made the muzzle look")
	assert_eq([d.muzzle_look.frames, d.tip_look.frames, d.wall_impact_look.frames], [sf, sf, sf], "with their frames")
	assert_eq([d.muzzle_look.scale, d.tip_look.scale, d.wall_impact_look.scale], [Vector2(2.5, 2.5), Vector2(2.5, 2.5), Vector2(2.5, 2.5)], "effects_scale became their scale")
	assert_null(d.charge_look, "an unset slot stays empty")
	assert_eq(d.body_material, mat, "material is the body material")
	assert_eq([d.get("tip_frames"), d.get("effects_scale"), d.get("material")], [sf, 2.5, mat], "scripts reading the old names still get them")
	var listed: Array = d.get_property_list().map(func(p): return p["name"])
	for old in ["charge_frames", "muzzle_frames", "tip_frames", "pierce_frames", "wall_impact_frames", "effects_scale", "material"]:
		assert_false(listed.has(old), "%s is never listed (never saved again)" % old)
	assert_eq(ResourceSaver.save(d, SAVE_PATH), OK, "saved")
	var text := FileAccess.get_file_as_string(SAVE_PATH)
	assert_false("tip_frames" in text or "effects_scale" in text, "the saved file holds only the new properties")
	assert_true("tip_look" in text and "body_material" in text, "the looks are saved")
	var loaded: LaserData2D = ResourceLoader.load(SAVE_PATH, "", ResourceLoader.CACHE_MODE_IGNORE)
	assert_eq([loaded.tip_look.scale, loaded.tip_look.frames.get_frame_count(&"default")], [Vector2(2.5, 2.5), 1], "and load back")


func test_an_old_scene_file_loads_its_looks() -> void:
	# A .tres written by the old version (the user's own scenes look like it).
	var f := FileAccess.open(SAVE_PATH, FileAccess.WRITE)
	f.store_string('[gd_resource type="LaserData2D" load_steps=3 format=3]\n\n[sub_resource type="SpriteFrames" id="sf"]\nanimations = [{"frames": [], "loop": true, "name": &"default", "speed": 5.0}]\n\n[sub_resource type="CanvasItemMaterial" id="mat"]\n\n[resource]\ntip_frames = SubResource("sf")\nwall_impact_frames = SubResource("sf")\neffects_scale = 2.82\nmaterial = SubResource("mat")\nz_index = 3\n')
	f.close()
	var d: LaserData2D = ResourceLoader.load(SAVE_PATH, "", ResourceLoader.CACHE_MODE_IGNORE)
	assert_not_null(d, "loads")
	assert_not_null(d.tip_look, "tip_frames became the tip look")
	assert_almost_eq(d.wall_impact_look.scale.x, 2.82, 0.0001, "effects_scale applied after the frames")
	assert_not_null(d.body_material, "material became the body material")
	assert_eq(d.z_index, 3, "the rest loads as before")
