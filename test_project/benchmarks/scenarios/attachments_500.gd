extends BlastBenchmark
## 500 bullets each carrying a node attachment (pooled), refired every 90
## frames: attachment follow, disable/pool and re-attach costs.
var _scene: PackedScene
var _data: Array[DirectionalBulletsData2D] = []

func describe() -> String:
	return "500 bullets with node attachments, refired every 90 frames"

func setup() -> void:
	var a: Node = load("res://benchmarks/bench_attachment.gd").new()
	_scene = PackedScene.new()
	_scene.pack(a)
	a.free()
	for i in 5:
		_data.append(ring_data(100, Vector2(400 + 250 * i, 540), 20.0, 150.0, 1.4))

func step(frame: int) -> void:
	if frame % 90 == 0:
		for d in _data:
			var vol: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
			vol.all_bullets_set_attachment(_scene, Vector2.ZERO, true)
