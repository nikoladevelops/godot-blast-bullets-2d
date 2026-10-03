extends BlastTest
## Spawner homing/orbit settings reach every volley it fires (and every
## retarget pass), the homing signals fire with exact counts and payloads,
## the fire arc follows spin and reuses the targets it approved, and
## orbiting without homing warns once and stays silent otherwise.

var target: Node2D


func before_each() -> void:
	await super()
	target = Node2D.new()
	target.position = Vector2(400, 0)
	add(target)


func _homing_spawner(amount: int = 3, speed: float = 50.0) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(amount, speed, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, amount)
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	sp.set_homing_target_path(sp.get_path_to(target))
	watch_signals(sp)
	return sp


func test_steering_settings_reach_the_volley() -> void:
	var sp := _homing_spawner()
	sp.set_homing_smoothing(7.5)
	sp.set_homing_update_interval(0.2)
	sp.set_homing_distance_before_reached(12.0)
	sp.set_homing_take_control_of_texture_rotation(false)
	sp.set_homing_delay_sec(0.3)
	sp.set_homing_duration_sec(2.0)
	sp.set_homing_lose_range_px(900.0)
	assert_true(sp.shoot_once(), "shot")
	var v: BulletVolley2D = sp.get_live_volleys()[0]
	assert_almost_eq(v.get_homing_smoothing(), 7.5, 0.0001, "smoothing")
	assert_almost_eq(v.get_homing_update_interval(), 0.2, 0.0001, "update interval")
	assert_almost_eq(v.get_homing_distance_before_reached(), 12.0, 0.0001, "reached distance")
	assert_false(v.get_homing_take_control_of_texture_rotation(), "texture control")
	assert_almost_eq(v.get_homing_delay_sec(), 0.3, 0.0001, "delay")
	assert_almost_eq(v.get_homing_duration_sec(), 2.0, 0.0001, "duration")
	assert_almost_eq(v.get_homing_lose_range_px(), 900.0, 0.0001, "lose range")


func test_per_bullet_smoothing_fans_out() -> void:
	var sp := _homing_spawner(4)
	sp.set_homing_mode(BulletSpawner2D.HOMING_PER_BULLET)
	sp.set_homing_per_bullet_smoothing_enabled(true)
	sp.set_homing_smoothing_start(5.0)
	sp.set_homing_smoothing_step(1.5)
	assert_true(sp.shoot_once(), "shot")
	var v: BulletVolley2D = sp.get_live_volleys()[0]
	for i in 4:
		assert_almost_eq(v.bullet_get_homing_smoothing(i), 5.0 + 1.5 * i, 0.0001, "bullet %d smoothing" % i)


func test_homing_signals_fire_once_per_volley_with_payloads() -> void:
	var sp := _homing_spawner()
	assert_true(sp.shoot_once(), "shot")
	assert_signal_emit_count(sp, "homing_targets_resolved", 1, "targets resolved once")
	assert_eq(get_signal_parameters(sp, "homing_targets_resolved", 0)[1], [target], "with the resolved targets")
	assert_signal_emit_count(sp, "volley_homing_configured", 1, "configured once")
	assert_eq(get_signal_parameters(sp, "volley_homing_configured", 0)[1], 1, "names the volley index")


func test_reaching_the_target_is_forwarded_once_per_bullet() -> void:
	target.position = Vector2(60, 0)
	var sp := _homing_spawner(1, 400.0)
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
	sp.set_homing_distance_before_reached(20.0)
	assert_true(sp.shoot_once(), "shot")
	for i in 60:
		await physics()
		if get_signal_emit_count(sp, "volley_bullet_homing_target_reached") > 0:
			break
	await idle(2)
	assert_signal_emit_count(sp, "volley_bullet_homing_target_reached", 1, "reached once")
	var params: Array = get_signal_parameters(sp, "volley_bullet_homing_target_reached", 0)
	assert_eq(params[1], 0, "bullet index")
	assert_eq(params[2], target, "the target reached")


func test_retarget_pass_counts_and_previous_volleys_switch() -> void:
	var sp := _homing_spawner()
	sp.set_homing_retarget_interval_sec(0.05)
	sp.set_homing_retarget_phase(0.05)
	assert_true(sp.shoot_once(), "volley 1")
	assert_true(sp.shoot_once(), "volley 2")
	for i in 30:
		await idle(1)
		if get_signal_emit_count(sp, "retarget_applied") > 0:
			break
	assert_eq(get_signal_parameters(sp, "retarget_applied", 0)[0], 2, "both flying volleys re-aimed")
	sp.set_homing_retarget_previous_volleys(false)
	assert_eq(sp.retarget_live_volleys(), 1, "only the newest volley when previous volleys are off")


