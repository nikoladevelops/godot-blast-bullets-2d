extends SceneTree
## Wave-1 regression suite: R1/R2/R3/R4/R5/R6 + small diffs.
## Run: godot --headless --path test_project --script tests/spawner/test_wave1_regressions.gd

const H := preload("res://tests/common/blast_test_helpers.gd")
var failures := 0
func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)
func _is_editor_visible(spawner: BulletSpawner2D, prop: String) -> bool:
	for p in spawner.get_property_list():
		if str(p.get("name", "")) == prop:
			return (int(p.get("usage", 0)) & PROPERTY_USAGE_EDITOR) != 0
	return false
func _rot(speed: float) -> BulletRotationData2D:
	var r := BulletRotationData2D.new()
	r.rotation_speed = speed
	r.max_rotation_speed = 3000.0
	r.rotation_acceleration = 0.0
	return r
func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	printerr("W1 R1 fire-arc uses EFFECTIVE GENERATOR frame")
	var spawner := BulletSpawner2D.new()
	get_root().add_child(spawner)
	await process_frame
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(H.make_directional_data(4))
	spawner.set_shooting_enabled(false)
	spawner.pattern_source = 3
	spawner.helper_bullets_amount = 4
	var foe := Node2D.new()
	foe.name = "W1Foe"
	foe.position = Vector2(300, 0)
	foe.add_to_group("w1swarm")
	get_root().add_child(foe)
	await process_frame
	spawner.set_homing_enabled(true)
	spawner.set_homing_target_source(0)
	spawner.set_homing_node_group("w1swarm")
	spawner.set_homing_fire_arc_deg(20.0)
	var gen := Node2D.new()
	gen.position = Vector2(200, 0)
	gen.rotation = 0.0
	get_root().add_child(gen)
	await process_frame
	spawner.set_transforms_generator(gen)
	spawner.position = Vector2(0, 0)
	spawner.rotation = PI
	var fb: int = spawner.get_volleys_fired()
	var ok1: bool = spawner.shoot_once()
	_check(ok1 and spawner.get_volleys_fired() == fb + 1, "R1 generator-cone shot fires though spawner faces away")
	gen.rotation = PI
	spawner.rotation = 0.0
	var fb2: int = spawner.get_volleys_fired()
	var ok2: bool = spawner.shoot_once()
	_check(ok2 == false and spawner.get_volleys_fired() == fb2, "R1 outside-generator-cone skipped though spawner faces foe")
	spawner.set_transforms_generator(null)
	spawner.set_homing_fire_arc_deg(0.0)
	gen.queue_free()
	printerr("W1 R2 rotation presence survives same-owner wake")
	var d2 := H.make_directional_data(2, 0.0)
	d2.all_bullet_rotation_data = [_rot(77.0), _rot(33.0)]
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d2)
	v2.set_shared_bullet_rotation_data(_rot(999.0))
	var rb: float = float(v2.debug_get_bullet_info(0)["rotation_speed"])
	_check(absf(rb - 77.0) < 0.5, "R2 authored 77 kept, got %.2f" % rb)
	v2.disable_bullet(0)
	await physics_frame
	v2.wake_bullet(0)
	await physics_frame
	v2.set_shared_bullet_rotation_data(_rot(555.0))
	var ra: float = float(v2.debug_get_bullet_info(0)["rotation_speed"])
	var rs: float = float(v2.debug_get_bullet_info(1)["rotation_speed"])
	_check(absf(ra - 77.0) < 1.0, "R2 wake preserves authored entry, got %.2f" % ra)
	_check(absf(rs - 33.0) < 1.0, "R2 sibling untouched, got %.2f" % rs)
	printerr("W1 R3 stagger preserved across unrelated setters")
	spawner.set_homing_enabled(true)
	spawner.set_homing_retarget_mode(1)
	spawner.set_homing_retarget_interval_sec(0.5)
	spawner.set_homing_retarget_phase(0.4)
	await process_frame
	var cd0: float = spawner.debug_get_retarget_countdown()
	spawner.set_homing_enabled(true)
	var cd1: float = spawner.debug_get_retarget_countdown()
	_check(absf(cd1 - cd0) < 0.001, "R3 no-op enable preserves %.3f->%.3f" % [cd0, cd1])
	spawner.set_homing_retarget_mode(1)
	var cd2: float = spawner.debug_get_retarget_countdown()
	_check(absf(cd2 - cd0) < 0.001, "R3 no-op mode preserves %.3f->%.3f" % [cd0, cd2])
	spawner.set_homing_retarget_previous_volleys(not spawner.get_homing_retarget_previous_volleys())
	var cd3: float = spawner.debug_get_retarget_countdown()
	_check(absf(cd3 - cd0) < 0.001, "R3 unrelated flag preserves %.3f->%.3f" % [cd0, cd3])
	spawner.set_homing_retarget_interval_sec(5.0)
	var cd4: float = spawner.debug_get_retarget_countdown()
	_check(cd4 <= 5.0 + 0.001, "R3 longer interval clamps, got %.3f" % cd4)
	spawner.set_homing_retarget_interval_sec(0.5)
	spawner.set_homing_retarget_phase(0.12)
	var cd5: float = spawner.debug_get_retarget_countdown()
	_check(absf(cd5 - 0.12) < 0.001, "R3 phase edit resets, got %.3f" % cd5)
	printerr("W1 R4 gravity fill-gaps")
	var d4b := H.make_directional_data(3, 0.0)
	d4b.all_bullet_gravity = [Vector2(1000, 0)] # strict: slot 0 authored, slots 1-2 genuine gaps
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4b)
	v4.set_gravity(Vector2(0, 2000))
	_check(v4.bullet_get_gravity(0) == Vector2(1000, 0), "R4 authored +X survives")
	_check(v4.bullet_get_gravity(1) == Vector2(0, 2000), "R4 gap1 takes shared")
	_check(v4.bullet_get_gravity(2) == Vector2(0, 2000), "R4 gap2 takes shared")
	v4.bullet_set_gravity(1, Vector2(0, 0)) # deliberate zero claims presence on former gap
	v4.set_gravity(Vector2(0, 777))
	_check(v4.bullet_get_gravity(1) == Vector2(0, 0), "R4 deliberate zero survives")
	_check(v4.bullet_get_gravity(2) == Vector2(0, 777), "R4 still-gap slot follows new shared")
	_check(v4.bullet_get_gravity(0) == Vector2(1000, 0), "R4 seed-authored slot still intact")
	v4.set_gravity(Vector2(NAN, 0))
	_check(v4.bullet_get_gravity(2) == Vector2(0, 777), "R4 NaN rejected")
	printerr("W1 R5 outline_distribution gating")
	spawner.pattern_source = 25
	_check(not _is_editor_visible(spawner, "helper_outline_distribution"), "R5 hidden on CIRCLE")
	spawner.pattern_source = 9
	_check(not _is_editor_visible(spawner, "helper_outline_distribution"), "R5 hidden on ELLIPSE")
	spawner.pattern_source = 26
	_check(_is_editor_visible(spawner, "helper_outline_distribution"), "R5 shown on RECTANGLE")
	spawner.pattern_source = 28
	_check(_is_editor_visible(spawner, "helper_outline_distribution"), "R5 shown on POLYGON")
	spawner.pattern_source = 15
	_check(_is_editor_visible(spawner, "helper_outline_distribution"), "R5 shown on STAR")
	printerr("W1 R6 linear orbit re-arm full params")
	var tgt := Node2D.new()
	tgt.position = Vector2(400, 0)
	get_root().add_child(tgt)
	await process_frame
	var v6: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(3, 250.0))
	v6.set_homing_smoothing(6.0)
	v6.all_bullets_push_back_homing_target(tgt)
	v6.all_bullets_enable_orbiting(64.0, 2, 0)
	_check(v6.bullet_is_orbiting_enabled(0) and v6.bullet_is_orbiting_enabled(2), "R6 initial arm holds")
	v6.all_bullets_enable_orbiting_linear(40.0, 10.0, 1, 1, 0, -1, 0, 8.0, 0, false)
	var radii: Array = v6.all_bullets_get_orbiting_radius()
	_check(radii.size() == 3 and absf(float(radii[0]) - 40.0) < 0.5 and absf(float(radii[2]) - 60.0) < 0.5, "R6 radii 40/50/60")
	_check(v6.bullet_get_orbiting_direction(0) == 1, "R6 direction re-armed")
	_check(v6.bullet_get_orbiting_texture_rotation(1) == 1, "R6 texture re-armed")
	_check(v6.bullet_get_orbiting_follow_deadzone(2) == 8.0, "R6 deadzone re-armed")
	_check(v6.bullet_get_orbiting_rigid_follow(0) == false, "R6 rigid off re-armed")
	tgt.queue_free()
	printerr("W1 small diffs")
	var va: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 100.0))
	va.clear_all_bullets()
	await process_frame
	await process_frame
	var vb: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 100.0))
	var stats: Dictionary = factory.debug_get_pool_hit_stats()
	_check(int(stats.get("directional_hits", 0)) >= 0, "R10 stats readable")
	_check(vb.get_amount_bullets() == 2, "R10 reuse intact")
	spawner.set_pattern_source(8)
	_check(_is_editor_visible(spawner, "helper_flower_type"), "3.3 flower type visible")
	var pf: int = spawner.get_volleys_fired()
	spawner.apply_pattern_preset(-1)
	spawner.apply_pattern_preset(9999)
	_check(spawner.get_volleys_fired() == pf, "5.4 bad preset no-op")
	foe.queue_free()
	factory.reset()
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling")
	spawner.queue_free()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL WAVE1 REGRESSION TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
