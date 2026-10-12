extends BlastTest
## Pickups, the hard cases: time scale interplay, conversion from inside
## handlers and callbacks (busy latch), clear effects on the way out,
## determinism of the scatter, moving targets, rotation, two pickup kinds at
## once, and a few thousand bullets.

var collected: Array = []


func _on_collected(target: Variant, position: Vector2, volley: BulletVolley2D, index: int, value: Variant) -> void:
	collected.append({ "target": target, "position": position, "volley": volley, "index": index, "value": value })


func before_each() -> void:
	await super()
	collected.clear()
	factory.pickup_collected.connect(_on_collected)
	AttachmentConvertProbe2D.factory = null
	AttachmentConvertProbe2D.pickup = null
	AttachmentConvertProbe2D.refused_calls = 0
	AttachmentConvertProbe2D.results.clear()


func _coin(value: Variant = 1) -> BulletPickupData2D:
	var p := BulletPickupData2D.new()
	p.magnet_delay_sec = 0.1
	p.magnet_acceleration = 3000.0
	p.magnet_max_speed = 1500.0
	p.drag = 2.0
	p.collect_radius = 20.0
	p.lifetime_sec = 30.0
	p.value = value
	return p


func _target(pos: Vector2) -> Node2D:
	var n := Node2D.new()
	n.position = pos
	add(n)
	return n


func _bullets(points: Array, speed := 0.0, lifetime := 60.0) -> BulletVolley2D:
	var data := H.make_volley_data(points.size(), speed, lifetime)
	var arr: Array = []
	for p in points:
		arr.append(Transform2D(0.0, p))
	data.transforms = arr
	return factory.spawn_volley(data)


## Frames until `n` collections happened (or `limit`).
func _frames_to_collect(n: int, limit := 600) -> int:
	var frames := 0
	while collected.size() < n and frames < limit:
		await idle(1)
		frames += 1
	return frames


# --- time scale ------------------------------------------------------------------------------------

func test_pickups_follow_the_factory_time_scale() -> void:
	_bullets([Vector2(0, 0)])
	factory.convert_all_bullets_to_pickups(_coin(), Vector2(600, 0))
	var normal := await _frames_to_collect(1)
	assert_eq(collected.size(), 1, "collected at real time")
	collected.clear()
	factory.time_scale = 0.5
	_bullets([Vector2(0, 0)])
	factory.convert_all_bullets_to_pickups(_coin(), Vector2(600, 0))
	var slow := await _frames_to_collect(1)
	assert_eq(collected.size(), 1, "collected in slow motion too")
	assert_gt(float(slow), float(normal) * 1.5, "half speed takes clearly longer (%d vs %d frames)" % [slow, normal])
	collected.clear()
	factory.time_scale = 0.0
	_bullets([Vector2(0, 0)])
	factory.convert_all_bullets_to_pickups(_coin(), Vector2(600, 0))
	await idle(60)
	assert_eq(collected.size(), 0, "a frozen world collects nothing")


func test_a_slowed_spawner_does_not_slow_the_pickups_made_from_its_bullets() -> void:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.helper_ring_radius = 0.0
	sp.time_scale = 0.1
	assert_true(sp.shoot_once(), "shot")
	sp.convert_live_bullets_to_pickups(_coin(), Vector2(600, 0))
	var slowed_spawner := await _frames_to_collect(1)
	collected.clear()
	_bullets([Vector2(0, 0)])
	factory.convert_all_bullets_to_pickups(_coin(), Vector2(600, 0))
	var plain := await _frames_to_collect(1)
	assert_lt(absf(float(slowed_spawner) - float(plain)), float(plain) * 0.2 + 2.0, "pickups are factory-owned: %d vs %d frames" % [slowed_spawner, plain])


# --- handlers and callbacks ---------------------------------------------------------------------------

