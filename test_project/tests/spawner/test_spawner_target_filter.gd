extends BlastTest
## The ONE target filter shared by every node source of graze
## (graze_target_source) and homing (homing_target_source): a target is a
## Node2D inside the tree, not queued for deletion, at a finite position, in
## the bullets' World2D, never a BulletFactory2D or anything inside one,
## never a preview layer. On top of it, per source: the scans (Node Name,
## Node Children, direct or recursive) never find the spawner or anything
## under it; the explicit sources (Node Group members, Node Path) may name
## the spawner's own children; homing Node Path may name the spawner itself
## (bullets fly back to or orbit it); graze never uses the spawner itself.
## Every combination of feature x source x candidate kind is checked.

## Candidate kinds, each built fresh next to a fresh spawner.
const KINDS := ["ok", "self", "marker", "factory", "in_factory", "deep_in_factory", "dying", "plain", "other_world", "nan", "preview", "out_of_tree"]

## [feature, source, kinds the source must find].
const CASES := [
	["graze", "group", ["ok", "marker"]],
	["graze", "path", ["ok", "marker"]],
	["graze", "name", ["ok"]],
	["graze", "children", ["ok"]],
	["graze", "children_recursive", ["ok"]],
	["homing", "group", ["ok", "marker"]],
	["homing", "path", ["ok", "self", "marker"]],
	["homing", "name", ["ok"]],
	["homing", "children", ["ok"]],
	["homing", "children_recursive", ["ok"]],
	# Lasers: the same filter; Node Group never takes the spawner's children
	# (its documented rule), Node Path may name them but never the spawner.
	["laser", "group", ["ok"]],
	["laser", "path", ["ok", "marker"]],
	["laser", "name", ["ok"]],
	["laser", "children", ["ok"]],
	["laser", "children_recursive", ["ok"]],
]


## The candidate of `kind` (named "Cand<kind>", in group "cand"), placed
## where the kind says; `sp` is the spawner it is checked against.
func _candidate(kind: String, sp: PatternSpawner2D) -> Node:
	var n: Node
	match kind:
		"self":
			n = sp
		"factory":
			n = factory
		"plain":
			n = Node.new()
			add(n)
		_:
			var n2 := Node2D.new()
			n2.position = Vector2(30, 0)
			n = n2
			match kind:
				"marker":
					sp.add_child(n2) # a child of the spawner (a pattern marker)
				"in_factory":
					factory.add_child(n2)
				"deep_in_factory":
					var holder := Node.new()
					factory.add_child(holder)
					holder.add_child(n2)
				"other_world":
					var viewport := SubViewport.new() # its own World2D
					add(viewport)
					viewport.add_child(n2)
				_:
					add(n2)
			if kind == "nan":
				n2.position = Vector2(NAN, 0)
			if kind == "preview":
				n2.set_meta(&"blastbullets_pattern_preview", true)
	n.name = "Cand" + kind
	n.add_to_group(&"cand")
	return n


