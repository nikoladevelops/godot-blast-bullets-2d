extends BlastBenchmark
## 10,000 bullets (10 x 1000) that all expire on the same frame, three
## waves: the expiry burst shows up in frame_ms.max / p99.
func describe() -> String:
	return "10k bullets expiring on the same frame (3 waves)"

var _data: Array[DirectionalBulletsData2D] = []

func setup() -> void:
	warmup_frames = 10
	measure_frames = 240
	for i in 10:
		_data.append(ring_data(1000, Vector2(960 * (i % 5), 1200 * (i / 5)), 40.0, 50.0, 1.0))
	# Pre-warm: the first wave would otherwise measure cold allocation
	# (that is cold_spawn_scaling's job) instead of the expiry burst.
	for d in _data:
		factory.populate_bullets_pool(BulletFactory2D.debug_expected_pool_key(d), d, 1)

func step(frame: int) -> void:
	if frame % 80 == 10:
		for d in _data:
			factory.spawn_controllable_directional_bullets(d)
