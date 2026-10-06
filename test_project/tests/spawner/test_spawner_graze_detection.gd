extends BlastTest
## BulletSpawner2D graze target sources: graze_target_source (Zone Groups,
## Node Path, Node Name, Node Children) with homing's name matching,
## graze_filter_group, graze_update_interval and refresh_graze_targets().
## Defaults and serialized ids are locked, setters reject and keep with
## exact texts, each source shows only its own knobs and names its empty
## setting in the setup warnings. Enemies spawned after the spawner fired are
## grazed (the reported case). Every source finds exactly its nodes (never
## the spawner, its markers, a bullet factory's nodes, dying or non-Node2D
## nodes), the filter applies to all of them, the interval holds membership
## between scans while positions stay live, a refresh rescans at once, edits
## reach bullets in flight, orphaned volleys keep their spawner's settings,
## both ring previews draw the source's targets, more than 64 matches warn
## once, default spawners share the factory's one scan per tick, detectors
## live exactly as long as their spawner or its bullets and never leak
## through the pool. Homing's refresh is retarget_live_volleys().

const ZONE_GROUPS := BulletSpawner2D.GRAZE_SOURCE_ZONE_GROUPS
const NODE_PATH := BulletSpawner2D.GRAZE_SOURCE_NODE_PATH
const NODE_NAME := BulletSpawner2D.GRAZE_SOURCE_NODE_NAME
const NODE_CHILDREN := BulletSpawner2D.GRAZE_SOURCE_NODE_CHILDREN


## A spawner at the origin firing one resting bullet (circle r4), graze on
## with `zones` (default: one 20 px ring over the default test group).
func _spawner(zones: Array = [], source: int = ZONE_GROUPS) -> BulletSpawner2D:
	var d := H.make_volley_data(1, 0.0, 60.0)
	d.collision_shape = H.make_circle_shape(4.0)
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.graze_zones = zones if not zones.is_empty() else [H.make_graze_zone([20.0])]
	sp.graze_enabled = true
	sp.graze_target_source = source
	return sp


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
	sp.graze_zones = [H.make_graze_zone([20.0])]
	sp.graze_enabled = true
	sp.graze_target_source = source
	add_child(sp)
	sp.set_bullet_factory(factory)
	return sp


