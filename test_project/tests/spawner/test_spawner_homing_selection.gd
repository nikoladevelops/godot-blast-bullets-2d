extends BlastTest
## Homing target resolution: every selection mode does what its name says
## (nearest order, seeded random, tree order, round robin across shots,
## distribute deal), max_targets bounds, detection range and filter group,
## node-name match modes and case sensitivity, recursive children.

var foes: Array = []


func before_each() -> void:
	await super()
	foes.clear()
	for i in 4:
		var foe := Node2D.new()
		foe.name = "Foe%d" % i
		foe.position = Vector2(100.0 * (i + 1), 0)
		add(foe)
		foe.add_to_group("foes")
		foes.append(foe)


func _spawner(selection: int, max_targets: int = 2) -> BulletSpawner2D:
	var sp := make_spawner(H.make_directional_data(4, 50.0, 30.0))
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
	sp.set_homing_node_group(&"foes")
	sp.set_homing_target_selection(selection)
	sp.set_homing_max_targets(max_targets)
	return sp


func test_nearest_returns_closest_first() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SELECT_NEAREST)
	foes[0].position = Vector2(350, 0) # now third-closest
	assert_eq(sp.resolve_homing_targets(true, false), [foes[1], foes[2]], "two nearest, nearest first")


func test_first_returns_tree_order() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SELECT_FIRST, 3)
	assert_eq(sp.resolve_homing_targets(true, false), [foes[0], foes[1], foes[2]], "tree order")


func test_random_is_seeded_and_never_repeats_within_a_pick() -> void:
	var a := _spawner(BulletSpawner2D.HOMING_SELECT_RANDOM, 3)
	var b := _spawner(BulletSpawner2D.HOMING_SELECT_RANDOM, 3)
	a.set_homing_random_seed(7)
	b.set_homing_random_seed(7)
	for i in 4:
		var pick_a: Array = a.resolve_homing_targets(true)
		var pick_b: Array = b.resolve_homing_targets(true)
		assert_eq(pick_a, pick_b, "same seed, same picks (round %d)" % i)
		var unique := {}
		for t in pick_a:
			unique[t] = true
		assert_eq(unique.size(), 3, "no target twice in one pick")


func test_round_robin_rotates_across_shots_and_peeks_without_consuming() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SELECT_ROUND_ROBIN, 1)
	assert_eq(sp.resolve_homing_targets(true, false), [foes[0]], "peek")
	assert_eq(sp.resolve_homing_targets(true, false), [foes[0]], "peeking does not rotate")
	var chased: Array = []
	for i in 5:
		assert_true(sp.shoot_once(), "shot %d" % i)
		var v: DirectionalBullets2D = sp.get_live_volleys()[i]
		chased.append(v.shared_homing_deque_get_current_homing_target())
	assert_eq(chased, [foes[0], foes[1], foes[2], foes[3], foes[0]], "one new target per shot, wrapping")


func test_distribute_deals_the_pool_across_bullets() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SELECT_DISTRIBUTE, 2)
	sp.set_homing_mode(BulletSpawner2D.HOMING_PER_BULLET)
	assert_true(sp.shoot_once(), "shot")
	var v: DirectionalBullets2D = sp.get_live_volleys()[0]
	var chased: Array = []
	for i in v.get_amount_bullets():
		chased.append(v.bullet_get_current_homing_target(i))
	assert_eq(chased, [foes[0], foes[1], foes[0], foes[1]], "bullet i chases pool[i % pool]")


func test_max_targets_bounds_are_enforced() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SELECT_NEAREST)
	sp.set_homing_max_targets(0)
	sp.set_homing_max_targets(10001)
	expect_errors_containing("homing_max_targets", 2, "both out-of-range values rejected")
	assert_eq(sp.get_homing_max_targets(), 2, "old value kept")


func test_detection_range_and_filter_group_narrow_the_pool() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SELECT_FIRST, 4)
	sp.set_homing_max_detection_range(250.0)
	assert_eq(sp.resolve_homing_targets(true, false), [foes[0], foes[1]], "only foes within 250 px")
	sp.set_homing_max_detection_range(0.0)
	foes[2].add_to_group("elite")
	sp.set_homing_filter_group(&"elite")
	assert_eq(sp.resolve_homing_targets(true, false), [foes[2]], "the filter group keeps members only")


func test_node_name_match_modes_and_case() -> void:
	var names := ["Player", "Player2", "EnemyPlayer", "player"]
	for n in names:
		var node := Node2D.new()
		node.name = n
		add(node)
	var sp := make_spawner()
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_NAME)
	sp.set_homing_node_name("Player")
	sp.set_homing_max_targets(10)
	sp.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_FIRST)
	sp.set_homing_node_name_case_sensitive(true)
	var expected := {
		BulletSpawner2D.HOMING_NAME_MATCH_EXACT: ["Player"],
		BulletSpawner2D.HOMING_NAME_MATCH_CONTAINS: ["Player", "Player2", "EnemyPlayer"],
		BulletSpawner2D.HOMING_NAME_MATCH_STARTS_WITH: ["Player", "Player2"],
		BulletSpawner2D.HOMING_NAME_MATCH_ENDS_WITH: ["Player", "EnemyPlayer"],
	}
	for mode in expected:
		sp.set_homing_node_name_match_mode(mode)
		var got: Array = sp.resolve_homing_targets(true, false).map(func(n): return str(n.name))
		got.sort()
		var want: Array = expected[mode].duplicate()
		want.sort()
		assert_eq(got, want, "match mode %d" % mode)
	sp.set_homing_node_name_match_mode(BulletSpawner2D.HOMING_NAME_MATCH_EXACT)
	sp.set_homing_node_name_case_sensitive(false)
	var loose: Array = sp.resolve_homing_targets(true, false).map(func(n): return str(n.name))
	loose.sort()
	assert_eq(loose, ["Player", "player"], "case-insensitive exact match")


func test_children_source_recursive_switch() -> void:
	var parent := Node2D.new()
	add(parent)
	var child := Node2D.new()
	child.name = "Child"
	parent.add_child(child)
	var grandchild := Node2D.new()
	grandchild.name = "Grandchild"
	child.add_child(grandchild)
	var sp := make_spawner()
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_CHILDREN)
	sp.set_homing_children_parent_path(sp.get_path_to(parent))
	sp.set_homing_max_targets(10)
	sp.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_FIRST)
	sp.set_homing_children_recursive(false)
	assert_eq(sp.resolve_homing_targets(true, false), [child], "direct children only")
	sp.set_homing_children_recursive(true)
	assert_eq(sp.resolve_homing_targets(true, false).size(), 2, "recursive includes the grandchild")


func test_mouse_source_resolves_no_nodes() -> void:
	var sp := make_spawner()
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_MOUSE)
	assert_eq(sp.resolve_homing_targets(true, false), [], "the mouse is not a node target")