func test_orbit_settings_reach_every_bullet() -> void:
	var sp := _homing_spawner(3)
	sp.set_orbiting_enabled(true)
	sp.set_orbiting_radius_linear_enabled(true)
	sp.set_orbiting_radius_start(40.0)
	sp.set_orbiting_radius_step(10.0)
	sp.set_orbiting_direction(BulletVolley2D.OrbitLeft)
	sp.set_orbiting_follow_mode(BulletVolley2D.FollowDeadzone)
	sp.set_orbiting_follow_deadzone(15.0)
	sp.set_orbiting_lock_policy(BulletVolley2D.StayLocked)
	sp.set_orbiting_rigid_follow(true)
	assert_true(sp.shoot_once(), "shot")
	var v: BulletVolley2D = sp.get_live_volleys()[0]
	for i in 3:
		assert_true(v.bullet_is_orbiting_enabled(i), "bullet %d orbits" % i)
		assert_almost_eq(v.bullet_get_orbiting_radius(i), 40.0 + 10.0 * i, 0.0001, "bullet %d radius fan" % i)
		assert_eq(v.bullet_get_orbiting_direction(i), BulletVolley2D.OrbitLeft, "direction")
		assert_eq(v.bullet_get_orbiting_follow_mode(i), BulletVolley2D.FollowDeadzone, "follow mode")
		assert_almost_eq(v.bullet_get_orbiting_follow_deadzone(i), 15.0, 0.0001, "deadzone")
		assert_eq(v.bullet_get_orbiting_lock_policy(i), BulletVolley2D.StayLocked, "lock policy")
		assert_true(v.bullet_get_orbiting_rigid_follow(i), "rigid follow")


func test_orbiting_without_homing_warns_once_and_stays_silent() -> void:
	var sp := make_spawner(H.make_volley_data(2, 50.0, 30.0))
	sp.set_orbiting_enabled(true)
	watch_signals(sp)
	for i in 3:
		assert_true(sp.shoot_once(), "plain shot %d" % i)
	var warnings := 0
	for err in get_errors():
		if err.is_push_warning() and err.contains_text("orbiting_enabled needs homing_enabled"):
			warnings += 1
	assert_eq(warnings, 1, "one warning, not one per volley")
	assert_signal_emit_count(sp, "homing_targets_resolved", 0, "no homing signals without homing")
	assert_signal_emit_count(sp, "volley_homing_configured", 0, "no homing signals without homing")


func test_fire_arc_follows_spin() -> void:
	var sp := _homing_spawner()
	sp.set_homing_fire_arc_deg(60.0) # +-30 deg around the facing
	target.position = Vector2(0, 400) # 90 deg off the unspun facing (+X)
	assert_false(sp.shoot_once(), "outside the arc while facing +X")
	assert_eq(str(get_signal_parameters(sp, "volley_skipped", 0)[0]), "outside_fire_arc", "reported")
	sp.set_spin_enabled(true)
	sp.set_spin_speed_deg_per_sec(5400.0) # 90 deg per frame
	await idle(1)
	assert_almost_eq(fposmod(sp.get_spin_angle_deg(), 360.0), 90.0, 0.5, "spun a quarter turn")
	sp.set_spin_enabled(false)
	assert_true(sp.shoot_once(), "the spun muzzle now faces the target")


func test_fire_arc_volley_chases_the_target_it_approved() -> void:
	for i in 3:
		var foe := Node2D.new()
		foe.position = Vector2(300, -40 + 40 * i)
		add(foe)
		foe.add_to_group("arc_foes")
	var sp := make_spawner(H.make_volley_data(1, 50.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.set_homing_enabled(true)
	sp.set_homing_node_group(&"arc_foes")
	sp.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_RANDOM)
	sp.set_homing_random_seed(3)
	sp.set_homing_fire_arc_deg(90.0)
	var reference := make_spawner(H.make_volley_data(1, 50.0, 30.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	reference.set_homing_enabled(true)
	reference.set_homing_node_group(&"arc_foes")
	reference.set_homing_target_selection(BulletSpawner2D.HOMING_SELECT_RANDOM)
	reference.set_homing_random_seed(3)
	# 8 rounds: seed 3 happens to draw the same foe for the first four.
	for i in 8:
		var expected: Array = reference.resolve_homing_targets(true)
		assert_true(sp.shoot_once(), "shot %d" % i)
		var v: BulletVolley2D = sp.get_live_volleys()[i]
		assert_eq(v.shared_homing_deque_get_current_homing_target(), expected[0], "shot %d chases the seeded pick (one draw per shot)" % i)
