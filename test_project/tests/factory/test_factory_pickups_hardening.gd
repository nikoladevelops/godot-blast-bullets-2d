extends BlastTest
## Pickups and despawn bounds, adversarial cases: touching must never silently
## kill a pickup, non-finite numbers from huge scale settings never reach the
## physics server, hit-stop, pre-populated pools, source attachments, steady
## memory over many cycles, re-entrancy from the new signals, stale homing
## state of the source bullets, and the despawn-bounds opt-out.

const P := preload("res://tests/common/pickup_test_helpers.gd")

var collected: Array = []


func _on_collected(target: Variant, position: Vector2, volley: BulletVolley2D, index: int, value: Variant) -> void:
	collected.append({ "target": target, "position": position, "volley": volley, "index": index, "value": value })


func before_each() -> void:
	await super()
	collected.clear()
	factory.pickup_collected.connect(_on_collected)


func _target(pos: Vector2) -> Node2D:
	var n := Node2D.new()
	n.position = pos
	add(n)
	return n


func _wait(n: int, frames := 400) -> int:
	var f := 0
	while collected.size() < n and f < frames:
		await idle(1)
		f += 1
	return f


# --- touching never kills ------------------------------------------------------------------------------------

func test_a_pickup_that_touches_something_without_collecting_by_touch_survives() -> void:
	var area := make_area(Vector2(150, 0), Vector2(60, 60), 4)
	P.still(factory, [Vector2.ZERO])
	var coin := P.coin()
	coin.collision_mask = 4
	coin.collision_radius = 12.0
	coin.collect_on_collision = false
	coin.magnet_delay_sec = 0.0
	watch_signals(factory)
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, Vector2(900, 0))[0]
	for i in 200:
		await physics(1)
		if get_signal_emit_count(factory, "area_entered") > 0:
			break
	assert_signal_emitted(factory, "area_entered", "the touch is reported")
	for i in 20:
		await physics(1)
	assert_true(pv.is_bullet_status_enabled(0), "and the pickup is still alive: a touch is not a kill")
	assert_not_null(area)


# --- non-finite numbers ----------------------------------------------------------------------------------------------

func test_overflowing_scale_settings_never_produce_non_finite_poses() -> void:
	P.still(factory, [Vector2(10, 20), Vector2(30, 40)])
	var coin := P.coin()
	coin.scale_multiplier = 1.0e38
	coin.scale_random_min = 1.0e38
	coin.scale_random_max = 1.0e38
	coin.magnet_delay_sec = 5.0
	var out: Array = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 5000)))
	assert_eq(out.size(), 1, "the conversion still happens")
	for i in 2:
		var t: Transform2D = out[0].get_bullet_transform(i)
		assert_true(t.is_finite(), "pose %d is finite" % i)
		assert_true(absf(t.determinant()) > 0.0, "and invertible")
	await idle(5)
	expect_no_errors("the physics server never saw an infinite transform")


func test_overflowing_velocity_settings_keep_the_pickup_moving_sanely() -> void:
	var v := P.still(factory, [Vector2.ZERO])
	v.bullet_set_velocity(0, Vector2(300, 0))
	var coin := P.coin()
	coin.initial_velocity_scale = 1.0e38
	coin.scatter_speed_max = 1.0e38
	coin.magnet_delay_sec = 5.0
	var out: Array = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 5000)))
	var vel: Vector2 = out[0].get_bullet_velocity(0)
	assert_true(vel.is_finite(), "the velocity is finite")
	await idle(5)
	assert_true(out[0].get_bullet_transform(0).is_finite(), "and so is the position after ticks")
	expect_no_errors("nothing logged")


# --- hit-stop and pools -----------------------------------------------------------------------------------------------------

func test_converting_during_a_hit_stop_works_and_the_pickups_fly_after_resume() -> void:
	P.still(factory, [Vector2.ZERO])
	factory.set_is_factory_processing_bullets(false)
	var out: Array = factory.convert_all_bullets_to_pickups(P.coin(), _target(Vector2(250, 0)))
	assert_eq(out.size(), 1, "converted while paused")
	await idle(10)
	assert_eq(collected.size(), 0, "nothing moves during the hit-stop")
	factory.set_is_factory_processing_bullets(true)
	await _wait(1)
	assert_eq(collected.size(), 1, "after the resume it is collected")