func _shoot(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fired")
	return got[0] if not got.is_empty() else null


## A named Node2D at `pos` (under `parent`, else under the test).
func _node(node_name: String, pos: Vector2, parent: Node = null) -> Node2D:
	var n := Node2D.new()
	n.name = node_name
	n.position = pos
	if parent == null:
		add(n)
	else:
		parent.add_child(n)
	assert_eq(String(n.name), node_name, "the name stuck")
	return n


func _ids(nodes: Array) -> Array:
	var out: Array = []
	for n in nodes:
		out.append((n as Object).get_instance_id())
	return out


func _graze_warnings(sp: BulletSpawner2D) -> Array:
	var out: Array = []
	for w in sp.get_setup_warnings():
		if "graze" in w.to_lower():
			out.append(w)
	return out


func test_api_is_bound_with_documented_defaults() -> void:
	var sp := make_spawner()
	for m in ["get_graze_target_source", "set_graze_target_source", "get_graze_filter_group", "set_graze_filter_group", "get_graze_target_path", "set_graze_target_path", "get_graze_node_name", "set_graze_node_name", "get_graze_node_name_match_mode", "set_graze_node_name_match_mode", "get_graze_node_name_case_sensitive", "set_graze_node_name_case_sensitive", "get_graze_children_parent_path", "set_graze_children_parent_path", "get_graze_children_recursive", "set_graze_children_recursive", "get_graze_update_interval", "set_graze_update_interval", "refresh_graze_targets", "debug_get_graze_detector_stats"]:
		assert_true(sp.has_method(m), "BulletSpawner2D.%s is bound" % m)
	assert_eq([ZONE_GROUPS, NODE_PATH, NODE_NAME, NODE_CHILDREN], [0, 1, 2, 3], "serialized source ids")
	assert_eq(sp.graze_target_source, ZONE_GROUPS, "zone groups by default (each zone's target_group)")
	assert_eq(sp.graze_filter_group, &"", "no filter")
	assert_eq(sp.graze_target_path, NodePath(""), "no path")
	assert_eq(sp.graze_node_name, "Player", "name")
	assert_eq(sp.graze_node_name_match_mode, BulletSpawner2D.HOMING_NAME_MATCH_CONTAINS, "contains, like homing")
	assert_false(sp.graze_node_name_case_sensitive, "case-insensitive")
	assert_eq(sp.graze_children_parent_path, NodePath(""), "no parent")
	assert_false(sp.graze_children_recursive, "direct children")
	assert_eq(sp.graze_update_interval, 0.0, "scans every tick")
	assert_true(sp.debug_get_graze_detector_stats()["shares_factory_lists"], "defaults share the factory's lists")


func test_setters_reject_and_keep() -> void:
	var sp := make_spawner()
	var plain := Node.new()
	add(plain)
	sp.graze_target_source = NODE_NAME
	sp.set("graze_target_source", -1)
	sp.set("graze_target_source", 4)
	sp.graze_node_name_match_mode = BulletSpawner2D.HOMING_NAME_MATCH_EXACT
	sp.set("graze_node_name_match_mode", -1)
	sp.set("graze_node_name_match_mode", 4)
	sp.graze_update_interval = 0.25
	sp.graze_update_interval = NAN
	sp.graze_update_interval = -0.1
	sp.graze_update_interval = INF
	sp.graze_target_path = sp.get_path_to(plain)
	expect_error_sequence([
		"BulletSpawner2D: invalid graze_target_source, keeping the old value.",
		"BulletSpawner2D: invalid graze_target_source, keeping the old value.",
		"BulletSpawner2D: invalid graze_node_name_match_mode, keeping the old value.",
		"BulletSpawner2D: invalid graze_node_name_match_mode, keeping the old value.",
		"BulletSpawner2D: graze_update_interval must be finite and >= 0 (0 scans every tick), keeping the old value.",
		"BulletSpawner2D: graze_update_interval must be finite and >= 0 (0 scans every tick), keeping the old value.",
		"BulletSpawner2D: graze_update_interval must be finite and >= 0 (0 scans every tick), keeping the old value.",
		"BulletSpawner2D: graze_target_path must point to a Node2D, keeping the old value.",
	])
	assert_eq(sp.graze_target_source, NODE_NAME, "source kept")
	assert_eq(sp.graze_node_name_match_mode, BulletSpawner2D.HOMING_NAME_MATCH_EXACT, "match mode kept")
	assert_eq(sp.graze_update_interval, 0.25, "interval kept")
	assert_eq(sp.graze_target_path, NodePath(""), "path kept")
	assert_false(sp.debug_get_graze_detector_stats()["shares_factory_lists"], "a name source has its own lists")


func test_each_source_shows_only_its_own_knobs() -> void:
	var sp := make_spawner()
	var always := ["graze_target_source", "graze_filter_group", "graze_update_interval"]
	var own := {
		ZONE_GROUPS: [],
		NODE_PATH: ["graze_target_path"],
		NODE_NAME: ["graze_node_name", "graze_node_name_match_mode", "graze_node_name_case_sensitive"],
		NODE_CHILDREN: ["graze_children_parent_path", "graze_children_recursive"],
	}
	var specific: Array = []
	for source in own:
		specific.append_array(own[source])
	for k in always + specific:
		assert_false(is_editor_visible(sp, StringName(k)), k + " hides while graze is off")
	sp.graze_enabled = true
	watch_signals(sp)
	for source in own:
		sp.graze_target_source = source
		for k in always:
			assert_true(is_editor_visible(sp, StringName(k)), "%s shows under source %d" % [k, source])
		for k in specific:
			assert_eq(is_editor_visible(sp, StringName(k)), (own[source] as Array).has(k), "%s under source %d" % [k, source])
	assert_signal_emit_count(sp, "property_list_changed", 4, "every source change refreshes the inspector")


func test_setup_warnings_name_an_empty_source_setting() -> void:
	var groupless := H.make_graze_zone()
	groupless.target_group = &""
	var sp := _spawner([groupless])
	assert_eq(_graze_warnings(sp), ["graze_zones[0] has an empty target_group: it never finds a target."], "zone groups need a group")
	sp.graze_target_source = NODE_PATH
	assert_eq(_graze_warnings(sp), ["graze_target_source is Node Path but graze_target_path is empty: no graze target is found."], "the zone's group no longer matters")
	sp.graze_target_source = NODE_NAME
	sp.graze_node_name = ""
	assert_eq(_graze_warnings(sp), ["graze_target_source is Node Name but graze_node_name is empty: no graze target is found."], "empty name")
	sp.graze_target_source = NODE_CHILDREN
	assert_eq(_graze_warnings(sp), ["graze_target_source is Node Children but graze_children_parent_path is empty: no graze target is found."], "no parent")
	sp.graze_children_parent_path = NodePath("..")
	assert_eq(_graze_warnings(sp), [], "a usable setting")


func test_enemies_spawned_after_the_spawner_are_grazed() -> void:
	# The reported case: graze over an enemy group, the enemies spawned after
	# the shot, more of them than the old cap of four.
	var sp := _spawner([H.make_graze_zone([20.0], &"enemies")])
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory()
	for i in 5:
		make_graze_target(Vector2(-1000 - 100 * i, 0), &"enemies")
	var sixth := make_graze_target(Vector2(10, 0), &"enemies")
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the sixth enemy, spawned after the shot, grazes on the next tick")
	assert_eq(log[0][1], sixth, "named")
	assert_eq(sp.resolve_graze_targets(0).size(), 6, "all six are targets")


func test_node_name_source_matches_per_mode_and_case() -> void:
	var names := ["Enemy", "BigEnemy", "EnemyShip", "enemy_small", "Ally"]
	for i in names.size():
		_node(names[i], Vector2(1000 + 100 * i, 0))
	var sp := _spawner([], NODE_NAME)
	sp.graze_node_name = "Enemy"
	var cases := [
		[BulletSpawner2D.HOMING_NAME_MATCH_EXACT, false, ["Enemy"]],
		[BulletSpawner2D.HOMING_NAME_MATCH_EXACT, true, ["Enemy"]],
		[BulletSpawner2D.HOMING_NAME_MATCH_CONTAINS, false, ["Enemy", "BigEnemy", "EnemyShip", "enemy_small"]],
		[BulletSpawner2D.HOMING_NAME_MATCH_CONTAINS, true, ["Enemy", "BigEnemy", "EnemyShip"]],
		[BulletSpawner2D.HOMING_NAME_MATCH_STARTS_WITH, false, ["Enemy", "EnemyShip", "enemy_small"]],
		[BulletSpawner2D.HOMING_NAME_MATCH_STARTS_WITH, true, ["Enemy", "EnemyShip"]],
		[BulletSpawner2D.HOMING_NAME_MATCH_ENDS_WITH, false, ["Enemy", "BigEnemy"]],
		[BulletSpawner2D.HOMING_NAME_MATCH_ENDS_WITH, true, ["Enemy", "BigEnemy"]],
	]
	for c in cases:
		sp.graze_node_name_match_mode = c[0]
		sp.graze_node_name_case_sensitive = c[1]
		var got: Array = []
		for n in sp.resolve_graze_targets(0):
			got.append(String((n as Node).name))
		assert_eq(got, c[2], "mode %d, case sensitive %s (tree order)" % [c[0], c[1]])


func test_node_name_scan_skips_the_spawner_its_markers_factories_dying_and_plain_nodes() -> void:
	var sp := _spawner([], NODE_NAME)
	sp.graze_node_name = "Target"
	sp.name = "TargetSpawner"
	_node("TargetMarker", Vector2.ZERO, sp)
	_node("TargetInFactory", Vector2.ZERO, factory)
	var plain := Node.new()
	plain.name = "TargetPlain"
	add(plain)
	var dying := _node("TargetDying", Vector2(5, 0))
	dying.queue_free()
	var real := _node("TargetReal", Vector2(10, 0))
	assert_eq(sp.resolve_graze_targets(0), [real], "only the live Node2D outside the spawner and the factory")
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "one graze")
	assert_eq(log[0][1], real, "by the real target")


