extends BlastTest
## Misconfiguration UX + muzzle offset space:
## - spawn_position_offset_space GLOBAL (default, compat) vs LOCAL (turns
##   with the generator), and the preview draws the offset exactly where
##   the bullets spawn (spin included);
## - editor configuration warnings list every blocking misconfiguration;
## - an auto-firing misconfigured spawner reports ONCE, not every interval
##   (manual shoot_once calls always report), re-armed by a config change.
##
## get_setup_warnings() (the editor's warning icon) names every
## misconfiguration that would make shots fail or a feature silently do
## nothing - and is empty for a correctly wired spawner.

func _first_bullet(sp: BulletSpawner2D) -> Vector2:
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	var v: BulletVolley2D = get_signal_parameters(sp, "volley_fired", get_signal_emit_count(sp, "volley_fired") - 1)[0]
	return v.get_bullet_transform(0).origin


func test_offset_space_global_vs_local() -> void:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.global_position = Vector2(100, 100)
	sp.rotation = PI / 2.0
	sp.spawn_position_offset = Vector2(20, 0)
	assert_eq(sp.spawn_position_offset_space, BulletSpawner2D.SPAWN_OFFSET_GLOBAL, "default stays GLOBAL (scene compatibility)")
	assert_almost_eq(_first_bullet(sp), Vector2(120, 100), Vector2(0.01, 0.01), "GLOBAL: world-space shift")
	sp.spawn_position_offset_space = BulletSpawner2D.SPAWN_OFFSET_LOCAL
	assert_almost_eq(_first_bullet(sp), Vector2(100, 120), Vector2(0.01, 0.01), "LOCAL: the muzzle turns with the spawner")
	sp.spawn_position_offset_space = 5
	expect_error_sequence(["spawn_position_offset_space must be 0 (Global) or 1 (Local)"])
	assert_eq(sp.spawn_position_offset_space, BulletSpawner2D.SPAWN_OFFSET_LOCAL, "invalid space rejected")


func test_preview_draws_the_offset(space: int = use_parameters([0, 1])) -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 10)
	sp.position = Vector2(400, 300)
	sp.rotation = 0.7
	sp.spawn_position_offset = Vector2(30, -12)
	sp.spawn_position_offset_space = space
	sp.spin_enabled = true
	sp.spin_speed_deg_per_sec = 45.0
	await idle(12)
	var layer: Node2D = sp.get_node("~BlastBulletsPatternPreview/Dots")
	var dots: PackedVector2Array = sp.debug_get_preview_dot_points()
	watch_signals(sp)
	assert_true(sp.shoot_once())
	var v: BulletVolley2D = get_signal_parameters(sp, "volley_fired", 0)[0]
	for i in dots.size():
		assert_almost_eq(layer.get_global_transform() * dots[i], v.get_bullet_transform(i).origin, Vector2(0.05, 0.05), "space %d: dot %d drawn where bullet %d spawned" % [space, i, i])


func test_configuration_warnings() -> void:
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	add_in_own_world(sp) # no factory in its world to fall back on
	var w: PackedStringArray = sp.get_setup_warnings()
	assert_true(_has(w, "No BulletFactory2D in this world"), "missing factory listed")
	assert_true(_has(w, "No spawn_data"), "missing spawn data listed")
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(H.make_volley_data(2))
	sp.movement_enabled = true
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_AIMED
	w = sp.get_setup_warnings()
	assert_false(_has(w, "No BulletFactory2D"), "factory fixed")
	assert_false(_has(w, "No spawn_data"), "spawn data fixed")
	assert_true(_has(w, "movement_path is empty"), "movement without a path listed")
	assert_true(_has(w, "Aimed pattern needs helper_aimed_target"), "aimed without a target listed")
	var bare := H.make_volley_data(1)
	bare.sprite_frames = null
	sp.set_spawn_data(bare)
	assert_true(_has(sp.get_setup_warnings(), "bullets will be invisible"), "invisible bullets listed")
	await idle(1)
	expect_warning_sequence(["BulletSpawner2D: movement_path is not a Path2D with a non-empty curve in the tree; the spawner stays where it is."], "and the first frame that travels names the missing path")


