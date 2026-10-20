extends BlastTest
## Bouncing mixes with every other source of heading or force. Bullets fly
## +X into a tall bounce wall (its face at x = 300). Whatever else steers or
## pushes them (homing with or without texture control, flocking, wobble,
## gravity, inherited spawner momentum, rotation data owning the heading,
## texture rotation, speed / direction curves, movement patterns, time
## scale, the bounce visual options), each bullet bounces ONCE and then
## moves away from the wall for the next 10 ticks: no source undoes the
## reflection and drives it back in.

const WALL_FACE_X := 300.0
const TICKS_AFTER := 10

var bounces: Dictionary = {} # bullet index -> physics frame of its first bounce
var bounce_count := 0
var frame := 0
var split_children: Array = []


func _flat_curve(value: float) -> Curve:
	var c := Curve.new()
	c.min_value = -1000.0
	c.max_value = 1000.0
	c.add_point(Vector2(0, value))
	c.add_point(Vector2(1, value))
	return c


## `n` bullets stacked vertically at x = 100, flying +X at `speed`, bouncing
## off layer value 16 with the precise (flat face) normal.
func _data(n: int, speed: float) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, speed, 10.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(100, -24.0 * (n - 1) / 2.0 + 24.0 * i)))
	d.transforms = arr
	d.monitorable = true
	d.collision_shape = H.make_circle_shape(6.0)
	d.set_collision_mask_from_array([5])
	d.set_bounce_mask_from_array([5])
	d.bounce_mode = 1 # precise: a flat wall bounces flat (radial is for round targets)
	d.bullet_max_collision_count = 0
	return d


## Case name -> [bullets, own speed, inherited velocity]; _configure edits
## the data before the spawn, _after the live volley.
const CASES := ["plain", "rotate_texture_off", "smooth_visual",
	"homing_control", "homing_control_rotate_off", "homing_control_smooth",
	"homing_no_control", "homing_no_control_rotate_off", "homing_texture_rotation",
	"flock_face", "flock_no_face", "wobble_lateral", "wobble_angular", "wobble_angular_face",
	"gravity", "inherited_velocity", "adjust_rotation", "adjust_rotation_rotate_off",
	"texture_rotation", "speed_curve", "direction_curve_override", "direction_curve_additive",
	"movement_pattern", "time_scale_half",
	"homing_flock", "homing_wobble_angular", "homing_gravity", "homing_inherited", "homing_direction_curve",
	"flock_wobble", "split_children", "speed_curve_inherited"]


func _configure(name: String, d: BulletVolleyData2D) -> void:
	if name.begins_with("homing"):
		d.homing_smoothing = 1.0 # turns slowly toward the target up and back
		d.homing_take_control_of_texture_rotation = not name.begins_with("homing_no_control")
	if name.ends_with("rotate_off"):
		d.bounce_rotate_texture = false
	if name.ends_with("smooth") or name == "smooth_visual":
		d.bounce_rotation_smooth = 0.2
	match name:
		"homing_texture_rotation", "texture_rotation":
			d.texture_rotation_radians = PI / 2.0
		"wobble_lateral", "wobble_angular", "wobble_angular_face":
			var w := BulletWobbleData2D.new()
			w.enabled = true
			w.mode = BulletWobbleData2D.WOBBLE_LATERAL if name == "wobble_lateral" else BulletWobbleData2D.WOBBLE_ANGULAR
			w.amplitude = 10.0 if name == "wobble_lateral" else 0.3
			w.frequency_hz = 3.0
			w.face_movement_direction = name == "wobble_angular_face"
			d.shared_bullet_wobble_data = w
		"gravity", "homing_gravity":
			d.gravity = Vector2(0, 300)
		"homing_wobble_angular", "flock_wobble":
			var w := BulletWobbleData2D.new()
			w.enabled = true
			w.mode = BulletWobbleData2D.WOBBLE_ANGULAR
			w.amplitude = 0.3
			w.frequency_hz = 3.0
			d.shared_bullet_wobble_data = w
		"homing_direction_curve":
			var c := BulletCurvesData2D.new()
			c.x_direction_curve = _flat_curve(1.0)
			c.x_direction_use_unit_curve = false
			c.x_direction_curve_mode = 1
			d.shared_bullet_curves_data = c
		"adjust_rotation", "adjust_rotation_rotate_off":
			d.adjust_direction_based_on_rotation = true
			d.shared_bullet_rotation_data = H.make_rotation(0.0, 0.0)
		"speed_curve", "speed_curve_inherited":
			var c := BulletCurvesData2D.new()
			c.movement_speed_curve = _flat_curve(300.0)
			c.movement_use_unit_curve = false
			d.shared_bullet_curves_data = c
		"direction_curve_override", "direction_curve_additive":
			var c := BulletCurvesData2D.new()
			c.x_direction_curve = _flat_curve(1.0 if name.ends_with("override") else 0.2)
			c.x_direction_use_unit_curve = false
			c.x_direction_curve_mode = 1 if name.ends_with("override") else 0
			d.shared_bullet_curves_data = c