func test_node_path_source_grazes_the_node_at_the_path() -> void:
	var target := _node("PathTarget", Vector2(10, 0))
	make_graze_target(Vector2(-10, 0)) # a zone-group member: not this source
	var sp := _spawner([], NODE_PATH)
	sp.graze_target_path = sp.get_path_to(target)
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "one graze")
	assert_eq(log[0][1], target, "the node at the path, not the zone group's member")
	sp.graze_target_path = NodePath("")
	assert_eq(sp.resolve_graze_targets(0), [], "an empty path finds nothing")
	sp.graze_target_path = sp.get_path_to(target)
	target.free()
	assert_eq(sp.resolve_graze_targets(0), [], "a freed node is no target")


func test_node_children_source_direct_or_recursive() -> void:
	var squad := _node("Squad", Vector2(1000, 0))
	var a := _node("A", Vector2.ZERO, squad)
	var b := _node("B", Vector2(50, 0), squad)
	var holder := Node.new()
	holder.name = "Holder"
	squad.add_child(holder)
	var c := _node("C", Vector2(0, -300), holder) # under a plain Node: its position is global
	var d := _node("D", Vector2(0, 300), a)
	var sp := _spawner([], NODE_CHILDREN)
	sp.graze_children_parent_path = sp.get_path_to(squad)
	assert_eq(sp.resolve_graze_targets(0), [a, b], "direct Node2D children")
	sp.graze_children_recursive = true
	assert_eq(sp.resolve_graze_targets(0), [a, d, b, c], "the whole subtree, depth-first in tree order")
	squad.position = Vector2(-10, 0) # A sits 10 px from the bullet
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "one graze")
	assert_eq(log[0][1], a, "by the nearest child")