func test_converting_from_a_lifetime_handler_works_directly() -> void:
	var short_data := H.make_volley_data(1, 0.0, 0.1)
	short_data.is_life_time_over_signal_enabled = true
	short_data.transforms = [Transform2D(0.0, Vector2(0, 0))]
	factory.spawn_volley(short_data)
	var long := _bullets([Vector2(50, 0), Vector2(60, 0)], 0.0, 60.0)
	var target := _target(Vector2(400, 0))
	var results := []
	factory.life_time_over.connect(func(_v, _i): results.append(factory.convert_all_bullets_to_pickups(_coin(), target)))
	for i in 20:
		await idle(1)
		if not results.is_empty():
			break
	assert_eq(results.size(), 1, "the handler ran once")
	assert_eq(results[0].size(), 1, "and converted from inside the tick")
	# The expiring bullet is still alive inside its own handler, so it is
	# converted too (a dying enemy bullet can drop a coin); the expiry pass
	# then skips it (its life moved on).
	assert_eq(results[0][0].get_amount_bullets(), 3, "the two long-lived bullets and the expiring one")
	assert_false(long.is_bullet_status_enabled(0) or long.is_bullet_status_enabled(1), "they left their volley")


func test_a_disable_callback_cannot_start_a_second_conversion() -> void:
	var scene := PackedScene.new()
	var probe := AttachmentConvertProbe2D.new()
	assert_eq(scene.pack(probe), OK, "probe packs")
	probe.free()
	var v := _bullets([Vector2(0, 0), Vector2(10, 0)])
	v.bullet_set_attachment(0, scene, Vector2.ZERO, true)
	var coin := _coin()
	AttachmentConvertProbe2D.factory = factory
	AttachmentConvertProbe2D.pickup = coin
	var out: Array = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(300, 0)))
	assert_eq(out.size(), 1, "the outer conversion completed")
	assert_eq(out[0].get_amount_bullets(), 2, "with both bullets")
	assert_eq(AttachmentConvertProbe2D.refused_calls, 1, "the callback tried once")
	expect_error_sequence(["BulletFactory2D.convert_all_bullets_to_pickups: BulletFactory2D is busy (a sweep is running). Convert after it, e.g. from the next frame (convert_all_bullets_to_pickups_deferred queues it), nothing converted."])
	assert_eq(AttachmentConvertProbe2D.results, [[]], "and got an empty result")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "nothing dangles")


func test_a_collected_handler_may_convert_again() -> void:
	_bullets([Vector2(0, 0)])
	var target := _target(Vector2(300, 0))
	var second_wave := []
	factory.pickup_collected.connect(func(_t, _p, _v, _i, value):
		if value == "first":
			_bullets([Vector2(0, 40)])
			second_wave.append(factory.convert_all_bullets_to_pickups(_coin("second"), target)))
	factory.convert_all_bullets_to_pickups(_coin("first"), target)
	await _frames_to_collect(2)
	assert_eq(second_wave.size(), 1, "the handler converted a new wave")
	assert_eq(collected.map(func(c): return c["value"]), ["first", "second"], "both kinds were collected, each with its own value")


# --- clear effects and determinism -------------------------------------------------------------------------

func test_clear_effects_only_with_the_option() -> void:
	var data := H.make_volley_data(2, 0.0, 60.0)
	data.transforms = [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(10, 0))]
	data.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_CLEAR)]
	factory.debug_set_effect_log_enabled(true)
	factory.spawn_volley(data)
	var target := _target(Vector2(300, 0))
	factory.convert_all_bullets_to_pickups(_coin(), target)
	var clears := factory.debug_get_effect_log().filter(func(e): return int(e["trigger"]) == BulletEffectLayerData2D.EFFECT_ON_CLEAR)
	assert_eq(clears.size(), 0, "silent by default")
	factory.debug_clear_effect_log()
	factory.spawn_volley(data)
	var loud := _coin()
	loud.fire_clear_effects = true
	factory.convert_all_bullets_to_pickups(loud, target)
	clears = factory.debug_get_effect_log().filter(func(e): return int(e["trigger"]) == BulletEffectLayerData2D.EFFECT_ON_CLEAR)
	assert_eq(clears.size(), 2, "with fire_clear_effects each converted bullet fires its On Clear layer")


