extends BlastBenchmark
## Emitter bullets: 4 volleys of 64 slow emitters (256) each fire the full
## 8-bullet ring of a template spawner every 0.25 s (staggered per bullet),
## 1.5 s child lives: ~8k children alive, ~8k emitter shots per second
## through the factory's emit flush (shoot_from per request, pooled ring
## volleys of 8). The emitters respawn every 4 s.
var _emitters: Array[BulletVolleyData2D] = []
var _timing: BulletEmitterData2D
var _template: BulletSpawner2D

func describe() -> String:
	return "256 emitter bullets firing 8-rings every 0.25 s (~8k children)"

func setup() -> void:
	warmup_frames = 300 # a full emitter cycle (4 s) plus the children's lives: the pools are warm
	_template = BulletSpawner2D.new()
	_template.set_shooting_enabled(false)
	add_child(_template)
	_template.set_bullet_factory(factory)
	var child := H.make_volley_data(4, 200.0, 1.5)
	child.shared_bullet_speed_data = child.all_bullet_speed_data[0]
	child.all_bullet_speed_data = []
	_template.set_spawn_data(child)
	_template.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
	_template.helper_bullets_amount = 8
	_timing = BulletEmitterData2D.new()
	_timing.interval_sec = 0.25
	_timing.stagger_sec = 0.004
	_timing.spin_per_shot_degrees = 11.0
	for k in 4:
		_emitters.append(ring_data(64, Vector2(480 + 320 * k, 540), 40.0, 30.0, 4.0))

func step(frame: int) -> void:
	if frame % 240 == 0:
		for d in _emitters:
			var v: BulletVolley2D = factory.spawn_volley(d)
			v.emitter_set(_timing, _template)
	extra["emitted"] = factory.debug_get_emitter_stats().get("fired_total", 0)