func test_a_spawner_never_finds_itself() -> void:
	var sp := _spawner([], NODE_NAME)
	sp.name = "PlayerTurret"
	assert_eq(sp.resolve_graze_targets(0), [], "name: never itself (Contains 'Player')")
	sp.graze_target_source = NODE_PATH
	sp.graze_target_path = NodePath(".")
	assert_eq(sp.resolve_graze_targets(0), [], "path: never itself")
	sp.graze_target_source = NODE_CHILDREN
	sp.graze_children_parent_path = NodePath("..")
	assert_false(sp.resolve_graze_targets(0).has(sp), "children: never itself")
	sp.graze_target_source = ZONE_GROUPS
	sp.add_to_group(&"graze_targets")
	assert_eq(sp.resolve_graze_targets(0), [], "group: never itself")
	sp.graze_filter_group = &"graze_targets" # own lists now
	assert_eq(sp.resolve_graze_targets(0), [], "filtered group: never itself")


func test_the_filter_group_applies_to_every_source() -> void:
	var kept := _node("FilterKept", Vector2(10, 0))
	kept.add_to_group(&"graze_targets")
	kept.add_to_group(&"allowed")
	var dropped := _node("FilterDropped", Vector2(20, 0))
	dropped.add_to_group(&"graze_targets")
	var sp := _spawner()
	sp.graze_filter_group = &"allowed"
	assert_false(sp.debug_get_graze_detector_stats()["shares_factory_lists"], "a filter needs own lists")
	assert_eq(sp.resolve_graze_targets(0), [kept], "zone groups")
	sp.graze_target_source = NODE_NAME
	sp.graze_node_name = "Filter"
	assert_eq(sp.resolve_graze_targets(0), [kept], "node name")
	sp.graze_target_source = NODE_CHILDREN
	sp.graze_children_parent_path = NodePath("..")
	assert_eq(sp.resolve_graze_targets(0), [kept], "node children")
	sp.graze_target_source = NODE_PATH
	sp.graze_target_path = sp.get_path_to(dropped)
	assert_eq(sp.resolve_graze_targets(0), [], "node path: a node outside the filter is no target")
	sp.graze_target_path = sp.get_path_to(kept)
	assert_eq(sp.resolve_graze_targets(0), [kept], "node path")


