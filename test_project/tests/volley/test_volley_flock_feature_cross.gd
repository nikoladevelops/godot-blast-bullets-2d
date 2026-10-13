extends BlastTest
## Flocking crossed with every other volley feature: each test flies a flock
## next to one feature and asserts BOTH still do their job (graze reports,
## flight sounds play, attachments ride their bullets, trails bake, per-bullet
## homing closes in, orbits lock, wobble weaves, teleports keep the flock,
## velocity and direction setters are honored, freeze/wake churn, pickups,
## area clears, randomized speeds, two factories, a paused factory, a reset
## from a handler) with finite positions, unit headings and a dangling-free
## factory throughout.

const DT := 1.0 / 60.0
const P := preload("res://tests/common/pickup_test_helpers.gd")


func after_all() -> void:
	# The flight-sound test plays real streams: the same headless-audio drain
	# as the sound suites (the mixer thread releases playbacks a moment later).
	OS.delay_msec(500)
	await super()


func _cluster(count: int, at := Vector2.ZERO, seed_value := 3, spread := 40.0) -> Array:
	seed(seed_value)
	var tr: Array = []
	for k in count:
		tr.append(Transform2D(randf_range(-0.6, 0.6), at + Vector2(randf_range(0, spread), randf_range(0, spread))))
	return tr


func _flock_volley(tr: Array, flock: BulletFlock2D = null, speed := 120.0) -> BulletVolley2D:
	var v: BulletVolley2D = factory.spawn_volley(H.make_flight_data(tr, speed, 1000.0))
	v.flock_join(flock if flock != null else BulletFlock2D.new())
	return v


func _step(n: int) -> void:
	for k in n:
		assert_true(factory.debug_advance_time(DT), "tick %d runs" % k)


## Finite pose, unit heading for every live bullet + a consistent factory.
func _broken(v: BulletVolley2D) -> Array:
	var bad: Array = []
	if not is_instance_valid(v) or v.debug_get_life_state() != "active":
		return bad
	for i in v.get_amount_bullets():
		if not v.is_bullet_status_enabled(i):
			continue
		var d: Vector2 = v.get_bullet_direction(i)
		if not v.get_bullet_transform(i).is_finite() or absf(d.length() - 1.0) > 1e-4:
			bad.append("bullet %d: %s %s" % [i, v.get_bullet_transform(i).origin, d])
	var dangling: Dictionary = factory.debug_assert_no_dangling()
	if not dangling["ok"]:
		bad.append(dangling["error"])
	return bad


func _steered(v: BulletVolley2D) -> bool:
	for i in v.get_amount_bullets():
		if v.debug_get_bullet_flock_steering(i) != Vector2():
			return true
	return false


