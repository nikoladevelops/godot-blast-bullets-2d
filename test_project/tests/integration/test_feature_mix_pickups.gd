extends BlastTest
## Pickup conversion mixes with every feature the source bullets carried
## (a bounce mirror, flocking, homing, direction curves, wobble, a locked
## orbit, inherited momentum, gravity, spin, texture rotation, time scale):
## in both the built-in burst-and-magnet motion and Keep Bullet Motion,
## every converted bullet becomes a pickup that reaches its collector.

const P := preload("res://tests/common/pickup_test_helpers.gd")
const N := 4

var collected := 0

const SOURCES := ["plain", "bounced", "flock", "homing", "direction_curve", "wobble", "orbit_locked",
	"inherited_velocity", "gravity", "spin", "texture_rotation", "time_scale_half"]


func _cases() -> Array:
	var out: Array = []
	for s in SOURCES:
		for mode in [BulletPickupData2D.MOTION_BURST_AND_MAGNET, BulletPickupData2D.MOTION_KEEP_BULLET_MOTION]:
			out.append([s, mode])
	return out


func _source(name: String) -> BulletVolley2D:
	var d := H.make_volley_data(N, 200.0, 60.0)
	var arr: Array = []
	for i in N:
		arr.append(Transform2D(0.0, Vector2(0, 10.0 * i)))
	d.transforms = arr
	d.collision_shape = H.make_circle_shape(4.0)
	match name:
		"bounced":
			make_wall(Vector2(80, 0), Vector2(20, 400), 16)
			d.monitorable = true
			d.set_collision_mask_from_array([5])
			d.set_bounce_mask_from_array([5])
			d.bounce_mode = 1
			d.bullet_max_collision_count = 0
		"direction_curve":
			var c := BulletCurvesData2D.new()
			c.y_direction_curve = H.make_flat_curve(0.5)
			c.y_direction_use_unit_curve = false
			d.shared_bullet_curves_data = c
		"wobble":
			var w := BulletWobbleData2D.new()
			w.enabled = true
			w.amplitude = 8.0
			w.frequency_hz = 3.0
			d.shared_bullet_wobble_data = w
		"gravity":
			d.gravity = Vector2(0, 100)
		"spin":
			d.shared_bullet_rotation_data = H.make_rotation(5.0, 5.0)
		"texture_rotation":
			d.texture_rotation_radians = PI / 2.0
		"homing", "orbit_locked":
			d.homing_smoothing = 20.0
			d.homing_take_control_of_texture_rotation = true
	var v: BulletVolley2D = factory.spawn_volley(d, Vector2(50, 30) if name == "inherited_velocity" else Vector2.ZERO)
	match name:
		"flock":
			v.flock_join(H.make_flock(10.0, 1.0, 1.0, 60.0, 20.0))
		"homing":
			v.shared_homing_deque_push_back_global_position_target(Vector2(-300, 200))
		"orbit_locked":
			for i in N:
				v.bullet_homing_push_back_global_position_target(i, Vector2(150, 0))
				v.bullet_enable_orbiting(i, 40.0)
		"time_scale_half":
			factory.time_scale = 0.5
	return v


func test_every_converted_bullet_is_collected(p: Array = use_parameters(_cases())) -> void:
	var name: String = p[0]
	collected = 0
	factory.pickup_collected.connect(func(_t, _p, _v, _i, _val) -> void: collected += 1)
	var v := _source(name)
	for k in 45: # the source flies (bounces, locks its orbit, flocks...)
		await physics(1)
	var live := 0
	for i in N:
		if v.is_bullet_status_enabled(i):
			live += 1
	assert_gt(live, 0, "%s: bullets to convert" % name)
	var coin := P.coin()
	coin.motion_mode = p[1]
	coin.magnet_delay_sec = 0.05
	var collector := Node2D.new()
	collector.position = Vector2(-200, -150)
	add(collector)
	await idle(1)
	factory.convert_all_bullets_to_pickups(coin, collector)
	for k in 600:
		await physics(1)
		if collected >= live:
			break
	assert_eq(collected, live, "%s (mode %d): every pickup reached its collector" % [name, p[1]])
