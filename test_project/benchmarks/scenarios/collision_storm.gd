extends BlastBenchmark
## Sustained collision traffic: every frame a 200-bullet fan fires into a
## wall 120 px away and dies on contact (max 1 hit). Exercises the overlap
## queue, dedup table, collision drain, kill path and pool churn together.
var _walls := 4

func describe() -> String:
	return "200-bullet volley/frame dying on walls (collision drain + pool churn)"

func setup() -> void:
	for w in _walls:
		make_static_box(Vector2(400 + w * 1000, 300), Vector2(40, 900))

func step(frame: int) -> void:
	var w := frame % _walls
	var d := H.make_directional_data(200, 900.0, 3.0)
	var arr: Array = []
	for i in 200:
		var y := 300.0 - 400.0 + 4.0 * i
		arr.append(Transform2D(0.0, Vector2(280 + w * 1000, y)))
	d.transforms = arr
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(4.0)
	d.bullet_max_collision_count = 1
	factory.spawn_controllable_directional_bullets(d)