func test_update_interval_holds_membership_between_scans() -> void:
	var sp := _spawner([], NODE_NAME)
	sp.graze_node_name = "Mover"
	sp.graze_update_interval = 0.5
	var far := _node("MoverFar", Vector2(-500, 0))
	var log := H.record_graze(sp)
	var v := _shoot(sp)
	step_factory() # the first scan finds MoverFar
	var scans: int = sp.debug_get_graze_detector_stats()["scans"]
	var late := _node("MoverLate", Vector2(-500, 100))
	step_factory(20) # 1/3 s: no rescan yet
	assert_eq(sp.resolve_graze_targets(0), [far], "a newcomer waits for the next scan")
	far.position = Vector2(-10, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "a found target moving onto the bullet grazes at once: positions are live")
	assert_eq(sp.debug_get_graze_detector_stats()["scans"], scans, "no scan in between")
	step_factory(15) # past 0.5 s since the first scan
	assert_eq(sp.debug_get_graze_detector_stats()["scans"], scans + 1, "one rescan per interval")
	assert_eq(sp.resolve_graze_targets(0), [far, late], "the rescan found the newcomer")
	far.free()
	step_factory()
	assert_eq(sp.resolve_graze_targets(0), [late], "a freed target stops counting at once")
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "its visit ended silently")
	assert_false(v.is_bullet_inside_graze(0, 0), "no visit left")


func test_refresh_graze_targets_rescans_at_once() -> void:
	var sp := _spawner([], NODE_NAME)
	sp.graze_node_name = "Fresh"
	sp.graze_update_interval = 10.0
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory()
	var late := _node("FreshLate", Vector2(10, 0))
	step_factory(3)
	assert_eq(log, [], "not before the next scan, 10 s away")
	assert_eq(sp.refresh_graze_targets(), 1, "one target now")
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the flying bullet grazes it on the next tick")
	assert_eq(log[0][1], late, "named")


func test_refresh_counts_distinct_targets_and_refuses_outside_the_tree() -> void:
	var a := make_graze_target(Vector2(100, 0))
	var b := make_graze_target(Vector2(200, 0), &"graze_other")
	var sp := _spawner([H.make_graze_zone([20.0]), H.make_graze_zone([20.0]), H.make_graze_zone([20.0], &"graze_other")])
	assert_eq(sp.refresh_graze_targets(), 2, "two zones over one group and a third zone: two distinct targets")
	assert_eq(_ids(sp.resolve_graze_targets(2)), _ids([b]), "the third zone's own group")
	assert_eq(_ids(sp.resolve_graze_targets(0)), _ids([a]), "the first zone's")
	var loose := BulletSpawner2D.new()
	assert_eq(loose.refresh_graze_targets(), 0, "nothing outside the tree")
	expect_error_sequence(["BulletSpawner2D.refresh_graze_targets: spawner is outside the scene tree, nothing refreshed."])
	loose.free()


func test_setting_edits_reach_bullets_in_flight() -> void:
	var sp := _spawner([], NODE_NAME)
	sp.graze_node_name = "Alpha"
	_node("Beta", Vector2(10, 0))
	var log := H.record_graze(sp)
	var v := _shoot(sp)
	step_factory(2)
	assert_eq(log, [], "Beta is no Alpha")
	sp.graze_node_name = "Beta"
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the flying bullet follows the new name")
	assert_true(v.is_bullet_inside_graze(0, 0), "inside")
	sp.graze_target_source = ZONE_GROUPS # Beta is in no zone group
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "switching the source away ends the visit silently")
	assert_false(v.is_bullet_inside_graze(0, 0), "no visit left")