func _has(w: PackedStringArray, needle: String) -> bool:
	for s in w:
		if s.contains(needle):
			return true
	return false


func test_auto_fire_reports_a_misconfiguration_once() -> void:
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.shoot_interval_sec = 0.05
	add(sp)
	sp.set_bullet_factory(factory)
	sp.set_shooting_enabled(true) # no spawn_data: every interval fails
	await idle(30) # ~10 attempts
	assert_eq(expect_errors_containing("shoot_once: no spawn_data assigned", 1, "", true), 1, "auto-fire: one report, not one per interval") # lint: at-least the count is asserted exactly by assert_eq
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING # a real configuration change: re-armed
	await idle(10)
	expect_errors_containing("shoot_once: no spawn_data assigned", 1, "re-armed after a config change")
	sp.set_shooting_enabled(false)
	sp.shoot_once()
	sp.shoot_once()
	expect_errors_containing("shoot_once: no spawn_data assigned", 2, "manual calls always report")


## A rejected or no-op config write is not a configuration change: it must not
## reset the auto-fire error latch and re-fire the same misconfiguration.
func test_a_rejected_write_does_not_rearm_the_auto_fire_error() -> void:
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.shoot_interval_sec = 0.05
	add(sp)
	sp.set_bullet_factory(factory)
	sp.set_shooting_enabled(true) # no spawn_data: every interval fails
	await idle(30)
	expect_errors_containing("shoot_once: no spawn_data assigned", 1, "one report, latched")
	sp.set_pattern_source(99) # rejected: not a config change
	sp.set_movement_enabled(sp.get_movement_enabled()) # no-op: not a config change
	expect_errors_containing("invalid pattern_source", 1, "the rejection reports")
	await idle(20)
	expect_errors_containing("shoot_once: no spawn_data assigned", 0, "rejected/no-op writes keep the latch")
	sp.set_shooting_enabled(false)


## A no-op feature switch is not a configuration change either: it must not
## reset the auto-fire error latch.
func test_a_noop_feature_switch_does_not_rearm_the_auto_fire_error() -> void:
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.shoot_interval_sec = 0.05
	add(sp)
	sp.set_bullet_factory(factory)
	sp.set_shooting_enabled(true) # no spawn_data: every interval fails
	await idle(30)
	expect_errors_containing("shoot_once: no spawn_data assigned", 1, "one report, latched")
	sp.set_graze_enabled(sp.get_graze_enabled()) # no-op (false -> false)
	sp.set_sound_enabled(sp.get_sound_enabled()) # no-op
	await idle(20)
	expect_errors_containing("shoot_once: no spawn_data assigned", 0, "no-op switches keep the latch")
	sp.set_shooting_enabled(false)


## Every stored property written back with its own value keeps the latch:
## a script that writes its whole config every frame must not re-arm the
## auto-fire report each frame (ClassDB-driven: new setters are covered).
func test_every_same_value_write_keeps_the_auto_fire_latch(kind: String = use_parameters(["BulletSpawner2D", "LaserSpawner2D"])) -> void:
	var text := "no spawn_data assigned" if kind == "BulletSpawner2D" else "no laser_data assigned"
	var sp: PatternSpawner2D = ClassDB.instantiate(kind)
	sp.set_shooting_enabled(false)
	sp.shoot_interval_sec = 0.05 # 3 frames
	add(sp)
	sp.set_bullet_factory(factory)
	sp.set_shooting_enabled(true) # no data: every interval fails
	await idle(10)
	expect_errors_containing(text, 1, "one report, latched")
	var rearmed: Array = []
	for prop in sp.get_property_list():
		if not (prop.usage & PROPERTY_USAGE_STORAGE) or prop.name in ["script", "owner"]:
			continue
		if prop.type == TYPE_ARRAY:
			continue # an Array write always counts: a script may have edited it in place
		if not ClassDB.class_has_method(kind, "set_" + prop.name) and not prop.name in ["process_mode"]:
			continue
		sp.set(prop.name, sp.get(prop.name))
		await idle(4) # longer than one interval
		var seen := 0
		for err in get_errors():
			if not err.handled and err.contains_text(text):
				err.handled = true
				seen += 1
		if seen > 0:
			rearmed.append(prop.name)
	assert_eq(", ".join(rearmed), "", "same-value writes that re-armed the report")
	expect_no_errors()
	sp.set_shooting_enabled(false)


