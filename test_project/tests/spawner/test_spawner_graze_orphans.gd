extends BlastTest
## Graze after the spawner is gone (orphaned_volleys): with Keep Flying
## (default) and Hand To Factory the flying bullets keep grazing and every
## graze still reaches BulletFactory2D (the zones live on the volley, the
## targets come from the factory); Clear and Remove leave nothing to graze.
## A spawner freed from inside its own graze handler still lets the factory
## hear that graze, then its policy applies. No warning is involved: graze
## never needed the spawner.


## A spawner the tests free themselves, one resting bullet per shot, armed
## with one 20 px zone over the default group.
func _spawner(policy: int) -> BulletSpawner2D:
	var d := H.make_volley_data(1, 0.0, 60.0)
	d.collision_shape = H.make_circle_shape(4.0)
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.set_homing_enabled(false)
	sp.set_spawn_data(d)
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
	sp.helper_bullets_amount = 1
	sp.orphaned_volleys = policy
	sp.graze_zones = [H.make_graze_zone([20.0])]
	sp.graze_enabled = true
	sp.graze_node_group = &"graze_targets"
	add_child(sp)
	sp.set_bullet_factory(factory)
	return sp


func _shoot(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fired")
	return got[0]


func _graze_after_free(policy: int) -> Array:
	var t := make_graze_target(Vector2(100, 0))
	var sp := _spawner(policy)
	var log := H.record_graze(factory)
	var v := _shoot(sp)
	step_factory()
	sp.free()
	t.position = Vector2(10, 0)
	step_factory()
	t.position = Vector2(100, 0)
	step_factory()
	if is_instance_valid(v) and v.debug_get_life_state() == "active":
		assert_true(v.is_graze_armed(), "the orphaned volley kept its zones")
	return H.graze_kinds(log)


func test_keep_flying_bullets_still_graze_on_the_factory() -> void:
	assert_eq(_graze_after_free(BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING), ["enter:0:0", "exit:0:0"], "enter and exit reach the factory")


func test_hand_to_factory_bullets_graze_on_the_factory() -> void:
	assert_eq(_graze_after_free(BulletSpawner2D.ORPHANED_VOLLEYS_HAND_TO_FACTORY), ["enter:0:0", "exit:0:0"], "factory-owned now, still grazing")


func test_clear_and_remove_leave_nothing_to_graze() -> void:
	for policy in [BulletSpawner2D.ORPHANED_VOLLEYS_CLEAR, BulletSpawner2D.ORPHANED_VOLLEYS_REMOVE]:
		assert_eq(_graze_after_free(policy), [], "policy %d: the bullets went with the spawner" % policy)


func test_spawner_freed_inside_its_own_graze_handler() -> void:
	make_graze_target(Vector2(10, 0))
	var keep := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING)
	var clear := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_CLEAR)
	for sp in [keep, clear]:
		sp.bullet_grazed.connect(func(_t: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
			sp.queue_free())
	var factory_log := H.record_graze(factory)
	var kept := _shoot(keep)
	var cleared := _shoot(clear)
	step_factory()
	assert_eq(factory_log.size(), 2, "the factory heard both grazes after each spawner's own handler")
	await idle(1)
	assert_false(is_instance_valid(keep) or is_instance_valid(clear), "both spawners freed at the end of the frame")
	assert_eq(kept.debug_get_life_state(), "active", "Keep Flying: the bullet flies on")
	assert_eq(cleared.debug_get_life_state(), "pooled", "Clear: the bullet went with its spawner")
