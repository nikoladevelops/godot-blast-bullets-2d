extends BlastTest
## A pooled volley is a stale handle: whatever a script calls on it, the next
## life that reuses it (maybe another spawner's) starts exactly like the life
## before it did. For EVERY bound BulletVolley2D method (read from ClassDB, so
## new methods are covered), called with valid arguments on a pooled handle:
## the reused volley's full per-bullet state, its clocks and its timers equal
## those of the previous life, before and after a tick.

## Methods that legitimately act on a pooled handle, and why.
const EXEMPT := {
	"enable_volley": "the documented way to wake a parked or pooled volley explicitly",
	"queue_free": "Node API",
	"free": "Node API",
}

var _target: Node2D
var _path: Path2D
var _scene: PackedScene
var _calls: Array = []


func before_each() -> void:
	await super()
	_target = Node2D.new()
	_target.position = Vector2(300, 140)
	add(_target)
	_path = Path2D.new()
	_path.curve = Curve2D.new()
	for pt in [Vector2(0, 0), Vector2(40, 30), Vector2(80, -30), Vector2(120, 0)]:
		_path.curve.add_point(pt)
	add(_path)
	if _scene == null:
		var probe: Node = load("res://tests/scenes/attachment_probe.gd").new()
		_scene = PackedScene.new()
		_scene.pack(probe)
		probe.free()


func _curve(points: Array) -> Curve:
	var c := Curve.new()
	c.min_value = -10000.0
	c.max_value = 10000.0
	for p in points:
		c.add_point(p)
	return c


## A valid argument for `a` (the first, valid variant of the snapshot's
## argument table, tools/api_snapshot.py).
func _arg(a: Dictionary) -> Variant:
	var t := int(a.get("type", TYPE_NIL))
	var cn := String(a.get("class_name", ""))
	var n := String(a.get("name", ""))
	match t:
		TYPE_INT:
			if n == "bullet_index":
				return 1
			if n.ends_with("_start") or n == "start_index":
				return 0
			if n.ends_with("_end_inclusive") or n == "end_index_inclusive":
				return -1
			if cn.contains("."):
				var parts := cn.split(".")
				var vals := []
				for c in ClassDB.class_get_enum_constants(parts[0], parts[1], true):
					vals.append(ClassDB.class_get_integer_constant(parts[0], c))
				return vals[mini(1, vals.size() - 1)]
			return 1
		TYPE_FLOAT:
			return 37.5
		TYPE_BOOL:
			return true
		TYPE_VECTOR2:
			return Vector2(120, -40)
		TYPE_TRANSFORM2D:
			return Transform2D(0.3, Vector2(50, 20))
		TYPE_COLOR:
			return Color(0.5, 0.2, 0.9)
		TYPE_STRING, TYPE_STRING_NAME:
			return "default"
		TYPE_NODE_PATH:
			return get_path_to(_path)
		TYPE_DICTIONARY:
			return {}
		TYPE_CALLABLE:
			return func(...args) -> void: _calls.append(args.size())
		TYPE_PACKED_INT32_ARRAY:
			return PackedInt32Array([0, 2])
		TYPE_PACKED_FLOAT32_ARRAY:
			return PackedFloat32Array([1.0, 1.5])
		TYPE_PACKED_VECTOR2_ARRAY:
			return PackedVector2Array([Vector2(0, 0), Vector2(50, 10)])
		TYPE_OBJECT:
			return _object(cn)
		TYPE_NIL:
			if n.contains("position") or n.contains("node2d") or n.contains("target"):
				return _target
			return H.make_speed(1.0, 2.0, 3.0)
		TYPE_ARRAY:
			var hs := String(a.get("hint_string", ""))
			if hs != "" and ClassDB.class_exists(hs):
				return [_object(hs), _object(hs)]
			if n.contains("homing") or n.contains("node2d"):
				return [_target, Vector2(200, 100)]
			if hs == "Transform2D" or n.contains("transform"):
				return [Transform2D(0.0, Vector2(10, 0))]
			if hs == "Vector2":
				return [Vector2(0, 300), Vector2(40, 0)]
			return [1.0, true]
	return null


