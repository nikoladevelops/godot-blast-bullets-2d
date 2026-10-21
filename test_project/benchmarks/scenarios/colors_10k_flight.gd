extends BlastBenchmark
## volley_10k_flight with per-bullet colors: the same 10,000 bullets (10
## rings x 1000), each volley drawing a rainbow from its data's
## bullet_colors (a 12-float MultiMesh stride instead of 8). Compare its
## tick with volley_10k_flight: colors cost the wider buffer only.
func describe() -> String:
	return "10k bullets in flight with per-bullet colors (stride 12)"

func setup() -> void:
	for i in 10:
		var d := ring_data(1000, Vector2(960 * (i % 5), 1200 * (i / 5)), 50.0, 60.0, 1000.0)
		var colors := PackedColorArray()
		for b in 1000:
			colors.append(Color.from_hsv(b / 1000.0, 0.8, 1.0))
		d.use_bullet_colors = true
		d.bullet_colors = colors
		factory.spawn_volley(d)
