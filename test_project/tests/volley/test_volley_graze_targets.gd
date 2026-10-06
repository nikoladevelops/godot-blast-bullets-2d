extends BlastTest
## Graze target resolution (the shared filter in core/graze_targets2d.hpp,
## through the factory's per-sweep cache): only live Node2D members of the
## group count; members that are not Node2D, queued for deletion, outside
## the tree, at a non-finite position or in another World2D are skipped;
## an empty group is a dormant zone (no warning); more than 4 targets keeps
## the first 4 in tree order and warns once; a respawned target is picked up
## on the next tick while Once bits stay per zone.


func _resting_bullet_in_zone(radius: float = 20.0) -> BulletVolley2D:
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	v.graze_set_zones([H.make_graze_zone([radius])])
	return v


func test_an_empty_group_is_a_dormant_zone() -> void:
	var v := _resting_bullet_in_zone()
	var log := H.record_graze(factory)
	step_factory(3)
	assert_eq(log, [], "nothing to graze")
	assert_false(v.debug_get_graze_info()["active_this_tick"], "no per-bullet work this tick")
	assert_eq(factory.debug_get_graze_targets(&"graze_targets"), [], "no targets")


func test_members_that_are_not_node2d_are_skipped() -> void:
	var plain := Node.new()
	add(plain)
	plain.add_to_group(&"graze_targets")
	var control := Control.new()
	add(control)
	control.add_to_group(&"graze_targets")
	var t := make_graze_target(Vector2(10, 0))
	var log := H.record_graze(factory)
	_resting_bullet_in_zone()
	step_factory()
	assert_eq(factory.debug_get_graze_targets(&"graze_targets").size(), 1, "only the Node2D")
	assert_eq(log.size(), 1, "grazed once")
	assert_eq(log[0][1], t, "by the Node2D")


func test_a_target_queued_for_deletion_is_skipped() -> void:
	var t := make_graze_target(Vector2(10, 0))
	var log := H.record_graze(factory)
	_resting_bullet_in_zone()
	t.queue_free()
	step_factory(2)
	assert_eq(log, [], "a dying target grazes nothing")
	assert_eq(factory.debug_get_graze_targets(&"graze_targets"), [], "not in the snapshot")


func test_a_target_outside_the_tree_is_skipped() -> void:
	var t := make_graze_target(Vector2(10, 0))
	var parent := t.get_parent()
	parent.remove_child(t)
	var log := H.record_graze(factory)
	_resting_bullet_in_zone()
	step_factory(2)
	assert_eq(log, [], "a detached target grazes nothing")
	parent.add_child(t)
	step_factory()
	assert_eq(log.size(), 1, "back in the tree (still in its group): grazes")


func test_a_target_at_a_non_finite_position_is_skipped() -> void:
	var t := make_graze_target(Vector2(10, 0))
	t.position = Vector2(NAN, 0)
	var log := H.record_graze(factory)
	var v := _resting_bullet_in_zone()
	step_factory(2)
	assert_eq(log, [], "a NaN target grazes nothing")
	assert_eq(factory.debug_get_graze_targets(&"graze_targets"), [], "not in the snapshot")
	t.position = Vector2(10, 0)
	step_factory()
	assert_eq(log.size(), 1, "finite again: grazes")
	assert_true(H.finite_volley([v.get_bullet_transform(0)]), "the bullet stayed finite")


func test_a_target_in_another_world_is_skipped() -> void:
	var viewport := SubViewport.new()
	add(viewport)
	var other := Node2D.new()
	other.position = Vector2(10, 0)
	viewport.add_child(other)
	other.add_to_group(&"graze_targets")
	assert_ne(other.get_world_2d(), factory.get_world_2d(), "the SubViewport has its own World2D")
	var log := H.record_graze(factory)
	_resting_bullet_in_zone()
	step_factory(2)
	assert_eq(log, [], "coordinates of another world never mix")


func test_more_than_four_targets_keeps_the_first_four_and_warns_once() -> void:
	for i in 4:
		make_graze_target(Vector2(500 + 50 * i, 0))
	var fifth := make_graze_target(Vector2(10, 0))
	var log := H.record_graze(factory)
	_resting_bullet_in_zone()
	step_factory(3)
	expect_warning_sequence(["BulletGrazeZone2D: group 'graze_targets' holds 5 graze targets; only the first 4 in tree order are tested."])
	assert_eq(log, [], "the fifth target (last in tree order) is not tested")
	var ids: Array = []
	for t in factory.debug_get_graze_targets(&"graze_targets"):
		ids.append(t["id"])
	assert_eq(ids.size(), 4, "four targets")
	assert_false(ids.has(fifth.get_instance_id()), "not the fifth")


func test_a_respawned_target_is_picked_up_next_tick() -> void:
	var old := make_graze_target(Vector2(10, 0))
	var v := graze_volley(H.transforms_at([Vector2.ZERO, Vector2(300, 0)]), 0.0)
	v.graze_set_zones([H.make_graze_zone([20.0])])
	var log := H.record_graze(factory)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "bullet 0 grazed the first target")
	old.free()
	var respawned := make_graze_target(Vector2(305, 0))
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:1:0"], "bullet 1 grazes the new target; bullet 0's visit ended silently with its target (no exit naming the newcomer)")
	assert_eq(log[1][1], respawned, "named")
	respawned.position = Vector2(5, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:1:0", "exit:1:0"], "bullet 1 left it; Once bits are per zone, so bullet 0 does not graze the replacement")
