extends BlastBenchmark
## 10,000 bullets (10 rings x 1000) flying outward, no
## collisions: the pure movement + render-buffer + shape-transform cost.
func describe() -> String:
	return "10k bullets in flight, no overlaps"

func setup() -> void:
	for i in 10:
		factory.spawn_volley(ring_data(1000, Vector2(960 * (i % 5), 1200 * (i / 5)), 50.0, 60.0, 1000.0))