func test_scatter_follows_the_global_seed() -> void:
	var coin := _coin()
	coin.scatter_speed_min = 50.0
	coin.scatter_speed_max = 150.0
	coin.initial_velocity_scale = 0.0
	coin.magnet_delay_sec = 5.0
	var runs: Array = []
	for r in 2:
		_bullets([Vector2(0, 0), Vector2(1, 0), Vector2(2, 0)])
		seed(99)
		var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 900)))[0]
		runs.append([pv.get_bullet_velocity(0), pv.get_bullet_velocity(1), pv.get_bullet_velocity(2)])
		pv.clear_all_bullets()
		await idle(1)
	assert_eq(runs[0], runs[1], "seed(99) reproduces the scatter")
	var no_scatter := _coin()
	seed(5)
	var expected := randf()
	_bullets([Vector2(0, 0)])
	seed(5)
	factory.convert_all_bullets_to_pickups(no_scatter, _target(Vector2(0, 900)))
	assert_eq(randf(), expected, "without scatter the global RNG is not touched")


# --- geometry ------------------------------------------------------------------------------------------------------

func test_a_moving_target_is_followed() -> void:
	_bullets([Vector2(0, 0)])
	var target := _target(Vector2(200, 0))
	var coin := _coin()
	coin.magnet_max_speed = 700.0
	factory.convert_all_bullets_to_pickups(coin, target)
	for i in 400:
		target.position += Vector2(0, 3.0)
		await idle(1)
		if not collected.is_empty():
			break
	assert_eq(collected.size(), 1, "caught up with the moving target")
	assert_lt(collected[0]["position"].distance_to(target.position), 30.0, "collected next to where the target is, not where it was")


func test_rotation_and_position_of_each_source_are_kept() -> void:
	var data := H.make_volley_data(2, 0.0, 60.0)
	data.transforms = [Transform2D(0.7, Vector2(100, 20)), Transform2D(-1.2, Vector2(-30, 40))]
	var v := factory.spawn_volley(data)
	var before := [v.get_bullet_transform(0), v.get_bullet_transform(1)]
	var coin := _coin()
	coin.magnet_delay_sec = 5.0
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 900)))[0]
	var found := 0
	for i in 2:
		for b in before:
			if pv.get_bullet_transform(i).is_equal_approx(b):
				found += 1
	assert_eq(found, 2, "each pickup has its source's pose")


# --- scale --------------------------------------------------------------------------------------------------------

func test_a_couple_of_thousand_bullets_are_converted_and_collected_exactly_once() -> void:
	var pts: Array = []
	for i in 2500:
		pts.append(Vector2(float(i % 50) * 6.0, float(i / 50) * 6.0))
	_bullets(pts)
	var coin := _coin()
	coin.scatter_speed_max = 200.0
	var out: Array = factory.convert_all_bullets_to_pickups(coin, Vector2(150, 150))
	assert_eq(out[0].get_amount_bullets(), 2500, "one volley of 2500")
	var frames := await _frames_to_collect(2500, 900)
	assert_eq(collected.size(), 2500, "every pickup collected (%d frames)" % frames)
	var seen := {}
	for c in collected:
		seen[c["index"]] = true
	assert_eq(seen.size(), 2500, "each index exactly once")
	await idle(2)
	assert_eq(factory.get_active_bullet_count(), 0, "nothing left")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "nothing dangles")


func test_converting_then_resetting_the_factory_midflight_leaves_nothing_behind() -> void:
	_bullets([Vector2(0, 0), Vector2(5, 0), Vector2(10, 0)])
	factory.convert_all_bullets_to_pickups(_coin(), _target(Vector2(5000, 0)))
	await idle(5)
	factory.reset()
	await idle(2)
	assert_eq(factory.get_active_bullet_count(), 0, "reset cleared the pickups")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling state")
	assert_eq(collected.size(), 0, "nothing was collected by the reset")
