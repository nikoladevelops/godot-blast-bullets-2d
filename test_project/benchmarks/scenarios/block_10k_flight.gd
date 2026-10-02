extends BlastBenchmark
## 10,000 block bullets (10 rigid volleys x 1000).
func describe() -> String:
	return "10k block bullets in flight (rigid volleys)"

func setup() -> void:
	for i in 10:
		var d := H.make_block_data(1000, 60.0, 1000.0)
		var arr: Array = []
		for k in 1000:
			arr.append(Transform2D(0.0, Vector2(960 * (i % 5) + (k % 40) * 12.0, 1200 * (i / 5) + (k / 40) * 12.0)))
		d.transforms = arr
		d.collision_shape = H.make_circle_shape(4.0)
		factory.spawn_block_bullets(d)
