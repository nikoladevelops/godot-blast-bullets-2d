extends BlastBenchmark
## A 1500-bullet star spawner patrolling a Path2D (ping-pong, sine in-out,
## rotate-with-path) with the runtime preview ON, firing 8/s: movement +
## rigid bake re-pose + zero-rebuild preview together.
var _sp: BulletSpawner2D

func describe() -> String:
	return "1500-bullet star spawner moving along a Path2D, preview on, firing 8/s"

func setup() -> void:
	var path := Path2D.new()
	var c := Curve2D.new()
	for p in [Vector2(200, 200), Vector2(900, 300), Vector2(1500, 150), Vector2(1700, 800)]:
		c.add_point(p)
	path.curve = c
	add_child(path)
	_sp = BulletSpawner2D.new()
	_sp.set_shooting_enabled(false)
	add_child(_sp)
	_sp.set_bullet_factory(factory)
	_sp.set_spawn_data(H.make_volley_data(4, 250.0, 1.5))
	_sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_STAR
	_sp.helper_bullets_amount = 1500
	_sp.show_pattern_preview = true
	_sp.show_preview_during_runtime = true
	_sp.shoot_interval_sec = 0.125
	_sp.set_movement_path_node(path)
	_sp.movement_loop_mode = BulletSpawner2D.MOVEMENT_LOOP_PING_PONG
	_sp.movement_transition = Tween.TRANS_SINE
	_sp.movement_rotate_with_path = true
	_sp.movement_duration_sec = 2.0
	_sp.movement_enabled = true
	_sp.set_shooting_enabled(true)

func results() -> Dictionary:
	var r := super()
	extra["preview_rebuilds"] = int(_sp.debug_get_preview_stats()["rebuilds"])
	extra["pattern_cache_hits"] = int(_sp.debug_get_pattern_cache_info()["hits"])
	extra["pattern_cache_misses"] = int(_sp.debug_get_pattern_cache_info()["misses"])
	return r