func test_a_prepopulated_pool_serves_pickup_conversions_completely() -> void:
	var coin := P.coin()
	coin.sprite_frames = P.frames()
	coin.animation = &"spin"
	coin.attachment = make_probe_scene()
	coin.z_index = 4
	coin.tint = Color(1, 0.5, 0.5, 0.8)
	factory.populate_pickup_pool(coin, 2, 2)
	P.still(factory, [Vector2.ZERO, Vector2(8, 0)]) # takes one pooled volley itself
	factory.debug_reset_pool_stats()
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(300, 0)))[0]
	await idle(2)
	assert_eq(int(factory.debug_get_pool_hit_stats()["hits"]), 1, "the pre-populated volley was used")
	assert_eq(pv.get_sprite_animation(), &"spin", "animation")
	assert_eq(pv.z_index, 4, "z-index")
	assert_almost_eq(pv.self_modulate.a, 0.8, 0.001, "opacity")
	assert_eq(pv.get_amount_active_attachments(), 2, "attachments")
	await _wait(2)
	assert_eq(collected.size(), 2, "and it works end to end")


func test_freeing_and_repopulating_around_pickups_stays_consistent() -> void:
	for round in 3:
		P.still(factory, [Vector2.ZERO, Vector2(5, 0)])
		factory.convert_all_bullets_to_pickups(P.coin(), _target(Vector2(5000, 0)))
		await idle(3)
		factory.free_active_bullets()
		factory.free_bullets_pool()
		await idle(2)
		assert_eq(factory.get_active_bullet_count(), 0, "round %d: cleared" % round)
		assert_true(factory.debug_assert_no_dangling().get("ok", false), "round %d: nothing dangles" % round)


# --- source bullets leave cleanly ----------------------------------------------------------------------------------------------------------

func test_source_attachments_are_released_not_leaked() -> void:
	var scene := make_probe_scene()
	var data := H.make_volley_data(3, 0.0, 60.0)
	data.transforms = [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(10, 0)), Transform2D(0.0, Vector2(20, 0))]
	data.shared_bullet_attachment = scene
	var src := factory.spawn_volley(data)
	await idle(2)
	assert_eq(src.get_amount_active_attachments(), 3, "the source bullets carry attachments")
	factory.convert_all_bullets_to_pickups(P.coin(), _target(Vector2(300, 0)))
	assert_eq(src.get_amount_active_attachments(), 0, "converting releases them to the pool")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "nothing dangles")


func test_stale_homing_of_the_source_bullets_does_not_leak_into_the_pickups() -> void:
	var old_target := _target(Vector2(-600, 0))
	var v := P.still(factory, [Vector2.ZERO], 200.0)
	v.all_bullets_push_back_homing_target(old_target)
	await idle(3)
	var new_target := _target(Vector2(0, 400))
	var coin := P.coin()
	coin.magnet_delay_sec = 0.0
	factory.convert_all_bullets_to_pickups(coin, new_target)
	await _wait(1)
	assert_eq(collected.size(), 1, "collected")
	assert_same(collected[0]["target"], new_target, "by the new target the conversion named")
	assert_lt(collected[0]["position"].distance_to(new_target.position), 40.0, "not dragged towards the old homing target")


func test_steady_object_count_over_many_cycles() -> void:
	var coin := P.coin()
	coin.attachment = make_probe_scene()
	coin.sprite_frames = P.frames()
	coin.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_SPAWN)]
	var target := _target(Vector2(120, 0))
	var counts: Array = []
	for cycle in 14:
		P.still(factory, [Vector2.ZERO, Vector2(6, 0), Vector2(12, 0)])
		factory.convert_all_bullets_to_pickups(coin, target)
		collected.clear()
		await _wait(3)
		await idle(10) # effects finish
		counts.append(int(Performance.get_monitor(Performance.OBJECT_COUNT)))
	assert_lte(counts[-1], counts[4] + 6, "no object growth once the pools are warm: %s" % [counts])


# --- re-entrancy from the new signals ------------------------------------------------------------------------------------------------------------------

func test_a_bullets_converted_handler_may_clear_and_convert_again() -> void:
	P.still(factory, [Vector2.ZERO, Vector2(5, 0)])
	var seen := []
	factory.bullets_converted.connect(func(count, volleys):
		seen.append(count)
		if seen.size() == 1:
			factory.clear_active_bullets() # the pickups just made
			P.still(factory, [Vector2(0, 30)])
			factory.convert_all_bullets_to_pickups(P.coin(), Vector2(500, 0)))
	factory.convert_all_bullets_to_pickups(P.coin(), _target(Vector2(300, 0)))
	assert_eq(seen.size(), 2, "the nested conversion also announced itself")
	assert_eq(seen[0], 2, "first: two bullets")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "clean")


