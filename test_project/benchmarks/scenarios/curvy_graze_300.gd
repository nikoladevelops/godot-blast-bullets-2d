extends BlastBenchmark
## Laser graze with many targets: 256 curvy snakes (8 lasers x 32, ~36
## segments each) weaving through 300 graze targets of one group, every
## laser carrying a 2-ring zone. Per tick and beam: the beam's box grown by
## the largest ring culls far targets before the exact polyline distance.
var _data: LaserData2D
var _zone: GrazeZone2D
var _targets: Array = []
var _beams: Array = []
var _grazes := 0

func describe() -> String:
	return "256 curvy lasers grazing among 300 targets (2-ring zone)"

func setup() -> void:
	warmup_frames = 240 # two waves: the pool holds the 8 lasers
	var rng := RandomNumberGenerator.new()
	rng.seed = 11
	for i in 300:
		var t := Node2D.new()
		t.position = Vector2(rng.randf_range(0, 1920), rng.randf_range(0, 1080))
		add_child(t)
		t.add_to_group(&"bench_graze")
		_targets.append(t)
	_data = H.make_laser(0.0, 0.0, 1.9, 0.0, 240.0)
	_data.type = LaserData2D.LASER_CURVY
	_data.speed = 400.0
	_data.wave_amplitude = 20.0
	_data.wave_frequency = 2.0
	_zone = H.make_graze_zone([20.0, 40.0])
	factory.laser_grazed.connect(_on_graze)
	for k in 8:
		var center := Vector2(300 + 440 * (k % 4), 300 + 480 * (k / 4))
		var beams: Array[Transform2D] = []
		for b in 32:
			beams.append(Transform2D(TAU * b / 32.0, center))
		_beams.append(beams)

func _on_graze(_t: Object, _l: LaserVolley2D, _b: int, _z: GrazeZone2D, _r: int) -> void:
	_grazes += 1

func step(frame: int) -> void:
	if frame % 120 == 0:
		for beams: Array[Transform2D] in _beams:
			var laser: LaserVolley2D = factory.spawn_laser(_data, beams)
			laser.graze_set_zones([_zone], &"bench_graze")
	extra["grazes"] = _grazes
