extends BlastTest
## Grazing mixes with every other source of motion. A bullet flies past a
## graze target (one ring, Once); whatever else moves it (a bounce wall
## behind the target that sends it back past it, homing, a flock, wobble,
## gravity, inherited momentum, speed curves, a movement pattern, time
## scale, a teleport), it grazes the ring exactly once per life and exits
## exactly once.

const CASES := ["plain", "bounce_back", "homing", "flock", "wobble_lateral", "gravity", "inherited_velocity",
	"speed_curve", "movement_pattern", "time_scale_half", "teleport_across"]


func test_one_graze_and_one_exit_per_life(name: String = use_parameters(CASES)) -> void:
	var target := make_graze_target(Vector2(200, 20), &"mix_graze")
	if name == "bounce_back":
		make_wall(Vector2(320, 0), Vector2(20, 400), 16)
	var n := 3 if name == "flock" else 1
	var d := H.make_volley_data(n, 300.0, 60.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(0, 2.0 * i)))
	d.transforms = arr
	d.collision_shape = H.make_circle_shape(4.0)
	match name:
		"bounce_back":
			d.monitorable = true
			d.set_collision_mask_from_array([5])
			d.set_bounce_mask_from_array([5])
			d.bounce_mode = 1
			d.bullet_max_collision_count = 0
		"homing":
			d.homing_smoothing = 2.0
			d.homing_take_control_of_texture_rotation = true
		"wobble_lateral":
			var w := BulletWobbleData2D.new()
			w.enabled = true
			w.amplitude = 6.0
			w.frequency_hz = 3.0
			d.shared_bullet_wobble_data = w
		"gravity":
			d.gravity = Vector2(0, 40)
		"speed_curve":
			var c := BulletCurvesData2D.new()
			c.movement_speed_curve = H.make_flat_curve(300.0)
			c.movement_use_unit_curve = false
			d.shared_bullet_curves_data = c
	var v: BulletVolley2D = factory.spawn_volley(d, Vector2(60, 0) if name == "inherited_velocity" else Vector2.ZERO)
	v.graze_set_zones([H.make_graze_zone([80.0])], &"mix_graze")
	match name:
		"homing":
			v.shared_homing_deque_push_back_global_position_target(Vector2(600, 20))
		"flock":
			v.flock_join(H.make_flock(10.0, 0.0, 0.0, 40.0, 12.0))
		"movement_pattern":
			var c := Curve2D.new()
			for p in [Vector2(0, 0), Vector2(30, 6), Vector2(60, -6), Vector2(90, 0)]:
				c.add_point(p)
			v.set_shared_movement_pattern_curve(c)
		"time_scale_half":
			factory.time_scale = 0.5
	var log := H.record_graze(factory)
	for k in (240 if name == "time_scale_half" else 120):
		await physics(1)
		if name == "teleport_across" and k == 38:
			v.teleport_bullet(0, Vector2(500, 20)) # inside the ring now: jumps out of it mid-visit
	var per_bullet := {}
	for e in log:
		var key := "%s:%d" % [e[0], e[3]]
		per_bullet[key] = int(per_bullet.get(key, 0)) + 1
	var grazers := 0
	for i in n:
		var enters := int(per_bullet.get("enter:%d" % i, 0))
		grazers += enters
		# A flock may steer a bullet clear of the ring: never more than once,
		# and one exit per graze. Every other mix passes through it.
		if name == "flock":
			assert_lte(enters, 1, "%s: bullet %d grazed at most once (%s)" % [name, i, H.graze_kinds(log)])
		else:
			assert_eq(enters, 1, "%s: bullet %d grazed once (%s)" % [name, i, H.graze_kinds(log)])
		assert_eq(int(per_bullet.get("exit:%d" % i, 0)), enters, "%s: bullet %d exited once per graze (%s)" % [name, i, H.graze_kinds(log)])
	assert_gt(grazers, 0, "%s: the ring was grazed" % name)
	assert_true(is_instance_valid(target), "the target stayed")
