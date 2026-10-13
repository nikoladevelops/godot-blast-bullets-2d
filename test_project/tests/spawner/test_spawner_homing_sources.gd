extends BlastTest
## One contract, checked for EVERY homing_target_source on the same scene:
## a spawner at the origin firing one bullet along +X (snap steering), the
## source's target and a nearer decoy no source matches. Every node source
## (Node Group, Node Path, Node Name, Node Children) chases exactly its node
## and steers at it, picks up a node that appears after the shot on the
## next retarget pass (an empty shot warns once with its own text), applies
## homing_filter_group, follows setting edits on retarget, and trims a
## freed target from the flying volley. Detection range culls the
## multi-target sources (never Node Path), every selection mode picks the
## same targets whichever multi-target source found them, Node Path may name
## the spawner itself (bullets fly back to it) or its children, Global
## Position is a snapshot per volley that a retarget pass refreshes, the
## Mouse is chased live, every source keeps its own settings across source
## switches and every target setting survives a scene round trip.
##
## Homing target detection, one section per homing_target_source: what each
## source finds, what it must never pick (the spawner itself, its markers,
## a bullet factory or nodes under it, nodes already queued for deletion,
## non-Node2Ds),
## and how failures are reported: a shot that finds nothing still fires as a
## plain volley, emits homing_targets_resolved([]), and warns ONCE per homing
## configuration (changing a homing target setting re-arms the warning; a
## group emptying again during play does not). Nothing is guessed: no
## default group or name, an empty one is named in a warning and in the
## editor's setup warnings (one per source with an empty setting).

const GROUP := BulletSpawner2D.HOMING_SOURCE_NODE_GROUP
const MOUSE := BulletSpawner2D.HOMING_SOURCE_MOUSE
const GLOBAL := BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION
const PATH := BulletSpawner2D.HOMING_SOURCE_NODE_PATH
const NAME := BulletSpawner2D.HOMING_SOURCE_NODE_NAME
const CHILDREN := BulletSpawner2D.HOMING_SOURCE_NODE_CHILDREN
const SOURCES := [GROUP, PATH, NAME, CHILDREN]
const MULTI_SOURCES := [GROUP, NAME, CHILDREN]


## The warning an empty shot prints once, per node source.
const EMPTY_WARNINGS := {
	GROUP: "BulletSpawner2D::resolve_homing_targets: no live Node2D found in group 'src', volley flies without homing.",
	PATH: "BulletSpawner2D::resolve_homing_targets: homing_target_path does not point at a live Node2D, volley flies without homing.",
	NAME: "BulletSpawner2D::resolve_homing_targets: no live Node2D named like 'SrcTarget' found, volley flies without homing.",
	CHILDREN: "BulletSpawner2D::resolve_homing_targets: parent has no live Node2D children to chase, volley flies without homing.",
}


## The Node Children source's parent, at the origin.
var parent: Node2D


func before_each() -> void:
	await super()
	parent = Node2D.new()
	parent.name = "SrcParent"
	add(parent)