## Points `sp`'s `feature` source at `c` and returns what it resolves.
func _resolve(feature: String, source: String, sp: PatternSpawner2D, c: Node, parent_path: NodePath, path: NodePath) -> Array:
	var rejected := c is not Node2D and source == "path"
	if feature == "laser":
		var ls := sp as LaserSpawner2D
		match source:
			"group":
				ls.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_GROUP
				ls.laser_target_group = &"cand"
			"path":
				ls.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_PATH
				ls.laser_target_path = path
				if rejected:
					expect_error_sequence(["LaserSpawner2D: laser_target_path must point to a Node2D, keeping the old value."])
			"name":
				ls.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_NAME
				ls.laser_target_name_match_mode = PatternSpawner2D.HOMING_NAME_MATCH_EXACT
				ls.laser_target_node_name = String(c.name)
			"children", "children_recursive":
				ls.laser_target_source = LaserSpawner2D.LASER_TARGET_NODE_CHILDREN
				ls.laser_target_children_recursive = source == "children_recursive"
				ls.laser_target_children_parent_path = sp.get_path_to(sp.get_parent()) if source == "children_recursive" else parent_path
		return ls.resolve_laser_targets()
	var bs := sp as BulletSpawner2D
	if feature == "graze":
		bs.graze_zones = [H.make_graze_zone([20.0])]
		bs.graze_enabled = true
		match source:
			"group":
				bs.graze_node_group = &"cand"
			"path":
				bs.graze_target_source = BulletSpawner2D.GRAZE_SOURCE_NODE_PATH
				bs.graze_target_path = path
				if rejected:
					expect_error_sequence(["BulletSpawner2D: graze_target_path must point to a Node2D, keeping the old value."])
			"name":
				bs.graze_target_source = BulletSpawner2D.GRAZE_SOURCE_NODE_NAME
				bs.graze_node_name_match_mode = BulletSpawner2D.HOMING_NAME_MATCH_EXACT
				bs.graze_node_name = String(c.name)
			"children", "children_recursive":
				bs.graze_target_source = BulletSpawner2D.GRAZE_SOURCE_NODE_CHILDREN
				bs.graze_children_recursive = source == "children_recursive"
				bs.graze_children_parent_path = sp.get_path_to(sp.get_parent()) if source == "children_recursive" else parent_path
		bs.refresh_graze_targets() # this tick's lists, rescanned now
		return bs.resolve_graze_targets()
	bs.homing_enabled = true
	bs.homing_retarget_mode = BulletSpawner2D.HOMING_RETARGET_OFF
	bs.homing_max_targets = 1000
	bs.homing_target_selection = BulletSpawner2D.HOMING_SELECT_FIRST
	match source:
		"group":
			bs.homing_node_group = &"cand"
		"path":
			bs.homing_target_source = BulletSpawner2D.HOMING_SOURCE_NODE_PATH
			bs.homing_target_path = path
			if rejected:
				expect_error_sequence(["BulletSpawner2D: homing_target_path must point to a Node2D, keeping the old value."])
		"name":
			bs.homing_target_source = BulletSpawner2D.HOMING_SOURCE_NODE_NAME
			bs.homing_node_name_match_mode = BulletSpawner2D.HOMING_NAME_MATCH_EXACT
			bs.homing_node_name = String(c.name)
		"children", "children_recursive":
			bs.homing_target_source = BulletSpawner2D.HOMING_SOURCE_NODE_CHILDREN
			bs.homing_children_recursive = source == "children_recursive"
			bs.homing_children_parent_path = sp.get_path_to(sp.get_parent()) if source == "children_recursive" else parent_path
	return bs.resolve_homing_targets(true, false)


## Whether `feature`'s `source` finds a candidate of `kind`.
func _finds(feature: String, source: String, kind: String) -> bool:
	var sp: PatternSpawner2D
	if feature == "laser":
		var ls := LaserSpawner2D.new()
		ls.set_shooting_enabled(false)
		ls.laser_data = H.make_laser()
		ls.pattern_source = LaserSpawner2D.PATTERN_FROM_SELF
		add_child(ls)
		ls.set_bullet_factory(factory)
		sp = ls
	else:
		sp = make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.name = "FilterSpawner"
	var factory_name := factory.name
	var c := _candidate(kind, sp)
	var parent_path := sp.get_path_to(c.get_parent())
	var path := sp.get_path_to(c)
	if kind == "dying":
		c.queue_free()
	if kind == "out_of_tree":
		c.get_parent().remove_child(c)
	var found := false
	for target in _resolve(feature, source, sp, c, parent_path, path):
		found = found or (target is Object and is_instance_valid(target) and target == c)
	# Leave nothing behind for the next kind (one factory per test).
	c.remove_from_group(&"cand")
	factory.name = factory_name
	match kind:
		"out_of_tree":
			c.free()
		"marker", "in_factory":
			c.get_parent().remove_child(c)
			c.free()
		"deep_in_factory":
			c.get_parent().free()
	sp.get_parent().remove_child(sp)
	sp.free()
	return found


func test_every_source_applies_the_shared_filter(p: Array = use_parameters(CASES)) -> void:
	var feature: String = p[0]
	var source: String = p[1]
	var found: Array = []
	for kind in KINDS:
		if _finds(feature, source, kind):
			found.append(kind)
	assert_eq(found, p[2], "%s %s finds exactly these kinds" % [feature, source])
