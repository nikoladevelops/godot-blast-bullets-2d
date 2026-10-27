extends BlastBenchmark
## 40 ring spawners (16 bullets) each firing one shoot_once_with() override
## every frame into a warm pool: an empty override, then a real one-knob
## override on alternate frames. step_ms is the override cost (snapshot,
## apply, shot, restore). extra.override_empty_us / override_knob_us /
## plain_us are per-shot means measured in setup (plain = shoot_once).
var _spawners: Array[BulletSpawner2D] = []
var _empty := {}
var _knob := {"helper_ring_radius": 140.0}

func describe() -> String:
	return "40 spawners x shoot_once_with() per frame (empty / one-knob overrides)"

func _per_shot_us(call: Callable, reps: int) -> float:
	var t0 := Time.get_ticks_usec()
	for r in reps:
		for sp in _spawners:
			call.call(sp)
	return float(Time.get_ticks_usec() - t0) / float(reps * _spawners.size())

func setup() -> void:
	warmup_frames = 30
	measure_frames = 200
	var data := H.make_volley_data(16, 200.0, 0.1)
	for i in 40:
		var sp := BulletSpawner2D.new()
		sp.set_shooting_enabled(false)
		sp.position = Vector2(100 + 40 * (i % 10), 100 + 60 * (i / 10))
		add_child(sp)
		sp.set_bullet_factory(factory)
		sp.set_spawn_data(data)
		sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
		sp.helper_bullets_amount = 16
		_spawners.append(sp)
	for d in [data]:
		factory.populate_bullets_pool(BulletFactory2D.debug_expected_pool_key(d), d, 400)
	extra["plain_us"] = _per_shot_us(func(sp: BulletSpawner2D) -> void: sp.shoot_once(), 5)
	extra["override_empty_us"] = _per_shot_us(func(sp: BulletSpawner2D) -> void: sp.shoot_once_with(_empty), 5)
	extra["override_knob_us"] = _per_shot_us(func(sp: BulletSpawner2D) -> void: sp.shoot_once_with(_knob), 5)
	factory.free_active_bullets()

func step(frame: int) -> void:
	var entry := _empty if frame % 2 == 0 else _knob
	for sp in _spawners:
		sp.shoot_once_with(entry)