## A real transforms_generator change re-arms the auto-fire report; writing
## the same generator back keeps the latch (the documented policy).
func test_a_transforms_generator_change_rearms_the_auto_fire_error() -> void:
	var sp := make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.shoot_interval_sec = 0.05
	sp.set_spawn_data(null) # every shot fails: "no spawn_data assigned"
	var a := Node2D.new()
	add(a)
	var b := Node2D.new()
	add(b)
	sp.set_transforms_generator(a)
	sp.set_shooting_enabled(true)
	await idle(30)
	expect_errors_containing("shoot_once: no spawn_data assigned", 1, "one report, latched")
	sp.set_transforms_generator(a) # same generator: keeps the latch
	await idle(10)
	expect_errors_containing("shoot_once: no spawn_data assigned", 0, "a same-value write keeps it")
	sp.set_transforms_generator(b) # a real config change
	await idle(20)
	expect_errors_containing("shoot_once: no spawn_data assigned", 1, "re-armed after a real change")
	sp.set_shooting_enabled(false)


## The node setters (Variant in, Object out) store no property of their own,
## so the stored sweep above never reaches them: the same node written back
## keeps the latch, a different node re-arms it (ClassDB-driven).
func test_every_node_setter_keeps_the_latch_on_the_same_node(kind: String = use_parameters(["BulletSpawner2D", "LaserSpawner2D"])) -> void:
	var text := "no spawn_data assigned" if kind == "BulletSpawner2D" else "no laser_data assigned"
	var setters: Array = []
	for cls in ["PatternSpawner2D", kind]:
		for m in ClassDB.class_get_method_list(cls, true):
			var getter: String = "get_" + str(m.name).substr(4)
			if str(m.name).begins_with("set_") and m.args.size() == 1 and m.args[0].type == TYPE_NIL and ClassDB.class_has_method(kind, getter):
				setters.append(str(m.name).substr(4))
	assert_true(setters.has("bullet_factory") and setters.has("helper_aimed_target"), "the walk finds the node setters: %s" % [setters])
	var rearmed: Array = []
	for prop in setters:
		var path := Path2D.new() # a Node2D and a Path2D: valid for every node setter but the factory
		path.curve = Curve2D.new()
		add(path)
		var sp: PatternSpawner2D = ClassDB.instantiate(kind)
		sp.set_shooting_enabled(false)
		sp.shoot_interval_sec = 0.05 # 3 frames
		add(sp)
		sp.set_bullet_factory(factory)
		if prop != "bullet_factory":
			sp.call("set_" + prop, path)
		sp.set_shooting_enabled(true) # no data: every interval fails
		await idle(10)
		expect_errors_containing(text, 1, "%s: one report, latched" % prop)
		sp.call("set_" + prop, sp.call("get_" + prop))
		await idle(4) # longer than one interval
		var seen := 0
		for err in get_errors():
			if not err.handled and err.contains_text(text):
				err.handled = true
				seen += 1
		if seen > 0:
			rearmed.append(prop)
		sp.set_shooting_enabled(false)
	assert_eq(", ".join(rearmed), "", "same-node writes that re-armed the report")


