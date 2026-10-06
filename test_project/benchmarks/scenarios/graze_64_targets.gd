extends BlastBenchmark
## graze_10k_flight with the target cap filled: the same 10,000 bullets
## (10 rings x 1000), one 3-ring zone over an enemy group of 64 Node2Ds
## spread over the field and weaving every frame. Measures how the
## per-bullet graze test scales with live targets (one group scan per tick,
## the closest-approach loop runs over every target); compare its tick with
## graze_10k_flight (1 target).
var _targets: Array[Node2D] = []
var _grazes := 0

func describe() -> String:
	return "10k bullets in flight, graze armed (3 rings, 64 moving targets)"

func setup() -> void:
	for i in 64:
		var t := Node2D.new()
		add_child(t)
		t.add_to_group(&"bench_enemies")
		_targets.append(t)
	var zone := BulletGrazeZone2D.new()
	zone.ring_count = 3
	zone.ring_1_radius = 16.0
	zone.ring_2_radius = 32.0
	zone.ring_3_radius = 48.0
	factory.bullet_grazed.connect(_on_grazed)
	for i in 10:
		var v: BulletVolley2D = factory.spawn_volley(ring_data(1000, Vector2(960 * (i % 5), 1200 * (i / 5)), 50.0, 60.0, 1000.0))
		v.graze_set_zones([zone], &"bench_enemies")

func _on_grazed(_t: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
	_grazes += 1

func step(frame: int) -> void:
	var t := frame / 60.0
	for i in _targets.size():
		var home := Vector2(480 * (i % 8), 300 * (i / 8))
		_targets[i].position = home + Vector2(cos(t * 0.7 + i), sin(t * 1.3 + i)) * 120.0
	extra["grazes"] = _grazes
