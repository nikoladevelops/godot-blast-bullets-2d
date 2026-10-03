extends BlastBenchmark
## A 1500-bullet heart spawner with continuous spin AND the runtime pattern
## preview on, firing every 0.25 s: the preview pose cost per frame is the
## thing to watch (extra.preview_* when the spawner exposes stats).
var _sp: BulletSpawner2D

func describe() -> String:
	return "1500-bullet heart spawner, spin + runtime preview, firing 4/s"

func setup() -> void:
	_sp = BulletSpawner2D.new()
	_sp.set_shooting_enabled(false)
	_sp.position = Vector2(960, 540)
	add_child(_sp)
	_sp.set_bullet_factory(factory)
	_sp.set_spawn_data(H.make_volley_data(4, 250.0, 2.0))
	_sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_HEART
	_sp.helper_bullets_amount = 1500
	_sp.helper_heart_size = 150.0
	_sp.spin_enabled = true
	_sp.spin_speed_deg_per_sec = 90.0
	_sp.show_pattern_preview = true
	_sp.show_preview_during_runtime = true
	_sp.shoot_interval_sec = 0.25
	_sp.set_shooting_enabled(true)