## A spawner at the origin firing one bullet along +X at 100 px/s, homing
## on `source` with snap steering, retargeting off.
func _spawner(source: int) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(1, 100.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.homing_enabled = true
	sp.homing_retarget_mode = BulletSpawner2D.HOMING_RETARGET_OFF
	sp.homing_smoothing = 0.0
	sp.homing_target_source = source
	match source:
		GROUP:
			sp.homing_node_group = &"src"
		PATH:
			sp.homing_target_path = NodePath("../SrcTarget") # not there yet: accepted
		NAME:
			sp.homing_node_name = "SrcTarget"
		CHILDREN:
			sp.homing_children_parent_path = NodePath("../SrcParent")
	watch_signals(sp)
	return sp


## A node `source` finds, at global `pos` (Node Path finds index 0 only).
func _target(source: int, pos: Vector2, index := 0) -> Node2D:
	var n := Node2D.new()
	n.position = pos
	if source == CHILDREN:
		parent.add_child(n)
	else:
		add(n)
	n.name = "SrcTarget" if index == 0 else "SrcTarget%d" % index
	if source == GROUP:
		n.add_to_group(&"src")
	return n


## A node no source matches.
func _decoy(pos: Vector2) -> Node2D:
	var n := Node2D.new()
	n.name = "Decoy"
	n.position = pos
	add(n)
	return n


func _shoot(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fired")
	return got[0] if not got.is_empty() else null


## Whether bullet 0 of `v` flies straight at `point` (within ~2.5 degrees).
func _flies_at(v: BulletVolley2D, point: Vector2) -> bool:
	var velocity := v.get_bullet_velocity(0)
	var to_point := point - v.get_bullet_transform(0).origin
	return velocity.length() > 0.0 and velocity.normalized().dot(to_point.normalized()) > 0.999


func test_a_shot_chases_its_target_and_steers_at_it(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	_decoy(Vector2(0, 20)) # nearer, matched by no source
	var t := _target(source, Vector2(100, 100))
	var v := _shoot(sp)
	assert_eq(get_signal_parameters(sp, "homing_targets_resolved", 0)[1], [t], "source %d: resolves exactly its node" % source)
	assert_eq(v.shared_homing_deque_get_current_homing_target(), t, "source %d: the volley chases it" % source)
	step_factory(2)
	assert_true(_flies_at(v, t.global_position), "source %d: the bullet turned toward it" % source)


func test_a_node_that_appears_later_is_chased_after_the_next_retarget_pass(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	sp.homing_retarget_mode = BulletSpawner2D.HOMING_RETARGET_ON_INTERVAL
	sp.homing_retarget_interval_sec = 0.1
	var v := _shoot(sp)
	expect_warning_sequence([EMPTY_WARNINGS[source]])
	assert_false(v.shared_homing_deque_check_has_homing_targets(), "source %d: nothing to chase at the shot" % source)
	var t := _target(source, Vector2(0, 300))
	for i in 30: # the pass runs every 0.1 s (6 frames)
		await idle(1)
		if v.shared_homing_deque_check_has_homing_targets():
			break
	assert_eq(v.shared_homing_deque_get_current_homing_target(), t, "source %d: a retarget pass found it" % source)
	assert_gt(get_signal_emit_count(sp, "retarget_applied"), 0, "source %d: and reported it" % source)


func test_the_filter_group_applies(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	sp.homing_filter_group = &"allowed"
	var t := _target(source, Vector2(100, 0))
	assert_eq(sp.resolve_homing_targets(true, false), [], "source %d: a node outside the filter group is no target" % source)
	t.add_to_group(&"allowed")
	assert_eq(sp.resolve_homing_targets(true, false), [t], "source %d: in the filter group it is" % source)


func test_detection_range_culls_the_multi_target_sources_only(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	sp.homing_max_detection_range = 50.0
	var t := _target(source, Vector2(100, 0))
	var expected := [t] if source == PATH else []
	assert_eq(sp.resolve_homing_targets(true, false), expected, "source %d: 100 px away, range 50 (Node Path: no range)" % source)
	sp.homing_max_detection_range = 150.0
	assert_eq(sp.resolve_homing_targets(true, false), [t], "source %d: within range" % source)


func test_every_selection_mode_picks_the_same_for_every_multi_source() -> void:
	# One node set every multi-target source finds in the same tree order:
	# children of SrcParent, named SrcTarget<i>, in the group.
	var nodes: Array = []
	for i in 5:
		var n := _target(CHILDREN, Vector2(40 * (5 - i), 10 * i), i + 1)
		n.add_to_group(&"src")
		nodes.append(n)
	var modes := [
		[BulletSpawner2D.HOMING_SELECT_NEAREST, 3],
		[BulletSpawner2D.HOMING_SELECT_FIRST, 3],
		[BulletSpawner2D.HOMING_SELECT_RANDOM, 3],
		[BulletSpawner2D.HOMING_SELECT_ROUND_ROBIN, 2],
		[BulletSpawner2D.HOMING_SELECT_DISTRIBUTE, 2],
	]
	for m in modes:
		var picks := {}
		for source in MULTI_SOURCES:
			var sp := _spawner(source)
			sp.homing_target_selection = m[0]
			sp.homing_max_targets = m[1]
			sp.homing_random_seed = 7
			# Three consecutive resolves: round robin advances, random rolls on.
			var rounds: Array = []
			for r in 3:
				rounds.append(sp.resolve_homing_targets(true, true))
			picks[source] = rounds
		assert_eq(picks[NAME], picks[GROUP], "mode %d: Node Name picks what Node Group picks" % m[0])
		assert_eq(picks[CHILDREN], picks[GROUP], "mode %d: Node Children picks what Node Group picks" % m[0])
		assert_eq((picks[GROUP][0] as Array).size(), m[1], "mode %d: max_targets bounds the pick" % m[0])
	var sp := _spawner(GROUP)
	sp.homing_target_selection = BulletSpawner2D.HOMING_SELECT_NEAREST
	sp.homing_max_targets = 5
	assert_eq(sp.resolve_homing_targets(true, false), [nodes[4], nodes[3], nodes[2], nodes[1], nodes[0]], "nearest first")


func test_a_freed_target_is_trimmed_from_the_flying_volley(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	var t := _target(source, Vector2(300, 0))
	var v := _shoot(sp)
	assert_eq(v.shared_homing_deque_get_current_homing_target(), t, "source %d: chasing" % source)
	t.free()
	step_factory()
	assert_false(v.shared_homing_deque_check_has_homing_targets(), "source %d: the freed target was trimmed" % source)
	assert_true(H.finite_volley([v.get_bullet_transform(0)]), "source %d: the bullet flies on, finite" % source)


func test_retargeting_follows_setting_edits(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	var first := _target(source, Vector2(300, 0))
	var elsewhere := Node2D.new()
	elsewhere.name = "Elsewhere"
	add(elsewhere)
	var other := Node2D.new()
	other.position = Vector2(0, -300)
	elsewhere.add_child(other)
	other.name = "Other"
	var v := _shoot(sp)
	assert_eq(v.shared_homing_deque_get_current_homing_target(), first, "source %d: the first setting's node" % source)
	match source:
		GROUP:
			other.add_to_group(&"other")
			sp.homing_node_group = &"other"
		PATH:
			sp.homing_target_path = sp.get_path_to(other)
		NAME:
			sp.homing_node_name = "Other"
		CHILDREN:
			sp.homing_children_parent_path = NodePath("../Elsewhere")
	assert_eq(sp.retarget_live_volleys(), 1, "source %d: one volley re-aimed" % source)
	assert_eq(v.shared_homing_deque_get_current_homing_target(), other, "source %d: it chases the new setting's node" % source)


func test_the_path_may_name_the_spawner_itself_or_its_children() -> void:
	var sp := _spawner(PATH)
	var core := Node2D.new()
	core.name = "Core"
	core.position = Vector2(0, 50)
	sp.add_child(core)
	sp.homing_target_path = NodePath("Core")
	assert_eq(sp.resolve_homing_targets(true, false), [core], "its own child")
	sp.homing_target_path = NodePath(".")
	assert_eq(sp.resolve_homing_targets(true, false), [sp], "itself: bullets fly back to the spawner")
	sp.homing_delay_sec = 0.25 # fly out first
	sp.homing_distance_before_reached = 0.0
	var v := _shoot(sp)
	step_factory(15) # 0.25 s straight out
	var out := v.get_bullet_transform(0).origin.length()
	assert_gt(out, 20.0, "flew out")
	step_factory(10)
	assert_lt(v.get_bullet_transform(0).origin.length(), out, "then came back toward the spawner")


func test_the_global_position_is_a_snapshot_per_volley_that_retargets_refresh() -> void:
	var sp := _spawner(GLOBAL)
	sp.homing_global_position = Vector2(0, 200)
	var v := _shoot(sp)
	assert_eq(v.shared_homing_deque_check_current_target_type(), BulletVolley2D.GlobalPositionTarget, "a position target")
	assert_eq(v.shared_homing_deque_get_current_homing_target(), Vector2(0, 200), "the point")
	step_factory(2)
	assert_true(_flies_at(v, Vector2(0, 200)), "steers at it")
	sp.homing_global_position = Vector2(0, -200)
	step_factory(2)
	assert_true(_flies_at(v, Vector2(0, 200)), "the flying volley keeps its snapshot")
	assert_eq(sp.retarget_live_volleys(), 1, "a retarget pass")
	assert_eq(v.shared_homing_deque_get_current_homing_target(), Vector2(0, -200), "refreshes it")
	step_factory(2)
	assert_true(_flies_at(v, Vector2(0, -200)), "and the bullet turns")


func test_the_mouse_is_chased_live() -> void:
	var sp := _spawner(MOUSE)
	mouse_to(Vector2(0, 300))
	var v := _shoot(sp)
	assert_eq(v.shared_homing_deque_check_current_target_type(), BulletVolley2D.MousePositionTarget, "a mouse target")
	step_factory(2)
	assert_true(_flies_at(v, Vector2(0, 300)), "steers at the cursor")
	mouse_to(Vector2(-300, 0))
	step_factory(2)
	assert_true(_flies_at(v, Vector2(-300, 0)), "the cursor moved: the bullet follows it")


func test_every_source_keeps_its_own_settings_across_switches() -> void:
	var sp := _spawner(GROUP)
	sp.homing_target_path = NodePath("../SrcTarget")
	sp.homing_node_name = "SrcTarget"
	sp.homing_children_parent_path = NodePath("../SrcParent")
	sp.homing_global_position = Vector2(7, 8)
	sp.homing_max_targets = 10
	sp.homing_target_selection = BulletSpawner2D.HOMING_SELECT_FIRST
	var group_target := _target(GROUP, Vector2(100, 0), 1)
	var path_target := _target(PATH, Vector2(200, 0))
	var child := _target(CHILDREN, Vector2(300, 0), 2)
	var expected := {
		GROUP: [group_target], PATH: [path_target], NAME: [child, group_target, path_target],
		CHILDREN: [child], GLOBAL: [Vector2(7, 8)], MOUSE: [],
	}
	for round in 2:
		for source in expected:
			sp.homing_target_source = source
			assert_eq(sp.resolve_homing_targets(true, false), expected[source], "round %d, source %d: only the active source decides" % [round, source])


func test_every_homing_target_setting_survives_a_scene_round_trip() -> void:
	var values := {
		"homing_target_source": CHILDREN,
		"homing_node_group": &"saved_group",
		"homing_filter_group": &"saved_filter",
		"homing_target_selection": BulletSpawner2D.HOMING_SELECT_ROUND_ROBIN,
		"homing_random_seed": 42,
		"homing_max_targets": 3,
		"homing_max_detection_range": 250.0,
		"homing_global_position": Vector2(12, -34),
		"homing_target_path": NodePath("../Saved"),
		"homing_node_name": "SavedName",
		"homing_node_name_match_mode": BulletSpawner2D.HOMING_NAME_MATCH_STARTS_WITH,
		"homing_node_name_case_sensitive": true,
		"homing_children_parent_path": NodePath("../SavedParent"),
		"homing_children_recursive": true,
	}
	var src := BulletSpawner2D.new()
	src.set_shooting_enabled(false)
	src.homing_enabled = true
	for key in values:
		assert_true(key in src, key + " exists")
		src.set(key, values[key])
	var scene := PackedScene.new()
	assert_eq(scene.pack(src), OK, "packs")
	src.free()
	var back := scene.instantiate() as BulletSpawner2D
	for key in values:
		assert_eq(back.get(key), values[key], key + " survives")
	back.free()


func _tracking_spawner(source: int) -> BulletSpawner2D:
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
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
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
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
	sp.set_homing_node_group(&"foes")
	sp.set_homing_max_targets(10)
	sp.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_FIRST)
	sp.add_to_group("foes") # e.g. an enemy turret in the enemies group
	var a := _node("A", Vector2(100, 0), ["foes"])
	var dying := _node("Dying", Vector2(150, 0), ["foes"])
	dying.queue_free() # dies at the end of this frame
	assert_eq(sp.resolve_homing_targets(true, false), [a], "never the spawner, never a node queued for deletion")


func test_empty_group_fires_plain_volleys_and_warns_once_per_configuration() -> void:
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
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
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
	sp.set_homing_node_group(&"far")
	_node("Far", Vector2(900, 0), ["far"])
	sp.set_homing_max_detection_range(100.0)
	assert_true(sp.shoot_once(), "plain volley")
	assert_true(sp.shoot_once(), "plain volley")
	assert_eq(_warnings("no live Node2D found in group 'far'"), 1, "out of range counts as none, warned once")


# --- Node Path ----------------------------------------------------------------


func test_node_path_failures_warn_once_and_fire_plain() -> void:
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
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
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
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
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	var target := _node("T", Vector2(300, 0))
	sp.set_homing_target_path(sp.get_path_to(target))
	sp.set_homing_filter_group(&"elite")
	assert_true(sp.shoot_once(), "plain volley")
	assert_eq(_warnings("homing_target_path node is not in homing_filter_group"), 1, "explains why")
	target.add_to_group("elite")
	assert_eq(sp.resolve_homing_targets(true, false), [target], "accepted once in the group")


func test_node_path_rejects_a_non_node2d_in_the_tree() -> void:
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
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
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	sp.set_homing_global_position(Vector2(320, -40))
	assert_eq(sp.resolve_homing_targets(true, false), [Vector2(320, -40)], "exactly the point")
	assert_true(sp.shoot_once(), "shot")
	assert_eq(_resolved(sp, 0), [Vector2(320, -40)], "signal carries the point")
	var v: BulletVolley2D = sp.get_live_volleys()[0]
	assert_eq(v.shared_homing_deque_check_current_target_type(), BulletVolley2D.GlobalPositionTarget, "queued as a position target")


func test_mouse_source_queues_the_cursor_in_both_modes() -> void:
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_MOUSE)
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
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_NODE_NAME)
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
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_NODE_CHILDREN)
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
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_NODE_CHILDREN)
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


func test_children_never_pick_a_bullet_factory_or_its_nodes() -> void:
	var sp := _tracking_spawner(BulletSpawner2D.HOMING_SOURCE_NODE_CHILDREN)
	var in_factory := Node2D.new() # the factory's containers hold every bullet node
	factory.add_child(in_factory)
	var foe := _node("zzSiblingFoe", Vector2(200, 0))
	sp.set_homing_children_parent_path(sp.get_path_to(factory.get_parent()))
	sp.set_homing_children_recursive(true)
	sp.set_homing_max_targets(10000)
	sp.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_FIRST)
	var found := sp.resolve_homing_targets(true, false)
	assert_false(found.has(factory), "never the factory itself")
	assert_false(found.has(in_factory), "nor anything inside it")
	assert_true(found.has(foe), "its siblings count")
	in_factory.free()


# --- No default targets ---------------------------------------------------------


func test_no_default_targets_the_developer_names_them() -> void:
	var sp := make_spawner()
	assert_eq(sp.homing_node_group, &"", "no default group")
	assert_eq(sp.homing_node_name, "", "no default name")
	_node("Player", Vector2(100, 0), [&"enemies", &"player"]) # the names the old defaults used
	sp.set_homing_enabled(true)
	sp.set_homing_retarget_mode(BulletSpawner2D.HOMING_RETARGET_OFF)
	watch_signals(sp)
	assert_true(sp.shoot_once(), "an unnamed group still fires")
	assert_true(sp.shoot_once(), "twice")
	assert_eq(_warnings("BulletSpawner2D::resolve_homing_targets: homing_node_group is empty, volley flies without homing."), 1, "the empty group is named, once")
	assert_eq(get_signal_parameters(sp, "homing_targets_resolved", 0)[1], [], "nobody chased: nothing is guessed")


func test_setup_warnings_name_each_empty_homing_setting() -> void:
	var sp := make_spawner()
	assert_eq(_homing_warnings(sp), [], "homing off: nothing")
	sp.set_homing_enabled(true)
	var expected := {
		BulletSpawner2D.HOMING_SOURCE_NODE_GROUP: "homing_target_source is Node Group but homing_node_group is empty: volleys fly without homing.",
		BulletSpawner2D.HOMING_SOURCE_NODE_NAME: "homing_target_source is Node Name but homing_node_name is empty: volleys fly without homing.",
		BulletSpawner2D.HOMING_SOURCE_NODE_PATH: "homing_target_source is Node Path but homing_target_path is empty: volleys fly without homing.",
		BulletSpawner2D.HOMING_SOURCE_NODE_CHILDREN: "homing_target_source is Node Children but homing_children_parent_path is empty: volleys fly without homing.",
	}
	for source in expected:
		sp.set_homing_target_source(source)
		assert_eq(_homing_warnings(sp), [expected[source]], "source %d" % source)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_MOUSE)
	assert_eq(_homing_warnings(sp), [], "the mouse needs no setting")
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
	sp.set_homing_node_group(&"foes")
	assert_eq(_homing_warnings(sp), [], "a named group")


func _homing_warnings(sp: BulletSpawner2D) -> Array:
	var out: Array = []
	for w in sp.get_setup_warnings():
		if w.begins_with("homing_target_source"):
			out.append(w)
	return out
