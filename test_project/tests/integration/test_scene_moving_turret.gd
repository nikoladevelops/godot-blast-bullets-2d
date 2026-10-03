extends BlastTest
## Integration scene (tests/scenes/moving_turret.tscn), authored like a user
## would in the editor: a ring spawner patrolling a Path2D (ping-pong, sine
## in-out, speed timing, rotate-with-path, inherited velocity) auto-firing at
## a floor. Locks serialization (every movement property/enum id survives
## load) and the end-to-end behavior headless.

const SCENE := preload("res://tests/scenes/moving_turret.tscn")

var root: Node2D
var turret: BulletSpawner2D
var scene_factory: BulletFactory2D


func before_each() -> void:
	await super()
	root = SCENE.instantiate()
	add(root)
	turret = root.get_node("Turret")
	scene_factory = root.get_node("BulletFactory2D")
	await idle(2)


func test_serialized_movement_properties_load() -> void:
	assert_true(turret.movement_enabled)
	assert_eq(turret.get_movement_path_node(), root.get_node("Patrol"), "path resolved from the NodePath")
	assert_eq(turret.movement_loop_mode, BulletSpawner2D.MOVEMENT_LOOP_PING_PONG, "loop mode id 2 = PING_PONG")
	assert_eq(turret.movement_timing, BulletSpawner2D.MOVEMENT_TIMING_SPEED, "timing id 1 = SPEED")
	assert_eq(turret.movement_transition, Tween.TRANS_SINE, "transition id 1 = Tween.TRANS_SINE")
	assert_eq(turret.movement_ease, Tween.EASE_IN_OUT, "ease id 2 = Tween.EASE_IN_OUT")
	assert_eq(turret.movement_speed, 200.0)
	assert_true(turret.movement_rotate_with_path)
	assert_true(turret.inherit_movement_velocity)
	assert_eq(turret.pattern_source, BulletSpawner2D.PATTERN_FROM_HELPER_RING)


func test_patrols_shoots_and_hits() -> void:
	watch_signals(turret)
	watch_signals(scene_factory)
	var start := turret.global_position
	await idle(60)
	assert_gt(turret.global_position.x, start.x + 50.0, "patrolling along the path")
	assert_almost_eq(turret.global_position.y, 300.0, 0.5, "stays on the horizontal path")
	# shoot_interval_sec 0.25 over one second (first pull due at once).
	assert_between(get_signal_emit_count(turret, "volley_fired"), 4, 5, "auto-fires every 0.25 s while moving")
	await idle(180) # 4 s total: 400 px at 200 px/s = 2 s per leg -> turned around
	assert_gte(get_signal_emit_count(turret, "movement_endpoint_reached"), 1, "reached the end and turned")
	var hits: int = get_signal_emit_count(turret, "body_entered")
	assert_gt(hits, 0, "bullets hit the floor (spawner-owned volleys report on the spawner)")
	assert_signal_not_emitted(scene_factory, "directional_body_entered", "factory stays silent for spawner volleys")
	assert_true(scene_factory.debug_assert_no_dangling().get("ok", false))
