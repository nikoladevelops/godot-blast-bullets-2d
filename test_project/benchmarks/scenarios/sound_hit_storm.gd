extends BlastBenchmark
## The collision storm (200-bullet fan into walls every frame, max 1 hit) with
## an On Hit sound on every volley: one hit offer per bullet, interval-gated.
var _walls := 4
var _data: Array[BulletVolleyData2D] = []
var _sound: BulletSoundData2D


func describe() -> String:
	return "200-bullet volley/frame dying on walls with an On Hit sound"


func setup() -> void:
	_sound = H.make_sound(BulletSoundData2D.SOUND_ON_HIT, 8)
	_sound.min_interval_sec = 0.02
	for w in _walls:
		make_static_box(Vector2(400 + w * 1000, 300), Vector2(40, 900))
		var d := H.make_volley_data(200, 900.0, 3.0)
		var arr: Array = []
		for i in 200:
			arr.append(Transform2D(0.0, Vector2(280 + w * 1000, 300.0 - 400.0 + 4.0 * i)))
		d.transforms = arr
		d.set_collision_mask_from_array([3])
		d.collision_shape = H.make_circle_shape(4.0)
		d.bullet_max_collision_count = 1
		d.monitorable = true
		_data.append(d)


func step(frame: int) -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data[frame % _walls])
	v.sound_set_effects([_sound])