func test_graze_reports_while_the_flock_steers() -> void:
	make_graze_target(Vector2(150, 20))
	var log := H.record_graze(factory)
	var v := _flock_volley(_cluster(8))
	v.graze_set_zones([H.make_graze_zone([80.0])], &"graze_targets") # the default flock spreads the cluster
	_step(90)
	assert_gt(log.size(), 0, "the flock grazed the target")
	assert_true(_steered(v), "and kept flocking")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_flight_sounds_play_while_the_flock_steers() -> void:
	var v := _flock_volley(_cluster(6))
	v.sound_set_effects([H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT)])
	var before := int(factory.debug_get_sound_stats()["plays_total"])
	_step(30)
	assert_gt(int(factory.debug_get_sound_stats()["plays_total"]), before, "flight sounds played")
	assert_true(_steered(v), "and the flock steered")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_attachments_ride_their_flocking_bullets() -> void:
	var v := _flock_volley(_cluster(5))
	v.all_bullets_set_attachment(make_probe_scene(), Vector2.ZERO, true)
	_step(45)
	assert_true(_steered(v), "the flock steered")
	for i in 5:
		var att: Node2D = v.bullet_get_attachment(i)
		assert_not_null(att, "bullet %d keeps its attachment" % i)
		if att != null:
			assert_almost_eq(att.global_position, v.get_bullet_global_transform(i).origin, Vector2(0.01, 0.01), "attachment %d sits on its turned bullet" % i)
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_trails_bake_behind_flocking_bullets() -> void:
	var d := H.make_flight_data(_cluster(6), 120.0, 1000.0)
	d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW, 2)]
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.flock_join(BulletFlock2D.new())
	_step(30)
	assert_gt(int(v.debug_get_effect_layers_info().get("trail_bake_count", 0)), 0, "trails bake")
	assert_true(_steered(v), "the flock steered")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_per_bullet_homing_closes_in_while_flocking() -> void:
	var d := H.make_flight_data(_cluster(6), 150.0, 1000.0)
	d.homing_smoothing = 4.0
	d.homing_take_control_of_texture_rotation = true
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.flock_join(BulletFlock2D.new())
	var targets := [Vector2(400, -200), Vector2(400, 260)]
	var start: Array = []
	for i in 6:
		v.bullet_homing_push_back_global_position_target(i, targets[i % 2])
		start.append(v.get_bullet_transform(i).origin.distance_to(targets[i % 2]))
	_step(90) # 1.5 s: the default separation pushes the pairs sideways first
	for i in 6:
		assert_lt(v.get_bullet_transform(i).origin.distance_to(targets[i % 2]), start[i] - 100.0, "bullet %d closes in on its own target" % i)
	assert_true(_steered(v), "while the flock steers")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_orbiting_bullets_still_lock_onto_their_ring() -> void:
	var d := H.make_flight_data([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 30)), Transform2D(0.0, Vector2(0, -30))], 300.0, 1000.0)
	d.homing_smoothing = 30.0
	d.homing_take_control_of_texture_rotation = true
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.flock_join(H.make_flock(2.0, 1.0, 1.0))
	v.shared_homing_deque_push_back_global_position_target(Vector2(250, 0))
	for b in 3:
		v.bullet_enable_orbiting(b, 50.0, BulletVolley2D.OrbitRight, BulletVolley2D.FaceTarget)
	var locked := false
	for tick in 300: # early break once every bullet locked
		_step(1)
		locked = v.bullet_is_orbiting_locked(0) and v.bullet_is_orbiting_locked(1) and v.bullet_is_orbiting_locked(2)
		if locked:
			break
	assert_true(locked, "the flock never keeps an orbit from locking")
	_step(30)
	for b in 3:
		assert_almost_eq(v.get_bullet_transform(b).origin.distance_to(Vector2(250, 0)), 50.0, 1.5, "bullet %d rides its ring" % b)
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_wobble_weaves_on_top_of_the_flock() -> void:
	var d := H.make_flight_data(_cluster(6), 120.0, 1000.0)
	var wobble := BulletWobbleData2D.new()
	wobble.enabled = true
	wobble.amplitude = 25.0
	wobble.frequency_hz = 2.0
	wobble.face_movement_direction = true
	d.shared_bullet_wobble_data = wobble
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.flock_join(BulletFlock2D.new())
	var problems: Array = []
	for tick in 90:
		_step(1)
		problems.append_array(_broken(v))
	assert_eq(problems.slice(0, 4), [], "finite, unit, consistent every tick")
	assert_true(_steered(v), "the flock steered")


func test_a_teleport_moves_the_whole_flock_without_breaking_it() -> void:
	var v := _flock_volley(_cluster(8))
	_step(5)
	var before: Array = []
	for i in 8:
		before.append(v.debug_get_bullet_flock_neighbors(i))
	var pos0: Vector2 = v.get_bullet_transform(0).origin
	factory.teleport_shift_all_bullets(Vector2(5000, -3000))
	assert_almost_eq(v.get_bullet_transform(0).origin, pos0 + Vector2(5000, -3000), Vector2(0.01, 0.01), "shifted")
	_step(1)
	var after: Array = []
	for i in 8:
		after.append(v.debug_get_bullet_flock_neighbors(i))
	assert_eq(after, before, "the flock moved as a whole: same flockmates")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_velocity_and_direction_setters_are_honored() -> void:
	var v := _flock_volley(_cluster(6), H.make_flock(1.0, 1.0, 1.0))
	_step(5)
	v.bullet_set_velocity(0, Vector2(0, -150))
	assert_almost_eq(v.get_bullet_velocity(0), Vector2(0, -150), Vector2(0.01, 0.01), "the exact velocity that was set")
	v.set_bullet_direction(1, Vector2(-1, 0))
	assert_almost_eq(v.get_bullet_direction(1), Vector2(-1, 0), Vector2(1e-5, 1e-5), "the direction that was set")
	_step(1)
	assert_gt(v.get_bullet_direction(0).dot(Vector2(0, -1)), 0.9, "the flock turns on from the set heading, it does not snap back")
	assert_gt(v.get_bullet_direction(1).dot(Vector2(-1, 0)), 0.9, "same for the direction setter")
	assert_almost_eq(v.get_bullet_velocity(0).length(), 150.0, 0.01, "the flock never changes the speed")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_freeze_and_wake_churn() -> void:
	var v := _flock_volley(_cluster(10), H.make_flock(1.5, 1.0, 1.0))
	v.set_is_auto_pooling_enabled(false)
	seed(77)
	var problems: Array = []
	for tick in 120:
		var i := randi() % 10
		if v.is_bullet_status_enabled(i):
			v.disable_bullet(i)
		else:
			v.enable_bullet(i)
		_step(1)
		problems.append_array(_broken(v))
		for j in 10:
			if not v.is_bullet_status_enabled(j) and v.debug_get_bullet_flock_neighbors(j) != 0:
				problems.append("tick %d: frozen bullet %d was gathered" % [tick, j])
	assert_eq(problems.slice(0, 4), [], "frozen bullets never flock, the rest stays consistent")


