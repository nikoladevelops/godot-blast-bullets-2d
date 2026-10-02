class_name BlastBenchmark
extends Node2D
## Base class for headless benchmark scenarios (tools/run_benchmarks.py).
##
## Lifecycle (driven by bench_main.gd):
##   1. a fresh BulletFactory2D is added as `factory`;
##   2. `await setup()` builds the scenario (spawners, targets, walls...);
##   3. `warmup_frames` frames run (pools fill, caches warm), then the
##      factory's cumulative stats are reset;
##   4. `measure_frames` frames run; per frame we sample the wall time of the
##      whole engine frame (with --fixed-fps this is pure CPU: no sleeping),
##      the factory's own physics tick, Godot's physics/process times and
##      memory/object counts;
##   5. results() returns percentiles + `extra` (scenario-specific numbers).
## `step(frame)` runs once per frame before the engine frame is measured;
## keep per-frame scenario work cheap and deterministic (seeded RNG).

const H := preload("res://tests/common/blast_test_helpers.gd")

var factory: BulletFactory2D
var warmup_frames := 60
var measure_frames := 300
## Scenario-specific metrics (numbers only), merged into the result JSON.
var extra := {}
var rng := RandomNumberGenerator.new()

var _frame_us: Array[float] = []
var _tick_us: Array[float] = []
var _physics_us: Array[float] = []
var _process_us: Array[float] = []
var _bullets: Array[float] = []
var _mem_peak := 0.0
var _mem_start := 0.0


## Override: build the scenario. May await frames.
func setup() -> void:
	pass


## Override: per-frame scenario work (spawn, move targets...). `frame` counts
## from 0 across warmup + measurement.
func step(_frame: int) -> void:
	pass


## One-line human description (printed in LATEST.md).
func describe() -> String:
	return ""


func run() -> Dictionary:
	rng.seed = 1234567
	factory = BulletFactory2D.new()
	factory.name = "BenchFactory"
	add_child(factory)
	await get_tree().process_frame
	await get_tree().process_frame
	await setup()
	for i in warmup_frames:
		step(i)
		await get_tree().process_frame
	factory.reset_frame_stats()
	_mem_start = Performance.get_monitor(Performance.MEMORY_STATIC)
	var prev := Time.get_ticks_usec()
	for i in measure_frames:
		step(warmup_frames + i)
		await get_tree().process_frame
		var now := Time.get_ticks_usec()
		_frame_us.append(float(now - prev))
		prev = now
		var s: Dictionary = factory.get_frame_stats()
		_tick_us.append(float(s["physics_tick_usec"]))
		_bullets.append(float(s["active_bullets"]))
		_physics_us.append(Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1e6)
		_process_us.append(Performance.get_monitor(Performance.TIME_PROCESS) * 1e6)
		_mem_peak = maxf(_mem_peak, Performance.get_monitor(Performance.MEMORY_STATIC))
	return results()


static func _stats_ms(samples: Array[float]) -> Dictionary:
	if samples.is_empty():
		return {"p50": 0.0, "p95": 0.0, "p99": 0.0, "max": 0.0, "mean": 0.0}
	var sorted := samples.duplicate()
	sorted.sort()
	var n := sorted.size()
	var total := 0.0
	for v in sorted:
		total += v
	var pick := func(q: float) -> float:
		return sorted[clampi(int(ceil(q * n)) - 1, 0, n - 1)] / 1000.0
	return {"p50": pick.call(0.50), "p95": pick.call(0.95), "p99": pick.call(0.99),
		"max": sorted[n - 1] / 1000.0, "mean": total / n / 1000.0}


func results() -> Dictionary:
	var stats: Dictionary = factory.get_frame_stats()
	var mean_bullets := 0.0
	for b in _bullets:
		mean_bullets += b
	mean_bullets /= maxf(1.0, _bullets.size())
	return {
		"description": describe(),
		"warmup_frames": warmup_frames,
		"measure_frames": measure_frames,
		"frame_ms": _stats_ms(_frame_us),
		"factory_tick_ms": _stats_ms(_tick_us),
		"physics_ms": _stats_ms(_physics_us),
		"process_ms": _stats_ms(_process_us),
		"memory_static_peak_mb": _mem_peak / 1048576.0,
		"memory_static_growth_mb": (_mem_peak - _mem_start) / 1048576.0,
		"objects_end": Performance.get_monitor(Performance.OBJECT_COUNT),
		"nodes_end": Performance.get_monitor(Performance.OBJECT_NODE_COUNT),
		"active_bullets_mean": mean_bullets,
		"spawned_bullets": stats["spawned_bullets_total"],
		"expired_bullets": stats["expired_bullets_total"],
		"collision_records": stats["collision_records_total"],
		"pool_hits": stats["pool_hits"],
		"pool_misses": stats["pool_misses"],
		"extra": extra,
	}


# ---------------- shared scenario builders ----------------

## Directional data: n bullets on a ring of radius r around `center`, flying
## outward at `speed`. Collision layer 2, mask 3 (walls/areas use value 4).
func ring_data(n: int, center: Vector2, r: float, speed: float, lifetime: float) -> DirectionalBulletsData2D:
	var d := H.make_directional_data(n, speed, lifetime)
	var arr: Array = []
	for i in n:
		var a := TAU * float(i) / float(n)
		arr.append(Transform2D(a, center + Vector2(cos(a), sin(a)) * r))
	d.transforms = arr
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(4.0)
	return d


func make_static_box(pos: Vector2, size: Vector2, layer_value: int = 4) -> StaticBody2D:
	var body := StaticBody2D.new()
	body.position = pos
	body.collision_layer = layer_value
	body.collision_mask = 2
	var cs := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = size
	cs.shape = box
	body.add_child(cs)
	add_child(body)
	return body
