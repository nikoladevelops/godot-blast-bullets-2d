extends BlastBenchmark
## volley_10k_flight with graze armed: the same 10,000 bullets (10 rings x
## 1000), every volley carrying one 3-ring graze zone, and one target
## weaving through the first rings with a connected factory handler. The
## per-bullet graze test (swept circle test, fast path for far bullets) on
## top of the plain flight; compare its tick with volley_10k_flight.
var _target: Node2D
var _grazes := 0

func describe() -> String:
	return "10k bullets in flight, graze armed (3 rings, 1 moving target)"

func setup() -> void:
	_target = Node2D.new()
	add_child(_target)
	_target.add_to_group(&"bench_player")
	var zone := BulletGrazeZone2D.new()
	zone.ring_count = 3
	zone.ring_1_radius = 16.0
	zone.ring_2_radius = 32.0
	zone.ring_3_radius = 48.0
	factory.bullet_grazed.connect(_on_grazed)
	for i in 10:
		var v: BulletVolley2D = factory.spawn_volley(ring_data(1000, Vector2(960 * (i % 5), 1200 * (i / 5)), 50.0, 60.0, 1000.0))
		v.graze_set_zones([zone], &"bench_player")

func _on_grazed(_t: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
	_grazes += 1

func step(frame: int) -> void:
	var t := frame / 60.0
	_target.position = Vector2(960, 0) + Vector2(cos(t * 0.7), sin(t * 1.3)) * 240.0
	extra["grazes"] = _grazes
