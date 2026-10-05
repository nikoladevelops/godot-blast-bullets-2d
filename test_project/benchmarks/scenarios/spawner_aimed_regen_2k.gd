extends BlastBenchmark
## One 2000-bullet Aimed spawner firing EVERY frame at a moving target (life
## 0.3 s, warm pool): Aimed reads the target, so the bake cache can never be
## used and every shot regenerates the pattern. step_ms is the full
## regenerate + shoot_once cost (the path Aimed, Corridor, Custom, Path2D and
## Children always take).
var _sp: BulletSpawner2D
var _target: Node2D

func describe() -> String:
	return "2000-bullet aimed spawner regenerating its pattern every frame (uncacheable source)"

func setup() -> void:
	warmup_frames = 40
	measure_frames = 200
	_target = Node2D.new()
	_target.position = Vector2(1400, 300)
	add_child(_target)
	_sp = BulletSpawner2D.new()
	_sp.set_shooting_enabled(false)
	_sp.position = Vector2(400, 540)
	add_child(_sp)
	_sp.set_bullet_factory(factory)
	_sp.set_spawn_data(H.make_volley_data(4, 250.0, 0.3))
	_sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_AIMED
	_sp.helper_bullets_amount = 2000
	_sp.helper_aimed_spread = 1.2
	_sp.set_helper_aimed_target(_target)

func step(frame: int) -> void:
	_target.position = Vector2(1400, 540) + Vector2(cos(frame * 0.05), sin(frame * 0.05)) * 300.0
	_sp.shoot_once()