func test_a_despawn_handler_may_convert_the_rest() -> void:
	factory.despawn_bounds_rect = Rect2(0, 0, 300, 300)
	factory.despawn_bounds_enabled = true
	var v := P.still(factory, [Vector2(290, 100), Vector2(100, 100), Vector2(120, 120)])
	v.bullet_set_velocity(0, Vector2(900, 0))
	var made := []
	factory.bullets_despawned.connect(func(_p, _v, _i):
		made.append(factory.convert_all_bullets_to_pickups(P.coin(), _target(Vector2(150, 150)))))
	await idle(1)
	for i in 5:
		factory.debug_advance_time(0.1)
	assert_eq(made.size(), 1, "the handler converted the rest of the screen")
	assert_eq(made[0][0].get_amount_bullets(), 2, "the two bullets still inside")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "clean")


func test_a_deferred_reset_from_a_collected_handler_is_safe() -> void:
	P.still(factory, [Vector2.ZERO, Vector2(5, 0)])
	factory.pickup_collected.connect(func(_t, _p, _v, _i, _val): factory.reset_deferred())
	factory.convert_all_bullets_to_pickups(P.coin(), Vector2(150, 0))
	await _wait(1)
	await idle(5)
	assert_eq(factory.get_active_bullet_count(), 0, "everything was reset")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling state")


# --- despawn bounds opt-out ---------------------------------------------------------------------------------------------------------------------------------

func test_the_pickup_can_ignore_the_despawn_bounds() -> void:
	factory.despawn_bounds_rect = Rect2(0, 0, 200, 200)
	factory.despawn_bounds_enabled = true
	var coin := P.coin()
	coin.motion_mode = BulletPickupData2D.MOTION_KEEP_BULLET_MOTION
	assert_true(coin.obey_despawn_bounds, "obeys the bounds by default")
	coin.obey_despawn_bounds = false
	var v := P.still(factory, [Vector2(150, 100)])
	v.bullet_set_velocity(0, Vector2(900, 0))
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, Vector2(5000, 100))[0]
	await idle(1)
	for i in 8:
		factory.debug_advance_time(0.1)
	assert_true(pv.is_bullet_status_enabled(0), "it flew far outside the bounds and lives")
	coin.obey_despawn_bounds = true
	var v2 := P.still(factory, [Vector2(150, 100)])
	v2.bullet_set_velocity(0, Vector2(900, 0))
	var pv2: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, Vector2(5000, 100))[0]
	await idle(1)
	for i in 8:
		factory.debug_advance_time(0.1)
	assert_false(pv2.is_bullet_status_enabled(0), "with the default it is culled like any bullet")


func test_a_volley_can_ignore_the_despawn_bounds() -> void:
	factory.despawn_bounds_rect = Rect2(0, 0, 200, 200)
	factory.despawn_bounds_enabled = true
	var data := H.make_volley_data(1, 0.0, 30.0)
	data.transforms = [Transform2D(0.0, Vector2(150, 100))]
	assert_false(data.ignore_despawn_bounds, "off by default")
	data.ignore_despawn_bounds = true
	var v := factory.spawn_volley(data)
	v.bullet_set_velocity(0, Vector2(900, 0))
	await idle(1)
	for i in 8:
		factory.debug_advance_time(0.1)
	assert_true(v.is_bullet_status_enabled(0), "bounds do not touch a volley that opted out")
	v.clear_all_bullets()
	await idle(1)
	data.ignore_despawn_bounds = false
	var again := factory.spawn_volley(data)
	assert_same(again, v, "pooled volley reused")
	again.bullet_set_velocity(0, Vector2(900, 0))
	await idle(1)
	for i in 8:
		factory.debug_advance_time(0.1)
	assert_false(again.is_bullet_status_enabled(0), "the opt-out did not leak through the pool")


# --- telegraphs and bursts outside the bounds ----------------------------------------------------------------------------------------------------------------------

