extends BlastBenchmark
## Script-path twin of spawner_warm_shot_5k: the same 5000 transforms passed
## as a TypedArray to spawn_controllable_directional_bullets every frame
## (the cost a GDScript user pays per spawn call: one unbox per bullet).
var _data: DirectionalBulletsData2D

func describe() -> String:
	return "5000-bullet TypedArray spawn every frame (script API), warm pool"

func setup() -> void:
	warmup_frames = 40
	measure_frames = 200
	_data = ring_data(5000, Vector2(960, 540), 300.0, 250.0, 0.3)

func step(_frame: int) -> void:
	factory.spawn_controllable_directional_bullets(_data)
