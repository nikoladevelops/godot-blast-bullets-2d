extends BlastBenchmark
## A bullet hell spawner's everyday mix, over and over: every 0.15 s a
## volley of 55 big bullets (128 px, random scale 1..2) is fired from one
## muzzle, homing on a target (turn rate 5) with the default wobble, all in
## ONE Shared default flock: bursts stacked at the muzzle, a crowd buzzing
## around the target, lifetime 4 s (about 1,500 bullets alive). Measures the
## flock pass with big, overlapping, random-sized bullets plus homing.

var flock := BulletFlock2D.new()
var target := Vector2(900, 500)


func describe() -> String:
	return "55-bullet homing bursts every 0.15 s, 128 px random sizes, wobble, one default flock"


func _shot() -> void:
	var tr: Array = []
	for i in 55:
		var a := TAU * i / 55.0
		var s := rng.randf_range(1.0, 2.0)
		tr.append(Transform2D(a, Vector2(200, 500) + Vector2(cos(a), sin(a)) * 12.0).scaled_local(Vector2(s, s)))
	var d := H.make_flight_data(tr, 300.0, 4.0)
	d.texture_size = Vector2(128, 128)
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(30.0)
	d.homing_smoothing = 5.0
	d.homing_take_control_of_texture_rotation = true
	var w := BulletWobbleData2D.new()
	w.enabled = true
	d.shared_bullet_wobble_data = w
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.flock_join(flock, BulletFlock2D.SCOPE_SHARED)
	v.shared_homing_deque_push_back_global_position_target(target)


func setup() -> void:
	# Start from a full crowd: 4 s of shots already in flight, fired at the
	# real cadence (9 ticks apart).
	for k in 26:
		_shot()
		for t in 9:
			factory.debug_advance_time(1.0 / 60.0)


func step(frame: int) -> void:
	if frame % 9 == 0:
		_shot()
	var s: Dictionary = factory.debug_get_flock_stats()
	extra["flock_pass_usec"] = s["pass_usec"]
	extra["neighbor_checks"] = s["neighbor_checks"]
