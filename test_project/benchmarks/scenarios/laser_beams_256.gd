extends BlastBenchmark
## Lasers: 8 sweeping LaserVolley2Ds of 32 beams each (256 beams, 500 px)
## swinging inside a box of walls (one wall ray per beam per tick), every
## laser carrying a 2-ring graze zone around one weaving target, and 24
## static pierce targets on the hit mask (one shape query per beam per
## tick, repeat hits every 0.1 s, a connected hit handler).
var _target: Node2D
var _hits := 0

func describe() -> String:
	return "256 sweeping beams (8 lasers), walls, graze, 24 pierce targets"

func setup() -> void:
	_target = Node2D.new()
	add_child(_target)
	_target.add_to_group(&"bench_player")
	make_static_box(Vector2(960, 40), Vector2(1900, 40), 8)
	make_static_box(Vector2(960, 1040), Vector2(1900, 40), 8)
	make_static_box(Vector2(40, 540), Vector2(40, 1000), 8)
	make_static_box(Vector2(1880, 540), Vector2(40, 1000), 8)
	for i in 24:
		make_static_box(Vector2(200 + 70 * i, 300 + 400 * (i % 2)), Vector2(24, 24), 4)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 500.0) # active until ended
	d.wall_mask = 8
	d.hit_interval_sec = 0.1
	d.sweep_speed_degrees = 40.0
	d.sweep_arc_degrees = 90.0
	var zone := H.make_graze_zone([20.0, 40.0])
	factory.laser_hit.connect(_on_hit)
	for k in 8:
		var center := Vector2(300 + 440 * (k % 4), 300 + 480 * (k / 4))
		var beams: Array[Transform2D] = []
		for b in 32:
			beams.append(Transform2D(TAU * b / 32.0, center))
		var laser: LaserVolley2D = factory.spawn_laser(d, beams)
		laser.graze_set_zones([zone], &"bench_player")

func _on_hit(_t: Node, _l: LaserVolley2D, _b: int, _p: Vector2) -> void:
	_hits += 1

func step(frame: int) -> void:
	var t := frame / 60.0
	_target.position = Vector2(960, 540) + Vector2(cos(t * 0.7), sin(t * 1.3)) * 400.0
	extra["hits"] = _hits
