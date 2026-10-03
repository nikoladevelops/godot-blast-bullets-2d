extends BlastTest
## Homing target detection, one section per homing_target_source: what each
## source finds, what it must never pick (the spawner itself, its markers,
## nodes under the factory, nodes already queued for deletion, non-Node2Ds),
## and how failures are reported: a shot that finds nothing still fires as a
## plain volley, emits homing_targets_resolved([]), and warns ONCE per homing
## configuration (changing a homing target setting re-arms the warning; a
## group emptying again during play does not).


func _spawner(source: int) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(2, 50.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.set_homing_enabled(true)
	sp.set_homing_retarget_mode(BulletSpawner2D.HOMING_RETARGET_OFF) # no background passes
	sp.set_homing_target_source(source)
	watch_signals(sp)
	return sp


func _node(node_name: String, pos: Vector2, groups: Array = []) -> Node2D:
	var n := Node2D.new()
	n.name = node_name
	n.position = pos
	add(n)
	for g in groups:
		n.add_to_group(g)
	return n


## Counts (and claims) push warnings containing `text` since the last claim.
func _warnings(text: String) -> int:
	var n := 0
	for err in get_errors():
		if not err.handled and err.is_push_warning() and err.contains_text(text):
			err.handled = true
			n += 1
	return n


func _resolved(sp: BulletSpawner2D, i: int) -> Array:
	return get_signal_parameters(sp, "homing_targets_resolved", i)[1]


# --- Node Group ---------------------------------------------------------------

func test_group_skips_non_node2d_members_and_applies_the_filter() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
	sp.set_homing_node_group(&"foes")
	sp.set_homing_max_targets(10)
	sp.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_FIRST)
	var plain := Node.new()
	add(plain)
	plain.add_to_group("foes")
	var a := _node("A", Vector2(100, 0), ["foes"])
	var b := _node("B", Vector2(200, 0), ["foes", "elite"])
	assert_eq(sp.resolve_homing_targets(true, false), [a, b], "only Node2D members are targets")
	sp.set_homing_filter_group(&"elite")
	assert_eq(sp.resolve_homing_targets(true, false), [b], "the filter group keeps its members only")


func test_group_never_targets_the_spawner_or_dying_nodes() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
	sp.set_homing_node_group(&"foes")
	sp.set_homing_max_targets(10)
	sp.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_FIRST)
	sp.add_to_group("foes") # e.g. an enemy turret in the enemies group
	var a := _node("A", Vector2(100, 0), ["foes"])
	var dying := _node("Dying", Vector2(150, 0), ["foes"])
	dying.queue_free() # dies at the end of this frame
	assert_eq(sp.resolve_homing_targets(true, false), [a], "never the spawner, never a node queued for deletion")


func test_empty_group_fires_plain_volleys_and_warns_once_per_configuration() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
	sp.set_homing_node_group(&"nobody")
	for i in 3:
		assert_true(sp.shoot_once(), "plain volley %d still fires" % i)
	assert_eq(_warnings("no live Node2D found in group 'nobody', volley flies without homing"), 1, "one warning for three volleys")
	assert_signal_emit_count(sp, "homing_targets_resolved", 3, "every volley reports its resolution")
	assert_eq(_resolved(sp, 0), [], "with no targets")
	var v: BulletVolley2D = sp.get_live_volleys()[0]
	assert_false(v.shared_homing_deque_check_has_homing_targets(), "the volley flies plain")
	# Gameplay emptying the group again (a wave cleared) stays quiet.
	var foe := _node("Foe", Vector2(200, 0), ["nobody"])
	assert_true(sp.shoot_once(), "a target appears")
	foe.free()
	assert_true(sp.shoot_once(), "and is gone again")
	assert_eq(_warnings("no live Node2D found in group"), 0, "no repeat warning within one configuration")
	sp.set_homing_node_group(&"still_nobody")
	assert_true(sp.shoot_once(), "new configuration")
	assert_eq(_warnings("no live Node2D found in group 'still_nobody'"), 1, "a homing setting change re-arms the warning")


