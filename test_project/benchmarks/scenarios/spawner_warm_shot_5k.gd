extends BlastBenchmark
## One 5000-bullet heart spawner firing EVERY frame into a warm pool (life
## 0.3 s): step_ms is the full shoot_once() cost - pattern (bake cache),
## validation and volley setup - with no allocation and no cold RIDs.
var _sp: BulletSpawner2D

func describe() -> String:
	return "5000-bullet heart spawner, shoot_once every frame, warm pool"

func setup() -> void:
	warmup_frames = 40
	measure_frames = 200
	_sp = BulletSpawner2D.new()
	_sp.set_shooting_enabled(false)
	_sp.position = Vector2(960, 540)
	add_child(_sp)
	_sp.set_bullet_factory(factory)
	_sp.set_spawn_data(H.make_volley_data(4, 250.0, 0.3))
	_sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_HEART
	_sp.helper_bullets_amount = 5000
	_sp.helper_heart_size = 300.0

func step(_frame: int) -> void:
	_sp.rotation += 0.01 # rigid motion: the bake re-poses, never regenerates
	_sp.shoot_once()
