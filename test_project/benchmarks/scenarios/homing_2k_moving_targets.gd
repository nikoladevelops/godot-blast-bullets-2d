extends BlastBenchmark
## 2,000 homing bullets (20 volleys x 100, shared deques) chasing 4 targets
## that circle the arena.
var _targets: Array[Node2D] = []

func describe() -> String:
	return "2k homing bullets chasing 4 moving targets"

func setup() -> void:
	for t in 4:
		var n := Node2D.new()
		add_child(n)
		_targets.append(n)
	for v in 20:
		var vol: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(ring_data(100, Vector2(400 + 60 * v, 400), 30.0, 220.0, 1000.0))
		vol.set_homing_smoothing(4.0)
		vol.shared_homing_deque_push_back_node2d_target(_targets[v % 4])

func step(frame: int) -> void:
	var t := frame / 60.0
	for i in _targets.size():
		var a := t * (0.6 + 0.2 * i) + i * TAU / 4.0
		_targets[i].position = Vector2(960, 540) + Vector2(cos(a), sin(a)) * (300.0 + 40.0 * i)
