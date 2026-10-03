extends BlastTest
## Wave-1 regression pins: fire arc in the generator frame, rotation presence
## surviving a same-owner wake, retarget stagger preserved across no-op
## setters, gravity fill-gaps, outline-distribution gating, linear orbit
## re-arm, bad presets are no-ops.


func _rot(speed: float) -> BulletRotationData2D:
	var r := BulletRotationData2D.new()
	r.rotation_speed = speed
	r.max_rotation_speed = 3000.0
	return r


func test_fire_arc_uses_generator_frame() -> void:
	var sp := make_spawner(H.make_directional_data(4))
	var foe: Node2D = add(Node2D.new())
	foe.position = Vector2(300, 0)
	foe.add_to_group("w1swarm")
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
	sp.set_homing_node_group("w1swarm")
	sp.set_homing_fire_arc_deg(20.0)
	var gen: Node2D = add(Node2D.new())
	gen.position = Vector2(200, 0)
	await idle(1)
	sp.set_transforms_generator(gen)
	sp.rotation = PI
	assert_true(sp.shoot_once(), "generator-cone shot fires though the spawner faces away")
	gen.rotation = PI
	sp.rotation = 0.0
	assert_false(sp.shoot_once(), "outside the generator cone skipped though the spawner faces the foe")
	assert_eq(sp.get_volleys_fired(), 1)


func test_rotation_presence_survives_same_owner_wake() -> void:
	var d := H.make_directional_data(2, 0.0)
	d.all_bullet_rotation_data = [_rot(77.0), _rot(33.0)]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	v.set_shared_bullet_rotation_data(_rot(999.0))
	assert_almost_eq(float(v.debug_get_bullet_info(0)["rotation_speed"]), 77.0, 0.5, "authored 77 kept")
	v.disable_bullet(0)
	await physics()
	v.wake_bullet(0)
	await physics()
	v.set_shared_bullet_rotation_data(_rot(555.0))
	assert_almost_eq(float(v.debug_get_bullet_info(0)["rotation_speed"]), 77.0, 1.0, "wake preserves the authored entry")
	assert_almost_eq(float(v.debug_get_bullet_info(1)["rotation_speed"]), 33.0, 1.0, "sibling untouched")


func test_stagger_preserved_across_noop_setters() -> void:
	var sp := make_spawner()
	sp.set_homing_enabled(true)
	sp.set_homing_retarget_mode(1)
	sp.set_homing_retarget_interval_sec(0.5)
	sp.set_homing_retarget_phase(0.4)
	await idle(1)
	var cd0: float = sp.debug_get_retarget_countdown()
	sp.set_homing_enabled(true)
	assert_almost_eq(sp.debug_get_retarget_countdown(), cd0, 0.001, "no-op enable preserves")
	sp.set_homing_retarget_mode(1)
	assert_almost_eq(sp.debug_get_retarget_countdown(), cd0, 0.001, "no-op mode preserves")
	sp.set_homing_retarget_previous_volleys(not sp.get_homing_retarget_previous_volleys())
	assert_almost_eq(sp.debug_get_retarget_countdown(), cd0, 0.001, "unrelated flag preserves")
	sp.set_homing_retarget_interval_sec(5.0)
	assert_lte(sp.debug_get_retarget_countdown(), 5.001, "longer interval clamps")
	sp.set_homing_retarget_interval_sec(0.5)
	sp.set_homing_retarget_phase(0.12)
	assert_almost_eq(sp.debug_get_retarget_countdown(), 0.12, 0.001, "phase edit resets")


func test_gravity_fills_gaps_only() -> void:
	var d := H.make_directional_data(3, 0.0)
	d.all_bullet_gravity = [Vector2(1000, 0)]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	v.set_gravity(Vector2(0, 2000))
	assert_eq(v.bullet_get_gravity(0), Vector2(1000, 0), "authored +X survives")
	assert_eq(v.bullet_get_gravity(1), Vector2(0, 2000), "gap 1 takes shared")
	assert_eq(v.bullet_get_gravity(2), Vector2(0, 2000), "gap 2 takes shared")
	v.bullet_set_gravity(1, Vector2.ZERO)
	v.set_gravity(Vector2(0, 777))
	assert_eq(v.bullet_get_gravity(1), Vector2.ZERO, "deliberate zero survives")
	assert_eq(v.bullet_get_gravity(2), Vector2(0, 777), "still-gap slot follows the new shared")
	assert_eq(v.bullet_get_gravity(0), Vector2(1000, 0), "seed-authored slot intact")
	v.set_gravity(Vector2(NAN, 0))
	expect_error_sequence(["set_gravity: value must be finite, keeping the old value"])
	assert_eq(v.bullet_get_gravity(2), Vector2(0, 777), "NaN rejected")


func test_outline_distribution_gating() -> void:
	var sp := make_spawner()
	for src in [BulletSpawner2D.PATTERN_FROM_HELPER_CIRCLE, BulletSpawner2D.PATTERN_FROM_HELPER_ELLIPSE]:
		sp.pattern_source = src
		assert_false(is_editor_visible(sp, &"helper_outline_distribution"), "hidden on source %d" % src)
	for src in [BulletSpawner2D.PATTERN_FROM_HELPER_RECTANGLE, BulletSpawner2D.PATTERN_FROM_HELPER_POLYGON, BulletSpawner2D.PATTERN_FROM_HELPER_STAR]:
		sp.pattern_source = src
		assert_true(is_editor_visible(sp, &"helper_outline_distribution"), "shown on source %d" % src)
	sp.set_pattern_source(BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER)
	assert_true(is_editor_visible(sp, &"helper_flower_type"), "flower type visible")


func test_linear_orbit_rearm_full_params() -> void:
	var tgt: Node2D = add(Node2D.new())
	tgt.position = Vector2(400, 0)
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(3, 250.0))
	v.set_homing_smoothing(6.0)
	v.all_bullets_push_back_homing_target(tgt)
	v.all_bullets_enable_orbiting(64.0, DirectionalBullets2D.OrbitRight, DirectionalBullets2D.FaceTarget)
	assert_true(v.bullet_is_orbiting_enabled(0) and v.bullet_is_orbiting_enabled(2), "initial arm holds")
	v.all_bullets_enable_orbiting_linear(40.0, 10.0, 1, 1, 0, -1, 0, 8.0, 0, false)
	var radii: Array = v.all_bullets_get_orbiting_radius()
	assert_eq(radii.size(), 3)
	assert_almost_eq(float(radii[0]), 40.0, 0.5)
	assert_almost_eq(float(radii[2]), 60.0, 0.5)
	assert_eq(v.bullet_get_orbiting_direction(0), 1, "direction re-armed")
	assert_eq(v.bullet_get_orbiting_texture_rotation(1), 1, "texture rotation re-armed")
	assert_eq(v.bullet_get_orbiting_follow_deadzone(2), 8.0, "deadzone re-armed")
	assert_false(v.bullet_get_orbiting_rigid_follow(0), "rigid follow off re-armed")


func test_bad_presets_are_noops_and_reuse_intact() -> void:
	var sp := make_spawner()
	sp.apply_pattern_preset(BulletFactory2D.PATTERN_PRESET_CUSTOM) # -1: valid "custom", a no-op
	sp.apply_pattern_preset(9999)
	expect_error("preset out of range")
	assert_eq(sp.get_volleys_fired(), 0, "bad presets fire nothing")
	var va: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 100.0))
	va.clear_all_bullets()
	await idle()
	var vb: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 100.0))
	assert_eq(vb.get_amount_bullets(), 2, "reuse intact after a clear")
