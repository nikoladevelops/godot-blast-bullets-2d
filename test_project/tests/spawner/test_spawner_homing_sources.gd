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
