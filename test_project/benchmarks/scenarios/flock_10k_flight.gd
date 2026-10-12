extends BlastBenchmark
## volley_10k_flight with flocking: the same 10,000 bullets (10 rings x
## 1000, dense: every bullet has hundreds of flockmates in range, so each
## reads exactly max_neighbors = 32), each ring its own Per Volley flock.
## The capped worst case of a big burst; compare with volley_10k_flight.

func describe() -> String:
	return "10k bullets in flight, 10 Per Volley flocks (dense, capped)"

func setup() -> void:
	var flock := BulletFlock2D.new()
	for i in 10:
		var v: BulletVolley2D = factory.spawn_volley(ring_data(1000, Vector2(960 * (i % 5), 1200 * (i / 5)), 50.0, 60.0, 1000.0))
		v.flock_join(flock, BulletFlock2D.SCOPE_PER_VOLLEY)

func step(_frame: int) -> void:
	var s: Dictionary = factory.debug_get_flock_stats()
	extra["flock_pass_usec"] = s["pass_usec"]
	extra["neighbor_checks"] = s["neighbor_checks"]
