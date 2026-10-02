extends BlastBenchmark
## A busy boss fight: 3 spawners (spiral spin, fan burst, homing ring) + a
## player target moving around + walls; mixed features at once.
var _player: Node2D

func describe() -> String:
	return "3 spawners (spin spiral, fan, homing ring) + moving player + walls"

func _spawner(src: int, n: int, pos: Vector2, interval: float) -> BulletSpawner2D:
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.position = pos
	add_child(sp)
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(H.make_directional_data(4, 260.0, 3.0))
	sp.pattern_source = src
	sp.helper_bullets_amount = n
	sp.shoot_interval_sec = interval
	return sp

func setup() -> void:
	_player = Node2D.new()
	_player.add_to_group("bench_player")
	add_child(_player)
	make_static_box(Vector2(960, 1000), Vector2(1900, 40))
	var a := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_SPIRAL, 120, Vector2(600, 300), 0.05)
	a.spin_enabled = true
	a.spin_speed_deg_per_sec = 120.0
	var b := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_FAN, 60, Vector2(1300, 300), 0.2)
	var c := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 80, Vector2(960, 200), 0.5)
	c.homing_node_group = &"bench_player"
	c.set_homing_enabled(true)
	for sp in [a, b, c]:
		sp.set_shooting_enabled(true)

func step(frame: int) -> void:
	var t := frame / 60.0
	_player.position = Vector2(960, 700) + Vector2(cos(t * 0.8) * 500.0, sin(t * 1.7) * 120.0)