func test_range_cull_to_empty_warns_once() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
	sp.set_homing_node_group(&"far")
	_node("Far", Vector2(900, 0), ["far"])
	sp.set_homing_max_detection_range(100.0)
	assert_true(sp.shoot_once(), "plain volley")
	assert_true(sp.shoot_once(), "plain volley")
	assert_eq(_warnings("no live Node2D found in group 'far'"), 1, "out of range counts as none, warned once")


# --- Node Path ----------------------------------------------------------------

func test_node_path_failures_warn_once_and_fire_plain() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	assert_true(sp.shoot_once(), "an empty path still fires")
	assert_true(sp.shoot_once(), "twice")
	assert_eq(_warnings("homing_target_path does not point at a live Node2D"), 1, "one warning")
	var target := _node("T", Vector2(300, 0))
	sp.set_homing_target_path(sp.get_path_to(target))
	assert_true(sp.shoot_once(), "live target")
	assert_eq(_resolved(sp, 2), [target], "resolved to the node")
	target.free()
	assert_true(sp.shoot_once(), "the target was freed: plain volley, no crash")
	assert_true(sp.shoot_once(), "again")
	assert_eq(_resolved(sp, 3), [], "nothing resolved")
	assert_eq(_warnings("homing_target_path does not point at a live Node2D"), 1, "warned once for the freed target")


func test_node_path_target_out_of_tree_or_dying_is_not_chased() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	var target := _node("T", Vector2(300, 0))
	sp.set_homing_target_path(sp.get_path_to(target))
	var parent := target.get_parent()
	parent.remove_child(target)
	assert_eq(sp.resolve_homing_targets(true, false), [], "a node outside the tree is not a target")
	parent.add_child(target)
	assert_eq(sp.resolve_homing_targets(true, false), [target], "back in the tree")
	target.queue_free()
	assert_eq(sp.resolve_homing_targets(true, false), [], "a node queued for deletion is not a target")


func test_node_path_filter_mismatch_warns() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	var target := _node("T", Vector2(300, 0))
	sp.set_homing_target_path(sp.get_path_to(target))
	sp.set_homing_filter_group(&"elite")
	assert_true(sp.shoot_once(), "plain volley")
	assert_eq(_warnings("homing_target_path node is not in homing_filter_group"), 1, "explains why")
	target.add_to_group("elite")
	assert_eq(sp.resolve_homing_targets(true, false), [target], "accepted once in the group")


func test_node_path_rejects_a_non_node2d_in_the_tree() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	var good := _node("Good", Vector2(300, 0))
	var plain := Node.new()
	add(plain)
	sp.set_homing_target_path(sp.get_path_to(good))
	sp.set_homing_target_path(sp.get_path_to(plain))
	expect_error_sequence(["homing_target_path must point to a Node2D, keeping the old value"])
	assert_eq(sp.get_homing_target_path(), sp.get_path_to(good), "old path kept")
	sp.set_homing_target_path(NodePath("NotThereYet"))
	expect_no_errors("an unresolved path is accepted (the node may be added later)")
	var detached := BulletSpawner2D.new()
	detached.set_homing_target_path(NodePath("../Anything"))
	expect_no_errors("out of the tree any path is accepted (scene loading)")
	detached.free()


# --- Global Position / Mouse ---------------------------------------------------

func test_global_position_source_queues_the_point() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	sp.set_homing_global_position(Vector2(320, -40))
	assert_eq(sp.resolve_homing_targets(true, false), [Vector2(320, -40)], "exactly the point")
	assert_true(sp.shoot_once(), "shot")
	assert_eq(_resolved(sp, 0), [Vector2(320, -40)], "signal carries the point")
	var v: BulletVolley2D = sp.get_live_volleys()[0]
	assert_eq(v.shared_homing_deque_check_current_target_type(), BulletVolley2D.GlobalPositionTarget, "queued as a position target")


