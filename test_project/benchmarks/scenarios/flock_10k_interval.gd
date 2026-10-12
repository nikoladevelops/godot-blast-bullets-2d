extends BlastBenchmark
## flock_10k_flight with BulletFlock2D.update_interval = 3 ticks: every bullet
## recomputes its steering once per 3 ticks (staggered) and turns toward its
## last steering in between. Compare with flock_10k_flight (every tick).

func describe() -> String:
	return "10k bullets, 10 Per Volley flocks, steering refreshed every 3 ticks"

func setup() -> void:
	var flock := BulletFlock2D.new()
	flock.update_interval = 3.0 / 60.0
	for i in 10:
		var v: BulletVolley2D = factory.spawn_volley(ring_data(1000, Vector2(960 * (i % 5), 1200 * (i / 5)), 50.0, 60.0, 1000.0))
		v.flock_join(flock, BulletFlock2D.SCOPE_PER_VOLLEY)

func step(_frame: int) -> void:
	var s: Dictionary = factory.debug_get_flock_stats()
	extra["flock_pass_usec"] = s["pass_usec"]
	extra["neighbor_checks"] = s["neighbor_checks"]
	extra["queried_members"] = s["queried_members"]
