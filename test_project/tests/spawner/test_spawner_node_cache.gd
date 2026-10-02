extends BlastTest
## Node cache: a validated manual assignment is never overwritten by a stale
## path; an out-of-tree assigned node stays assigned; path assignment still
## resolves.

var gen_a: Node2D
var gen_b: Node2D
var sp: BulletSpawner2D


func before_each() -> void:
	await super()
	gen_a = add(Node2D.new())
	gen_a.position = Vector2(100, 0)
	gen_b = add(Node2D.new())
	gen_b.position = Vector2(200, 0)
	sp = make_spawner()


func test_manual_assignment_wins_over_stale_path() -> void:
	sp.set_transforms_generator_path(sp.get_path_to(gen_a))
	var parent := sp.get_parent()
	parent.remove_child(sp)
	sp.set_transforms_generator(gen_b)
	parent.add_child(sp)
	await idle(1)
	assert_eq(sp.get_transforms_generator(), gen_b, "manual assignment survives re-entry")
	assert_eq(sp.get_transforms_generator(), gen_b, "repeated getter stable")


func test_out_of_tree_node_stays_assigned() -> void:
	sp.set_transforms_generator(gen_b)
	var parent := gen_b.get_parent()
	parent.remove_child(gen_b)
	assert_eq(sp.get_transforms_generator(), gen_b, "assigned node kept while out of the tree")
	parent.add_child(gen_b)
	assert_eq(sp.get_transforms_generator(), gen_b, "valid again after re-entry")


func test_path_assignment_resolves() -> void:
	sp.set_transforms_generator(gen_b)
	sp.set_transforms_generator_path(sp.get_path_to(gen_a))
	assert_eq(sp.get_transforms_generator(), gen_a, "path resolves to A")