func test_mouse_source_queues_the_cursor_in_both_modes() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_MOUSE)
	assert_eq(sp.resolve_homing_targets(false, false), [], "the mouse never resolves to nodes")
	assert_eq(_warnings("flies without homing"), 0, "and that is not a failure")
	assert_true(sp.shoot_once(), "shared-mode shot")
	assert_eq(_resolved(sp, 0), [], "nothing resolved")
	var shared: BulletVolley2D = sp.get_live_volleys()[0]
	assert_eq(shared.shared_homing_deque_check_current_target_type(), BulletVolley2D.MousePositionTarget, "the shared queue chases the cursor")
	sp.set_homing_mode(BulletSpawner2D.HOMING_PER_BULLET)
	assert_true(sp.shoot_once(), "per-bullet shot")
	var per: BulletVolley2D = sp.get_live_volleys()[1]
	for i in per.get_amount_bullets():
		assert_eq(per.bullet_homing_check_current_target_type(i), BulletVolley2D.MousePositionTarget, "bullet %d chases the cursor" % i)
	sp.set_homing_fire_arc_deg(10.0)
	assert_true(sp.shoot_once(), "the fire arc never applies to the mouse")


# --- Node Name ----------------------------------------------------------------

func test_name_source_failures_and_exclusions() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_NODE_NAME)
	sp.set_homing_node_name("") # default is "Player"
	assert_true(sp.shoot_once(), "an empty name still fires")
	assert_eq(_warnings("homing_node_name is empty, volley flies without homing"), 1, "explains why")
	sp.set_homing_node_name("zzFoe")
	sp.set_homing_node_name_match_mode(BulletSpawner2D.HOMING_NAME_MATCH_CONTAINS)
	sp.set_homing_max_targets(50)
	sp.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_FIRST)
	sp.name = "zzFoeSpawner" # a broad pattern would match the spawner itself
	var marker := Node2D.new()
	marker.name = "zzFoeMarker" # ...and its own pattern markers
	sp.add_child(marker)
	var in_factory := Node2D.new()
	in_factory.name = "zzFoeInFactory" # ...and anything under the factory
	factory.add_child(in_factory)
	var dying := _node("zzFoeDying", Vector2(150, 0))
	dying.queue_free()
	var foe := _node("zzFoeReal", Vector2(200, 0))
	assert_eq(sp.resolve_homing_targets(true, false), [foe], "only the real target")
	foe.free()
	assert_true(sp.shoot_once(), "no match: plain volley")
	assert_eq(_warnings("no live Node2D named like 'zzFoe' found"), 1, "warned once")
	in_factory.free()


# --- Node Children ------------------------------------------------------------

func test_children_source_failures_and_walk() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_NODE_CHILDREN)
	assert_true(sp.shoot_once(), "no parent path: plain volley")
	assert_eq(_warnings("Node Children source needs homing_children_parent_path"), 1, "explains why")
	sp.set_homing_children_parent_path(NodePath("Missing"))
	assert_true(sp.shoot_once(), "missing parent: plain volley")
	assert_eq(_warnings("homing_children_parent_path does not point at a live node"), 1, "explains why")
	var parent := Node2D.new()
	add(parent)
	var holder := Node.new() # not a Node2D: never a target, but may hold some
	parent.add_child(holder)
	var deep := Node2D.new()
	holder.add_child(deep)
	sp.set_homing_children_parent_path(sp.get_path_to(parent))
	sp.set_homing_max_targets(10)
	sp.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_FIRST)
	sp.set_homing_children_recursive(false)
	assert_eq(sp.resolve_homing_targets(true, false), [], "the only direct child is not a Node2D")
	assert_true(sp.shoot_once(), "plain volley")
	assert_eq(_warnings("parent has no live Node2D children to chase"), 1, "explains why")
	sp.set_homing_children_recursive(true)
	assert_eq(sp.resolve_homing_targets(true, false), [deep], "recursion walks through the plain Node")


func test_children_of_the_spawners_own_parent_exclude_the_spawner() -> void:
	var sp := _spawner(BulletSpawner2D.HOMING_SOURCE_NODE_CHILDREN)
	var parent := Node2D.new()
	add(parent)
	sp.get_parent().remove_child(sp)
	parent.add_child(sp)
	var foe := Node2D.new()
	parent.add_child(foe)
	sp.set_homing_children_parent_path(sp.get_path_to(parent))
	sp.set_homing_max_targets(10)
	sp.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_FIRST)
	assert_eq(sp.resolve_homing_targets(true, false), [foe], "the spawner never chases itself")