func _after(name: String, v: BulletVolley2D) -> void:
	if name.begins_with("homing"):
		v.shared_homing_deque_push_back_global_position_target(Vector2(100, -500))
	match name:
		"split_children":
			var stage := BulletSplitStage2D.new()
			stage.count = 2
			stage.split_on_bounce = true
			stage.split_on_hit = false
			stage.split_on_lifetime_over = false
			stage.parent_survives = true
			stage.max_splits_per_bullet = 1
			stage.spread_degrees = 40.0
			var chain := BulletSplitData2D.new()
			chain.stages = [stage]
			var child := _data(2, 300.0)
			v.split_set_data(chain, child)
			factory.bullet_split.connect(func(_p, _c, children, _d) -> void: split_children.append(children))
		"flock_face", "flock_no_face", "homing_flock", "flock_wobble":
			var f := H.make_flock(20.0, 1.0, 1.0, 100.0, 40.0)
			f.face_heading = name == "flock_face"
			v.flock_join(f)
		"movement_pattern":
			var c := Curve2D.new()
			for p in [Vector2(0, 0), Vector2(30, 12), Vector2(60, -12), Vector2(90, 0)]:
				c.add_point(p)
			v.set_shared_movement_pattern_curve(c)
		"time_scale_half":
			factory.time_scale = 0.5


func inherited_case(name: String) -> bool:
	return name.ends_with("inherited") or name == "inherited_velocity"


func _on_bounce(_t: Object, _v: BulletVolley2D, i: int) -> void:
	bounce_count += 1
	if not bounces.has(i):
		bounces[i] = frame


func test_every_bullet_leaves_the_wall_after_its_bounce(name: String = use_parameters(CASES)) -> void:
	bounces.clear()
	bounce_count = 0
	frame = 0
	make_wall(Vector2(WALL_FACE_X + 20.0, 0), Vector2(40, 3000), 16)
	factory.bounce_body_entered.connect(_on_bounce)
	split_children.clear()
	var n := 3 if name.contains("flock") else 1
	var d := _data(n, 150.0 if inherited_case(name) else 300.0)
	_configure(name, d)
	var inherited := Vector2(400, 0) if name.ends_with("inherited") or name == "inherited_velocity" else Vector2.ZERO
	var v: BulletVolley2D = factory.spawn_volley(d, inherited)
	_after(name, v)
	var xs: Dictionary = {} # bullet -> [x per frame after its bounce]
	for k in 240:
		await physics(1)
		frame += 1
		for i in bounces:
			if not xs.has(i):
				xs[i] = []
			if xs[i].size() <= TICKS_AFTER + 2 and v.is_bullet_status_enabled(i):
				xs[i].append(v.get_bullet_transform(i).origin.x)
		if bounces.size() == n and xs.values().all(func(a): return a.size() > TICKS_AFTER + 2):
			break
	assert_eq(bounces.size(), n, "%s: every bullet bounced" % name)
	var problems: Array = []
	for i in xs:
		var a: Array = xs[i]
		# Two ticks of slack: the nudge and the first reflected move.
		for t in range(2, a.size() - 1):
			if a[t + 1] >= a[t] - 0.01:
				problems.append("bullet %d tick %d after its bounce: x %.2f -> %.2f (not moving away)" % [i, t, a[t], a[t + 1]])
				break
		if not a.is_empty() and a[-1] >= WALL_FACE_X:
			problems.append("bullet %d is still in the wall (x %.2f)" % [i, a[-1]])
	assert_eq("\n".join(problems), "", name)
	assert_eq(bounce_count, n, "%s: one bounce per bullet (no re-hits)" % name)
	for i in n:
		assert_true(v.get_bullet_transform(i).is_finite() and v.get_bullet_velocity(i).is_finite(), "%s: finite" % name)
	if name == "split_children":
		assert_gt(split_children.size(), 0, "the bounce split")
		for children in split_children:
			if is_instance_valid(children):
				for i in children.get_amount_bullets():
					if children.is_bullet_status_enabled(i):
						assert_lt(children.get_bullet_velocity(i).x, 0.0, "split children fly away from the wall")
