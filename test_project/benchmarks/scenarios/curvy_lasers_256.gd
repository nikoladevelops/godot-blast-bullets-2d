extends BlastBenchmark
## Curvy lasers: 8 LaserVolley2Ds of 32 snakes each (256 curvy beams, 240 px
## bodies, ~36 segments) whose heads track one weaving target at 120 deg/s
## and wave sideways; a wave lives 1.9 s and a new one fires every 2 s (the
## pool reuses the 8 lasers). Per tick: head step, trail trim, the contact
## queries (grouped boxes over empty space); per frame: one triangle array
## per laser.
var _data: LaserData2D
var _target: Node2D
var _beams: Array = []

func describe() -> String:
	return "256 curvy tracking lasers (8 x 32), a wave every 2 s, 1.9 s lives"

func setup() -> void:
	warmup_frames = 240 # two waves: the pool holds the 8 lasers
	_target = Node2D.new()
	add_child(_target)
	_data = H.make_laser(0.0, 0.0, 1.9, 0.0, 240.0)
	_data.type = LaserData2D.LASER_CURVY
	_data.aim = LaserData2D.LASER_AIM_TRACK
	_data.turn_rate_degrees = 120.0
	_data.speed = 400.0
	_data.wave_amplitude = 20.0
	_data.wave_frequency = 2.0
	for k in 8:
		var center := Vector2(300 + 440 * (k % 4), 300 + 480 * (k / 4))
		var beams: Array[Transform2D] = []
		for b in 32:
			beams.append(Transform2D(TAU * b / 32.0, center))
		_beams.append(beams)

func step(frame: int) -> void:
	var t := frame / 60.0
	_target.position = Vector2(960, 540) + Vector2(cos(t * 0.9), sin(t * 1.7)) * 300.0
	if frame % 120 == 0:
		for beams: Array[Transform2D] in _beams:
			var laser: LaserVolley2D = factory.spawn_laser(_data, beams)
			laser.set_targets([_target])
	extra["lasers"] = factory.get_active_laser_count()
