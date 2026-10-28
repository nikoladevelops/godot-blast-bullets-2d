extends BlastTest
## Flocking mixes with every other source of heading or force. A clump of
## bullets flies +X almost on top of each other; a separation-only flock
## must spread it apart whatever else steers or pushes them (homing, a
## bounce wall, wobble, gravity, speed and direction curves, inherited
## momentum, texture rotation, rotation data owning the facing, a movement
## pattern, time scale), and every bullet stays finite.

const N := 8

const CASES := ["plain", "homing", "homing_no_control", "bounce_wall", "wobble_lateral", "wobble_angular",
	"gravity", "speed_curve", "direction_curve", "inherited_velocity", "texture_rotation", "rotation_spin",
	"adjust_rotation", "movement_pattern", "time_scale_half"]


func _data() -> BulletVolleyData2D:
	var d := H.make_volley_data(N, 200.0, 30.0)
	var arr: Array = []
	for i in N:
		arr.append(Transform2D(0.0, Vector2(0.5 * i, 0.3 * i))) # nearly stacked
	d.transforms = arr
	d.collision_shape = H.make_circle_shape(4.0)
	return d


func _configure(name: String, d: BulletVolleyData2D) -> void:
	match name:
		"homing", "homing_no_control":
			d.homing_smoothing = 3.0
			d.homing_take_control_of_texture_rotation = name == "homing"
		"bounce_wall":
			d.monitorable = true
			d.set_collision_mask_from_array([5])
			d.set_bounce_mask_from_array([5])
			d.bounce_mode = 1
			d.bullet_max_collision_count = 0
		"wobble_lateral", "wobble_angular":
			var w := BulletWobbleData2D.new()
			w.enabled = true
			w.mode = BulletWobbleData2D.WOBBLE_LATERAL if name == "wobble_lateral" else BulletWobbleData2D.WOBBLE_ANGULAR
			w.amplitude = 6.0 if name == "wobble_lateral" else 0.2
			w.frequency_hz = 2.0
			d.shared_bullet_wobble_data = w
		"gravity":
			d.gravity = Vector2(0, 150)
		"speed_curve", "direction_curve":
			var c := BulletCurvesData2D.new()
			if name == "speed_curve":
				c.movement_speed_curve = H.make_flat_curve(200.0)
				c.movement_use_unit_curve = false
			else:
				c.y_direction_curve = H.make_flat_curve(0.1)
				c.y_direction_use_unit_curve = false
			d.shared_bullet_curves_data = c
		"texture_rotation":
			d.texture_rotation_radians = PI / 2.0
		"rotation_spin":
			d.shared_bullet_rotation_data = H.make_rotation(3.0, 3.0)
		"adjust_rotation":
			d.adjust_direction_based_on_rotation = true
			d.shared_bullet_rotation_data = H.make_rotation(0.5, 0.5)


func test_a_separating_flock_spreads_the_clump(name: String = use_parameters(CASES)) -> void:
	handle_events(factory) # the wall hits are handled; this test pins the flock
	if name == "bounce_wall":
		make_wall(Vector2(120, 0), Vector2(20, 800), 16)
	var d := _data()
	_configure(name, d)
	var v: BulletVolley2D = factory.spawn_volley(d, Vector2(60, 0) if name == "inherited_velocity" else Vector2.ZERO)
	var f := H.make_flock(20.0, 0.0, 0.0, 60.0, 30.0)
	v.flock_join(f)
	if name.begins_with("homing"):
		v.shared_homing_deque_push_back_global_position_target(Vector2(600, 0))
	match name:
		"movement_pattern":
			var c := Curve2D.new()
			for p in [Vector2(0, 0), Vector2(30, 8), Vector2(60, -8), Vector2(90, 0)]:
				c.add_point(p)
			v.set_shared_movement_pattern_curve(c)
		"time_scale_half":
			factory.time_scale = 0.5
	var start := min_pair_distance(origins(v))
	for k in (120 if name == "time_scale_half" else 60):
		await physics(1)
	if name == "homing_no_control":
		expect_warning_sequence(["BulletVolley2D has homing targets but homing_take_control_of_texture_rotation is false (and no movement pattern/rotation data), so homing will not steer bullets. Set it to true."], "homing without texture control steers nothing, and says so")
	var spread := min_pair_distance(origins(v))
	assert_gt(spread, maxf(start, 3.0), "%s: the clump spread (%.2f -> %.2f px)" % [name, start, spread])
	assert_eq(broken_bullets(v), [], "%s: finite" % name)
