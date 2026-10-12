extends BlastTest
## How a pickup moves: the built-in burst and magnet, the bullet's own motion
## kept as it was, or the developer's BulletSpeedData2D / BulletCurvesData2D
## (CUSTOM), with optional BulletRotationData2D in every mode; the pull delay;
## per-pickup random scale and rotation; mode edits reaching the next
## conversion only.

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


## A volley of `n` bullets at the origin moving +x with explicit speed data.
func _moving(n: int, speed: float, max_speed: float, accel: float) -> BulletVolley2D:
	var data := H.make_volley_data(n, speed, 60.0)
	for s in data.all_bullet_speed_data:
		s.max_speed = max_speed
		s.acceleration = accel
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(i, 0)))
	data.transforms = arr
	return factory.spawn_volley(data)


func _wait(n: int, frames := 400) -> int:
	var f := 0
	while collected.size() < n and f < frames:
		await idle(1)
		f += 1
	return f


# --- the three modes -------------------------------------------------------------------------------------

func test_mode_ids_are_stable_and_validated() -> void:
	assert_eq(BulletPickupData2D.MOTION_BURST_AND_MAGNET, 0, "serialized id")
	assert_eq(BulletPickupData2D.MOTION_KEEP_BULLET_MOTION, 1, "serialized id")
	assert_eq(BulletPickupData2D.MOTION_CUSTOM, 2, "serialized id")
	var p := BulletPickupData2D.new()
	assert_eq(p.motion_mode, 0, "the built-in coin physics by default")
	p.motion_mode = 3
	expect_error_sequence(["BulletPickupData2D: motion_mode must be 0 (Burst And Magnet), 1 (Keep Bullet Motion) or 2 (Custom), keeping the old value."])
	p.motion_mode = -1
	expect_error_sequence(["BulletPickupData2D: motion_mode must be 0 (Burst And Magnet), 1 (Keep Bullet Motion) or 2 (Custom), keeping the old value."])
	assert_eq(p.motion_mode, 0, "kept")


func test_burst_and_magnet_decays_then_accelerates() -> void:
	_moving(1, 400.0, 400.0, 0.0)
	var coin := P.coin()
	coin.initial_velocity_scale = 1.0
	coin.drag = 6.0
	coin.magnet_delay_sec = 0.5
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 4000)))[0]
	var s0 := pv.get_bullet_velocity(0).length()
	await idle(20)
	assert_lt(pv.get_bullet_velocity(0).length(), s0 * 0.7, "drag eats the burst")
	await idle(60)
	assert_gt(pv.get_bullet_velocity(0).y, 100.0, "the magnet pulls it towards the target")


func test_keep_bullet_motion_keeps_the_speed_the_bullets_had() -> void:
	_moving(2, 120.0, 260.0, 80.0)
	await idle(1) # the bullets have accelerated a little already
	var coin := P.coin()
	coin.motion_mode = BulletPickupData2D.MOTION_KEEP_BULLET_MOTION
	coin.initial_velocity_scale = 0.01 # ignored in this mode
	coin.drag = 50.0 # ignored too
	coin.magnet_delay_sec = 5.0
	var before := factory.get_active_bullet_count()
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 9000)))[0]
	assert_eq(before, 2, "two bullets")
	var s0 := pv.get_bullet_velocity(0).length()
	assert_gt(s0, 120.0, "the pickup starts at the speed the bullet had reached, not 1%")
	await idle(30)
	assert_gt(pv.get_bullet_velocity(0).length(), s0, "it keeps accelerating like the bullet did")
	await idle(120)
	assert_almost_eq(pv.get_bullet_velocity(0).length(), 260.0, 1.0, "and tops out at the bullet's own top speed")


func test_keep_mode_still_gets_pulled_to_the_target() -> void:
	_moving(1, 200.0, 200.0, 0.0)
	var coin := P.coin()
	coin.motion_mode = BulletPickupData2D.MOTION_KEEP_BULLET_MOTION
	coin.magnet_delay_sec = 0.05
	coin.magnet_smoothing = 0.0
	var target := _target(Vector2(0, 300))
	factory.convert_all_bullets_to_pickups(coin, target)
	var frames := await _wait(1)
	assert_eq(collected.size(), 1, "steered from +x round to the target above and collected it (%d frames)" % frames)


