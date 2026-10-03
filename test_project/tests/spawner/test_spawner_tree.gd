extends BlastTest
## Integration fuzz for BulletSpawner2D + BulletFactory2D in a live tree:
## error paths, preview/cap/custom/line collection, homing resolution,
## live shooting, and factory smoke. Survival + documented behavior.
## Adapted from the SceneTree original: shared member factory, spawners via
## add()/make_spawner (autofreed), frames via idle(), rejections fail loud.
func _finite_volley(v: Array) -> bool:
	for t in v:
		var tr: Transform2D = t
		if not tr.is_finite():
			return false
	return true

func _make_data() -> BulletVolleyData2D:
	var data := BulletVolleyData2D.new()
	data.transforms = [Transform2D.IDENTITY, Transform2D(0.0, Vector2(50, 0))]
	var sp := BulletSpeedData2D.new()
	sp.speed = 200.0
	sp.max_speed = 3000.0
	sp.acceleration = 1500.0
	data.all_bullet_speed_data = [sp]
	data.max_life_time = 5.0
	data.sprite_frames = H.make_sprite_frames()
	data.set_collision_layer_from_array([2])
	data.set_collision_mask_from_array([3])
	return data


func test_error_paths() -> void:
	var bare := BulletSpawner2D.new()
	add(bare)
	bare.set_shooting_enabled(false)
	# No factory, no data: loud failure, no state change, no crash.
	assert_true(bare.shoot_once() == false, "shoot without factory fails")
	expect_errors_containing("no BulletFactory2D assigned", 1, "missing factory fails loud")
	assert_true(bare.get_volleys_fired() == 0, "failed shot not counted")
	bare.set_bullet_factory(factory)
	assert_true(bare.shoot_once() == false, "shoot without spawn_data fails")
	expect_errors_containing("no spawn_data assigned", 1, "missing data fails loud")
	assert_true(bare.spawn_pattern_list([], false, 0.25) == 0, "empty pattern list rejected")
	expect_errors_containing("entries is empty", 1, "empty list fails loud")
	var src0: int = bare.get_pattern_source()
	var amt0: int = bare.get_helper_bullets_amount()
	var fired_bad: int = bare.spawn_pattern_list(
		[42, "x", {"pattern_source": 999}, {"helper_bullets_amount": "x"}, {"spawn_data": null}],
		true, 0.25)
	assert_true(fired_bad == 0, "malformed simultaneous list fires nothing")
	expect_errors_containing("must be a Dictionary", 2, "non-dict entries fail loud")
	expect_errors_containing("out of range", 1, "bad pattern_source entry fails loud")
	expect_errors_containing("must be an int", 1, "bad amount entry fails loud")
	expect_errors_containing("must be a BulletVolleyData2D", 1, "null data entry fails loud")
	expect_errors_containing("no spawn_data assigned", 3, "entry shots fail loud without data")
	assert_true(bare.get_pattern_source() == src0, "pattern_source restored after bad list")
	assert_true(bare.get_helper_bullets_amount() == amt0, "bullets_amount restored after bad list")
	assert_true(bare.get_spawn_data() == null or not bare.get_spawn_data().is_valid(), "spawn_data untouched by bad list")
	var seq: int = bare.spawn_pattern_list([{"helper_bullets_amount": 3}], false, 0.01)
	assert_true(seq == 1 and bare.is_pattern_list_active(), "sequential list queues")
	bare.stop_pattern_list()
	assert_true(not bare.is_pattern_list_active(), "stop_pattern_list disarms")
	bare.pattern_source = 33
	expect_errors_containing("invalid pattern_source", 1, "source 33 fails loud")
	assert_true(bare.get_pattern_source() == src0, "pattern_source 33 rejected")
	bare.pattern_source = -1
	expect_errors_containing("invalid pattern_source", 1, "source -1 fails loud")
	assert_true(bare.get_pattern_source() == src0, "pattern_source -1 rejected")

func test_preview_cap_custom_line() -> void:
	var spawner := make_spawner(_make_data(), BulletSpawner2D.PATTERN_FROM_SELF, 5)
	spawner.show_preview_during_runtime = true
	spawner.show_pattern_preview = true
	spawner.helper_bullets_amount = 5
	# (Every source's count/empty-input contract: test_spawner_patterns and
	# test_spawner_pattern_counts.)
	# Cap boundary: 10000 accepted and collected, 10001 rejected.
	spawner.helper_bullets_amount = 10000
	assert_true(spawner.get_helper_bullets_amount() == 10000, "amount cap accepts 10000")
	spawner.pattern_source = 3
	assert_true(spawner.collect_spawn_transforms().size() == 10000, "cap volley collects 10000")
	spawner.helper_bullets_amount = 10001
	expect_errors_containing("must be <= 10000", 1, "amount cap fails loud")
	assert_true(spawner.get_helper_bullets_amount() == 10000, "amount 10001 rejected")
	spawner.helper_bullets_amount = 5
	# Custom compose + order ops (spawner at origin identity).
	spawner.pattern_source = 24
	spawner.set_helper_custom_transforms([Transform2D(0.0, Vector2(10, 0)), Transform2D(0.0, Vector2(0, 20)), Transform2D(0.0, Vector2(-5, -5))])
	var cust: Array = spawner.collect_spawn_transforms()
	assert_eq(cust.size(), 3, "custom collects all")
	assert_almost_eq((cust[0] as Transform2D).origin, Vector2(10, 0), Vector2(0.01, 0.01), "custom composes marker")
	# (Reverse / slot-offset order ops: test_spawner_patterns.)
	# Regression (COW aliasing): rebuilds and mode switches must never wipe
	# placed transforms — the snapshot keeps a private copy now.
	assert_true(spawner.get_helper_custom_transforms().size() == 3, "custom array survives rebuilds")
	spawner.pattern_source = 6
	spawner.pattern_source = 24
	assert_true(spawner.get_helper_custom_transforms().size() == 3, "custom array survives mode switch")
	assert_true(spawner.collect_spawn_transforms().size() == 3, "custom still collects after switch")
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
	assert_true(line_ok, "line centers row")
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
	assert_true(rev_ok, "line reverse flips row")
	spawner.set_helper_line_reverse(false)

