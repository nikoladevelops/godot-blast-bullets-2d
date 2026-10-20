extends BlastTest
## Orbiting mixes with every other source of heading or force. A bullet
## homes in, locks onto its ring around a fixed point and must keep circling
## at its radius whatever else steers or pushes it (gravity, wobble,
## inherited momentum, speed / direction curves, texture rotation, rotation
## data, a flock, bounce armed, time scale). The orbit owns the
## displacement while locked.

const CENTER := Vector2(250, 0)
const RADIUS := 50.0
const TOLERANCE := 4.0

const CASES := ["plain", "gravity", "wobble_lateral", "wobble_angular", "inherited_velocity", "speed_curve",
	"direction_curve", "texture_rotation", "rotation_spin", "flock", "bounce_armed", "time_scale_half"]


func _data(n: int) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 300.0, 30.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(0, 12.0 * i)))
	d.transforms = arr
	d.homing_smoothing = 30.0
	d.homing_take_control_of_texture_rotation = true
	return d


func _configure(name: String, d: BulletVolleyData2D) -> void:
	match name:
		"gravity":
			d.gravity = Vector2(0, 200)
		"wobble_lateral", "wobble_angular":
			var w := BulletWobbleData2D.new()
			w.enabled = true
			w.mode = BulletWobbleData2D.WOBBLE_LATERAL if name == "wobble_lateral" else BulletWobbleData2D.WOBBLE_ANGULAR
			w.amplitude = 8.0 if name == "wobble_lateral" else 0.3
			w.frequency_hz = 3.0
			d.shared_bullet_wobble_data = w
		"speed_curve", "direction_curve":
			var c := BulletCurvesData2D.new()
			if name == "speed_curve":
				c.movement_speed_curve = H.make_flat_curve(300.0)
				c.movement_use_unit_curve = false
			else:
				c.y_direction_curve = H.make_flat_curve(0.3)
				c.y_direction_use_unit_curve = false
			d.shared_bullet_curves_data = c
		"texture_rotation":
			d.texture_rotation_radians = PI / 2.0
		"rotation_spin":
			d.shared_bullet_rotation_data = H.make_rotation(4.0, 4.0)
		"bounce_armed":
			d.set_collision_mask_from_array([5])
			d.set_bounce_mask_from_array([5])
			d.bounce_mode = 1


func test_a_locked_orbit_keeps_its_radius(name: String = use_parameters(CASES)) -> void:
	var n := 3 if name == "flock" else 1
	var d := _data(n)
	_configure(name, d)
	var v: BulletVolley2D = factory.spawn_volley(d, Vector2(80, 40) if name == "inherited_velocity" else Vector2.ZERO)
	for i in n:
		v.bullet_homing_push_back_global_position_target(i, CENTER)
		v.bullet_enable_orbiting(i, RADIUS, BulletVolley2D.OrbitRight, BulletVolley2D.FaceTarget)
	if name == "flock":
		v.flock_join(H.make_flock(10.0, 0.0, 0.0, 60.0, 20.0))
	if name == "time_scale_half":
		factory.time_scale = 0.5
	var locked := false
	for k in 480:
		factory.debug_advance_time(1.0 / 60.0)
		if range(n).all(func(i): return v.bullet_is_orbiting_locked(i)):
			locked = true
			break
	assert_true(locked, "%s: every bullet locked onto its ring" % name)
	var worst := 0.0
	for k in 120:
		factory.debug_advance_time(1.0 / 60.0)
		for i in n:
			worst = maxf(worst, absf(v.get_bullet_transform(i).origin.distance_to(CENTER) - RADIUS))
	assert_lt(worst, TOLERANCE, "%s: the orbit holds its radius (worst error %.2f px)" % [name, worst])
	assert_eq(broken_bullets(v), [], "%s: finite" % name)