func test_custom_mode_uses_the_developers_speed_data() -> void:
	_moving(2, 33.0, 33.0, 0.0)
	var coin := P.coin()
	coin.motion_mode = BulletPickupData2D.MOTION_CUSTOM
	coin.speed_data = H.make_speed(500.0, 500.0, 0.0)
	coin.magnet_delay_sec = 0.0
	var target := _target(Vector2(500, 0))
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, target)[0]
	assert_almost_eq(pv.get_bullet_velocity(0).length(), 500.0, 0.5, "the authored speed, not the bullet's")
	var frames := await _wait(2)
	assert_eq(collected.size(), 2, "collected after about a second (%d frames)" % frames)
	assert_lt(frames, 90, "at 500 px/s over ~500 px")


func test_custom_mode_speed_data_may_accelerate() -> void:
	_moving(1, 10.0, 10.0, 0.0)
	var coin := P.coin()
	coin.motion_mode = BulletPickupData2D.MOTION_CUSTOM
	coin.speed_data = H.make_speed(50.0, 800.0, 400.0)
	coin.magnet_delay_sec = 0.0
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(9000, 0)))[0]
	var s0 := pv.get_bullet_velocity(0).length()
	await idle(30)
	assert_gt(pv.get_bullet_velocity(0).length(), s0 + 100.0, "accelerates as authored")
	await idle(120)
	assert_almost_eq(pv.get_bullet_velocity(0).length(), 800.0, 1.0, "up to the authored top speed")


func test_custom_mode_without_data_stands_still_and_expires_cleanly() -> void:
	_moving(1, 100.0, 100.0, 0.0)
	var coin := P.coin()
	coin.motion_mode = BulletPickupData2D.MOTION_CUSTOM
	coin.lifetime_sec = 0.5
	coin.fade_out_sec = 0.0
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(300, 0)))[0]
	var at := pv.get_bullet_transform(0).origin
	await idle(20)
	assert_eq(pv.get_bullet_transform(0).origin, at, "no speed data means no speed: it stays")
	await idle(30)
	assert_eq(factory.get_active_bullet_count(), 0, "and expires by its lifetime")
	assert_eq(collected.size(), 0, "never collected")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "clean")


func test_custom_mode_with_a_speed_curve() -> void:
	_moving(1, 5.0, 5.0, 0.0)
	var coin := P.coin()
	coin.motion_mode = BulletPickupData2D.MOTION_CUSTOM
	var curves := BulletCurvesData2D.new()
	curves.movement_speed_curve = H.make_flat_curve(240.0) # x = normalized pickup lifetime
	coin.curves_data = curves
	coin.magnet_delay_sec = 0.0
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(9000, 0)))[0]
	await idle(5)
	assert_almost_eq(pv.get_bullet_velocity(0).length(), 240.0, 0.5, "the curve drives the speed")


func test_the_pull_delay_applies_in_every_mode() -> void:
	for mode in [BulletPickupData2D.MOTION_BURST_AND_MAGNET, BulletPickupData2D.MOTION_KEEP_BULLET_MOTION, BulletPickupData2D.MOTION_CUSTOM]:
		_moving(1, 100.0, 100.0, 0.0)
		var coin := P.coin()
		coin.motion_mode = mode
		coin.speed_data = H.make_speed(100.0, 100.0, 0.0)
		coin.magnet_delay_sec = 0.5
		coin.initial_velocity_scale = 1.0
		coin.drag = 0.0
		var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 5000)))[0]
		await idle(15)
		assert_almost_eq(pv.get_bullet_velocity(0).y, 0.0, 0.01, "mode %d: before the delay it flies straight" % mode)
		await idle(40)
		assert_gt(pv.get_bullet_velocity(0).y, 1.0, "mode %d: after the delay it turns to the target" % mode)
		pv.clear_all_bullets()
		await idle(1)


func test_rotation_data_spins_the_pickup_in_every_mode() -> void:
	for mode in [BulletPickupData2D.MOTION_BURST_AND_MAGNET, BulletPickupData2D.MOTION_KEEP_BULLET_MOTION]:
		_moving(1, 50.0, 50.0, 0.0)
		var coin := P.coin()
		coin.motion_mode = mode
		coin.rotation_data = H.make_rotation(3.0, 100.0, 0.0)
		coin.magnet_delay_sec = 5.0
		var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 5000)))[0]
		var r0 := pv.get_bullet_transform(0).get_rotation()
		await idle(20)
		var turned := absf(wrapf(pv.get_bullet_transform(0).get_rotation() - r0, -PI, PI))
		assert_gt(turned, 0.5, "mode %d: the rotation data spins it" % mode)
		pv.clear_all_bullets()
		await idle(1)


