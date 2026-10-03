extends BlastBenchmark
## A 1500-bullet ring spawner whose PARENT moves every frame (a turret on a
## moving ship) with the runtime preview on, firing every 0.25 s.
var _ship: Node2D

func describe() -> String:
	return "1500-bullet spawner on a moving parent, runtime preview on, firing 4/s"

func setup() -> void:
	_ship = Node2D.new()
	add_child(_ship)
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	_ship.add_child(sp)
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(H.make_volley_data(4, 250.0, 2.0))
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
	sp.helper_bullets_amount = 1500
	sp.show_pattern_preview = true
	sp.show_preview_during_runtime = true
	sp.shoot_interval_sec = 0.25
	sp.set_shooting_enabled(true)

func step(frame: int) -> void:
	var t := frame / 60.0
	_ship.position = Vector2(960, 540) + Vector2(cos(t), sin(t * 1.3)) * 300.0
