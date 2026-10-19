extends BlastBenchmark
## volley_10k_flight with the collision-shape debugger drawing every bullet:
## the debugger's per-tick upload of 10,000 shape transforms.
func describe() -> String:
	return "10k bullets in flight, collision debugger on"

func setup() -> void:
	factory.is_debugger_enabled = true
	for i in 10:
		factory.spawn_volley(ring_data(1000, Vector2(960 * (i % 5), 1200 * (i / 5)), 50.0, 60.0, 1000.0))
