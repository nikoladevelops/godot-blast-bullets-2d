extends BlastBenchmark
## Live hit handlers at scale: every frame a 100-bullet fan hits a wall and a
## GDScript handler on factory.body_entered reads each bullet's custom data,
## pose and velocity while the bullet is still alive; every 8th hit heals the
## bullet (vetoes the kill), so the post-handler kill decision runs both ways.
## About 2k bullets are in flight. Measures the in-tick emit + kill path.
var _walls := 4
var _data: Array[BulletVolleyData2D] = []
var _hits := 0
var _healed := 0
var _tag := Resource.new()

func describe() -> String:
	return "100-bullet volley/frame into walls, a GDScript hit handler per bullet (live emit + kill decision)"

func setup() -> void:
	var customs: Array = []
	for i in 100:
		customs.append(_tag)
	for w in _walls:
		make_static_box(Vector2(400 + w * 1000, 300), Vector2(40, 900))
		var d := H.make_volley_data(100, 900.0, 3.0)
		var arr: Array = []
		for i in 100:
			arr.append(Transform2D(0.0, Vector2(280 + w * 1000, 300.0 - 400.0 + 8.0 * i)))
		d.transforms = arr
		d.set_collision_mask_from_array([3])
		d.collision_shape = H.make_circle_shape(4.0)
		d.bullet_max_collision_count = 1
		d.monitorable = true # required to detect StaticBody2D walls
		d.all_bullets_custom_data = customs
		_data.append(d)
	factory.body_entered.connect(_on_hit)

func _on_hit(_body: Node, v: BulletVolley2D, i: int) -> void:
	_hits += 1
	var custom: Resource = v.bullet_get_custom_data(i)
	var pos: Vector2 = v.get_bullet_global_transform(i).origin
	var vel: Vector2 = v.get_bullet_velocity(i)
	if custom == _tag and pos.is_finite() and vel.is_finite() and _hits % 8 == 0:
		v.set_bullet_collision_count(i, 0) # heal: the plugin keeps it alive
		_healed += 1

func step(frame: int) -> void:
	factory.spawn_volley(_data[frame % _walls])


func results() -> Dictionary:
	var r := super()
	extra["handler_calls"] = _hits
	extra["healed"] = _healed
	if _hits < measure_frames * 50:
		push_error("hit_handlers_2k: only %d handler calls - the walls are not being hit" % _hits)
	return r
