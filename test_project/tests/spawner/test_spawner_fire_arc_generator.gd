extends BlastTest
## The fire-arc gate reads the muzzle (effective generator), not the spawner
## node: with an external marker the old gate skipped valid shots and
## admitted invalid ones. A self-generated spawner is unchanged.


func _arc_spawner(gen: Node2D) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	if gen != null:
		sp.set_transforms_generator(gen)
	sp.homing_enabled = true
	sp.homing_target_source = BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION
	sp.homing_fire_arc_deg = 60.0
	watch_signals(sp)
	return sp


func _marker() -> Node2D:
	# Faces +Y from (200, 0); the spawner sits at the origin facing +X.
	var gen: Node2D = add(Node2D.new())
	gen.position = Vector2(200, 0)
	gen.rotation = PI / 2.0
	return gen


func test_target_in_generator_arc_fires() -> void:
	var sp := _arc_spawner(_marker())
	await idle(1)
	sp.homing_global_position = Vector2(200, 200)
	assert_true(sp.shoot_once(), "shot fires (old code skipped it)")
	assert_signal_emit_count(sp, "volley_fired", 1)
	assert_signal_not_emitted(sp, "volley_skipped")


func test_target_outside_generator_arc_is_skipped() -> void:
	var sp := _arc_spawner(_marker())
	await idle(1)
	sp.homing_global_position = Vector2(400, 0) # ahead of the spawner, 90 deg off the marker
	assert_false(sp.shoot_once(), "shot skipped (old code fired it)")
	assert_signal_emit_count(sp, "volley_skipped", 1)


func test_self_generated_spawner_unchanged() -> void:
	var sp := _arc_spawner(null)
	sp.homing_global_position = Vector2(300, 0)
	assert_true(sp.shoot_once(), "dead-ahead target fires")
	sp.homing_global_position = Vector2(0, 300)
	assert_false(sp.shoot_once(), "behind-arc target skipped")


## The gate resolves the targets before the volley exists and the shot reuses
## them: a pre_shoot handler that frees one of them must never hand the freed
## node to the volley's queue or to homing_targets_resolved (it was carried
## across the user code as a raw Variant).
func test_a_target_freed_in_pre_shoot_never_reaches_the_volley() -> void:
	var keep: Node2D = add(Node2D.new())
	keep.position = Vector2(150, 0)
	keep.add_to_group(&"arc_foes")
	var doomed := Node2D.new()
	doomed.position = Vector2(100, 0)
	doomed.add_to_group(&"arc_foes")
	add_child(doomed)
	var sp := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.homing_enabled = true
	sp.homing_node_group = &"arc_foes"
	sp.homing_max_targets = 2
	sp.homing_fire_arc_deg = 90.0
	var resolved: Array = []
	var fired: Array = []
	sp.pre_shoot.connect(func(_v, _n) -> void:
		if is_instance_valid(doomed):
			doomed.free())
	sp.homing_targets_resolved.connect(func(_v, t: Array) -> void: resolved.append_array(t))
	sp.volley_fired.connect(func(v, _n) -> void: fired.append(v))
	assert_true(sp.shoot_once(), "the shot fires")
	expect_no_errors("the freed target is dropped quietly")
	assert_eq(resolved, [keep], "only the live target is reported")
	assert_eq(fired.size(), 1, "one volley")
	if fired.size() == 1:
		assert_eq((fired[0] as BulletVolley2D).shared_homing_deque_check_homing_targets_amount(), 1, "only the live target is queued")