func test_a_source_switch_re_anchors_open_visits_to_the_nearest_target() -> void:
	# Another source hands out another target list: every open visit moves
	# to the nearest target it is still inside (else ends silently).
	var sp := _spawner([], NODE_NAME)
	sp.graze_node_name = "Both"
	var held := _node("BothHeld", Vector2(15, 0))
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the visit belongs to the held target")
	var nearer := _node("BothNearer", Vector2(-5, 0)) # inside too; Once: no new graze
	step_factory()
	sp.graze_target_source = NODE_CHILDREN
	sp.graze_children_parent_path = NodePath("..") # the same two (the factory sibling never counts)
	step_factory()
	held.position = Vector2(300, 0)
	nearer.position = Vector2(-300, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "exit:0:0"], "one visit, one exit")
	assert_eq(log[1][1], nearer, "re-anchored to the nearest target at the switch")


func test_the_children_source_never_finds_a_bullet_factory() -> void:
	var sp := _spawner([], NODE_CHILDREN)
	sp.graze_children_parent_path = sp.get_path_to(factory.get_parent())
	sp.graze_children_recursive = true
	var inside := _node("InsideFactory", Vector2(5, 0), factory)
	var sibling := _node("Sibling", Vector2(10, 0))
	var found := sp.resolve_graze_targets(0)
	assert_false(found.has(factory), "not the factory")
	assert_false(found.has(inside), "nor anything inside it")
	assert_true(found.has(sibling), "its siblings count")


func test_orphaned_volleys_keep_their_spawner_settings() -> void:
	var named := _loose_spawner(NODE_NAME)
	named.graze_node_name = "Orphan"
	var log := H.record_graze(factory)
	_shoot(named)
	named.free()
	var late := _node("OrphanLate", Vector2(10, 0))
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the name scan goes on without the spawner")
	assert_eq(log[0][1], late, "named")
	var target := _node("OrphanPathTarget", Vector2(500, 300))
	var pathed := _loose_spawner(NODE_PATH)
	pathed.position = Vector2(500, 0)
	pathed.graze_target_path = pathed.get_path_to(target)
	_shoot(pathed)
	step_factory() # resolved while the spawner lives
	pathed.free()
	target.position = Vector2(510, 0)
	step_factory()
	assert_eq(log.size(), 2, "the node the path pointed at still grazes the orphan")
	assert_eq(log[1][1], target, "named")


func test_the_ring_preview_draws_the_source_targets() -> void:
	var sp := _spawner([], NODE_NAME)
	sp.graze_node_name = "Shown"
	var shown := _node("ShownA", Vector2(100, 0))
	make_graze_target(Vector2(-100, 0)) # a zone-group member: not this source
	sp.graze_show_preview = true
	sp.graze_preview_during_runtime = true
	await idle(2)
	var ids: Array = []
	for c in sp.debug_get_graze_preview_circles():
		ids.append(c["target_id"])
	assert_eq(ids, _ids([shown]), "rings around the named node only")
	assert_eq(_ids(sp.resolve_graze_targets(0)), ids, "exactly what the runtime tests")


func test_the_factory_preview_draws_each_spawners_targets() -> void:
	var zone := H.make_graze_zone([20.0])
	zone.preview_during_runtime = true
	var a := _spawner([zone], NODE_NAME)
	a.graze_node_name = "RingA"
	var b := _spawner([zone], NODE_NAME)
	b.graze_node_name = "RingB"
	var ta := _node("RingA", Vector2(100, 0))
	var tb := _node("RingB", Vector2(-100, 0))
	await idle(2)
	var info: Dictionary = factory.debug_get_graze_runtime_preview()
	var ids: Array = []
	for c in info["circles"]:
		ids.append(c["target_id"])
	ids.sort()
	var expected := _ids([ta, tb])
	expected.sort()
	assert_eq(ids, expected, "one zone, drawn around each spawner's own targets")
	assert_eq(info["zones"], 2, "once per (zone, target source)")


