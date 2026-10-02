extends BlastBenchmark
## Steady-state spawn/expire churn with a warm pool: one 300-bullet volley
## per frame, 0.5 s lifetime (~30 live volleys, ~9k bullets).
func describe() -> String:
	return "one 300-bullet volley per frame, 0.5 s life (warm-pool spawn + expiry)"

var _data: Array[DirectionalBulletsData2D] = []

func setup() -> void:
	for k in 8:
		_data.append(ring_data(300, Vector2(200 + k * 200, 540), 20.0, 300.0, 0.5))

func step(frame: int) -> void:
	factory.spawn_controllable_directional_bullets(_data[frame % 8])
