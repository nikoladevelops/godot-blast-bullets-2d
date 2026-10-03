extends BlastBenchmark
## Cold-spawn cost vs volley size (no pool): N = 1000, 2000, 4000, 8000.
## A linear implementation doubles per step; quadratic physics setup
## quadruples. extra.cold_<N>_ms + extra.scaling_8k_over_1k summarize it.
func describe() -> String:
	return "cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys"

func setup() -> void:
	measure_frames = 30
	warmup_frames = 5
	for n in [1000, 2000, 4000, 8000]:
		var best := 1e12
		for rep in 3:
			var d := ring_data(n, Vector2(960, 540), 100.0, 0.0, 1000.0)
			var t0 := Time.get_ticks_usec()
			var vol: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
			best = minf(best, float(Time.get_ticks_usec() - t0) / 1000.0)
			await get_tree().process_frame
			factory.free_active_bullets()
			await get_tree().process_frame
			factory.free_bullets_pool()
			await get_tree().process_frame
		extra["cold_%d_ms" % n] = best
	extra["scaling_8k_over_1k"] = float(extra["cold_8000_ms"]) / maxf(0.001, float(extra["cold_1000_ms"]))
