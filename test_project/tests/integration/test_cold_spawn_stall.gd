extends BlastTest
## Brand-new volleys (pool misses) enter the physics space fully configured:
## their layer, mask and monitorable are written before the area can pair with
## anything, and the area joins the space only once every shape sits at its
## bullet. Before, a fresh area joined the space carrying the server defaults
## (layer and mask 1) and N identity shapes: 40 new monitorable volleys of 64
## bullets fired from a node's _physics_process stalled the next physics step
## for ~3 s (external report). Cold volleys must still collide exactly like
## pooled ones.

## Fires its volleys from inside a physics callback, after the factory ticked.
class Shooter extends Node:
	var target_factory: BulletFactory2D
	var datas: Array = []
	var armed := false
	var fired: Array = []

	func _physics_process(_delta: float) -> void:
		if armed:
			armed = false
			for data in datas:
				fired.append(target_factory.spawn_volley(data))


func _ring_data(at: Vector2, count: int, layer: int, mask: int) -> BulletVolleyData2D:
	var data := BulletVolleyData2D.new()
	data.sprite_frames = H.make_sprite_frames()
	data.texture_size = Vector2(8, 8)
	var speed := BulletSpeedData2D.new()
	speed.speed = 100.0
	speed.max_speed = 100.0
	data.shared_bullet_speed_data = speed
	data.max_life_time = 0.05
	var shape := CircleShape2D.new()
	shape.radius = 4.0
	data.collision_shape = shape
	data.collision_layer = layer
	data.collision_mask = mask
	data.monitorable = true
	data.bullet_max_collision_count = 1
	var transforms: Array[Transform2D] = []
	for i in count:
		var angle := TAU * i / count
		transforms.append(Transform2D(angle, at + Vector2.from_angle(angle) * 24.0))
	data.transforms = transforms
	return data


## Still, monitorable bullets on mask value 4 (layer 3), the make_area default.
func _hit_data(n: int, lifetime: float) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 0.0, lifetime)
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	return d


func _shooter(datas: Array) -> Shooter:
	var shooter := Shooter.new()
	shooter.target_factory = factory
	shooter.datas = datas
	add(shooter)
	return shooter


## Worst wall-clock physics step among the next `frames` steps, in ms.
func _worst_step_ms(frames: int) -> float:
	var worst := 0
	for i in frames:
		var start := Time.get_ticks_usec()
		await get_tree().physics_frame
		worst = maxi(worst, Time.get_ticks_usec() - start)
	return worst / 1000.0


func test_new_volleys_fired_from_physics_do_not_stall_the_next_step() -> void:
	var datas: Array = []
	for i in 40: # rings 64 px apart: no two volleys overlap
		datas.append(_ring_data(Vector2(640 + 64 * (i % 10), 480 + 64 * (i / 10)), 64, 0, 1))
	var shooter := _shooter(datas)
	await physics(3)
	shooter.armed = true
	var worst := await _worst_step_ms(4)
	assert_eq(shooter.fired.size(), 40, "every volley spawned (all pool misses)")
	# Fixed: ~2-3 ms on the dev machine; broken: ~3000 ms. The budget leaves
	# room for a loaded parallel run without letting the quadratic step back in.
	assert_lt(worst, time_budget_ms(400.0), "no physics step stalls after 2560 new bullets (%.1f ms)" % worst)
	await idle(6)


func test_layer_with_bit_one_or_none_never_matters() -> void:
	# Layer 0 and layer 4 both stalled with mask 1: the default layer was in play.
	for layer in [0, 4]:
		var datas: Array = []
		for i in 20:
			datas.append(_ring_data(Vector2(640 + 64 * (i % 10), 480 + 64 * (i / 10)), 64, layer, 1))
		var shooter := _shooter(datas)
		await physics(2)
		shooter.armed = true
		var worst := await _worst_step_ms(4)
		assert_lt(worst, time_budget_ms(250.0), "layer %d: no stall (%.1f ms)" % [layer, worst])
		await idle(6)


func test_a_cold_volley_fired_from_physics_hits_on_its_first_step() -> void:
	var area := make_area(Vector2(300, 300), Vector2(60, 60), 4)
	await idle(2) # the area must be in the space for queries
	watch_signals(factory)
	var data := _hit_data(3, 5.0)
	data.transforms = [Transform2D(0.0, Vector2(300, 300)), Transform2D(0.0, Vector2(305, 300)), Transform2D(0.0, Vector2(2000, 2000))]
	data.bullet_max_collision_count = 1
	var shooter := _shooter([data])
	await physics(1)
	shooter.armed = true
	for i in 6:
		await physics(1)
		if get_signal_emit_count(factory, "area_entered") >= 2:
			break
	assert_eq(get_signal_emit_count(factory, "area_entered"), 2, "both overlapping bullets hit; the far one does not")
	var v: BulletVolley2D = shooter.fired[0]
	assert_eq([v.is_bullet_status_enabled(0), v.is_bullet_status_enabled(1), v.is_bullet_status_enabled(2)], [false, false, true], "the two hits were spent, the far bullet flies on")
	assert_true(is_instance_valid(area), "target untouched")


func test_a_cold_volley_never_hits_outside_its_mask() -> void:
	make_area(Vector2(300, 300), Vector2(60, 60), 1) # layer 1: the old default
	await idle(2)
	watch_signals(factory)
	var data := _hit_data(2, 5.0)
	data.transforms = [Transform2D(0.0, Vector2(300, 300)), Transform2D(0.0, Vector2(305, 300))]
	var shooter := _shooter([data])
	await physics(1)
	shooter.armed = true
	await physics(6)
	assert_eq(get_signal_emit_count(factory, "area_entered"), 0, "a fresh area never pairs under the default mask")


func test_a_pooled_volley_after_a_cold_one_still_hits() -> void:
	var data := _hit_data(2, 0.05)
	data.transforms = [Transform2D(0.0, Vector2(300, 300)), Transform2D(0.0, Vector2(2000, 2000))]
	factory.spawn_volley(data)
	for i in 30:
		await idle(1)
		if factory.get_active_bullet_count() == 0:
			break
	make_area(Vector2(300, 300), Vector2(60, 60), 4)
	await idle(2)
	watch_signals(factory)
	var reused: BulletVolley2D = factory.spawn_volley(data)
	for i in 6:
		await physics(1)
		if get_signal_emit_count(factory, "area_entered") >= 1:
			break
	assert_eq(get_signal_emit_count(factory, "area_entered"), 1, "the reused volley collides at its new pose")
	assert_true(is_instance_valid(reused))