func test_homing_resolve() -> void:
	var spawner := make_spawner(_make_data(), BulletSpawner2D.PATTERN_FROM_SELF, 5)
	spawner.set_homing_enabled(true)
	spawner.set_homing_max_targets(5)
	spawner.set_homing_target_source(0)
	spawner.set_homing_node_group("no_such_group_xyz")
	assert_true(spawner.resolve_homing_targets(true).is_empty(), "empty group resolves empty")
	var d1 := Node2D.new()
	d1.name = "DummyA"
	d1.position = Vector2(100, 0)
	d1.add_to_group("test_targets")
	add(d1)
	var d2 := Node2D.new()
	d2.name = "DummyB"
	d2.position = Vector2(200, 0)
	d2.add_to_group("test_targets")
	add(d2)
	spawner.set_homing_node_group("test_targets")
	spawner.set_homing_max_targets(2)
	assert_eq(spawner.resolve_homing_targets(true, false), [d1, d2], "group source resolves both, nearest first (per-mode semantics: test_spawner_homing_selection)")
	spawner.set_homing_target_source(1)
	assert_true(spawner.resolve_homing_targets(true).is_empty(), "mouse resolves empty array")
	spawner.set_homing_target_source(2)
	spawner.set_homing_global_position(Vector2(50, 50))
	var gp: Array = spawner.resolve_homing_targets(true)
	assert_true(gp.size() == 1 and (gp[0] as Vector2) == Vector2(50, 50), "global position resolves")
	spawner.set_homing_global_position(Vector2(NAN, NAN))
	expect_errors_containing("must be finite", 1, "NAN global fails loud")
	assert_true(spawner.get_homing_global_position() == Vector2(50, 50), "NAN global keeps old value")
	assert_true(spawner.resolve_homing_targets(true).size() == 1, "kept global still resolves")
	spawner.set_homing_target_source(3)
	spawner.set_homing_target_path(NodePath("nope"))
	assert_true(spawner.resolve_homing_targets(true).is_empty(), "dead path resolves empty")
	spawner.set_homing_target_path(d1.get_path())
	assert_true(spawner.resolve_homing_targets(true).size() == 1, "live path resolves")
	spawner.set_homing_target_source(4)
	spawner.set_homing_node_name("")
	assert_true(spawner.resolve_homing_targets(true).is_empty(), "empty name resolves empty")
	spawner.set_homing_node_name("Dummy")
	spawner.set_homing_node_name_match_mode(1)
	assert_true(spawner.resolve_homing_targets(true).size() == 2, "contains name resolves both")
	spawner.set_homing_target_source(5)
	spawner.set_homing_children_parent_path(NodePath("."))
	# Regression: the runtime preview holder is a Node2D child of the
	# spawner and must never resolve as a homing target.
	assert_true(spawner.resolve_homing_targets(true).is_empty(), "childless parent resolves empty")
	assert_true(spawner.retarget_live_volleys() == 0, "retarget with no volleys is 0")
	spawner.set_homing_enabled(false)

func test_live_shoot() -> void:
	var spawner := make_spawner(_make_data(), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 5)
	spawner.pattern_source = 3
	# Homing on so the volley is tracked (plain volleys stay untracked by
	# design: only homing volleys need retargeting).
	var t1 := Node2D.new()
	t1.position = Vector2(100, 0)
	t1.add_to_group("test_targets")
	add(t1)
	var t2 := Node2D.new()
	t2.position = Vector2(200, 0)
	t2.add_to_group("test_targets")
	add(t2)
	spawner.set_homing_enabled(true)
	spawner.set_homing_target_source(0)
	spawner.set_homing_node_group("test_targets")
	assert_true(spawner.shoot_once(), "homing shot fires")
	assert_eq(spawner.get_volleys_fired(), 1, "fired volley counted")
	assert_eq(spawner.get_live_volley_count(), 1, "fired volley tracked")
	assert_eq(spawner.get_active_live_bullet_count(), spawner.helper_bullets_amount, "live bullets census")
	spawner.clear_live_volleys()
	assert_eq(spawner.get_live_volley_count(), 0, "clear forgets volleys")
	factory.free_active_bullets()
	assert_eq(spawner.get_active_live_bullet_count(), 0, "free_active drains census")

func test_factory_smoke() -> void:
	factory.reset()
	factory.free_active_bullets()
	factory.free_disabled_bullets()
	factory.free_bullets_pool()
	assert_true(factory.debug_get_total_bullets_amount() == 0, "empty factory totals 0")
	assert_true(factory.debug_get_active_bullets_amount() == 0, "empty factory actives 0")
	assert_true(factory.debug_get_bullets_pool_amount() == 0, "empty factory pool 0")
	factory.populate_bullets_pool(null, null, 0)
	factory.populate_bullets_pool(null, _make_data(), 3)
	expect_errors_containing("explicit VolleyPoolKey2D", 2, "null-key populate fails loud")
	assert_true(factory.debug_get_bullets_pool_amount() == 0, "null-key populate creates nothing")
