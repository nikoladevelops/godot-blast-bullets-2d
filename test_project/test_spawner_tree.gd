extends SceneTree

# Headless integration fuzz for BulletSpawner2D + BulletFactory2D in a live tree.
# Run with:
#   godot --headless --path test_project --script test_spawner_tree.gd
# Exit code 0 = all checks pass, 1 = a failure printed below.
# Reaching the end proves survival of every error path; the PASS lines prove
# the documented behavior (rejections, restores, fallbacks, empty resolutions).

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _finite_volley(v: Array) -> bool:
	for t in v:
		var tr: Transform2D = t
		if not tr.is_finite():
			return false
	return true

func _make_data() -> DirectionalBulletsData2D:
	var data := DirectionalBulletsData2D.new()
	data.transforms = [Transform2D.IDENTITY, Transform2D(0.0, Vector2(50, 0))]
	var sp := BulletSpeedData2D.new()
	sp.speed = 200.0
	sp.max_speed = 3000.0
	sp.acceleration = 1500.0
	data.all_bullet_speed_data = [sp]
	data.max_life_time = 5.0
	data.set_collision_layer_from_array([2])
	data.set_collision_mask_from_array([3])
	return data

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	factory.name = "Factory"
	get_root().add_child(factory)
	var spawner := BulletSpawner2D.new()
	spawner.name = "Spawner"
	get_root().add_child(spawner)
	await process_frame
	await process_frame
	# Manual-fire only: auto-shoot would spam on every frame (and hang the
	# run if a future bug ever aborts this script before quit()).
	spawner.set_shooting_enabled(false)

	printerr("TREE T1 error paths")
	# No factory, no data: loud failure, no state change, no crash.
	var bare := BulletSpawner2D.new()
	get_root().add_child(bare)
	bare.set_shooting_enabled(false)
	_check(bare.shoot_once() == false, "shoot without factory fails")
	_check(bare.get_volleys_fired() == 0, "failed shot not counted")
	bare.set_bullet_factory(factory)
	_check(bare.shoot_once() == false, "shoot without spawn_data fails")
	_check(bare.spawn_pattern_list([], false, 0.25) == 0, "empty pattern list rejected")
	var src0: int = bare.get_pattern_source()
	var amt0: int = bare.get_helper_bullets_amount()
	var fired_bad: int = bare.spawn_pattern_list(
		[42, "x", {"pattern_source": 999}, {"helper_bullets_amount": "x"}, {"spawn_data": null}],
		true, 0.25)
	_check(fired_bad == 0, "malformed simultaneous list fires nothing")
	_check(bare.get_pattern_source() == src0, "pattern_source restored after bad list")
	_check(bare.get_helper_bullets_amount() == amt0, "bullets_amount restored after bad list")
	_check(bare.get_spawn_data() == null or not bare.get_spawn_data().is_valid(), "spawn_data untouched by bad list")
	var seq: int = bare.spawn_pattern_list([{"helper_bullets_amount": 3}], false, 0.01)
	_check(seq == 1 and bare.is_pattern_list_active(), "sequential list queues")
	bare.stop_pattern_list()
	_check(not bare.is_pattern_list_active(), "stop_pattern_list disarms")
	bare.pattern_source = 33
	_check(bare.get_pattern_source() == src0, "pattern_source 33 rejected")
	bare.pattern_source = -1
	_check(bare.get_pattern_source() == src0, "pattern_source -1 rejected")
	bare.queue_free()

	printerr("TREE T2 preview x33")
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(_make_data())
	spawner.show_preview_during_runtime = true
	spawner.show_pattern_preview = true
	spawner.helper_bullets_amount = 5
	for src in 33:
		spawner.pattern_source = src
		var tf: Array = spawner.collect_spawn_transforms()
		if src == 7 or src == 24 or src == 29:
			_check(tf.is_empty(), "src %d targetless/empty is empty" % src)
		else:
			_check(tf.size() >= 1 and _finite_volley(tf), "src %d collects sane (n=%d)" % [src, tf.size()])
	# Cap boundary: 10000 accepted and collected, 10001 rejected.
	spawner.helper_bullets_amount = 10000
	_check(spawner.get_helper_bullets_amount() == 10000, "amount cap accepts 10000")
	spawner.pattern_source = 3
	_check(spawner.collect_spawn_transforms().size() == 10000, "cap volley collects 10000")
	spawner.helper_bullets_amount = 10001
	_check(spawner.get_helper_bullets_amount() == 10000, "amount 10001 rejected")
	spawner.helper_bullets_amount = 5
	# Custom compose + order ops (spawner at origin identity).
	spawner.pattern_source = 24
	spawner.set_helper_custom_transforms([Transform2D(0.0, Vector2(10, 0)), Transform2D(0.0, Vector2(0, 20)), Transform2D(0.0, Vector2(-5, -5))])
	var cust: Array = spawner.collect_spawn_transforms()
	_check(cust.size() == 3, "custom collects all")
	if cust.size() == 3:
		_check((cust[0] as Transform2D).origin.distance_to(Vector2(10, 0)) < 0.01, "custom composes marker")
	spawner.set_helper_custom_reverse(true)
	var crev: Array = spawner.collect_spawn_transforms()
	_check(crev.size() == 3, "custom reverse keeps count")
	if crev.size() == 3:
		_check((crev[0] as Transform2D).origin.distance_to(Vector2(-5, -5)) < 0.01, "custom reverse flips order")
	spawner.set_helper_custom_reverse(false)
	# Regression (COW aliasing): rebuilds and mode switches must never wipe
	# placed transforms — the snapshot keeps a private copy now.
	_check(spawner.get_helper_custom_transforms().size() == 3, "custom array survives rebuilds")
	spawner.pattern_source = 6
	spawner.pattern_source = 24
	_check(spawner.get_helper_custom_transforms().size() == 3, "custom array survives mode switch")
	_check(spawner.collect_spawn_transforms().size() == 3, "custom still collects after switch")
	# Line row + order ops: center-anchored row, reverse flips it.
	spawner.pattern_source = 6
	spawner.helper_bullets_amount = 5
	spawner.set_helper_line_direction(Vector2(1, 0))
	spawner.set_helper_line_spacing(10.0)
	var line_plain: Array = spawner.collect_spawn_transforms()
	var line_x: Array = []
	for t in line_plain:
		line_x.append((t as Transform2D).origin.x)
	var line_ok := line_x.size() == 5
	var line_want := [-20.0, -10.0, 0.0, 10.0, 20.0]
	for i in line_x.size():
		if i >= 5 or not (line_x[i] is float and absf(line_x[i] - line_want[i]) < 0.01):
			line_ok = false
	_check(line_ok, "line centers row")
	spawner.set_helper_line_reverse(true)
	var line_rev: Array = spawner.collect_spawn_transforms()
	var rev_x: Array = []
	for t in line_rev:
		rev_x.append((t as Transform2D).origin.x)
	var rev_ok := rev_x.size() == 5
	var rev_want := [20.0, 10.0, 0.0, -10.0, -20.0]
	for i in rev_x.size():
		if i >= 5 or not (rev_x[i] is float and absf(rev_x[i] - rev_want[i]) < 0.01):
			rev_ok = false
	_check(rev_ok, "line reverse flips row")
	spawner.set_helper_line_reverse(false)

	printerr("TREE T3 homing resolve")
	spawner.set_homing_enabled(true)
	spawner.set_homing_max_targets(5)
	spawner.set_homing_target_source(0)
	spawner.set_homing_node_group("no_such_group_xyz")
	_check(spawner.resolve_homing_targets(true).is_empty(), "empty group resolves empty")
	var d1 := Node2D.new()
	d1.name = "DummyA"
	d1.position = Vector2(100, 0)
	d1.add_to_group("test_targets")
	get_root().add_child(d1)
	var d2 := Node2D.new()
	d2.name = "DummyB"
	d2.position = Vector2(200, 0)
	d2.add_to_group("test_targets")
	get_root().add_child(d2)
	spawner.set_homing_node_group("test_targets")
	for sel in [0, 1, 2, 3, 4]:
		spawner.set_homing_target_selection(sel)
		_check(spawner.resolve_homing_targets(true).size() == 2, "selection %d resolves both" % sel)
	spawner.set_homing_target_source(1)
	_check(spawner.resolve_homing_targets(true).is_empty(), "mouse resolves empty array")
	spawner.set_homing_target_source(2)
	spawner.set_homing_global_position(Vector2(50, 50))
	var gp: Array = spawner.resolve_homing_targets(true)
	_check(gp.size() == 1 and (gp[0] as Vector2) == Vector2(50, 50), "global position resolves")
	spawner.set_homing_global_position(Vector2(NAN, NAN))
	_check(spawner.get_homing_global_position() == Vector2(50, 50), "NAN global keeps old value")
	_check(spawner.resolve_homing_targets(true).size() == 1, "kept global still resolves")
	spawner.set_homing_target_source(3)
	spawner.set_homing_target_path(NodePath("nope"))
	_check(spawner.resolve_homing_targets(true).is_empty(), "dead path resolves empty")
	spawner.set_homing_target_path(d1.get_path())
	_check(spawner.resolve_homing_targets(true).size() == 1, "live path resolves")
	spawner.set_homing_target_source(4)
	spawner.set_homing_node_name("")
	_check(spawner.resolve_homing_targets(true).is_empty(), "empty name resolves empty")
	spawner.set_homing_node_name("Dummy")
	spawner.set_homing_node_name_match_mode(1)
	_check(spawner.resolve_homing_targets(true).size() == 2, "contains name resolves both")
	spawner.set_homing_target_source(5)
	spawner.set_homing_children_parent_path(NodePath("."))
	# Regression: the runtime preview holder is a Node2D child of the
	# spawner and must never resolve as a homing target.
	_check(spawner.resolve_homing_targets(true).is_empty(), "childless parent resolves empty")
	_check(spawner.retarget_live_volleys() == 0, "retarget with no volleys is 0")
	spawner.set_homing_enabled(false)

	printerr("TREE T4 live shoot")
	spawner.pattern_source = 3
	# Homing on so the volley is tracked (plain volleys stay untracked by
	# design: only homing volleys need retargeting).
	spawner.set_homing_enabled(true)
	spawner.set_homing_target_source(0)
	spawner.set_homing_node_group("test_targets")
	var ok: bool = spawner.shoot_once()
	if ok:
		_check(spawner.get_volleys_fired() == 1, "fired volley counted")
		_check(spawner.get_live_volley_count() == 1, "fired volley tracked")
		_check(spawner.get_active_live_bullet_count() >= 1, "live bullets census")
		spawner.clear_live_volleys()
		_check(spawner.get_live_volley_count() == 0, "clear forgets volleys")
		factory.free_active_bullets()
		_check(spawner.get_active_live_bullet_count() == 0, "free_active drains census")
	else:
		_check(spawner.get_volleys_fired() == 0, "failed shot not counted (headless spawn unsupported)")

	printerr("TREE T5 factory smoke")
	factory.reset()
	factory.free_active_bullets()
	factory.free_disabled_bullets()
	factory.free_bullets_pool(0)
	_check(factory.debug_get_total_bullets_amount(0) == 0, "empty factory totals 0")
	_check(factory.debug_get_active_bullets_amount(0) == 0, "empty factory actives 0")
	_check(factory.debug_get_bullets_pool_amount(0) == 0, "empty factory pool 0")
	factory.populate_bullets_pool(null, null, 0)
	factory.populate_bullets_pool(null, _make_data(), 3)
	_check(factory.debug_get_bullets_pool_amount(0) == 0, "null-key populate creates nothing")
	d1.queue_free()
	d2.queue_free()
	print("----")
	if failures == 0:
		print("ALL SPAWNER TREE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
