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
	assert_eq(sp.retarget_live_volleys(), 1, "retarget touches live volleys")
	sp.set_orbiting_enabled(false)
	sp.set_homing_enabled(false)
	assert_true(sp.shoot_once(), "plain shot fires with homing off")
	assert_eq(sp.get_live_volley_count(), 2, "homing volleys still tracked")


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


## A ring of `n` bullets 10 px around the origin homing onto a lone target
## at (120, 0) and orbiting it at radius 40 (circle speed 160 px/s).
func _orbiter(policy: int, n := 4) -> Array:
	var target := make_graze_target(Vector2(120, 0), &"orbit_lone")
	var s := make_spawner(H.make_volley_data(n, 160.0, 10.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, n)
	s.helper_ring_radius = 10.0
	s.homing_node_group = &"orbit_lone"
	s.homing_smoothing = 6.0
	s.homing_take_control_of_texture_rotation = true
	s.homing_retarget_interval_sec = 1000.0 # retargets only when the test asks
	s.set_homing_enabled(true)
	s.orbiting_radius = 40.0
	s.orbiting_lock_policy = policy
	s.orbiting_enabled = true
	var got: Array = []
	s.volley_fired.connect(func(v, _n) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(s.shoot_once(), "fires")
	return [s, got[0], target]


func test_a_retarget_onto_the_same_target_never_stalls_an_orbit(policy: int = use_parameters([BulletVolley2D.RelockAlways, BulletVolley2D.RelockOnTargetChange, BulletVolley2D.StayLocked])) -> void:
	# RelockAlways re-acquires the radius on every retarget (documented):
	# the bullet relocks right on the ring and keeps circling at its speed
	# (it used to stand still for the relock tick, every retarget interval).
	var o: Array = _orbiter(policy)
	var s: BulletSpawner2D = o[0]
	var v: BulletVolley2D = o[1]
	for f in 120:
		await physics(1)
	assert_true(v.bullet_is_orbiting_locked(0), "circling before the retarget")
	var step := 160.0 / 60.0
	var steps: Array = []
	var last: Vector2 = v.get_bullet_transform(0).origin
	for f in 6:
		if f == 2:
			assert_eq(s.retarget_live_volleys(), 1, "the pass reached the volley")
		await physics(1)
		var now: Vector2 = v.get_bullet_transform(0).origin
		steps.append(snappedf(now.distance_to(last), 0.01))
		last = now
	for d in steps:
		assert_almost_eq(float(d), step, step * 0.05, "policy %d: every tick moves a full step around the ring %s" % [policy, steps])
	assert_true(v.bullet_is_orbiting_locked(0), "circling after the retarget")


func test_the_lock_tick_moves_a_full_step() -> void:
	# Arriving at the ring: the tick that locks moves the bullet as far as
	# any other tick (onto the ring, then along it), never only the last
	# sliver of the approach.
	var o: Array = _orbiter(BulletVolley2D.RelockAlways, 1)
	var v: BulletVolley2D = o[1]
	var step := 160.0 / 60.0
	var last: Vector2 = v.get_bullet_transform(0).origin
	var lock_step := -1.0
	for f in 120:
		await physics(1)
		var now: Vector2 = v.get_bullet_transform(0).origin
		if v.bullet_is_orbiting_locked(0):
			lock_step = now.distance_to(last)
			break
		last = now
	assert_gt(lock_step, 0.0, "the bullet locked")
	assert_almost_eq(lock_step, step, step * 0.05, "the lock tick moved a full step")


func test_the_live_bullet_budget_counts_split_children_never_frozen_bullets_or_pickups() -> void:
	var s := make_spawner(H.make_volley_data(2, 50.0, 5.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	var stage := BulletSplitStage2D.new()
	stage.count = 3
	stage.split_after_sec = 0.05
	stage.parent_survives = true
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	s.split_data = chain
	s.split_enabled = true
	var got: Array = []
	s.volley_fired.connect(func(v, _n) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(s.shoot_once(), "fires")
	for f in 8:
		await physics(1)
	assert_eq(s.get_active_live_bullet_count(), 8, "2 parents + 6 children: the children are the spawner's")
	var v: BulletVolley2D = got[0]
	v.set_is_auto_pooling_enabled(false)
	v.disable_bullet(0)
	assert_eq(s.get_active_live_bullet_count(), 7, "a frozen bullet is not live")
	s.max_live_bullets = 7
	watch_signals(s)
	assert_false(s.shoot_once(), "the budget is full")
	assert_signal_emitted_with_parameters(s, "volley_skipped", [&"over_budget"])
	await idle(1)
	factory.convert_all_bullets_to_pickups(BulletPickupData2D.new(), Vector2(900, 0))
	assert_eq(s.get_active_live_bullet_count(), 0, "pickups belong to the factory")
	assert_true(s.shoot_once(), "the budget is free again")