func test_a_spinning_pickup_is_still_pulled_to_the_target_and_collected() -> void:
	_moving(1, 100.0, 100.0, 0.0)
	var coin := P.coin()
	coin.rotation_data = H.make_rotation(6.0, 100.0, 0.0)
	factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 300)))
	var frames := await _wait(1)
	assert_eq(collected.size(), 1, "spin and pull combine (%d frames)" % frames)


# --- randomization ---------------------------------------------------------------------------------------------

func test_random_scale_and_rotation_stay_in_range_and_pairs_never_reject() -> void:
	var pts: Array = []
	for i in 200:
		pts.append(Vector2(i, 0))
	P.still(factory, pts)
	var coin := P.coin()
	coin.scale_random_min = 3.0 # above the current max of 1.0: accepted
	assert_eq(coin.scale_random_min, 3.0, "no pair rejection")
	coin.scale_random_max = 1.0
	coin.rotation_random_min_degrees = 40.0
	coin.rotation_random_max_degrees = -40.0
	coin.magnet_delay_sec = 5.0
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 5000)))[0]
	var lo := INF
	var hi := -INF
	var rlo := INF
	var rhi := -INF
	for i in 200:
		var t := pv.get_bullet_transform(i)
		lo = minf(lo, t.x.length())
		hi = maxf(hi, t.x.length())
		var deg := rad_to_deg(wrapf(t.get_rotation(), -PI, PI))
		rlo = minf(rlo, deg)
		rhi = maxf(rhi, deg)
	assert_gte(lo, 1.0 - 0.001, "scale never below the smaller end")
	assert_lte(hi, 3.0 + 0.001, "never above the larger end")
	assert_gt(hi - lo, 0.5, "and varies")
	assert_gte(rlo, -40.0 - 0.1, "rotation low end")
	assert_lte(rhi, 40.0 + 0.1, "rotation high end")
	assert_gt(rhi - rlo, 20.0, "rotation varies")


func test_randomization_off_draws_nothing_from_the_global_rng() -> void:
	P.still(factory, [Vector2.ZERO, Vector2(5, 0)])
	seed(11)
	var expected := randf()
	seed(11)
	factory.convert_all_bullets_to_pickups(P.coin(), _target(Vector2(0, 5000)))
	assert_eq(randf(), expected, "no scale, rotation or scatter rolls were drawn")


func test_random_rolls_follow_the_global_seed() -> void:
	var coin := P.coin()
	coin.scale_random_min = 0.5
	coin.scale_random_max = 2.0
	coin.magnet_delay_sec = 5.0
	var runs: Array = []
	for r in 2:
		P.still(factory, [Vector2.ZERO, Vector2(5, 0), Vector2(10, 0)])
		seed(4)
		var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 5000)))[0]
		runs.append([pv.get_bullet_transform(0).x.length(), pv.get_bullet_transform(1).x.length(), pv.get_bullet_transform(2).x.length()])
		pv.clear_all_bullets()
		await idle(1)
	assert_eq(runs[0], runs[1], "seed(4) reproduces the rolls")


# --- edits -------------------------------------------------------------------------------------------------------

func test_a_mode_edit_reaches_the_next_conversion_not_the_flying_pickups() -> void:
	_moving(1, 100.0, 100.0, 0.0)
	var coin := P.coin()
	coin.motion_mode = BulletPickupData2D.MOTION_KEEP_BULLET_MOTION
	coin.magnet_delay_sec = 5.0
	var first: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 5000)))[0]
	coin.motion_mode = BulletPickupData2D.MOTION_BURST_AND_MAGNET
	coin.drag = 20.0
	_moving(1, 100.0, 100.0, 0.0)
	var second: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 5000)))[0]
	await idle(30)
	assert_almost_eq(first.get_bullet_velocity(0).length(), 100.0, 0.5, "the pickup made before the edit keeps flying at the kept speed")
	assert_lt(second.get_bullet_velocity(0).length(), 60.0, "the one made after the edit drags")
