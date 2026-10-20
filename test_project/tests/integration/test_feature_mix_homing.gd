extends BlastTest
## Homing mixes with every other source of heading or force. Bullets leave
## the origin flying +X; a static target waits up and to the right. Whatever
## else steers or pushes them (flocking, wobble, inherited spawner momentum,
## texture rotation, speed curves, rotation data, movement patterns, time
## scale, snapping or per-bullet smoothing, size-aware reach, a bounce wall
## on the way), every bullet reaches the target within a time bound (and
## leaves on arrival, like a hit).

const TARGET := Vector2(320, 220)
const FRAMES := 360 # 6 s budget

var reached: Dictionary = {}

const CASES := ["plain", "snap", "texture_rotation", "flock", "flock_no_face", "wobble_lateral",
	"wobble_angular", "wobble_angular_face", "inherited_velocity", "speed_curve", "rotation_spin",
	"movement_pattern", "time_scale_half", "reach_counts_size", "per_bullet_smoothing", "bounce_wall_on_the_way",
	"gravity_light"]


func _flat_curve(value: float) -> Curve:
	var c := Curve.new()
	c.min_value = -1000.0
	c.max_value = 1000.0
	c.add_point(Vector2(0, value))
	c.add_point(Vector2(1, value))
	return c


func _data(n: int) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 260.0, 30.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(0, -20.0 * (n - 1) / 2.0 + 20.0 * i)))
	d.transforms = arr
	d.collision_shape = H.make_circle_shape(6.0)
	d.homing_smoothing = 4.0
	d.homing_take_control_of_texture_rotation = true
	d.homing_distance_before_reached = 12.0
	return d


func _configure(name: String, d: BulletVolleyData2D) -> void:
	match name:
		"snap":
			d.homing_smoothing = 0.0
		"flock", "flock_no_face":
			# Separation knocks bullets off the direct line: a turn circle
			# (speed / smoothing) wider than the reach radius would loiter
			# around the target forever (documented homing limit).
			d.homing_smoothing = 10.0
			d.homing_distance_before_reached = 30.0
		"texture_rotation":
			d.texture_rotation_radians = PI / 2.0
		"wobble_lateral", "wobble_angular", "wobble_angular_face":
			var w := BulletWobbleData2D.new()
			w.enabled = true
			w.mode = BulletWobbleData2D.WOBBLE_LATERAL if name == "wobble_lateral" else BulletWobbleData2D.WOBBLE_ANGULAR
			w.amplitude = 8.0 if name == "wobble_lateral" else 0.25
			w.frequency_hz = 3.0
			w.face_movement_direction = name == "wobble_angular_face"
			d.shared_bullet_wobble_data = w
		"speed_curve":
			var c := BulletCurvesData2D.new()
			c.movement_speed_curve = _flat_curve(260.0)
			c.movement_use_unit_curve = false
			d.shared_bullet_curves_data = c
		"rotation_spin":
			d.shared_bullet_rotation_data = H.make_rotation(6.0, 6.0)
		"reach_counts_size":
			d.homing_reach_counts_bullet_size = true
			d.homing_distance_before_reached = 2.0
		"bounce_wall_on_the_way":
			d.monitorable = true
			d.set_collision_mask_from_array([5])
			d.set_bounce_mask_from_array([5])
			d.bounce_mode = 1
			d.bullet_max_collision_count = 0
		"gravity_light":
			d.gravity = Vector2(0, 60)


func _after(name: String, v: BulletVolley2D) -> void:
	v.shared_homing_deque_push_back_global_position_target(TARGET)
	match name:
		"flock", "flock_no_face":
			var f := H.make_flock(15.0, 1.0, 1.0, 100.0, 30.0)
			f.face_heading = name == "flock"
			v.flock_join(f)
		"movement_pattern":
			var c := Curve2D.new()
			for p in [Vector2(0, 0), Vector2(30, 10), Vector2(60, -10), Vector2(90, 0)]:
				c.add_point(p)
			v.set_shared_movement_pattern_curve(c)
		"time_scale_half":
			factory.time_scale = 0.5
		"per_bullet_smoothing":
			v.all_bullets_set_homing_smoothing(3.0)


## A bullet that reaches its target hits it and leaves (as in a game): a
## flock's separation outranks homing, so a swarm never parks on one point.
func _on_reached(v: BulletVolley2D, i: int, _t: Node2D, _p: Vector2) -> void:
	if not reached.has(i):
		reached[i] = true
		v.disable_bullet(i)


func test_every_bullet_reaches_its_target(name: String = use_parameters(CASES)) -> void:
	reached.clear()
	var n := 3 if name.begins_with("flock") else 1
	if name == "bounce_wall_on_the_way":
		# A short wall straight ahead: the bullet bounces, then homes in around it.
		make_wall(Vector2(140, 0), Vector2(20, 60), 16)
	var d := _data(n)
	_configure(name, d)
	var inherited := Vector2(0, -80) if name == "inherited_velocity" else Vector2.ZERO
	var v: BulletVolley2D = factory.spawn_volley(d, inherited)
	v.bullet_homing_target_reached.connect(_on_reached)
	_after(name, v)
	var budget := FRAMES * (2 if name == "time_scale_half" else 1)
	for k in budget:
		await physics(1)
		if reached.size() == n:
			break
	assert_eq(reached.size(), n, "%s: every bullet reached the target" % name)
	for i in n:
		if v.is_bullet_status_enabled(i):
			assert_true(v.get_bullet_transform(i).is_finite() and v.get_bullet_velocity(i).is_finite(), "%s: finite" % name)