func test_converting_a_flock_to_pickups_leaves_no_flocking_behind() -> void:
	var target := Node2D.new()
	target.position = Vector2(-300, 0)
	add(target)
	var v := _flock_volley(_cluster(6))
	_step(5)
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 1, "armed")
	var pickups: Array = v.convert_to_pickups(P.coin(), target)
	assert_gt(pickups.size(), 0, "converted into pickup volleys")
	for pv in pickups:
		assert_null((pv as BulletVolley2D).flock_get(), "a pickup volley never flocks")
	_step(3)
	var stats: Dictionary = factory.debug_get_flock_stats()
	assert_eq(stats["armed_volleys"], 0, "the converted volley left the pool with its flock dropped")
	assert_eq(stats["grid_members"], 0, "pickups never flock")
	assert_true(factory.debug_assert_no_dangling()["ok"], "consistent")


func test_an_area_clear_removes_flockmates() -> void:
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(10, 0)), Transform2D(0.0, Vector2(0, 30)), Transform2D(0.0, Vector2(300, 0))]
	var v := _flock_volley(tr)
	_step(1)
	assert_eq(factory.debug_get_flock_stats()["grid_members"], 4, "four in the grid")
	factory.clear_bullets_in_circle(Vector2(0, 0), 15.0)
	_step(1)
	assert_eq(factory.debug_get_flock_stats()["grid_members"], 2, "the cleared pair left the flock")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_randomized_speeds_stay_owned_by_the_spawner() -> void:
	var sp := make_spawner(H.make_volley_data(8, 100.0, 1000.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8)
	sp.helper_ring_radius = 30.0
	sp.bullet_speed_scale_min = 0.5
	sp.bullet_speed_scale_max = 2.0
	sp.bullet_random_seed = 5
	sp.flock_enabled = true
	sp.flock = BulletFlock2D.new()
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v))
	sp.shoot_once()
	var v: BulletVolley2D = got[0]
	var speeds: Array = []
	for i in 8:
		speeds.append(v.get_bullet_velocity(i).length())
	assert_gt(speeds.max() - speeds.min(), 10.0, "the speeds really differ")
	_step(1)
	assert_true(_steered(v), "the flock steers the ring")
	_step(59)
	for i in 8:
		assert_almost_eq(v.get_bullet_velocity(i).length(), speeds[i], 0.01, "bullet %d keeps its randomized speed" % i)
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_two_factories_never_share_a_flock() -> void:
	var other := BulletFactory2D.new()
	add(other)
	await idle(1)
	var f := BulletFlock2D.new()
	var tr := _cluster(8)
	var here := _flock_volley(tr, f)
	var there: BulletVolley2D = other.spawn_volley(H.make_flight_data(tr, 120.0, 1000.0))
	there.flock_join(f)
	var solo := _flock_volley(tr, BulletFlock2D.new())
	solo.global_position = Vector2.ZERO
	for k in 30:
		factory.debug_advance_time(DT)
		other.debug_advance_time(DT)
	for i in 8:
		assert_eq(there.get_bullet_transform(i), solo.get_bullet_transform(i), "bullet %d: the other factory's flock flies exactly like a lone one" % i)
	assert_eq(other.debug_get_flock_stats()["flocks"], 1, "one flock over there")
	other.free_active_bullets()


func test_a_paused_factory_turns_nothing() -> void:
	var v := _flock_volley(_cluster(6), H.make_flock(0.0, 0.0, 5.0))
	_step(2)
	var headings: Array = []
	for i in 6:
		headings.append(v.get_bullet_direction(i))
	factory.set_is_factory_processing_bullets(false)
	for k in 5:
		factory.debug_advance_time(DT)
	for i in 6:
		assert_eq(v.get_bullet_direction(i), headings[i], "bullet %d holds while paused" % i)
	factory.set_is_factory_processing_bullets(true)
	_step(1)
	assert_true(_steered(v), "flocking resumes")
	assert_eq(_broken(v), [], "finite, unit, consistent")


func test_a_reset_from_a_handler_leaves_nothing_armed() -> void:
	var trigger_data := H.make_flight_data([Transform2D(0.0, Vector2(-500, -500))], 0.0, 0.05)
	trigger_data.is_life_time_over_signal_enabled = true
	var trigger: BulletVolley2D = factory.spawn_volley(trigger_data)
	for k in 3:
		_flock_volley(_cluster(5, Vector2(k * 100, 0), k))
	var fired := [false]
	factory.life_time_over.connect(func(v: BulletVolley2D, _idx: Array) -> void:
		if v == trigger:
			fired[0] = true
			factory.reset_deferred()
	)
	for tick in 10:
		_step(1)
		if fired[0]:
			break
	await idle(2)
	assert_true(fired[0], "the handler ran")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "the deferred reset disarmed every flock")
	assert_true(factory.debug_assert_no_dangling()["ok"], "consistent")
