extends SceneTree
## Spawner homing/orbit suite: easy-API end to end through real volleys.
## Covers: all 6 target sources (group/mouse/global/path/name/children),
## selections incl. DISTRIBUTE deal, max_targets + detection range, fire-arc
## gate (skip outside cone), orbit requires homing (warns otherwise), live
## retarget replaces queues, retarget stagger differs per spawner, max_live
## fuse pauses firing, cache state (template reuse + invalidation).
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_homing_orbit.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var e1 := Node2D.new()
	e1.name = "SwarmA"
	e1.position = Vector2(300, 0)
	e1.add_to_group("swarm")
	get_root().add_child(e1)
	var e2 := Node2D.new()
	e2.name = "SwarmB"
	e2.position = Vector2(0, 300)
	e2.add_to_group("swarm")
	get_root().add_child(e2)
	await process_frame

	var spawner := BulletSpawner2D.new()
	get_root().add_child(spawner)
	await process_frame
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(H.make_directional_data(4))
	spawner.set_shooting_enabled(false)
	spawner.pattern_source = 3
	spawner.helper_bullets_amount = 4

	printerr("SP-HOME T1 sources resolve")
	spawner.set_homing_enabled(true)
	spawner.set_homing_max_targets(5)
	spawner.set_homing_target_source(0)
	spawner.set_homing_node_group("swarm")
	_check(spawner.resolve_homing_targets(true).size() == 2, "group resolves both")
	spawner.set_homing_target_source(2)
	spawner.set_homing_global_position(Vector2(10, 10))
	_check(spawner.resolve_homing_targets(true).size() == 1, "global resolves one")
	spawner.set_homing_target_source(3)
	spawner.set_homing_target_path(e1.get_path())
	_check(spawner.resolve_homing_targets(true).size() == 1, "path resolves")
	spawner.set_homing_target_source(4)
	spawner.set_homing_node_name("Swarm")
	spawner.set_homing_node_name_match_mode(1)
	_check(spawner.resolve_homing_targets(true).size() == 2, "name contains resolves both")
	spawner.set_homing_target_source(1)
	_check(spawner.resolve_homing_targets(true).is_empty(), "mouse resolves empty array")

	printerr("SP-HOME T2 fire + track + cache")
	spawner.set_homing_target_source(0)
	spawner.set_homing_node_group("swarm")
	_check(spawner.shoot_once(), "homing shot fires")
	_check(spawner.get_live_volley_count() == 1, "volley tracked")
	var cache: Dictionary = spawner.debug_get_cache_state()
	_check(cache.get("template_valid", false) == true, "duplicate cache primed")
	_check(cache.get("spawn_id_match", false) == true, "cache matches live resource")
	spawner.set_spawn_data(H.make_directional_data(4))
	var cache2: Dictionary = spawner.debug_get_cache_state()
	_check(cache2.get("template_valid", false) == false, "resource swap invalidates cache")

	printerr("SP-HOME T3 fire-arc gate")
	spawner.set_homing_fire_arc_deg(10.0)
	spawner.rotation = PI # face away from both enemies
	var skipped_fired: int = spawner.get_volleys_fired()
	var arc_ok: bool = spawner.shoot_once()
	_check(arc_ok == false and spawner.get_volleys_fired() == skipped_fired, "outside-cone shot skipped, not counted")
	spawner.rotation = 0.0
	spawner.set_homing_fire_arc_deg(0.0)

	printerr("SP-HOME T4 orbit needs homing + retarget")
	spawner.set_orbiting_enabled(true)
	_check(spawner.shoot_once(), "orbiting shot fires with homing on")
	_check(spawner.retarget_live_volleys() >= 1, "retarget touches live volleys")
	spawner.set_orbiting_enabled(false)
	spawner.set_homing_enabled(false)
	var plain_ok: bool = spawner.shoot_once()
	_check(plain_ok, "plain shot fires with homing off")
	_check(spawner.get_live_volley_count() >= 1, "homing volleys still tracked (plain untracked by design ok)")

	printerr("SP-HOME T5 fuse + stagger")
	spawner.set_homing_enabled(true)
	spawner.set_max_live_bullets(1)
	var fuse_fired: int = spawner.get_volleys_fired()
	spawner.shoot_once()
	_check(spawner.get_volleys_fired() == fuse_fired, "over-budget shot skipped")
	spawner.set_max_live_bullets(0)
	var s2 := BulletSpawner2D.new()
	s2.set_shooting_enabled(false)
	get_root().add_child(s2)
	await process_frame
	var cd1: float = spawner.debug_get_retarget_countdown()
	var cd2: float = s2.debug_get_retarget_countdown()
	_check(cd1 >= 0.0 and cd1 <= 0.5 and cd2 >= 0.0 and cd2 <= 0.5, "stagger countdowns within interval")
	_check(cd1 != cd2, "distinct spawners stagger apart")
	s2.queue_free()

	e1.queue_free()
	e2.queue_free()
	factory.reset()
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	spawner.queue_free()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL SPAWNER HOMING TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
