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
