extends BlastTest
## Reparenting (remove + add) keeps a spawner's state: nodes assigned by
## pointer, tracked homing volleys, running burst chains and pattern lists
## survive and simply resume. A node assigned while out of the tree replaces
## the old path instead of reverting to it. Spawners enabling homing on the
## same frame keep a staggered retarget clock.


func _reparent(node: Node, new_parent: Node) -> void:
	node.get_parent().remove_child(node)
	new_parent.add_child(node)


func test_pointer_assigned_factory_survives_reparenting() -> void:
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.set_homing_enabled(false)
	sp.set_spawn_data(H.make_directional_data(2, 50.0, 30.0))
	sp.set_bullet_factory(factory) # spawner not in the tree yet
	add(sp)
	assert_true(sp.shoot_once(), "fires after entering the tree")
	var holder := Node2D.new()
	add(holder)
	_reparent(sp, holder)
	assert_true(sp.shoot_once(), "still wired after a reparent")
	assert_eq(sp.bullet_factory_path, sp.get_path_to(factory), "the path was filled in for saving")


func test_generator_assigned_out_of_tree_replaces_the_old_path() -> void:
	var sp := make_spawner()
	var a := Node2D.new()
	a.position = Vector2(100, 0)
	add(a)
	var b := Node2D.new()
	b.position = Vector2(-100, 0)
	add(b)
	sp.set_transforms_generator(a)
	var parent := sp.get_parent()
	parent.remove_child(sp)
	sp.set_transforms_generator(b) # spawner out of the tree
	parent.add_child(sp)
	assert_eq(sp.get_transforms_generator(), b, "the newer assignment wins")
	parent.remove_child(sp)
	parent.add_child(sp)
	assert_eq(sp.get_transforms_generator(), b, "and survives another round trip")
	assert_eq(sp.transforms_generator, sp.get_path_to(b), "the stored path follows")


func test_tracked_homing_volleys_survive_reparenting() -> void:
	var sp := make_spawner(H.make_directional_data(2, 50.0, 30.0))
	var target := Node2D.new()
	target.position = Vector2(300, 0)
	add(target)
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	sp.set_homing_target_path(sp.get_path_to(target))
	assert_true(sp.shoot_once(), "homing shot")
	var holder := Node2D.new()
	add(holder)
	_reparent(sp, holder)
	sp.set_homing_target_path(sp.get_path_to(target))
	assert_eq(sp.get_live_volley_count(), 1, "still tracking the flying volley")
	assert_eq(sp.retarget_live_volleys(), 1, "and can still steer it")


func test_burst_chain_resumes_after_reparenting() -> void:
	var sp := make_spawner(H.make_directional_data(1, 50.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.burst_enabled = true
	sp.burst_count = 4
	sp.burst_interval_sec = 0.05
	watch_signals(sp)
	sp.begin_burst()
	for i in 30:
		await idle(1)
		if get_signal_emit_count(sp, "burst_shot_fired") >= 1:
			break
	var holder := Node2D.new()
	add(holder)
	_reparent(sp, holder)
	for i in 60:
		await idle(1)
		if get_signal_emit_count(sp, "burst_finished") >= 1:
			break
	assert_signal_emit_count(sp, "burst_shot_fired", 4, "the chain completed after the reparent")
	assert_signal_emit_count(sp, "burst_finished", 1, "finished once")


func test_pattern_list_resumes_after_reparenting() -> void:
	var sp := make_spawner(H.make_directional_data(2, 50.0, 30.0))
	watch_signals(sp)
	sp.spawn_pattern_list([{"helper_bullets_amount": 2}, {"helper_bullets_amount": 3}, {"helper_bullets_amount": 4}], false, 0.05)
	for i in 30:
		await idle(1)
		if get_signal_emit_count(sp, "volley_fired") >= 1:
			break
	var holder := Node2D.new()
	add(holder)
	_reparent(sp, holder)
	for i in 60:
		await idle(1)
		if get_signal_emit_count(sp, "pattern_list_finished") >= 1:
			break
	assert_signal_emit_count(sp, "volley_fired", 3, "every entry fired")
	assert_signal_emit_count(sp, "pattern_list_finished", 1, "finished once")


func test_spawners_enabling_homing_together_stay_staggered() -> void:
	var a := make_spawner()
	var b := make_spawner()
	a.set_homing_retarget_interval_sec(0.5)
	b.set_homing_retarget_interval_sec(0.5)
	a.set_homing_enabled(true)
	b.set_homing_enabled(true)
	assert_ne(a.debug_get_retarget_countdown(), b.debug_get_retarget_countdown(), "different instances, different first pass")
	a.set_homing_retarget_phase(0.2)
	assert_eq(a.debug_get_retarget_countdown(), 0.2, "an explicit phase is used verbatim")
