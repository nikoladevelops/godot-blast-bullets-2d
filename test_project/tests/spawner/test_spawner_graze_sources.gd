extends BlastTest
## One contract, checked for EVERY node graze_target_source (Node Group,
## Node Path, Node Name, Node Children) on the same scene: a resting bullet
## at the origin, the source's target and a nearer decoy no source matches.
## Each source grazes exactly its node and names it, picks up a node that
## appears after the shot on the next tick, holds membership between scans
## (graze_update_interval) while positions stay live, refresh_graze_targets()
## rescans at once, a freed target drops at once and a target that stops
## matching (left the group, renamed, reparented) drops at the next scan with
## its visit ending silently, a target outside the tree sits out until it is
## back, graze_filter_group applies, setting edits reach bullets in flight,
## orphaned volleys keep the source, the ring preview draws exactly the
## runtime targets, results come in tree order, paths resolve from the
## spawner, every source keeps its own settings across source switches and
## every target setting survives a scene round trip.

const GROUP := BulletSpawner2D.GRAZE_SOURCE_NODE_GROUP
const PATH := BulletSpawner2D.GRAZE_SOURCE_NODE_PATH
const NAME := BulletSpawner2D.GRAZE_SOURCE_NODE_NAME
const CHILDREN := BulletSpawner2D.GRAZE_SOURCE_NODE_CHILDREN
const SOURCES := [GROUP, PATH, NAME, CHILDREN]
## The sources that find several nodes (Node Path finds one).
const MULTI_SOURCES := [GROUP, NAME, CHILDREN]

## The Node Children source's parent, at the origin (children's local
## positions are their global positions).
var parent: Node2D


func before_each() -> void:
	await super()
	parent = Node2D.new()
	parent.name = "SrcParent"
	add(parent)


## Graze settings for `source` on `sp`: one 20 px ring, the source aimed at
## the nodes _target() builds.
func _configure(sp: BulletSpawner2D, source: int) -> BulletSpawner2D:
	sp.graze_zones = [H.make_graze_zone([20.0])]
	sp.graze_enabled = true
	sp.graze_target_source = source
	match source:
		GROUP:
			sp.graze_node_group = &"src"
		PATH:
			sp.graze_target_path = NodePath("../SrcTarget") # not there yet: accepted
		NAME:
			sp.graze_node_name = "SrcTarget"
		CHILDREN:
			sp.graze_children_parent_path = NodePath("../SrcParent")
	return sp


## A spawner at the origin firing one resting bullet (circle r4).
func _spawner(source: int) -> BulletSpawner2D:
	var d := H.make_volley_data(1, 0.0, 60.0)
	d.collision_shape = H.make_circle_shape(4.0)
	return _configure(make_spawner(d, BulletSpawner2D.PATTERN_FROM_SELF, 1), source)


## Same, not autofreed (tests that free the spawner themselves).
func _loose_spawner(source: int) -> BulletSpawner2D:
	var d := H.make_volley_data(1, 0.0, 60.0)
	d.collision_shape = H.make_circle_shape(4.0)
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.set_homing_enabled(false)
	sp.set_spawn_data(d)
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
	sp.helper_bullets_amount = 1
	add_child(sp)
	sp.set_bullet_factory(factory)
	return _configure(sp, source)


## A node `source` finds, at global `pos`. `index` tells several apart
## (Node Path finds only index 0, the node named "SrcTarget").
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


## A node no source matches (no group, another name, not under SrcParent).
func _decoy(pos: Vector2) -> Node2D:
	var n := Node2D.new()
	n.name = "Decoy"
	n.position = pos
	add(n)
	return n


## Makes `t` stop matching `source` (it stays in the tree, same place).
func _unmatch(source: int, t: Node2D) -> void:
	match source:
		GROUP:
			t.remove_from_group(&"src")
		PATH, NAME:
			t.name = "Renamed"
		CHILDREN:
			var away := Node2D.new()
			add(away)
			t.reparent(away)