func test_a_spawner_outside_the_bounds_does_not_telegraph() -> void:
	factory.despawn_bounds_rect = Rect2(0, 0, 400, 300)
	factory.despawn_bounds_enabled = true
	var sp := make_spawner(H.make_volley_data(2, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.position = Vector2(900, 100)
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.2
	sp.shoot_interval_sec = 0.1
	watch_signals(sp)
	sp.set_shooting_enabled(true)
	await idle(60)
	assert_signal_not_emitted(sp, "volley_telegraphed", "no warning for a shot that cannot happen")
	assert_signal_not_emitted(sp, "volley_fired", "and no shot")
	assert_gt(get_signal_emit_count(sp, "volley_skipped"), 0, "the skips are reported")
	sp.position = Vector2(100, 100)
	await idle(60)
	assert_signal_emitted(sp, "volley_telegraphed", "back inside it warns")
	assert_signal_emitted(sp, "volley_fired", "and shoots")


func test_populate_pickup_pool_validates_and_has_a_deferred_twin() -> void:
	factory.populate_pickup_pool(null, 2, 1)
	expect_error_sequence(["BulletFactory2D.populate_pickup_pool: pickup must be a BulletPickupData2D, nothing populated."])
	factory.populate_pickup_pool(P.coin(), 0, 1)
	expect_error_sequence(["BulletFactory2D.populate_pickup_pool: bullet_count must be between 1 and 20000, nothing populated."])
	factory.populate_pickup_pool_deferred(P.coin(), 3, 2)
	await idle(2)
	P.still(factory, [Vector2.ZERO, Vector2(5, 0), Vector2(10, 0)]) # takes one of the two
	factory.debug_reset_pool_stats()
	factory.convert_all_bullets_to_pickups(P.coin(), Vector2(5000, 0))
	assert_eq(int(factory.debug_get_pool_hit_stats()["hits"]), 1, "the deferred pre-population served a three-bullet conversion")


# --- tunnelling: a fast mover cannot skip its target --------------------------------------------------------------------------

func test_a_very_fast_pickup_with_a_tiny_radius_is_still_collected() -> void:
	P.still(factory, [Vector2.ZERO])
	var coin := P.coin()
	coin.motion_mode = BulletPickupData2D.MOTION_CUSTOM
	coin.speed_data = H.make_speed(30000.0, 30000.0, 0.0) # 500 px per tick
	coin.magnet_delay_sec = 0.0
	coin.collect_radius = 3.0
	factory.convert_all_bullets_to_pickups(coin, Vector2(400, 0))
	var frames := await _wait(1, 60)
	assert_eq(collected.size(), 1, "the swept reach test catches it (%d frames)" % frames)


func test_a_fast_homing_bullet_reports_reaching_its_target_exactly_once() -> void:
	var data := H.make_volley_data(1, 30000.0, 30.0)
	data.transforms = [Transform2D(0.0, Vector2.ZERO)]
	data.homing_distance_before_reached = 3.0
	var v := factory.spawn_volley(data)
	var reached := []
	v.bullet_homing_target_reached.connect(func(_v, i, _t, _p): reached.append(i))
	v.bullet_homing_push_back_homing_target(0, Vector2(400, 0))
	for i in 10:
		await idle(1)
	assert_eq(reached, [0], "a bullet that flies through the target in one tick still reached it, once")


func test_a_slow_bullet_that_stays_outside_the_radius_does_not_report() -> void:
	var data := H.make_volley_data(1, 100.0, 30.0)
	data.transforms = [Transform2D(0.0, Vector2.ZERO)]
	data.homing_distance_before_reached = 3.0
	var v := factory.spawn_volley(data)
	var reached := []
	v.bullet_homing_target_reached.connect(func(_v, i, _t, _p): reached.append(i))
	v.bullet_homing_push_back_homing_target(0, Vector2(400, 50)) # passes 50 px beside the point it heads for
	for i in 20:
		await idle(1)
	assert_eq(reached, [], "no false report while it is far away")


func test_a_big_group_assigns_every_pickup_its_true_nearest_member() -> void:
	var members: Array = []
	for k in 120:
		var m := _target(Vector2(300 + 220 * cos(k * TAU / 120.0), 300 + 220 * sin(k * TAU / 120.0)))
		m.add_to_group("many")
		members.append(m)
	var points: Array = []
	var rnd := RandomNumberGenerator.new()
	rnd.seed = 7
	for i in 600:
		points.append(Vector2(rnd.randf_range(0, 600), rnd.randf_range(0, 600)))
	P.still(factory, points)
	var coin := P.coin()
	coin.motion_mode = BulletPickupData2D.MOTION_KEEP_BULLET_MOTION
	coin.magnet_delay_sec = 0.0
	coin.collect_radius = 12.0
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, "many")[0]
	# Expected owner of each pickup: the brute-force nearest member.
	var expected: Array = []
	for i in 600:
		var at: Vector2 = pv.get_bullet_transform(i).origin
		var best: Node2D = members[0]
		for m in members:
			if at.distance_squared_to(m.position) < at.distance_squared_to(best.position):
				best = m
		expected.append(best)
	assert_eq(pv.get_amount_bullets(), 600, "one volley of 600")
	for i in 600:
		assert_eq(int(pv.bullet_check_has_homing_targets(i)), 1, "pickup %d has its own target" % i)
		assert_same(pv.bullet_get_current_homing_target(i), expected[i], "pickup %d goes to its nearest member" % i)
