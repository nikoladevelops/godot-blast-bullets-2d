extends BlastBenchmark
## Pickups: every 60 frames a 5000-bullet volley is converted into coins that
## fly to a target and are collected on arrival (conversion call, burst/magnet
## phases, 5000 collections through the homing reach path, pool churn).
func describe() -> String:
	return "5k bullets converted to pickups every 60 frames, magnet to a target, collected on arrival"

var _data: BulletVolleyData2D
var _coin := BulletPickupData2D.new()
var _target := Node2D.new()

func setup() -> void:
	warmup_frames = 70
	measure_frames = 300
	_data = ring_data(5000, Vector2(960, 540), 300.0, 0.0, 30.0)
	_target.position = Vector2(960, 540)
	add_child(_target)
	_coin.magnet_delay_sec = 0.2
	_coin.magnet_acceleration = 2500.0
	_coin.magnet_max_speed = 1800.0
	_coin.collect_radius = 30.0
	_coin.scatter_speed_max = 150.0
	_coin.lifetime_sec = 20.0
	extra["conversions"] = 0
	# Pre-warm the pool for both volley sizes (spawn + the pickup volley).
	factory.populate_bullets_pool(BulletFactory2D.debug_expected_pool_key(_data), _data, 2)

func step(frame: int) -> void:
	if frame % 60 == 5:
		factory.spawn_volley(_data)
		factory.convert_all_bullets_to_pickups(_coin, _target)
		extra["conversions"] = int(extra["conversions"]) + 1
