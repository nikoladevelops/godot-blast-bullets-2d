extends BlastBenchmark
## A realistic swarm: 2,000 bullets (20 volleys x 100, scattered over a
## 1200 px square) flying as ONE Shared flock with the default rules. The
## flock pass (exact spatial hash grid, 32 neighbors) plus the per-bullet
## turn; compare its tick with a plain flight of the same size.

func describe() -> String:
	return "2k bullets, one Shared flock (default rules)"

func setup() -> void:
	var flock := BulletFlock2D.new()
	for k in 20:
		var tr: Array = []
		for i in 100:
			tr.append(Transform2D(rng.randf_range(-PI, PI), Vector2(rng.randf_range(0, 1200), rng.randf_range(0, 1200))))
		var d := H.make_flight_data(tr, 120.0, 1000.0)
		d.set_collision_mask_from_array([3])
		d.collision_shape = H.make_circle_shape(4.0)
		var v: BulletVolley2D = factory.spawn_volley(d)
		v.flock_join(flock)

func step(_frame: int) -> void:
	var s: Dictionary = factory.debug_get_flock_stats()
	extra["flock_pass_usec"] = s["pass_usec"]
	extra["neighbor_checks"] = s["neighbor_checks"]
