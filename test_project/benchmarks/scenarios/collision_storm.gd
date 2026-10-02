extends BlastBenchmark
## Sustained collision traffic: every frame a 200-bullet fan fires into a
## wall 120 px away and dies on contact (max 1 hit). Exercises the overlap
## queue, dedup table, collision drain, kill path and pool churn together.
var _walls := 4
var _data: Array[DirectionalBulletsData2D] = []

func describe() -> String:
	return "200-bullet volley/frame dying on walls (collision drain + pool churn)"

func setup() -> void:
	for w in _walls:
		make_static_box(Vector2(400 + w * 1000, 300), Vector2(40, 900))
		var d := H.make_directional_data(200, 900.0, 3.0)
		var arr: Array = []
		for i in 200:
			arr.append(Transform2D(0.0, Vector2(280 + w * 1000, 300.0 - 400.0 + 4.0 * i)))
		d.transforms = arr
		d.set_collision_mask_from_array([3])
		d.collision_shape = H.make_circle_shape(4.0)
		d.bullet_max_collision_count = 1
		d.monitorable = true # required to detect StaticBody2D walls
		_data.append(d)

func step(frame: int) -> void:
	factory.spawn_controllable_directional_bullets(_data[frame % _walls])


func results() -> Dictionary:
	var r := super()
	# Self-check: a storm that never collides measures nothing (this scenario
	# once ran for weeks with monitorable = false and zero real hits).
	extra["records_per_frame"] = float(r["collision_records"]) / float(measure_frames)
	if r["collision_records"] < measure_frames * 100:
		push_error("collision_storm: only %d collision records in %d frames - the walls are not being hit" % [r["collision_records"], measure_frames])
	return r
