extends BlastTest
## Spawner homing/orbit easy API through real volleys: target sources
## (group / global / path / name / mouse), firing + tracking + duplicate
## cache, the fire-arc gate, orbit needing homing + live retarget, the
## live-bullet fuse, retarget stagger per spawner.

var sp: BulletSpawner2D
var e1: Node2D
var e2: Node2D


func before_each() -> void:
	await super()
	e1 = add(Node2D.new())
	e1.name = "SwarmA"
	e1.position = Vector2(300, 0)
	e1.add_to_group("swarm")
	e2 = add(Node2D.new())
	e2.name = "SwarmB"
	e2.position = Vector2(0, 300)
	e2.add_to_group("swarm")
	sp = make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.set_homing_enabled(true)
	sp.set_homing_max_targets(5)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
	sp.set_homing_node_group("swarm")
	await idle(1)


func test_sources_resolve() -> void:
	assert_eq(sp.resolve_homing_targets(true).size(), 2, "group resolves both")
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	sp.set_homing_global_position(Vector2(10, 10))
	assert_eq(sp.resolve_homing_targets(true).size(), 1, "global resolves one")
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	sp.set_homing_target_path(e1.get_path())
	assert_eq(sp.resolve_homing_targets(true).size(), 1, "path resolves")
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_NAME)
	sp.set_homing_node_name("Swarm")
	sp.set_homing_node_name_match_mode(BulletSpawner2D.HOMING_NAME_MATCH_CONTAINS)
	assert_eq(sp.resolve_homing_targets(true).size(), 2, "name-contains resolves both")
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_MOUSE)
	assert_true(sp.resolve_homing_targets(true).is_empty(), "mouse resolves an empty array")


func test_fire_track_and_cache() -> void:
	assert_true(sp.shoot_once(), "homing shot fires")
	assert_eq(sp.get_live_volley_count(), 1, "volley tracked")
	var cache: Dictionary = sp.debug_get_pattern_cache_info()
	assert_true(cache.get("template_valid", false), "duplicate cache primed")
	assert_true(cache.get("spawn_id_match", false), "cache matches the live resource")
	sp.set_spawn_data(H.make_volley_data(4))
	assert_false(sp.debug_get_pattern_cache_info().get("template_valid", true), "resource swap invalidates the cache")


func test_fire_arc_gate() -> void:
	sp.set_homing_fire_arc_deg(10.0)
	sp.rotation = PI # face away from both enemies
	watch_signals(sp)
	assert_false(sp.shoot_once(), "outside-cone shot skipped")
	assert_eq(sp.get_volleys_fired(), 0, "skipped shot not counted")
	assert_signal_emitted_with_parameters(sp, "volley_skipped", [&"outside_fire_arc"])


func test_orbit_and_retarget() -> void:
	sp.set_orbiting_enabled(true)
	assert_true(sp.shoot_once(), "orbiting shot fires with homing on")
	assert_gte(sp.retarget_live_volleys(), 1, "retarget touches live volleys")
	sp.set_orbiting_enabled(false)
	sp.set_homing_enabled(false)
	assert_true(sp.shoot_once(), "plain shot fires with homing off")
	assert_gte(sp.get_live_volley_count(), 1, "homing volleys still tracked")


func test_live_bullet_fuse() -> void:
	assert_true(sp.shoot_once())
	sp.set_max_live_bullets(1)
	var fired: int = sp.get_volleys_fired()
	watch_signals(sp)
	sp.shoot_once()
	assert_eq(sp.get_volleys_fired(), fired, "over-budget shot skipped")
	assert_signal_emitted_with_parameters(sp, "volley_skipped", [&"over_budget"])


func test_retarget_stagger_differs_per_spawner() -> void:
	var s2 := make_spawner()
	s2.set_homing_enabled(true)
	await idle(1)
	var cd1: float = sp.debug_get_retarget_countdown()
	var cd2: float = s2.debug_get_retarget_countdown()
	assert_between(cd1, 0.0, 0.5, "countdown within the interval")
	assert_between(cd2, 0.0, 0.5, "countdown within the interval")
	assert_ne(cd1, cd2, "distinct spawners stagger apart")