## A real node change still re-arms, also a null written over a path that
## resolves to nothing (both sides null, but the stored path changes).
func test_a_real_node_change_rearms_the_auto_fire_error(prop: String = use_parameters(["helper_aimed_target", "helper_path2d_node", "transforms_generator", "bullet_factory"])) -> void:
	const TEXT := "shoot_once: no spawn_data assigned"
	var path_setter := {"helper_aimed_target": "set_helper_aimed_target_path", "helper_path2d_node": "set_helper_path2d_path", "transforms_generator": "set_transforms_generator_path"}
	var sp := make_spawner()
	sp.shoot_interval_sec = 0.05 # 3 frames
	sp.set_spawn_data(null) # every shot fails
	var a := Path2D.new() # a Node2D and a Path2D: valid for every node setter but the factory
	a.curve = Curve2D.new()
	add(a)
	var b := Path2D.new()
	b.curve = Curve2D.new()
	add(b)
	var other: Node = b
	if prop == "bullet_factory":
		other = BulletFactory2D.new()
		add(other)
	else:
		sp.call("set_" + prop, a)
	sp.set_shooting_enabled(true)
	await idle(10)
	expect_errors_containing(TEXT, 1, "one report, latched")
	sp.call("set_" + prop, other)
	await idle(4) # longer than one interval
	expect_errors_containing(TEXT, 1, "a different node re-arms")
	if path_setter.has(prop):
		assert_true(ClassDB.class_has_method("BulletSpawner2D", path_setter[prop]), "path setter bound")
		sp.call(path_setter[prop], NodePath("missing"))
		await idle(4)
		expect_errors_containing(TEXT, 1, "a new path re-arms")
		assert_null(sp.call("get_" + prop), "the path resolves to nothing")
		sp.call("set_" + prop, null)
		await idle(4)
		expect_errors_containing(TEXT, 1, "null over an unresolved path clears the path: a change")
		assert_eq(sp.call(path_setter[prop].replace("set_", "get_")), NodePath(), "the path is gone")
	sp.set_shooting_enabled(false)


func _has_1(sp: BulletSpawner2D, text: String) -> bool:
	return Array(sp.get_setup_warnings()).any(func(w): return text in str(w))


func test_wired_spawner_has_no_warnings() -> void:
	var sp := make_spawner()
	assert_eq(Array(sp.get_setup_warnings()), [], "nothing to fix")


func test_each_misconfiguration_is_named() -> void:
	var sp := make_spawner()
	var wrong := Node2D.new()
	add(wrong)
	sp.set_bullet_factory(null)
	move_to_own_world(sp) # no factory in its world to fall back on
	assert_true(_has_1(sp, "No BulletFactory2D in this world"), "missing factory")
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(null)
	assert_true(_has_1(sp, "No spawn_data"), "missing spawn data")
	var invisible := BulletVolleyData2D.new()
	sp.set_spawn_data(invisible)
	assert_true(_has_1(sp, "bullets will be invisible"), "spawn data without art")
	sp.set_spawn_data(H.make_volley_data(2))
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_AIMED
	assert_true(_has_1(sp, "Aimed pattern needs helper_aimed_target"), "aimed without a target")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_CORRIDOR
	assert_true(_has_1(sp, "Corridor pattern aims at helper_aimed_target"), "corridor without a target")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D
	assert_true(_has_1(sp, "Path2D pattern needs helper_path2d_path"), "path pattern without a path")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM
	assert_true(_has_1(sp, "Custom pattern has no helper_custom_transforms"), "empty custom pattern")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
	sp.movement_enabled = true
	assert_true(_has_1(sp, "movement_path is empty"), "movement without a path")
	sp.movement_enabled = false
	sp.set_orbiting_enabled(true)
	assert_true(_has_1(sp, "orbiting_enabled needs homing_enabled"), "orbiting without homing")
	sp.set_orbiting_enabled(false)
	assert_eq(Array(sp.get_setup_warnings()), [], "clean again")