func _shoot(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fired")
	return got[0] if not got.is_empty() else null


func _ids(nodes: Array) -> Array:
	var out: Array = []
	for n in nodes:
		out.append((n as Object).get_instance_id())
	return out


func test_the_source_grazes_its_target_and_names_it(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	_decoy(Vector2(5, 0)) # nearer, matched by no source
	var t := _target(source, Vector2(10, 0))
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d: one graze" % source)
	assert_eq(log[0][1], t, "source %d: named, never the nearer decoy" % source)
	assert_eq(sp.resolve_graze_targets(), [t], "source %d: resolves exactly its node" % source)


func test_a_node_that_appears_after_the_shot_grazes_next_tick(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory(2)
	assert_eq(log, [], "source %d: nothing to graze yet" % source)
	var t := _target(source, Vector2(10, 0))
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d: found by the next tick's scan" % source)
	assert_eq(log[0][1], t, "source %d: named" % source)


func test_between_scans_positions_stay_live(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	sp.graze_update_interval = 10.0
	var t := _target(source, Vector2(-500, 0))
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory() # the first scan finds it, far away
	var scans: int = sp.debug_get_graze_detector_stats()["scans"]
	t.position = Vector2(10, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d: a found target moving onto the bullet grazes at once" % source)
	assert_eq(sp.debug_get_graze_detector_stats()["scans"], scans, "source %d: without a rescan" % source)


func test_a_newcomer_waits_for_the_next_scan_or_a_refresh(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	sp.graze_update_interval = 10.0
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory() # the first scan finds nothing
	var t := _target(source, Vector2(10, 0))
	step_factory(3)
	assert_eq(log, [], "source %d: not before the next scan, 10 s away" % source)
	assert_eq(sp.resolve_graze_targets(), [], "source %d: not a target yet" % source)
	assert_eq(sp.refresh_graze_targets(), 1, "source %d: a refresh finds it now" % source)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d: the flying bullet grazes it next tick" % source)
	assert_eq(log[0][1], t, "source %d: named" % source)


func test_a_freed_target_drops_at_once_between_scans(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	sp.graze_update_interval = 10.0
	var t := _target(source, Vector2(10, 0))
	var log := H.record_graze(sp)
	var v := _shoot(sp)
	step_factory()
	assert_true(v.is_bullet_inside_graze(0, 0), "source %d: inside" % source)
	t.free()
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d: the visit ended silently" % source)
	assert_false(v.is_bullet_inside_graze(0, 0), "source %d: no visit left" % source)
	assert_eq(sp.resolve_graze_targets(), [], "source %d: no target" % source)


func test_a_target_that_stops_matching_drops_at_the_next_scan(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	var t := _target(source, Vector2(10, 0))
	var log := H.record_graze(sp)
	var v := _shoot(sp)
	step_factory()
	assert_true(v.is_bullet_inside_graze(0, 0), "source %d: inside" % source)
	_unmatch(source, t)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d: no exit, the visit ended silently" % source)
	assert_false(v.is_bullet_inside_graze(0, 0), "source %d: no visit left" % source)
	assert_eq(sp.resolve_graze_targets(), [], "source %d: no longer a target" % source)


func test_a_target_outside_the_tree_sits_out_until_it_is_back(source: int = use_parameters(SOURCES)) -> void:
	for interval in [0.0, 10.0]: # rescanned every tick, then held between scans
		var sp := _spawner(source)
		sp.graze_update_interval = interval
		var t := _target(source, Vector2(-500, 0))
		var log := H.record_graze(sp)
		_shoot(sp)
		step_factory()
		var home := t.get_parent()
		home.remove_child(t)
		t.position = Vector2(10, 0)
		step_factory(2)
		assert_eq(log, [], "source %d, interval %s: a detached target grazes nothing" % [source, interval])
		home.add_child(t)
		step_factory()
		assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d, interval %s: back in the tree, it grazes" % [source, interval])
		t.free()
		sp.free()


func test_the_filter_group_applies(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	sp.graze_filter_group = &"allowed"
	var t := _target(source, Vector2(10, 0))
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory(2)
	assert_eq(log, [], "source %d: a node outside the filter group never grazes" % source)
	assert_eq(sp.resolve_graze_targets(), [], "source %d: and is no target" % source)
	t.add_to_group(&"allowed")
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d: in the filter group: grazes" % source)


func test_setting_edits_reach_bullets_in_flight(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	_target(source, Vector2(-500, 0)) # the first setting's node, far away
	var other := Node2D.new()
	other.position = Vector2(10, 0)
	var other_parent := Node2D.new()
	other_parent.name = "Elsewhere" # not "Other...": Contains would match it
	add(other_parent)
	other_parent.add_child(other)
	other.name = "Other"
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory(2)
	assert_eq(log, [], "source %d: the first setting's node is far" % source)
	match source:
		GROUP:
			other.add_to_group(&"other")
			sp.graze_node_group = &"other"
		PATH:
			sp.graze_target_path = sp.get_path_to(other)
		NAME:
			sp.graze_node_name = "Other"
		CHILDREN:
			sp.graze_children_parent_path = NodePath("../Elsewhere")
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d: the flying bullet follows the new setting" % source)
	assert_eq(log[0][1], other, "source %d: named" % source)


func test_orphaned_volleys_keep_the_source(source: int = use_parameters(SOURCES)) -> void:
	var sp := _loose_spawner(source)
	var t := _target(source, Vector2(-500, 0))
	var log := H.record_graze(factory)
	_shoot(sp)
	step_factory() # resolved while the spawner lives
	sp.free()
	t.position = Vector2(10, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d: the orphaned volley still grazes its target" % source)
	assert_eq(log[0][1], t, "source %d: named, on the factory" % source)


func test_orphaned_volleys_find_newcomers(source: int = use_parameters(MULTI_SOURCES)) -> void:
	var sp := _loose_spawner(source)
	var log := H.record_graze(factory)
	_shoot(sp)
	step_factory()
	sp.free()
	var late := _target(source, Vector2(10, 0), 7)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d: a node that appears after the spawner is gone still grazes" % source)
	assert_eq(log[0][1], late, "source %d: named" % source)


func test_the_preview_draws_exactly_the_runtime_targets(source: int = use_parameters(SOURCES)) -> void:
	var sp := _spawner(source)
	sp.graze_show_preview = true
	sp.graze_preview_during_runtime = true
	_decoy(Vector2(5, 0))
	var expected := [_target(source, Vector2(100, 0))]
	if source != PATH:
		expected.append(_target(source, Vector2(200, 0), 1))
	await idle(2)
	var ids: Array = []
	for c in sp.debug_get_graze_preview_circles():
		ids.append(c["target_id"])
	assert_eq(ids, _ids(expected), "source %d: rings around its nodes only, in tree order" % source)
	assert_eq(_ids(sp.resolve_graze_targets()), ids, "source %d: exactly what the runtime tests" % source)


func test_results_come_in_tree_order(source: int = use_parameters(MULTI_SOURCES)) -> void:
	var sp := _spawner(source)
	var a := _target(source, Vector2(100, 0), 1)
	var b := _target(source, Vector2(200, 0), 2)
	var c := _target(source, Vector2(300, 0), 3)
	c.get_parent().move_child(c, 0)
	sp.refresh_graze_targets()
	assert_eq(sp.resolve_graze_targets(), [c, a, b], "source %d: tree order, not creation order" % source)


func test_paths_resolve_from_the_spawner() -> void:
	# A relative path means something else once the spawner moves: the next
	# scan follows it.
	for source in [PATH, CHILDREN]:
		var sp := _spawner(source)
		var outside := _target(source, Vector2(-500, 0)) # ../SrcTarget or a child of ../SrcParent
		var holder := Node2D.new()
		add(holder)
		var inner_parent := Node2D.new()
		inner_parent.name = "SrcParent"
		holder.add_child(inner_parent)
		var inside := Node2D.new()
		inside.position = Vector2(10, 0)
		if source == PATH:
			holder.add_child(inside)
			inside.name = "SrcTarget"
		else:
			inner_parent.add_child(inside)
		var log := H.record_graze(sp)
		_shoot(sp)
		step_factory()
		assert_eq(log, [], "source %d: the path's node is far" % source)
		sp.reparent(holder)
		step_factory()
		assert_eq(H.graze_kinds(log), ["enter:0:0"], "source %d: after the move the path names the holder's node" % source)
		assert_eq(log[0][1], inside, "source %d: named" % source)
		assert_ne(log[0][1], outside, "source %d: not the old one" % source)
		sp.free()


func test_renaming_a_node_into_the_pattern_makes_it_a_target() -> void:
	var sp := _spawner(NAME)
	var n := _decoy(Vector2(10, 0))
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory()
	assert_eq(log, [], "Decoy is no SrcTarget")
	n.name = "SrcTargetLate"
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "renamed into the pattern: found by the next scan")


func test_every_source_keeps_its_own_settings_across_switches() -> void:
	var sp := _spawner(GROUP)
	sp.graze_target_path = NodePath("../SrcTarget")
	sp.graze_node_name = "SrcTarget"
	sp.graze_children_parent_path = NodePath("../SrcParent")
	var group_target := _target(GROUP, Vector2(100, 0), 1)
	var path_target := _target(PATH, Vector2(200, 0))
	var child := _target(CHILDREN, Vector2(300, 0), 2)
	for round in 2:
		for source in SOURCES:
			sp.graze_target_source = source
			sp.refresh_graze_targets()
			# Name scans the scene in tree order: SrcParent (with the child) came first.
			var expected: Array = {GROUP: [group_target], PATH: [path_target], NAME: [child, group_target, path_target], CHILDREN: [child]}[source]
			assert_eq(sp.resolve_graze_targets(), expected, "round %d, source %d: only the active source decides" % [round, source])
	assert_eq([sp.graze_node_group, sp.graze_target_path, sp.graze_node_name, sp.graze_children_parent_path], [&"src", NodePath("../SrcTarget"), "SrcTarget", NodePath("../SrcParent")], "every setting kept")


func test_every_target_setting_survives_a_scene_round_trip() -> void:
	var values := {
		"graze_target_source": NAME,
		"graze_node_group": &"saved_group",
		"graze_filter_group": &"saved_filter",
		"graze_target_path": NodePath("../Saved"),
		"graze_node_name": "SavedName",
		"graze_node_name_match_mode": BulletSpawner2D.HOMING_NAME_MATCH_ENDS_WITH,
		"graze_node_name_case_sensitive": true,
		"graze_children_parent_path": NodePath("../SavedParent"),
		"graze_children_recursive": true,
		"graze_update_interval": 0.75,
	}
	var src := BulletSpawner2D.new()
	src.set_shooting_enabled(false)
	src.graze_enabled = true
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