func _object(cls: String) -> Variant:
	match cls:
		"BulletSpeedData2D":
			return H.make_speed(250.0, 900.0, 50.0)
		"BulletRotationData2D":
			return H.make_rotation(2.0, 8.0, 1.0)
		"BulletCurvesData2D":
			var c := BulletCurvesData2D.new()
			c.movement_speed_curve = _curve([Vector2(0, 100), Vector2(1, 300)])
			c.x_direction_curve = _curve([Vector2(0, 0.0), Vector2(1, 1.0)])
			return c
		"BulletWobbleData2D":
			var w := BulletWobbleData2D.new()
			w.enabled = true
			w.amplitude = 25.0
			w.frequency_hz = 2.0
			return w
		"Curve2D":
			return _path.curve
		"PackedScene":
			return _scene
		"Shape2D", "CircleShape2D":
			return H.make_circle_shape(5.0)
		"SpriteFrames":
			return H.make_sprite_frames()
		"Node2D", "Node", "Object", "CanvasItem":
			return _target
		"Resource":
			return H.make_speed(1.0, 2.0, 3.0)
	return null


func _state(v: BulletVolley2D) -> Array:
	var rows := []
	for i in v.get_amount_bullets():
		rows.append([v.get_bullet_transform(i), v.debug_get_bullet_info(i), v.debug_get_orbiting_info(i),
				v.debug_get_bounce_info(i), v.debug_get_wobble_info(i), v.debug_get_curves_info(i)])
	rows.append(v.debug_get_clocks())
	rows.append(["timers", v.debug_get_timer_count()])
	return rows


## Writes through the pooled handle that the next life inherits.
func _leaks_through(method: Dictionary) -> String:
	var data := H.make_volley_data(4, 150.0, 30.0)
	var v: BulletVolley2D = factory.spawn_volley(data)
	factory.debug_advance_time(1.0 / 60.0)
	var before := str(_state(v))
	v.clear_all_bullets() # back to the pool
	if not v.is_pooled():
		return "not pooled"
	var args := []
	for a in method.get("args", []):
		args.append(_arg(a))
	v.callv(String(method["name"]), args)
	for err in get_errors(): # refusals of the stale handle are fine
		err.handled = true
	if not is_instance_valid(v):
		return ""
	var reused: BulletVolley2D = factory.spawn_volley(data)
	if reused != v:
		return "" # the call took the volley out of the pool (freed or woke it)
	factory.debug_advance_time(1.0 / 60.0)
	var after := str(_state(reused))
	reused.clear_all_bullets()
	for err in get_errors():
		err.handled = true
	return "" if after == before else "the next life inherited it"


func test_writes_through_a_pooled_handle_never_reach_the_next_life() -> void:
	var leaks: Array = []
	for m in ClassDB.class_get_method_list(&"BulletVolley2D", true):
		var name := String(m["name"])
		if name.begins_with("_") or name.begins_with("get_") or name.begins_with("is_") or name.begins_with("debug_") or EXEMPT.has(name):
			continue
		var why := _leaks_through(m)
		if why != "":
			leaks.append("%s: %s" % [name, why])
	assert_eq("\n".join(leaks), "", "no write through a pooled handle reaches the next life")


## A timer attached through a pooled handle is refused with the stale-handle
## error (it ran while pooled and carried into the next owner's life).
func test_a_timer_on_a_pooled_handle_is_refused() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 0.0, 30.0))
	v.clear_all_bullets()
	assert_true(v.is_pooled(), "pooled")
	v.attach_time_based_function(0.1, func() -> void: _calls.append(1), true, false)
	expect_error_sequence(["attach_time_based_function: this volley is in the pool (its last bullet died), so this handle is stale. Spawn a new volley instead."])
	assert_eq(v.debug_get_timer_count(), 0, "no timer was attached")
	for k in 12:
		await physics(1)
	assert_eq(_calls, [], "nothing ran while pooled")