func test_more_than_64_matches_keep_the_first_64_and_warn_once() -> void:
	for i in 65:
		_node("Swarm%d" % i, Vector2(1000 + i, 0))
	var sp := _spawner([], NODE_NAME)
	sp.graze_node_name = "Swarm"
	assert_eq(sp.resolve_graze_targets(0).size(), 64, "the first 64")
	sp.resolve_graze_targets(0)
	expect_warning_sequence(["BulletSpawner2D: graze_node_name 'Swarm' matches 65 graze targets; only the first 64 in tree order are tested."])
	var squad := _node("BigSquad", Vector2(-1000, 0))
	for i in 65:
		_node("Member%d" % i, Vector2(i, 0), squad)
	sp.graze_target_source = NODE_CHILDREN
	sp.graze_children_parent_path = sp.get_path_to(squad)
	assert_eq(sp.resolve_graze_targets(0).size(), 64, "the first 64 children")
	expect_warning_sequence(["BulletSpawner2D: graze_children_parent_path holds 65 graze targets; only the first 64 in tree order are tested."])


func test_spawners_at_the_defaults_share_the_factorys_scan() -> void:
	make_graze_target(Vector2(10, 0))
	var spawners: Array = []
	for i in 3:
		var sp := _spawner()
		sp.position = Vector2(0, 100 * i)
		_shoot(sp)
		spawners.append(sp)
	step_factory()
	var before: int = factory.debug_get_graze_stats()["refreshes"]
	step_factory(4)
	assert_eq(int(factory.debug_get_graze_stats()["refreshes"]) - before, 4, "one group scan per tick for all of them")
	for sp in spawners:
		assert_eq((sp as BulletSpawner2D).debug_get_graze_detector_stats()["scans"], 0, "no spawner scanned on its own")


func test_a_detector_lives_as_long_as_its_spawner_or_bullets() -> void:
	var base: int = factory.debug_get_graze_stats()["live_detectors"]
	var sp := _loose_spawner(NODE_NAME)
	sp.debug_get_graze_detector_stats()
	assert_eq(factory.debug_get_graze_stats()["live_detectors"], base + 1, "the spawner made one")
	var v := _shoot(sp)
	sp.free()
	assert_eq(factory.debug_get_graze_stats()["live_detectors"], base + 1, "its flying volley keeps it")
	v.disable_bullet(0)
	assert_eq(v.debug_get_life_state(), "pooled", "pooled")
	assert_eq(factory.debug_get_graze_stats()["live_detectors"], base, "gone with the last volley")


func test_a_pooled_spawner_volley_reused_by_the_factory_finds_zone_groups() -> void:
	_node("PoolName", Vector2(-10, 0))
	var member := make_graze_target(Vector2(10, 0))
	var sp := _spawner([], NODE_NAME)
	sp.graze_node_name = "PoolName"
	var log := H.record_graze(factory)
	var v := _shoot(sp)
	step_factory()
	assert_eq(log.size(), 1, "the spawner life grazed the named node")
	v.disable_bullet(0)
	var reused := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	assert_eq(reused, v, "the pool handed it back")
	reused.graze_set_zones([H.make_graze_zone([20.0])])
	log.clear()
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "one graze")
	assert_eq(log[0][1], member, "by the zone group's member: the spawner's settings did not survive the pool")


func test_homing_refresh_is_retarget_live_volleys() -> void:
	# Homing's counterpart of refresh_graze_targets(): retarget_live_volleys()
	# rescans and re-aims every flying homing volley, retargeting off or on.
	var sp := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.homing_enabled = true
	sp.homing_retarget_mode = BulletSpawner2D.HOMING_RETARGET_OFF
	sp.homing_node_group = &"late_homing"
	var v := _shoot(sp)
	expect_warning_sequence(["BulletSpawner2D::resolve_homing_targets: no live Node2D found in group 'late_homing', volley flies without homing."])
	assert_false(v.shared_homing_deque_check_has_homing_targets(), "nothing to chase at the shot")
	var enemy := make_graze_target(Vector2(100, 0), &"late_homing")
	assert_eq(sp.retarget_live_volleys(), 1, "one volley refreshed")
	assert_eq(v.shared_homing_deque_get_current_homing_target(), enemy, "it chases the enemy spawned after the shot")
