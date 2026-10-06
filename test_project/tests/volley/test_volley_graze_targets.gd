extends BlastTest
## Graze target resolution (the shared filter in core/graze_targets2d.hpp,
## through the factory's per-sweep cache): only live Node2D members of the
## group count; members that are not Node2D, queued for deletion, outside
## the tree, at a non-finite position or in another World2D are skipped;
## an empty group is a dormant zone (no warning); up to 64 targets graze
## (a member that joins the group after the shot, even the fifth, is picked
## up on the next tick); more than 64 keeps the first 64 in tree order and
## warns once; a target keeps its slot while others come and go (no
## spurious exits), every slot up to 63 names its target on exit; volleys
## over one group share one scan per tick; a respawned target is picked up
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


func test_a_fifth_member_joining_after_the_shot_is_grazed_next_tick() -> void:
	for i in 4: # four members far away take the first slots
		make_graze_target(Vector2(-1000 - 100 * i, 0))
	var log := H.record_graze(factory)
	_resting_bullet_in_zone()
	step_factory(2)
	assert_eq(log, [], "nothing near the bullet yet")
	var late := make_graze_target(Vector2(10, 0))
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the fifth member grazes on the next tick")
	assert_eq(log[0][1], late, "named")


func test_every_one_of_64_targets_grazes_and_names_itself_on_exit() -> void:
	var targets: Array = []
	var points: Array = []
	for i in 64: # one target per bullet, 100 px apart (rings never overlap)
		targets.append(make_graze_target(Vector2(100 * i + 10, 0)))
		points.append(Vector2(100 * i, 0))
	var v := graze_volley(H.transforms_at(points), 0.0)
	v.graze_set_zones([H.make_graze_zone([20.0])])
	var log := H.record_graze(factory)
	step_factory()
	assert_eq(log.size(), 64, "64 grazes")
	var named := true
	for e in log:
		named = named and e[0] == "enter" and e[1] == targets[e[3]]
	assert_true(named, "each bullet grazed its own target")
	log.clear()
	for t in targets:
		(t as Node2D).position.y = 500.0
	step_factory()
	assert_eq(log.size(), 64, "64 exits")
	var exits_named := true
	for e in log:
		exits_named = exits_named and e[0] == "exit" and e[1] == targets[e[3]]
	assert_true(exits_named, "every exit names its own target, slots past 3 included")


func test_more_than_64_targets_keeps_the_first_64_and_warns_once() -> void:
	for i in 64:
		make_graze_target(Vector2(500 + 50 * i, 0))
	var last := make_graze_target(Vector2(10, 0))
	var log := H.record_graze(factory)
	_resting_bullet_in_zone()
	step_factory(3)
	expect_warning_sequence(["BulletGrazeZone2D: group 'graze_targets' holds 65 graze targets; only the first 64 in tree order are tested."])
	assert_eq(log, [], "the 65th target (last in tree order) is not tested")
	var ids: Array = []
	for t in factory.debug_get_graze_targets(&"graze_targets"):
		ids.append(t["id"])
	assert_eq(ids.size(), 64, "64 targets")
	assert_false(ids.has(last.get_instance_id()), "not the 65th")


func test_a_target_keeps_its_slot_while_others_come_and_go() -> void:
	# A visit belongs to the target of its latest new graze. If a change
	# elsewhere in the list moved that target's slot, the visit would move
	# to the NEAREST target it is inside and the exit would name that one.
	var first := make_graze_target(Vector2(-500, 0)) # slot 0
	var held := make_graze_target(Vector2(10, 0)) # slot 1
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	v.graze_set_zones([H.make_graze_zone([20.0])])
	var log := H.record_graze(factory)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the visit belongs to the held target")
	var nearer := make_graze_target(Vector2(-5, 0)) # inside too, nearer; Once: no new graze
	step_factory()
	first.free() # the slot before the held target empties
	step_factory()
	for i in 3: # newcomers take free slots
		make_graze_target(Vector2(-600 - 100 * i, 0))
	step_factory(2)
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "nothing else happened")
	held.position = Vector2(300, 0)
	nearer.position = Vector2(-300, 0)
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "exit:0:0"], "leaving exits once")
	assert_eq(log[1][1], held, "naming the held target, not the nearer one")


func test_volleys_over_one_group_share_one_scan_per_tick() -> void:
	make_graze_target(Vector2(10, 0))
	for i in 3:
		graze_volley(H.transforms_at([Vector2(0, 100 * i)]), 0.0).graze_set_zones([H.make_graze_zone([20.0])])
	step_factory()
	var before: int = factory.debug_get_graze_stats()["refreshes"]
	step_factory(4)
	assert_eq(int(factory.debug_get_graze_stats()["refreshes"]) - before, 4, "one group scan per tick, whatever the volley count")


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
