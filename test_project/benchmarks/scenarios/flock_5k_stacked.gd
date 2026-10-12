extends BlastBenchmark
## Worst case: 5,000 bullets spawned on ONE point (a muzzle stack) in one
## flock. Every flockmate is in range, so the cap (32) bounds the work to
## exactly 160k checks per tick while separation parts the stack.

func describe() -> String:
	return "5k bullets stacked on one point, one flock (cap bound)"

func setup() -> void:
	var tr: Array = []
	for i in 5000:
		tr.append(Transform2D(TAU * i / 5000.0, Vector2(600, 600)))
	var d := H.make_flight_data(tr, 80.0, 1000.0)
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(4.0)
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.flock_join(BulletFlock2D.new())

func step(_frame: int) -> void:
	var s: Dictionary = factory.debug_get_flock_stats()
	extra["flock_pass_usec"] = s["pass_usec"]
	extra["neighbor_checks"] = s["neighbor_checks"]
